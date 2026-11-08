#pragma once

// Internal C++ front-end AST. This header is part of the compiler
// implementation: it is not a public API and may change as later M9 phases add
// semantic analysis and code generation.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::cxx::ast {

struct Location {
    int line = 0;
    int column = 0;
};

// A possibly qualified name. `global` records a leading `::`.
struct Name {
    std::vector<std::string> parts;
    bool global = false;

    [[nodiscard]] bool empty() const { return parts.empty(); }
    [[nodiscard]] std::string to_string() const;
};

enum class BuiltinKind {
    Void,
    Bool,
    Char,
    Int,
    UnsignedInt,
    LongLong,
    UnsignedLongLong,
    Float,
    Double,
    LongDouble,
    Auto,
};

enum class TypeKind {
    Builtin,
    Named,
    Pointer,
    LValueRef,
    RValueRef,
    Array,
    Function,
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct ArrayBoundExpr;
using ArrayBoundExprPtr = std::shared_ptr<ArrayBoundExpr>;

struct ArrayBoundExpr {
    enum class Op { Value, Name, Add, Sub, Mul, Div };
    Op op = Op::Value;
    std::int64_t value = 0;
    std::string name;
    ArrayBoundExprPtr left;
    ArrayBoundExprPtr right;
};

struct Type {
    TypeKind kind = TypeKind::Builtin;
    BuiltinKind builtin = BuiltinKind::Int;
    Name name;                       // Named
    TypePtr element;                 // Pointer / reference / array
    std::uint32_t array_size = 0;    // Array (0 = unknown)
    std::string array_bound_name;    // Array bound symbol (template parameter)
    int array_bound_offset = 0;      // Constant offset applied to the bound
    int array_bound_scale = 1;       // Constant multiplier applied to the bound
    ArrayBoundExprPtr array_bound_expression; // Dependent bound expression
    std::vector<TypePtr> parameters; // Function
    std::vector<TypePtr> template_arguments; // Named template-id
    bool variadic = false;           // Function
    bool is_const = false;
    bool is_volatile = false;
    bool is_noexcept = false;
    bool has_template_integer_value = false;
    std::int64_t template_integer_value = 0;

    [[nodiscard]] bool is_builtin(BuiltinKind value) const {
        return kind == TypeKind::Builtin && builtin == value;
    }
    [[nodiscard]] bool is_integral() const;
    [[nodiscard]] bool is_floating() const;
    [[nodiscard]] bool is_pointer_like() const;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] TypePtr clone() const;

    static TypePtr make_builtin(BuiltinKind value);
    static TypePtr make_named(Name value);
    static TypePtr make_pointer(TypePtr element, bool is_const = false,
                                bool is_volatile = false);
    static TypePtr make_lvalue_ref(TypePtr element, bool is_const = false);
    static TypePtr make_rvalue_ref(TypePtr element);
    static TypePtr make_array(TypePtr element, std::uint32_t size);
};

enum class Access {
    Public,
    Protected,
    Private,
};

struct Expr;
struct Stmt;
struct Decl;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;
using DeclPtr = std::shared_ptr<Decl>;

struct Param {
    TypePtr type;
    Name name;
    ExprPtr default_value;
    Location location;
    bool variadic = false;
};

enum class ExprKind {
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    CharacterLiteral,
    BoolLiteral,
    NullptrLiteral,
    Name,
    This,
    Unary,
    Binary,
    Assign,
    Conditional,
    Call,
    Index,
    Member,
    Scope,
    New,
    Delete,
    Cast,
    Sizeof,
    Alignof,
    Typeid,
    Lambda,
    InitializerList,
    Unknown,
};

struct Expr {
    ExprKind kind = ExprKind::Unknown;
    Location location;
    std::string text;          // operator spelling, decoded string, or literal text
    std::int64_t integer = 0;
    double floating = 0.0;
    Name name;                 // Name/Member/Scope/Special
    TypePtr type;              // Cast/New/Sizeof/Alignof/trailing return
    std::vector<ExprPtr> children;
    std::vector<ExprPtr> arguments; // call/new/lambda body expression list
    std::vector<ExprPtr> placement_arguments; // new (placement) T
    ExprPtr new_initializer;   // new T{...} / new T[n]{...}
    std::vector<DeclPtr> parameters; // lambda parameters
    StmtPtr body;              // lambda body
    std::vector<Name> template_arguments;
    std::vector<TypePtr> template_type_arguments;
    bool is_arrow = false;     // Member access through `->`
    bool is_prefix = false;    // prefix unary/increment
    bool global_scope = false; // leading `::`
    bool new_nothrow = false;  // new (std::nothrow) T
    bool new_value_initialize = false; // new T() / new T[n]()
    bool has_field_designator = false;
    bool has_index_designator = false;
    std::string field_designator;
    int index_designator = 0;
};

enum class StmtKind {
    Empty,
    Compound,
    Expression,
    Declaration,
    If,
    While,
    DoWhile,
    For,
    RangeFor,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Return,
    Throw,
    Try,
    Goto,
    Label,
    Unknown,
};

struct CatchHandler {
    DeclPtr parameter;
    StmtPtr body;
    Location location;
};

struct Stmt {
    StmtKind kind = StmtKind::Unknown;
    Location location;
    ExprPtr expression;        // expression stmt / if/while/switch/return/throw
    ExprPtr condition;         // for/range-for
    ExprPtr increment;         // for
    DeclPtr declaration;       // declaration stmt / for init / range-for decl
    StmtPtr body;              // loop/if body
    StmtPtr else_body;         // if
    std::vector<StmtPtr> statements; // compound / switch body / try body
    std::vector<CatchHandler> handlers;
    std::string label;
};

enum class DeclKind {
    Namespace,
    Class,
    Enum,
    Function,
    Variable,
    Parameter,
    Using,
    TypeAlias,
    Template,
    StaticAssert,
    Access,
    Friend,
    Unknown,
};

struct Initializer {
    Name name;
    std::vector<ExprPtr> arguments;
    bool is_base = false;
};

struct EnumMember {
    Name name;
    ExprPtr value;
    Location location;
};

struct Decl {
    DeclKind kind = DeclKind::Unknown;
    Location location;
    Name name;
    TypePtr type;
    std::vector<Param> parameters;
    std::vector<DeclPtr> children;        // namespace/class/function/enum members
    std::vector<DeclPtr> template_parameters;
    std::vector<TypePtr> specialization_arguments;
    std::vector<TypePtr> owner_template_arguments;
    bool is_non_type_template_parameter = false;
    std::vector<Initializer> initializers; // constructor base/member init list
    std::vector<Name> base_names;          // class bases
    std::vector<EnumMember> enum_members;
    StmtPtr body;
    ExprPtr condition;                    // static_assert
    ExprPtr message;                      // static_assert message
    ExprPtr initializer;                  // variable initializer

    bool variadic = false;
    bool is_const = false;
    bool is_volatile = false;
    bool is_static = false;
    bool is_virtual = false;
    bool is_inline = false;
    bool is_explicit = false;
    int alignment = 0;
    bool is_friend = false;
    bool is_pure = false;
    bool is_defaulted = false;
    bool is_deleted = false;
    bool is_constexpr = false;
    bool is_mutable = false;
    bool is_thread_local = false;
    bool is_typedef = false;
    bool is_final = false;
    bool is_override = false;
    bool is_template = false;
    bool is_specialization = false;
    bool is_enum_class = false;
    bool is_extern_c = false;
    bool is_extern = false;
    bool is_using_namespace = false;
    bool is_constructor = false;
    bool is_destructor = false;
    bool is_operator = false;
    Access access = Access::Public;
};

struct TranslationUnit {
    std::string source_name;
    std::vector<DeclPtr> declarations;
};

[[nodiscard]] std::string_view expr_kind_name(ExprKind kind);
[[nodiscard]] std::string_view stmt_kind_name(StmtKind kind);
[[nodiscard]] std::string_view decl_kind_name(DeclKind kind);
[[nodiscard]] std::string_view builtin_kind_name(BuiltinKind kind);

} // namespace kr32::cxx::ast
