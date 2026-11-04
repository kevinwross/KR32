# C++ templates in KR32

This document records the template surface currently implemented by the KR32
C++ compiler and the parts still deferred.

## Supported

### Function templates

```cpp
template <typename T>
T twice(T value) {
    return value + value;
}

int main() {
    return twice(21);
}
```

- Type-parameter deduction from call arguments
- Explicit template arguments in calls where the parser accepts them
- Instantiation and weak emission of the resulting function

### Class templates

```cpp
template <typename T>
class Box {
public:
    T value;
    T get() { return value; }
};
```

- Field substitution
- Method substitution
- Nested constructor/destructor instantiation
- Per-instantiation layout and method symbols
- Method calls on instantiated objects

### Explicit class specializations

```cpp
template <>
class Box<int> {
public:
    int value;
    int get() { return 42; }
};
```

- Full explicit specializations
- Specialization selected before the generic template

### Partial specializations

```cpp
template <typename T>
class Box<T*> {
public:
    T* value;
};
```

- One-level pointer patterns such as `T*`
- Multi-level pointer patterns such as `T**`
- Reference patterns such as `T&`
- Fixed-size array patterns such as `T[3]`
- Template-parameter binding from the matched actual type
- Fallback to the generic template when no pattern matches

### Non-type parameters

```cpp
template <int N>
struct Array {
    int values[N + 1];
};
```

- Integer non-type parameters
- Array bounds derived from a parameter plus a constant multiplier and/or
  offset
- Constant expressions in non-type arguments
- Template static data members emitted weak and merged across translation
  units
- Function-local statics inside template instantiations merged across
  translation units
- Template function instantiations emitted in `.text.<symbol>` COMDAT
  sections and deduplicated by the linker

### Out-of-class member definitions

```cpp
template <typename T>
T Box<T>::get() {
    return value;
}
```

- Attached to each instantiation during class-template cloning
- Parameter and return types substituted through the class bindings
- Also supported for explicit class specializations

### Static data members

```cpp
template <typename T>
class Box {
public:
    static T value;
};

template <typename T>
T Box<T>::value = 42;
```

- Definition initializers attach to each instantiation
- Late static-template globals are emitted after function code generation
- Static members are accessible through an instance or as
  `Box<int>::value`

### Non-type template parameters

```cpp
template <int N>
class Box {
public:
    int size() { return N; }
};
```

- Integer literal template arguments
- Distinct instantiations for distinct values
- Bound values used inside member function bodies
- Bound values used as array sizes
- Integer constant expressions such as `2 + 3` and `1 << 3`
- Dependent array bounds over multiple parameters, with `+`, `-`, `*`, `/`,
  unary minus, and parentheses, such as `N + M` or `(N + M) * 2`

### Nested template-id partial patterns

```cpp
template <typename T> struct Box { int kind() { return 1; } };
template <typename T> struct Box<Box<T>> { int kind() { return 2; } };
```

- Nested template-id patterns match recursively
- Type parameters bind through each nested argument layer

### COMDAT groups

- Template functions are emitted in `.text.<symbol>` COMDAT sections
- Template static data members use `.data.<symbol>` COMDAT sections
- Template function-local statics and their guards use `.bss.<symbol>` COMDAT
  sections
- The linker retains one copy of each matching COMDAT section across objects

### Function-template explicit specializations

```cpp
template <typename T> T twice(T value) { return value + value; }
template <> int twice<int>(int value) { return 42; }
```

- Explicit specialization arguments are matched against call arguments
- Specialized bodies replace the generic instantiation for matching calls
- Local declarations such as `int first = twice(1);` retain the call
- Specialized and generic instantiations of the same template can coexist in
  one translation unit

## Deferred

- Arbitrary dependent expressions beyond array-bound constant arithmetic
- Full template partial ordering, SFINAE, variable templates, and concepts
- Template-aware debug information beyond class/function/variable records
- Nested template-id patterns outside the supported recursive argument match

## Testing

Template regressions live in `tests/cxx/test_cxx_codegen.cpp` and cover
generic instantiation, explicit class specializations, pointer-pattern partial
specializations, and initializer-list templates.
