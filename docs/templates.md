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
- Template-parameter binding from the matched actual type
- Fallback to the generic template when no pattern matches

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

### Function-template explicit specializations

```cpp
template <typename T> T twice(T value) { return value + value; }
template <> int twice<int>(int value) { return 42; }
```

- Explicit specialization arguments are matched against call arguments
- Specialized bodies replace the generic instantiation for matching calls

## Deferred

- Non-type parameters usable as array bounds
- Nested and multi-level partial-specialization patterns
- Reference and template-template parameter patterns
- COMDAT section groups and duplicate-instantiation merging
- Template-aware debug information beyond class/function records

## Testing

Template regressions live in `tests/cxx/test_cxx_codegen.cpp` and cover
generic instantiation, explicit class specializations, pointer-pattern partial
specializations, and initializer-list templates.
