#include "cxx/codegen.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
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

std::string mangle_component(const std::string& name) {
    static const std::map<std::string, std::string> operators = {
        {"operator+", "operator_plus"},
        {"operator-", "operator_minus"},
        {"operator*", "operator_star"},
        {"operator/", "operator_slash"},
        {"operator%", "operator_percent"},
        {"operator==", "operator_eq"},
        {"operator!=", "operator_ne"},
        {"operator<", "operator_lt"},
        {"operator<=", "operator_le"},
        {"operator>", "operator_gt"},
        {"operator>=", "operator_ge"},
        {"operator<<", "operator_lshift"},
        {"operator>>", "operator_rshift"},
        {"operator[]", "operator_index"},
        {"operator()", "operator_call"},
        {"operator=", "operator_assign"},
        {"operator+=", "operator_add_assign"},
        {"operator-=", "operator_sub_assign"},
        {"operator*=", "operator_mul_assign"},
        {"operator/=", "operator_div_assign"},
        {"operator%=", "operator_mod_assign"},
        {"operator&", "operator_amp"},
        {"operator|", "operator_pipe"},
        {"operator^", "operator_xor"},
        {"operator~", "operator_not"},
        {"operator!", "operator_lnot"},
        {"operator&&", "operator_land"},
        {"operator||", "operator_lor"},
        {"operator++", "operator_inc"},
        {"operator--", "operator_dec"},
        {"operator->", "operator_arrow"},
        {"operator->*", "operator_arrow_star"},
        {"operator,", "operator_comma"},
    };
    const auto found = operators.find(name);
    return found == operators.end() ? name : found->second;
}

std::string mangle_qualified(std::vector<std::string> parts) {
    if (parts.size() == 1) {
        const std::string part = mangle_component(parts[0]);
        return std::to_string(part.size()) + part;
    }
    std::string result = "N";
    for (const std::string& part : parts) {
        const std::string component = mangle_component(part);
        result += std::to_string(component.size()) + component;
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
    case BuiltinKind::LongLong: return "l";
    case BuiltinKind::UnsignedLongLong: return "m";
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
        if (type->has_template_integer_value) {
            result += "L" + std::to_string(type->template_integer_value) + "E";
        } else {
            result += mangle_builtin(type->builtin);
        }
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

ExprPtr clone_expr(const Expr& source) {
    auto copy = std::make_unique<Expr>();
    copy->kind = source.kind;
    copy->location = source.location;
    copy->text = source.text;
    copy->integer = source.integer;
    copy->floating = source.floating;
    copy->name = source.name;
    copy->type = source.type ? source.type->clone() : nullptr;
    copy->body = source.body;
    copy->template_arguments = source.template_arguments;
    for (const TypePtr& argument : source.template_type_arguments) {
        copy->template_type_arguments.push_back(argument ? argument->clone()
                                                         : nullptr);
    }
    copy->is_arrow = source.is_arrow;
    copy->is_prefix = source.is_prefix;
    copy->global_scope = source.global_scope;
    copy->new_nothrow = source.new_nothrow;
    copy->new_value_initialize = source.new_value_initialize;
    copy->new_initializer =
        source.new_initializer ? clone_expr(*source.new_initializer) : nullptr;
    for (const ExprPtr& argument : source.placement_arguments) {
        copy->placement_arguments.push_back(argument ? clone_expr(*argument) : nullptr);
    }
    copy->has_field_designator = source.has_field_designator;
    copy->has_index_designator = source.has_index_designator;
    copy->field_designator = source.field_designator;
    copy->index_designator = source.index_designator;
    copy->parameters = source.parameters;
    for (const ExprPtr& child : source.children) {
        copy->children.push_back(child ? clone_expr(*child) : nullptr);
    }
    for (const ExprPtr& argument : source.arguments) {
        copy->arguments.push_back(argument ? clone_expr(*argument) : nullptr);
    }
    return copy;
}

bool is_integer(const TypePtr& type) {
    return type && type->is_integral();
}

bool is_unsigned_integer(const TypePtr& type) {
    return type && type->kind == TypeKind::Builtin &&
           (type->builtin == BuiltinKind::UnsignedInt ||
            type->builtin == BuiltinKind::UnsignedLongLong);
}

bool is_long_long(const TypePtr& type) {
    return type && type->kind == TypeKind::Builtin &&
           (type->builtin == BuiltinKind::LongLong ||
            type->builtin == BuiltinKind::UnsignedLongLong);
}

bool is_unsigned_operation(const TypePtr& left, const TypePtr& right) {
    return is_unsigned_integer(left) || is_unsigned_integer(right);
}

bool has_unsigned_suffix(const std::string& raw) {
    std::size_t index = raw.size();
    while (index > 0 && (raw[index - 1] == 'l' || raw[index - 1] == 'L')) {
        --index;
    }
    return index > 0 && (raw[index - 1] == 'u' || raw[index - 1] == 'U');
}

bool has_long_long_suffix(const std::string& raw) {
    std::size_t index = raw.size();
    int longs = 0;
    while (index > 0 && (raw[index - 1] == 'l' || raw[index - 1] == 'L')) {
        --index;
        ++longs;
    }
    return longs >= 2;
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
    if (is_long_long(left) || is_long_long(right)) {
        return Type::make_builtin(is_unsigned_operation(left, right)
                                      ? BuiltinKind::UnsignedLongLong
                                      : BuiltinKind::LongLong);
    }
    if (is_unsigned_operation(left, right)) {
        return Type::make_builtin(BuiltinKind::UnsignedInt);
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
                          bool destructor, bool is_const = false) {
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
        const std::string component = mangle_component(std::string(name));
        result += std::to_string(component.size()) + component;
    }
    result += "E";
    if (is_const) {
        result += "K";
    }
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
        debug_record("S 0 " + unit.source_name);
        collect_classes(unit.declarations, "");
        ensure_bad_alloc_class();
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
                if (function->from_template && !function->symbol.empty()) {
                    text_ += ".section .text." + function->symbol + "\n";
                    emit_function(*function);
                    text_ += ".text\n";
                } else {
                    emit_function(*function);
                }
            }
        }
        emit_globals(true);
        for (const std::string& function : late_text_functions_) {
            text_ += function;
        }
        emit_cxx_init();
        emit_tls_runtime();
        emit_rtti();
        emit_lsdas();
        emit_eh_frame();
        emit_static_fini();
        bss_ += ".weak __kr32_exception_value\n";
        bss_ += "__kr32_exception_value:\n    .space 8\n";
        bss_ += ".weak __kr32_exception_type\n";
        bss_ += "__kr32_exception_type:\n    .space 4\n";
        bss_ += ".weak __kr32_exception_pending\n";
        bss_ += "__kr32_exception_pending:\n    .space 4\n";
        bss_ += ".weak __kr32_exception_object\n";
        bss_ += "__kr32_exception_object:\n    .space 4\n";
        std::string result = text_;
        if (!rodata_.empty()) {
            result += ".rodata\n" + rodata_;
        }
        if (!lsda_.empty()) {
            result += lsda_;
        }
        if (!data_.empty()) {
            result += ".data\n" + data_;
        }
        if (!bss_.empty()) {
            result += ".bss\n" + bss_;
        }
        if (!startup_.empty()) {
            result += startup_;
        }
        if (!tls_image_.empty()) {
            result += ".tls\n";
            std::sort(tls_symbols_.begin(), tls_symbols_.end(),
                      [](const TlsSymbol& left, const TlsSymbol& right) {
                          return left.offset < right.offset;
                      });
            int cursor = 0;
            for (const TlsSymbol& symbol : tls_symbols_) {
                if (symbol.offset > cursor) {
                    result += "    .space " +
                              std::to_string(symbol.offset - cursor) + "\n";
                    cursor = symbol.offset;
                }
                if (symbol.exported) {
                    result += ".global " + symbol.symbol + "\n";
                }
                result += symbol.symbol + ":\n";
                for (int index = 0; index < symbol.size; ++index) {
                    const std::size_t position =
                        static_cast<std::size_t>(symbol.offset + index);
                    result += "    .byte " +
                              std::to_string(position < tls_image_.size()
                                                 ? tls_image_[position]
                                                 : 0) +
                              "\n";
                }
                cursor += symbol.size;
            }
        }
        if (!debug_.empty()) {
            result += ".debug_info\n" + debug_;
        }
        return result;
    }

private:
    struct ClassInfo;
    struct Global;

    struct Function {
        const Decl* decl = nullptr;
        std::string qualified;
        std::string symbol;
        std::vector<TypePtr> parameters;
        TypePtr result;
        bool extern_c = false;
        bool variadic = false;
        bool from_template = false;
        bool defined = false;
        bool is_static = false;
        bool is_member = false;
        bool is_constructor = false;
        bool is_destructor = false;
        bool is_virtual = false;
        bool is_pure = false;
        bool is_const = false;
        bool weak = false;
        int vtable_index = -1;
        std::string member_name;
        ClassInfo* owner = nullptr;
        ClassInfo* friend_owner = nullptr;
        bool lambda_closure = false;
        int lambda_closure_size = 0;
        std::map<std::string, int> lambda_capture_offsets;
        std::map<std::string, TypePtr> lambda_capture_types;
        std::set<std::string> lambda_capture_references;
        Access access = Access::Public;
    };

    struct Global {
        const Decl* decl = nullptr;
        std::string qualified;
        std::string symbol;
        TypePtr type;
        bool extern_c = false;
        bool defined = false;
        bool is_static = false;
        bool is_thread_local = false;
        int tls_offset = 0;
        std::string tls_slot_symbol;
        const Expr* initializer = nullptr;
        bool needs_init = false;
        bool template_static_member = false;
        Access access = Access::Public;
        ClassInfo* owner = nullptr;
    };

    struct MemberInfo {
        std::string name;
        TypePtr type;
        int offset = 0;
        bool is_static = false;
        Access access = Access::Public;
        ClassInfo* owner = nullptr;
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
        std::map<std::string, std::int64_t> template_integer_values;
        std::map<std::string, Access> static_access;
        std::set<std::string> friend_classes;
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
        Global* static_global = nullptr;
        Function* lambda_target = nullptr;
        int lambda_closure_offset = -1;
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
        TypePtr type = resolve_type(raw);
        while (type && (type->kind == TypeKind::LValueRef ||
                        type->kind == TypeKind::RValueRef)) {
            type = type->element;
        }
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

    bool can_access(const ClassInfo* owner, Access access) const {
        if (owner == nullptr || access == Access::Public) {
            return true;
        }
        if (current_function_ != nullptr &&
            current_function_->friend_owner == owner) {
            return true;
        }
        if (current_class_ == nullptr) {
            return false;
        }
        if (current_class_ == owner) {
            return true;
        }
        if (owner->friend_classes.find(current_class_->qualified) !=
            owner->friend_classes.end()) {
            return true;
        }
        if (access == Access::Protected) {
            for (ClassInfo* base = current_class_->base; base != nullptr;
                 base = base->base) {
                if (base == owner) {
                    return true;
                }
            }
        }
        return false;
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
                const std::string qualified = qualify(prefix, declaration->name);
                if (declaration->is_specialization &&
                    !declaration->specialization_arguments.empty()) {
                    std::string key = qualified;
                    for (std::size_t position = key.find("::");
                         position != std::string::npos;
                         position = key.find("::", position)) {
                        key.replace(position, 2, "__");
                    }
                    key += "__";
                    for (std::size_t index = 0;
                         index < declaration->specialization_arguments.size();
                         ++index) {
                        if (index != 0) {
                            key += "_";
                        }
                        key += mangle_type(
                            declaration->specialization_arguments[index]);
                    }
                    class_specializations_[key] = declaration.get();
                } else if (!declaration->specialization_arguments.empty()) {
                    PartialTemplate partial;
                    partial.name = qualified;
                    partial.pattern = declaration->specialization_arguments;
                    partial.decl = declaration.get();
                    class_partial_specializations_.push_back(std::move(partial));
                } else {
                    class_templates_[qualified] = declaration.get();
                }
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

    void ensure_bad_alloc_class() {
        if (class_index_.find("std::bad_alloc") != class_index_.end()) {
            return;
        }
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Class;
        declaration->name.parts.push_back("std");
        declaration->name.parts.push_back("bad_alloc");
        declaration->location = {1, 1};
        synthetic_declarations_.push_back(declaration);
        auto info = std::make_unique<ClassInfo>();
        info->decl = declaration.get();
        info->qualified = "std::bad_alloc";
        info->size = 1;
        info->align = 1;
        info->laid_out = true;
        info->vtables_built = true;
        class_index_["std::bad_alloc"] = std::move(info);
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
            field.access = member->access;
            field.owner = &info;
            info.size += size;
            info.align = std::max(info.align, alignment);
            info.member_index[field.name] = nullptr;
            info.members.push_back(std::move(field));
        }
        if (info.members.empty() && info.base == nullptr &&
            !info.polymorphic) {
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

    bool match_partial_pattern(
        const TypePtr& pattern, const TypePtr& actual,
        const std::set<std::string>& parameters,
        std::map<std::string, TypePtr>& bindings) {
        if (!pattern || !actual) {
            return pattern == actual;
        }
        if (pattern->kind == TypeKind::Named && pattern->name.parts.size() == 1 &&
            parameters.count(pattern->name.parts[0]) != 0) {
            bindings[pattern->name.parts[0]] = actual;
            return true;
        }
        if (pattern->kind == TypeKind::Pointer) {
            if (actual->kind != TypeKind::Pointer || !actual->element) {
                return false;
            }
            return match_partial_pattern(pattern->element, actual->element,
                                         parameters, bindings);
        }
        if (pattern->kind == TypeKind::LValueRef ||
            pattern->kind == TypeKind::RValueRef) {
            if (actual->kind != pattern->kind || !actual->element) {
                return false;
            }
            return match_partial_pattern(pattern->element, actual->element,
                                         parameters, bindings);
        }
        if (pattern->kind == TypeKind::Array) {
            if (actual->kind != TypeKind::Array || !actual->element ||
                pattern->array_size != actual->array_size) {
                return false;
            }
            return match_partial_pattern(pattern->element, actual->element,
                                         parameters, bindings);
        }
        if (pattern->kind == TypeKind::Named &&
            actual->kind == TypeKind::Named &&
            pattern->name.to_string() == actual->name.to_string() &&
            !pattern->template_arguments.empty() &&
            pattern->template_arguments.size() ==
                actual->template_arguments.size()) {
            for (std::size_t index = 0; index < pattern->template_arguments.size();
                 ++index) {
                if (!match_partial_pattern(pattern->template_arguments[index],
                                           actual->template_arguments[index],
                                           parameters, bindings)) {
                    return false;
                }
            }
            return true;
        }
        return mangle_type(pattern) == mangle_type(actual);
    }

    TypePtr instantiate_class_template(const TypePtr& named) {
        const std::string template_name = named->name.to_string();
        std::string key = template_name;
        for (std::size_t position = key.find("::"); position != std::string::npos;
             position = key.find("::", position)) {
            key.replace(position, 2, "__");
        }
        key += "__";
        for (std::size_t index = 0; index < named->template_arguments.size(); ++index) {
            if (index != 0) {
                key += "_";
            }
            key += named->template_arguments[index]
                       ? mangle_type(named->template_arguments[index])
                       : "x";
        }
        const auto existing_specialization = class_specializations_.find(key);
        if (existing_specialization != class_specializations_.end()) {
            const auto existing = class_index_.find(key);
            if (existing == class_index_.end()) {
                auto info = std::make_unique<ClassInfo>();
                info->decl = existing_specialization->second;
                info->qualified = key;
                ClassInfo* raw = info.get();
                class_index_[key] = std::move(info);
                Decl* specialization_decl =
                    const_cast<Decl*>(existing_specialization->second);
                for (DeclPtr& child : specialization_decl->children) {
                    if (!child || child->kind != DeclKind::Function ||
                        child->body || child->name.parts.empty()) {
                        continue;
                    }
                    const std::string method = child->name.parts.back();
                    for (const TemplateMemberDefinition& definition :
                         template_member_definitions_) {
                        if (definition.owner != template_name ||
                            definition.method != method ||
                            definition.pattern.size() !=
                                named->template_arguments.size()) {
                            continue;
                        }
                        bool matches = true;
                        for (std::size_t index = 0;
                             index < definition.pattern.size(); ++index) {
                            if (mangle_type(definition.pattern[index]) !=
                                mangle_type(named->template_arguments[index])) {
                                matches = false;
                                break;
                            }
                        }
                        if (matches) {
                            child->body = definition.decl->body;
                            break;
                        }
                    }
                }
                for (DeclPtr& child : specialization_decl->children) {
                    if (!child || child->kind != DeclKind::Variable ||
                        !child->is_static || child->initializer ||
                        child->name.parts.empty()) {
                        continue;
                    }
                    const std::string member = child->name.parts.back();
                    for (const TemplateStaticDefinition& definition :
                         template_static_definitions_) {
                        if (definition.owner != template_name ||
                            definition.member != member ||
                            definition.pattern.size() !=
                                named->template_arguments.size()) {
                            continue;
                        }
                        bool matches = true;
                        for (std::size_t index = 0;
                             index < definition.pattern.size(); ++index) {
                            if (mangle_type(definition.pattern[index]) !=
                                mangle_type(named->template_arguments[index])) {
                                matches = false;
                                break;
                            }
                        }
                        if (matches) {
                            child->initializer =
                                clone_expr(*definition.decl->initializer);
                            break;
                        }
                    }
                }
                in_template_instantiation_ = true;
                collect_class_members(*existing_specialization->second, key);
                in_template_instantiation_ = false;
                layout_class(*raw);
            }
            Name name;
            name.parts.push_back(key);
            return Type::make_named(name);
        }
        const Decl* source = nullptr;
        std::map<std::string, TypePtr> bindings;
        for (const PartialTemplate& partial : class_partial_specializations_) {
            if (partial.name != template_name ||
                partial.pattern.size() != named->template_arguments.size()) {
                continue;
            }
            std::set<std::string> parameters;
            for (const DeclPtr& parameter : partial.decl->template_parameters) {
                if (parameter && !parameter->name.parts.empty()) {
                    parameters.insert(parameter->name.parts[0]);
                }
            }
            std::map<std::string, TypePtr> candidate_bindings;
            bool matches = true;
            for (std::size_t index = 0; index < partial.pattern.size(); ++index) {
                if (!match_partial_pattern(partial.pattern[index],
                                           named->template_arguments[index],
                                           parameters, candidate_bindings)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                source = partial.decl;
                bindings = std::move(candidate_bindings);
                break;
            }
        }
        if (source == nullptr) {
            const auto found_template = class_templates_.find(template_name);
            if (found_template == class_templates_.end()) {
                return named;
            }
            source = found_template->second;
            for (std::size_t index = 0;
                 index < source->template_parameters.size() &&
                 index < named->template_arguments.size();
                 ++index) {
                const DeclPtr& parameter = source->template_parameters[index];
                if (parameter && !parameter->name.parts.empty()) {
                    bindings[parameter->name.parts[0]] =
                        named->template_arguments[index];
                }
            }
        }

        const auto existing = class_index_.find(key);
        if (existing != class_index_.end()) {
            Name name;
            name.parts.push_back(key);
            return Type::make_named(name);
        }

        auto clone = std::make_shared<Decl>();
        clone->kind = DeclKind::Class;
        clone->location = source->location;
        clone->name.parts = {key};
        clone->is_template = false;
        for (const DeclPtr& child : source->children) {
            if (!child) {
                continue;
            }
            auto child_copy = std::make_shared<Decl>();
            child_copy->kind = child->kind;
            child_copy->location = child->location;
            child_copy->name = child->name;
            child_copy->type = substitute_type(child->type, bindings);
            child_copy->body = child->body;
            if (!child_copy->body && child->kind == DeclKind::Function &&
                !child->name.parts.empty()) {
                const std::string method = child->name.parts.back();
                for (const TemplateMemberDefinition& definition :
                     template_member_definitions_) {
                    if (definition.owner != template_name ||
                        definition.method != method ||
                        definition.pattern.size() !=
                            named->template_arguments.size()) {
                        continue;
                    }
                    bool matches = true;
                    for (std::size_t index = 0;
                         index < definition.pattern.size(); ++index) {
                        const TypePtr pattern =
                            substitute_type(definition.pattern[index], bindings);
                        if (mangle_type(pattern) !=
                            mangle_type(named->template_arguments[index])) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        child_copy->body = definition.decl->body;
                        break;
                    }
                }
            }
            child_copy->initializer = nullptr;
            if (child->kind == DeclKind::Variable && child->is_static &&
                !child->name.parts.empty()) {
                const std::string member = child->name.parts.back();
                for (const TemplateStaticDefinition& definition :
                     template_static_definitions_) {
                    if (definition.owner != template_name ||
                        definition.member != member ||
                        definition.pattern.size() !=
                            named->template_arguments.size()) {
                        continue;
                    }
                    bool matches = true;
                    for (std::size_t index = 0;
                         index < definition.pattern.size(); ++index) {
                        const TypePtr pattern =
                            substitute_type(definition.pattern[index], bindings);
                        if (mangle_type(pattern) !=
                            mangle_type(named->template_arguments[index])) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        child_copy->initializer =
                            clone_expr(*definition.decl->initializer);
                        break;
                    }
                }
            }
            child_copy->is_static = child->is_static;
            child_copy->is_virtual = child->is_virtual;
            child_copy->is_override = child->is_override;
            child_copy->is_pure = child->is_pure;
            child_copy->is_inline = child->is_inline || child->kind == DeclKind::Function;
            child_copy->is_constructor = child->is_constructor;
            child_copy->is_destructor = child->is_destructor;
            child_copy->is_operator = child->is_operator;
            child_copy->is_explicit = child->is_explicit;
            child_copy->is_defaulted = child->is_defaulted;
            child_copy->is_deleted = child->is_deleted;
            child_copy->is_template = false;
            child_copy->variadic = child->variadic;
            child_copy->is_const = child->is_const;
            child_copy->access = child->access;
            for (const Param& parameter : child->parameters) {
                Param copied;
                copied.type = substitute_type(parameter.type, bindings);
                copied.name = parameter.name;
                copied.variadic = parameter.variadic;
                child_copy->parameters.push_back(std::move(copied));
            }
            clone->children.push_back(std::move(child_copy));
        }
        template_class_storage_.push_back(clone);

        auto info = std::make_unique<ClassInfo>();
        info->decl = clone.get();
        info->qualified = key;
        for (std::size_t index = 0;
             index < source->template_parameters.size() &&
             index < named->template_arguments.size();
             ++index) {
            const DeclPtr& parameter = source->template_parameters[index];
            const TypePtr& argument = named->template_arguments[index];
            if (parameter && parameter->is_non_type_template_parameter &&
                argument && argument->has_template_integer_value &&
                !parameter->name.parts.empty()) {
                info->template_integer_values[parameter->name.parts[0]] =
                    argument->template_integer_value;
            }
        }
        ClassInfo* raw = info.get();
        class_index_[key] = std::move(info);
        in_template_instantiation_ = true;
        collect_class_members(*clone, key);
        in_template_instantiation_ = false;
        layout_class(*raw);
        Name name;
        name.parts.push_back(key);
        return Type::make_named(name);
    }

    TypePtr resolve_type(TypePtr type) {
        if (!type) {
            return type;
        }
        if (type->kind == TypeKind::Named && !type->template_arguments.empty()) {
            return instantiate_class_template(type);
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
            case BuiltinKind::LongLong:
            case BuiltinKind::UnsignedLongLong: return 8;
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

    TypePtr qualify_self_type(const TypePtr& type, ClassInfo* owner) {
        if (!type || type->kind != TypeKind::Named || owner == nullptr ||
            type->name.parts.size() != 1) {
            return type;
        }
        const std::string& qualified = owner->qualified;
        const std::string& name = type->name.parts[0];
        const bool self =
            qualified == name ||
            (qualified.size() > name.size() + 2 &&
             qualified.compare(qualified.size() - name.size(), name.size(),
                               name) == 0 &&
             qualified[qualified.size() - name.size() - 2] == ':' &&
             qualified[qualified.size() - name.size() - 1] == ':');
        if (!self) {
            return type;
        }
        auto copy = std::make_shared<Type>(*type);
        copy->name.parts = split_qualified(qualified);
        return copy;
    }

    Function* collect_function(const Decl& declaration, const std::string& prefix) {
        if (!declaration.owner_template_arguments.empty() &&
            declaration.name.parts.size() > 1) {
            Name owner_name;
            owner_name.parts.assign(declaration.name.parts.begin(),
                                    declaration.name.parts.end() - 1);
            TemplateMemberDefinition definition;
            definition.owner = qualify(prefix, owner_name);
            definition.pattern = declaration.owner_template_arguments;
            definition.method = declaration.name.parts.back();
            definition.decl = &declaration;
            template_member_definitions_.push_back(std::move(definition));
            return nullptr;
        }
        if (declaration.is_template) {            const std::string key = qualify(prefix, declaration.name);
            if (declaration.is_specialization &&
                !declaration.specialization_arguments.empty()) {
                function_specializations_[key].push_back(&declaration);
            } else {
                template_functions_[key].push_back(&declaration);
            }
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
        auto function = std::make_unique<Function>();
        function->decl = &declaration;
        function->qualified = qualified;
        function->extern_c = declaration.is_extern_c;
        function->is_static = declaration.is_static;
        function->access = declaration.access;
        function->defined = declaration.body != nullptr;
        function->variadic = declaration.variadic;
        function->owner = owner;
        function->is_member = owner != nullptr && !declaration.is_static;
        function->is_constructor = constructor;
        function->is_destructor = destructor;
        function->is_virtual = declaration.is_virtual || declaration.is_override;
        function->is_pure = declaration.is_pure;
        function->is_const = declaration.is_const;
        function->weak = declaration.is_inline;
        function->member_name = base_name;
        function->result = (constructor || destructor)
                               ? Type::make_builtin(BuiltinKind::Void)
                               : resolve_type(
                                     qualify_self_type(declaration.type, owner));
        for (const Param& parameter : declaration.parameters) {
            function->parameters.push_back(decay(resolve_type(
                qualify_self_type(parameter.type, owner))));
        }
        if (owner && (constructor || destructor)) {
            function->symbol = mangle_member(owner->qualified, base_name,
                                             function->parameters, constructor,
                                             destructor, function->is_const);
        } else {
            function->symbol = mangle_function(function->qualified,
                                               function->parameters,
                                               function->extern_c,
                                               function->variadic);
        }
        if (owner != nullptr && function->is_const && !constructor &&
            !destructor && !function->is_static) {
            function->symbol += "K";
        }

        if (function->is_static) {
            function->symbol = "__cxx_static_" +
                               std::to_string(static_counter_++) + "_" +
                               function->symbol;
        }
        static const std::set<std::string> runtime_functions = {
            "exit", "malloc", "free", "memcpy", "memset", "strlen", "putchar"};
        if (owner == nullptr && runtime_functions.find(base_name) !=
                                    runtime_functions.end()) {
            function->extern_c = true;
            function->symbol = base_name;
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
                const auto slot = info.virtual_slots.find(function->member_name);
                const bool overrides = slot != info.virtual_slots.end();
                if ((!function->is_virtual && !overrides) ||
                    function->is_constructor || function->is_destructor) {
                    continue;
                }
                function->is_virtual = true;
                if (overrides) {
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
        const bool inherited_virtual_destructor =
            info.base != nullptr &&
            info.base->virtual_slots.find("@dtor") != info.base->virtual_slots.end();
        if (info.destructor != nullptr &&
            (info.destructor->is_virtual || inherited_virtual_destructor)) {
            info.destructor->is_virtual = true;
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
                if (child->is_friend) {
                    if (Function* friend_function = collect_function(*child, "")) {
                        friend_function->friend_owner = info;
                    }
                } else {
                    collect_function(*child, prefix);
                }
            } else if (child->kind == DeclKind::Variable && child->is_static) {
                collect_global(*child, prefix);
                const auto found = global_index_.find(
                    qualify(prefix, child->name));
                info->static_access[child->name.to_string()] = child->access;
                if (found != global_index_.end()) {
                    info->static_globals[child->name.to_string()] = found->second;
                    found->second->access = child->access;
                    found->second->owner = info;
                }
            } else if (child->kind == DeclKind::Class) {
                if (child->is_friend) {
                    info->friend_classes.insert(child->name.to_string());
                } else {
                    collect_class_members(*child, qualify(prefix, child->name));
                }
            } else if (child->kind == DeclKind::TypeAlias ||
                       child->kind == DeclKind::Using) {
                collect_using(*child, prefix);
            }
        }
    }

    void collect_global(const Decl& declaration, const std::string& prefix) {
        if (declaration.is_template) {            if (!declaration.owner_template_arguments.empty() &&
                declaration.name.parts.size() > 1) {
                Name owner_name;
                owner_name.parts.assign(declaration.name.parts.begin(),
                                        declaration.name.parts.end() - 1);
                TemplateStaticDefinition definition;
                definition.owner = qualify(prefix, owner_name);
                definition.pattern = declaration.owner_template_arguments;
                definition.member = declaration.name.parts.back();
                definition.decl = &declaration;
                template_static_definitions_.push_back(std::move(definition));
                return;
            }
            error(declaration, "variable templates are not supported");
            return;
        }
        auto global = std::make_unique<Global>();
        global->template_static_member = in_template_instantiation_;
        global->decl = &declaration;
        global->qualified = qualify(prefix, declaration.name);
        global->extern_c = declaration.is_extern_c;
        global->is_static = declaration.is_static;
        global->is_thread_local = declaration.is_thread_local;
        if (global->is_thread_local) {
            const int alignment = std::max(1, type_align(resolve_type(declaration.type)));
            tls_cursor_ = (tls_cursor_ + alignment - 1) / alignment * alignment;
            global->tls_offset = tls_cursor_;
            tls_cursor_ += std::max(1, type_size(resolve_type(declaration.type)));
            global->tls_slot_symbol =
                ".Ltls_slot_" + std::to_string(tls_slot_counter_++);
        }
        global->type = resolve_type(declaration.type);
        if (declaration.name.parts.size() > 1) {
            Name class_name;
            class_name.parts.assign(declaration.name.parts.begin(),
                                    declaration.name.parts.end() - 1);
            if (ClassInfo* owner = find_class(class_name)) {
                const std::string member_name = declaration.name.parts.back();
                const auto access = owner->static_access.find(member_name);
                if (access != owner->static_access.end()) {
                    global->owner = owner;
                    global->access = access->second;
                }
            }
        }
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

    bool lambda_capture(const Name& name, TypePtr& type, int& offset) const {
        if (current_lambda_function_ == nullptr || name.parts.size() != 1) {
            return false;
        }
        const auto found =
            current_lambda_function_->lambda_capture_offsets.find(name.parts[0]);
        if (found == current_lambda_function_->lambda_capture_offsets.end()) {
            return false;
        }
        offset = found->second;
        const auto found_type =
            current_lambda_function_->lambda_capture_types.find(name.parts[0]);
        type = found_type != current_lambda_function_->lambda_capture_types.end()
                   ? found_type->second
                   : Type::make_builtin(BuiltinKind::Int);
        return true;
    }

    Global* lookup_global(const Name& name, const std::string& prefix) {        const std::vector<std::string> keys = candidate_keys(name, prefix);
        for (const std::string& key : keys) {
            const auto found = global_index_.find(key);
            if (found != global_index_.end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    Global* lookup_template_static(const Expr& expression) {
        if (expression.template_type_arguments.empty() ||
            expression.name.parts.size() < 2) {
            return nullptr;
        }
        Name class_name;
        class_name.parts.assign(expression.name.parts.begin(),
                                expression.name.parts.end() - 1);
        TypePtr named = Type::make_named(class_name);
        named->template_arguments = expression.template_type_arguments;
        TypePtr resolved = resolve_type(named);
        ClassInfo* info = class_of(resolved);
        if (info == nullptr) {
            return nullptr;
        }
        const auto found =
            info->static_globals.find(expression.name.parts.back());
        return found == info->static_globals.end() ? nullptr : found->second;
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

    TypePtr lambda_function_type(Function* function) {
        auto type = std::make_shared<Type>();
        type->kind = TypeKind::Function;
        type->element = function->result;
        type->parameters = function->parameters;
        type->variadic = function->variadic;
        return Type::make_pointer(type);
    }

    Function* materialize_lambda(const Expr& expression) {
        const auto existing = lambda_functions_.find(&expression);
        if (existing != lambda_functions_.end()) {
            return existing->second;
        }
        std::map<std::string, int> capture_offsets;
        std::map<std::string, TypePtr> capture_types;
        int capture_size = 0;
        std::set<std::string> capture_references;
        auto add_capture = [&](const std::string& name, bool reference) {
            Name capture_name;
            capture_name.parts.push_back(name);
            Local* source = lookup_local(capture_name);
            if (source == nullptr) {
                error(expression.location,
                      "lambda capture is not a local variable: " + name);
                return;
            }
            if (!reference && source->type &&
                source->type->kind == TypeKind::Array) {
                error(expression.location,
                      "array lambda captures must be by reference");
                return;
            }
            if (capture_offsets.find(name) != capture_offsets.end()) {
                error(expression.location, "duplicate lambda capture: " + name);
                return;
            }
            capture_offsets[name] = capture_size;
            capture_types[name] = source->type;
            if (reference) {
                capture_references.insert(name);
            }
            capture_size += 4;
        };
        if (!expression.text.empty()) {
            std::string captures = expression.text;
            std::size_t start = 0;
            while (start <= captures.size()) {
                const std::size_t comma = captures.find(',', start);
                std::string name = captures.substr(
                    start, comma == std::string::npos ? std::string::npos
                                                       : comma - start);
                while (!name.empty() &&
                       std::isspace(static_cast<unsigned char>(name.front())) != 0) {
                    name.erase(name.begin());
                }
                while (!name.empty() &&
                       std::isspace(static_cast<unsigned char>(name.back())) != 0) {
                    name.pop_back();
                }
                if (name == "=" || name == "&") {
                    const bool reference = name == "&";
                    std::set<std::string> seen;
                    for (auto scope = local_scopes_.rbegin();
                         scope != local_scopes_.rend(); ++scope) {
                        for (const auto& entry : *scope) {
                            if (seen.insert(entry.first).second) {
                                add_capture(entry.first, reference);
                            }
                        }
                    }
                } else if (name == "this") {
                    error(expression.location,
                          "this lambda captures are not supported yet");
                } else if (!name.empty()) {
                    bool reference = false;
                    if (name.front() == '&') {
                        reference = true;
                        name.erase(name.begin());
                    }
                    if (!name.empty()) {
                        add_capture(name, reference);
                    }
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
        }
        auto declaration = std::make_shared<Decl>();
        declaration->kind = DeclKind::Function;
        declaration->location = expression.location;
        declaration->name.parts.push_back(
            "__lambda_" + std::to_string(lambda_counter_++));
        declaration->body = expression.body;
        declaration->type = expression.type
                                ? resolve_type(expression.type)
                                : Type::make_builtin(BuiltinKind::Int);
        for (const DeclPtr& parameter : expression.parameters) {
            if (!parameter) {
                continue;
            }
            Param copy;
            copy.type = parameter->type;
            copy.name = parameter->name;
            copy.location = parameter->location;
            declaration->parameters.push_back(std::move(copy));
        }
        lambda_decl_storage_.push_back(declaration);
        auto function = std::make_unique<Function>();
        function->decl = declaration.get();
        function->qualified = declaration->name.to_string();
        function->symbol = function->qualified;
        function->result = declaration->type;
        for (const Param& parameter : declaration->parameters) {
            function->parameters.push_back(resolve_type(parameter.type));
        }
        function->defined = expression.body != nullptr;
        function->lambda_closure = !capture_offsets.empty();
        function->lambda_closure_size = capture_size;
        function->lambda_capture_offsets = capture_offsets;
        function->lambda_capture_types = capture_types;
        function->lambda_capture_references = capture_references;
        Function* raw = function.get();
        function_storage_.push_back(std::move(function));
        lambda_functions_[&expression] = raw;
        return raw;
    }

    TypePtr type_of(const Expr& expression, bool decay_arrays = true) {
        switch (expression.kind) {
        case ExprKind::IntegerLiteral: {
            const bool unsigned_suffix = has_unsigned_suffix(expression.text);
            const bool long_long_suffix =
                has_long_long_suffix(expression.text);
            if (unsigned_suffix) {
                return Type::make_builtin(
                    long_long_suffix || expression.integer > 2147483647LL
                        ? BuiltinKind::UnsignedLongLong
                        : BuiltinKind::UnsignedInt);
            }
            return Type::make_builtin(
                long_long_suffix || expression.integer > 2147483647LL ||
                        expression.integer < -2147483648LL
                    ? BuiltinKind::LongLong
                    : BuiltinKind::Int);
        }
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
            TypePtr capture_type;
            int capture_offset = 0;
            if (lambda_capture(expression.name, capture_type, capture_offset)) {
                (void)capture_offset;
                return capture_type;
            }
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
                    lookup_template_static(expression)) {
                TypePtr type = resolve_type(global->type);
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
                const auto integer_value = current_class_->template_integer_values.find(
                    expression.name.parts[0]);
                if (integer_value != current_class_->template_integer_values.end()) {
                    return Type::make_builtin(BuiltinKind::Int);
                }
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
            if (expression.text != "&" && expression.text != "*") {
                if (!expression.is_prefix &&
                    (expression.text == "++" || expression.text == "--")) {
                    if (Function* operation = find_member_operator(
                            class_of(type_of(*expression.children[0], false)),
                            "operator" + expression.text,
                            Type::make_builtin(BuiltinKind::Int), false)) {
                        return operation->result;
                    }
                }
                if (Function* operation = find_member_operator(
                        class_of(type_of(*expression.children[0], false)),
                        "operator" + expression.text, nullptr, true)) {
                    return operation->result;
                }
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
            if (Function* operation = find_member_operator(
                    class_of(left), "operator" + expression.text, right, false)) {
                return operation->result;
            }
            if (Function* operation = find_free_operator(
                    "operator" + expression.text, left, right)) {
                return operation->result;
            }
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
        case ExprKind::Assign: {
            const TypePtr left = type_of(*expression.children[0], false);
            if (Function* operation = find_member_operator(
                    class_of(left), "operator" + expression.text,
                    type_of(*expression.children[1]), false)) {
                return operation->result;
            }
            return left;
        }
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
                if (ClassInfo* info =
                        find_class(expression.children[0]->name.to_string())) {
                    Name name;
                    name.parts = split_qualified(info->qualified);
                    return Type::make_named(name);
                }
            }
            if (!expression.children.empty() &&
                may_be_callable_object(*expression.children[0])) {
                if (Function* operation = find_call_operator(
                        class_of(type_of(*expression.children[0], false)),
                        expression.arguments)) {
                    return operation->result;
                }
            }
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
                if (atomic_intrinsic_name(expression, intrinsic)) {
                    if (intrinsic == "__atomic_store_n") {
                        return Type::make_builtin(BuiltinKind::Void);
                    }
                    if (intrinsic == "__atomic_compare_exchange_n") {
                        return Type::make_builtin(BuiltinKind::Bool);
                    }
                    return Type::make_builtin(BuiltinKind::Int);
                }
                if (syscall_intrinsic_name(expression, intrinsic)) {
                    return Type::make_builtin(BuiltinKind::Int);
                }
            }
            if (!expression.children.empty() &&
                expression.children[0]->kind == ExprKind::Lambda) {
                return materialize_lambda(*expression.children[0])->result;
            }
            if (!expression.children.empty() &&
                expression.children[0]->kind == ExprKind::Name) {
                if (Local* local = lookup_local(expression.children[0]->name)) {
                    if (local->lambda_target != nullptr) {
                        return resolve_type(local->lambda_target->result);
                    }
                }
            }
            std::string list_method;
            ClassInfo* list_info = nullptr;
            MemberInfo* list_data = nullptr;
            MemberInfo* list_size = nullptr;
            if (initializer_list_member_call(expression, list_method, list_info,
                                             list_data, list_size)) {
                return Type::make_pointer(list_data->type->element);
            }

            if (!expression.children.empty() &&
                expression.children[0]->kind == ExprKind::Name &&
                expression.children[0]->name.to_string() == "std::move" &&
                expression.arguments.size() == 1) {
                return type_of(*expression.arguments[0], false);
            }
            if (Function* function = resolve_call(expression)) {
                return resolve_type(function->result);
            }
            return Type::make_builtin(BuiltinKind::Int);
        }
        case ExprKind::Index: {
            const TypePtr base = type_of(*expression.children[0]);
            if (Function* operation = find_member_operator(
                    class_of(base), "operator[]",
                    type_of(*expression.children[1]), false)) {
                return operation->result;
            }
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
                } else if (Function* arrow = find_member_operator(
                               class_of(object_type), "operator->", nullptr,
                               true)) {
                    if (arrow->result &&
                        (arrow->result->kind == TypeKind::Pointer ||
                         arrow->result->kind == TypeKind::LValueRef ||
                         arrow->result->kind == TypeKind::RValueRef)) {
                        object_type = arrow->result->element;
                    } else {
                        diagnose_once(expression,
                                      "'->' operator must return a pointer");
                        return Type::make_builtin(BuiltinKind::Int);
                    }
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
            diagnose_once(expression, "expression is not supported until a later M9 phase");
            return Type::make_builtin(BuiltinKind::Int);
        case ExprKind::Lambda: {
            Function* function = materialize_lambda(expression);
            return lambda_function_type(function);
        }
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
            const TypePtr parameter_element = param->element;
            if (parameter_element && parameter_element->kind == TypeKind::Builtin &&
                parameter_element->builtin == BuiltinKind::Void) {
                return 1;
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

    Function* find_member_operator(ClassInfo* info,
                                   const std::string& name,
                                   const TypePtr& argument,
                                   bool unary) {
        if (info == nullptr) {
            return nullptr;
        }
        const auto candidates = class_methods(info, name);
        for (Function* candidate : candidates) {
            const std::size_t arity = unary ? 0U : 1U;
            if (candidate->parameters.size() != arity) {
                continue;
            }
            if (arity == 0) {
                return candidate;
            }
            TypePtr parameter = candidate->parameters[0];
            if (parameter && (parameter->kind == TypeKind::LValueRef ||
                              parameter->kind == TypeKind::RValueRef)) {
                parameter = parameter->element;
            }
            if (same_type(decay(parameter), decay(argument))) {
                return candidate;
            }
            ClassInfo* parameter_class = class_of(parameter);
            if (parameter_class != nullptr &&
                parameter_class == class_of(argument)) {
                return candidate;
            }
        }
        return nullptr;
    }

    bool may_be_callable_object(const Expr& expression) {
        if (expression.kind == ExprKind::Name) {
            if (lookup_local(expression.name) != nullptr) {
                return true;
            }
            return lookup_global(expression.name, current_prefix_) != nullptr;
        }
        return expression.kind == ExprKind::Index;
    }

    Function* find_call_operator(ClassInfo* info,
                                 const std::vector<ExprPtr>& arguments) {
        if (info == nullptr) {
            return nullptr;
        }
        for (Function* candidate : class_methods(info, "operator()")) {
            if (candidate->parameters.size() != arguments.size()) {
                continue;
            }
            bool matches = true;
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                TypePtr parameter = candidate->parameters[index];
                if (parameter && (parameter->kind == TypeKind::LValueRef ||
                                  parameter->kind == TypeKind::RValueRef)) {
                    parameter = parameter->element;
                }
                TypePtr argument = decay(type_of(*arguments[index]));
                if (argument && (argument->kind == TypeKind::LValueRef ||
                                 argument->kind == TypeKind::RValueRef)) {
                    argument = argument->element;
                }
                if (!same_type(decay(parameter), argument)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return candidate;
            }
        }
        return nullptr;
    }

    Function* find_free_operator(const std::string& name,
                                 const TypePtr& left,
                                 const TypePtr& right) {
        for (const auto& entry : function_index_) {
            for (Function* candidate : entry.second) {
                if (candidate->owner != nullptr ||
                    candidate->member_name != name ||
                    candidate->parameters.size() != 2) {
                    continue;
                }
                TypePtr first = candidate->parameters[0];
                TypePtr second = candidate->parameters[1];
                if (first && (first->kind == TypeKind::LValueRef ||
                              first->kind == TypeKind::RValueRef)) {
                    first = first->element;
                }
                if (second && (second->kind == TypeKind::LValueRef ||
                               second->kind == TypeKind::RValueRef)) {
                    second = second->element;
                }
                TypePtr left_type = decay(left);
                TypePtr right_type = decay(right);
                if (left_type && (left_type->kind == TypeKind::LValueRef ||
                                  left_type->kind == TypeKind::RValueRef)) {
                    left_type = left_type->element;
                }
                if (right_type && (right_type->kind == TypeKind::LValueRef ||
                                   right_type->kind == TypeKind::RValueRef)) {
                    right_type = right_type->element;
                }
                ClassInfo* first_class = class_of(first);
                ClassInfo* left_class = class_of(left_type);
                ClassInfo* second_class = class_of(second);
                ClassInfo* right_class = class_of(right_type);
                const bool first_matches =
                    first_class != nullptr && left_class != nullptr
                        ? first_class == left_class
                        : same_type(decay(first), left_type);
                const bool second_matches =
                    second_class != nullptr && right_class != nullptr
                        ? second_class == right_class
                        : same_type(decay(second), right_type);
                if (first_matches && second_matches) {
                    return candidate;
                }
            }
        }
        return nullptr;
    }

    TypePtr gen_free_operator_call(const Expr& left,
                                   const Expr& right,
                                   Function* operation) {
        const TypePtr first = operation->parameters[0];
        const TypePtr second = operation->parameters[1];
        int first_temp = -1;
        int second_temp = -1;
        if (first && (first->kind == TypeKind::LValueRef ||
                      first->kind == TypeKind::RValueRef)) {
            gen_address(left);
            first_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(first_temp) + "]");
        } else {
            gen_expr(left);
            first_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(first_temp) + "]");
        }
        if (second && (second->kind == TypeKind::LValueRef ||
                       second->kind == TypeKind::RValueRef)) {
            gen_address(right);
            second_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(second_temp) + "]");
        } else {
            gen_expr(right);
            second_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(second_temp) + "]");
        }
        emit("    LOAD R0, [R7 + " + std::to_string(first_temp) + "]");
        emit("    LOAD R1, [R7 + " + std::to_string(second_temp) + "]");
        const ClassInfo* result_class =
            operation->result &&
                    operation->result->kind != TypeKind::LValueRef &&
                    operation->result->kind != TypeKind::RValueRef
                ? class_of(operation->result)
                : nullptr;
        const bool register_result =
            result_class != nullptr && is_register_return_class(result_class);
        int result_temp = -1;
        if (result_class != nullptr && !register_result) {
            result_temp = allocate_local(operation->result);
            emit("    LEA R4, [R7 + " + std::to_string(result_temp) + "]");
        }
        emit("    CALL " + operation->symbol);
        if (result_class != nullptr) {
            if (register_result) {
                result_temp = allocate_local(operation->result);
                emit("    LEA R2, [R7 + " + std::to_string(result_temp) + "]");
                emit("    STORE R0, [R2]");
                if (result_class->size > 4) {
                    emit("    STORE R1, [R2 + 4]");
                }
            }
            emit("    LEA R0, [R7 + " + std::to_string(result_temp) + "]");
        }
        return operation->result;
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

    bool eval_array_bound_expression(
        const ArrayBoundExprPtr& expression,
        const std::map<std::string, TypePtr>& bindings,
        std::int64_t& value) {
        if (!expression) {
            return false;
        }
        if (expression->op == ArrayBoundExpr::Op::Value) {
            value = expression->value;
            return true;
        }
        if (expression->op == ArrayBoundExpr::Op::Name) {
            const auto found = bindings.find(expression->name);
            if (found == bindings.end() || !found->second ||
                !found->second->has_template_integer_value) {
                return false;
            }
            value = found->second->template_integer_value;
            return true;
        }
        std::int64_t left = 0;
        std::int64_t right = 0;
        if (!eval_array_bound_expression(expression->left, bindings, left) ||
            !eval_array_bound_expression(expression->right, bindings, right)) {
            return false;
        }
        switch (expression->op) {
        case ArrayBoundExpr::Op::Add: value = left + right; return true;
        case ArrayBoundExpr::Op::Sub: value = left - right; return true;
        case ArrayBoundExpr::Op::Mul: value = left * right; return true;
        case ArrayBoundExpr::Op::Div:
            if (right == 0) {
                return false;
            }
            value = left / right;
            return true;
        default: return false;
        }
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
            if (type->kind == TypeKind::Array &&
                type->array_bound_expression) {
                std::int64_t value = 0;
                if (eval_array_bound_expression(type->array_bound_expression,
                                                bindings, value)) {
                    copy->array_size = static_cast<std::uint32_t>(value);
                }
            } else if (type->kind == TypeKind::Array &&
                       !type->array_bound_name.empty()) {
                const auto bound = bindings.find(type->array_bound_name);
                if (bound != bindings.end() && bound->second &&
                    bound->second->has_template_integer_value) {
                    copy->array_size = static_cast<std::uint32_t>(
                        bound->second->template_integer_value *
                            type->array_bound_scale +
                        type->array_bound_offset);
                }
            }
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
        function->from_template = true;
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
        for (const std::string& key : keys) {
            const auto found_specializations = function_specializations_.find(key);
            if (found_specializations == function_specializations_.end()) {
                continue;
            }
            for (const Decl* specialization : found_specializations->second) {
                const std::vector<TypePtr>& candidate_arguments =
                    call.children[0]->template_type_arguments.empty()
                        ? argument_types
                        : call.children[0]->template_type_arguments;
                if (specialization->specialization_arguments.size() !=
                    candidate_arguments.size()) {
                    continue;
                }
                bool matches = true;
                for (std::size_t index = 0;
                     index < specialization->specialization_arguments.size();
                     ++index) {
                    if (mangle_type(specialization->specialization_arguments[index]) !=
                        mangle_type(candidate_arguments[index])) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) {
                    continue;
                }
                for (const Decl* generic : templates) {
                    if (generic->template_parameters.size() !=
                        candidate_arguments.size()) {
                        continue;
                    }
                    Decl copy;
                    copy.kind = DeclKind::Function;
                    copy.location = specialization->location;
                    copy.name = specialization->name;
                    copy.type = specialization->type;
                    for (const Param& parameter : specialization->parameters) {
                        Param copied;
                        copied.type = parameter.type;
                        copied.name = parameter.name;
                        copied.location = parameter.location;
                        copied.variadic = parameter.variadic;
                        if (parameter.default_value) {
                            copied.default_value =
                                clone_expr(*parameter.default_value);
                        }
                        copy.parameters.push_back(std::move(copied));
                    }
                    copy.body = specialization->body;
                    copy.template_parameters = generic->template_parameters;
                    copy.variadic = specialization->variadic;
                    copy.is_inline = specialization->is_inline;
                    copy.is_extern_c = specialization->is_extern_c;
                    copy.is_static = specialization->is_static;
                    const std::string qualified =
                        qualify(current_prefix_, specialization->name);
                    if (Function* function = instantiate_template(
                            copy, qualified, candidate_arguments,
                            argument_types)) {
                        return function;
                    }
                }
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
        if (!candidates.empty()) {
            bool has_non_template = false;
            for (Function* candidate : candidates) {
                if (!candidate->from_template) {
                    has_non_template = true;
                    break;
                }
            }
            if (!has_non_template) {
                candidates.clear();
            }
        }
        if (candidates.empty()) {
            // Template candidates are instantiated after argument types are
            // known, below.
        }
        bool has_initializer_list_argument = false;
        for (const ExprPtr& argument : call.arguments) {
            if (argument && argument->kind == ExprKind::InitializerList) {
                has_initializer_list_argument = true;
                break;
            }
        }
        if (has_initializer_list_argument) {
            for (Function* candidate : candidates) {
                if (candidate->parameters.size() != call.arguments.size()) {
                    continue;
                }
                bool matches = true;
                for (std::size_t index = 0; index < call.arguments.size(); ++index) {
                    if (!call.arguments[index] ||
                        call.arguments[index]->kind != ExprKind::InitializerList) {
                        continue;
                    }
                    ClassInfo* parameter_class =
                        class_of(resolve_type(candidate->parameters[index]));
                    if (!is_initializer_list_class(parameter_class)) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    call_targets_[&call] = candidate;
                    return candidate;
                }
            }
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
        bool const_object = false;
        if (!call.children.empty() &&
            call.children[0]->kind == ExprKind::Member) {
            const TypePtr object =
                type_of(*call.children[0]->children[0], false);
            const_object = object &&
                           (object->is_const ||
                            (object->element && object->element->is_const));
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
            if (candidate->owner != nullptr && candidate->is_member) {
                if (!candidate->is_const && const_object) {
                    cost += 100;
                } else if (candidate->is_const && !const_object) {
                    cost += 1;
                }
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

    int allocate_local_bytes(int size, int alignment = 4) {
        alignment = std::max(4, alignment);
        frame_size_ = (frame_size_ + alignment - 1) / alignment * alignment;
        const int offset = frame_size_;
        frame_size_ += (std::max(1, size) + 3) / 4 * 4;
        return offset;
    }

    void emit(std::string line) {
        text_ += line;
        text_ += '\n';
    }

    std::string new_label(std::string_view prefix) {
        return ".L" + std::string(prefix) + "_" + std::to_string(label_counter_++);
    }

    void debug_record(const std::string& record) {
        debug_ += "    .asciz \"" + escape_asm(record) + "\"\n";
    }

    std::string debug_type(const TypePtr& raw) {
        const TypePtr type = resolve_type(raw);
        if (!type) {
            return "void";
        }
        if (type->kind == TypeKind::Pointer) {
            return debug_type(type->element) + "*";
        }
        if (type->kind == TypeKind::LValueRef || type->kind == TypeKind::RValueRef) {
            return debug_type(type->element) + "&";
        }
        if (type->kind == TypeKind::Array) {
            const std::string element = debug_type(type->element);
            if (type->array_size == 0) {
                return element + "[]";
            }
            return element + "[" + std::to_string(type->array_size) + "]";
        }
        if (type->kind == TypeKind::Function) {
            return "function";
        }
        if (type->kind == TypeKind::Named) {
            return type->name.to_string();
        }
        if (type->kind == TypeKind::Builtin) {
            switch (type->builtin) {
            case BuiltinKind::Void: return "void";
            case BuiltinKind::Bool: return "bool";
            case BuiltinKind::Char: return "char";
            case BuiltinKind::Float: return "float";
            case BuiltinKind::Double: return "double";
            default: return "int";
            }
        }
        return "int";
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
        if (is_long_long(resolved)) {
            emit("    MOV R2, R0");
            emit("    LOAD R0, [R2]");
            emit("    LOAD R1, [R2 + 4]");
        } else if (resolved && resolved->kind == TypeKind::Builtin &&
            (resolved->builtin == BuiltinKind::Char ||
             resolved->builtin == BuiltinKind::Bool)) {
            emit("    LOAD8S R0, [R0]");
        } else {
            emit("    LOAD R0, [R0]");
        }
    }

    void store_value(const TypePtr& type) {
        const TypePtr resolved = resolve_type(type);
        if (is_long_long(resolved)) {
            emit("    STORE R0, [R1]");
            emit("    STORE R2, [R1 + 4]");
        } else if (resolved && resolved->kind == TypeKind::Builtin &&
            (resolved->builtin == BuiltinKind::Char ||
             resolved->builtin == BuiltinKind::Bool)) {
            emit("    STORE8 R0, [R1]");
        } else {
            emit("    STORE R0, [R1]");
        }
    }

    void emit_tls_address(Global* global) {
        emit("    LEA R0, [" + global->tls_slot_symbol + "]");
        emit("    LOAD R0, [R0]");
        emit("    LEA R1, [.Ltls_base]");
        emit("    LOAD R1, [R1]");
        emit("    SUB R0, R0, R1");
        emit("    ADD R0, R11, R0");
        emit("    ADD R0, R0, #32");
    }

    void gen_address(const Expr& expression) {
        if (expression.kind == ExprKind::Index) {
            if (Function* operation = find_member_operator(
                    class_of(type_of(*expression.children[0])), "operator[]",
                    type_of(*expression.children[1]), false)) {
                gen_member_operator_call(*expression.children[0],
                                         *expression.children[1], operation);
                return;
            }
        }
        if (expression.kind == ExprKind::Unary && expression.text == "*" &&
            !expression.children.empty()) {
            gen_expr(*expression.children[0]);
            return;
        }
        if (expression.kind == ExprKind::Binary ||
            expression.kind == ExprKind::Call ||
            expression.kind == ExprKind::Index) {
            const TypePtr type = type_of(expression);
            if (type && (type->kind == TypeKind::LValueRef ||
                         type->kind == TypeKind::RValueRef)) {
                if (expression.kind == ExprKind::Call) {
                    gen_call(expression);
                } else {
                    gen_expr(expression);
                }
                return;
            }
            if ((expression.kind == ExprKind::Call ||
                 expression.kind == ExprKind::Binary) &&
                class_of(type) != nullptr) {
                gen_expr(expression);
                return;
            }
        }
        if (expression.kind == ExprKind::Call && !expression.children.empty() &&
            expression.children[0]->kind == ExprKind::Name &&
            expression.children[0]->name.to_string() == "std::move" &&
            expression.arguments.size() == 1) {
            gen_address(*expression.arguments[0]);
            return;
        }
        if (expression.kind == ExprKind::Name) {
            TypePtr capture_type;
            int capture_offset = 0;
            if (lambda_capture(expression.name, capture_type, capture_offset)) {
                emit("    LOAD R0, [R7 + " +
                     std::to_string(current_lambda_closure_offset_) + "]");
                if (capture_offset != 0) {
                    emit("    ADD R0, R0, #" + std::to_string(capture_offset));
                }
                if (current_lambda_function_ != nullptr &&
                    current_lambda_function_->lambda_capture_references.find(
                        expression.name.parts[0]) !=
                        current_lambda_function_->lambda_capture_references.end()) {
                    emit("    LOAD R0, [R0]");
                }
                return;
            }
            if (Local* local = lookup_local(expression.name)) {
                if (local->static_global != nullptr) {
                    emit("    LEA R0, [" + local->static_global->symbol + "]");
                } else if (local->is_reference) {
                    emit("    LOAD R0, [R7 + " + std::to_string(local->offset) + "]");
                } else {
                    emit("    LEA R0, [R7 + " + std::to_string(local->offset) + "]");
                }
                return;
            }
            if (Global* global = lookup_template_static(expression)) {
                if (global->owner != nullptr &&
                    !can_access(global->owner, global->access)) {
                    diagnose_once(expression, "member is not accessible");
                    emit("    MOV R0, #0");
                    return;
                }
                if (global->is_thread_local) {
                    emit_tls_address(global);
                } else {
                    emit("    LEA R0, [" + global->symbol + "]");
                }
                return;
            }
            if (Global* global = lookup_global(expression.name, current_prefix_)) {
                if (global->owner != nullptr &&
                    !can_access(global->owner, global->access)) {
                    diagnose_once(expression, "member is not accessible");
                    emit("    MOV R0, #0");
                    return;
                }
                if (global->is_thread_local) {
                    emit_tls_address(global);
                } else {
                    emit("    LEA R0, [" + global->symbol + "]");
                }
                return;
            }
            if (current_class_ != nullptr && expression.name.parts.size() == 1) {
                if (MemberInfo* member =
                        find_member(current_class_, expression.name.parts[0])) {
                    if (!can_access(member->owner, member->access)) {
                        diagnose_once(expression, "member is not accessible");
                        emit("    MOV R0, #0");
                        return;
                    }
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
            Function* arrow_operator = nullptr;
            if (expression.is_arrow && object_type &&
                (object_type->kind == TypeKind::Pointer ||
                 object_type->kind == TypeKind::LValueRef ||
                 object_type->kind == TypeKind::RValueRef)) {
                object_type = object_type->element;
            } else if (expression.is_arrow) {
                arrow_operator = find_member_operator(
                    class_of(object_type), "operator->", nullptr, true);
                if (arrow_operator != nullptr && arrow_operator->result &&
                    (arrow_operator->result->kind == TypeKind::Pointer ||
                     arrow_operator->result->kind == TypeKind::LValueRef ||
                     arrow_operator->result->kind == TypeKind::RValueRef)) {
                    object_type = arrow_operator->result->element;
                }
            }
            ClassInfo* info = class_of(object_type);
            if (!info) {
                diagnose_once(expression, "member access on a non-class type");
                emit("    MOV R0, #0");
                return;
            }
            if (MemberInfo* member = find_member(info, member_name)) {
                if (!can_access(member->owner, member->access)) {
                    diagnose_once(expression, "member is not accessible");
                    emit("    MOV R0, #0");
                    return;
                }
                if (arrow_operator != nullptr) {
                    gen_zero_argument_member_call(*expression.children[0],
                                                  arrow_operator);
                } else if (expression.is_arrow) {
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
                    const auto access = current->static_access.find(member_name);
                    if (access != current->static_access.end() &&
                        !can_access(current, access->second)) {
                        diagnose_once(expression, "member is not accessible");
                        emit("    MOV R0, #0");
                        return;
                    }
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

    int emit_lambda_closure(Function* lambda) {
        const int offset = allocate_local_bytes(lambda->lambda_closure_size);
        for (const auto& capture : lambda->lambda_capture_offsets) {
            Name capture_name;
            capture_name.parts.push_back(capture.first);
            Local* source = lookup_local(capture_name);
            if (source == nullptr) {
                continue;
            }
            if (lambda->lambda_capture_references.find(capture.first) !=
                lambda->lambda_capture_references.end()) {
                if (source->is_reference) {
                    emit("    LOAD R0, [R7 + " + std::to_string(source->offset) + "]");
                } else {
                    emit("    LEA R0, [R7 + " + std::to_string(source->offset) + "]");
                }
            } else {
                emit("    LOAD R0, [R7 + " + std::to_string(source->offset) + "]");
                if (source->type && (source->type->kind == TypeKind::LValueRef ||
                                     source->type->kind == TypeKind::RValueRef)) {
                    emit("    LOAD R0, [R0]");
                }
            }
            emit("    STORE R0, [R7 + " +
                 std::to_string(offset + capture.second) + "]");
        }
        emit("    LEA R0, [R7 + " + std::to_string(offset) + "]");
        return offset;
    }

    void set_vptr_at(ClassInfo& info, int address_temp) {
        if (!info.polymorphic) {
            return;
        }
        const std::vector<std::string> parts = split_qualified(info.qualified);
        const std::string vtable_symbol = "_ZTV" + mangle_qualified(parts);
        emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
        emit("    LEA R0, [" + vtable_symbol + "]");
        emit("    ADD R0, R0, #8");
        emit("    STORE R0, [R1]");
    }

    void construct_default_subobjects(ClassInfo& info, int address_temp) {
        const std::vector<ExprPtr> empty;
        if (info.base != nullptr) {
            construct_object_at(info.base, address_temp, empty, "", true);
        }
        for (const MemberInfo& member : info.members) {
            ClassInfo* member_class = class_of(member.type);
            if (member_class == nullptr) {
                continue;
            }
            const int member_address =
                allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
            emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
            if (member.offset != 0) {
                emit("    ADD R0, R0, #" + std::to_string(member.offset));
            }
            emit("    STORE R0, [R7 + " + std::to_string(member_address) + "]");
            construct_object_at(member_class, member_address, empty);
        }
        set_vptr_at(info, address_temp);
    }

    void construct_object_at(ClassInfo* info, int address_temp,
                             const std::vector<ExprPtr>& arguments,
                             const std::string& landing = {},
                             bool allow_abstract = false) {
        if (info->qualified == "std::thread" && !arguments.empty() &&
            arguments[0] && arguments[0]->kind == ExprKind::Lambda &&
            !arguments[0]->text.empty()) {
            Function* lambda = materialize_lambda(*arguments[0]);
            (void)emit_lambda_closure(lambda);
            const int closure_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(closure_temp) + "]");
            const std::string entry =
                "__lambda_thread_entry_" + std::to_string(lambda_thread_counter_++);
            std::string thunk;
            thunk += ".text\n.align 4\n" + entry + ":\n";
            thunk += "    PUSH R7\n    ADJSP #-8\n    LEA R7, [SP]\n";
            thunk += "    CALL " + lambda->symbol + "\n";
            thunk += "    ADJSP #8\n    POP R7\n";
            thunk += "    MOV R0, #0\n    SYSCALL #47\n    HALT\n";
            late_text_functions_.push_back(std::move(thunk));
            emit("    LEA R0, [" + entry + "]");
            emit("    LOAD R1, [R7 + " + std::to_string(closure_temp) + "]");
            emit("    MOV R2, #0");
            emit("    SYSCALL #44");
            emit("    LEA R1, [R7 + " + std::to_string(address_temp) + "]");
            emit("    STORE R0, [R1]");
            return;
        }
        if (info->abstract && !allow_abstract) {
            error(*info->decl, "cannot instantiate abstract class: " + info->qualified);
            return;
        }
        Function* constructor = find_constructor_for_arguments(info, arguments);
        if (constructor != nullptr &&
            !can_access(constructor->owner, constructor->access)) {
            error(*info->decl, "constructor is not accessible");
            return;
        }
        if (!constructor) {
            if (!arguments.empty()) {
                error(*info->decl, "no matching constructor for " + info->qualified);
                return;
            }
            emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
            emit("    MOV R1, #0");
            emit("    MOV R2, #" + std::to_string(info->size));
            emit("    CALL memset");
            construct_default_subobjects(*info, address_temp);
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
        const std::string call_start = new_label("ctor_call_start");
        const std::string call_end = new_label("ctor_call_end");
        emit(call_start + ":");
        emit("    CALL " + constructor->symbol);
        emit(call_end + ":");
        if (!landing.empty()) {
            record_call_site(call_start, call_end, landing);
            const std::string no_exception = new_label("ctor_no_exception");
            emit("    LOAD R2, [__kr32_exception_pending]");
            emit("    CMP R2, #0");
            emit("    JZ " + no_exception);
            emit("    JMP " + landing);
            emit(no_exception + ":");
        } else {
            emit_pending_check();
        }
    }

    void emit_bad_alloc_throw() {
        ClassInfo* info = find_class("std::bad_alloc");
        if (info == nullptr) {
            emit("    MOV R0, #1");
            emit("    CALL exit");
            return;
        }
        uses_exceptions_ = true;
        Name bad_name;
        bad_name.parts.push_back("std");
        bad_name.parts.push_back("bad_alloc");
        const int type_id = exception_type_id(Type::make_named(bad_name));
        const int object =
            allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
        emit("    MOV R0, #" + std::to_string(std::max(1, info->size)));
        emit("    CALL __cxa_allocate_exception");
        const std::string allocated = new_label("bad_alloc_object");
        emit("    CMP R0, #0");
        emit("    JNZ " + allocated);
        emit("    MOV R0, #1");
        emit("    CALL exit");
        emit(allocated + ":");
        emit("    STORE R0, [R7 + " + std::to_string(object) + "]");
        emit("    LEA R1, [" + typeinfo_symbol(info) + "]");
        emit("    STORE R1, [R0 - 12]");
        emit("    MOV R1, #0");
        emit("    STORE R1, [R0 - 8]");
        emit("    STORE R1, [R0 - 4]");
        emit("    STORE R0, [__kr32_exception_object]");
        emit("    MOV R0, #" + std::to_string(type_id));
        emit("    STORE R0, [__kr32_exception_type]");
        emit("    MOV R0, #1");
        emit("    STORE R0, [__kr32_exception_pending]");
        const std::string throw_start = new_label("bad_alloc_throw");
        const std::string throw_end = new_label("bad_alloc_throw_end");
        const std::string throw_landing = new_label("bad_alloc_landing");
        const std::string throw_continue = new_label("bad_alloc_continue");
        emit(throw_start + ":");
        emit("    CALL __kr32_throw");
        emit(throw_end + ":");
        emit("    JMP " + throw_continue);
        emit_unwind_landing(throw_landing);
        emit(throw_continue + ":");
        record_call_site(throw_start, throw_end, throw_landing);
    }

    void emit_dynamic_destructor(ClassInfo* info, int address_temp) {
        if (info == nullptr) {
            return;
        }
        Function* destructor = find_destructor(info);
        if (destructor == nullptr) {
            return;
        }
        const auto slot = info->virtual_slots.find("@dtor");
        if (slot != info->virtual_slots.end()) {
            emit("    LOAD R4, [R7 + " + std::to_string(address_temp) + "]");
            emit("    LOAD R1, [R4]");
            emit("    LOAD R12, [R1 + " +
                 std::to_string(slot->second * 4) + "]");
            emit("    MOV R0, R4");
            emit("    CALL R12");
            emit_pending_check();
            return;
        }
        emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
        emit("    CALL " + destructor->symbol);
        emit_pending_check();
    }

    void store_new_value(const TypePtr& type, const Expr& value, int address_temp) {
        if (is_fp(type)) {
            gen_as_fp(value, type);
            emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
            emit(std::string("    FSTORE.") +
                 (is_float32(type) ? "W" : "D") + " F0, [R1]");
            return;
        }
        gen_expr(value);
        emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
        store_value(type);
    }

    TypePtr gen_new(const Expr& expression) {
        const TypePtr pointee = resolve_type(expression.type);
        ClassInfo* info = class_of(pointee);
        const bool array = !expression.children.empty();
        const int element_size = std::max(1, type_size(pointee));
        const int alignment = std::max(4, type_align(pointee));
        const int pointer_temp = allocate_local(Type::make_pointer(pointee));
        const std::string done = new_label("new_done");

        // Placement new: construct into caller-provided storage; no allocation.
        if (!expression.placement_arguments.empty()) {
            if (array) {
                error(expression.location, "placement new[] is not supported");
                emit("    MOV R0, #0");
                return Type::make_pointer(pointee);
            }
            if (expression.placement_arguments.size() != 1) {
                error(expression.location, "placement new expects one pointer argument");
                emit("    MOV R0, #0");
                return Type::make_pointer(pointee);
            }
            gen_expr(*expression.placement_arguments[0]);
            emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
            if (info != nullptr) {
                if (expression.new_initializer != nullptr &&
                    !expression.new_initializer->children.empty()) {
                    error(expression.location,
                          "initializer-list new for class types is not supported");
                }
                construct_object_at(info, pointer_temp, expression.arguments);
            } else if (expression.new_initializer != nullptr) {
                emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                emit("    MOV R1, #0");
                emit("    MOV R2, #" + std::to_string(element_size));
                emit("    CALL memset");
                if (!expression.new_initializer->children.empty()) {
                    if (expression.new_initializer->children.size() > 1) {
                        error(expression.location,
                              "too many initializers for scalar new");
                    }
                    store_new_value(pointee,
                                    *expression.new_initializer->children[0],
                                    pointer_temp);
                }
            } else if (expression.new_value_initialize) {
                emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                emit("    MOV R1, #0");
                emit("    MOV R2, #" + std::to_string(element_size));
                emit("    CALL memset");
            } else if (expression.arguments.size() == 1) {
                store_new_value(pointee, *expression.arguments[0], pointer_temp);
            }
            emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
            return Type::make_pointer(pointee);
        }

        if (array) {
            gen_expr(*expression.children[0]);
            const int count_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(count_temp) + "]");
            emit("    MOV R0, #" + std::to_string(element_size));
            emit("    LOAD R1, [R7 + " + std::to_string(count_temp) + "]");
            emit("    MOV R2, #" + std::to_string(alignment));
            emit("    CALL __kr32_new_array");
        } else {
            emit("    MOV R0, #" + std::to_string(element_size));
            emit("    MOV R1, #" + std::to_string(alignment));
            emit("    CALL __kr32_new_alloc");
        }
        emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
        const std::string allocated = new_label("new_allocated");
        emit("    CMP R0, #0");
        emit("    JNZ " + allocated);
        if (expression.new_nothrow) {
            emit("    MOV R0, #0");
            emit("    STORE R0, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    JMP " + done);
        } else {
            emit_bad_alloc_throw();
            emit("    JMP " + done);
        }
        emit(allocated + ":");

        if (array) {
            const int bound_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            // The bound was stored in a temporary before allocation; find it by
            // recomputing the count header instead, which the allocator saved.
            emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    LOAD R1, [R0 - 8]");
            emit("    STORE R1, [R7 + " + std::to_string(bound_temp) + "]");

            if (expression.new_initializer != nullptr &&
                !(info != nullptr && expression.new_initializer->children.empty())) {
                if (info != nullptr && !expression.new_initializer->children.empty()) {
                    error(expression.location,
                          "initializer-list new[] for class types is not supported");
                } else {
                    emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                    emit("    MOV R1, #0");
                    emit("    LOAD R2, [R7 + " + std::to_string(bound_temp) + "]");
                    if (element_size != 1) {
                        emit("    MUL R2, R2, #" + std::to_string(element_size));
                    }
                    emit("    CALL memset");
                    int literal_bound = -1;
                    if (expression.children[0]->kind == ExprKind::IntegerLiteral) {
                        literal_bound = static_cast<int>(expression.children[0]->integer);
                    }
                    for (std::size_t index = 0;
                         index < expression.new_initializer->children.size(); ++index) {
                        if (literal_bound >= 0 &&
                            index >= static_cast<std::size_t>(literal_bound)) {
                            error(expression.location,
                                  "too many initializers for new array");
                            break;
                        }
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(pointer_temp) + "]");
                        if (index > 0) {
                            emit("    ADD R0, R0, #" +
                                 std::to_string(index * element_size));
                        }
                        emit("    STORE R0, [R7 + " + std::to_string(bound_temp) +
                             "]");
                        store_new_value(pointee,
                                        *expression.new_initializer->children[index],
                                        bound_temp);
                    }
                }
            } else if (info != nullptr) {
                Function* constructor = find_own_constructor(info, 0);
                if (constructor != nullptr) {
                    const int index_temp =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    const std::string loop_label = new_label("new_array");
                    const std::string done_label = new_label("new_array_done");
                    emit("    MOV R0, #0");
                    emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit(loop_label + ":");
                    emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    LOAD R1, [R7 + " + std::to_string(bound_temp) + "]");
                    emit("    CMP R0, R1");
                    emit("    JGE " + done_label);
                    emit("    MUL R1, R0, #" + std::to_string(element_size));
                    emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                    emit("    ADD R0, R0, R1");
                    const std::string ctor_start = new_label("array_ctor_start");
                    const std::string ctor_end = new_label("array_ctor_end");
                    const std::string ctor_landing = new_label("array_ctor_landing");
                    const std::string ctor_continue = new_label("array_ctor_continue");
                    emit(ctor_start + ":");
                    emit("    CALL " + constructor->symbol);
                    emit(ctor_end + ":");
                    emit("    JMP " + ctor_continue);
                    emit(ctor_landing + ":");
                    if (Function* destructor = find_destructor(info)) {
                        const int unwind_index =
                            allocate_local(Type::make_builtin(BuiltinKind::Int));
                        const int unwind_address =
                            allocate_local(Type::make_pointer(
                                Type::make_builtin(BuiltinKind::Void)));
                        const std::string unwind_loop =
                            new_label("new_array_unwind");
                        const std::string unwind_done =
                            new_label("new_array_unwind_done");
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(index_temp) + "]");
                        emit("    SUB R0, R0, #1");
                        emit("    STORE R0, [R7 + " +
                             std::to_string(unwind_index) + "]");
                        emit(unwind_loop + ":");
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(unwind_index) + "]");
                        emit("    CMP R0, #0");
                        emit("    JLT " + unwind_done);
                        emit("    MUL R1, R0, #" + std::to_string(element_size));
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(pointer_temp) + "]");
                        emit("    ADD R0, R0, R1");
                        emit("    STORE R0, [R7 + " +
                             std::to_string(unwind_address) + "]");
                        emit("    CALL " + destructor->symbol);
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(unwind_index) + "]");
                        emit("    SUB R0, R0, #1");
                        emit("    STORE R0, [R7 + " +
                             std::to_string(unwind_index) + "]");
                        emit("    JMP " + unwind_loop);
                        emit(unwind_done + ":");
                    }
                    emit("    LOAD R0, [R7 + " +
                         std::to_string(pointer_temp) + "]");
                    emit("    CALL __kr32_new_free");
                    if (!exception_handlers_.empty()) {
                        const ExceptionHandler handler = exception_handlers_.back();
                        emit_destructors_from(handler.scope_depth);
                        emit("    JMP " + handler.label);
                    } else {
                        emit("    JMP _Unwind_Resume");
                    }
                    emit(ctor_continue + ":");
                    record_call_site(ctor_start, ctor_end, ctor_landing);
                    emit("    LOAD R0, [R7 + " +
                         std::to_string(index_temp) + "]");
                    emit("    ADD R0, R0, #1");
                    emit("    STORE R0, [R7 + " +
                         std::to_string(index_temp) + "]");
                    emit("    JMP " + loop_label);
                    emit(done_label + ":");
                } else {
                    const int index_temp =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    const int element_temp =
                        allocate_local(Type::make_pointer(
                            Type::make_builtin(BuiltinKind::Void)));
                    const std::vector<ExprPtr> empty;
                    const std::string loop_label = new_label("new_array_default");
                    const std::string done_label = new_label("new_array_default_done");
                    emit("    MOV R0, #0");
                    emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit(loop_label + ":");
                    emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    LOAD R1, [R7 + " + std::to_string(bound_temp) + "]");
                    emit("    CMP R0, R1");
                    emit("    JGE " + done_label);
                    emit("    MUL R1, R0, #" + std::to_string(element_size));
                    emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                    emit("    ADD R0, R0, R1");
                    emit("    STORE R0, [R7 + " + std::to_string(element_temp) + "]");
                    construct_object_at(info, element_temp, empty);
                    emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    ADD R0, R0, #1");
                    emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    JMP " + loop_label);
                    emit(done_label + ":");
                }
            } else if (expression.new_value_initialize) {
                emit("    LOAD R0, [R7 + " +
                     std::to_string(pointer_temp) + "]");
                emit("    MOV R1, #0");
                emit("    LOAD R2, [R7 + " + std::to_string(bound_temp) + "]");
                if (element_size != 1) {
                    emit("    MUL R2, R2, #" + std::to_string(element_size));
                }
                emit("    CALL memset");
            }
        } else if (info != nullptr) {
            if (expression.new_initializer != nullptr &&
                !expression.new_initializer->children.empty()) {
                error(expression.location,
                      "initializer-list new for class types is not supported");
            }
            const std::string cleanup = new_label("new_constructor_cleanup");
            const std::string after = new_label("new_constructor_after");
            construct_object_at(info, pointer_temp, expression.arguments,
                                cleanup);
            emit("    JMP " + after);
            emit(cleanup + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    CALL __kr32_new_free");
            if (!exception_handlers_.empty()) {
                const ExceptionHandler handler = exception_handlers_.back();
                emit_destructors_from(handler.scope_depth);
                emit("    JMP " + handler.label);
            } else {
                emit("    JMP _Unwind_Resume");
            }
            emit(after + ":");
        } else if (expression.new_initializer != nullptr) {
            emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    MOV R1, #0");
            emit("    MOV R2, #" + std::to_string(element_size));
            emit("    CALL memset");
            if (!expression.new_initializer->children.empty()) {
                if (expression.new_initializer->children.size() > 1) {
                    error(expression.location,
                          "too many initializers for scalar new");
                }
                store_new_value(pointee,
                                *expression.new_initializer->children[0],
                                pointer_temp);
            }
        } else if (expression.new_value_initialize) {
            emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    MOV R1, #0");
            emit("    MOV R2, #" + std::to_string(element_size));
            emit("    CALL memset");
        } else if (expression.arguments.size() == 1) {
            store_new_value(pointee, *expression.arguments[0], pointer_temp);
        } else if (!expression.arguments.empty()) {
            error(expression.location, "too many arguments for scalar new");
        }

        emit(done + ":");
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
        const std::string done = new_label("delete_done");
        emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
        emit("    CMP R0, #0");
        emit("    JZ " + done);
        if (expression.text == "[]") {
            const int count_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            const int index_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            const int element_temp =
                allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
            emit("    LOAD R1, [R7 + " + std::to_string(pointer_temp) + "]");
            emit("    LOAD R0, [R1 - 8]");
            emit("    STORE R0, [R7 + " + std::to_string(count_temp) + "]");
            if (ClassInfo* info = class_of(pointee)) {
                if (find_destructor(info) != nullptr) {
                    const std::string loop_label = new_label("delete_array");
                    const std::string elements_done = new_label("delete_array_done");
                    emit("    MOV R0, #0");
                    emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit(loop_label + ":");
                    emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    LOAD R1, [R7 + " + std::to_string(count_temp) + "]");
                    emit("    CMP R0, R1");
                    emit("    JGE " + elements_done);
                    emit("    MUL R1, R0, #" +
                         std::to_string(std::max(1, type_size(pointee))));
                    emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
                    emit("    ADD R0, R0, R1");
                    emit("    STORE R0, [R7 + " + std::to_string(element_temp) + "]");
                    emit_dynamic_destructor(info, element_temp);
                    emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    ADD R0, R0, #1");
                    emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
                    emit("    JMP " + loop_label);
                    emit(elements_done + ":");
                }
            }
        } else if (ClassInfo* info = class_of(pointee)) {
            emit_dynamic_destructor(info, pointer_temp);
        }
        emit("    LOAD R0, [R7 + " + std::to_string(pointer_temp) + "]");
        emit("    CALL __kr32_new_free");
        emit(done + ":");
        emit("    MOV R0, #0");
        return Type::make_builtin(BuiltinKind::Void);
    }

    std::string exception_type_key(const TypePtr& type) {
        TypePtr resolved = resolve_type(type);
        if (resolved) {
            resolved = resolved->clone();
            resolved->is_const = false;
            resolved->is_volatile = false;
        }
        return mangle_type(decay(resolved));
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
            if (ClassInfo* info = class_of(type)) {
                gen_address(*statement.expression);
                const int source =
                    allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
                emit("    STORE R0, [R7 + " + std::to_string(source) + "]");
                emit("    MOV R0, #" + std::to_string(info->size));
                emit("    CALL __cxa_allocate_exception");
                const int object =
                    allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
                emit("    STORE R0, [R7 + " + std::to_string(object) + "]");
                const std::vector<std::string> parts =
                    split_qualified(info->qualified);
                const std::string typeinfo = "_ZTI" + mangle_qualified(parts);
                emit("    LEA R1, [" + typeinfo + "]");
                emit("    STORE R1, [R0 - 12]");
                if (Function* destructor = find_destructor(info)) {
                    emit("    LEA R1, [" + destructor->symbol + "]");
                } else {
                    emit("    MOV R1, #0");
                }
                emit("    STORE R1, [R0 - 8]");
                emit("    MOV R1, #0");
                emit("    STORE R1, [R0 - 4]");
                emit("    LOAD R1, [R7 + " + std::to_string(source) + "]");
                emit("    MOV R2, #" + std::to_string(info->size));
                emit("    CALL memcpy");
                emit("    LOAD R0, [R7 + " + std::to_string(object) + "]");
                emit("    STORE R0, [__kr32_exception_object]");
            } else {
                gen_expr(*statement.expression);
                if (is_fp(type)) {
                    emit(std::string("    FSTORE.") +
                         (is_float32(type) ? "W" : "D") +
                         " F0, [__kr32_exception_value]");
                } else {
                    emit("    STORE R0, [__kr32_exception_value]");
                }
            }
            emit("    MOV R0, #" + std::to_string(exception_type_id(type)));
            emit("    STORE R0, [__kr32_exception_type]");
        }
        if (!statement.expression) {
            emit("    LOAD R0, [__kr32_exception_object]");
            emit("    CALL __cxa_rethrow");
        }
        emit("    MOV R0, #1");
        emit("    STORE R0, [__kr32_exception_pending]");
        const std::string throw_start = new_label("throw_start");
        const std::string throw_end = new_label("throw_end");
        const std::string throw_landing = new_label("throw_landing");
        const std::string throw_continue = new_label("throw_continue");
        emit(throw_start + ":");
        emit("    CALL __kr32_throw");
        emit(throw_end + ":");
        emit("    JMP " + throw_continue);
        emit_unwind_landing(throw_landing);
        emit(throw_continue + ":");
        record_call_site(throw_start, throw_end, throw_landing);
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
        const std::string lsda_label = new_label("lsda");
        const std::string no_match_label = new_label("catch_no_match");
        LsdaRecord record;
        record.label = lsda_label;
        emit("    LEA R0, [" + lsda_label + "]");
        emit("    LOAD R1, [__kr32_exception_type]");
        emit("    CALL __kr32_personality_v0");
        emit("    CMP R0, #0");
        emit("    JZ " + no_match_label);
        emit("    JMP R0");
        for (const CatchHandler& clause : statement.handlers) {
            const std::string clause_label = new_label("catch_clause");
            int type_id = 0;
            if (clause.parameter && clause.parameter->type) {
                TypePtr type = resolve_type(clause.parameter->type);
                if (type && (type->kind == TypeKind::LValueRef ||
                             type->kind == TypeKind::RValueRef)) {
                    type = type->element;
                }
                type_id = exception_type_id(type);
            }
            record.entries.emplace_back(type_id, clause_label);
            emit(clause_label + ":");
            emit("    MOV R0, #0");
            emit("    STORE R0, [__kr32_exception_pending]");
            emit("    LOAD R0, [__kr32_exception_object]");
            emit("    CALL __cxa_begin_catch");
            push_scope();
            if (clause.parameter && clause.parameter->type) {
                const TypePtr declared = resolve_type(clause.parameter->type);
                const bool is_reference =
                    declared && (declared->kind == TypeKind::LValueRef ||
                                 declared->kind == TypeKind::RValueRef);
                const TypePtr type = is_reference ? declared->element : declared;
                Local local;
                local.type = declared;
                local.is_reference = is_reference;
                if (ClassInfo* info = class_of(type)) {
                    if (is_reference) {
                        local.offset = allocate_local(
                            Type::make_pointer(Type::make_builtin(BuiltinKind::Void)));
                        if (!clause.parameter->name.parts.empty()) {
                            local_scopes_.back()[clause.parameter->name.parts.back()] =
                                local;
                        }
                        emit("    LOAD R0, [__kr32_exception_object]");
                        emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
                    } else {
                        local.offset = allocate_local(type);
                        if (!clause.parameter->name.parts.empty()) {
                            local_scopes_.back()[clause.parameter->name.parts.back()] =
                                local;
                        }
                        emit("    LOAD R1, [__kr32_exception_object]");
                        emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                        emit("    MOV R2, #" + std::to_string(info->size));
                        emit("    CALL memcpy");
                    }
                } else {
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
            }
            if (clause.body) {
                ++active_catch_count_;
                gen_statement(*clause.body);
                --active_catch_count_;
            }
            pop_scope();
            emit("    LOAD R0, [__kr32_exception_object]");
            emit("    CALL __cxa_end_catch");
            emit("    JMP " + end_label);
        }
        exception_lsdas_.push_back(std::move(record));
        emit(no_match_label + ":");
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

    void emit_unwind_landing(const std::string& label) {
        emit(label + ":");
        if (!exception_handlers_.empty()) {
            const ExceptionHandler handler = exception_handlers_.back();
            emit_destructors_from(handler.scope_depth);
            emit("    JMP " + handler.label);
        } else {
            emit_destructors_from(0);
            emit("    JMP _Unwind_Resume");
        }
    }

    void record_call_site(const std::string& start, const std::string& end,
                          const std::string& landing) {
        current_call_sites_.push_back({start, end, landing});
    }

    bool atomic_intrinsic_name(const Expr& call, std::string& name) {
        if (call.children.empty() || call.children[0]->kind != ExprKind::Name) {
            return false;
        }
        name = call.children[0]->name.to_string();
        return name == "__atomic_load_n" || name == "__atomic_store_n" ||
               name == "__atomic_fetch_add" ||
               name == "__atomic_compare_exchange_n";
    }

    int atomic_argument_value(const Expr& expression) {
        gen_expr(expression);
        const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
        return temp;
    }

    TypePtr gen_atomic_intrinsic(const Expr& call, const std::string& name) {
        if (name == "__atomic_load_n") {
            gen_expr(*call.arguments[0]);
            emit("    MOV R1, R0");
            emit("    LL R0, [R1]");
            emit("    FENCE");
            return Type::make_builtin(BuiltinKind::Int);
        }
        if (name == "__atomic_store_n") {
            gen_expr(*call.arguments[0]);
            const int address = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(address) + "]");
            gen_expr(*call.arguments[1]);
            emit("    MOV R1, R0");
            emit("    LOAD R0, [R7 + " + std::to_string(address) + "]");
            emit("    STORE R1, [R0]");
            emit("    FENCE");
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Void);
        }
        if (name == "__atomic_fetch_add") {
            const int address = atomic_argument_value(*call.arguments[0]);
            const int value = atomic_argument_value(*call.arguments[1]);
            const std::string loop = new_label("atomic_add");
            emit(loop + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(address) + "]");
            emit("    LL R1, [R0]");
            emit("    LOAD R2, [R7 + " + std::to_string(value) + "]");
            emit("    ADD R2, R1, R2");
            emit("    SC R2, [R0]");
            emit("    JNZ " + loop);
            emit("    MOV R0, R1");
            return Type::make_builtin(BuiltinKind::Int);
        }
        const int address = atomic_argument_value(*call.arguments[0]);
        const int expected = atomic_argument_value(*call.arguments[1]);
        const int desired = atomic_argument_value(*call.arguments[2]);
        const std::string loop = new_label("atomic_cas");
        const std::string fail = new_label("atomic_cas_fail");
        const std::string done = new_label("atomic_cas_done");
        emit(loop + ":");
        emit("    LOAD R0, [R7 + " + std::to_string(address) + "]");
        emit("    LL R1, [R0]");
        emit("    LOAD R2, [R7 + " + std::to_string(expected) + "]");
        emit("    LOAD R2, [R2]");
        emit("    CMP R1, R2");
        emit("    JNZ " + fail);
        emit("    LOAD R3, [R7 + " + std::to_string(desired) + "]");
        emit("    SC R3, [R0]");
        emit("    JNZ " + loop);
        emit("    MOV R0, #1");
        emit("    JMP " + done);
        emit(fail + ":");
        emit("    LOAD R0, [R7 + " + std::to_string(expected) + "]");
        emit("    STORE R1, [R0]");
        emit("    MOV R0, #0");
        emit(done + ":");
        return Type::make_builtin(BuiltinKind::Bool);
    }

    bool syscall_intrinsic_name(const Expr& call, std::string& name) {
        if (call.children.empty() || call.children[0]->kind != ExprKind::Name) {
            return false;
        }
        name = call.children[0]->name.to_string();
        return name == "__builtin_kr32_syscall";
    }

    TypePtr gen_syscall_intrinsic(const Expr& call) {
        if (call.arguments.empty() ||
            call.arguments[0]->kind != ExprKind::IntegerLiteral) {
            diagnose_once(call, "syscall vector must be an integer literal");
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        }
        std::vector<int> arguments;
        for (std::size_t index = 1; index < call.arguments.size() && index <= 3;
             ++index) {
            gen_expr(*call.arguments[index]);
            const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
            arguments.push_back(temp);
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            emit("    LOAD R" + std::to_string(index) + ", [R7 + " +
                 std::to_string(arguments[index]) + "]");
        }
        emit("    SYSCALL #" + std::to_string(call.arguments[0]->integer));
        return Type::make_builtin(BuiltinKind::Int);
    }

    TypePtr gen_expr(const Expr& expression) {
        switch (expression.kind) {
        case ExprKind::IntegerLiteral: {
            const bool unsigned_suffix = has_unsigned_suffix(expression.text);
            const bool long_long = has_long_long_suffix(expression.text) ||
                                   expression.integer > 2147483647LL ||
                                   expression.integer < -2147483648LL;
            if (long_long) {
                const std::uint64_t bits =
                    static_cast<std::uint64_t>(expression.integer);
                emit("    MOV R0, #" +
                     std::to_string(static_cast<std::uint32_t>(bits)));
                emit("    MOV R1, #" +
                     std::to_string(static_cast<std::uint32_t>(bits >> 32)));
            } else {
                emit("    MOV R0, #" + std::to_string(expression.integer));
            }
            return Type::make_builtin(
                unsigned_suffix
                    ? (long_long ? BuiltinKind::UnsignedLongLong
                                 : BuiltinKind::UnsignedInt)
                    : (long_long ? BuiltinKind::LongLong : BuiltinKind::Int));
        }
        case ExprKind::CharacterLiteral:
        case ExprKind::BoolLiteral:
            emit("    MOV R0, #" + std::to_string(expression.integer));
            return expression.kind == ExprKind::BoolLiteral
                       ? Type::make_builtin(BuiltinKind::Bool)
                       : Type::make_builtin(BuiltinKind::Char);
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
            TypePtr capture_type;
            int capture_offset = 0;
            if (lambda_capture(expression.name, capture_type, capture_offset)) {
                emit("    LOAD R0, [R7 + " +
                     std::to_string(current_lambda_closure_offset_) + "]");
                if (capture_offset != 0) {
                    emit("    ADD R0, R0, #" + std::to_string(capture_offset));
                }
                if (current_lambda_function_ != nullptr &&
                    current_lambda_function_->lambda_capture_references.find(
                        expression.name.parts[0]) !=
                        current_lambda_function_->lambda_capture_references.end()) {
                    emit("    LOAD R0, [R0]");
                }
                if (is_fp(capture_type)) {
                    load_fp(capture_type);
                } else {
                    load_value(capture_type);
                }
                return capture_type;
            }
            if (Local* local = lookup_local(expression.name)) {
                gen_address(expression);
                TypePtr type = resolve_type(local->type);
                if (type && type->kind == TypeKind::LValueRef) {
                    type = type->element;
                }
                if (type && (type->kind == TypeKind::Array ||
                             class_of(type) != nullptr)) {
                    return type;
                }
                if (is_fp(type)) {
                    load_fp(type);
                    return type;
                }
                load_value(type);
                return type;
            }
            if (Global* global = lookup_template_static(expression)) {
                if (global->owner != nullptr &&
                    !can_access(global->owner, global->access)) {
                    diagnose_once(expression, "member is not accessible");
                    emit("    MOV R0, #0");
                    return Type::make_builtin(BuiltinKind::Int);
                }
                if (global->is_thread_local) {
                    emit_tls_address(global);
                } else {
                    emit("    LEA R0, [" + global->symbol + "]");
                }
                TypePtr type = resolve_type(global->type);
                if (type && (type->kind == TypeKind::Array ||
                             class_of(type) != nullptr)) {
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
                if (global->owner != nullptr &&
                    !can_access(global->owner, global->access)) {
                    diagnose_once(expression, "member is not accessible");
                    emit("    MOV R0, #0");
                    return Type::make_builtin(BuiltinKind::Int);
                }
                if (global->is_thread_local) {
                    emit_tls_address(global);
                } else {
                    emit("    LEA R0, [" + global->symbol + "]");
                }
                TypePtr type = resolve_type(global->type);
                if (type && (type->kind == TypeKind::Array ||
                             class_of(type) != nullptr)) {
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
                const auto integer_value = current_class_->template_integer_values.find(
                    expression.name.parts[0]);
                if (integer_value != current_class_->template_integer_values.end()) {
                    emit("    MOV R0, #" +
                         std::to_string(integer_value->second));
                    return Type::make_builtin(BuiltinKind::Int);
                }
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
        case ExprKind::Call: {
            const TypePtr result = gen_call(expression);
            if (result && (result->kind == TypeKind::LValueRef ||
                           result->kind == TypeKind::RValueRef) &&
                class_of(result->element) == nullptr) {
                if (is_fp(result->element)) {
                    load_fp(result->element);
                } else {
                    load_value(result->element);
                }
            }
            return result;
        }
        case ExprKind::Index: {
            if (Function* operation = find_member_operator(
                    class_of(type_of(*expression.children[0])), "operator[]",
                    type_of(*expression.children[1]), false)) {
                gen_member_operator_call(*expression.children[0],
                                         *expression.children[1], operation);
                const TypePtr result = type_of(expression);
                if (result && (result->kind == TypeKind::LValueRef ||
                               result->kind == TypeKind::RValueRef)) {
                    const TypePtr value = result->element;
                    if (class_of(value) != nullptr) {
                        return result;
                    }
                    if (is_fp(value)) {
                        load_fp(value);
                    } else {
                        load_value(value);
                    }
                    return result;
                }
                return result;
            }
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
        case ExprKind::Cast: {
            const TypePtr target = resolve_type(expression.type);
            if (expression.text == "dynamic_cast") {
                return gen_dynamic_cast(expression, target);
            }
            const TypePtr source = type_of(*expression.children[0]);
            gen_expr(*expression.children[0]);
            if (is_fp(target) && is_long_long(source)) {
                const std::string two32 = intern_fp_constant(4294967296.0, false);
                emit("    MOV R2, R0");
                emit("    MOV R3, R1");
                emit("    FCVT.D.W F0, R3");
                emit("    FLOAD.D F1, [" + two32 + "]");
                emit("    FMUL.D F0, F0, F1");
                emit("    FCVT.D.WU F2, R2");
                emit("    FADD.D F0, F0, F2");
                if (is_float32(target)) {
                    emit("    FCVT.S.D F0, F0");
                }
            } else if (is_fp(target) && !is_fp(source)) {
                emit(std::string("    FCVT.") + (is_float32(target) ? "S" : "D") +
                     ".W F0, R0");
            } else if (is_fp(target) && is_fp(source)) {
                convert_fp(source, target);
            } else if (!is_fp(target) && is_fp(source) &&
                       is_long_long(target)) {
                if (is_float32(source)) {
                    emit("    FCVT.D.S F0, F0");
                }
                const std::string zero = intern_fp_constant(0.0, false);
                const std::string two32 = intern_fp_constant(4294967296.0, false);
                const int negative_temp =
                    allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    FLOAD.D F1, [" + zero + "]");
                emit("    FLT.D R0, F0, F1");
                emit("    STORE R0, [R7 + " + std::to_string(negative_temp) + "]");
                const std::string positive = new_label("llconvpos");
                emit("    CMP R0, #0");
                emit("    JZ " + positive);
                emit("    FNEG.D F0, F0");
                emit(positive + ":");
                emit("    FLOAD.D F1, [" + two32 + "]");
                emit("    FDIV.D F2, F0, F1");
                emit("    FCVT.W.D R1, F2");
                emit("    FCVT.D.W F3, R1");
                emit("    FMUL.D F3, F3, F1");
                emit("    FSUB.D F4, F0, F3");
                emit("    FCVT.WU.D R0, F4");
                const std::string done = new_label("llconvdone");
                emit("    LOAD R2, [R7 + " + std::to_string(negative_temp) + "]");
                emit("    CMP R2, #0");
                emit("    JZ " + done);
                emit("    MOV R2, R0");
                emit("    MOV R3, R1");
                emit("    MOV R0, #0");
                emit("    MOV R1, #0");
                const std::string no_borrow = new_label("llconvnoborrow");
                const std::string borrow_done = new_label("llconvborrow");
                emit("    SUB R0, R0, R2");
                emit("    MOV R6, #0");
                emit("    JCS " + no_borrow);
                emit("    MOV R6, #1");
                emit("    JMP " + borrow_done);
                emit(no_borrow + ":");
                emit(borrow_done + ":");
                emit("    SUB R1, R1, R3");
                emit("    SUB R1, R1, R6");
                emit(done + ":");
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
            if (is_long_long(target) && !is_long_long(source) && !is_fp(source)) {
                if (is_unsigned_integer(source)) {
                    emit("    MOV R1, #0");
                } else {
                    emit("    MOV R1, R0");
                    emit("    SAR R1, R1, #31");
                }
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
            diagnose_once(expression, "expression is not supported until a later M9 phase");
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        case ExprKind::Lambda: {
            Function* function = materialize_lambda(expression);
            if (function->lambda_closure) {
                diagnose_once(
                    expression,
                    "capturing lambdas cannot be converted to function pointers");
            }
            emit("    LEA R0, [" + function->symbol + "]");
            return lambda_function_type(function);
        }
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
        if (op != "&" && op != "*") {
            if (!expression.is_prefix && (op == "++" || op == "--")) {
                if (Function* operation = find_member_operator(
                        class_of(type_of(*expression.children[0], false)),
                        "operator" + op, Type::make_builtin(BuiltinKind::Int),
                        false)) {
                    return gen_member_postfix(*expression.children[0],
                                              operation);
                }
            }
            if (Function* operation = find_member_operator(
                    class_of(type_of(*expression.children[0], false)),
                    "operator" + op, nullptr, true)) {
                return gen_member_unary(*expression.children[0], operation);
            }
        }
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
            if (resolved && resolved->kind == TypeKind::Pointer) {
                const int element_size =
                    resolved->element ? type_size(resolve_type(resolved->element)) : 1;
                if (element_size > 1) {
                    if (op == "++") {
                        emit("    ADD R0, R0, #" + std::to_string(element_size - 1));
                    } else {
                        emit("    SUB R0, R0, #" + std::to_string(element_size - 1));
                    }
                }
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
            if (is_long_long(operand_type)) {
                const std::string no_borrow = new_label("llnoborrow");
                const std::string borrow_done = new_label("llborrowdone");
                emit("    MOV R4, R0");
                emit("    MOV R5, R1");
                emit("    MOV R0, #0");
                emit("    MOV R1, #0");
                emit("    SUB R0, R0, R4");
                emit("    MOV R6, #0");
                emit("    JCS " + no_borrow);
                emit("    MOV R6, #1");
                emit("    JMP " + borrow_done);
                emit(no_borrow + ":");
                emit(borrow_done + ":");
                emit("    SUB R1, R1, R5");
                emit("    SUB R1, R1, R6");
            } else {
                emit("    NEG R0, R0");
            }
        } else if (op == "~") {
            if (is_long_long(operand_type)) {
                emit("    NOT R0, R0");
                emit("    NOT R1, R1");
            } else {
                emit("    NOT R0, R0");
            }
        } else if (op == "!") {
            emit("    CMP R0, #0");
            emit("    MOV R0, #0");
            const std::string done = new_label("not");
            emit("    JNZ " + done);
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

    TypePtr gen_long_long_binary(const Expr& expression, const TypePtr& left,
                                 const TypePtr& right) {
        const std::string& op = expression.text;
        const bool unsigned_operation = is_unsigned_operation(left, right);
        const bool comparison =
            op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
            op == ">=";
        gen_expr(*expression.children[0]);
        if (!is_long_long(left)) {
            emit("    MOV R1, R0");
            emit(std::string("    ") +
                 (is_unsigned_integer(left) ? "SHR" : "SAR") + " R1, R1, #31");
        }
        const int left_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        emit("    STORE R0, [R7 + " + std::to_string(left_temp) + "]");
        emit("    STORE R1, [R7 + " + std::to_string(left_temp + 4) + "]");
        gen_expr(*expression.children[1]);
        if (!is_long_long(right)) {
            emit("    MOV R5, R0");
            emit(std::string("    ") +
                 (is_unsigned_integer(right) ? "SHR" : "SAR") + " R5, R5, #31");
            emit("    MOV R4, R0");
        } else {
            emit("    MOV R4, R0");
            emit("    MOV R5, R1");
        }
        emit("    LOAD R0, [R7 + " + std::to_string(left_temp) + "]");
        emit("    LOAD R1, [R7 + " + std::to_string(left_temp + 4) + "]");
        if (comparison) {
            const std::string is_true = new_label("lltrue");
            const std::string is_false = new_label("llfalse");
            const std::string done = new_label("lldone");
            emit("    CMP R1, R5");
            if (op == "==") {
                emit("    JNZ " + is_false);
                emit("    CMP R0, R4");
                emit("    JZ " + is_true);
                emit("    JMP " + is_false);
            } else if (op == "!=") {
                emit("    JNZ " + is_true);
                emit("    CMP R0, R4");
                emit("    JZ " + is_false);
                emit("    JMP " + is_true);
            } else {
                if (op == "<" || op == "<=") {
                    emit(std::string("    ") + (unsigned_operation ? "JCS " : "JLT ") +
                         is_true);
                    emit(std::string("    ") + (unsigned_operation ? "JHI " : "JGT ") +
                         is_false);
                } else {
                    emit(std::string("    ") + (unsigned_operation ? "JHI " : "JGT ") +
                         is_true);
                    emit(std::string("    ") + (unsigned_operation ? "JCS " : "JLT ") +
                         is_false);
                }
                emit("    CMP R0, R4");
                if (op == "<") {
                    emit(std::string("    ") + (unsigned_operation ? "JCS " : "JLT ") +
                         is_true);
                } else if (op == "<=") {
                    emit(std::string("    ") + (unsigned_operation ? "JLS " : "JLE ") +
                         is_true);
                } else if (op == ">") {
                    emit(std::string("    ") + (unsigned_operation ? "JHI " : "JGT ") +
                         is_true);
                } else {
                    emit(std::string("    ") + (unsigned_operation ? "JHI " : "JGT ") +
                         is_true);
                    emit("    JZ " + is_true);
                }
                emit("    JMP " + is_false);
            }
            emit(is_true + ":");
            emit("    MOV R0, #1");
            emit("    JMP " + done);
            emit(is_false + ":");
            emit("    MOV R0, #0");
            emit(done + ":");
            return Type::make_builtin(BuiltinKind::Bool);
        }
        if (op == "+") {
            const std::string no_carry = new_label("llnocarry");
            emit("    ADD R0, R0, R4");
            emit("    MOV R6, #0");
            emit("    JNC " + no_carry);
            emit("    MOV R6, #1");
            emit(no_carry + ":");
            emit("    ADD R1, R1, R5");
            emit("    ADD R1, R1, R6");
        } else if (op == "-") {
            const std::string no_borrow = new_label("llnoborrow");
            const std::string borrow_done = new_label("llborrowdone");
            emit("    SUB R0, R0, R4");
            emit("    MOV R6, #0");
            emit("    JCS " + no_borrow);
            emit("    MOV R6, #1");
            emit("    JMP " + borrow_done);
            emit(no_borrow + ":");
            emit(borrow_done + ":");
            emit("    SUB R1, R1, R5");
            emit("    SUB R1, R1, R6");
        } else if (op == "&") {
            emit("    AND R0, R0, R4");
            emit("    AND R1, R1, R5");
        } else if (op == "|") {
            emit("    OR R0, R0, R4");
            emit("    OR R1, R1, R5");
        } else if (op == "^") {
            emit("    XOR R0, R0, R4");
            emit("    XOR R1, R1, R5");
        } else if (op == "*") {
            emit("    MUL R6, R0, R4");
            emit("    MULHU R12, R0, R4");
            emit("    MUL R13, R0, R5");
            emit("    MUL R14, R1, R4");
            emit("    ADD R12, R12, R13");
            emit("    ADD R12, R12, R14");
            emit("    MOV R0, R6");
            emit("    MOV R1, R12");
        } else if (op == "/" || op == "%") {
            emit("    MOV R2, R4");
            emit("    MOV R3, R5");
            const std::string helper =
                unsigned_operation
                    ? (op == "/" ? "__kr32_ulldiv" : "__kr32_ulmod")
                    : (op == "/" ? "__kr32_lldiv" : "__kr32_llmod");
            emit("    CALL " + helper);
        } else if (op == "<<" || op == ">>") {
            const std::string zero = new_label("llshift0");
            const std::string wide = new_label("llshiftwide");
            const std::string done = new_label("llshiftdone");
            emit("    CMP R4, #0");
            emit("    JZ " + zero);
            emit("    CMP R4, #32");
            emit("    JCS " + wide);
            emit("    MOV R6, R0");
            if (op == "<<") {
                emit("    SHL R0, R0, R4");
                emit("    SHL R1, R1, R4");
                emit("    MOV R12, #32");
                emit("    SUB R12, R12, R4");
                emit("    SHR R6, R6, R12");
                emit("    OR R1, R1, R6");
            } else if (unsigned_operation) {
                emit("    SHR R0, R0, R4");
                emit("    SHL R6, R1, R4");
                emit("    OR R0, R0, R6");
                emit("    SHR R1, R1, R4");
            } else {
                emit("    SHR R0, R0, R4");
                emit("    SHL R6, R1, R4");
                emit("    OR R0, R0, R6");
                emit("    SAR R1, R1, R4");
            }
            emit("    JMP " + done);
            emit(wide + ":");
            emit("    SUB R12, R4, #32");
            if (op == "<<") {
                emit("    MOV R1, R0");
                emit("    SHL R1, R1, R12");
                emit("    MOV R0, #0");
            } else if (unsigned_operation) {
                emit("    SHR R0, R1, R12");
                emit("    MOV R1, #0");
            } else {
                emit("    SAR R0, R1, R12");
                emit("    SAR R1, R1, #31");
            }
            emit("    JMP " + done);
            emit(zero + ":");
            emit(done + ":");
        }
        return Type::make_builtin(unsigned_operation
                                      ? BuiltinKind::UnsignedLongLong
                                      : BuiltinKind::LongLong);
    }

    TypePtr gen_member_operator_call(const Expr& left,
                                     const Expr& right,
                                     Function* operation) {
        const int this_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        gen_address(left);
        emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        const TypePtr parameter =
            operation->parameters.empty()
                ? Type::make_builtin(BuiltinKind::Void)
                : operation->parameters[0];
        int argument_temp = -1;
        bool floating_argument = false;
        bool class_argument = false;
        int class_size = 0;
        ClassInfo* parameter_class = class_of(parameter);
        if (parameter && (parameter->kind == TypeKind::LValueRef ||
                          parameter->kind == TypeKind::RValueRef)) {
            gen_address(right);
            argument_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(argument_temp) + "]");
        } else if (parameter_class != nullptr &&
                   is_register_class(parameter_class)) {
            gen_expr(right);
            argument_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(argument_temp) + "]");
            class_argument = true;
            class_size = parameter_class->size;
        } else if (is_fp(parameter)) {
            gen_as_fp(right, Type::make_builtin(BuiltinKind::Double));
            floating_argument = true;
        } else {
            gen_expr(right);
            argument_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(argument_temp) + "]");
        }
        emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        if (class_argument) {
            emit("    LOAD R2, [R7 + " + std::to_string(argument_temp) + "]");
            emit("    LOAD R1, [R2]");
            if (class_size > 4) {
                emit("    LOAD R2, [R2 + 4]");
            }
        } else if (!floating_argument && argument_temp >= 0) {
            emit("    LOAD R1, [R7 + " + std::to_string(argument_temp) + "]");
        }
        emit("    CALL " + operation->symbol);
        return operation->result;
    }

    TypePtr gen_zero_argument_member_call(const Expr& object,
                                          Function* operation) {
        const int this_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        gen_address(object);
        emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    CALL " + operation->symbol);
        return operation->result;
    }

    TypePtr gen_member_operator(const Expr& expression, Function* operation) {
        return gen_member_operator_call(*expression.children[0],
                                        *expression.children[1], operation);
    }

    TypePtr gen_member_operator_args(const Expr& object,
                                     const std::vector<ExprPtr>& arguments,
                                     Function* operation) {
        const int this_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        gen_address(object);
        emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        std::vector<int> temporary;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const TypePtr parameter =
                index < operation->parameters.size()
                    ? operation->parameters[index]
                    : Type::make_builtin(BuiltinKind::Int);
            if (parameter && (parameter->kind == TypeKind::LValueRef ||
                              parameter->kind == TypeKind::RValueRef)) {
                gen_address(*arguments[index]);
            } else {
                gen_expr(*arguments[index]);
            }
            const int slot =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(slot) + "]");
            temporary.push_back(slot);
        }
        emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        for (std::size_t index = 0; index < temporary.size(); ++index) {
            emit("    LOAD R" + std::to_string(index + 1) + ", [R7 + " +
                 std::to_string(temporary[index]) + "]");
        }
        emit("    CALL " + operation->symbol);
        return operation->result;
    }

    TypePtr gen_member_postfix(const Expr& expression, Function* operation) {
        const int this_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        gen_address(expression);
        emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    MOV R1, #0");
        emit("    CALL " + operation->symbol);
        return operation->result;
    }

    TypePtr gen_member_unary(const Expr& expression, Function* operation) {
        const int this_temp =
            allocate_local(Type::make_builtin(BuiltinKind::Int));
        gen_address(expression);
        emit("    STORE R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    LOAD R0, [R7 + " + std::to_string(this_temp) + "]");
        emit("    CALL " + operation->symbol);
        return operation->result;
    }

    TypePtr gen_binary(const Expr& expression) {
        const std::string& op = expression.text;
        const TypePtr left_operand = type_of(*expression.children[0]);
        if (Function* operation = find_member_operator(
                class_of(left_operand), "operator" + op,
                type_of(*expression.children[1]), false)) {
            return gen_member_operator(expression, operation);
        }
        if (Function* operation = find_free_operator(
                "operator" + op, left_operand,
                type_of(*expression.children[1]))) {
            return gen_free_operator_call(*expression.children[0],
                                          *expression.children[1], operation);
        }
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
        if (is_long_long(left_type) || is_long_long(right_type)) {
            return gen_long_long_binary(expression, left_type, right_type);
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

        const bool unsigned_operation =
            is_unsigned_operation(left_type, right_type);
        if (op == "+") emit("    ADD R0, R0, R1");
        else if (op == "-") emit("    SUB R0, R0, R1");
        else if (op == "*") emit("    MUL R0, R0, R1");
        else if (op == "/") emit(unsigned_operation ? "    DIV R0, R0, R1"
                                                     : "    DIVS R0, R0, R1");
        else if (op == "%") emit(unsigned_operation ? "    MOD R0, R0, R1"
                                                     : "    MODS R0, R0, R1");
        else if (op == "&") emit("    AND R0, R0, R1");
        else if (op == "|") emit("    OR R0, R0, R1");
        else if (op == "^") emit("    XOR R0, R0, R1");
        else if (op == "<<") emit("    SHL R0, R0, R1");
        else if (op == ">>") emit(unsigned_operation ? "    SHR R0, R0, R1"
                                                     : "    SAR R0, R0, R1");
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                 op == ">" || op == ">=") {
            const std::string true_label = new_label("true");
            const std::string end_label = new_label("cmpend");
            emit("    CMP R0, R1");
            if (op == "==") emit("    JZ " + true_label);
            if (op == "!=") emit("    JNZ " + true_label);
            if (unsigned_operation) {
                if (op == "<") emit("    JNC " + true_label);
                if (op == "<=") emit("    JLS " + true_label);
                if (op == ">") emit("    JHI " + true_label);
                if (op == ">=") emit("    JCS " + true_label);
            } else {
                if (op == "<") emit("    JLT " + true_label);
                if (op == "<=") emit("    JLE " + true_label);
                if (op == ">") emit("    JGT " + true_label);
                if (op == ">=") emit("    JGE " + true_label);
            }
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
            if (Function* operation = find_member_operator(
                    info, "operator" + expression.text,
                    type_of(*expression.children[1]), false)) {
                return gen_member_operator_call(*expression.children[0],
                                                *expression.children[1],
                                                operation);
            }
            if (expression.text != "=") {
                diagnose_once(expression,
                              "compound assignment on a class is not supported yet");
                return lhs_type;
            }
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
        if (is_long_long(lhs_type) && expression.text != "=") {
            const std::string op =
                expression.text.substr(0, expression.text.size() - 1);
            gen_address(*expression.children[0]);
            const int address_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");
            emit("    LOAD R0, [R7 + " + std::to_string(address_temp) + "]");
            load_value(lhs_type);
            const int left_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(left_temp) + "]");
            emit("    STORE R1, [R7 + " + std::to_string(left_temp + 4) + "]");
            gen_expr(*expression.children[1]);
            const TypePtr right_type = type_of(*expression.children[1]);
            if (!is_long_long(right_type)) {
                emit("    MOV R5, R0");
                emit(std::string("    ") +
                     (is_unsigned_integer(right_type) ? "SHR" : "SAR") +
                     " R5, R5, #31");
                emit("    MOV R4, R0");
            } else {
                emit("    MOV R4, R0");
                emit("    MOV R5, R1");
            }
            emit("    LOAD R0, [R7 + " + std::to_string(left_temp) + "]");
            emit("    LOAD R1, [R7 + " + std::to_string(left_temp + 4) + "]");
            const bool unsigned_operation =
                is_unsigned_operation(lhs_type, right_type);
            if (op == "+") {
                const std::string no_carry = new_label("llnocarry");
                emit("    ADD R0, R0, R4");
                emit("    MOV R6, #0");
                emit("    JNC " + no_carry);
                emit("    MOV R6, #1");
                emit(no_carry + ":");
                emit("    ADD R1, R1, R5");
                emit("    ADD R1, R1, R6");
            } else if (op == "-") {
                const std::string no_borrow = new_label("llnoborrow");
                const std::string borrow_done = new_label("llborrowdone");
                emit("    SUB R0, R0, R4");
                emit("    MOV R6, #0");
                emit("    JCS " + no_borrow);
                emit("    MOV R6, #1");
                emit("    JMP " + borrow_done);
                emit(no_borrow + ":");
                emit(borrow_done + ":");
                emit("    SUB R1, R1, R5");
                emit("    SUB R1, R1, R6");
            } else if (op == "*") {
                emit("    MUL R6, R0, R4");
                emit("    MULHU R12, R0, R4");
                emit("    MUL R13, R0, R5");
                emit("    MUL R14, R1, R4");
                emit("    ADD R12, R12, R13");
                emit("    ADD R12, R12, R14");
                emit("    MOV R0, R6");
                emit("    MOV R1, R12");
            } else if (op == "&") {
                emit("    AND R0, R0, R4");
                emit("    AND R1, R1, R5");
            } else if (op == "|") {
                emit("    OR R0, R0, R4");
                emit("    OR R1, R1, R5");
            } else if (op == "^") {
                emit("    XOR R0, R0, R4");
                emit("    XOR R1, R1, R5");
            } else if (op == "/" || op == "%") {
                emit("    MOV R2, R4");
                emit("    MOV R3, R5");
                emit("    CALL " +
                     std::string(unsigned_operation
                                     ? (op == "/" ? "__kr32_ulldiv" : "__kr32_ulmod")
                                     : (op == "/" ? "__kr32_lldiv" : "__kr32_llmod")));
            } else if (op == "<<" || op == ">>") {
                const std::string zero = new_label("llshift0");
                const std::string done = new_label("llshiftdone");
                emit("    CMP R4, #0");
                emit("    JZ " + zero);
                emit("    MOV R6, R0");
                if (op == "<<") {
                    emit("    SHL R0, R0, R4");
                    emit("    SHL R1, R1, R4");
                    emit("    MOV R12, #32");
                    emit("    SUB R12, R12, R4");
                    emit("    SHR R6, R6, R12");
                    emit("    OR R1, R1, R6");
                } else if (unsigned_operation) {
                    emit("    SHR R0, R0, R4");
                    emit("    SHL R6, R1, R4");
                    emit("    OR R0, R0, R6");
                    emit("    SHR R1, R1, R4");
                } else {
                    emit("    SHR R0, R0, R4");
                    emit("    SHL R6, R1, R4");
                    emit("    OR R0, R0, R6");
                    emit("    SAR R1, R1, R4");
                }
                emit("    JMP " + done);
                emit(zero + ":");
                emit(done + ":");
            } else {
                diagnose_once(expression, "unsupported long long assignment operator");
            }
            emit("    MOV R2, R1");
            emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
            store_value(lhs_type);
            return lhs_type;
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
            if (is_pointer(lhs_type)) {
                if (op != "+" && op != "-") {
                    diagnose_once(expression,
                                  "only += and -= are valid for pointer compound assignment");
                } else {
                    const int element_size =
                        lhs_type->element ? type_size(lhs_type->element) : 1;
                    if (element_size > 1) {
                        emit("    MUL R1, R1, #" + std::to_string(element_size));
                    }
                }
            }
            emit("    LOAD R0, [R7 + " + std::to_string(left_temp) + "]");
            const bool unsigned_operation =
                is_unsigned_operation(lhs_type, type_of(*expression.children[1]));
            if (op == "+") emit("    ADD R0, R0, R1");
            else if (op == "-") emit("    SUB R0, R0, R1");
            else if (op == "*") emit("    MUL R0, R0, R1");
            else if (op == "/") emit(unsigned_operation ? "    DIV R0, R0, R1"
                                                         : "    DIVS R0, R0, R1");
            else if (op == "%") emit(unsigned_operation ? "    MOD R0, R0, R1"
                                                         : "    MODS R0, R0, R1");
            else if (op == "&") emit("    AND R0, R0, R1");
            else if (op == "|") emit("    OR R0, R0, R1");
            else if (op == "^") emit("    XOR R0, R0, R1");
            else if (op == "<<") emit("    SHL R0, R0, R1");
            else if (op == ">>") emit(unsigned_operation ? "    SHR R0, R0, R1"
                                                         : "    SAR R0, R0, R1");
            else diagnose_once(expression, "unsupported assignment operator");
        }
        if (is_long_long(lhs_type)) {
            if (expression.text == "=") {
                const TypePtr value_type = type_of(*expression.children[1]);
                if (!is_long_long(value_type)) {
                    emit("    MOV R1, R0");
                    emit(std::string("    ") +
                         (is_unsigned_integer(value_type) ? "SHR" : "SAR") +
                         " R1, R1, #31");
                }
            }
            emit("    MOV R2, R1");
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
            expression.children[0]->kind == ExprKind::Name &&
            expression.children[0]->name.to_string() == "std::move" &&
            expression.arguments.size() == 1) {
            return gen_expr(*expression.arguments[0]);
        }
        if (!expression.children.empty() &&
            expression.children[0]->kind == ExprKind::Name) {
            if (ClassInfo* info =
                    find_class(expression.children[0]->name.to_string())) {
                const int offset = allocate_local_bytes(info->size);
                construct_object(info, offset, expression.arguments);
                emit("    LEA R0, [R7 + " + std::to_string(offset) + "]");
                Name name;
                name.parts = split_qualified(info->qualified);
                return Type::make_named(name);
            }
        }
        if (!expression.children.empty() &&
            may_be_callable_object(*expression.children[0])) {
            if (Function* operation = find_call_operator(
                    class_of(type_of(*expression.children[0], false)),
                    expression.arguments)) {
                if (expression.arguments.empty()) {
                    return gen_member_unary(*expression.children[0], operation);
                }
                if (expression.arguments.size() == 1) {
                    return gen_member_operator_call(
                        *expression.children[0], *expression.arguments[0],
                        operation);
                }
                return gen_member_operator_args(*expression.children[0],
                                                expression.arguments,
                                                operation);
            }
        }
        std::string list_method;
        ClassInfo* list_info = nullptr;
        MemberInfo* list_data = nullptr;
        MemberInfo* list_size = nullptr;
        if (initializer_list_member_call(expression, list_method, list_info,
                                         list_data, list_size)) {
            gen_expr(*expression.children[0]->children[0]);
            const int object_temp =
                allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Int)));
            emit("    STORE R0, [R7 + " + std::to_string(object_temp) + "]");
            emit("    LOAD R1, [R7 + " + std::to_string(object_temp) + "]");
            emit("    LOAD R0, [R1 + " +
                 std::to_string(list_data->offset) + "]");
            if (list_method == "end") {
                emit("    LOAD R1, [R7 + " + std::to_string(object_temp) + "]");
                emit("    LOAD R1, [R1 + " +
                     std::to_string(list_size->offset) + "]");
                const int element_size = type_size(list_data->type->element);
                if (element_size != 1) {
                    emit("    MUL R1, R1, #" + std::to_string(element_size));
                }
                emit("    ADD R0, R0, R1");
            }
            return Type::make_pointer(list_data->type->element);
        }
        std::string intrinsic;
        if (matrix_intrinsic_name(expression, intrinsic)) {
            return emit_matrix_intrinsic(expression, intrinsic);
        }
        if (atomic_intrinsic_name(expression, intrinsic)) {
            return gen_atomic_intrinsic(expression, intrinsic);
        }
        if (syscall_intrinsic_name(expression, intrinsic)) {
            return gen_syscall_intrinsic(expression);
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
        Function* function = nullptr;
        int lambda_closure_offset = -1;
        if (!expression.children.empty() &&
            expression.children[0]->kind == ExprKind::Lambda) {
            function = materialize_lambda(*expression.children[0]);
            if (function->lambda_closure) {
                diagnose_once(expression,
                              "immediately invoked capturing lambda is not supported yet");
            }
        } else if (!expression.children.empty() &&
                   expression.children[0]->kind == ExprKind::Name) {
            if (Local* local = lookup_local(expression.children[0]->name)) {
                function = local->lambda_target;
                lambda_closure_offset = local->lambda_closure_offset;
            }
        }
        if (function == nullptr) {
            function = resolve_call(expression);
        }
        if (!function) {
            emit("    MOV R0, #0");
            return Type::make_builtin(BuiltinKind::Int);
        }
        if (!can_access(function->owner, function->access)) {
            diagnose_once(expression, "member function is not accessible");
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
        int integer_argument =
            (member_call ? 1 : 0) + (lambda_closure_offset >= 0 ? 1 : 0);
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
                } else if (is_long_long(argument_type)) {
                    gen_expr(*expression.arguments[index]);
                    emit("    STORE R0, [R7 + " + std::to_string(temp) + "]");
                    emit("    STORE R1, [R7 + " +
                         std::to_string(temp + 4) + "]");
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
            } else if (ClassInfo* parameter_class = class_of(parameter)) {
                if (aggregate_abi(parameter_class) == AggregateAbi::Floating) {
                    gen_expr(*expression.arguments[index]);
                    const int argument_object = allocate_local_bytes(16);
                    emit("    MOV R1, R0");
                    emit("    MOV R2, #" + std::to_string(parameter_class->size));
                    emit("    LEA R0, [R7 + " +
                         std::to_string(argument_object) + "]");
                    emit("    CALL memcpy");
                    slot.fp = true;
                    slot.type = parameter;
                    slot.offset = argument_object;
                    fp_argument += parameter_class->size > 8 ? 2 : 1;
                    slots.push_back(std::move(slot));
                    continue;
                }
                if (is_register_class(parameter_class)) {
                    gen_expr(*expression.arguments[index]);
                    const int address_temp =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");
                    emit("    LOAD R1, [R7 + " + std::to_string(address_temp) + "]");
                    emit("    LOAD R0, [R1]");
                    if (parameter_class->size > 4) {
                        emit("    LOAD R1, [R1 + 4]");
                    }
                    slot.type = parameter;
                    slot.offset = allocate_local(parameter);
                    emit("    STORE R0, [R7 + " + std::to_string(slot.offset) + "]");
                    if (parameter_class->size > 4) {
                        emit("    STORE R1, [R7 + " +
                             std::to_string(slot.offset + 4) + "]");
                        integer_argument += 2;
                    } else {
                        ++integer_argument;
                    }
                    slots.push_back(std::move(slot));
                    continue;
                }
                if (is_initializer_list_class(parameter_class) &&
                    expression.arguments[index]->kind ==
                        ExprKind::InitializerList) {
                    const int object = prepare_initializer_list_argument(
                        parameter_class, parameter, *expression.arguments[index]);
                    slot.offset =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    if (object >= 0) {
                        emit("    LEA R0, [R7 + " + std::to_string(object) + "]");
                    } else {
                        emit("    MOV R0, #0");
                    }
                    emit("    STORE R0, [R7 + " + std::to_string(slot.offset) + "]");
                    ++integer_argument;
                    slots.push_back(std::move(slot));
                    continue;
                }
                gen_expr(*expression.arguments[index]);
                const int argument_object = allocate_local(parameter);
                emit("    MOV R1, R0");
                emit("    MOV R2, #" + std::to_string(parameter_class->size));
                emit("    LEA R0, [R7 + " + std::to_string(argument_object) + "]");
                emit("    CALL memcpy");
                slot.offset = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    LEA R0, [R7 + " + std::to_string(argument_object) + "]");
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
            } else if (is_long_long(parameter)) {
                gen_expr(*expression.arguments[index]);
                const TypePtr argument_type =
                    type_of(*expression.arguments[index]);
                if (!is_long_long(argument_type)) {
                    emit("    MOV R1, R0");
                    emit(std::string("    ") +
                         (is_unsigned_integer(argument_type) ? "SHR" : "SAR") +
                         " R1, R1, #31");
                }
                slot.type = parameter;
                slot.offset = allocate_local(parameter);
                emit("    STORE R0, [R7 + " + std::to_string(slot.offset) + "]");
                emit("    STORE R1, [R7 + " +
                     std::to_string(slot.offset + 4) + "]");
                integer_argument += 2;
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
        if (lambda_closure_offset >= 0) {
            emit("    LEA R0, [R7 + " + std::to_string(lambda_closure_offset) + "]");
            integer_register = 1;
        }
        for (const ArgumentSlot& slot : slots) {
            if (slot.fp &&
                aggregate_abi(class_of(slot.type)) == AggregateAbi::Floating) {
                const ClassInfo* aggregate = class_of(slot.type);
                emit("    FLOAD.D F" + std::to_string(fp_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                if (aggregate->size > 8) {
                    const bool word =
                        aggregate->size - 8 <= 4;
                    emit(std::string("    FLOAD.") + (word ? "W" : "D") + " F" +
                         std::to_string(fp_register + 1) + ", [R7 + " +
                         std::to_string(slot.offset + 8) + "]");
                    fp_register += 2;
                } else {
                    ++fp_register;
                }
            } else if (slot.fp) {
                emit(std::string("    FLOAD.") +
                     (is_float32(slot.type) ? "W" : "D") + " F" +
                     std::to_string(fp_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                ++fp_register;
            } else if (ClassInfo* register_class = class_of(slot.type);
                       register_class != nullptr &&
                       is_register_class(register_class) &&
                       register_class->size > 4) {
                emit("    LOAD R" + std::to_string(integer_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                emit("    LOAD R" + std::to_string(integer_register + 1) +
                     ", [R7 + " + std::to_string(slot.offset + 4) + "]");
                integer_register += 2;
            } else if (is_long_long(slot.type)) {
                emit("    LOAD R" + std::to_string(integer_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                emit("    LOAD R" + std::to_string(integer_register + 1) +
                     ", [R7 + " + std::to_string(slot.offset + 4) + "]");
                integer_register += 2;
            } else {
                emit("    LOAD R" + std::to_string(integer_register) + ", [R7 + " +
                     std::to_string(slot.offset) + "]");
                ++integer_register;
            }
        }
        const TypePtr call_result = resolve_type(function->result);
        const bool reference_call_result =
            call_result && (call_result->kind == TypeKind::LValueRef ||
                            call_result->kind == TypeKind::RValueRef);
        const ClassInfo* call_result_class =
            reference_call_result ? nullptr : class_of(call_result);
        const bool register_result =
            call_result_class != nullptr &&
            is_register_return_class(call_result_class);
        int class_result_temp = -1;
        if (call_result_class != nullptr && !register_result) {
            class_result_temp = allocate_local(call_result);
            emit("    LEA R4, [R7 + " + std::to_string(class_result_temp) + "]");
        }
        const std::string call_start = new_label("cs_start");
        emit(call_start + ":");
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
        const std::string call_end = new_label("cs_end");
        const std::string call_landing = new_label("cs_landing");
        const std::string call_continue = new_label("cs_continue");
        emit(call_end + ":");
        emit("    JMP " + call_continue);
        emit_unwind_landing(call_landing);
        emit(call_continue + ":");
        record_call_site(call_start, call_end, call_landing);
        if (register_result) {
            const int result_object =
                aggregate_abi(call_result_class) == AggregateAbi::Floating
                    ? allocate_local_bytes(16)
                    : allocate_local(call_result);
            if (aggregate_abi(call_result_class) == AggregateAbi::Floating) {
                emit("    LEA R1, [R7 + " + std::to_string(result_object) + "]");
                emit("    FSTORE.D F0, [R1]");
                if (call_result_class->size > 8) {
                    const bool word = call_result_class->size - 8 <= 4;
                    emit(std::string("    FSTORE.") + (word ? "W" : "D") +
                         " F1, [R1 + 8]");
                }
                emit("    LEA R0, [R7 + " + std::to_string(result_object) + "]");
            } else {
                if (call_result_class->size > 4) {
                    emit("    MOV R2, R1");
                }
                emit("    LEA R1, [R7 + " + std::to_string(result_object) + "]");
                emit("    STORE R0, [R1]");
                if (call_result_class->size > 4) {
                    emit("    STORE R2, [R1 + 4]");
                }
                emit("    LEA R0, [R7 + " + std::to_string(result_object) + "]");
            }
        }
        if (class_result_temp >= 0) {
            emit("    LEA R0, [R7 + " + std::to_string(class_result_temp) + "]");
        }
        return resolve_type(function->result);
    }

    void gen_statement(const Stmt& statement) {
        if (current_function_ != nullptr && statement.location.line > 0) {
            const std::string label = new_label("line");
            emit(label + ":");
            debug_record("L " + label + " 0 " +
                         std::to_string(statement.location.line) + " " +
                         std::to_string(statement.location.column));
        }
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
            if (statement.expression) {
                ClassInfo* list_result =
                    current_function_ != nullptr
                        ? class_of(resolve_type(current_function_->result))
                        : nullptr;
                if (statement.expression->kind == ExprKind::InitializerList &&
                    is_initializer_list_class(list_result)) {
                    MemberInfo* data_member = find_member(list_result, "data_");
                    MemberInfo* size_member = find_member(list_result, "size_");
                    if (data_member == nullptr || size_member == nullptr ||
                        data_member->type == nullptr ||
                        data_member->type->kind != TypeKind::Pointer ||
                        data_member->type->element == nullptr) {
                        error(statement.location,
                              "initializer_list layout is not supported");
                        return;
                    }
                    const TypePtr element = data_member->type->element;
                    const int element_size = type_size(element);
                    const std::size_t count =
                        statement.expression->children.size();
                    const std::string label = new_label("initlist_return");
                    bss_ += ".align 4\n" + label + ":\n    .space " +
                            std::to_string(std::max(1, static_cast<int>(count) *
                                                           element_size)) + "\n";
                    for (std::size_t index = 0; index < count; ++index) {
                        gen_expr(*statement.expression->children[index]);
                        if (is_long_long(element)) {
                            const TypePtr value_type =
                                type_of(*statement.expression->children[index]);
                            if (!is_long_long(value_type)) {
                                emit("    MOV R1, R0");
                                emit(std::string("    ") +
                                     (is_unsigned_integer(value_type) ? "SHR" : "SAR") +
                                     " R1, R1, #31");
                            }
                            emit("    MOV R2, R1");
                        }
                        emit("    LEA R1, [" + label + "]");
                        if (index != 0) {
                            emit("    ADD R1, R1, #" +
                                 std::to_string(index * element_size));
                        }
                        store_value(element);
                    }
                    if (current_sret_slot_ < 0) {
                        error(statement.location,
                              "initializer_list return requires a destination object");
                    } else {
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(current_sret_slot_) + "]");
                        emit("    LEA R1, [" + label + "]");
                        emit("    STORE R1, [R0 + " +
                             std::to_string(data_member->offset) + "]");
                        emit("    MOV R1, #" + std::to_string(count));
                        emit("    STORE R1, [R0 + " +
                             std::to_string(size_member->offset) + "]");
                    }
                    emit_all_local_destructors();
                    emit("    MOV R0, #0");
                    emit("    JMP " + return_label_);
                    return;
                }
                const TypePtr declared_result =
                    current_function_ != nullptr
                        ? resolve_type(current_function_->result)
                        : nullptr;
                if (declared_result &&
                    (declared_result->kind == TypeKind::LValueRef ||
                     declared_result->kind == TypeKind::RValueRef)) {
                    gen_address(*statement.expression);
                    emit_all_local_destructors();
                    emit("    JMP " + return_label_);
                    return;
                }
                const TypePtr result_type = type_of(*statement.expression);
                if (ClassInfo* result_class = class_of(result_type)) {
                    gen_expr(*statement.expression);
                    const int source_temp =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    emit("    STORE R0, [R7 + " + std::to_string(source_temp) + "]");
                    if (aggregate_abi(result_class) == AggregateAbi::Floating) {
                        emit("    LOAD R1, [R7 + " + std::to_string(source_temp) + "]");
                        emit("    FLOAD.D F0, [R1]");
                        if (result_class->size > 8) {
                            const bool word = result_class->size - 8 <= 4;
                            emit(std::string("    FLOAD.") + (word ? "W" : "D") +
                                 " F1, [R1 + 8]");
                        }
                        emit("    JMP " + return_label_);
                        return;
                    }
                    if (is_register_return_class(result_class)) {
                        emit("    LOAD R1, [R7 + " + std::to_string(source_temp) + "]");
                        emit("    LOAD R0, [R1]");
                        const int value_temp =
                            allocate_local(Type::make_builtin(BuiltinKind::Int));
                        emit("    STORE R0, [R7 + " + std::to_string(value_temp) + "]");
                        int high_temp = -1;
                        if (result_class->size > 4) {
                            emit("    LOAD R1, [R1 + 4]");
                            high_temp =
                                allocate_local(Type::make_builtin(BuiltinKind::Int));
                            emit("    STORE R1, [R7 + " +
                                 std::to_string(high_temp) + "]");
                        }
                        emit_all_local_destructors();
                        emit("    LOAD R0, [R7 + " + std::to_string(value_temp) + "]");
                        if (high_temp >= 0) {
                            emit("    LOAD R1, [R7 + " +
                                 std::to_string(high_temp) + "]");
                        }
                        emit("    JMP " + return_label_);
                        return;
                    }
                    if (current_sret_slot_ < 0) {
                        error(statement.location,
                              "class return requires a destination object");
                    } else {
                        emit("    LOAD R0, [R7 + " + std::to_string(current_sret_slot_) +
                             "]");
                        emit("    LOAD R1, [R7 + " + std::to_string(source_temp) + "]");
                        emit("    MOV R2, #" + std::to_string(result_class->size));
                        emit("    CALL memcpy");
                    }
                    emit_all_local_destructors();
                    emit("    MOV R0, #0");
                    emit("    JMP " + return_label_);
                    return;
                }
            }
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
            if (active_catch_count_ > 0) {
                emit("    LOAD R0, [__kr32_exception_object]");
                emit("    CALL __cxa_end_catch");
            }
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
        case StmtKind::Switch: {
            if (!statement.condition) {
                return;
            }
            gen_expr(*statement.condition);
            const int switch_value =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(switch_value) + "]");
            const std::string end = new_label("switchend");
            const std::string dispatch = new_label("switchdispatch");
            emit("    JMP " + dispatch);
            break_labels_.push_back(end);
            push_scope();
            std::vector<std::pair<std::int64_t, std::string>> cases;
            std::string default_label;
            if (statement.body && statement.body->kind == StmtKind::Compound) {
                for (const StmtPtr& child : statement.body->statements) {
                    if (!child) {
                        continue;
                    }
                    if (child->kind == StmtKind::Case) {
                        std::int64_t value = 0;
                        if (!child->expression || !eval_const(*child->expression, value)) {
                            error(child->location, "case label must be a constant integer");
                            continue;
                        }
                        const std::string label = new_label("casebody");
                        cases.push_back({value, label});
                        emit(label + ":");
                    } else if (child->kind == StmtKind::Default) {
                        default_label = new_label("defaultbody");
                        emit(default_label + ":");
                    } else {
                        gen_statement(*child);
                    }
                }
            } else if (statement.body) {
                gen_statement(*statement.body);
            }
            emit("    JMP " + end);
            emit(dispatch + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(switch_value) + "]");
            for (const auto& entry : cases) {
                emit("    MOV R1, #" + std::to_string(entry.first));
                emit("    CMP R0, R1");
                emit("    JZ " + entry.second);
            }
            emit("    JMP " + (default_label.empty() ? end : default_label));
            emit(end + ":");
            pop_scope();
            break_labels_.pop_back();
            return;
        }
        case StmtKind::Case:
        case StmtKind::Default:
            return;
        case StmtKind::RangeFor: {
            if (!statement.expression || !statement.declaration) {
                error(statement.location,
                      "range-for requires a declared loop variable and an array");
                return;
            }
            TypePtr range_type;
            int list_array_offset = -1;
            bool list_range = false;
            MemberInfo* list_data_member = nullptr;
            MemberInfo* list_size_member = nullptr;
            if (statement.expression->kind == ExprKind::InitializerList) {
                if (statement.expression->children.empty()) {
                    error(statement.location,
                          "range-for initializer list cannot be empty");
                    return;
                }
                TypePtr element_type =
                    resolve_type(type_of(*statement.expression->children[0]));
                range_type = Type::make_array(
                    element_type,
                    static_cast<std::uint32_t>(
                        statement.expression->children.size()));
                list_array_offset = allocate_local(range_type);
                zero_object(list_array_offset, type_size(range_type));
                gen_array_initializer(range_type, list_array_offset,
                                      *statement.expression);
            } else {
                range_type = type_of(*statement.expression, false);
                ClassInfo* list_info = class_of(range_type);
                if (is_initializer_list_class(list_info)) {
                    list_data_member = find_member(list_info, "data_");
                    list_size_member = find_member(list_info, "size_");
                    if (list_data_member == nullptr ||
                        list_size_member == nullptr ||
                        list_data_member->type == nullptr ||
                        list_data_member->type->kind != TypeKind::Pointer ||
                        list_data_member->type->element == nullptr) {
                        error(statement.location,
                              "initializer_list layout is not supported");
                        return;
                    }
                    list_range = true;
                    range_type = Type::make_array(
                        resolve_type(list_data_member->type->element), 0);
                }
            }
            if (!range_type || range_type->kind != TypeKind::Array ||
                !range_type->element) {
                error(statement.location,
                      "range-for currently supports arrays only");
                return;
            }
            push_scope();
            const int base_temp =
                allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Int)));
            int count_temp = -1;
            if (list_range) {
                gen_expr(*statement.expression);
                const int object_temp =
                    allocate_local(Type::make_pointer(Type::make_builtin(BuiltinKind::Int)));
                emit("    STORE R0, [R7 + " + std::to_string(object_temp) + "]");
                emit("    LOAD R1, [R7 + " + std::to_string(object_temp) + "]");
                emit("    LOAD R0, [R1 + " +
                     std::to_string(list_data_member->offset) + "]");
                emit("    STORE R0, [R7 + " + std::to_string(base_temp) + "]");
                emit("    LOAD R1, [R7 + " + std::to_string(object_temp) + "]");
                emit("    LOAD R0, [R1 + " +
                     std::to_string(list_size_member->offset) + "]");
                count_temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(count_temp) + "]");
            } else {
                if (list_array_offset >= 0) {
                    emit("    LEA R0, [R7 + " + std::to_string(list_array_offset) + "]");
                } else {
                    gen_expr(*statement.expression);
                }
                emit("    STORE R0, [R7 + " + std::to_string(base_temp) + "]");
            }
            const int index_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    MOV R0, #0");
            emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
            declare_local(*statement.declaration);
            Local* variable = lookup_local(statement.declaration->name);
            if (variable == nullptr) {
                error(statement.location, "range-for loop variable was not declared");
                pop_scope();
                return;
            }
            const int variable_offset = variable->offset;
            const int element_size = type_size(range_type->element);
            const bool fp_element = is_fp(range_type->element);
            const std::string start = new_label("rangefor");
            const std::string increment = new_label("rangeinc");
            const std::string end = new_label("rangeend");
            break_labels_.push_back(end);
            continue_labels_.push_back(increment);
            emit(start + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
            if (count_temp >= 0) {
                emit("    LOAD R1, [R7 + " + std::to_string(count_temp) + "]");
            } else {
                emit("    MOV R1, #" + std::to_string(range_type->array_size));
            }
            emit("    CMP R0, R1");
            emit("    JGE " + end);
            emit("    LOAD R0, [R7 + " + std::to_string(base_temp) + "]");
            emit("    LOAD R1, [R7 + " + std::to_string(index_temp) + "]");
            if (element_size != 1) {
                emit("    MUL R1, R1, #" + std::to_string(element_size));
            }
            emit("    ADD R2, R0, R1");
            emit("    LEA R1, [R7 + " + std::to_string(variable_offset) + "]");
            if (fp_element) {
                emit(std::string("    FLOAD.") +
                     (is_float32(range_type->element) ? "W" : "D") + " F0, [R2]");
                emit(std::string("    FSTORE.") +
                     (is_float32(range_type->element) ? "W" : "D") + " F0, [R1]");
            } else if (is_long_long(range_type->element)) {
                emit("    LOAD R0, [R2]");
                emit("    LOAD R3, [R2 + 4]");
                emit("    STORE R0, [R1]");
                emit("    STORE R3, [R1 + 4]");
            } else {
                emit("    LOAD R0, [R2]");
                emit("    STORE R0, [R1]");
            }
            if (statement.body) {
                gen_statement(*statement.body);
            }
            emit(increment + ":");
            emit("    LOAD R0, [R7 + " + std::to_string(index_temp) + "]");
            emit("    ADD R0, R0, #1");
            emit("    STORE R0, [R7 + " + std::to_string(index_temp) + "]");
            emit("    JMP " + start);
            emit(end + ":");
            continue_labels_.pop_back();
            break_labels_.pop_back();
            pop_scope();
            return;
        }
        case StmtKind::Throw:
            gen_throw(statement);
            return;
        case StmtKind::Try:
            gen_try(statement);
            return;
        case StmtKind::Goto:
            if (current_function_ != nullptr) {
                emit("    JMP .Luser_" + current_function_->symbol + "_" +
                     statement.label);
            } else {
                error(statement.location, "goto outside a function");
            }
            return;
        case StmtKind::Label:
            if (current_function_ != nullptr) {
                emit(".Luser_" + current_function_->symbol + "_" + statement.label + ":");
            } else {
                error(statement.location, "label outside a function");
            }
            return;
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

    Function* find_own_constructor(ClassInfo* info, std::size_t argument_count) {
        if (info == nullptr) {
            return nullptr;
        }
        for (Function* constructor : info->constructors) {
            if (constructor->parameters.size() == argument_count) {
                return constructor;
            }
        }
        return nullptr;
    }

    Function* find_constructor_for_arguments(
        ClassInfo* info, const std::vector<ExprPtr>& arguments) {
        Function* best = nullptr;
        int best_score = -100000;
        for (Function* constructor : info->constructors) {
            if (constructor->parameters.size() != arguments.size()) {
                continue;
            }
            int score = 0;
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                const TypePtr parameter_raw =
                    resolve_type(constructor->parameters[index]);
                const TypePtr argument_raw =
                    arguments[index] ? resolve_type(type_of(*arguments[index]))
                                     : nullptr;
                TypePtr parameter = parameter_raw;
                TypePtr argument = argument_raw;
                if (parameter && (parameter->kind == TypeKind::LValueRef ||
                                  parameter->kind == TypeKind::RValueRef)) {
                    parameter = parameter->element;
                }
                if (argument && (argument->kind == TypeKind::LValueRef ||
                                 argument->kind == TypeKind::RValueRef)) {
                    argument = argument->element;
                }
                ClassInfo* parameter_class = class_of(parameter);
                ClassInfo* argument_class =
                    argument ? class_of(argument) : nullptr;
                if (parameter_class != nullptr || argument_class != nullptr) {
                    score += parameter_class == argument_class ? 10 : -100;
                } else {
                    score += 1;
                }
            }
            if (score > best_score) {
                best_score = score;
                best = constructor;
            }
        }
        return best;
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
        Function* constructor = find_constructor_for_arguments(info, arguments);
        if (constructor != nullptr &&
            !can_access(constructor->owner, constructor->access)) {
            error(*info->decl, "constructor is not accessible");
            return;
        }
        if (!constructor) {
            if (arguments.empty()) {
                zero_object(offset, info->size);
                const int address_temp =
                    allocate_local(Type::make_pointer(
                        Type::make_builtin(BuiltinKind::Void)));
                emit("    LEA R0, [R7 + " + std::to_string(offset) + "]");
                emit("    STORE R0, [R7 + " + std::to_string(address_temp) + "]");
                construct_default_subobjects(*info, address_temp);
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

    bool initializer_list_member_call(const Expr& call,
                                      std::string& method,
                                      ClassInfo*& info,
                                      MemberInfo*& data_member,
                                      MemberInfo*& size_member) {
        if (call.children.empty() || call.children[0]->kind != ExprKind::Member) {
            return false;
        }
        const Expr& member = *call.children[0];
        method = member.name.to_string();
        if (method != "begin" && method != "end") {
            return false;
        }
        info = class_of(type_of(*member.children[0], false));
        if (!is_initializer_list_class(info)) {
            return false;
        }
        data_member = find_member(info, "data_");
        size_member = find_member(info, "size_");
        return data_member != nullptr && size_member != nullptr &&
               data_member->type != nullptr &&
               data_member->type->kind == TypeKind::Pointer &&
               data_member->type->element != nullptr;
    }

    bool is_initializer_list_class(const ClassInfo* info) const {
        return info != nullptr &&
               info->qualified.find("initializer_list__") != std::string::npos;
    }

    enum class AggregateAbi {
        None,
        Integer,
        Floating,
        Memory,
    };

    AggregateAbi aggregate_abi(const ClassInfo* info) const {
        if (info == nullptr) {
            return AggregateAbi::None;
        }
        if (is_initializer_list_class(info) || info->polymorphic ||
            info->abstract || info->base != nullptr ||
            !info->constructors.empty() || info->destructor != nullptr) {
            return AggregateAbi::Memory;
        }
        if (info->size == 0) {
            return AggregateAbi::Memory;
        }
        bool has_fp = false;
        bool has_integer = false;
        for (const MemberInfo& member : info->members) {
            if (member.type == nullptr || member.type->kind == TypeKind::Array ||
                member.type->kind == TypeKind::Function) {
                return AggregateAbi::Memory;
            }
            if (is_fp(member.type)) {
                has_fp = true;
            } else {
                has_integer = true;
            }
        }
        if (has_fp) {
            if (has_integer || info->size > 16) {
                return AggregateAbi::Memory;
            }
            return AggregateAbi::Floating;
        }
        return info->size <= 8 ? AggregateAbi::Integer : AggregateAbi::Memory;
    }

    bool is_register_class(const ClassInfo* info) const {
        return aggregate_abi(info) == AggregateAbi::Integer;
    }

    bool is_register_return_class(const ClassInfo* info) const {
        const AggregateAbi abi = aggregate_abi(info);
        return abi == AggregateAbi::Integer || abi == AggregateAbi::Floating;
    }

    int prepare_initializer_list_argument(ClassInfo* info, const TypePtr& type,
                                          const Expr& initializer) {
        MemberInfo* data_member = find_member(info, "data_");
        MemberInfo* size_member = find_member(info, "size_");
        if (data_member == nullptr || size_member == nullptr ||
            data_member->type == nullptr ||
            data_member->type->kind != TypeKind::Pointer ||
            data_member->type->element == nullptr) {
            error(initializer.location,
                  "initializer_list layout is not supported");
            return -1;
        }
        const TypePtr element_type = data_member->type->element;
        const std::uint32_t count =
            static_cast<std::uint32_t>(initializer.children.size());
        const TypePtr array_type = Type::make_array(element_type, count);
        const int array_offset = allocate_local(array_type);
        zero_object(array_offset, type_size(array_type));
        gen_array_initializer(array_type, array_offset, initializer);
        const int object_offset = allocate_local(type);
        emit("    LEA R0, [R7 + " + std::to_string(array_offset) + "]");
        emit("    STORE R0, [R7 + " +
             std::to_string(object_offset + data_member->offset) + "]");
        emit("    MOV R0, #" + std::to_string(count));
        emit("    STORE R0, [R7 + " +
             std::to_string(object_offset + size_member->offset) + "]");
        return object_offset;
    }

    void gen_array_initializer(const TypePtr& type, int offset, const Expr& initializer) {
        zero_object(offset, std::max(1, type_size(type)));
        if (initializer.kind != ExprKind::InitializerList) {
            return;
        }
        const int element_size = type_size(type->element);
        std::size_t index = 0;
        for (const ExprPtr& element : initializer.children) {
            if (!element) {
                continue;
            }
            if (element->has_index_designator) {
                if (element->index_designator < 0) {
                    error(element->location, "array designator is out of range");
                    continue;
                }
                index = static_cast<std::size_t>(element->index_designator);
            }
            if (index >= type->array_size) {
                error(element->location, "array designator is out of range");
                continue;
            }
            const int element_offset = offset + static_cast<int>(index) * element_size;
            if (type->element->kind == TypeKind::Array) {
                gen_array_initializer(type->element, element_offset, *element);
            } else if (is_fp(type->element)) {
                gen_as_fp(*element, type->element);
                emit(std::string("    FSTORE.") +
                     (is_float32(type->element) ? "W" : "D") + " F0, [R7 + " +
                     std::to_string(element_offset) + "]");
            } else {
                gen_expr(*element);
                if (is_long_long(type->element)) {
                    const TypePtr value_type = type_of(*element);
                    if (!is_long_long(value_type)) {
                        emit("    MOV R1, R0");
                        emit(std::string("    ") +
                             (is_unsigned_integer(value_type) ? "SHR" : "SAR") +
                             " R1, R1, #31");
                    }
                    emit("    MOV R2, R1");
                }
                emit("    LEA R1, [R7 + " + std::to_string(element_offset) + "]");
                store_value(type->element);
            }
            ++index;
        }
    }

    void gen_class_aggregate_initializer(ClassInfo* info, int offset,
                                         const Expr& initializer) {
        zero_object(offset, std::max(1, info->size));
        if (initializer.kind != ExprKind::InitializerList) {
            return;
        }
        std::size_t index = 0;
        for (const ExprPtr& value : initializer.children) {
            if (!value) {
                continue;
            }
            if (value->has_field_designator) {
                index = info->members.size();
                for (std::size_t candidate = 0; candidate < info->members.size();
                     ++candidate) {
                    if (info->members[candidate].name == value->field_designator) {
                        index = candidate;
                        break;
                    }
                }
                if (index >= info->members.size()) {
                    error(value->location,
                          "unknown field designator: " + value->field_designator);
                    continue;
                }
            }
            if (index >= info->members.size()) {
                error(value->location, "too many aggregate initializer elements");
                continue;
            }
            const MemberInfo& member = info->members[index];
            const int member_offset = offset + member.offset;
            if (member.type && member.type->kind == TypeKind::Array) {
                gen_array_initializer(member.type, member_offset, *value);
            } else if (ClassInfo* member_class = class_of(member.type)) {
                gen_class_aggregate_initializer(member_class, member_offset, *value);
            } else if (is_fp(member.type)) {
                gen_as_fp(*value, member.type);
                emit(std::string("    FSTORE.") +
                     (is_float32(member.type) ? "W" : "D") + " F0, [R7 + " +
                     std::to_string(member_offset) + "]");
            } else {
                gen_expr(*value);
                emit("    LEA R1, [R7 + " + std::to_string(member_offset) + "]");
                store_value(member.type);
            }
            ++index;
        }
    }

    void declare_local(const Decl& declaration) {
        if (local_scopes_.empty()) {
            push_scope();
        }
        TypePtr type = resolve_type(declaration.type);
        if (declaration.type && declaration.type->kind == TypeKind::Builtin &&
            declaration.type->builtin == BuiltinKind::Auto &&
            declaration.initializer &&
            declaration.initializer->kind == ExprKind::InitializerList &&
            !declaration.initializer->children.empty()) {
            const TypePtr element_type =
                resolve_type(type_of(*declaration.initializer->children[0]));
            Name list_name;
            list_name.parts.push_back("std");
            list_name.parts.push_back("initializer_list");
            TypePtr list_type = Type::make_named(list_name);
            list_type->template_arguments.push_back(element_type);
            type = resolve_type(list_type);
        } else if (declaration.type &&
                   declaration.type->kind == TypeKind::Builtin &&
                   declaration.type->builtin == BuiltinKind::Auto &&
                   declaration.initializer &&
                   declaration.initializer->kind == ExprKind::Call) {
            const TypePtr inferred =
                resolve_type(type_of(*declaration.initializer));
            if (inferred != nullptr &&
                !(inferred->kind == TypeKind::Builtin &&
                  inferred->builtin == BuiltinKind::Void)) {
                type = inferred;
            }
        }
        Function* lambda_target = nullptr;
        if (declaration.initializer &&
            declaration.initializer->kind == ExprKind::Lambda) {
            lambda_target = materialize_lambda(*declaration.initializer);
            type = lambda_function_type(lambda_target);
        }
        if (declaration.is_static) {
            auto global = std::make_unique<Global>();
            global->decl = &declaration;
            const std::string owner_symbol =
                current_function_ != nullptr ? current_function_->symbol : "global";
            global->qualified = "__cxx_local_static_" + owner_symbol + "_" +
                                std::to_string(static_counter_);
            global->symbol = global->qualified;
            global->type = type;
            global->defined = true;
            global->is_static = true;
            global->initializer = declaration.initializer.get();
            const bool template_owner =
                current_function_ != nullptr && current_function_->from_template;
            const std::string linkage = template_owner ? ".weak " : ".global ";
            Global* raw = global.get();
            global_storage_.push_back(std::move(global));
            if (template_owner) {
                bss_ += ".section .bss." + raw->symbol + "\n";
            }
            bss_ += linkage + raw->symbol + "\n" + raw->symbol +
                    ":\n    .space " + std::to_string(std::max(1, type_size(type))) +
                    "\n";
            Local local;
            local.type = type;
            local.static_global = raw;
            if (!declaration.name.parts.empty()) {
                local_scopes_.back()[declaration.name.parts.back()] = local;
            }
            ClassInfo* info = class_of(type);
            const bool guarded = declaration.initializer != nullptr || info != nullptr;
            if (guarded) {
                const std::string guard = raw->symbol + "_guard";
                bss_ += linkage + guard + "\n" + guard +
                        ":\n    .space 4\n";
                emit("    LEA R0, [" + guard + "]");
                emit("    CALL __cxa_guard_acquire");
                const std::string done = new_label("guard_done");
                emit("    CMP R0, #0");
                emit("    JZ " + done);
                if (info != nullptr) {
                    const int address =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    emit("    LEA R0, [" + raw->symbol + "]");
                    emit("    STORE R0, [R7 + " + std::to_string(address) + "]");
                    std::vector<ExprPtr> empty;
                    const std::vector<ExprPtr>* arguments = &empty;
                    if (declaration.initializer &&
                        declaration.initializer->kind == ExprKind::Call) {
                        arguments = &declaration.initializer->arguments;
                    } else if (declaration.initializer &&
                               declaration.initializer->kind ==
                                   ExprKind::InitializerList) {
                        arguments = &declaration.initializer->children;
                    }
                    construct_object_at(info, address, *arguments);
                    if (find_destructor(info) != nullptr) {
                        static_destructors_.push_back(raw);
                    }
                } else if (declaration.initializer) {
                    gen_expr(*declaration.initializer);
                    emit("    LEA R1, [" + raw->symbol + "]");
                    store_value(type);
                }
                emit("    LEA R0, [" + guard + "]");
                emit("    CALL __cxa_guard_release");
                emit(done + ":");
            }
            if (template_owner) {
                bss_ += ".bss\n";
            }
            return;
        }
        Local local;
        local.type = type;
        local.lambda_target = lambda_target;
        local.is_reference = type && (type->kind == TypeKind::LValueRef ||
                                      type->kind == TypeKind::RValueRef);
        local.offset = allocate_local(type);
        if (lambda_target != nullptr && lambda_target->lambda_closure) {
            local.offset = allocate_local_bytes(lambda_target->lambda_closure_size);
            local.lambda_closure_offset = local.offset;
            for (const auto& capture : lambda_target->lambda_capture_offsets) {
                Name capture_name;
                capture_name.parts.push_back(capture.first);
                if (Local* source = lookup_local(capture_name)) {
                    if (lambda_target->lambda_capture_references.find(capture.first) !=
                        lambda_target->lambda_capture_references.end()) {
                        if (source->is_reference) {
                            emit("    LOAD R0, [R7 + " +
                                 std::to_string(source->offset) + "]");
                        } else {
                            emit("    LEA R0, [R7 + " +
                                 std::to_string(source->offset) + "]");
                        }
                    } else {
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(source->offset) + "]");
                    }
                    emit("    STORE R0, [R7 + " +
                         std::to_string(local.offset + capture.second) + "]");
                }
            }
        }
        if (current_function_ != nullptr && !declaration.name.parts.empty()) {
            debug_record("V " + current_function_->qualified + " " +
                         declaration.name.parts.back() + " " +
                         std::to_string(local.offset) + " " + debug_type(type) + " " +
                         std::to_string(std::max(1, type_size(type))));
        }
        if (!declaration.name.parts.empty()) {
            local_scopes_.back()[declaration.name.parts.back()] = local;
        }
        if (local.lambda_closure_offset >= 0) {
            return;
        }
        if (ClassInfo* info = class_of(type)) {
            if (declaration.initializer &&
                declaration.initializer->kind == ExprKind::InitializerList &&
                is_initializer_list_class(info)) {
                const int object = prepare_initializer_list_argument(
                    info, type, *declaration.initializer);
                if (object >= 0) {
                    emit("    LEA R1, [R7 + " + std::to_string(object) + "]");
                    emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    MOV R2, #" + std::to_string(info->size));
                    emit("    CALL memcpy");
                }
                return;
            }
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
            if (declaration.initializer &&
                declaration.initializer->kind == ExprKind::Call) {
                if (info->qualified == "std::thread" &&
                    !declaration.initializer->arguments.empty() &&
                    declaration.initializer->arguments[0] &&
                    declaration.initializer->arguments[0]->kind ==
                        ExprKind::Lambda &&
                    !declaration.initializer->arguments[0]->text.empty()) {
                    construct_object_at(info, local.offset,
                                        declaration.initializer->arguments);
                    if (Function* destructor = find_destructor(info)) {
                        local_destructors_.back().push_back({local.offset, destructor});
                    }
                    return;
                }
                bool callee_is_class = false;
                if (!declaration.initializer->children.empty() &&
                    declaration.initializer->children[0]->kind ==
                        ExprKind::Name) {
                    const std::string callee_name =
                        declaration.initializer->children[0]->name.to_string();
                    callee_is_class =
                        find_class(declaration.initializer->children[0]->name) !=
                            nullptr ||
                        !declaration.initializer->children[0]
                             ->template_type_arguments.empty() ||
                        info->qualified.rfind(callee_name, 0) == 0;
                }
                Function* called =
                    callee_is_class ? nullptr : resolve_call(*declaration.initializer);
                if (called != nullptr &&
                    class_of(resolve_type(called->result)) == info) {
                    gen_expr(*declaration.initializer);
                    const int result_address =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    emit("    STORE R0, [R7 + " + std::to_string(result_address) + "]");
                    emit("    LOAD R1, [R7 + " + std::to_string(result_address) + "]");
                    emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    MOV R2, #" + std::to_string(info->size));
                    emit("    CALL memcpy");
                    if (Function* destructor = find_destructor(info)) {
                        local_destructors_.back().push_back({local.offset, destructor});
                    }
                    return;
                }
            }
            if (declaration.initializer &&
                declaration.initializer->kind == ExprKind::InitializerList &&
                find_constructor(info, declaration.initializer->children.size()) ==
                    nullptr) {
                Function* list_constructor = find_constructor(info, 1);
                if (list_constructor != nullptr &&
                    !list_constructor->parameters.empty()) {
                    const TypePtr parameter =
                        resolve_type(list_constructor->parameters[0]);
                    ClassInfo* list_class = class_of(parameter);
                    if (is_initializer_list_class(list_class)) {
                        const int list_object = prepare_initializer_list_argument(
                            list_class, parameter, *declaration.initializer);
                        emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                        emit("    LEA R1, [R7 + " + std::to_string(list_object) + "]");
                        emit("    CALL " + list_constructor->symbol);
                        if (Function* destructor = find_destructor(info)) {
                            local_destructors_.back().push_back(
                                {local.offset, destructor});
                        }
                        return;
                    }
                }
                gen_class_aggregate_initializer(info, local.offset,
                                                 *declaration.initializer);
                if (Function* destructor = find_destructor(info)) {
                    local_destructors_.back().push_back({local.offset, destructor});
                }
                return;
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
                gen_array_initializer(type, local.offset, *declaration.initializer);
            }
            return;
        }
        if (declaration.initializer) {
            const Expr* initializer = declaration.initializer.get();
            if (initializer->kind == ExprKind::Call) {
                bool function_call = false;
                if (!initializer->children.empty() &&
                    initializer->children[0]->kind == ExprKind::Lambda) {
                    function_call = true;
                }
                if (!initializer->children.empty() &&
                    initializer->children[0]->kind == ExprKind::Member) {
                    function_call = true;
                }
                if (!initializer->children.empty() &&
                    initializer->children[0]->kind == ExprKind::Name) {
                    function_call =
                        !lookup_functions(initializer->children[0]->name,
                                          current_prefix_).empty();
                    if (Local* lambda_local =
                            lookup_local(initializer->children[0]->name)) {
                        function_call = function_call ||
                                        lambda_local->lambda_target != nullptr;
                    }
                    if (!function_call) {
                        for (const std::string& key : candidate_keys(
                                 initializer->children[0]->name,
                                 current_prefix_)) {
                            if (template_functions_.find(key) !=
                                    template_functions_.end() ||
                                function_specializations_.find(key) !=
                                    function_specializations_.end()) {
                                function_call = true;
                                break;
                            }
                        }
                    }
                }
                if (!function_call) {
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
            }
            if (is_fp(type)) {
                gen_as_fp(*initializer, type);
                emit(std::string("    FSTORE.") + (is_float32(type) ? "W" : "D") +
                     " F0, [R7 + " + std::to_string(local.offset) + "]");
                return;
            }
            gen_expr(*initializer);
            if (is_long_long(type)) {
                const TypePtr initializer_type = type_of(*initializer);
                if (!is_long_long(initializer_type)) {
                    emit("    MOV R1, R0");
                    emit(std::string("    ") +
                         (is_unsigned_integer(initializer_type) ? "SHR" : "SAR") +
                         " R1, R1, #31");
                }
                emit("    MOV R2, R1");
            }
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
        current_call_sites_.clear();
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
        current_lambda_closure_offset_ = -1;
        current_lambda_function_ = nullptr;
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
        if (function.lambda_closure) {
            Local closure;
            closure.type = Type::make_pointer(Type::make_builtin(BuiltinKind::Void));
            closure.offset = allocate_local(closure.type);
            emit("    STORE R0, [R7 + " + std::to_string(closure.offset) + "]");
            current_lambda_closure_offset_ = closure.offset;
            current_lambda_function_ = &function;
            integer_argument = 1;
        }
        current_sret_slot_ = -1;
        const TypePtr resolved_result = resolve_type(function.result);
        const bool reference_result =
            resolved_result &&
            (resolved_result->kind == TypeKind::LValueRef ||
             resolved_result->kind == TypeKind::RValueRef);
        if (!reference_result) {
            if (ClassInfo* result_class = class_of(resolved_result);
                result_class != nullptr && !is_register_return_class(result_class)) {
                current_sret_slot_ =
                    allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R4, [R7 + " +
                     std::to_string(current_sret_slot_) + "]");
            }
        }
        bool has_class_parameter = false;
        for (const Param& parameter : function.decl->parameters) {
            const TypePtr parameter_type = resolve_type(parameter.type);
            if (parameter_type &&
                parameter_type->kind != TypeKind::LValueRef &&
                parameter_type->kind != TypeKind::RValueRef &&
                class_of(parameter_type) != nullptr) {
                has_class_parameter = true;
                break;
            }
        }
        std::vector<int> saved_integer_arguments;
        if (has_class_parameter) {
            for (int index = 0; index < 4; ++index) {
                const int temp = allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R" + std::to_string(index) + ", [R7 + " +
                     std::to_string(temp) + "]");
                saved_integer_arguments.push_back(temp);
            }
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
            if (!local.is_reference &&
                aggregate_abi(class_of(local.type)) == AggregateAbi::Floating) {
                local.offset = allocate_local_bytes(16);
            } else {
                local.offset = allocate_local(local.type);
            }
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
            } else if (!local.is_reference &&
                       aggregate_abi(class_of(local.type)) ==
                           AggregateAbi::Floating) {
                const ClassInfo* aggregate = class_of(local.type);
                const int registers = aggregate->size > 8 ? 2 : 1;
                if (fp_argument + registers <= 8) {
                    emit("    FSTORE.D F" + std::to_string(fp_argument) +
                         ", [R7 + " + std::to_string(local.offset) + "]");
                    if (registers == 2) {
                        const bool word = aggregate->size - 8 <= 4;
                        emit(std::string("    FSTORE.") + (word ? "W" : "D") +
                             " F" + std::to_string(fp_argument + 1) + ", [R7 + " +
                             std::to_string(local.offset + 8) + "]");
                    }
                    fp_argument += registers;
                } else {
                    error(parameter.location,
                          "more than eight FP parameters are not supported yet");
                }
            } else if (is_long_long(local.type)) {
                if (integer_argument + 1 < 4) {
                    if (!saved_integer_arguments.empty()) {
                        emit("    LOAD R0, [R7 + " +
                             std::to_string(saved_integer_arguments[
                                 static_cast<std::size_t>(integer_argument)]) +
                             "]");
                        emit("    LOAD R1, [R7 + " +
                             std::to_string(saved_integer_arguments[
                                 static_cast<std::size_t>(integer_argument + 1)]) +
                             "]");
                    } else {
                        emit("    MOV R0, R" + std::to_string(integer_argument));
                        emit("    MOV R1, R" +
                             std::to_string(integer_argument + 1));
                    }
                    emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    STORE R1, [R7 + " +
                         std::to_string(local.offset + 4) + "]");
                    integer_argument += 2;
                } else {
                    error(parameter.location,
                          "long long parameter needs two integer registers");
                }
            } else if (ClassInfo* register_class = class_of(local.type);
                       register_class != nullptr &&
                       is_register_class(register_class)) {
                const int registers = register_class->size <= 4 ? 1 : 2;
                if (integer_argument + registers <= 4) {
                    for (int register_index = 0; register_index < registers;
                         ++register_index) {
                        if (!saved_integer_arguments.empty()) {
                            emit("    LOAD R0, [R7 + " +
                                 std::to_string(saved_integer_arguments[
                                     static_cast<std::size_t>(integer_argument +
                                                              register_index)]) +
                                 "]");
                        } else {
                            emit("    MOV R0, R" +
                                 std::to_string(integer_argument + register_index));
                        }
                        emit("    STORE R0, [R7 + " +
                             std::to_string(local.offset + register_index * 4) + "]");
                    }
                    integer_argument += registers;
                } else {
                    error(parameter.location,
                          "more than four integer parameters are not supported yet");
                }
            } else if (!local.is_reference && class_of(local.type) != nullptr) {
                if (integer_argument < 4) {
                    const ClassInfo* parameter_class = class_of(local.type);
                    emit("    MOV R2, #" + std::to_string(parameter_class->size));
                    if (!saved_integer_arguments.empty()) {
                        emit("    LOAD R1, [R7 + " +
                             std::to_string(saved_integer_arguments[
                                 static_cast<std::size_t>(integer_argument)]) +
                             "]");
                    } else {
                        emit("    MOV R1, R" + std::to_string(integer_argument));
                    }
                    emit("    LEA R0, [R7 + " + std::to_string(local.offset) + "]");
                    emit("    CALL memcpy");
                    ++integer_argument;
                } else {
                    error(parameter.location,
                          "more than four integer parameters are not supported yet");
                }
            } else if (integer_argument < 4) {
                if (!saved_integer_arguments.empty()) {
                    emit("    LOAD R0, [R7 + " +
                         std::to_string(saved_integer_arguments[
                             static_cast<std::size_t>(integer_argument)]) +
                         "]");
                    emit("    STORE R0, [R7 + " + std::to_string(local.offset) + "]");
                } else {
                    emit("    STORE R" + std::to_string(integer_argument) + ", [R7 + " +
                         std::to_string(local.offset) + "]");
                }
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
                emit_init_array_loop();
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
        if (function.qualified == "main") {
            const int result_temp =
                allocate_local(Type::make_builtin(BuiltinKind::Int));
            emit("    STORE R0, [R7 + " + std::to_string(result_temp) + "]");
            emit("    CALL __kr32_cxx_fini");
            emit("    LOAD R0, [R7 + " + std::to_string(result_temp) + "]");
        }
        frame_size_ = (frame_size_ + 15) / 16 * 16 + 8;
        emit("    ADJSP #" + std::to_string(frame_size_));
        emit("    POP R7");
        emit("    RET");

        const std::string function_end = new_label("func_end");
        emit(function_end + ":");
        debug_record("F " + function.qualified + " " + function.symbol + " " +
                     function_end + " " + std::to_string(frame_size_) + " " +
                     debug_type(function.result));
        if (function.is_member && function.owner != nullptr && this_offset_ >= 0) {
            debug_record("V " + function.qualified + " this " +
                         std::to_string(this_offset_) + " " +
                         debug_type(Type::make_pointer(Type::make_named(
                             [&]() {
                                 Name name;
                                 name.parts = split_qualified(function.owner->qualified);
                                 return name;
                             }()))) +
                         " 4");
        }
        const std::string sites_label = new_label("func_sites");
        const std::string descriptor_label = new_label("func_eh");
        std::string descriptor = descriptor_label + ":\n";
        descriptor += "    .word " + function.symbol + "\n";
        descriptor += "    .word " + function_end + "\n";
        descriptor += "    .word " + std::to_string(frame_size_) + "\n";
        descriptor += "    .word " + sites_label + "\n";
        descriptor += sites_label + ":\n";
        descriptor += "    .word " + std::to_string(current_call_sites_.size()) + "\n";
        for (const CallSiteRecord& site : current_call_sites_) {
            descriptor += "    .word " + site.start + "\n";
            descriptor += "    .word " + site.end + "\n";
            descriptor += "    .word " + site.landing + "\n";
        }
        function_eh_records_.push_back({descriptor_label, std::move(descriptor)});

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
            debug_record("C " + info.qualified + " " +
                         std::to_string(std::max(1, info.size)) + " " +
                         (info.base != nullptr ? info.base->qualified : "-"));
            for (const MemberInfo& member : info.members) {
                debug_record("M " + info.qualified + " " + member.name + " " +
                             std::to_string(member.offset) + " " +
                             debug_type(member.type) + " " +
                             std::to_string(std::max(1, type_size(member.type))));
            }
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

    void emit_init_array_loop() {
        const std::string loop = new_label("init_array");
        const std::string done = new_label("init_array_done");
        emit("    LEA R4, [__init_array_start]");
        emit("    LEA R5, [__init_array_end]");
        emit(loop + ":");
        emit("    CMP R4, R5");
        emit("    JGE " + done);
        emit("    LOAD R6, [R4]");
        emit("    CALL R6");
        emit("    ADD R4, R4, #4");
        emit("    JMP " + loop);
        emit(done + ":");
    }

    void emit_fini_array_loop() {
        const std::string loop = new_label("fini_array");
        const std::string done = new_label("fini_array_done");
        emit("    LEA R4, [__fini_array_end]");
        emit("    LEA R5, [__fini_array_start]");
        emit(loop + ":");
        emit("    CMP R4, R5");
        emit("    JLE " + done);
        emit("    SUB R4, R4, #4");
        emit("    LOAD R6, [R4]");
        emit("    CALL R6");
        emit("    JMP " + loop);
        emit(done + ":");
    }

    void emit_static_initializers(bool tls_only = false) {
        for (const auto& global : global_storage_) {
            if (!global->needs_init ||
                global->is_thread_local != tls_only) {
                continue;
            }
            if (ClassInfo* info = class_of(global->type)) {
                const int address = allocate_local(Type::make_builtin(BuiltinKind::Int));
                if (global->is_thread_local) {
                    emit_tls_address(global.get());
                } else {
                    emit("    LEA R0, [" + global->symbol + "]");
                }
                emit("    STORE R0, [R7 + " + std::to_string(address) + "]");
                std::vector<ExprPtr> empty;
                const std::vector<ExprPtr>* arguments = &empty;
                if (global->initializer &&
                    global->initializer->kind == ExprKind::Call) {
                    arguments = &global->initializer->arguments;
                }
                construct_object_at(info, address, *arguments);
                continue;
            }
            if (!global->initializer) {
                continue;
            }
            if (is_fp(global->type)) {
                gen_as_fp(*global->initializer, global->type);
                if (global->is_thread_local) {
                    emit_tls_address(global.get());
                    emit("    MOV R1, R0");
                } else {
                    emit("    LEA R1, [" + global->symbol + "]");
                }
                emit(std::string("    FSTORE.") +
                     (is_float32(global->type) ? "W" : "D") + " F0, [R1]");
            } else {
                gen_expr(*global->initializer);
                if (is_long_long(global->type)) {
                    const TypePtr value_type =
                        type_of(*global->initializer);
                    if (!is_long_long(value_type)) {
                        emit("    MOV R1, R0");
                        emit(std::string("    ") +
                             (is_unsigned_integer(value_type) ? "SHR" : "SAR") +
                             " R1, R1, #31");
                    }
                }
                const int low_temp =
                    allocate_local(Type::make_builtin(BuiltinKind::Int));
                emit("    STORE R0, [R7 + " + std::to_string(low_temp) + "]");
                int high_temp = -1;
                if (is_long_long(global->type)) {
                    high_temp =
                        allocate_local(Type::make_builtin(BuiltinKind::Int));
                    emit("    STORE R1, [R7 + " +
                         std::to_string(high_temp) + "]");
                }
                if (global->is_thread_local) {
                    emit_tls_address(global.get());
                    emit("    MOV R1, R0");
                } else {
                    emit("    LEA R1, [" + global->symbol + "]");
                }
                emit("    LOAD R0, [R7 + " + std::to_string(low_temp) + "]");
                if (high_temp >= 0) {
                    emit("    LOAD R2, [R7 + " +
                         std::to_string(high_temp) + "]");
                }
                store_value(global->type);
            }
        }
    }

    void emit_cxx_init() {
        bool needed = false;
        for (const auto& global : global_storage_) {
            if (global->needs_init) {
                needed = true;
                break;
            }
        }
        if (!needed) {
            return;
        }
        const int saved_frame = frame_size_;
        Function* saved_function = current_function_;
        current_function_ = nullptr;
        frame_size_ = 0;
        push_scope();
        local_destructors_.push_back({});
        text_ += ".text\n.Lcxx_init:\n";
        text_ += "    PUSH R7\n    ADJSP #-1024\n    LEA R7, [SP]\n";
        emit_static_initializers();
        if (tls_cursor_ > 0) {
            emit("    CALL __kr32_tls_init");
        }
        text_ += "    ADJSP #1024\n    POP R7\n    RET\n";
        local_destructors_.pop_back();
        pop_scope();
        frame_size_ = saved_frame;
        current_function_ = saved_function;
        startup_ += ".init_array\n    .word .Lcxx_init\n";
    }

    void emit_tls_runtime() {
        bool needs_tls_init = false;
        for (const auto& global : global_storage_) {
            if (global->is_thread_local && global->needs_init) {
                needs_tls_init = true;
                break;
            }
        }
        if (!needs_tls_init && tls_destructors_.empty()) {
            return;
        }
        const int saved_frame = frame_size_;
        Function* saved_function = current_function_;
        current_function_ = nullptr;
        frame_size_ = 0;
        push_scope();
        local_destructors_.push_back({});
        text_ += ".text\n.weak __kr32_tls_init\n__kr32_tls_init:\n";
        text_ += "    PUSH R7\n    ADJSP #-1024\n    LEA R7, [SP]\n";
        if (needs_tls_init) {
            emit_static_initializers(true);
        }
        text_ += "    ADJSP #1024\n    POP R7\n    RET\n";
        text_ += ".weak __kr32_tls_fini\n__kr32_tls_fini:\n";
        text_ += "    PUSH R7\n    ADJSP #-1024\n    LEA R7, [SP]\n";
        for (auto iterator = tls_destructors_.rbegin();
             iterator != tls_destructors_.rend(); ++iterator) {
            Global* global = *iterator;
            ClassInfo* info = class_of(global->type);
            if (info == nullptr) {
                continue;
            }
            if (Function* destructor = find_destructor(info)) {
                emit_tls_address(global);
                text_ += "    CALL " + destructor->symbol + "\n";
            }
        }
        text_ += "    ADJSP #1024\n    POP R7\n    RET\n";
        text_ += ".weak __kr32_thread_entry\n__kr32_thread_entry:\n";
        text_ += "    PUSH R4\n    PUSH R5\n";
        text_ += "    MOV R4, R0\n    MOV R5, R1\n";
        text_ += "    CALL __kr32_tls_init\n";
        text_ += "    MOV R0, R5\n";
        text_ += "    CALL R4\n";
        text_ += "    CALL __kr32_tls_fini\n";
        text_ += "    MOV R0, #0\n";
        text_ += "    SYSCALL #47\n";
        text_ += "    HALT\n";
        local_destructors_.pop_back();
        pop_scope();
        frame_size_ = saved_frame;
        current_function_ = saved_function;
    }

    void emit_lsdas() {
        if (exception_lsdas_.empty()) {
            return;
        }
        text_ += ".text\n.weak __kr32_personality_v0\n__kr32_personality_v0:\n";
        text_ += "    LOAD R2, [R0]\n";
        text_ += "    ADD R0, R0, #4\n";
        text_ += ".Lpersonality_loop:\n";
        text_ += "    CMP R2, #0\n";
        text_ += "    JLE .Lpersonality_fail\n";
        text_ += "    LOAD R3, [R0]\n";
        text_ += "    CMP R3, #0\n";
        text_ += "    JZ .Lpersonality_match\n";
        text_ += "    CMP R3, R1\n";
        text_ += "    JZ .Lpersonality_match\n";
        text_ += "    ADD R0, R0, #8\n";
        text_ += "    SUB R2, R2, #1\n";
        text_ += "    JMP .Lpersonality_loop\n";
        text_ += ".Lpersonality_match:\n";
        text_ += "    LOAD R0, [R0 + 4]\n";
        text_ += "    RET\n";
        text_ += ".Lpersonality_fail:\n";
        text_ += "    MOV R0, #0\n";
        text_ += "    RET\n";
        lsda_ += ".cpp_except\n";
        for (const LsdaRecord& record : exception_lsdas_) {
            lsda_ += record.label + ":\n";
            lsda_ += "    .word " + std::to_string(record.entries.size()) + "\n";
            for (const auto& entry : record.entries) {
                lsda_ += "    .word " + std::to_string(entry.first) + "\n";
                lsda_ += "    .word " + entry.second + "\n";
            }
        }
    }

    void emit_eh_frame() {
        if (function_eh_records_.empty()) {
            return;
        }
        lsda_ += ".data\n.Lcxx_eh_frame:\n";
        lsda_ += "    .word " + std::to_string(function_eh_records_.size()) + "\n";
        for (const FunctionEhRecord& record : function_eh_records_) {
            lsda_ += "    .word " + record.label + "\n";
        }
        for (const FunctionEhRecord& record : function_eh_records_) {
            lsda_ += record.text;
        }
        startup_ += ".eh_frame_array\n    .word .Lcxx_eh_frame\n";
        text_ += ".text\n.weak __kr32_throw\n__kr32_throw:\n";
        text_ += "    PUSH R7\n    ADJSP #-24\n    LEA R7, [SP]\n";
        text_ += "    LOAD R2, [R7 + 24]\n";
        text_ += "    LOAD R3, [R7 + 28]\n";
        text_ += "    JMP __kr32_unwind_continue\n";
        text_ += "__kr32_unwind_continue:\n";
        text_ += "    LEA R4, [__eh_frame_array_start]\n";
        text_ += "    LEA R5, [__eh_frame_array_end]\n";
        text_ += ".Lunwind_table_loop:\n";
        text_ += "    CMP R4, R5\n";
        text_ += "    JGE .Lunwind_fail\n";
        text_ += "    LOAD R6, [R4]\n";
        text_ += "    ADD R4, R4, #4\n";
        text_ += "    LOAD R7, [R6]\n";
        text_ += "    ADD R6, R6, #4\n";
        text_ += ".Lunwind_desc_loop:\n";
        text_ += "    CMP R7, #0\n";
        text_ += "    JLE .Lunwind_table_loop\n";
        text_ += "    LOAD R10, [R6]\n";
        text_ += "    LOAD R9, [R10]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JLT .Lunwind_next_desc\n";
        text_ += "    LOAD R9, [R10 + 4]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JGT .Lunwind_next_desc\n";
        text_ += "    LOAD R1, [R10 + 8]\n";
        text_ += "    LOAD R10, [R10 + 12]\n";
        text_ += "    LOAD R8, [R10]\n";
        text_ += "    ADD R10, R10, #4\n";
        text_ += ".Lunwind_site_loop:\n";
        text_ += "    CMP R8, #0\n";
        text_ += "    JLE .Lunwind_pop\n";
        text_ += "    LOAD R9, [R10]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JLT .Lunwind_next_site\n";
        text_ += "    LOAD R9, [R10 + 4]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JGT .Lunwind_next_site\n";
        text_ += "    LOAD R9, [R10 + 8]\n";
        text_ += "    STORE R2, [__kr32_unwind_frame]\n";
        text_ += "    STORE R3, [__kr32_unwind_pc]\n";
        text_ += "    MOV R7, R2\n";
        text_ += "    SETSP R7\n";
        text_ += "    JMP R9\n";
        text_ += ".Lunwind_next_site:\n";
        text_ += "    ADD R10, R10, #12\n";
        text_ += "    SUB R8, R8, #1\n";
        text_ += "    JMP .Lunwind_site_loop\n";
        text_ += ".Lunwind_pop:\n";
        text_ += "    ADD R9, R2, R1\n";
        text_ += "    LOAD R3, [R9 + 4]\n";
        text_ += "    LOAD R2, [R9]\n";
        text_ += "    JMP __kr32_unwind_continue\n";
        text_ += ".Lunwind_next_desc:\n";
        text_ += "    ADD R6, R6, #4\n";
        text_ += "    SUB R7, R7, #1\n";
        text_ += "    JMP .Lunwind_desc_loop\n";
        text_ += ".Lunwind_fail:\n";
        text_ += "    MOV R0, #1\n";
        text_ += "    CALL exit\n";
        text_ += "    HALT\n";
        text_ += ".weak _Unwind_Resume\n_Unwind_Resume:\n";
        text_ += "    LOAD R2, [__kr32_unwind_frame]\n";
        text_ += "    LOAD R3, [__kr32_unwind_pc]\n";
        text_ += "    LEA R4, [__eh_frame_array_start]\n";
        text_ += "    LEA R5, [__eh_frame_array_end]\n";
        text_ += ".Lunwind_resume_table:\n";
        text_ += "    CMP R4, R5\n";
        text_ += "    JGE .Lunwind_fail\n";
        text_ += "    LOAD R6, [R4]\n";
        text_ += "    ADD R4, R4, #4\n";
        text_ += "    LOAD R7, [R6]\n";
        text_ += "    ADD R6, R6, #4\n";
        text_ += ".Lunwind_resume_desc:\n";
        text_ += "    CMP R7, #0\n";
        text_ += "    JLE .Lunwind_resume_table\n";
        text_ += "    LOAD R10, [R6]\n";
        text_ += "    LOAD R9, [R10]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JLT .Lunwind_resume_next\n";
        text_ += "    LOAD R9, [R10 + 4]\n";
        text_ += "    CMP R3, R9\n";
        text_ += "    JGT .Lunwind_resume_next\n";
        text_ += "    LOAD R1, [R10 + 8]\n";
        text_ += "    ADD R9, R2, R1\n";
        text_ += "    LOAD R3, [R9 + 4]\n";
        text_ += "    LOAD R2, [R9]\n";
        text_ += "    JMP __kr32_unwind_continue\n";
        text_ += ".Lunwind_resume_next:\n";
        text_ += "    ADD R6, R6, #4\n";
        text_ += "    SUB R7, R7, #1\n";
        text_ += "    JMP .Lunwind_resume_desc\n";
        bss_ += ".weak __kr32_unwind_frame\n";
        bss_ += "__kr32_unwind_frame:\n    .space 4\n";
        bss_ += ".weak __kr32_unwind_pc\n";
        bss_ += "__kr32_unwind_pc:\n    .space 4\n";
    }

    void emit_static_fini() {
        text_ += ".weak __kr32_cxx_fini\n";
        text_ += "__kr32_cxx_fini:\n";
        text_ += "    LEA R0, [__kr32_cxx_fini_done]\n";
        text_ += "    LOAD R1, [R0]\n";
        text_ += "    CMP R1, #0\n";
        text_ += "    JNZ .Lcxx_fini_done\n";
        text_ += "    MOV R1, #1\n";
        text_ += "    STORE R1, [R0]\n";
        emit_fini_array_loop();
        text_ += ".Lcxx_fini_done:\n";
        text_ += "    RET\n";
        bss_ += ".weak __kr32_cxx_fini_done\n";
        bss_ += "__kr32_cxx_fini_done:\n    .space 4\n";
        text_ += ".weak __cxa_allocate_exception\n__cxa_allocate_exception:\n";
        text_ += "    ADD R0, R0, #12\n";
        text_ += "    CALL malloc\n";
        text_ += "    ADD R0, R0, #12\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_throw\n__cxa_throw:\n";
        text_ += "    STORE R1, [R0 - 12]\n";
        text_ += "    STORE R2, [R0 - 8]\n";
        text_ += "    MOV R3, #0\n";
        text_ += "    STORE R3, [R0 - 4]\n";
        text_ += "    STORE R0, [__kr32_exception_object]\n";
        text_ += "    MOV R3, #1\n";
        text_ += "    STORE R3, [__kr32_exception_pending]\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_begin_catch\n__cxa_begin_catch:\n";
        text_ += "    CMP R0, #0\n";
        text_ += "    JZ .Lbegin_catch_done\n";
        text_ += "    LOAD R1, [R0 - 4]\n";
        text_ += "    ADD R1, R1, #1\n";
        text_ += "    STORE R1, [R0 - 4]\n";
        text_ += ".Lbegin_catch_done:\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_end_catch\n__cxa_end_catch:\n";
        text_ += "    CMP R0, #0\n";
        text_ += "    JZ .Lend_catch_done\n";
        text_ += "    LOAD R1, [R0 - 4]\n";
        text_ += "    SUB R1, R1, #1\n";
        text_ += "    STORE R1, [R0 - 4]\n";
        text_ += "    CMP R1, #0\n";
        text_ += "    JNZ .Lend_catch_done\n";
        text_ += "    LOAD R2, [R0 - 8]\n";
        text_ += "    CMP R2, #0\n";
        text_ += "    JZ .Lend_catch_free\n";
        text_ += "    PUSH R0\n";
        text_ += "    CALL R2\n";
        text_ += "    POP R0\n";
        text_ += ".Lend_catch_free:\n";
        text_ += "    SUB R0, R0, #12\n";
        text_ += "    CALL free\n";
        text_ += ".Lend_catch_done:\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_rethrow\n__cxa_rethrow:\n";
        text_ += "    CMP R0, #0\n";
        text_ += "    JZ .Lrethrow_done\n";
        text_ += "    LOAD R1, [R0 - 4]\n";
        text_ += "    SUB R1, R1, #1\n";
        text_ += "    STORE R1, [R0 - 4]\n";
        text_ += ".Lrethrow_done:\n";
        text_ += "    MOV R0, #1\n";
        text_ += "    STORE R0, [__kr32_exception_pending]\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_guard_acquire\n__cxa_guard_acquire:\n";
        text_ += ".Lguard_retry:\n";
        text_ += "    LL R1, [R0]\n";
        text_ += "    CMP R1, #2\n";
        text_ += "    JZ .Lguard_ready\n";
        text_ += "    CMP R1, #0\n";
        text_ += "    JNZ .Lguard_wait\n";
        text_ += "    MOV R2, #1\n";
        text_ += "    SC R2, [R0]\n";
        text_ += "    JNZ .Lguard_retry\n";
        text_ += "    MOV R0, #1\n";
        text_ += "    RET\n";
        text_ += ".Lguard_wait:\n";
        text_ += "    PUSH R0\n";
        text_ += "    MOV R1, #1\n";
        text_ += "    SYSCALL #48\n";
        text_ += "    POP R0\n";
        text_ += "    JMP .Lguard_retry\n";
        text_ += ".Lguard_ready:\n";
        text_ += "    MOV R0, #0\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_guard_release\n__cxa_guard_release:\n";
        text_ += "    MOV R1, #2\n";
        text_ += "    STORE R1, [R0]\n";
        text_ += "    PUSH R0\n";
        text_ += "    MOV R1, #1\n";
        text_ += "    SYSCALL #49\n";
        text_ += "    POP R0\n";
        text_ += "    RET\n";
        text_ += ".weak __cxa_atexit\n__cxa_atexit:\n";
        text_ += "    LEA R3, [__kr32_atexit_count]\n";
        text_ += "    LOAD R4, [R3]\n";
        text_ += "    CMP R4, #16\n";
        text_ += "    JGE .Latexit_full\n";
        text_ += "    LEA R5, [__kr32_atexit_funcs]\n";
        text_ += "    MUL R6, R4, #4\n";
        text_ += "    ADD R5, R5, R6\n";
        text_ += "    STORE R0, [R5]\n";
        text_ += "    LEA R5, [__kr32_atexit_args]\n";
        text_ += "    ADD R5, R5, R6\n";
        text_ += "    STORE R1, [R5]\n";
        text_ += "    ADD R4, R4, #1\n";
        text_ += "    STORE R4, [R3]\n";
        text_ += ".Latexit_full:\n";
        text_ += "    MOV R0, #0\n    RET\n";
        text_ += ".text\n.Lcxx_fini:\n";
        text_ += "    PUSH R7\n    ADJSP #-24\n    LEA R7, [SP]\n";
        text_ += "    LEA R1, [__kr32_atexit_count]\n";
        text_ += "    LOAD R2, [R1]\n";
        text_ += "    STORE R2, [R7 + 0]\n";
        text_ += ".Latexit_loop:\n";
        text_ += "    LOAD R2, [R7 + 0]\n";
        text_ += "    CMP R2, #0\n";
        text_ += "    JLE .Latexit_done\n";
        text_ += "    SUB R2, R2, #1\n";
        text_ += "    STORE R2, [R7 + 0]\n";
        text_ += "    LEA R3, [__kr32_atexit_funcs]\n";
        text_ += "    MUL R4, R2, #4\n";
        text_ += "    ADD R3, R3, R4\n";
        text_ += "    LOAD R5, [R3]\n";
        text_ += "    LEA R3, [__kr32_atexit_args]\n";
        text_ += "    ADD R3, R3, R4\n";
        text_ += "    LOAD R0, [R3]\n";
        text_ += "    CALL R5\n";
        text_ += "    JMP .Latexit_loop\n";
        text_ += ".Latexit_done:\n";
        for (auto iterator = static_destructors_.rbegin();
             iterator != static_destructors_.rend(); ++iterator) {
            Global* global = *iterator;
            ClassInfo* info = class_of(global->type);
            if (info == nullptr) {
                continue;
            }
            if (Function* destructor = find_destructor(info)) {
                text_ += "    LEA R0, [" + global->symbol + "]\n";
                text_ += "    CALL " + destructor->symbol + "\n";
            }
        }
        for (auto iterator = tls_destructors_.rbegin();
             iterator != tls_destructors_.rend(); ++iterator) {
            Global* global = *iterator;
            ClassInfo* info = class_of(global->type);
            if (info == nullptr) {
                continue;
            }
            if (Function* destructor = find_destructor(info)) {
                emit_tls_address(global);
                text_ += "    CALL " + destructor->symbol + "\n";
            }
        }
        text_ += "    ADJSP #24\n    POP R7\n    RET\n";
        startup_ += ".fini_array\n    .word .Lcxx_fini\n";
        bss_ += ".weak __kr32_atexit_count\n";
        bss_ += "__kr32_atexit_count:\n    .space 4\n";
        bss_ += ".weak __kr32_atexit_funcs\n";
        bss_ += "__kr32_atexit_funcs:\n    .space 64\n";
        bss_ += ".weak __kr32_atexit_args\n";
        bss_ += "__kr32_atexit_args:\n    .space 64\n";
    }

    void write_constant_scalar(std::vector<std::uint8_t>& bytes, int offset,
                               const TypePtr& type, std::int64_t value) {
        const int size = std::max(1, type_size(type));
        for (int index = 0; index < size && offset + index < static_cast<int>(bytes.size());
             ++index) {
            bytes[static_cast<std::size_t>(offset + index)] =
                static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >>
                                           (index * 8)) &
                                          0xFFU);
        }
    }

    void emit_constant_object(const TypePtr& type, const Expr& initializer,
                              std::vector<std::uint8_t>& bytes, int offset) {
        if (type->kind == TypeKind::Array && type->element &&
            type->element->kind == TypeKind::Builtin &&
            type->element->builtin == BuiltinKind::Char &&
            initializer.kind == ExprKind::StringLiteral) {
            const int limit = std::min(static_cast<int>(type->array_size),
                                       static_cast<int>(initializer.text.size() + 1));
            for (int index = 0; index < limit; ++index) {
                bytes[static_cast<std::size_t>(offset + index)] =
                    index < static_cast<int>(initializer.text.size())
                        ? static_cast<std::uint8_t>(
                              initializer.text[static_cast<std::size_t>(index)])
                        : 0;
            }
            return;
        }
        if (type->kind == TypeKind::Array) {
            const int element_size = type_size(type->element);
            std::size_t index = 0;
            for (const ExprPtr& element : initializer.children) {
                if (!element) {
                    continue;
                }
                if (element->has_index_designator) {
                    if (element->index_designator < 0) {
                        error(element->location, "array designator is out of range");
                        continue;
                    }
                    index = static_cast<std::size_t>(element->index_designator);
                }
                if (index >= type->array_size) {
                    error(element->location, "array designator is out of range");
                    continue;
                }
                emit_constant_object(type->element, *element, bytes,
                                     offset + static_cast<int>(index) * element_size);
                ++index;
            }
            return;
        }
        if (ClassInfo* info = class_of(type)) {
            std::size_t index = 0;
            for (const ExprPtr& value : initializer.children) {
                if (!value) {
                    continue;
                }
                if (value->has_field_designator) {
                    index = info->members.size();
                    for (std::size_t candidate = 0; candidate < info->members.size();
                         ++candidate) {
                        if (info->members[candidate].name == value->field_designator) {
                            index = candidate;
                            break;
                        }
                    }
                    if (index >= info->members.size()) {
                        error(value->location,
                              "unknown field designator: " + value->field_designator);
                        continue;
                    }
                }
                if (index >= info->members.size()) {
                    error(value->location, "too many aggregate initializer elements");
                    continue;
                }
                const MemberInfo& member = info->members[index];
                emit_constant_object(member.type, *value, bytes,
                                     offset + member.offset);
                ++index;
            }
            return;
        }
        if (is_fp(type)) {
            double value = 0.0;
            if (!eval_const_fp(initializer, value)) {
                error(initializer.location, "global initializer must be constant");
                return;
            }
            if (is_float32(type)) {
                const float narrow = static_cast<float>(value);
                std::memcpy(bytes.data() + offset, &narrow, sizeof(narrow));
            } else {
                std::memcpy(bytes.data() + offset, &value, sizeof(value));
            }
            return;
        }
        std::int64_t value = 0;
        if (!eval_const(initializer, value)) {
            error(initializer.location, "global initializer must be constant");
            return;
        }
        write_constant_scalar(bytes, offset, type, value);
    }

    void emit_globals(bool late_template_statics = false) {
        auto append_data = [&](const Global* global, const std::string& text) {
            if (global->template_static_member) {
                data_ += ".section .data." + global->symbol + "\n" + text + "\n.data\n";
            } else {
                data_ += text;
            }
        };
        auto append_bss = [&](const Global* global, const std::string& text) {
            if (global->template_static_member) {
                bss_ += ".section .bss." + global->symbol + "\n" + text + "\n.bss\n";
            } else {
                bss_ += text;
            }
        };
        for (const auto& global : global_storage_) {
            if (global->template_static_member != late_template_statics) {
                continue;
            }
            if (!global->defined) {
                continue;
            }
            if (global->is_thread_local) {
                const int size = std::max(1, type_size(global->type));
                tls_symbols_.push_back({global->symbol, global->tls_offset, size,
                                        !global->is_static});
                data_ += global->tls_slot_symbol + ":\n    .word " +
                         global->symbol + "\n";
                if (!tls_base_emitted_) {
                    data_ += ".Ltls_base:\n    .word __kr32_tls_start\n";
                    tls_base_emitted_ = true;
                }
                const std::size_t end =
                    static_cast<std::size_t>(global->tls_offset + size);
                if (tls_image_.size() < end) {
                    tls_image_.resize(end, 0);
                }
                if (ClassInfo* info = class_of(global->type)) {
                    if (global->initializer &&
                        global->initializer->kind == ExprKind::InitializerList &&
                        find_constructor(info, global->initializer->children.size()) ==
                            nullptr) {
                        std::vector<std::uint8_t> bytes(
                            static_cast<std::size_t>(size), 0);
                        emit_constant_object(global->type, *global->initializer, bytes,
                                             0);
                        std::memcpy(tls_image_.data() + global->tls_offset,
                                    bytes.data(), bytes.size());
                    } else if (global->initializer ||
                               find_constructor(info, 0) != nullptr) {
                        global->needs_init = true;
                    }
                    if (find_destructor(info) != nullptr) {
                        tls_destructors_.push_back(global.get());
                    }
                    continue;
                }
                if (global->initializer) {
                    if (is_fp(global->type)) {
                        double value = 0.0;
                        if (eval_const_fp(*global->initializer, value)) {
                            if (is_float32(global->type)) {
                                const float narrow = static_cast<float>(value);
                                std::memcpy(tls_image_.data() + global->tls_offset,
                                            &narrow, sizeof(narrow));
                            } else {
                                std::memcpy(tls_image_.data() + global->tls_offset,
                                            &value, sizeof(value));
                            }
                        } else {
                            global->needs_init = true;
                        }
                    } else {
                        std::int64_t value = 0;
                        if (eval_const(*global->initializer, value)) {
                            if (size == 1) {
                                tls_image_[static_cast<std::size_t>(
                                    global->tls_offset)] =
                                    static_cast<std::uint8_t>(value);
                            } else if (is_long_long(global->type)) {
                                const std::int64_t wide = value;
                                std::memcpy(tls_image_.data() + global->tls_offset,
                                            &wide, sizeof(wide));
                            } else {
                                const std::uint32_t word =
                                    static_cast<std::uint32_t>(value);
                                std::memcpy(tls_image_.data() + global->tls_offset,
                                            &word, sizeof(word));
                            }
                        } else {
                            global->needs_init = true;
                        }
                    }
                }
                continue;
            }
            const int size = std::max(1, type_size(global->type));
            debug_record("G " + global->qualified + " " + global->symbol + " " +
                         debug_type(global->type) + " " + std::to_string(size));
            std::string output;
            output += global->template_static_member
                          ? ".weak " + global->symbol + "\n"
                          : ".global " + global->symbol + "\n";
            output += global->symbol + ":\n";
            if (ClassInfo* info = class_of(global->type)) {
                if (global->initializer &&
                    global->initializer->kind == ExprKind::InitializerList &&
                    find_constructor(info, global->initializer->children.size()) == nullptr) {
                    std::vector<std::uint8_t> bytes(
                        static_cast<std::size_t>(std::max(1, size)), 0);
                    emit_constant_object(global->type, *global->initializer, bytes, 0);
                    std::string image = ".align " +
                                        std::to_string(std::max(1, type_align(global->type))) +
                                        "\n" + output;
                    for (std::uint8_t byte : bytes) {
                        image += "    .byte " + std::to_string(byte) + "\n";
                    }
                    append_data(global.get(), image);
                    continue;
                }
                if (find_destructor(info) != nullptr) {
                    static_destructors_.push_back(global.get());
                }
                if (global->initializer || find_constructor(info, 0) != nullptr) {
                    global->needs_init = true;
                    output += "    .space " + std::to_string(size) + "\n";
                    append_bss(global.get(), output);
                    continue;
                }
            }
            if (global->initializer && global->type &&
                global->type->kind == TypeKind::Array &&
                global->initializer->kind == ExprKind::InitializerList) {
                std::vector<std::uint8_t> bytes(
                    static_cast<std::size_t>(std::max(1, size)), 0);
                emit_constant_object(global->type, *global->initializer, bytes, 0);
                std::string image = ".align " +
                                    std::to_string(std::max(1, type_align(global->type))) +
                                    "\n" + output;
                for (std::uint8_t byte : bytes) {
                    image += "    .byte " + std::to_string(byte) + "\n";
                }
                append_data(global.get(), image);
                continue;
            }
            if (global->initializer && !is_constant_global(*global)) {
                global->needs_init = true;
                output += "    .space " + std::to_string(size) + "\n";
                append_bss(global.get(), output);
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
                    } else if (is_long_long(global->type)) {
                        const std::uint64_t bits =
                            static_cast<std::uint64_t>(value);
                        output += "    .word " +
                                  std::to_string(static_cast<std::uint32_t>(bits)) +
                                  "\n";
                        output += "    .word " +
                                  std::to_string(
                                      static_cast<std::uint32_t>(bits >> 32)) +
                                  "\n";
                    } else {
                        output += "    .word " + std::to_string(value & 0xFFFFFFFFU) + "\n";
                    }
                }
                append_data(global.get(), output);
            } else {
                output += "    .space " + std::to_string(size) + "\n";
                append_bss(global.get(), output);
            }
        }
    }

    std::vector<Diagnostic>& diagnostics_;
    std::vector<std::unique_ptr<Function>> function_storage_;
    std::map<std::string, std::vector<Function*>> function_index_;
    std::map<std::string, std::vector<const Decl*>> template_functions_;
    std::map<std::string, std::vector<const Decl*>> function_specializations_;
    struct TemplateMemberDefinition {
        std::string owner;
        std::vector<TypePtr> pattern;
        std::string method;
        const Decl* decl = nullptr;
    };
    std::vector<TemplateMemberDefinition> template_member_definitions_;
    struct TemplateStaticDefinition {
        std::string owner;
        std::vector<TypePtr> pattern;
        std::string member;
        const Decl* decl = nullptr;
    };
    std::vector<TemplateStaticDefinition> template_static_definitions_;
    bool in_template_instantiation_ = false;
    std::vector<std::shared_ptr<Decl>> template_instantiation_decls_;
    std::vector<std::unique_ptr<Global>> global_storage_;
    std::map<std::string, Global*> global_index_;
    int tls_cursor_ = 0;
    std::vector<std::uint8_t> tls_image_;
    struct TlsSymbol {
        std::string symbol;
        int offset = 0;
        int size = 0;
        bool exported = false;
    };
    std::vector<TlsSymbol> tls_symbols_;
    std::vector<std::string> late_text_functions_;
    int lambda_thread_counter_ = 0;
    int tls_slot_counter_ = 0;
    bool tls_base_emitted_ = false;
    std::string startup_;
    std::string debug_;
    std::vector<Global*> static_destructors_;
    std::vector<Global*> tls_destructors_;
    std::map<std::string, TypePtr> type_aliases_;
    std::map<std::string, std::set<std::string>> imports_;
    std::map<std::string, std::map<std::string, std::string>> using_aliases_;
    std::map<std::string, std::unique_ptr<ClassInfo>> class_index_;
    std::vector<std::shared_ptr<Decl>> synthetic_declarations_;
    std::map<std::string, const Decl*> class_templates_;
    struct PartialTemplate {
        std::string name;
        std::vector<TypePtr> pattern;
        const Decl* decl = nullptr;
    };
    std::vector<PartialTemplate> class_partial_specializations_;
    std::map<std::string, const Decl*> class_specializations_;
    std::vector<DeclPtr> template_class_storage_;

    std::vector<std::map<std::string, Local>> local_scopes_;
    std::vector<std::vector<std::pair<int, Function*>>> local_destructors_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::map<const Expr*, Function*> call_targets_;
    std::map<const Expr*, Function*> lambda_functions_;
    std::vector<DeclPtr> lambda_decl_storage_;
    int lambda_counter_ = 0;
    std::set<const Expr*> diagnosed_expressions_;
    std::set<Function*> emitted_functions_;
    struct ExceptionHandler {
        std::string label;
        std::size_t scope_depth = 0;
    };
    struct LsdaRecord {
        std::string label;
        std::vector<std::pair<int, std::string>> entries;
    };
    struct CallSiteRecord {
        std::string start;
        std::string end;
        std::string landing;
    };
    struct FunctionEhRecord {
        std::string label;
        std::string text;
    };
    std::vector<ExceptionHandler> exception_handlers_;
    std::vector<LsdaRecord> exception_lsdas_;
    std::vector<CallSiteRecord> current_call_sites_;
    std::vector<FunctionEhRecord> function_eh_records_;
    int active_catch_count_ = 0;
    std::map<std::string, int> exception_type_ids_;
    bool uses_exceptions_ = false;
    std::string current_prefix_;
    ClassInfo* current_class_ = nullptr;
    int this_offset_ = -1;
    int current_sret_slot_ = -1;
    int current_lambda_closure_offset_ = -1;
    const Function* current_lambda_function_ = nullptr;
    Function* current_function_ = nullptr;
    std::string return_label_;
    std::string va_base_placeholder_;
    std::string text_;
    std::string rodata_;
    std::string lsda_;
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
