#pragma once
#include <array>

// CPU model space is left-handed, with row vectors and row-major matrices.
// p' = p * M; local * parent composes the hierarchy; translation is [12..14].
// Quaternion components are XYZW. GPU packing is a separate contract.
namespace mikudancestudio::cpu_math {
using Vec3 = std::array<float, 3>;
using Quaternion = std::array<float, 4>;
using Matrix = std::array<float, 16>;
Matrix Identity();
Matrix Translation(const Vec3&);
Matrix Multiply(const Matrix& a, const Matrix& b);
Matrix Transpose(const Matrix&);
// Failure leaves output unchanged (non-finite, numerically singular, overflow,
// or a float inverse with either identity-product residual exceeding 2e-5).
bool Inverse(const Matrix&, Matrix& output);
bool Normalize(const Vec3&, Vec3& output);
bool Normalize(const Quaternion&, Quaternion& output);
Matrix RotationQuaternion(const Quaternion&); // Like D3DX, does not normalize.
// Hamilton product a*b; deliberately distinct from D3DX's reversed arguments.
Quaternion HamiltonProduct(const Quaternion& a, const Quaternion& b);
Vec3 TransformPoint(const Vec3&, const Matrix&); // affine, no perspective divide
Vec3 TransformDirection(const Vec3&, const Matrix&); // no translation/normalization
// MMD's pivot sandwich: T(-rest) * R * T(offset) * T(rest).
Matrix BoneLocal(const Vec3& rest, const Vec3& offset, const Quaternion& rotation);
}
