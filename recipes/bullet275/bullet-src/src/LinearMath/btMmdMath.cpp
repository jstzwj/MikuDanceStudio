// MikuDanceStudio modifications: source-level forms of MMD's VC9 math.
// This file is compiled separately with x86 /arch:IA32 /fp:precise /O2 /Oi.
// double expressions retain x87 intermediates; volatile float marks an original
// float store. MSVC long double is only 64 bits and does not solve this problem.
// Ordered sums cross a noinline boundary before relaxed sqrt/trig evaluation:
// this prevents reassociation while selecting native x87 math, including the
// caller's precision/rounding mode. Do not enable whole-program optimization.
#include "btMmdMath.h"

#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)

#include <cmath>
#pragma float_control(precise, on)
#include <xmmintrin.h>

namespace btMmdMath {
namespace {
template <int I, int J, int K>
__declspec(noinline) double sumSquares(const btVector3& v) {
    return (double(v[I]) * v[I] + double(v[J]) * v[J]) + double(v[K]) * v[K];
}
__declspec(noinline) double pairSquares(float a, float b) {
    return double(a) * a + double(b) * b;
}
__declspec(noinline) double rootArgument(float a, float b, float c, bool trace) {
    return trace ? double(a) + 1.0 : ((double(a) - b) - c) + 1.0;
}
__declspec(noinline) double integrationSquare(const float* w) {
    return double(w[2]) * w[2] + (double(w[1]) * w[1] + double(w[0]) * w[0]);
}
#pragma float_control(precise, off, push)
__declspec(noinline) double scalarRoot(float a) { return std::sqrt(double(a)); }
template <int I, int J, int K>
__declspec(noinline) double vectorRoot(const btVector3& v) {
    return std::sqrt(sumSquares<I, J, K>(v));
}
__declspec(noinline) double pairRoot(float a, float b) {
    return std::sqrt(pairSquares(a, b));
}
__declspec(noinline) double quaternionRoot(float a, float b, float c, bool trace) {
    return std::sqrt(rootArgument(a, b, c, trace));
}
__declspec(noinline) double integrationRoot(const float* w) {
    return std::sqrt(integrationSquare(w));
}
#pragma float_control(pop)

} // namespace

#pragma float_control(precise, off, push)
float sinTimes(float angle, float factor) {
    volatile float sine = static_cast<float>(std::sin(static_cast<double>(angle)));
    return sine * factor;
}
float cosine(float angle) {
    return static_cast<float>(std::cos(static_cast<double>(angle)));
}
#pragma float_control(pop)

float dot3(const float* left, const float* right) {
    return static_cast<float>((double(left[0]) * right[0] + double(left[1]) * right[1]) +
                              double(left[2]) * right[2]);
}
float squaredDistance3(const float* left, const float* right) {
    const double dx = double(left[0]) - right[0], dy = double(left[1]) - right[1],
                 dz = double(left[2]) - right[2];
    return static_cast<float>((dx * dx + dy * dy) + dz * dz);
}
void cross3(float* out, const float* left, const float* right) {
    out[0] = static_cast<float>(double(left[1]) * right[2] - double(left[2]) * right[1]);
    out[1] = static_cast<float>(double(left[2]) * right[0] - double(left[0]) * right[2]);
    out[2] = static_cast<float>(double(left[0]) * right[1] - double(left[1]) * right[0]);
}
float edgeComponent(float position, float normal, float amount) {
    return static_cast<float>(double(normal) * amount + position);
}
float pmxEdgeComponent(float position, float normal, float frameScale, float vertexScale,
                       float materialSize) {
    // MMD x86 VA 4AA95E..4AA99C: keep all three products and the sum on
    // x87 until the final float store, using the calling worker's precision.
    return static_cast<float>(((double(normal) * frameScale) * vertexScale) * materialSize +
                              position);
}
float edgeDistanceScale(float distance, float fov, float modelScale) {
    // MMD x86 VA 4B1FEE..4B2020: the constants are float values promoted
    // to double. Store the distance/FOV product to float BEFORE applying
    // the model scale; omitting this store changes the original rounding.
    const double distanceScale = double(0.00004500000068219379f);
    const double fovScale = double(0.6000000238418579f);
    volatile float scale = static_cast<float>((double(distance) * distanceScale) *
                                              (double(fov) * fovScale + 1.0));
    return scale * modelScale;
}
float blendTransformComponent(const float* input, const float* matrix0,
                              const float* matrix1, int component, float weight0,
                              bool position) {
    volatile float weight1 = 1.0f - weight0;
    double transformed1 = (double(input[0]) * matrix1[component] +
                           double(input[1]) * matrix1[component + 4]) +
                          double(input[2]) * matrix1[component + 8];
    if (position)
        transformed1 += matrix1[component + 12];
    double transformed0 = (double(input[0]) * matrix0[component] +
                           double(input[1]) * matrix0[component + 4]) +
                          double(input[2]) * matrix0[component + 8];
    if (position)
        transformed0 += matrix0[component + 12];
    return static_cast<float>(transformed1 * weight1 + transformed0 * weight0);
}
float boneVCoordinate(int numerator, int origin, float length, float scale) {
    return static_cast<float>((double(numerator) / length * 8.0) * scale + origin);
}
void boneMarkerBounds(int x, int y, float scale, float* left, float* top, float* right,
                      float* bottom) {
    const double halfExtent = double(scale) * 9.5;
    volatile float leftEdge = static_cast<float>(x - halfExtent);
    volatile float topEdge = static_cast<float>(y - halfExtent);
    *left = leftEdge;
    *top = topEdge;
    *right = static_cast<float>(leftEdge + double(scale) * 18.0);
    *bottom = static_cast<float>(topEdge + double(scale) * 18.0);
}
void __fastcall rotationBasis(float* basis, const float* quaternion) {
    const double x = quaternion[0], y = quaternion[1], z = quaternion[2],
                 w = quaternion[3];
    volatile float normSquared = static_cast<float>(((y * y + x * x) + z * z) + w * w);
    volatile float scale = static_cast<float>(2.0 / normSquared);
    volatile float xs = static_cast<float>(x * scale), ys = static_cast<float>(y * scale),
                   zs = static_cast<float>(z * scale);
    volatile float xw = static_cast<float>(xs * w), yw = static_cast<float>(ys * w),
                   zw = static_cast<float>(zs * w);
    volatile float xx = static_cast<float>(x * xs), xy = static_cast<float>(x * ys),
                   xz = static_cast<float>(x * zs);
    volatile float yy = static_cast<float>(ys * y), yz = static_cast<float>(zs * y),
                   zz = static_cast<float>(zs * z);
    basis[0] = static_cast<float>(1.0 - (double(zz) + yy));
    basis[1] = static_cast<float>(double(xy) - zw);
    basis[2] = static_cast<float>(double(xz) + yw);
    basis[3] = 0;
    basis[4] = static_cast<float>(double(zw) + xy);
    basis[5] = static_cast<float>(1.0 - (double(zz) + xx));
    basis[6] = static_cast<float>(double(yz) - xw);
    basis[7] = 0;
    basis[8] = static_cast<float>(double(xz) - yw);
    basis[9] = static_cast<float>(double(yz) + xw);
    basis[10] = static_cast<float>(1.0 - (double(xx) + yy));
    basis[11] = 0;
}
void __fastcall transformPoint(float* out, const float* point, const float* matrix) {
    for (int column = 0; column < 3; column++)
        out[column] = static_cast<float>(
            ((double(point[0]) * matrix[column] + double(point[1]) * matrix[column + 4]) +
             double(point[2]) * matrix[column + 8]) +
            matrix[column + 12]);
}

float lengthXYZ(const btVector3& v) { return static_cast<float>(vectorRoot<0, 1, 2>(v)); }
void normalizeDirection(const btVector3& direction, btVector3& out) {
    const double inverseLength = 1.0 / vectorRoot<0, 1, 2>(direction);
    volatile float roundedInverseLength = static_cast<float>(inverseLength);
    out[0] = _mm_cvtss_f32(
        _mm_mul_ss(_mm_set_ss(direction[0]), _mm_set_ss(roundedInverseLength)));
    out[1] = static_cast<float>(inverseLength * direction[1]);
    out[2] = static_cast<float>(inverseLength * direction[2]);
    out[3] = 0;
}
float determinant(const btVector3& a, const btVector3& b, const btVector3& c) {
    return static_cast<float>(
        ((((double(b[0]) * c[1]) * a[2] + (double(c[0]) * b[2]) * a[1]) -
          (double(b[2]) * c[1]) * a[0]) -
         (double(a[1]) * b[0]) * c[2]) +
        (double(c[2]) * b[1]) * a[0] - (double(b[1]) * a[2]) * c[0]);
}
void triangleWeights(const btVector3& a, const btVector3& b, float lengthSquared,
                     float* weights) {
    const double inverseLength = 1.0 / scalarRoot(lengthSquared);
    const double weight0 = vectorRoot<2, 1, 0>(a) * inverseLength;
    const double weight1 = vectorRoot<2, 1, 0>(b) * inverseLength;
    weights[0] = static_cast<float>(weight0);
    weights[1] = static_cast<float>(weight1);
    weights[2] = static_cast<float>(1.0 - (weight1 + weight0));
}
void tetrahedronWeights(float detCBD, float detACD, float detBAD, float volume,
                        float* weights) {
    const double inverseVolume = 1.0 / double(volume),
                 weight0 = double(detCBD) * inverseVolume,
                 weight1 = double(detACD) * inverseVolume,
                 weight2 = double(detBAD) * inverseVolume;
    weights[0] = static_cast<float>(weight0);
    weights[1] = static_cast<float>(weight1);
    weights[2] = static_cast<float>(weight2);
    weights[3] = static_cast<float>(1.0 - ((weight2 + weight1) + weight0));
}
float lengthYZX(const btVector3& v) { return static_cast<float>(vectorRoot<1, 2, 0>(v)); }
float lengthXZY(const btVector3& v) { return static_cast<float>(vectorRoot<0, 2, 1>(v)); }
void normalize(btVector3& vector, float lengthSquared) {
    const double inverseLength = 1.0 / scalarRoot(lengthSquared);
    vector[0] = static_cast<float>(double(vector[0]) * inverseLength);
    vector[1] = static_cast<float>(double(vector[1]) * inverseLength);
    vector[2] = static_cast<float>(double(vector[2]) * inverseLength);
}
void tailNormalize(btVector3& normal, float lengthSquared, float distanceSquared,
                   float* inverseLengthOut, float* distanceOut) {
    const double inverseLength = 1.0 / scalarRoot(lengthSquared);
    *inverseLengthOut = static_cast<float>(inverseLength);
    normal[0] = static_cast<float>(double(normal[0]) * inverseLength);
    normal[1] = static_cast<float>(double(normal[1]) * inverseLength);
    normal[2] = static_cast<float>(double(normal[2]) * inverseLength);
    *distanceOut = static_cast<float>(scalarRoot(distanceSquared));
}
static __declspec(noinline) double distanceSquare(const btVector3& left,
                                                  const btVector3& right) {
    const double dx = double(left[0]) - right[0], dy = double(left[1]) - right[1],
                 dz = double(left[2]) - right[2];
    return (dz * dz + dy * dy) + dx * dx;
}
#pragma float_control(precise, off, push)
float negativeDistance(const btVector3& left, const btVector3& right) {
    return static_cast<float>(-std::sqrt(distanceSquare(left, right)));
}
float distanceMinusMargin(const btVector3& left, const btVector3& right, float margin) {
    return static_cast<float>(std::sqrt(distanceSquare(left, right)) - margin);
}
#pragma float_control(pop)
float capsuleDistance(float lengthSquared, float radiusA, float radiusB) {
    return static_cast<float>((scalarRoot(lengthSquared) - radiusA) - radiusB);
}
void capsuleNormal(btVector3& normal, const btVector3& points, float lengthSquared) {
    const double inverseLength = -(1.0 / scalarRoot(lengthSquared));
    normal[0] = static_cast<float>(double(points[0]) * inverseLength);
    normal[1] = static_cast<float>(double(points[1]) * inverseLength);
    normal[2] = static_cast<float>(double(points[2]) * inverseLength);
}
void planeSpace(btVector3& normal, float a, float b, bool zBranch) {
    const double inverseLength = 1.0 / pairRoot(a, b);
    if (zBranch) {
        normal[2] = static_cast<float>(double(b) * inverseLength);
        normal[1] = static_cast<float>(-(double(a) * inverseLength));
        normal[0] = 0;
    } else {
        normal[1] = static_cast<float>(double(a) * inverseLength);
        normal[0] = static_cast<float>(-(double(b) * inverseLength));
        normal[2] = 0;
    }
}
void normalizeSupport(btVector3& vector) {
    const double inverseLength = 1.0 / vectorRoot<1, 2, 0>(vector);
    vector[0] = static_cast<float>(double(vector[0]) * inverseLength);
    vector[1] = static_cast<float>(double(vector[1]) * inverseLength);
    vector[2] = static_cast<float>(double(vector[2]) * inverseLength);
}
void normalizeSupportLength(btVector3& vector, float lengthSquared) {
    const double inverseLength = 1.0 / scalarRoot(lengthSquared);
    vector[0] = static_cast<float>(double(vector[0]) * inverseLength);
    vector[1] = static_cast<float>(double(vector[1]) * inverseLength);
    vector[2] = static_cast<float>(double(vector[2]) * inverseLength);
}

#pragma float_control(precise, off, push)
static __forceinline double nativeAcos(double a) { return std::acos(a); }
#pragma float_control(pop)
static __declspec(noinline) double quaternionSquare(const float* quaternion) {
    return ((double(quaternion[2]) * quaternion[2] +
             double(quaternion[3]) * quaternion[3]) +
            double(quaternion[1]) * quaternion[1]) +
           double(quaternion[0]) * quaternion[0];
}
#pragma float_control(precise, off, push)
void quaternionAngle(const float* quaternion, float* inverseLengthOut, float* angleOut) {
    const double inverse = 1.0 / std::sqrt(quaternionSquare(quaternion));
    *inverseLengthOut = static_cast<float>(inverse);
    *angleOut = static_cast<float>(nativeAcos(inverse * quaternion[3]) * 2.0);
}

#pragma float_control(pop)
void quaternionRootParts(float diagonalI, float diagonalJ, float diagonalK,
                         bool positiveTrace, float* rootOut, float* halfRootOut) {
    const double root = quaternionRoot(diagonalI, diagonalJ, diagonalK, positiveTrace);
    *rootOut = static_cast<float>(root);
    *halfRootOut = static_cast<float>(root * 0.5);
}
#pragma float_control(precise, off, push)
static __forceinline double nativeSin(double a) { return std::sin(a); }
static __forceinline double nativeCos(double a) { return std::cos(a); }
#pragma float_control(pop)
static inline float multiplyFloat(float a, float b) {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
static inline float divideFloat(float a, float b) {
    return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
static inline float subtractFloat(float a, float b) {
    return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
// Each matrix element retains the original sum order. Floating-point addition
// is not associative, so the mathematically symmetric entries can differ by a bit.
namespace {
float addFloat(float a, float b) {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
template <int I, int J, int K>
float inertiaElement(const btMatrix3x3& basis, const btVector3& local, int row,
                     int column) {
    const float p0 =
        multiplyFloat(basis[column][0], multiplyFloat(basis[row][0], local[0]));
    const float p1 =
        multiplyFloat(basis[column][1], multiplyFloat(basis[row][1], local[1]));
    const float p2 =
        multiplyFloat(basis[column][2], multiplyFloat(basis[row][2], local[2]));
    const float terms[3] = {p0, p1, p2};
    return addFloat(addFloat(terms[I], terms[J]), terms[K]);
}
} // namespace
btMatrix3x3 inverseInertiaTensor(const btMatrix3x3& basis, const btVector3& local) {
    return btMatrix3x3(inertiaElement<2, 0, 1>(basis, local, 0, 0),
                       inertiaElement<2, 1, 0>(basis, local, 0, 1),
                       inertiaElement<2, 0, 1>(basis, local, 0, 2),
                       inertiaElement<1, 2, 0>(basis, local, 1, 0),
                       inertiaElement<1, 2, 0>(basis, local, 1, 1),
                       inertiaElement<2, 1, 0>(basis, local, 1, 2),
                       inertiaElement<1, 2, 0>(basis, local, 2, 0),
                       inertiaElement<1, 2, 0>(basis, local, 2, 1),
                       inertiaElement<2, 1, 0>(basis, local, 2, 2));
}

#pragma float_control(precise, off, push)
void integrateRotation(const float* angularVelocity, float timeStep, float* axis,
                       float* cosineOut) {
    double speed = integrationRoot(angularVelocity);
    volatile float speedFloat = static_cast<float>(speed);
    if (speed * timeStep > double(0.78539819f)) {
        speedFloat = divideFloat(0.78539819f, timeStep);
        speed = speedFloat;
    }
    // The original unordered comparison also selects Taylor for NaN.
    if (!(speedFloat >= 0.001f)) {
        float cubic = multiplyFloat(
            multiplyFloat(
                multiplyFloat(multiplyFloat(multiplyFloat(timeStep, timeStep), timeStep),
                              speedFloat),
                speedFloat),
            0.020833334f);
        float factor = subtractFloat(multiplyFloat(timeStep, 0.5f), cubic);
        axis[0] = multiplyFloat(angularVelocity[0], factor);
        axis[1] = multiplyFloat(angularVelocity[1], factor);
        axis[2] = multiplyFloat(angularVelocity[2], factor);
    } else {
        const double scale = nativeSin((speed * timeStep) * 0.5) / speed;
        axis[0] = static_cast<float>(angularVelocity[0] * scale);
        axis[1] = static_cast<float>(angularVelocity[1] * scale);
        axis[2] = static_cast<float>(angularVelocity[2] * scale);
    }
    *cosineOut = static_cast<float>(nativeCos((double(timeStep) * speed) * 0.5));
}

#pragma float_control(pop)
void gravityNoise(int rStrength, int rX, int rY, int rZ, int noise, float magnitude,
                  const float* direction, float* gravity) {
    volatile float strength =
        static_cast<float>(((double(rStrength) * 10.0) / 32767.0) * noise + magnitude);
    const int random[3] = {rX, rY, rZ};
    for (int i = 0; i < 3; i++)
        gravity[i] = static_cast<float>(
            (((double(random[i]) * 0.20000000298023224) / 32767.0 - 0.10000000149011612) +
             direction[i]) *
            strength);
}

} // namespace btMmdMath
#endif
