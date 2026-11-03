#pragma once

// First-gate matrix/transform library surface for KR32 C++.
//
// The compiler recognizes these declarations and lowers the named functions to
// tile instructions. The freestanding build injects the same declarations when
// it sees `#include <kr32/matrix.hpp>`; this header is the user-facing copy.

namespace kr32 {

struct vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct mat4 {
    float m[16]; // row-major 4x4 f32
};

mat4 matrix_mul(const mat4& a, const mat4& b);                 // MMUL
mat4 matrix_mul_add(const mat4& a, const mat4& b, const mat4& c); // MMAC
mat4 transpose(const mat4& a);                                // MTRANS
vec4 transform(const mat4& matrix, const vec4& vector);       // MVEC
float dot(const vec4& a, const vec4& b);                      // VDOT
void push(const mat4& matrix);                                // MPUSH
void pop(mat4& matrix);                                       // MPOP
void drop();                                                  // MDROP

} // namespace kr32
