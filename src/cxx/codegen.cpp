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

std::string mangle_member(std::string_view class_name, std::string_view name,
                          const std::vector<TypePtr>& parameter_types, bool constructor,
                          bool destructor) {
    const std::vector<std::string> class_parts = split_qualified(class_name);
    std::string result = "_ZN";
    for (const std::string& part : class_parts) {
        result += std::to_string(part.size()) + part;
    }
    if (constructor) {
        result += "C1";
    } else if (destructor) {
        result += "D1";
    } else {
        result += std::to_string(name.size()) + std::string(name);
    }
    result += "E";
    if (parameter_types.empty()) {
        result += "v";
    } else {
        for (const TypePtr& parameter : parameter_types) {
            result += mangle_type(decay(parameter));
        }
    }
    return result;
}

std::string mangle_template_function(
    std::string_view qualified_name, const std::vector<TypePtr>& template_arguments,
    const std::vector<TypePtr>& parameter_types) {
    const std::vector<std::string> parts = split_qualified(qualified_name);
    std::string result = "_Z" + mangle_qualified(parts) + "I";
    for (const TypePtr& argument : template_arguments) {
        result += mangle_type(argument);
    }
    result += "E";
    if (parameter_types.empty()) {
        result += "v";
    } else {
        for (const TypePtr& parameter : parameter_types) {
            result += mangle_type(decay(parameter));
        }
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
        collect_classes(unit.declarations, "");
        layout_classes();
        collect(unit.declarations, "");
        build_vtables();
        if (!diagnostics_.empty()) {
            return {};
        }
        emit_globals();
        for (std::size_t index = 0; index < function_storage_.size(); ++index) {
            Function* function = function_storage_[index].get();
            if (function->defined && emitted_functions_.insert(function).second) {
                emit_function(*function);
            }
        }
        emit_rtti();
        bss_ += ".weak __kr32_exception_value\n";
        bss_ += "__kr32_exception_value:\n    .space 8\n";
        bss_ += ".weak __kr32_exception_type\n";
        bss_ += "__kr32_exception_type:\n    .space 4\n";
        bss_ += ".weak __kr32_exception_pending\n";
        bss_ += "__kr32_exception_pending:\n    .space 4\n";
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
    struct ClassInfo;

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
        bool is_member = false;
        bool is_constructor = false;
        bool is_destructor = false;
        bool is_virtual = false;
        bool is_pure = false;
        bool weak = false;
        int vtable_index = -1;
        std::string member_name;
        ClassInfo* owner = nullptr;
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
        bool needs_init = false;
    };

    struct MemberInfo {
        std::string name;
        TypePtr type;
        int offset = 0;
        bool is_static = false;
    };

    struct ClassInfo {
        const Decl* decl = nullptr;
        std::string qualified;
        int size = 0;
        int align = 1;
        ClassInfo* base = nullptr;
        std::vector<MemberInfo> members;
        std::map<std::string, MemberInfo*> member_index;
        std::map<std::string, std::vector<Function*>> methods;
        std::vector<Function*> constructors;
        Function* destructor = nullptr;
        std::map<std::string, Global*> static_globals;
        std::vector<Function*> vtable;
        std::map<std::string, int> virtual_slots;
        bool polymorphic = false;
        bool abstract = false;
        bool vtables_built = false;
        bool laid_out = false;
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

    ClassInfo* find_class(const std::string& name) {
        const auto exact = class_index_.find(name);
        if (exact != class_index_.end()) {
            return exact->second.get();
        }
        ClassInfo* match = nullptr;
        for (const auto& entry : class_index_) {
            const std::string& qualified = entry.first;
            if (qualified.size() > name.size() + 2 &&
                qualified.compare(qualified.size() - name.size(), name.size(), name) == 0 &&
                qualified[qualified.size() - name.size() - 2] == ':' &&
                qualified[qualified.size() - name.size() - 1] == ':') {
                if (match != nullptr) {
                    return nullptr;
                }
                match = entry.second.get();
            }
        }
        return match;
    }

    ClassInfo* find_class(const Name& name) { return find_class(name.to_string()); }

    ClassInfo* class_of(const TypePtr& raw) {
        const TypePtr type = resolve_type(raw);
        if (!type || type->kind != TypeKind::Named) {
            return nullptr;
        }
        return find_class(type->name);
    }

    MemberInfo* find_member(ClassInfo* info, const std::string& name) {
        for (ClassInfo* current = info; current != nullptr; current = current->base) {
            const auto found = current->member_index.find(name);
            if (found != current->member_index.end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    void collect_classes(const std::vector<DeclPtr>& declarations,
                         const std::string& prefix) {
        for (const DeclPtr& declaration : declarations) {
            if (!declaration) {
                continue;
            }
            if (declaration->kind == DeclKind::Namespace) {
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
                collect_classes(declaration->children, nested);
                continue;
            }
            if (declaration->kind != DeclKind::Class) {
                continue;
            }
            if (declaration->is_template) {
                error(*declaration,
                      "class templates are not supported yet; function templates "
                      "and weak instantiation are implemented");
                continue;
            }
            auto info = std::make_unique<ClassInfo>();
            info->decl = declaration.get();
            info->qualified = qualify(prefix, declaration->name);
            if (class_index_.find(info->qualified) != class_index_.end()) {
                error(*declaration, "class already defined: " + info->qualified);
                continue;
            }
            ClassInfo* raw = info.get();
            class_index_[info->qualified] = std::move(info);
            collect_classes(declaration->children, raw->qualified);
        }
    }

    void layout_classes() {
        for (auto& entry : class_index_) {
            layout_class(*entry.second);
        }
    }

    void layout_class(ClassInfo& info) {
        if (info.laid_out) {
            return;
        }
        info.laid_out = true;
        for (const DeclPtr& child : info.decl->children) {
            if (child && child->kind == DeclKind::Function && child->is_virtual) {
                info.polymorphic = true;
            }
        }
        if (!info.decl->base_names.empty()) {
            ClassInfo* base = find_class(info.decl->base_names.front());
            if (!base) {
                error(*info.decl, "unknown base class: " +
                                       info.decl->base_names.front().to_string());
            } else {
                layout_class(*base);
                info.base = base;
                info.align = base->align;
                info.size = base->size;
                if (base->polymorphic) {
                    info.polymorphic = true;
                } else if (info.polymorphic) {
                    error(*info.decl,
                          "polymorphic class derived from a non-polymorphic base "
                          "is not supported yet");
                }
            }
        }
        if (info.polymorphic && info.base == nullptr) {
            info.size = 4;
            info.align = 4;
        }
        for (const DeclPtr& member : info.decl->children) {
            if (!member || member->kind != DeclKind::Variable || member->is_static) {
                continue;
            }
            TypePtr type = resolve_type(member->type);
            const int alignment = std::max(1, type_align(type));
            const int size = std::max(1, type_size(type));
            info.size = (info.size + alignment - 1) / alignment * alignment;
            MemberInfo field;
            field.name = member->name.to_string();
            field.type = type;
            field.offset = info.size;
            info.size += size;
            info.align = std::max(info.align, alignment);
            info.member_index[field.name] = nullptr;
            info.members.push_back(std::move(field));
        }
        if (info.members.empty() && info.base == nullptr) {
            info.size = 1;
            info.align = 1;
        } else {
            info.size = (info.size + info.align - 1) / info.align * info.align;
        }
        if (info.qualified == "kr32::vec4") {
            info.size = 16;
            info.align = 16;
        } else if (info.qualified == "kr32::mat4") {
            info.size = 64;
            info.align = 16;
        }
        for (MemberInfo& member : info.members) {
            info.member_index[member.name] = &member;
        }
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
        case TypeKind::Named: {
            if (ClassInfo* info = find_class(type->name)) {
                return info->size;
            }
            return 4;
        }
        case TypeKind::Function: return 4;
        }
        return 4;
    }

    int type_align(const TypePtr& type) {
        if (type && type->kind == TypeKind::Named) {
            if (ClassInfo* info = find_class(type->name)) {
                return info->align;
            }
        }
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
                collect_class_members(*declaration,
                                      qualify(prefix, declaration->name));
                break;
            case DeclKind::Enum:
                error(*declaration,
                      "enums are not supported until a later M9 phase");
                break;
            case DeclKind::Parameter:
            case DeclKind::Access:
            case DeclKind::Friend:
            case DeclKind::Unknown:
                break;
            }
        }
    }

    Function* collect_function(const Decl& declaration, const std::string& prefix) {
        if (declaration.is_template) {
            template_functions_[qualify(prefix, declaration.name)].push_back(
                &declaration);
            return nullptr;
        }
        ClassInfo* owner = nullptr;
        std::string base_name = declaration.name.parts.empty()
                                    ? std::string()
                                    : declaration.name.parts.back();
        std::string qualified;
        bool constructor = declaration.is_constructor;
        bool destructor = declaration.is_destructor;
        if (declaration.name.parts.size() > 1) {
            Name class_name;
            class_name.parts.assign(declaration.name.parts.begin(),
                                    declaration.name.parts.end() - 1);
            owner = find_class(class_name);
            if (owner) {
                qualified = owner->qualified + "::" + base_name;
                if (base_name == owner->qualified.substr(
                                     owner->qualified.find_last_of(':') + 1) &&
                    !destructor) {
                    constructor = true;
                }
            } else {
                qualified = declaration.name.to_string();
            }
        } else {
            owner = find_class(prefix);
            qualified = owner ? owner->qualified + "::" + base_name
                              : qualify(prefix, declaration.name);
        }
        if ((constructor || destructor) && !owner) {
            error(declaration, "member function has no class context");
            return nullptr;
        }
        if (declaration.is_operator) {
            error(declaration, "operator overloads are not supported until M9.6");
            return nullptr;
        }

        auto function = std::make_unique<Function>();
        function->decl = &declaration;
        function->qualified = qualified;
        function->extern_c = declaration.is_extern_c;
        function->is_static = declaration.is_static;
        function->defined = declaration.body != nullptr;
        function->variadic = declaration.variadic;
        function->owner = owner;
        function->is_member = owner != nullptr && !declaration.is_static;
        function->is_constructor = constructor;
        function->is_destructor = destructor;
        function->is_virtual = declaration.is_virtual || declaration.is_override;
        function->is_pure = declaration.is_pure;
        function->weak = declaration.is_inline;
        function->member_name = base_name;
        function->result = (constructor || destructor)
                               ? Type::make_builtin(BuiltinKind::Void)
                               : resolve_type(declaration.type);
        for (const Param& parameter : declaration.parameters) {
            function->parameters.push_back(decay(resolve_type(parameter.type)));
        }
        if (owner && (constructor || destructor)) {
            function->symbol = mangle_member(owner->qualified, base_name,
                                             function->parameters, constructor,
                                             destructor);
        } else {
            function->symbol = mangle_function(function->qualified,
                                               function->parameters,
                                               function->extern_c,
                                               function->variadic);
        }
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
                    return existing.get();
                }
                if (function->defined) {
                    existing->defined = true;
                    existing->decl = &declaration;
                }
                return existing.get();
            }
        }
        Function* raw = function.get();
        function_index_[function->qualified].push_back(raw);
        if (owner) {
            if (constructor) {
                owner->constructors.push_back(raw);
            } else if (destructor) {
                owner->destructor = raw;
            } else {
                owner->methods[base_name].push_back(raw);
            }
        }
        function_storage_.push_back(std::move(function));
        return raw;
    }

    void build_vtables() {
        for (auto& entry : class_index_) {
            build_vtable(*entry.second);
        }
    }

    void build_vtable(ClassInfo& info) {
        if (info.vtables_built) {
            return;
        }
        info.vtables_built = true;
        if (info.base != nullptr) {
            build_vtable(*info.base);
            info.vtable = info.base->vtable;
            info.virtual_slots = info.base->virtual_slots;
            info.polymorphic = info.polymorphic || info.base->polymorphic;
        }
        for (auto& entry : info.methods) {
            for (Function* function : entry.second) {
                if (!function->is_virtual || function->is_constructor ||
                    function->is_destructor) {
                    continue;
                }
                const auto slot = info.virtual_slots.find(function->member_name);
                if (slot != info.virtual_slots.end()) {
                    info.vtable[static_cast<std::size_t>(slot->second)] = function;
                    function->vtable_index = slot->second;
                } else {
                    info.virtual_slots[function->member_name] =
                        static_cast<int>(info.vtable.size());
                    function->vtable_index =
                        static_cast<int>(info.vtable.size());
                    info.vtable.push_back(function);
                }
            }
        }
        if (info.destructor != nullptr && info.destructor->is_virtual) {
            const auto slot = info.virtual_slots.find("@dtor");
            if (slot != info.virtual_slots.end()) {
                info.vtable[static_cast<std::size_t>(slot->second)] = info.destructor;
                info.vtable[static_cast<std::size_t>(slot->second) + 1] =
                    info.destructor;
            } else {
                info.virtual_slots["@dtor"] = static_cast<int>(info.vtable.size());
                info.vtable.push_back(info.destructor);
                info.vtable.push_back(info.destructor);
            }
        }
        if (!info.vtable.empty()) {
            info.polymorphic = true;
        }
        for (Function* slot : info.vtable) {
            if (slot != nullptr && slot->is_pure) {
                info.abstract = true;
            }
        }
    }

    void collect_class_members(const Decl& declaration, const std::string& prefix) {
        ClassInfo* info = find_class(prefix);
        if (!info) {
            return;
        }
        for (const DeclPtr& child : declaration.children) {
            if (!child) {
                continue;
            }
            if (child->kind == DeclKind::Function) {
                collect_function(*child, prefix);
            } else if (child->kind == DeclKind::Variable && child->is_static) {
                collect_global(*child, prefix);
                const auto found = global_index_.find(
                    qualify(prefix, child->name));
                if (found != global_index_.end()) {
                    info->static_globals[child->name.to_string()] = found->second;
                }
            } else if (child->kind == DeclKind::Class) {
                collect_class_members(*child,
                                      qualify(prefix, child->name));
            } else if (child->kind == DeclKind::TypeAlias ||
                       child->kind == DeclKind::Using) {
                collect_using(*child, prefix);
            }
        }
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

    void push_scope() {
        local_scopes_.emplace_back();
        local_destructors_.emplace_back();
    }
    void pop_scope() {
        if (!local_destructors_.empty()) {
            for (auto entry = local_destructors_.back().rbegin();
                 entry != local_destructors_.back().rend(); ++entry) {
                emit("    LEA R0, [R7 + " + std::to_string(entry->first) + "]");
                emit("    CALL " + entry->second->symbol);
            }
            local_destructors_.pop_back();
        }
        local_scopes_.pop_back();
    }

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
            if (current_class_ != nullptr && expression.name.parts.size() == 1) {
                if (MemberInfo* member =
                        find_member(current_class_, expression.name.parts[0])) {
                    return member->type;
                }
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
                std::string intrinsic;
                if (matrix_intrinsic_name(expression, intrinsic)) {
                    return matrix_intrinsic_type(intrinsic);
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
        case ExprKind::Typeid:
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
        case ExprKind::This: {
            TypePtr type = Type::make_builtin(BuiltinKind::Void);
            if (current_class_ != nullptr) {
                Name name;
                name.parts = split_qualified(current_class_->qualified);
                type = Type::make_named(name);
            }
            return Type::make_pointer(type);
        }
        case ExprKind::Member: {
            const TypePtr base_type = type_of(*expression.children[0], false);
            TypePtr object_type = base_type;
            if (expression.is_arrow) {
                if (object_type && (object_type->kind == TypeKind::Pointer ||
                                    object_type->kind == TypeKind::LValueRef ||
                                    object_type->kind == TypeKind::RValueRef)) {
                    object_type = object_type->element;
                } else {
                    diagnose_once(expression, "'->' requires a pointer");
                    return Type::make_builtin(BuiltinKind::Int);
                }
            }
            ClassInfo* info = class_of(object_type);
            if (!info) {
                diagnose_once(expression, "member access on a non-class type");
                return Type::make_builtin(BuiltinKind::Int);
            }
            const std::string member_name = expression.name.to_string();
            if (MemberInfo* member = find_member(info, member_name)) {
                return member->type;
            }
            for (ClassInfo* current = info; current != nullptr; current = current->base) {
                const auto global = current->static_globals.find(member_name);
                if (global != current->static_globals.end()) {
                    return global->second->type;
                }
            }
            diagnose_once(expression, "unknown member: " + member_name);
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::New:
            return Type::make_pointer(resolve_type(expression.type));
        case ExprKind::Delete:
            return Type::make_builtin(BuiltinKind::Void);
        case ExprKind::Scope:
        case ExprKind::Lambda:
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

    std::vector<Function*> class_methods(ClassInfo* info,
                                         const std::string& name) {
        for (ClassInfo* current = info; current != nullptr; current = current->base) {
            const auto found = current->methods.find(name);
            if (found != current->methods.end()) {
                return found->second;
            }
        }
        return {};
    }

    bool matrix_intrinsic_name(const Expr& call, std::string& name) {
        if (call.children.empty() || call.children[0]->kind != ExprKind::Name) {
            return false;
        }
        const std::vector<std::string> parts =
            split_qualified(call.children[0]->name.to_string());
        if (parts.empty()) {
            return false;
        }
        name = parts.back();
        static const std::set<std::string> names = {
            "matrix_mul", "matrix_mul_add", "transpose", "transform",
            "dot", "push", "pop", "drop"};
        return names.find(name) != names.end();
    }

    TypePtr matrix_intrinsic_type(const std::string& name) {
        if (name == "transform") {
            ClassInfo* info = find_class("kr32::vec4");
            if (info) {
                Name type_name;
                type_name.parts = split_qualified(info->qualified);
                return Type::make_named(type_name);
            }
        }
        if (name == "dot") {
            return Type::make_builtin(BuiltinKind::Float);
        }
        if (name == "push" || name == "pop" || name == "drop") {
            return Type::make_builtin(BuiltinKind::Void);
        }
        ClassInfo* info = find_class("kr32::mat4");
        if (info) {
            Name type_name;
            type_name.parts = split_qualified(info->qualified);
            return Type::make_named(type_name);
        }
        return Type::make_builtin(BuiltinKind::Int);
    }

    int matrix_argument_address(const Expr& expression) {
        gen_address(expression);
        const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
        return temp;
    }

    TypePtr emit_matrix_intrinsic(const Expr& call, const std::string& name) {
        const TypePtr result = matrix_intrinsic_type(name);
        if (name == "drop") {
            emit("    MDROP");
            emit("    MOV R0, #0");
            return result;
        }
        if (name == "push") {
            const int argument = matrix_argument_address(*call.arguments[0]);
            emit("    LOAD R0, [R7 + " + std::to_string(argument) + "]");
            emit("    MLOAD T0, [R0]");
            emit("    MPUSH T0");
            emit("    MOV R0, #0");
            return result;
        }
        if (name == "pop") {
            const int argument = matrix_argument_address(*call.arguments[0]);
            emit("    MPOP T0");
            emit("    LOAD R0, [R7 + " + std::to_string(argument) + "]");
            emit("    MSTORE T0, [R0]");
            emit("    MOV R0, #0");
            return result;
        }
        if (name == "dot") {
            const int left = matrix_argument_address(*call.arguments[0]);
            const int right = matrix_argument_address(*call.arguments[1]);
            emit("    LOAD R0, [R7 + " + std::to_string(left) + "]");
            emit("    MLOAD T0, [R0]");
            emit("    LOAD R0, [R7 + " + std::to_string(right) + "]");
            emit("    VLOAD T1, [R0]");
            emit("    VDOT F0, T0, T1");
            return result;
        }
        const int result_offset = allocate_local(result);
        if (name == "matrix_mul") {
            const int left = matrix_argument_address(*call.arguments[0]);
            const int right = matrix_argument_address(*call.arguments[1]);
            emit("    LOAD R0, [R7 + " + std::to_string(left) + "]");
            emit("    MLOAD T1, [R0]");
            emit("    LOAD R0, [R7 + " + std::to_string(right) + "]");
            emit("    MLOAD T2, [R0]");
            emit("    MMUL T0, T1, T2");
        } else if (name == "matrix_mul_add") {
            const int left = matrix_argument_address(*call.arguments[0]);
            const int right = matrix_argument_address(*call.arguments[1]);
            const int addend = matrix_argument_address(*call.arguments[2]);
            emit("    LOAD R0, [R7 + " + std::to_string(addend) + "]");
            emit("    MLOAD T0, [R0]");
            emit("    LOAD R0, [R7 + " + std::to_string(left) + "]");
            emit("    MLOAD T1, [R0]");
            emit("    LOAD R0, [R7 + " + std::to_string(right) + "]");
            emit("    MLOAD T2, [R0]");
            emit("    MMAC T0, T1, T2");
        } else if (name == "transpose") {
            const int argument = matrix_argument_address(*call.arguments[0]);
            emit("    LOAD R0, [R7 + " + std::to_string(argument) + "]");
            emit("    MLOAD T1, [R0]");
            emit("    MTRANS T0, T1");
        } else if (name == "transform") {
            const int matrix = matrix_argument_address(*call.arguments[0]);
            const int vector = matrix_argument_address(*call.arguments[1]);
            emit("    LOAD R0, [R7 + " + std::to_string(matrix) + "]");
            emit("    MLOAD T1, [R0]");
            emit("    LOAD R0, [R7 + " + std::to_string(vector) + "]");
            emit("    VLOAD T2, [R0]");
            emit("    MTRANS T3, T2");
            emit("    MVEC T0, T1, T3");
        } else {
            diagnose_once(call, "unsupported matrix intrinsic: " + name);
            return result;
        }
        emit("    LEA R1, [R7 + " + std::to_string(result_offset) + "]");
        if (name == "transform") {
            emit("    MTRANS T3, T0");
            emit("    VSTORE T3, [R1]");
        } else {
            emit("    MSTORE T0, [R1]");
        }
        emit("    LEA R0, [R7 + " + std::to_string(result_offset) + "]");
        return result;
    }

    TypePtr substitute_type(const TypePtr& type,
                            const std::map<std::string, TypePtr>& bindings) {
        if (!type) {
            return type;
        }
        if (type->kind == TypeKind::Named && type->name.parts.size() == 1) {
            const auto found = bindings.find(type->name.parts[0]);
            if (found != bindings.end()) {
                return found->second->clone();
            }
        }
        if (type->kind == TypeKind::Pointer || type->kind == TypeKind::LValueRef ||
            type->kind == TypeKind::RValueRef || type->kind == TypeKind::Array ||
            type->kind == TypeKind::Function) {
            auto copy = std::make_shared<Type>(*type);
            copy->element = substitute_type(type->element, bindings);
            copy->parameters.clear();
            for (const TypePtr& parameter : type->parameters) {
                copy->parameters.push_back(substitute_type(parameter, bindings));
            }
            return copy;
        }
        return type;
    }

    bool deduce_template(std::map<std::string, TypePtr>& bindings,
                         const TypePtr& pattern, const TypePtr& argument) {
        const TypePtr arg = resolve_type(argument);
        if (!pattern || !arg) {
            return true;
        }
        if (pattern->kind == TypeKind::Named && pattern->name.parts.size() == 1) {
            const std::string& name = pattern->name.parts[0];
            const auto found = bindings.find(name);
            if (found == bindings.end()) {
                bindings[name] = arg;
                return true;
            }
            return same_type(found->second, arg);
        }
        if ((pattern->kind == TypeKind::Pointer ||
             pattern->kind == TypeKind::LValueRef ||
             pattern->kind == TypeKind::RValueRef) &&
            arg->kind == pattern->kind) {
            return deduce_template(bindings, pattern->element, arg->element);
        }
        if (pattern->kind == TypeKind::Array && arg->kind == TypeKind::Array) {
            return deduce_template(bindings, pattern->element, arg->element);
        }
        return true;
    }

    Function* instantiate_template(const Decl& declaration,
                                   const std::string& qualified,
                                   const std::vector<TypePtr>& explicit_arguments,
                                   const std::vector<TypePtr>& argument_types) {
        std::map<std::string, TypePtr> bindings;
        std::vector<std::string> parameter_names;
        for (const DeclPtr& parameter : declaration.template_parameters) {
            if (parameter && !parameter->name.parts.empty()) {
                parameter_names.push_back(parameter->name.parts[0]);
            }
        }
        for (std::size_t index = 0;
             index < explicit_arguments.size() && index < parameter_names.size();
             ++index) {
            bindings[parameter_names[index]] = explicit_arguments[index];
        }
        for (std::size_t index = 0;
             index < declaration.parameters.size() && index < argument_types.size();
             ++index) {
            if (!deduce_template(bindings, declaration.parameters[index].type,
                                 argument_types[index])) {
                return nullptr;
            }
        }
        std::vector<TypePtr> template_arguments;
        for (const std::string& name : parameter_names) {
            const auto found = bindings.find(name);
            if (found == bindings.end()) {
                return nullptr;
            }
            template_arguments.push_back(found->second);
        }
        auto clone = std::make_shared<Decl>();
        clone->kind = DeclKind::Function;
        clone->location = declaration.location;
        clone->name = declaration.name;
        clone->type = substitute_type(declaration.type, bindings);
        clone->body = declaration.body;
        clone->is_inline = declaration.is_inline;
        clone->is_extern_c = declaration.is_extern_c;
        clone->is_static = declaration.is_static;
        clone->variadic = declaration.variadic;
        for (const Param& parameter : declaration.parameters) {
            Param copy;
            copy.type = substitute_type(parameter.type, bindings);
            copy.name = parameter.name;
            copy.location = parameter.location;
            copy.variadic = parameter.variadic;
            clone->parameters.push_back(std::move(copy));
        }
        std::vector<TypePtr> parameter_types;
        for (const Param& parameter : clone->parameters) {
            parameter_types.push_back(decay(resolve_type(parameter.type)));
        }
        const std::string symbol =
            mangle_template_function(qualified, template_arguments,
                                     parameter_types);
        for (const auto& existing : function_storage_) {
            if (existing->symbol == symbol) {
                return existing.get();
            }
        }
        auto function = std::make_unique<Function>();
        function->decl = clone.get();
        function->qualified = qualified;
        function->symbol = symbol;
        function->parameters = parameter_types;
        function->result = resolve_type(clone->type);
        function->defined = clone->body != nullptr;
        function->weak = true;
        Function* raw = function.get();
        template_instantiation_decls_.push_back(clone);
        function_index_[qualified].push_back(raw);
        function_storage_.push_back(std::move(function));
        return raw;
    }

    Function* try_template_call(const Expr& call,
                                const std::vector<TypePtr>& argument_types) {
        if (call.children.empty() || call.children[0]->kind != ExprKind::Name) {
            return nullptr;
        }
        const Name& name = call.children[0]->name;
        std::vector<const Decl*> templates;
        const std::vector<std::string> keys = candidate_keys(name, current_prefix_);
        for (const std::string& key : keys) {
            const auto found = template_functions_.find(key);
            if (found != template_functions_.end()) {
                templates.insert(templates.end(), found->second.begin(),
                                 found->second.end());
            }
        }
        for (const Decl* declaration : templates) {
            const std::string qualified = qualify(current_prefix_, declaration->name);
            if (Function* function = instantiate_template(
                    *declaration, qualified,
                    call.children[0]->template_type_arguments, argument_types)) {
                return function;
            }
        }
        return nullptr;
    }

    Function* resolve_call(const Expr& call) {
        const auto cached = call_targets_.find(&call);
        if (cached != call_targets_.end()) {
            return cached->second;
        }
        call_targets_[&call] = nullptr;
        std::vector<Function*> candidates;
        if (!call.children.empty() &&
            call.children[0]->kind == ExprKind::Name) {
            const Name& name = call.children[0]->name;
            candidates = lookup_functions(name, current_prefix_);
            if (candidates.empty() && current_class_ != nullptr &&
                name.parts.size() == 1) {
                candidates = class_methods(current_class_, name.parts[0]);
            }
        } else if (!call.children.empty() &&
                   call.children[0]->kind == ExprKind::Member) {
            const Expr& member = *call.children[0];
            TypePtr object_type = type_of(*member.children[0], false);
            if (member.is_arrow && object_type &&
                (object_type->kind == TypeKind::Pointer ||
                 object_type->kind == TypeKind::LValueRef ||
                 object_type->kind == TypeKind::RValueRef)) {
                object_type = object_type->element;
            }
            ClassInfo* info = class_of(object_type);
            if (info) {
                candidates = class_methods(info, member.name.to_string());
            }
        } else {
            diagnose_once(call, "unsupported call target");
            return nullptr;
        }
        if (candidates.empty()) {
            // Template candidates are instantiated after argument types are
            // known, below.
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
        if (candidates.empty()) {
            if (Function* instantiated = try_template_call(call, argument_types)) {
                candidates.push_back(instantiated);
            }
        }
        if (candidates.empty()) {
            diagnose_once(call, "unknown function");
            return nullptr;
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
            diagnose_once(call, "no matching function for call");
            return nullptr;
        }
        if (ambiguous) {
            diagnose_once(call, "ambiguous function call");
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
        if (expression.kind == ExprKind::Member) {
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
            if (current_class_ != nullptr && expression.name.parts.size() == 1) {
                if (MemberInfo* member =
                        find_member(current_class_, expression.name.parts[0])) {
                    if (this_offset_ < 0) {
                        diagnose_once(expression, "member used outside a member function");
                        emit("    MOV R0, #0");
                        return;
                    }
                    emit("    LOAD R0, [R7 + " + std::to_string(this_offset_) + "]");
                    if (member->offset != 0) {
                        emit("    ADD R0, R0, #" + std::to_string(member->offset));
                    }
                    return;
                }
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
        if (expression.kind == ExprKind::This) {
            if (this_offset_ < 0) {
                diagnose_once(expression, "'this' is only valid in a member function");
                emit("    MOV R0, #0");
            } else {
                emit("    LOAD R0, [R7 + " + std::to_string(this_offset_) + "]");
            }
            return;
        }
        if (expression.kind == ExprKind::Member) {
            const std::string member_name = expression.name.to_string();
            const TypePtr base_type = type_of(*expression.children[0], false);
            TypePtr object_type = base_type;
            if (expression.is_arrow && object_type &&
                (object_type->kind == TypeKind::Pointer ||
                 object_type->kind == TypeKind::LValueRef ||
                 object_type->kind == TypeKind::RValueRef)) {
                object_type = object_type->element;
            }
            ClassInfo* info = class_of(object_type);
            if (!info) {
                diagnose_once(expression, "member access on a non-class type");
                emit("    MOV R0, #0");
                return;
            }
            if (MemberInfo* member = find_member(info, member_name)) {
                if (expression.is_arrow) {
                    gen_expr(*expression.children[0]);
                } else {
                    gen_address(*expression.children[0]);
                }
                if (member->offset != 0) {
                    emit("    ADD R0, R0, #" + std::to_string(member->offset));
                }
                return;
            }
            for (ClassInfo* current = info; current != nullptr; current = current->base) {
                const auto global = current->static_globals.find(member_name);
                if (global != current->static_globals.end()) {
                    emit("    LEA R0, [" + global->second->symbol + "]");
                    return;
                }
            }
            diagnose_once(expression, "unknown member: " + member_name);
            emit("    MOV R0, #0");
            return;
        }
        diagnose_once(expression, "expression is not an lvalue in M9.3");
        emit("    MOV R0, #0");
    }

    TypePtr gen_dynamic_cast(const Expr& expression, const TypePtr& target) {
        if (!target || target->kind != TypeKind::Pointer) {
            diagnose_once(expression,
                          "reference dynamic_cast requires exception support (M9.9)");
            emit("    MOV R0, #0");
            return target;
        }
        ClassInfo* target_class = class_of(target->element);
        if (!target_class) {
            diagnose_once(expression, "dynamic_cast target must be a class pointer");
            emit("    MOV R0, #0");
            return target;
        }
        gen_expr(*expression.children[0]);
        const std::string null_label = new_label("dcast_null");
        const std::string loop_label = new_label("dcast_loop");
        const std::string success_label = new_label("dcast_ok");
        const std::string end_label = new_label("dcast_end");
        emit("    CMP R0, #0");
        emit("    JZ " + null_label);
        emit("    MOV R1, R0");
        emit("    LOAD R2, [R1]");
        emit("    LOAD R2, [R2 - 8]");
        emit("    LEA R3, [" + typeinfo_symbol(target_class) + "]");
        emit(loop_label + ":");
        emit("    CMP R2, R3");
        emit("    JZ " + success_label);
        emit("    LOAD R2, [R2 + 8]");
        emit("    CMP R2, #0");
        emit("    JNZ " + loop_label);
        emit(null_label + ":");
        emit("    MOV R0, #0");
        emit("    JMP " + end_label);
        emit(success_label + ":");
        emit("    MOV R0, R1");
        emit(end_label + ":");
        return target;
    }

    void construct_object_at(ClassInfo* info, int address_temp,
                             const std::vector<ExprPtr>& arguments) {
        if (info->abstract) {
            error(*info->decl, "cannot instantiate abstract class: " + info->qualified);
            return;
        }
        Function* constructor = find_constructor(info, arguments.size());
        if (!constructor) {
            if (!arguments.empty()) {
                error(*info->decl, "no matching constructor for " + info->qualified);
                return;
            }
            emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
            emit("    MOV R1, #0");
            emit("    MOV R2, #" + std::to_string(info->size));
            emit("    CALL memset");
            return;
        }
        std::vector<int> integer_temps;
        std::vector<std::pair<int, TypePtr>> fp_temps;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const TypePtr parameter = resolve_type(constructor->parameters[index]);
            if (parameter && (parameter->kind == TypeKind::LValueRef ||
                              parameter->kind == TypeKind::RValueRef)) {
                gen_address(*arguments[index]);
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                integer_temps.push_back(temp);
            } else if (is_fp(parameter)) {
                gen_as_fp(*arguments[index], parameter);
                const int temp = allocate_local(parameter);
                emit(std::string("    FSTORE.") +
                     (is_float32(parameter) ? "W" : "D") + " F0, [R7 + " +
                     std::to_string(temp) + "]");
                fp_temps.emplace_back(temp, parameter);
            } else {
                gen_expr(*arguments[index]);
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                integer_temps.push_back(temp);
            }
        }
        emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
        for (std::size_t index = 0; index < integer_temps.size(); ++index) {
            emit("    LOAD R" + std::to_string(index + 1) + ", [R7 + " +
                 std::to_string(integer_temps[index]) + "]");
        }
        for (std::size_t index = 0; index < fp_temps.size(); ++index) {
            emit(std::string("    FLOAD.") +
                 (is_float32(fp_temps[index].second) ? "W" : "D") + " F" +
                 std::to_string(index) + ", [R7 + " +
                 std::to_string(fp_temps[index].first) + "]");
        }
        emit("    CALL " + constructor->symbol);
        emit_pending_check();
    }

    TypePtr gen_new(const Expr& expression) {
        const TypePtr pointee = resolve_type(expression.type);
        ClassInfo* info = class_of(pointee);
        const int pointer_temp =
            allocate_local(Type::make_pointer(pointee));
        if (!expression.children.empty()) {
            gen_expr(*expression.children[0]);
            const int element_size = std::max(1, type_size(pointee));
            if (element_size != 1) {
                emit("    MUL R0, R0, #" + std::to_string(element_size));
            }
            emit("    CALL malloc");
            emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
            if (info != nullptr && find_constructor(info, 0) != nullptr) {
                diagnose_once(expression,
                              "new[] of classes with constructors is not supported yet");
            }
        } else {
            emit("    MOV R0, #" + std::to_string(std::max(1, type_size(pointee))));
            emit("    CALL malloc");
            emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
            if (info != nullptr) {
                construct_object_at(info, pointer_temp, expression.arguments);
            } else if (expression.arguments.size() == 1) {
                gen_expr(*expression.arguments[0]);
                emit("    LOAD R1, [R7 + " + std::to_string(pointer_temp) + "]");
                store_value(pointee);
            }
        }
        emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
        return Type::make_pointer(pointee);
    }

    TypePtr gen_delete(const Expr& expression) {
        const TypePtr pointer_type = type_of(*expression.children[0]);
        TypePtr pointee = pointer_type;
        if (pointee && (pointee->kind == TypeKind::Pointer ||
                        pointee->kind == TypeKind::LValueRef ||
                        pointee->kind == TypeKind::RValueRef)) {
            pointee = pointee->element;
        }
        gen_expr(*expression.children[0]);
        const int pointer_temp =
            allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
        emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
        if (expression.text != "[]") {
            if (ClassInfo* info = class_of(pointee)) {
                if (Function* destructor = find_destructor(info)) {
                    emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                    emit("    CALL " + destructor->symbol);
                }
            }
        }
        emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
        emit("    CALL free");
        emit("    MOV R0, #0");
        return Type::make_builtin(BuiltinKind::Void);
    }

    std::string exception_type_key(const TypePtr& type) {
        return mangle_type(decay(resolve_type(type)));
    }

    int exception_type_id(const TypePtr& type) {
        const std::string key = exception_type_key(type);
        const auto found = exception_type_ids_.find(key);
        if (found != exception_type_ids_.end()) {
            return found->second;
        }
        const int id = static_cast<int>(exception_type_ids_.size()) + 1;
        exception_type_ids_[key] = id;
        return id;
    }

    void emit_destructors_from(std::size_t depth) {
        for (std::size_t index = local_destructors_.size(); index > depth; --index) {
            auto& scope = local_destructors_[index - 1];
            for (auto entry = scope.rbegin(); entry != scope.rend(); ++entry) {
                emit("    LEA R0, [R7 + " + std::to_string(entry->first) + "]");
                emit("    CALL " + entry->second->symbol);
            }
        }
    }

    void emit_pending_check() {
        if (return_label_.empty()) {
            return;
        }
        const std::string clear_label = new_label("noexc");
        emit("    LOAD R2, [__kr32_exception_pending]");
        emit("    CMP R2, #0");
        emit("    JZ " + clear_label);
        if (!exception_handlers_.empty()) {
            const ExceptionHandler handler = exception_handlers_.back();
            emit_destructors_from(handler.scope_depth);
            emit("    JMP " + handler.label);
        } else {
            emit_destructors_from(0);
            if (current_function_ != nullptr &&
                current_function_->qualified == "main") {
                emit("    MOV R0, #1");
                emit("    CALL exit");
            } else {
                emit("    JMP " + return_label_);
            }
        }
        emit(clear_label + ":");
    }

    void gen_throw(const Stmt& statement) {
        uses_exceptions_ = true;
        if (statement.expression) {
            const TypePtr type = type_of(*statement.expression);
            gen_expr(*statement.expression);
            if (is_fp(type)) {
                emit(std::string("    FSTORE.") +
                     (is_float32(type) ? "W" : "D") +
                     " F0, [__kr32_exception_value]");
            } else {
                emit("    STORE R0, [__kr32_exception_value]");
            }
            emit("    MOV R0, #" + std::to_string(exception_type_id(type)));
            emit("    STORE R0, [__kr32_exception_type]");
        }
        emit("    MOV R0, #1");
        emit("    STORE R0, [__kr32_exception_pending]");
        if (exception_handlers_.empty()) {
            emit_destructors_from(0);
            if (current_function_ != nullptr &&
                current_function_->qualified == "main") {
                emit("    MOV R0, #1");
                emit("    CALL exit");
            } else {
                emit("    JMP " + return_label_);
            }
            return;
        }
        const ExceptionHandler handler = exception_handlers_.back();
        emit_destructors_from(handler.scope_depth);
        emit("    JMP " + handler.label);
    }

    void gen_try(const Stmt& statement) {
        uses_exceptions_ = true;
        const std::string catch_label = new_label("catch");
        const std::string end_label = new_label("try_end");
        ExceptionHandler handler;
        handler.label = catch_label;
        handler.scope_depth = local_destructors_.size();
        exception_handlers_.push_back(handler);
        if (statement.body) {
            gen_statement(*statement.body);
        }
        emit("    JMP " + end_label);
        emit(catch_label + ":");
        exception_handlers_.pop_back();
        for (const CatchHandler& clause : statement.handlers) {
            const std::string next_label = new_label("catch_next");
            if (clause.parameter && clause.parameter->type) {
                const TypePtr type = resolve_type(clause.parameter->type);
                const int id = exception_type_id(type);
                emit("    LOAD R0, [__kr32_exception_type]");
                emit("    CMP R0, #" + std::to_string(id));
                emit("    JNZ " + next_label);
            }
            emit("    MOV R0, #0");
            emit("    STORE R0, [__kr32_exception_pending]");
            push_scope();
            if (clause.parameter && clause.parameter->type) {
                const TypePtr type = resolve_type(clause.parameter->type);
                Local local;
                local.type = type;
                local.offset = allocate_local(type);
                if (!clause.parameter->name.parts.empty()) {
                    local_scopes_.back()[clause.parameter->name.parts.back()] = local;
                }
                if (is_fp(type)) {
                    emit(std::string("    FLOAD.") +
                         (is_float32(type) ? "W" : "D") +
                         " F0, [__kr32_exception_value]");
                    emit(std::string("    FSTORE.") +
                         (is_float32(type) ? "W" : "D") + " F0, [R7 + " +
                         std::to_string(local.offset) + "]");
                } else {
                    emit("    LOAD R0, [__kr32_exception_value]");
                    emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
                }
            }
            if (clause.body) {
                gen_statement(*clause.body);
            }
            pop_scope();
            emit("    JMP " + end_label);
            emit(next_label + ":");
        }
        if (exception_handlers_.empty()) {
            emit("    MOV R0, #1");
            emit("    CALL exit");
        } else {
            const ExceptionHandler outer = exception_handlers_.back();
            emit_destructors_from(outer.scope_depth);
            emit("    JMP " + outer.label);
        }
        emit(end_label + ":");
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
            if (current_class_ != nullptr && expression.name.parts.size() == 1) {
                if (MemberInfo* member =
                        find_member(current_class_, expression.name.parts[0])) {
                    gen_address(expression);
                    if (member->type && member->type->kind == TypeKind::Array) {
                        return member->type;
                    }
                    if (is_fp(member->type)) {
                        load_fp(member->type);
                    } else {
                        load_value(member->type);
                    }
                    return member->type;
                }
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
            if (expression.text == "dynamic_cast") {
                return gen_dynamic_cast(expression, target);
            }
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
        case ExprKind::Typeid: {
            if (expression.type) {
                TypePtr type = resolve_type(expression.type);
                if (type && (type->kind == TypeKind::Pointer ||
                             type->kind == TypeKind::LValueRef ||
                             type->kind == TypeKind::RValueRef)) {
                    type = type->element;
                }
                ClassInfo* info = class_of(type);
                if (info == nullptr) {
                    emit("    MOV R0, #0");
                } else {
                    emit("    LEA R0, [" + typeinfo_symbol(info) + "]");
                }
                return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
            }
            TypePtr type = type_of(*expression.children[0], false);
            TypePtr object = type;
            if (object && (object->kind == TypeKind::Pointer ||
                           object->kind == TypeKind::LValueRef ||
                           object->kind == TypeKind::RValueRef)) {
                object = object->element;
            }
            ClassInfo* info = class_of(object);
            if (info == nullptr) {
                emit("    MOV R0, #0");
                return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
            }
            if (info->polymorphic) {
                if (type && (type->kind == TypeKind::Pointer ||
                             type->kind == TypeKind::LValueRef ||
                             type->kind == TypeKind::RValueRef)) {
                    gen_expr(*expression.children[0]);
                } else {
                    gen_address(*expression.children[0]);
                }
                emit("    LOAD R1, [R0]");
                emit("    LOAD R0, [R1 - 8]");
            } else {
                emit("    LEA R0, [" + typeinfo_symbol(info) + "]");
            }
            return Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
        }
        case ExprKind::This:
            gen_address(expression);
            return type_of(expression);
        case ExprKind::Member: {
            gen_address(expression);
            const TypePtr type = type_of(expression);
            if (type && type->kind == TypeKind::Array) {
                return type;
            }
            if (is_fp(type)) {
                load_fp(type);
            } else {
                load_value(type);
            }
            return type;
        }
        case ExprKind::New:
            return gen_new(expression);
        case ExprKind::Delete:
            return gen_delete(expression);
        case ExprKind::Scope:
        case ExprKind::Lambda:
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
        if (ClassInfo* info = class_of(lhs_type)) {
            gen_address(*expression.children[0]);
            const int destination =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(destination) + "]");
            if (expression.children[1]->kind == ExprKind::Call) {
                gen_expr(*expression.children[1]);
            } else {
                gen_address(*expression.children[1]);
            }
            emit("    MOV R1, R0");
            emit("    LOAD R0, [R7 + " + std::to_string(destination) + "]");
            emit("    MOV R2, #" + std::to_string(info->size));
            emit("    CALL memcpy");
            return lhs_type;
        }
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
        std::string intrinsic;
        if (matrix_intrinsic_name(expression, intrinsic)) {
            return emit_matrix_intrinsic(expression, intrinsic);
        }
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
        const bool member_call = function->is_member;
        bool virtual_dispatch = false;
        if (function->is_virtual && function->vtable_index >= 0) {
            if (expression.children[0]->kind == ExprKind::Member) {
                const Expr& member = *expression.children[0];
                if (member.is_arrow) {
                    virtual_dispatch = true;
                } else {
                    const TypePtr object_type = type_of(*member.children[0], false);
                    virtual_dispatch =
                        object_type && (object_type->kind == TypeKind::Pointer ||
                                        object_type->kind == TypeKind::LValueRef ||
                                        object_type->kind == TypeKind::RValueRef);
                }
            } else if (current_class_ != nullptr) {
                virtual_dispatch = true;
            }
        }
        int this_temp = -1;
        if (member_call) {
            this_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            if (expression.children[0]->kind == ExprKind::Member) {
                const Expr& member = *expression.children[0];
                if (member.is_arrow) {
                    gen_expr(*member.children[0]);
                } else {
                    gen_address(*member.children[0]);
                }
            } else if (this_offset_ >= 0) {
                emit("    LOAD R0, [R7 + " + std::to_string(this_offset_) + "]");
            } else {
                diagnose_once(expression, "member call outside a member function");
                emit("    MOV R0, #0");
            }
            emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        }
        struct ArgumentSlot {
            int offset = 0;
            bool fp = false;
            TypePtr type;
        };
        std::vector<ArgumentSlot> slots;
        std::vector<int> stack_temps;
        int integer_argument = member_call ? 1 : 0;
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
        int integer_register = member_call ? 1 : 0;
        int fp_register = 0;
        if (member_call) {
            emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        }
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
        if (virtual_dispatch) {
            emit("    LOAD R4, [R0]");
            emit("    LOAD R4, [R4 + " +
                 std::to_string(function->vtable_index * 4) + "]");
            emit("    CALL R4");
        } else {
            emit("    CALL " + function->symbol);
        }
        if (stack_bytes != 0) {
            emit("    ADJSP #" + std::to_string(stack_bytes));
        }
        if (!function->is_destructor) {
            emit_pending_check();
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
        case StmtKind::Return: {
            TypePtr result_type;
            int value_temp = -1;
            if (statement.expression) {
                result_type = type_of(*statement.expression);
                gen_expr(*statement.expression);
                value_temp = allocate_local(result_type);
                if (is_fp(result_type)) {
                    emit(std::string("    FSTORE.") +
                         (is_float32(result_type) ? "W" : "D") + " F0, [R7 + " +
                         std::to_string(value_temp) + "]");
                } else {
                    emit("    STORE R0, [R7 + " + std::to_string(value_temp) + "]");
                }
            } else {
                emit("    MOV R0, #0");
            }
            emit_all_local_destructors();
            if (value_temp >= 0) {
                if (is_fp(result_type)) {
                    emit(std::string("    FLOAD.") +
                         (is_float32(result_type) ? "W" : "D") + " F0, [R7 + " +
                         std::to_string(value_temp) + "]");
                } else {
                    emit("    LOAD R0, [R7 + " + std::to_string(value_temp) + "]");
                }
            }
            emit("    JMP " + return_label_);
            return;
        }
        case StmtKind::Switch:
        case StmtKind::Case:
        case StmtKind::Default:
            error(statement.location, "switch statements are not supported until a later M9 phase");
            return;
        case StmtKind::RangeFor:
            error(statement.location, "range-for is not supported until M9.7");
            return;
        case StmtKind::Throw:
            gen_throw(statement);
            return;
        case StmtKind::Try:
            gen_try(statement);
            return;
        case StmtKind::Goto:
            error(statement.location, "goto is not supported in M9.3");
            return;
        case StmtKind::Label:
        case StmtKind::Unknown:
            return;
        }
    }

    Function* find_constructor(ClassInfo* info, std::size_t argument_count) {
        for (ClassInfo* current = info; current != nullptr; current = current->base) {
            for (Function* constructor : current->constructors) {
                if (constructor->parameters.size() == argument_count) {
                    return constructor;
                }
            }
        }
        return nullptr;
    }

    void zero_object(int offset, int size) {
        for (int cursor = 0; cursor < size; cursor += 4) {
            emit("    MOV R0, #0");
            emit("    STORE R0, [R7 + " + std::to_string(offset + cursor) + "]");
        }
    }

    void construct_object(ClassInfo* info, int offset,
                          const std::vector<ExprPtr>& arguments) {
        if (info->abstract) {
            error(*info->decl, "cannot instantiate abstract class: " + info->qualified);
            return;
        }
        Function* constructor = find_constructor(info, arguments.size());
        if (!constructor) {
            if (arguments.empty()) {
                zero_object(offset, info->size);
                return;
            }
            error(*info->decl, "no matching constructor for " + info->qualified);
            return;
        }
        std::vector<int> integer_temps;
        std::vector<std::pair<int, TypePtr>> fp_temps;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const TypePtr parameter = resolve_type(constructor->parameters[index]);
            if (parameter && (parameter->kind == TypeKind::LValueRef ||
                              parameter->kind == TypeKind::RValueRef)) {
                gen_address(*arguments[index]);
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                integer_temps.push_back(temp);
            } else if (is_fp(parameter)) {
                gen_as_fp(*arguments[index], parameter);
                const int temp = allocate_local(parameter);
                emit(std::string("    FSTORE.") +
                     (is_float32(parameter) ? "W" : "D") + " F0, [R7 + " +
                     std::to_string(temp) + "]");
                fp_temps.emplace_back(temp, parameter);
            } else {
                gen_expr(*arguments[index]);
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                integer_temps.push_back(temp);
            }
        }
        emit("    LEA R0, [R7 + " + std::to_string(offset) + "]");
        for (std::size_t index = 0; index < integer_temps.size(); ++index) {
            emit("    LOAD R" + std::to_string(index + 1) + ", [R7 + " +
                 std::to_string(integer_temps[index]) + "]");
        }
        for (std::size_t index = 0; index < fp_temps.size(); ++index) {
            emit(std::string("    FLOAD.") +
                 (is_float32(fp_temps[index].second) ? "W" : "D") + " F" +
                 std::to_string(index) + ", [R7 + " +
                 std::to_string(fp_temps[index].first) + "]");
        }
        emit("    CALL " + constructor->symbol);
        emit_pending_check();
    }

    Function* find_destructor(ClassInfo* info) {
        for (ClassInfo* current = info; current != nullptr; current = current->base) {
            if (current->destructor != nullptr) {
                return current->destructor;
            }
        }
        return nullptr;
    }

    const Initializer* find_initializer(const Decl& declaration,
                                        const std::string& name) {
        for (const Initializer& initializer : declaration.initializers) {
            if (initializer.name.to_string() == name) {
                return &initializer;
            }
        }
        return nullptr;
    }

    std::string typeinfo_symbol(ClassInfo* info) {
        if (info == nullptr) {
            return "0";
        }
        return "_ZTI" + mangle_qualified(split_qualified(info->qualified));
    }

    void set_vptr(ClassInfo& info) {
        if (!info.polymorphic) {
            return;
        }
        const std::vector<std::string> parts = split_qualified(info.qualified);
        const std::string vtable_symbol = "_ZTV" + mangle_qualified(parts);
        emit("    LOAD R1, [R7 + " + std::to_string(this_offset_) + "]");
        emit("    LEA R0, [" + vtable_symbol + "]");
        emit("    ADD R0, R0, #8");
        emit("    STORE R0, [R1]");
    }

    void emit_constructor_prefix(Function& function) {
        ClassInfo* info = function.owner;
        if (!info) {
            return;
        }
        if (info->base != nullptr) {
            if (Function* base_constructor = find_constructor(info->base, 0)) {
                emit("    LOAD R0, [R7 + " + std::to_string(this_offset_) + "]");
                emit("    CALL " + base_constructor->symbol);
                emit_pending_check();
            }
        }
        set_vptr(*info);
        for (const DeclPtr& child : info->decl->children) {
            if (!child || child->kind != DeclKind::Variable || child->is_static) {
                continue;
            }
            const std::string name = child->name.to_string();
            MemberInfo* member = find_member(info, name);
            if (!member) {
                continue;
            }
            const Initializer* initializer =
                find_initializer(*function.decl, name);
            ClassInfo* member_class = class_of(member->type);
            if (member_class) {
                const std::vector<ExprPtr> empty;
                const std::vector<ExprPtr>* arguments = &empty;
                if (initializer != nullptr) {
                    if (initializer->arguments.size() == 1 &&
                        initializer->arguments[0]->kind == ExprKind::InitializerList) {
                        arguments = &initializer->arguments[0]->children;
                    } else {
                        arguments = &initializer->arguments;
                    }
                }
                construct_object(member_class, this_offset_ + member->offset,
                                 *arguments);
            } else if (initializer != nullptr &&
                       !initializer->arguments.empty()) {
                if (is_fp(member->type)) {
                    gen_as_fp(*initializer->arguments[0], member->type);
                    emit("    LOAD R1, [R7 + " + std::to_string(this_offset_) + "]");
                    if (member->offset != 0) {
                        emit("    ADD R1, R1, #" + std::to_string(member->offset));
                    }
                    emit(std::string("    FSTORE.") +
                         (is_float32(member->type) ? "W" : "D") + " F0, [R1]");
                } else {
                    gen_expr(*initializer->arguments[0]);
                    emit("    LOAD R1, [R7 + " + std::to_string(this_offset_) + "]");
                    if (member->offset != 0) {
                        emit("    ADD R1, R1, #" + std::to_string(member->offset));
                    }
                    store_value(member->type);
                }
            }
        }
    }

    void emit_destructor_suffix(Function& function) {
        ClassInfo* info = function.owner;
        if (!info) {
            return;
        }
        for (auto member = info->members.rbegin(); member != info->members.rend();
             ++member) {
            ClassInfo* member_class = class_of(member->type);
            if (!member_class) {
                continue;
            }
            if (Function* destructor = find_destructor(member_class)) {
                emit("    LEA R0, [R7 + " +
                     std::to_string(this_offset_ + member->offset) + "]");
                emit("    CALL " + destructor->symbol);
            }
        }
        if (info->base != nullptr) {
            if (Function* base_destructor = find_destructor(info->base)) {
                emit("    LOAD R0, [R7 + " + std::to_string(this_offset_) + "]");
                emit("    CALL " + base_destructor->symbol);
            }
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
        if (ClassInfo* info = class_of(type)) {
            const std::vector<ExprPtr> empty;
            const std::vector<ExprPtr>* arguments = &empty;
            if (declaration.initializer &&
                declaration.initializer->kind == ExprKind::Call) {
                std::string intrinsic;
                if (matrix_intrinsic_name(*declaration.initializer, intrinsic)) {
                    gen_expr(*declaration.initializer);
                    emit("    MOV R1, R0");
                    emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    MOV R2, #" + std::to_string(info->size));
                    emit("    CALL memcpy");
                    return;
                }
            }
            if (declaration.initializer) {
                if (declaration.initializer->kind == ExprKind::Call) {
                    arguments = &declaration.initializer->arguments;
                } else if (declaration.initializer->kind ==
                           ExprKind::InitializerList) {
                    arguments = &declaration.initializer->children;
                } else {
                    gen_address(*declaration.initializer);
                    emit("    MOV R1, R0");
                    emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    MOV R2, #" + std::to_string(info->size));
                    emit("    CALL memcpy");
                    if (Function* destructor = find_destructor(info)) {
                        local_destructors_.back().push_back({local.offset, destructor});
                    }
                    return;
                }
            }
            construct_object(info, local.offset, *arguments);
            if (Function* destructor = find_destructor(info)) {
                local_destructors_.back().push_back({local.offset, destructor});
            }
            return;
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
            const Expr* initializer = declaration.initializer.get();
            if (initializer->kind == ExprKind::Call) {
                if (initializer->arguments.size() == 1) {
                    initializer = initializer->arguments[0].get();
                } else if (initializer->arguments.empty()) {
                    zero_object(local.offset, std::max(1, type_size(type)));
                    return;
                } else {
                    error(declaration, "invalid scalar initializer");
                    return;
                }
            }
            if (is_fp(type)) {
                gen_as_fp(*initializer, type);
                emit(std::string("    FSTORE.") + (is_float32(type) ? "W" : "D") +
                     " F0, [R7 + " + std::to_string(local.offset) + "]");
                return;
            }
            gen_expr(*initializer);
            emit("    LEA R1, [R7 + " + std::to_string(local.offset) + "]");
            store_value(type);
        }
    }

    void emit_all_local_destructors() {
        for (auto scope = local_destructors_.rbegin();
             scope != local_destructors_.rend(); ++scope) {
            for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry) {
                emit("    LEA R0, [R7 + " + std::to_string(entry->first) + "]");
                emit("    CALL " + entry->second->symbol);
            }
        }
    }

    void emit_function(Function& function) {
        if (!function.decl || !function.decl->body) {
            return;
        }
        current_function_ = &function;
        current_prefix_ = function.qualified;
        const std::size_t separator = current_prefix_.rfind("::");
        current_prefix_ =
            separator == std::string::npos ? "" : current_prefix_.substr(0, separator);
        local_scopes_.clear();
        exception_handlers_.clear();
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
            emit(function.weak ? ".weak " + function.symbol
                               : ".global " + function.symbol);
        } else {
            emit(".text");
        }
        emit(function.symbol + ":");
        emit("    PUSH R7");
        emit("    ADJSP #-" + placeholder);
        emit("    LEA R7, [SP]");

        current_class_ = function.owner;
        this_offset_ = -1;
        int integer_argument = 0;
        int fp_argument = 0;
        if (function.is_member) {
            Local local;
            local.type = Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
            local.offset = allocate_local(local.type);
            local.is_parameter = true;
            emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
            this_offset_ = local.offset;
            integer_argument = 1;
        }
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
            if (function.is_constructor) {
                emit_constructor_prefix(function);
            } else if (function.is_destructor && function.owner != nullptr) {
                set_vptr(*function.owner);
            }
            if (function.qualified == "main") {
                emit_static_initializers();
            }
            gen_statement(*function.decl->body);
            if (function.is_destructor) {
                emit_destructor_suffix(function);
            }
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

    void emit_rtti() {
        bool needs_pure_virtual = false;
        for (auto& entry : class_index_) {
            ClassInfo& info = *entry.second;
            const std::vector<std::string> parts = split_qualified(info.qualified);
            const std::string mangled_name = mangle_qualified(parts);
            const std::string vtable_symbol = "_ZTV" + mangled_name;
            const std::string typeinfo_symbol = "_ZTI" + mangled_name;
            const std::string typeinfo_name = "_ZTS" + mangled_name;
            data_ += ".weak " + typeinfo_symbol + "\n" + typeinfo_symbol + ":\n";
            data_ += "    .word 0\n";
            data_ += "    .word " + typeinfo_name + "\n";
            if (info.base != nullptr) {
                const std::vector<std::string> base_parts =
                    split_qualified(info.base->qualified);
                data_ += "    .word _ZTI" + mangle_qualified(base_parts) + "\n";
            } else {
                data_ += "    .word 0\n";
            }
            rodata_ += typeinfo_name + ":\n    .asciz \"" +
                       escape_asm(info.qualified) + "\"\n";
            if (!info.polymorphic) {
                continue;
            }
            data_ += ".weak " + vtable_symbol + "\n" + vtable_symbol + ":\n";
            data_ += "    .word " + typeinfo_symbol + "\n";
            data_ += "    .word 0\n";
            for (Function* slot : info.vtable) {
                if (slot == nullptr || slot->is_pure || !slot->defined) {
                    needs_pure_virtual = true;
                    data_ += "    .word __cxa_pure_virtual\n";
                } else {
                    data_ += "    .word " + slot->symbol + "\n";
                }
            }
        }
        if (needs_pure_virtual) {
            text_ += ".text\n.global __cxa_pure_virtual\n__cxa_pure_virtual:\n";
            text_ += "    JMP __cxa_pure_virtual\n";
        }
    }

    bool is_constant_global(const Global& global) {
        if (global.initializer == nullptr) {
            return true;
        }
        if (global.type && global.type->kind == TypeKind::Array &&
            global.initializer->kind == ExprKind::InitializerList) {
            for (const ExprPtr& element : global.initializer->children) {
                if (is_fp(global.type->element)) {
                    double value = 0.0;
                    if (!eval_const_fp(*element, value)) {
                        return false;
                    }
                } else {
                    std::int64_t value = 0;
                    if (!eval_const(*element, value)) {
                        return false;
                    }
                }
            }
            return true;
        }
        if (is_fp(global.type)) {
            double value = 0.0;
            return eval_const_fp(*global.initializer, value);
        }
        std::int64_t value = 0;
        return eval_const(*global.initializer, value);
    }

    void emit_static_initializers() {
        for (const auto& global : global_storage_) {
            if (!global->needs_init || !global->initializer) {
                continue;
            }
            if (is_fp(global->type)) {
                gen_as_fp(*global->initializer, global->type);
                emit("    LEA R1, [" + global->symbol + "]");
                emit(std::string("    FSTORE.") +
                     (is_float32(global->type) ? "W" : "D") + " F0, [R1]");
            } else {
                gen_expr(*global->initializer);
                emit("    LEA R1, [" + global->symbol + "]");
                store_value(global->type);
            }
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
            if (global->initializer && !is_constant_global(*global)) {
                global->needs_init = true;
                output += "    .space " + std::to_string(size) + "\n";
                bss_ += output;
                continue;
            }
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
    std::map<std::string, std::vector<const Decl*>> template_functions_;
    std::vector<std::shared_ptr<Decl>> template_instantiation_decls_;
    std::vector<std::unique_ptr<Global>> global_storage_;
    std::map<std::string, Global*> global_index_;
    std::map<std::string, TypePtr> type_aliases_;
    std::map<std::string, std::set<std::string>> imports_;
    std::map<std::string, std::map<std::string, std::string>> using_aliases_;
    std::map<std::string, std::unique_ptr<ClassInfo>> class_index_;

    std::vector<std::map<std::string, Local>> local_scopes_;
    std::vector<std::vector<std::pair<int, Function*>>> local_destructors_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::map<const Expr*, Function*> call_targets_;
    std::set<const Expr*> diagnosed_expressions_;
    std::set<Function*> emitted_functions_;
    struct ExceptionHandler {
        std::string label;
        std::size_t scope_depth = 0;
    };
    std::vector<ExceptionHandler> exception_handlers_;
    std::map<std::string, int> exception_type_ids_;
    bool uses_exceptions_ = false;
    std::string current_prefix_;
    ClassInfo* current_class_ = nullptr;
    int this_offset_ = -1;
    Function* current_function_ = nullptr;
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
