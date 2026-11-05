// KR32 matrix library example.
//
// Build and run:
//   kr32 build examples/matrix.cpp -o build/matrix-cxx.k32 --run
//
// The matrix functions are compiler intrinsics: calls lower to the MLOAD,
// MMUL, MVEC, MTRANS, and MSTORE tile instructions. They are only available
// from the C++ front end.

#include <kr32/matrix.hpp>

static kr32::mat4 make_translation(float tx, float ty, float tz) {
    kr32::mat4 m;
    m.m[0] = 1.0;  m.m[1] = 0.0;  m.m[2] = 0.0;  m.m[3] = tx;
    m.m[4] = 0.0;  m.m[5] = 1.0;  m.m[6] = 0.0;  m.m[7] = ty;
    m.m[8] = 0.0;  m.m[9] = 0.0;  m.m[10] = 1.0; m.m[11] = tz;
    m.m[12] = 0.0; m.m[13] = 0.0; m.m[14] = 0.0; m.m[15] = 1.0;
    return m;
}

static kr32::mat4 make_scale(float x, float y, float z) {
    kr32::mat4 m;
    m.m[0] = x;    m.m[1] = 0.0;  m.m[2] = 0.0;  m.m[3] = 0.0;
    m.m[4] = 0.0;  m.m[5] = y;    m.m[6] = 0.0;  m.m[7] = 0.0;
    m.m[8] = 0.0;  m.m[9] = 0.0;  m.m[10] = z;   m.m[11] = 0.0;
    m.m[12] = 0.0; m.m[13] = 0.0; m.m[14] = 0.0; m.m[15] = 1.0;
    return m;
}

int main() {
    kr32::vec4 point;
    point.x = 2.0;
    point.y = 4.0;
    point.z = 6.0;
    point.w = 1.0;
    kr32::mat4 identity;
    for (int i = 0; i < 16; ++i) {
        identity.m[i] = (i % 5 == 0) ? 1.0 : 0.0;
    }
    kr32::mat4* madeMatrix = new kr32::mat4;
    *madeMatrix = identity;
    if (madeMatrix->m[0] != 1.0 || madeMatrix->m[1] != 0.0) {
        return 4;
    }
    delete madeMatrix;

    // Translation uses the fourth column of the row-major matrix.
    kr32::mat4 translation = make_translation(10.0, 20.0, 30.0);
    kr32::vec4 translated = kr32::transform(translation, point);
    if (translated.x != 12.0 || translated.y != 24.0 ||
        translated.z != 36.0 || translated.w != 1.0) {
        return 1;
    }

    // Apply the scale first, then the translation: model = translation * scale.
    kr32::mat4 scale = make_scale(2.0, 2.0, 2.0);
    kr32::mat4 model = kr32::matrix_mul(translation, scale);
    kr32::vec4 result = kr32::transform(model, point);
    if (result.x != 14.0 || result.y != 28.0 ||
        result.z != 42.0 || result.w != 1.0) {
        return 2;
    }

    // Aliasing is safe: the product is computed before the assignment.
    kr32::mat4 twice = kr32::matrix_mul(scale, scale);
    scale = kr32::matrix_mul(scale, twice);
    if (scale.m[0] != 8.0) {
        return 3;
    }

    return 42;
}
