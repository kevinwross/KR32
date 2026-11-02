#include "cxx/codegen.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kr32::cxx {

namespace {

using namespace ast;

std::vector<std::string> split_qualified(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find("::", start);
        if (end == std::string_view::npos) {
            if (start < text.size()) {
                parts.emplace_back(text.substr(start));
            }
            break;
        }
        if (end > start) {
            parts.emplace_back(text.substr(start, end - start));
        }
        start = end + 2;
    }
    return parts;
}

std::string mangle_qualified(std::vector<std::string> parts) {
    if (parts.size() == 1) {
        return std::to_string(parts[0].size()) + parts[0];
    }
    std::string result = "N";
    for (const std::string& part : parts) {
        result += std::to_string(part.size()) + part;
    }
    result += "E";
    return result;
}

std::string mangle_type(const TypePtr& type);

std::string mangle_builtin(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Void: return "v";
    case BuiltinKind::Bool: return "b";
    case BuiltinKind::Char: return "c";
    case BuiltinKind::Int: return "i";
    case BuiltinKind::UnsignedInt: return "j";
    case BuiltinKind::Float: return "f";
    case BuiltinKind::Double: return "d";
    case BuiltinKind::LongDouble: return "d";
    case BuiltinKind::Auto: return "i";
    }
    return "i";
}

std::string mangle_type(const TypePtr& type) {
    if (!type) {
        return "v";
    }
    switch (type->kind) {
    case TypeKind::Pointer: {
        std::string result;
        if (type->is_const) {
            result += "K";
        }
        if (type->is_volatile) {
            result += "V";
        }
        result += "P";
        result += mangle_type(type->element);
        return result;
    }
    case TypeKind::LValueRef:
        return "R" + mangle_type(type->element);
    case TypeKind::RValueRef:
        return "O" + mangle_type(type->element);
    case TypeKind::Array:
        return "A" + std::to_string(type->array_size) + "_" + mangle_type(type->element);
    case TypeKind::Function: {
        std::string result = "F" + mangle_type(type->element);
        for (const TypePtr& parameter : type->parameters) {
            result += mangle_type(parameter);
        }
        return result;
    }
    case TypeKind::Builtin: {
        std::string result;
        if (type->is_const) {
            result += "K";
        }
        if (type->is_volatile) {
            result += "V";
        }
        result += mangle_builtin(type->builtin);
        return result;
    }
    case TypeKind::Named: {
        std::string result;
        if (type->is_const) {
            result += "K";
        }
        if (type->is_volatile) {
            result += "V";
        }
        result += mangle_qualified(type->name.parts);
        return result;
    }
    }
    return "i";
}

bool is_integer(const TypePtr& type) {
    return type && type->is_integral();
}

bool is_pointer(const TypePtr& type) {
    return type && (type->kind == TypeKind::Pointer || type->kind == TypeKind::Array);
}

TypePtr decay(const TypePtr& type) {
    if (type && type->kind == TypeKind::Array) {
        return Type::make_pointer(type->element);
    }
    return type;
}

bool same_type(const TypePtr& left, const TypePtr& right) {
    if (!left || !right) {
        return left == right;
    }
    if (left->kind != right->kind) {
        return false;
    }
    switch (left->kind) {
    case TypeKind::Builtin:
        return left->builtin == right->builtin;
    case TypeKind::Named:
        return left->name.to_string() == right->name.to_string();
    case TypeKind::Pointer:
    case TypeKind::LValueRef:
    case TypeKind::RValueRef:
        return same_type(left->element, right->element);
    case TypeKind::Array:
        return left->array_size == right->array_size &&
               same_type(left->element, right->element);
    case TypeKind::Function:
        return same_type(left->element, right->element) &&
               left->parameters.size() == right->parameters.size();
    }
    return false;
}

TypePtr promote(const TypePtr& type) {
    if (type && type->kind == TypeKind::Builtin &&
        (type->builtin == BuiltinKind::Bool || type->builtin == BuiltinKind::Char)) {
        return Type::make_builtin(BuiltinKind::Int);
    }
    return type;
}

bool is_fp(const TypePtr& type) {
    return type && type->kind == TypeKind::Builtin &&
           (type->builtin == BuiltinKind::Float ||
            type->builtin == BuiltinKind::Double ||
            type->builtin == BuiltinKind::LongDouble);
}

bool is_float32(const TypePtr& type) {
    return type && type->kind == TypeKind::Builtin &&
           type->builtin == BuiltinKind::Float;
}

bool is_float64(const TypePtr& type) {
    return type && type->kind == TypeKind::Builtin &&
           (type->builtin == BuiltinKind::Double ||
            type->builtin == BuiltinKind::LongDouble);
}

TypePtr arithmetic_type(const TypePtr& left, const TypePtr& right) {
    if (is_float64(left) || is_float64(right)) {
        return Type::make_builtin(BuiltinKind::Double);
    }
    if (is_float32(left) || is_float32(right)) {
        return Type::make_builtin(BuiltinKind::Float);
    }
    return Type::make_builtin(BuiltinKind::Int);
}

std::string fp_literal_text(double value, bool single) {
    std::ostringstream out;
    out << std::setprecision(single ? 9 : 17) << value;
    std::string text = out.str();
    const bool has_separator = text.find('.') != std::string::npos ||
                               text.find('e') != std::string::npos ||
                               text.find('E') != std::string::npos ||
                               text.find("inf") != std::string::npos ||
                               text.find("nan") != std::string::npos;
    if (!has_separator) {
        text += ".0";
    }
    return text;
}

std::string escape_asm(const std::string& text) {
    std::string result;
    for (const char c : text) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 32) {
                result += "\\x";
                static const char* hex = "0123456789abcdef";
                result.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                result.push_back(hex[static_cast<unsigned char>(c) & 0xF]);
            } else {
                result.push_back(c);
            }
            break;
        }
    }
    return result;
}

} // namespace

std::string mangle_variable(std::string_view qualified_name, bool extern_c) {
    if (extern_c) {
        return std::string(qualified_name);
    }
    const std::vector<std::string> parts = split_qualified(qualified_name);
    return "_Z" + mangle_qualified(parts);
}

std::string mangle_function(std::string_view qualified_name,
                            const std::vector<TypePtr>& parameter_types, bool extern_c,
                            bool variadic) {
    if (extern_c) {
        return std::string(qualified_name);
    }
    const std::vector<std::string> parts = split_qualified(qualified_name);
    if (parts.size() == 1 && parts[0] == "main") {
        return "main";
    }
    std::string result = "_Z" + mangle_qualified(parts);
    if (parameter_types.empty()) {
        result += "v";
    } else {
        for (const TypePtr& parameter : parameter_types) {
            result += mangle_type(decay(parameter));
        }
    }
    if (variadic) {
        result += "z";
    }
    return result;
}

namespace {

class Generator {
public:
    explicit Generator(std::vector<Diagnostic>& diagnostics)
        : diagnostics_(diagnostics) {
        type_aliases_["size_t"] = Type::make_builtin(BuiltinKind::UnsignedInt);
        type_aliases_["va_list"] =
            Type::make_pointer(Type::make_builtin(BuiltinKind::Char));
        type_aliases_["ptrdiff_t"] = Type::make_builtin(BuiltinKind::Int);
        type_aliases_["int8_t"] = Type::make_builtin(BuiltinKind::Char);
        type_aliases_["int16_t"] = Type::make_builtin(BuiltinKind::Int);
        type_aliases_["int32_t"] = Type::make_builtin(BuiltinKind::Int);
        type_aliases_["uint8_t"] = Type::make_builtin(BuiltinKind::Char);
        type_aliases_["uint16_t"] = Type::make_builtin(BuiltinKind::UnsignedInt);
        type_aliases_["uint32_t"] = Type::make_builtin(BuiltinKind::UnsignedInt);
    }

    std::string run(const TranslationUnit& unit) {
        collect(unit.declarations, "");
        if (!diagnostics_.empty()) {
            return {};
        }
        for (const auto& function : function_storage_) {
            if (function->defined) {
                emit_function(*function);
            }
        }
        emit_globals();
        std::string result = text_;
        if (!rodata_.empty()) {
            result += ".rodata\n" + rodata_;
        }
        if (!data_.empty()) {
            result += ".data\n" + data_;
        }
        if (!bss_.empty()) {
            result += ".bss\n" + bss_;
        }
        return result;
    }

private:
    struct Function {
        const Decl* decl = nullptr;
        std::string qualified;
        std::string symbol;
        std::vector<TypePtr> parameters;
        TypePtr result;
        bool extern_c = false;
        bool variadic = false;
        bool defined = false;
        bool is_static = false;
    };

    struct Global {
        const Decl* decl = nullptr;
        std::string qualified;
        std::string symbol;
        TypePtr type;
        bool extern_c = false;
        bool defined = false;
        bool is_static = false;
        const Expr* initializer = nullptr;
    };

    struct Local {
        TypePtr type;
        int offset = 0;
        bool is_reference = false;
        bool is_parameter = false;
    };

    void error(const Location& location, std::string message) {
        diagnostics_.push_back({location.line, location.column, std::move(message)});
    }

    void error(const Decl& declaration, std::string message) {
        error(declaration.location, std::move(message));
    }

    std::string qualify(const std::string& prefix, const Name& name) const {
        if (name.parts.empty()) {
            return prefix;
        }
        std::vector<std::string> parts = name.parts;
        if (name.parts.size() > 1) {
            return name.to_string();
        }
        if (prefix.empty()) {
            return parts[0];
        }
        return prefix + "::" + parts[0];
    }

    TypePtr resolve_type(TypePtr type) {
        if (!type) {
            return type;
        }
        if (type->kind == TypeKind::Named) {
            const auto alias = type_aliases_.find(type->name.to_string());
            if (alias != type_aliases_.end()) {
                TypePtr resolved = alias->second->clone();
                resolved->is_const = resolved->is_const || type->is_const;
                resolved->is_volatile = resolved->is_volatile || type->is_volatile;
                return resolved;
            }
            return type;
        }
        if (type->kind == TypeKind::Pointer || type->kind == TypeKind::LValueRef ||
            type->kind == TypeKind::RValueRef || type->kind == TypeKind::Array) {
            auto copy = std::make_shared<Type>(*type);
            copy->element = resolve_type(type->element);
            return copy;
        }
        return type;
    }

    int type_size(const TypePtr& raw) {
        const TypePtr type = resolve_type(raw);
        if (!type) {
            return 4;
        }
        switch (type->kind) {
        case TypeKind::Builtin:
            switch (type->builtin) {
            case BuiltinKind::Void: return 0;
            case BuiltinKind::Bool:
            case BuiltinKind::Char: return 1;
            case BuiltinKind::Int:
            case BuiltinKind::UnsignedInt:
            case BuiltinKind::Float: return 4;
            case BuiltinKind::Double:
            case BuiltinKind::LongDouble: return 8;
            case BuiltinKind::Auto: return 4;
            }
            return 4;
        case TypeKind::Pointer:
        case TypeKind::LValueRef:
        case TypeKind::RValueRef: return 4;
        case TypeKind::Array: {
            const int element = type_size(type->element);
            return element * static_cast<int>(type->array_size);
        }
        case TypeKind::Named: return 4;
        case TypeKind::Function: return 4;
        }
        return 4;
    }

    int type_align(const TypePtr& type) {
        const int size = type_size(type);
        if (size >= 8) {
            return 8;
        }
        return 4;
    }

    void collect(const std::vector<DeclPtr>& declarations, const std::string& prefix) {
        for (const DeclPtr& declaration : declarations) {
            if (!declaration) {
                continue;
            }
            switch (declaration->kind) {
            case DeclKind::Namespace: {
                std::string nested = prefix;
                if (!declaration->name.parts.empty()) {
                    for (const std::string& part : declaration->name.parts) {
                        if (!nested.empty()) {
                            nested += "::";
                        }
                        nested += part;
                    }
                } else {
                    if (!nested.empty()) {
                        nested += "::";
                    }
                    nested += "$anon" + std::to_string(anonymous_counter_++);
                }
                collect(declaration->children, nested);
                break;
            }
            case DeclKind::Function:
                collect_function(*declaration, prefix);
                break;
            case DeclKind::Variable:
                collect_global(*declaration, prefix);
                break;
            case DeclKind::TypeAlias:
                if (!declaration->name.parts.empty()) {
                    type_aliases_[qualify(prefix, declaration->name)] =
                        resolve_type(declaration->type);
                }
                break;
            case DeclKind::Using:
                collect_using(*declaration, prefix);
                break;
            case DeclKind::StaticAssert:
                break;
            case DeclKind::Template:
                error(*declaration, "templates are not supported until M9.8");
                break;
            case DeclKind::Class:
            case DeclKind::Enum:
                error(*declaration,
                      "classes and enums are not supported until M9.5");
                break;
            case DeclKind::Parameter:
            case DeclKind::Access:
            case DeclKind::Friend:
            case DeclKind::Unknown:
                break;
            }
        }
    }

    void collect_function(const Decl& declaration, const std::string& prefix) {
        if (declaration.is_template) {
            error(declaration, "function templates are not supported until M9.8");
            return;
        }
        if (declaration.is_constructor || declaration.is_destructor ||
            declaration.is_operator || !declaration.name.parts.empty()) {
            if (declaration.is_constructor || declaration.is_destructor ||
                declaration.is_operator) {
                error(declaration,
                      "member/operator functions are not supported until M9.5");
                return;
            }
        }
        auto function = std::make_unique<Function>();
        function->decl = &declaration;
        function->qualified = qualify(prefix, declaration.name);
        function->extern_c = declaration.is_extern_c;
        function->is_static = declaration.is_static;
        function->defined = declaration.body != nullptr;
        function->variadic = declaration.variadic;
        function->result = resolve_type(declaration.type);
        for (const Param& parameter : declaration.parameters) {
            function->parameters.push_back(decay(resolve_type(parameter.type)));
        }
        function->symbol = mangle_function(function->qualified, function->parameters,
                                           function->extern_c, function->variadic);
        if (function->is_static) {
            function->symbol = "__cxx_static_" +
                               std::to_string(static_counter_++) + "_" +
                               function->symbol;
        }
        for (const auto& existing : function_storage_) {
            if (existing->symbol == function->symbol) {
                if (existing->defined && function->defined) {
                    error(declaration, "function already defined: " +
                                           function->qualified);
                    return;
                }
                if (function->defined) {
                    existing->defined = true;
                    existing->decl = &declaration;
                }
                return;
            }
        }
        function_index_[function->qualified].push_back(function.get());
        function_storage_.push_back(std::move(function));
    }

    void collect_global(const Decl& declaration, const std::string& prefix) {
        if (declaration.is_template) {
            error(declaration, "variable templates are not supported");
            return;
        }
        auto global = std::make_unique<Global>();
        global->decl = &declaration;
        global->qualified = qualify(prefix, declaration.name);
        global->extern_c = declaration.is_extern_c;
        global->is_static = declaration.is_static;
        global->type = resolve_type(declaration.type);
        global->initializer = declaration.initializer.get();
        global->defined = !declaration.is_extern || declaration.initializer != nullptr;
        global->symbol =
            mangle_variable(global->qualified, global->extern_c);
        if (global->is_static) {
            global->symbol = "__cxx_static_global_" +
                             std::to_string(static_counter_++) + "_" +
                             global->symbol;
        }
        for (const auto& existing : global_storage_) {
            if (existing->symbol == global->symbol) {
                if (existing->initializer && global->initializer) {
                    error(declaration, "global already defined: " + global->qualified);
                    return;
                }
                if (global->initializer) {
                    existing->initializer = global->initializer;
                    existing->defined = true;
                }
                return;
            }
        }
        global_index_[global->qualified] = global.get();
        global_storage_.push_back(std::move(global));
    }

    void collect_using(const Decl& declaration, const std::string& prefix) {
        if (declaration.name.parts.empty()) {
            return;
        }
        if (declaration.is_using_namespace) {
            imports_[prefix].insert(declaration.name.to_string());
            return;
        }
        const std::string alias = declaration.name.parts.back();
        using_aliases_[prefix][alias] = declaration.name.to_string();
    }

    void push_scope() { local_scopes_.emplace_back(); }
    void pop_scope() { local_scopes_.pop_back(); }

    Local* lookup_local(const Name& name) {
        if (name.parts.size() != 1) {
            return nullptr;
        }
        for (auto scope = local_scopes_.rbegin(); scope != local_scopes_.rend(); ++scope) {
            const auto found = scope->find(name.parts[0]);
            if (found != scope->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    Global* lookup_global(const Name& name, const std::string& prefix) {
        const std::vector<std::string> keys = candidate_keys(name, prefix);
        for (const std::string& key : keys) {
            const auto found = global_index_.find(key);
            if (found != global_index_.end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    std::vector<std::string> candidate_keys(const Name& name,
                                             const std::string& prefix) const {
        std::vector<std::string> keys;
        if (name.global || name.parts.size() > 1) {
            keys.push_back(name.to_string());
            if (name.to_string().rfind("::", 0) == 0) {
                keys.back().erase(0, 2);
            }
            return keys;
        }
        if (name.parts.empty()) {
            return keys;
        }
        const std::string& base = name.parts[0];
        std::string scope = prefix;
        while (true) {
            keys.push_back(scope.empty() ? base : scope + "::" + base);
            const std::size_t separator = scope.rfind("::");
            if (separator == std::string::npos) {
                if (!scope.empty()) {
                    scope.clear();
                    continue;
                }
                break;
            }
            scope.resize(separator);
        }
        for (const std::string& key_scope : keys) {
            const std::size_t separator = key_scope.rfind("::");
            const std::string scope_name =
                separator == std::string::npos ? "" : key_scope.substr(0, separator);
            const auto alias_scope = using_aliases_.find(scope_name);
            if (alias_scope != using_aliases_.end()) {
                const auto alias = alias_scope->second.find(base);
                if (alias != alias_scope->second.end()) {
                    keys.push_back(alias->second);
                }
            }
        }
        return keys;
    }

    std::vector<Function*> lookup_functions(const Name& name,
                                            const std::string& prefix) {
        const std::vector<std::string> keys = candidate_keys(name, prefix);
        for (const std::string& key : keys) {
            const auto found = function_index_.find(key);
            if (found != function_index_.end()) {
                return found->second;
            }
        }
        // Namespace imports are searched after the enclosing scopes.
        std::string scope = prefix;
        while (true) {
            const auto imported = imports_.find(scope);
            if (imported != imports_.end() && name.parts.size() == 1) {
                for (const std::string& target : imported->second) {
                    const auto found =
                        function_index_.find(target + "::" + name.parts[0]);
                    if (found != function_index_.end()) {
                        return found->second;
                    }
                }
            }
            if (scope.empty()) {
                break;
            }
            const std::size_t separator = scope.rfind("::");
            scope = separator == std::string::npos ? "" : scope.substr(0, separator);
        }
        return {};
    }

    TypePtr type_of(const Expr& expression, bool decay_arrays = true) {
        switch (expression.kind) {
        case ExprKind::IntegerLiteral:
            return Type::make_builtin(BuiltinKind::Int);
        case ExprKind::CharacterLiteral:
            return Type::make_builtin(BuiltinKind::Char);
        case ExprKind::BoolLiteral:
            return Type::make_builtin(BuiltinKind::Bool);
        case ExprKind::FloatLiteral:
            return expression.text.find('f') != std::string::npos ||
                           expression.text.find('F') != std::string::npos
                       ? Type::make_builtin(BuiltinKind::Float)
                       : Type::make_builtin(BuiltinKind::Double);
        case ExprKind::StringLiteral:
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Char));
        case ExprKind::NullptrLiteral:
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
        case ExprKind::Name: {
            if (Local* local = lookup_local(expression.name)) {
                TypePtr type = resolve_type(local->type);
                if (type && type->kind == TypeKind::LValueRef) {
                    type = type->element;
                }
                if (decay_arrays) {
                    type = decay(type);
                }
                return type;
            }
            if (Global* global =
                    lookup_global(expression.name, current_prefix_)) {
                TypePtr type = resolve_type(global->type);
                if (decay_arrays) {
                    type = decay(type);
                }
                return type;
            }
            if (!lookup_functions(expression.name, current_prefix_).empty()) {
                return Type::make_pointer(Type::make_builtin(BuiltinKind::Int));
            }
            diagnose_once(expression, "unknown identifier: " +
                                          expression.name.to_string());
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Unary: {
            if (expression.text == "&") {
                return Type::make_pointer(type_of(*expression.children[0], false));
            }
            if (expression.text == "*") {
                const TypePtr pointer = type_of(*expression.children[0]);
                return pointer && pointer->element ? pointer->element
                                                   : Type::make_builtin(BuiltinKind::Int);
            }
            if (expression.text == "!") {
                return Type::make_builtin(BuiltinKind::Bool);
            }
            if (expression.text == "++" || expression.text == "--") {
                return type_of(*expression.children[0]);
            }
            return promote(type_of(*expression.children[0]));
        }
        case ExprKind::Binary: {
            const TypePtr left = type_of(*expression.children[0]);
            const TypePtr right = type_of(*expression.children[1]);
            if (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == "<=" ||
                expression.text == ">" || expression.text == ">=" ||
                expression.text == "&&" || expression.text == "||") {
                return Type::make_builtin(BuiltinKind::Bool);
            }
            if ((expression.text == "+" || expression.text == "-") &&
                is_pointer(left)) {
                return decay(left);
            }
            if (expression.text == "+" && is_pointer(right)) {
                return decay(right);
            }
            if (left && left->kind == TypeKind::Builtin &&
                left->builtin == BuiltinKind::Double) {
                return left;
            }
            return arithmetic_type(left, right);
        }
        case ExprKind::Assign:
            return type_of(*expression.children[0], false);
        case ExprKind::Conditional: {
            const TypePtr left = type_of(*expression.children[1]);
            const TypePtr right = type_of(*expression.children[2]);
            if (same_type(left, right)) {
                return left;
            }
            if (is_fp(left) || is_fp(right) || is_integer(left) ||
                is_integer(right)) {
                return arithmetic_type(left, right);
            }
            return promote(left);
        }
        case ExprKind::Call: {
            if (!expression.children.empty() &&
                expression.children[0]->kind == ExprKind::Name) {
                const std::string name =
                    expression.children[0]->name.to_string();
                if (name == "__builtin_va_start" || name == "__builtin_va_end") {
                    return Type::make_builtin(BuiltinKind::Void);
                }
                if (name == "__builtin_va_arg") {
                    return resolve_type(expression.type);
                }
            }
            if (Function* function = resolve_call(expression)) {
                return resolve_type(function->result);
            }
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Index: {
            const TypePtr base = type_of(*expression.children[0]);
            return base && base->element ? base->element
                                         : Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Cast:
            return resolve_type(expression.type);
        case ExprKind::Sizeof:
        case ExprKind::Alignof:
            return Type::make_builtin(BuiltinKind::Int);
        case ExprKind::Member:
        case ExprKind::Scope:
        case ExprKind::New:
        case ExprKind::Delete:
        case ExprKind::Typeid:
        case ExprKind::Lambda:
        case ExprKind::This:
        case ExprKind::InitializerList:
        case ExprKind::Unknown:
            diagnose_once(expression, "expression is not supported until a later M9 phase");
            return Type::make_builtin(BuiltinKind::Int);
        }
        return Type::make_builtin(BuiltinKind::Int);
    }

    void diagnose_once(const Expr& expression, std::string message) {
        if (diagnosed_expressions_.insert(&expression).second) {
            error(expression.location, std::move(message));
        }
    }

    int conversion_cost(const TypePtr& argument, const TypePtr& parameter,
                        bool argument_is_lvalue, bool argument_is_zero) {
        const TypePtr arg = decay(resolve_type(argument));
        const TypePtr param = resolve_type(parameter);
        if (!arg || !param) {
            return 100;
        }
        if (param->kind == TypeKind::LValueRef ||
            param->kind == TypeKind::RValueRef) {
            if (!argument_is_lvalue && param->kind == TypeKind::LValueRef) {
                return 100;
            }
            const TypePtr element = resolve_type(param->element);
            if (same_type(arg, element)) {
                return 0;
            }
            if (is_integer(arg) && is_integer(element)) {
                return 1;
            }
            return 100;
        }
        if (same_type(arg, param)) {
            return 0;
        }
        if (is_integer(arg) && is_integer(param)) {
            return 1;
        }
        if (is_fp(arg) && is_fp(param)) {
            return same_type(decay(arg), param) ? 0 : 1;
        }
        if (is_integer(arg) && is_fp(param)) {
            return 1;
        }
        if (is_fp(arg) && is_integer(param)) {
            return 2;
        }
        if (is_pointer(arg) && is_pointer(param)) {
            if (same_type(decay(arg), param)) {
                return 0;
            }
            return 100;
        }
        if (argument_is_zero && is_pointer(param)) {
            return 1;
        }
        if (is_pointer(arg) && param->kind == TypeKind::Builtin &&
            param->builtin == BuiltinKind::Bool) {
            return 2;
        }
        return 100;
    }

    Function* resolve_call(const Expr& call) {
        const auto cached = call_targets_.find(&call);
        if (cached != call_targets_.end()) {
            return cached->second;
        }
        call_targets_[&call] = nullptr;
        if (call.children.empty() || call.children[0]->kind != ExprKind::Name) {
            diagnose_once(call, "only direct function calls are supported in M9.3");
            return nullptr;
        }
        const Name& name = call.children[0]->name;
        std::vector<Function*> candidates = lookup_functions(name, current_prefix_);
        if (candidates.empty()) {
            diagnose_once(call, "unknown function: " + name.to_string());
            return nullptr;
        }
        std::vector<TypePtr> argument_types;
        std::vector<bool> lvalues;
        std::vector<bool> zeros;
        for (const ExprPtr& argument : call.arguments) {
            argument_types.push_back(type_of(*argument));
            lvalues.push_back(is_lvalue(*argument));
            std::int64_t value = 0;
            zeros.push_back(argument->kind == ExprKind::IntegerLiteral &&
                            argument->integer == 0);
            (void)value;
        }
        Function* best = nullptr;
        int best_cost = 1000000;
        bool ambiguous = false;
        for (Function* candidate : candidates) {
            if (candidate->variadic) {
                if (argument_types.size() < candidate->parameters.size()) {
                    continue;
                }
            } else if (candidate->parameters.size() != argument_types.size()) {
                continue;
            }
            int cost = 0;
            bool viable = true;
            const std::size_t named = candidate->parameters.size();
            for (std::size_t index = 0; index < named; ++index) {
                const int argument_cost =
                    conversion_cost(argument_types[index], candidate->parameters[index],
                                    lvalues[index], zeros[index]);
                if (argument_cost >= 100) {
                    viable = false;
                    break;
                }
                cost += argument_cost;
            }
            if (!viable) {
                continue;
            }
            if (cost < best_cost) {
                best = candidate;
                best_cost = cost;
                ambiguous = false;
            } else if (cost == best_cost) {
                ambiguous = true;
            }
        }
        if (!best) {
            diagnose_once(call, "no matching function for: " + name.to_string());
            return nullptr;
        }
        if (ambiguous) {
            diagnose_once(call, "ambiguous call to: " + name.to_string());
            return nullptr;
        }
        call_targets_[&call] = best;
        return best;
    }

    bool is_lvalue(const Expr& expression) const {
        if (expression.kind == ExprKind::Name) {
            return true;
        }
        if (expression.kind == ExprKind::Index) {
            return true;
        }
        if (expression.kind == ExprKind::Unary && expression.text == "*") {
            return true;
        }
        return false;
    }

    bool eval_const(const Expr& expression, std::int64_t& value) {
        switch (expression.kind) {
        case ExprKind::IntegerLiteral:
            value = expression.integer;
            return true;
        case ExprKind::CharacterLiteral:
            value = expression.integer;
            return true;
        case ExprKind::BoolLiteral:
            value = expression.integer;
            return true;
        case ExprKind::Unary: {
            std::int64_t operand = 0;
            if (expression.children.empty() ||
                !eval_const(*expression.children[0], operand)) {
                return false;
            }
            if (expression.text == "-") {
                value = -operand;
            } else if (expression.text == "+") {
                value = operand;
            } else if (expression.text == "~") {
                value = ~operand;
            } else if (expression.text == "!") {
                value = operand == 0 ? 1 : 0;
            } else {
                return false;
            }
            return true;
        }
        case ExprKind::Binary: {
            std::int64_t left = 0;
            std::int64_t right = 0;
            if (!eval_const(*expression.children[0], left) ||
                !eval_const(*expression.children[1], right)) {
                return false;
            }
            const std::string& op = expression.text;
            if (op == "+") value = left + right;
            else if (op == "-") value = left - right;
            else if (op == "*") value = left * right;
            else if (op == "/") value = right == 0 ? 0 : left / right;
            else if (op == "%") value = right == 0 ? 0 : left % right;
            else if (op == "&") value = left & right;
            else if (op == "|") value = left | right;
            else if (op == "^") value = left ^ right;
            else if (op == "<<") value = left << right;
            else if (op == ">>") value = left >> right;
            else if (op == "==") value = left == right ? 1 : 0;
            else if (op == "!=") value = left != right ? 1 : 0;
            else if (op == "<") value = left < right ? 1 : 0;
            else if (op == "<=") value = left <= right ? 1 : 0;
            else if (op == ">") value = left > right ? 1 : 0;
            else if (op == ">=") value = left >= right ? 1 : 0;
            else return false;
            return true;
        }
        case ExprKind::Sizeof: {
            if (expression.type) {
                value = type_size(expression.type);
                return true;
            }
            if (!expression.children.empty()) {
                value = type_size(type_of(*expression.children[0], false));
                return true;
            }
            return false;
        }
        default:
            return false;
        }
    }

    bool eval_const_fp(const Expr& expression, double& value) {
        switch (expression.kind) {
        case ExprKind::FloatLiteral:
            value = expression.floating;
            return true;
        case ExprKind::IntegerLiteral:
        case ExprKind::CharacterLiteral:
        case ExprKind::BoolLiteral:
            value = static_cast<double>(expression.integer);
            return true;
        case ExprKind::Unary: {
            double operand = 0.0;
            if (expression.children.empty() ||
                !eval_const_fp(*expression.children[0], operand)) {
                return false;
            }
            if (expression.text == "-") {
                value = -operand;
            } else if (expression.text == "+") {
                value = operand;
            } else {
                return false;
            }
            return true;
        }
        case ExprKind::Binary: {
            double left = 0.0;
            double right = 0.0;
            if (!eval_const_fp(*expression.children[0], left) ||
                !eval_const_fp(*expression.children[1], right)) {
                return false;
            }
            if (expression.text == "+") value = left + right;
            else if (expression.text == "-") value = left - right;
            else if (expression.text == "*") value = left * right;
            else if (expression.text == "/") value = right == 0.0 ? 0.0 : left / right;
            else return false;
            return true;
        }
        default:
            return false;
        }
    }

    int allocate_local(const TypePtr& type) {
        const int size = std::max(1, type_size(type));
        const int alignment = std::max(4, type_align(type));
        frame_size_ = (frame_size_ + alignment - 1) / alignment * alignment;
        const int offset = frame_size_;
        frame_size_ += (size + 3) / 4 * 4;
        return offset;
    }

    void emit(std::string line) {
        text_ += line;
        text_ += '\n';
    }

    std::string new_label(std::string_view prefix) {
        return ".L" + std::string(prefix) + "_" + std::to_string(label_counter_++);
    }

    std::string intern_string(const std::string& value) {
        const std::string label = ".Lstr_" + std::to_string(string_counter_++);
        rodata_ += label + ":\n    .asciz \"" + escape_asm(value) + "\"\n";
        return label;
    }

    std::string intern_fp_constant(double value, bool single) {
        const std::string label = ".Lfp_" + std::to_string(fp_counter_++);
        rodata_ += label + ":\n    ." + std::string(single ? "float " : "double ") +
                   fp_literal_text(value, single) + "\n";
        return label;
    }

    void load_fp(const TypePtr& type) {
        emit(std::string("    FLOAD.") + (is_float32(type) ? "W" : "D") + " F0, [R0]");
    }

    void store_fp(const TypePtr& type) {
        emit(std::string("    FSTORE.") + (is_float32(type) ? "W" : "D") +
             " F0, [R1]");
    }

    void convert_fp(const TypePtr& from, const TypePtr& to) {
        if (!is_fp(from) || !is_fp(to)) {
            return;
        }
        if (is_float32(from) && is_float64(to)) {
            emit("    FCVT.D.S F0, F0");
        } else if (is_float64(from) && is_float32(to)) {
            emit("    FCVT.S.D F0, F0");
        }
    }

    void gen_as_fp(const Expr& expression, const TypePtr& target) {
        const TypePtr source = type_of(expression);
        gen_expr(expression);
        if (!is_fp(source)) {
            emit(std::string("    FCVT.") + (is_float32(target) ? "S" : "D") +
                 ".W F0, R0");
        } else {
            convert_fp(source, target);
        }
    }

    void load_value(const TypePtr& type) {
        const TypePtr resolved = resolve_type(type);
        if (resolved && resolved->kind == TypeKind::Builtin &&
            (resolved->builtin == BuiltinKind::Char ||
             resolved->builtin == BuiltinKind::Bool)) {
            emit("    LOAD8S R0, [R0]");
        } else {
            emit("    LOAD R0, [R0]");
        }
    }

    void store_value(const TypePtr& type) {
        const TypePtr resolved = resolve_type(type);
        if (resolved && resolved->kind == TypeKind::Builtin &&
            (resolved->builtin == BuiltinKind::Char ||
             resolved->builtin == BuiltinKind::Bool)) {
            emit("    STORE8 R0, [R1]");
        } else {
            emit("    STORE R0, [R1]");
        }
    }

    void gen_address(const Expr& expression) {
        if (expression.kind == ExprKind::Name) {
            if (Local* local = lookup_local(expression.name)) {
                if (local->is_reference) {
                    emit("    LOAD R0, [R7 + " + std::to_string(local->offset) + "]");
                } else {
                    emit("    LEA R0, [R7 + " + std::to_string(local->offset) + "]");
                }
                return;
            }
            if (Global* global = lookup_global(expression.name, current_prefix_)) {
                emit("    LEA R0, [" + global->symbol + "]");
                return;
            }
            diagnose_once(expression, "unknown variable: " + expression.name.to_string());
            emit("    MOV R0, #0");
            return;
        }
        if (expression.kind == ExprKind::Index) {
            const TypePtr base_type = type_of(*expression.children[0]);
            const int element_size =
                base_type && base_type->element ? type_size(base_type->element) : 4;
            gen_expr(*expression.children[0]);
            const int base_temp = allocate_local(Type::make_pointer(
                Type::make_builtin(BuiltinKind::Int)));
            emit("    STORE R0, [R7 + " + std::to_string(base_temp) + "]");
            gen_expr(*expression.children[1]);
            if (element_size != 1) {
                emit("    MUL R0, R0, #" + std::to_string(element_size));
            }
            emit("    LOAD R1, [R7 + " + std::to_string(base_temp) + "]");
            emit("    ADD R0, R1, R0");
            return;
        }
        if (expression.kind == ExprKind::Unary && expression.text == "*") {
            gen_expr(*expression.children[0]);
            return;
        }
        diagnose_once(expression, "expression is not an lvalue in M9.3");
        emit("    MOV R0, #0");
    }

    TypePtr gen_expr(const Expr& expression) {
        switch (expression.kind) {
        case ExprKind::IntegerLiteral:
        case ExprKind::CharacterLiteral:
        case ExprKind::BoolLiteral:
            emit("    MOV R0, #" + std::to_string(expression.integer));
            return expression.kind == ExprKind::BoolLiteral
                       ? Type::make_builtin(BuiltinKind::Bool)
                       : Type::make_builtin(BuiltinKind::Int);
        case ExprKind::FloatLiteral: {
            const TypePtr type =
                expression.text.find('f') != std::string::npos ||
                        expression.text.find('F') != std::string::npos
                    ? Type::make_builtin(BuiltinKind::Float)
                    : Type::make_builtin(BuiltinKind::Double);
            const std::string label =
                intern_fp_constant(expression.floating, is_float32(type));
            emit(std::string("    FLOAD.") + (is_float32(type) ? "W" : "D") +
                 " F0, [" + label + "]");
            return type;
        }
        case ExprKind::StringLiteral:
            emit("    LEA R0, [" + intern_string(expression.text) + "]");
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Char));
        case ExprKind::NullptrLiteral:
            emit("    MOV R0, #0");
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
        case ExprKind::Name: {
            if (Local* local = lookup_local(expression.name)) {
                gen_address(expression);
                TypePtr type = resolve_type(local->type);
                if (type && type->kind == TypeKind::LValueRef) {
                    type = type->element;
                }
                if (type && type->kind == TypeKind::Array) {
                    return type;
                }
                if (is_fp(type)) {
                    load_fp(type);
                    return type;
                }
                load_value(type);
                return type;
            }
            if (Global* global = lookup_global(expression.name, current_prefix_)) {
                emit("    LEA R0, [" + global->symbol + "]");
                TypePtr type = resolve_type(global->type);
                if (type && type->kind == TypeKind::Array) {
                    return type;
                }
                if (is_fp(type)) {
                    load_fp(type);
                    return type;
                }
                load_value(type);
                return type;
            }
            if (const std::vector<Function*> functions =
                    lookup_functions(expression.name, current_prefix_);
                !functions.empty()) {
                emit("    LEA R0, [" + functions.front()->symbol + "]");
                return Type::make_pointer(Type::make_builtin(BuiltinKind::Int));
            }
            diagnose_once(expression, "unknown identifier: " + expression.name.to_string());
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Unary:
            return gen_unary(expression);
        case ExprKind::Binary:
            return gen_binary(expression);
        case ExprKind::Assign:
            return gen_assignment(expression);
        case ExprKind::Conditional:
            return gen_conditional(expression);
        case ExprKind::Call:
            return gen_call(expression);
        case ExprKind::Index: {
            gen_address(expression);
            const TypePtr type = type_of(expression);
            if (is_fp(type)) {
                load_fp(type);
            } else {
                load_value(type);
            }
            return type;
        }
        case ExprKind::Cast: {
            const TypePtr target = resolve_type(expression.type);
            const TypePtr source = type_of(*expression.children[0]);
            gen_expr(*expression.children[0]);
            if (is_fp(target) && !is_fp(source)) {
                emit(std::string("    FCVT.") + (is_float32(target) ? "S" : "D") +
                     ".W F0, R0");
            } else if (is_fp(target) && is_fp(source)) {
                convert_fp(source, target);
            } else if (!is_fp(target) && is_fp(source)) {
                emit(std::string("    FCVT.W.") + (is_float32(source) ? "S" : "D") +
                     " R0, F0");
            } else if (target && target->kind == TypeKind::Builtin &&
                       target->builtin == BuiltinKind::Bool) {
                emit("    CMP R0, #0");
                emit("    MOV R0, #0");
                const std::string done = new_label("bool");
                emit("    JZ " + done);
                emit("    MOV R0, #1");
                emit(done + ":");
            }
            return target;
        }
        case ExprKind::Sizeof: {
            std::int64_t value = 0;
            if (!eval_const(expression, value)) {
                value = 4;
            }
            emit("    MOV R0, #" + std::to_string(value));
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Alignof:
            emit("    MOV R0, #4");
            return Type::make_builtin(BuiltinKind::Int);
        case ExprKind::Member:
        case ExprKind::Scope:
        case ExprKind::New:
        case ExprKind::Delete:
        case ExprKind::Typeid:
        case ExprKind::Lambda:
        case ExprKind::This:
        case ExprKind::InitializerList:
        case ExprKind::Unknown:
            diagnose_once(expression, "expression is not supported until a later M9 phase");
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        }
        return Type::make_builtin(BuiltinKind::Int);
    }

    TypePtr gen_unary(const Expr& expression) {
        const std::string& op = expression.text;
        if (op == "&") {
            gen_address(*expression.children[0]);
            return Type::make_pointer(type_of(*expression.children[0], false));
        }
        if (op == "*") {
            gen_expr(*expression.children[0]);
            const TypePtr type = type_of(expression);
            if (is_fp(type)) {
                load_fp(type);
            } else {
                load_value(type);
            }
            return type;
        }
        if (op == "++" || op == "--") {
            const TypePtr type = type_of(*expression.children[0]);
            const TypePtr resolved = resolve_type(type);
            const bool byte_value =
                resolved && resolved->kind == TypeKind::Builtin &&
                (resolved->builtin == BuiltinKind::Char ||
                 resolved->builtin == BuiltinKind::Bool);
            gen_address(*expression.children[0]);
            emit("    MOV R1, R0");
            if (byte_value) {
                emit("    LOAD8S R0, [R1]");
            } else {
                emit("    LOAD R0, [R1]");
            }
            int old_temp = -1;
            if (!expression.is_prefix) {
                old_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(old_temp) + "]");
            }
            if (op == "++") {
                emit("    ADD R0, R0, #1");
            } else {
                emit("    SUB R0, R0, #1");
            }
            if (byte_value) {
                emit("    STORE8 R0, [R1]");
            } else {
                emit("    STORE R0, [R1]");
            }
            if (!expression.is_prefix) {
                emit("    LOAD R0, [R7 + " + std::to_string(old_temp) + "]");
            }
            return type;
        }
        gen_expr(*expression.children[0]);
        const TypePtr operand_type = type_of(*expression.children[0]);
        const TypePtr type = type_of(expression);
        if (is_fp(operand_type)) {
            if (op == "-") {
                emit(std::string("    FNEG.") + (is_float32(operand_type) ? "S" : "D") +
                     " F0, F0");
            } else if (op == "!") {
                const std::string zero =
                    intern_fp_constant(0.0, is_float32(operand_type));
                emit(std::string("    FLOAD.") +
                     (is_float32(operand_type) ? "W" : "D") + " F1, [" + zero + "]");
                emit("    FEQ R0, F0, F1");
                emit("    XOR R0, R0, #1");
                return Type::make_builtin(BuiltinKind::Bool);
            }
            return operand_type;
        }
        if (op == "-") {
            emit("    NEG R0, R0");
        } else if (op == "~") {
            emit("    NOT R0, R0");
        } else if (op == "!") {
            emit("    CMP R0, #0");
            emit("    MOV R0, #0");
            const std::string done = new_label("not");
            emit("    JZ " + done);
            emit("    MOV R0, #1");
            emit(done + ":");
        }
        return promote(type);
    }

    void gen_condition(const Expr& expression) {
        const TypePtr type = type_of(expression);
        if (is_fp(type)) {
            gen_expr(expression);
            const std::string zero = intern_fp_constant(0.0, is_float32(type));
            emit(std::string("    FLOAD.") + (is_float32(type) ? "W" : "D") +
                 " F1, [" + zero + "]");
            emit("    FEQ R0, F0, F1");
            emit("    XOR R0, R0, #1");
            return;
        }
        gen_expr(expression);
    }

    TypePtr gen_fp_binary(const Expr& expression, const TypePtr& left_type,
                          const TypePtr& right_type) {
        const std::string& op = expression.text;
        const TypePtr result_type = arithmetic_type(left_type, right_type);
        const std::string suffix = is_float32(result_type) ? ".S" : ".D";
        const bool compare = op == "==" || op == "!=" || op == "<" ||
                             op == "<=" || op == ">" || op == ">=";
        gen_as_fp(*expression.children[0], result_type);
        const int left_temp = allocate_local(result_type);
        emit(std::string("    FSTORE.") + (is_float32(result_type) ? "W" : "D") +
             " F0, [R7 + " + std::to_string(left_temp) + "]");
        gen_as_fp(*expression.children[1], result_type);
        const int right_temp = allocate_local(result_type);
        emit(std::string("    FSTORE.") + (is_float32(result_type) ? "W" : "D") +
             " F0, [R7 + " + std::to_string(right_temp) + "]");
        emit(std::string("    FLOAD.") + (is_float32(result_type) ? "W" : "D") +
             " F1, [R7 + " + std::to_string(left_temp) + "]");
        emit(std::string("    FLOAD.") + (is_float32(result_type) ? "W" : "D") +
             " F2, [R7 + " + std::to_string(right_temp) + "]");
        if (compare) {
            if (op == "==") {
                emit("    FEQ" + suffix + " R0, F1, F2");
            } else if (op == "!=") {
                emit("    FEQ" + suffix + " R0, F1, F2");
                emit("    XOR R0, R0, #1");
            } else if (op == "<") {
                emit("    FLT" + suffix + " R0, F1, F2");
            } else if (op == "<=") {
                emit("    FLE" + suffix + " R0, F1, F2");
            } else if (op == ">") {
                emit("    FLT" + suffix + " R0, F2, F1");
            } else {
                emit("    FLE" + suffix + " R0, F2, F1");
            }
            return Type::make_builtin(BuiltinKind::Bool);
        }
        if (op == "+") {
            emit("    FADD" + suffix + " F0, F1, F2");
        } else if (op == "-") {
            emit("    FSUB" + suffix + " F0, F1, F2");
        } else if (op == "*") {
            emit("    FMUL" + suffix + " F0, F1, F2");
        } else if (op == "/") {
            emit("    FDIV" + suffix + " F0, F1, F2");
        } else {
            diagnose_once(expression, "unsupported floating-point operator: " + op);
        }
        return result_type;
    }

    TypePtr gen_binary(const Expr& expression) {
        const std::string& op = expression.text;
        if (op == "&&" || op == "||") {
            const std::string short_label = new_label(op == "&&" ? "andfalse" : "ortrue");
            const std::string end_label = new_label("logicend");
            gen_condition(*expression.children[0]);
            emit("    CMP R0, #0");
            if (op == "&&") {
                emit("    JZ " + short_label);
            } else {
                emit("    JNZ " + short_label);
            }
            gen_condition(*expression.children[1]);
            emit("    CMP R0, #0");
            emit("    MOV R0, #0");
            emit("    JZ " + end_label);
            emit("    MOV R0, #1");
            emit("    JMP " + end_label);
            emit(short_label + ":");
            emit("    MOV R0, #" + std::string(op == "&&" ? "0" : "1"));
            emit(end_label + ":");
            return Type::make_builtin(BuiltinKind::Bool);
        }

        const TypePtr left_type = type_of(*expression.children[0]);
        const TypePtr right_type = type_of(*expression.children[1]);
        if (is_fp(left_type) || is_fp(right_type)) {
            return gen_fp_binary(expression, left_type, right_type);
        }
        gen_expr(*expression.children[0]);
        const int left_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(left_temp) + "]");
        gen_expr(*expression.children[1]);
        emit("    MOV R1, R0");
        emit("    LOAD R0, [R7 + " + std::to_string(left_temp) + "]");

        if ((op == "+" || op == "-") && is_pointer(left_type)) {
            const int element_size =
                left_type->element ? type_size(left_type->element) : 1;
            if (element_size != 1) {
                emit("    MUL R1, R1, #" + std::to_string(element_size));
            }
            emit(std::string("    ") + (op == "+" ? "ADD" : "SUB") +
                 " R0, R0, R1");
            return decay(left_type);
        }
        if (op == "+" && is_pointer(right_type)) {
            const int element_size =
                right_type->element ? type_size(right_type->element) : 1;
            if (element_size != 1) {
                emit("    MUL R0, R0, #" + std::to_string(element_size));
            }
            emit("    ADD R0, R0, R1");
            return decay(right_type);
        }

        if (op == "+") emit("    ADD R0, R0, R1");
        else if (op == "-") emit("    SUB R0, R0, R1");
        else if (op == "*") emit("    MUL R0, R0, R1");
        else if (op == "/") emit("    DIV R0, R0, R1");
        else if (op == "%") emit("    MOD R0, R0, R1");
        else if (op == "&") emit("    AND R0, R0, R1");
        else if (op == "|") emit("    OR R0, R0, R1");
        else if (op == "^") emit("    XOR R0, R0, R1");
        else if (op == "<<") emit("    SHL R0, R0, R1");
        else if (op == ">>") emit("    SAR R0, R0, R1");
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                 op == ">" || op == ">=") {
            const std::string true_label = new_label("true");
            const std::string end_label = new_label("cmpend");
            emit("    CMP R0, R1");
            if (op == "==") emit("    JZ " + true_label);
            if (op == "!=") emit("    JNZ " + true_label);
            if (op == "<") emit("    JLT " + true_label);
            if (op == "<=") emit("    JLE " + true_label);
            if (op == ">") emit("    JGT " + true_label);
            if (op == ">=") emit("    JGE " + true_label);
            emit("    MOV R0, #0");
            emit("    JMP " + end_label);
            emit(true_label + ":");
            emit("    MOV R0, #1");
            emit(end_label + ":");
            return Type::make_builtin(BuiltinKind::Bool);
        } else {
            diagnose_once(expression, "unsupported binary operator: " + op);
            return Type::make_builtin(BuiltinKind::Int);
        }
        return type_of(expression);
    }

    TypePtr gen_fp_assignment(const Expr& expression, const TypePtr& lhs_type) {
        const std::string suffix = is_float32(lhs_type) ? ".S" : ".D";
        const std::string width = is_float32(lhs_type) ? "W" : "D";
        gen_address(*expression.children[0]);
        const int address_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");
        if (expression.text == "=") {
            gen_as_fp(*expression.children[1], lhs_type);
        } else {
            const std::string op =
                expression.text.substr(0, expression.text.size() - 1);
            emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
            emit("    FLOAD." + width + " F0, [R1]");
            const int left_temp = allocate_local(lhs_type);
            emit("    FSTORE." + width + " F0, [R7 + " +
                 std::to_string(left_temp) + "]");
            gen_as_fp(*expression.children[1], lhs_type);
            const int right_temp = allocate_local(lhs_type);
            emit("    FSTORE." + width + " F0, [R7 + " +
                 std::to_string(right_temp) + "]");
            emit("    FLOAD." + width + " F1, [R7 + " +
                 std::to_string(left_temp) + "]");
            emit("    FLOAD." + width + " F2, [R7 + " +
                 std::to_string(right_temp) + "]");
            if (op == "+") {
                emit("    FADD" + suffix + " F0, F1, F2");
            } else if (op == "-") {
                emit("    FSUB" + suffix + " F0, F1, F2");
            } else if (op == "*") {
                emit("    FMUL" + suffix + " F0, F1, F2");
            } else if (op == "/") {
                emit("    FDIV" + suffix + " F0, F1, F2");
            } else {
                diagnose_once(expression, "unsupported floating-point assignment");
            }
        }
        emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
        emit("    FSTORE." + width + " F0, [R1]");
        return lhs_type;
    }

    TypePtr gen_assignment(const Expr& expression) {
        const TypePtr lhs_type = type_of(*expression.children[0], false);
        if (is_fp(lhs_type)) {
            return gen_fp_assignment(expression, lhs_type);
        }
        gen_address(*expression.children[0]);
        const int address_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");

        if (expression.text == "=") {
            gen_expr(*expression.children[1]);
        } else {
            const std::string op = expression.text.substr(0, expression.text.size() - 1);
            emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
            load_value(lhs_type);
            const int left_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(left_temp) + "]");
            gen_expr(*expression.children[1]);
            emit("    MOV R1, R0");
            emit("    LOAD R0, [R7 + " + std::to_string(left_temp) + "]");
            if (op == "+") emit("    ADD R0, R0, R1");
            else if (op == "-") emit("    SUB R0, R0, R1");
            else if (op == "*") emit("    MUL R0, R0, R1");
            else if (op == "/") emit("    DIV R0, R0, R1");
            else if (op == "%") emit("    MOD R0, R0, R1");
            else if (op == "&") emit("    AND R0, R0, R1");
            else if (op == "|") emit("    OR R0, R0, R1");
            else if (op == "^") emit("    XOR R0, R0, R1");
            else if (op == "<<") emit("    SHL R0, R0, R1");
            else if (op == ">>") emit("    SAR R0, R0, R1");
            else diagnose_once(expression, "unsupported assignment operator");
        }
        emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
        store_value(lhs_type);
        return lhs_type;
    }

    TypePtr gen_conditional(const Expr& expression) {
        const TypePtr type = type_of(expression);
        const std::string else_label = new_label("else");
        const std::string end_label = new_label("condend");
        gen_condition(*expression.children[0]);
        emit("    CMP R0, #0");
        emit("    JZ " + else_label);
        gen_expr(*expression.children[1]);
        emit("    JMP " + end_label);
        emit(else_label + ":");
        gen_expr(*expression.children[2]);
        emit(end_label + ":");
        return type;
    }

    TypePtr gen_call(const Expr& expression) {
        if (!expression.children.empty() &&
            expression.children[0]->kind == ExprKind::Name) {
            const std::string name = expression.children[0]->name.to_string();
            if (name == "__builtin_va_start") {
                if (expression.arguments.empty() ||
                    expression.arguments[0]->kind != ExprKind::Name) {
                    diagnose_once(expression, "va_start expects a va_list variable");
                    emit("    MOV R0, #0");
                    return Type::make_builtin(BuiltinKind::Void);
                }
                Local* local = lookup_local(expression.arguments[0]->name);
                if (!local) {
                    diagnose_once(expression, "unknown va_list variable");
                    emit("    MOV R0, #0");
                    return Type::make_builtin(BuiltinKind::Void);
                }
                emit("    LEA R0, [R7 + " + va_base_placeholder_ + "]");
                emit("    STORE R0, [R7 + " + std::to_string(local->offset) + "]");
                emit("    MOV R0, #0");
                return Type::make_builtin(BuiltinKind::Void);
            }
            if (name == "__builtin_va_end") {
                emit("    MOV R0, #0");
                return Type::make_builtin(BuiltinKind::Void);
            }
            if (name == "__builtin_va_arg") {
                if (expression.arguments.empty() ||
                    expression.arguments[0]->kind != ExprKind::Name) {
                    diagnose_once(expression, "va_arg expects a va_list variable");
                    emit("    MOV R0, #0");
                    return Type::make_builtin(BuiltinKind::Int);
                }
                const TypePtr type = resolve_type(expression.type);
                gen_address(*expression.arguments[0]);
                const int address_temp =
                    allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");
                emit("    LOAD R0, [R0]");
                if (is_fp(type)) {
                    if (is_float32(type)) {
                        emit("    FLOAD.D F1, [R0]");
                        emit("    FCVT.S.D F0, F1");
                    } else {
                        emit("    FLOAD.D F0, [R0]");
                    }
                } else {
                    emit("    LOAD R0, [R0]");
                }
                emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
                emit("    LOAD R2, [R1]");
                emit("    ADD R2, R2, #8");
                emit("    STORE R2, [R1]");
                return type;
            }
        }
        Function* function = resolve_call(expression);
        if (!function) {
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        }
        struct ArgumentSlot {
            int offset = 0;
            bool fp = false;
            TypePtr type;
        };
        std::vector<ArgumentSlot> slots;
        std::vector<int> stack_temps;
        int integer_argument = 0;
        int fp_argument = 0;
        const std::size_t named_parameters = function->parameters.size();
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            if (index >= named_parameters) {
                if (!function->variadic) {
                    diagnose_once(expression, "too many arguments for " +
                                                  function->qualified);
                    break;
                }
                const TypePtr argument_type = type_of(*expression.arguments[index]);
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Double));
                if (is_fp(argument_type)) {
                    gen_as_fp(*expression.arguments[index],
                              Type::make_builtin(BuiltinKind::Double));
                    emit("    FSTORE.D F0, [R7 + " + std::to_string(temp) + "]");
                } else {
                    gen_expr(*expression.arguments[index]);
                    emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                    emit("    MOV R0, #0");
                    emit("    STORE R0, [R7 + " + std::to_string(temp + 4) + "]");
                }
                stack_temps.push_back(temp);
                continue;
            }
            const TypePtr parameter = resolve_type(function->parameters[index]);
            ArgumentSlot slot;
            slot.type = parameter;
            if (parameter && (parameter->kind == TypeKind::LValueRef ||
                              parameter->kind == TypeKind::RValueRef)) {
                gen_address(*expression.arguments[index]);
                slot.offset = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(slot.offset) + "]");
                ++integer_argument;
            } else if (is_fp(parameter)) {
                gen_as_fp(*expression.arguments[index], parameter);
                slot.fp = true;
                slot.offset = allocate_local(parameter);
                emit(std::string("    FSTORE.") +
                     (is_float32(parameter) ? "W" : "D") + " F0, [R7 + " +
                     std::to_string(slot.offset) + "]");
                ++fp_argument;
            } else {
                gen_expr(*expression.arguments[index]);
                slot.offset = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(slot.offset) + "]");
                ++integer_argument;
            }
            slots.push_back(std::move(slot));
        }
        if (integer_argument > 4 || fp_argument > 8) {
            diagnose_once(expression, "too many register arguments");
            emit("    MOV R0, #0");
            return resolve_type(function->result);
        }
        int stack_bytes = 0;
        if (!stack_temps.empty()) {
            stack_bytes =
                static_cast<int>((stack_temps.size() * 8U + 15U) / 16U * 16U);
            emit("    ADJSP #-" + std::to_string(stack_bytes));
            for (std::size_t index = 0; index < stack_temps.size(); ++index) {
                const int offset = static_cast<int>(index) * 8;
                emit("    LOAD R4, [R7 + " + std::to_string(stack_temps[index]) + "]");
                emit("    STORE R4, [SP + " + std::to_string(offset) + "]");
                emit("    LOAD R4, [R7 + " +
                     std::to_string(stack_temps[index] + 4) + "]");
                emit("    STORE R4, [SP + " + std::to_string(offset + 4) + "]");
            }
        }
        int integer_register = 0;
        int fp_register = 0;
        for (const ArgumentSlot& slot : slots) {
            if (slot.fp) {
                emit(std::string("    FLOAD.") +
                     (is_float32(slot.type) ? "W" : "D") + " F" +
                     std::to_string(fp_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                ++fp_register;
            } else {
                emit("    LOAD R" + std::to_string(integer_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                ++integer_register;
            }
        }
        emit("    CALL " + function->symbol);
        if (stack_bytes != 0) {
            emit("    ADJSP #" + std::to_string(stack_bytes));
        }
        return resolve_type(function->result);
    }

    void gen_statement(const Stmt& statement) {
        switch (statement.kind) {
        case StmtKind::Empty:
            return;
        case StmtKind::Compound: {
            push_scope();
            for (const StmtPtr& child : statement.statements) {
                if (child) {
                    gen_statement(*child);
                }
            }
            pop_scope();
            return;
        }
        case StmtKind::Declaration:
            if (statement.declaration) {
                declare_local(*statement.declaration);
            }
            return;
        case StmtKind::Expression:
            if (statement.expression) {
                gen_expr(*statement.expression);
            }
            return;
        case StmtKind::If: {
            const std::string else_label = new_label("ifelse");
            const std::string end_label = new_label("ifend");
            gen_condition(*statement.condition);
            emit("    CMP R0, #0");
            emit("    JZ " + else_label);
            if (statement.body) {
                gen_statement(*statement.body);
            }
            emit("    JMP " + end_label);
            emit(else_label + ":");
            if (statement.else_body) {
                gen_statement(*statement.else_body);
            }
            emit(end_label + ":");
            return;
        }
        case StmtKind::While: {
            const std::string start = new_label("while");
            const std::string end = new_label("whileend");
            break_labels_.push_back(end);
            continue_labels_.push_back(start);
            emit(start + ":");
            gen_condition(*statement.condition);
            emit("    CMP R0, #0");
            emit("    JZ " + end);
            if (statement.body) {
                gen_statement(*statement.body);
            }
            emit("    JMP " + start);
            emit(end + ":");
            continue_labels_.pop_back();
            break_labels_.pop_back();
            return;
        }
        case StmtKind::DoWhile: {
            const std::string start = new_label("do");
            const std::string end = new_label("doend");
            break_labels_.push_back(end);
            continue_labels_.push_back(start);
            emit(start + ":");
            if (statement.body) {
                gen_statement(*statement.body);
            }
            gen_condition(*statement.condition);
            emit("    CMP R0, #0");
            emit("    JNZ " + start);
            emit(end + ":");
            continue_labels_.pop_back();
            break_labels_.pop_back();
            return;
        }
        case StmtKind::For: {
            push_scope();
            if (statement.declaration) {
                declare_local(*statement.declaration);
            } else if (statement.expression) {
                gen_expr(*statement.expression);
            }
            const std::string start = new_label("for");
            const std::string increment = new_label("forinc");
            const std::string end = new_label("forend");
            break_labels_.push_back(end);
            continue_labels_.push_back(increment);
            emit(start + ":");
            if (statement.condition) {
                gen_condition(*statement.condition);
                emit("    CMP R0, #0");
                emit("    JZ " + end);
            }
            if (statement.body) {
                gen_statement(*statement.body);
            }
            emit(increment + ":");
            if (statement.increment) {
                gen_expr(*statement.increment);
            }
            emit("    JMP " + start);
            emit(end + ":");
            continue_labels_.pop_back();
            break_labels_.pop_back();
            pop_scope();
            return;
        }
        case StmtKind::Break:
            if (!break_labels_.empty()) {
                emit("    JMP " + break_labels_.back());
            }
            return;
        case StmtKind::Continue:
            if (!continue_labels_.empty()) {
                emit("    JMP " + continue_labels_.back());
            }
            return;
        case StmtKind::Return:
            if (statement.expression) {
                gen_expr(*statement.expression);
            } else {
                emit("    MOV R0, #0");
            }
            emit("    JMP " + return_label_);
            return;
        case StmtKind::Switch:
        case StmtKind::Case:
        case StmtKind::Default:
            error(statement.location, "switch statements are not supported until a later M9 phase");
            return;
        case StmtKind::RangeFor:
            error(statement.location, "range-for is not supported until M9.7");
            return;
        case StmtKind::Throw:
        case StmtKind::Try:
            error(statement.location, "exceptions are not supported until M9.9");
            return;
        case StmtKind::Goto:
            error(statement.location, "goto is not supported in M9.3");
            return;
        case StmtKind::Label:
        case StmtKind::Unknown:
            return;
        }
    }

    void declare_local(const Decl& declaration) {
        if (local_scopes_.empty()) {
            push_scope();
        }
        const TypePtr type = resolve_type(declaration.type);
        Local local;
        local.type = type;
        local.is_reference = type && (type->kind == TypeKind::LValueRef ||
                                      type->kind == TypeKind::RValueRef);
        local.offset = allocate_local(type);
        if (!declaration.name.parts.empty()) {
            local_scopes_.back()[declaration.name.parts.back()] = local;
        }
        if (local.is_reference) {
            if (!declaration.initializer) {
                error(declaration, "reference declaration needs an initializer");
                return;
            }
            gen_address(*declaration.initializer);
            emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
            return;
        }
        if (type && type->kind == TypeKind::Array) {
            if (declaration.initializer &&
                declaration.initializer->kind == ExprKind::InitializerList) {
                const int element_size = type_size(type->element);
                std::size_t index = 0;
                for (const ExprPtr& element : declaration.initializer->children) {
                    if (index >= type->array_size) {
                        break;
                    }
                    if (is_fp(type->element)) {
                        gen_as_fp(*element, type->element);
                        emit(std::string("    FSTORE.") +
                             (is_float32(type->element) ? "W" : "D") + " F0, [R7 + " +
                             std::to_string(local.offset +
                                            static_cast<int>(index) * element_size) +
                             "]");
                    } else {
                        gen_expr(*element);
                        emit("    LEA R1, [R7 + " +
                             std::to_string(local.offset +
                                            static_cast<int>(index) * element_size) +
                             "]");
                        store_value(type->element);
                    }
                    ++index;
                }
            }
            return;
        }
        if (declaration.initializer) {
            if (is_fp(type)) {
                gen_as_fp(*declaration.initializer, type);
                emit(std::string("    FSTORE.") + (is_float32(type) ? "W" : "D") +
                     " F0, [R7 + " + std::to_string(local.offset) + "]");
                return;
            }
            gen_expr(*declaration.initializer);
            emit("    LEA R1, [R7 + " + std::to_string(local.offset) + "]");
            store_value(type);
        }
    }

    void emit_function(Function& function) {
        if (!function.decl || !function.decl->body) {
            return;
        }
        current_prefix_ = function.qualified;
        const std::size_t separator = current_prefix_.rfind("::");
        current_prefix_ =
            separator == std::string::npos ? "" : current_prefix_.substr(0, separator);
        local_scopes_.clear();
        break_labels_.clear();
        continue_labels_.clear();
        call_targets_.clear();
        diagnosed_expressions_.clear();
        frame_size_ = 0;
        push_scope();

        const int function_id = function_counter_++;
        const std::string placeholder =
            "__FRAME_" + std::to_string(function_id) + "__";
        va_base_placeholder_ = "__VABASE_" + std::to_string(function_id) + "__";
        return_label_ = new_label("return");
        if (!function.is_static) {
            emit(".text");
            emit(".global " + function.symbol);
        } else {
            emit(".text");
        }
        emit(function.symbol + ":");
        emit("    PUSH R7");
        emit("    ADJSP #-" + placeholder);
        emit("    LEA R7, [SP]");

        int integer_argument = 0;
        int fp_argument = 0;
        for (std::size_t index = 0; index < function.decl->parameters.size(); ++index) {
            const Param& parameter = function.decl->parameters[index];
            Local local;
            local.type = resolve_type(parameter.type);
            local.is_reference = local.type &&
                                 (local.type->kind == TypeKind::LValueRef ||
                                  local.type->kind == TypeKind::RValueRef);
            if (local.type && local.type->kind == TypeKind::Array) {
                local.type = decay(local.type);
            }
            local.offset = allocate_local(local.type);
            local.is_parameter = true;
            if (!parameter.name.parts.empty()) {
                local_scopes_.back()[parameter.name.parts.back()] = local;
            }
            if (!local.is_reference && is_fp(local.type)) {
                if (fp_argument < 8) {
                    emit(std::string("    FSTORE.") +
                         (is_float32(local.type) ? "W" : "D") + " F" +
                         std::to_string(fp_argument) + ", [R7 + " +
                         std::to_string(local.offset) + "]");
                    ++fp_argument;
                } else {
                    error(parameter.location,
                          "more than eight FP parameters are not supported yet");
                }
            } else if (integer_argument < 4) {
                emit("    STORE R" + std::to_string(integer_argument) + ", [R7 + " +
                     std::to_string(local.offset) + "]");
                ++integer_argument;
            } else {
                error(parameter.location,
                      "more than four integer parameters are not supported yet");
            }
        }

        if (function.decl->body) {
            gen_statement(*function.decl->body);
        }
        if (is_fp(resolve_type(function.result))) {
            const TypePtr result = resolve_type(function.result);
            const std::string zero = intern_fp_constant(0.0, is_float32(result));
            emit(std::string("    FLOAD.") + (is_float32(result) ? "W" : "D") +
                 " F0, [" + zero + "]");
        } else {
            emit("    MOV R0, #0");
        }
        emit(return_label_ + ":");
        frame_size_ = (frame_size_ + 15) / 16 * 16 + 8;
        emit("    ADJSP #" + std::to_string(frame_size_));
        emit("    POP R7");
        emit("    RET");

        std::string replacement = std::to_string(frame_size_);
        std::size_t position = 0;
        while ((position = text_.find(placeholder, position)) != std::string::npos) {
            text_.replace(position, placeholder.size(), replacement);
            position += replacement.size();
        }
        const std::string va_base = std::to_string(frame_size_ + 8);
        position = 0;
        while ((position = text_.find(va_base_placeholder_, position)) !=
               std::string::npos) {
            text_.replace(position, va_base_placeholder_.size(), va_base);
            position += va_base.size();
        }
    }

    void emit_globals() {
        for (const auto& global : global_storage_) {
            if (!global->defined) {
                continue;
            }
            const int size = std::max(1, type_size(global->type));
            std::string output;
            output += ".global " + global->symbol + "\n";
            output += global->symbol + ":\n";
            if (global->initializer) {
                if (global->type && global->type->kind == TypeKind::Array &&
                    global->initializer->kind == ExprKind::InitializerList) {
                    const int element_size = type_size(global->type->element);
                    const bool fp_element = is_fp(global->type->element);
                    std::size_t index = 0;
                    for (const ExprPtr& element : global->initializer->children) {
                        if (index >= global->type->array_size) {
                            break;
                        }
                        if (fp_element) {
                            double value = 0.0;
                            if (!eval_const_fp(*element, value)) {
                                error(*global->decl,
                                      "global array initializer must be constant");
                            }
                            output += std::string(is_float32(global->type->element)
                                                      ? "    .float "
                                                      : "    .double ") +
                                      fp_literal_text(value,
                                                      is_float32(global->type->element)) +
                                      "\n";
                        } else {
                            std::int64_t value = 0;
                            if (!eval_const(*element, value)) {
                                error(*global->decl,
                                      "global array initializer must be constant");
                                value = 0;
                            }
                            output += element_size == 1
                                          ? "    .byte " +
                                                std::to_string(value & 0xFF) + "\n"
                                          : "    .word " +
                                                std::to_string(value & 0xFFFFFFFFU) +
                                                "\n";
                        }
                        ++index;
                    }
                    while (index < global->type->array_size) {
                        if (fp_element) {
                            output += std::string(is_float32(global->type->element)
                                                      ? "    .float 0.0\n"
                                                      : "    .double 0.0\n");
                        } else {
                            output += element_size == 1 ? "    .byte 0\n"
                                                        : "    .word 0\n";
                        }
                        ++index;
                    }
                } else if (is_fp(global->type)) {
                    double value = 0.0;
                    if (!eval_const_fp(*global->initializer, value)) {
                        error(*global->decl, "global initializer must be constant");
                    }
                    output += std::string(is_float32(global->type) ? "    .float "
                                                                  : "    .double ") +
                              fp_literal_text(value, is_float32(global->type)) + "\n";
                } else {
                    std::int64_t value = 0;
                    if (!eval_const(*global->initializer, value)) {
                        error(*global->decl, "global initializer must be constant");
                        value = 0;
                    }
                    if (size == 1) {
                        output += "    .byte " + std::to_string(value & 0xFF) + "\n";
                    } else {
                        output += "    .word " + std::to_string(value & 0xFFFFFFFFU) + "\n";
                    }
                }
                data_ += output;
            } else {
                output += "    .space " + std::to_string(size) + "\n";
                bss_ += output;
            }
        }
    }

    std::vector<Diagnostic>& diagnostics_;
    std::vector<std::unique_ptr<Function>> function_storage_;
    std::map<std::string, std::vector<Function*>> function_index_;
    std::vector<std::unique_ptr<Global>> global_storage_;
    std::map<std::string, Global*> global_index_;
    std::map<std::string, TypePtr> type_aliases_;
    std::map<std::string, std::set<std::string>> imports_;
    std::map<std::string, std::map<std::string, std::string>> using_aliases_;

    std::vector<std::map<std::string, Local>> local_scopes_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::map<const Expr*, Function*> call_targets_;
    std::set<const Expr*> diagnosed_expressions_;
    std::string current_prefix_;
    std::string return_label_;
    std::string va_base_placeholder_;
    std::string text_;
    std::string rodata_;
    std::string data_;
    std::string bss_;
    int frame_size_ = 0;
    int label_counter_ = 0;
    int string_counter_ = 0;
    int fp_counter_ = 0;
    int anonymous_counter_ = 0;
    int static_counter_ = 0;
    int function_counter_ = 0;
};

} // namespace

std::string generate_assembly(const TranslationUnit& unit,
                              std::vector<Diagnostic>& diagnostics) {
    Generator generator(diagnostics);
    return generator.run(unit);
}

} // namespace kr32::cxx
