#include "cxx/ast.h"

#include <sstream>
#include <utility>

namespace kr32::cxx::ast {

namespace {

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string result;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            result += separator;
        }
        result += parts[index];
    }
    return result;
}

} // namespace

std::string Name::to_string() const {
    std::string result;
    if (global) {
        result += "::";
    }
    result += join(parts, "::");
    return result;
}

bool Type::is_integral() const {
    if (kind != TypeKind::Builtin) {
        return false;
    }
    switch (builtin) {
    case BuiltinKind::Bool:
    case BuiltinKind::Char:
    case BuiltinKind::UnsignedChar:
    case BuiltinKind::Short:
    case BuiltinKind::UnsignedShort:
    case BuiltinKind::Int:
    case BuiltinKind::UnsignedInt:
    case BuiltinKind::LongLong:
    case BuiltinKind::UnsignedLongLong:
        return true;
    default:
        return false;
    }
}

bool Type::is_floating() const {
    if (kind != TypeKind::Builtin) {
        return false;
    }
    return builtin == BuiltinKind::Float || builtin == BuiltinKind::Double ||
           builtin == BuiltinKind::LongDouble;
}

bool Type::is_pointer_like() const {
    return kind == TypeKind::Pointer || kind == TypeKind::LValueRef ||
           kind == TypeKind::RValueRef;
}

std::string Type::to_string() const {
    switch (kind) {
    case TypeKind::Builtin:
        return std::string(builtin_kind_name(builtin));
    case TypeKind::Named: {
        std::string result = name.to_string();
        if (!template_arguments.empty()) {
            result += "<";
            for (std::size_t index = 0; index < template_arguments.size(); ++index) {
                if (index != 0) {
                    result += ", ";
                }
                result += template_arguments[index] ? template_arguments[index]->to_string()
                                                    : "<null>";
            }
            result += ">";
        }
        return result;
    }
    case TypeKind::Pointer:
        return (element ? element->to_string() : std::string("<null>")) + "*";
    case TypeKind::LValueRef:
        return (element ? element->to_string() : std::string("<null>")) + "&";
    case TypeKind::RValueRef:
        return (element ? element->to_string() : std::string("<null>")) + "&&";
    case TypeKind::Array:
        return (element ? element->to_string() : std::string("<null>")) + "[" +
               std::to_string(array_size) + "]";
    case TypeKind::Function: {
        std::string result = (element ? element->to_string() : std::string("void")) + "(";
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (index != 0) {
                result += ", ";
            }
            result += parameters[index] ? parameters[index]->to_string() : "<null>";
        }
        if (variadic) {
            if (!parameters.empty()) {
                result += ", ";
            }
            result += "...";
        }
        result += ")";
        return result;
    }
    }
    return "<unknown>";
}

TypePtr Type::clone() const {
    auto result = std::make_shared<Type>();
    result->kind = kind;
    result->builtin = builtin;
    result->name = name;
    result->array_size = array_size;
    result->array_bound_name = array_bound_name;
    result->variadic = variadic;
    result->is_const = is_const;
    result->is_volatile = is_volatile;
    result->is_noexcept = is_noexcept;
    result->has_template_integer_value = has_template_integer_value;
    result->template_integer_value = template_integer_value;
    result->element = element ? element->clone() : nullptr;
    result->parameters.reserve(parameters.size());
    for (const TypePtr& parameter : parameters) {
        result->parameters.push_back(parameter ? parameter->clone() : nullptr);
    }
    result->template_arguments.reserve(template_arguments.size());
    for (const TypePtr& argument : template_arguments) {
        result->template_arguments.push_back(argument ? argument->clone() : nullptr);
    }
    return result;
}

TypePtr Type::make_builtin(BuiltinKind value) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::Builtin;
    result->builtin = value;
    return result;
}

TypePtr Type::make_named(Name value) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::Named;
    result->name = std::move(value);
    return result;
}

TypePtr Type::make_pointer(TypePtr element, bool is_const, bool is_volatile) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::Pointer;
    result->element = std::move(element);
    result->is_const = is_const;
    result->is_volatile = is_volatile;
    return result;
}

TypePtr Type::make_lvalue_ref(TypePtr element, bool is_const) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::LValueRef;
    result->element = std::move(element);
    result->is_const = is_const;
    return result;
}

TypePtr Type::make_rvalue_ref(TypePtr element) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::RValueRef;
    result->element = std::move(element);
    return result;
}

TypePtr Type::make_array(TypePtr element, std::uint32_t size) {
    auto result = std::make_shared<Type>();
    result->kind = TypeKind::Array;
    result->element = std::move(element);
    result->array_size = size;
    return result;
}

std::string_view expr_kind_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::IntegerLiteral: return "integer-literal";
    case ExprKind::FloatLiteral: return "float-literal";
    case ExprKind::StringLiteral: return "string-literal";
    case ExprKind::CharacterLiteral: return "character-literal";
    case ExprKind::BoolLiteral: return "bool-literal";
    case ExprKind::NullptrLiteral: return "nullptr-literal";
    case ExprKind::Name: return "name";
    case ExprKind::This: return "this";
    case ExprKind::Unary: return "unary";
    case ExprKind::Binary: return "binary";
    case ExprKind::Assign: return "assign";
    case ExprKind::Conditional: return "conditional";
    case ExprKind::Call: return "call";
    case ExprKind::Index: return "index";
    case ExprKind::Member: return "member";
    case ExprKind::Scope: return "scope";
    case ExprKind::New: return "new";
    case ExprKind::Delete: return "delete";
    case ExprKind::Cast: return "cast";
    case ExprKind::Sizeof: return "sizeof";
    case ExprKind::Alignof: return "alignof";
    case ExprKind::Typeid: return "typeid";
    case ExprKind::Lambda: return "lambda";
    case ExprKind::InitializerList: return "initializer-list";
    case ExprKind::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view stmt_kind_name(StmtKind kind) {
    switch (kind) {
    case StmtKind::Empty: return "empty";
    case StmtKind::Compound: return "compound";
    case StmtKind::Expression: return "expression";
    case StmtKind::Declaration: return "declaration";
    case StmtKind::If: return "if";
    case StmtKind::While: return "while";
    case StmtKind::DoWhile: return "do-while";
    case StmtKind::For: return "for";
    case StmtKind::RangeFor: return "range-for";
    case StmtKind::Switch: return "switch";
    case StmtKind::Case: return "case";
    case StmtKind::Default: return "default";
    case StmtKind::Break: return "break";
    case StmtKind::Continue: return "continue";
    case StmtKind::Return: return "return";
    case StmtKind::Throw: return "throw";
    case StmtKind::Try: return "try";
    case StmtKind::Goto: return "goto";
    case StmtKind::Label: return "label";
    case StmtKind::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view decl_kind_name(DeclKind kind) {
    switch (kind) {
    case DeclKind::Namespace: return "namespace";
    case DeclKind::Class: return "class";
    case DeclKind::Enum: return "enum";
    case DeclKind::Function: return "function";
    case DeclKind::Variable: return "variable";
    case DeclKind::Parameter: return "parameter";
    case DeclKind::Using: return "using";
    case DeclKind::TypeAlias: return "type-alias";
    case DeclKind::Template: return "template";
    case DeclKind::StaticAssert: return "static-assert";
    case DeclKind::Access: return "access";
    case DeclKind::Friend: return "friend";
    case DeclKind::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view builtin_kind_name(BuiltinKind kind) {
    switch (kind) {
    case BuiltinKind::Void: return "void";
    case BuiltinKind::Bool: return "bool";
    case BuiltinKind::Char: return "char";
    case BuiltinKind::UnsignedChar: return "unsigned char";
    case BuiltinKind::Short: return "short";
    case BuiltinKind::UnsignedShort: return "unsigned short";
    case BuiltinKind::Int: return "int";
    case BuiltinKind::UnsignedInt: return "unsigned int";
    case BuiltinKind::LongLong: return "long long";
    case BuiltinKind::UnsignedLongLong: return "unsigned long long";
    case BuiltinKind::Float: return "float";
    case BuiltinKind::Double: return "double";
    case BuiltinKind::LongDouble: return "long double";
    case BuiltinKind::Auto: return "auto";
    }
    return "unknown";
}

} // namespace kr32::cxx::ast
