#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/cpu_deform.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mikudancestudio::cpu_math {
Matrix Identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }
Matrix Translation(const Vec3& p) {
    auto m = Identity();
    std::copy(p.begin(), p.end(), m.begin() + 12);
    return m;
}
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix m{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m[4*r+c] = ((a[4*r] * b[c] + a[4*r+1] * b[4+c]) +
                        a[4*r+2] * b[8+c]) + a[4*r+3] * b[12+c];
    return m;
}
Matrix Transpose(const Matrix& a) {
    Matrix m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m[4*r+c] = a[4*c+r];
    return m;
}
bool Inverse(const Matrix& a, Matrix& output) {
    // Scaled partial-pivot Gauss-Jordan in double: a checked general inverse, not a
    // bit-exact replacement for the legacy D3DX inverse adapter.
    double work[4][8]{};
    double scale[4]{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(a[4*r+c])) return false;
            work[r][c] = a[4*r+c];
            scale[r] = std::max(scale[r], std::abs(work[r][c]));
        }
        if (scale[r] == 0) return false;
        work[r][4+r] = 1;
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c+1; r < 4; ++r)
            if (std::abs(work[r][c])/scale[r] > std::abs(work[pivot][c])/scale[pivot]) pivot = r;
        // Elimination of exactly dependent nonzero rows need not leave an
        // exact zero. A relative threshold preserves uniformly tiny matrices.
        if (std::abs(work[pivot][c])/scale[pivot] <= 64*std::numeric_limits<double>::epsilon()) return false;
        for (int n = 0; n < 8; ++n) std::swap(work[c][n], work[pivot][n]);
        std::swap(scale[c], scale[pivot]);
        const double divisor = work[c][c];
        for (double& value : work[c]) value /= divisor;
        for (int r = 0; r < 4; ++r) if (r != c) {
            const double factor = work[r][c];
            for (int n = 0; n < 8; ++n) work[r][n] -= factor * work[c][n];
        }
    }
    Matrix result;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        const double value = work[r][4+c];
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
        result[4*r+c] = static_cast<float>(value);
    }
    // Check the published float inverse, in both directions. A finite double
    // inverse can become unusable when stored as float, especially near a
    // singular matrix. Do not publish it or modify aliased input on failure.
    for (int r=0; r<4; ++r) for (int c=0; c<4; ++c) {
        double left=0, right=0;
        for (int k=0; k<4; ++k) {
            left += double(a[4*r+k])*result[4*k+c];
            right += double(result[4*r+k])*a[4*k+c];
        }
        const double expected=r==c?1:0;
        if (std::abs(left-expected)>2e-5 || std::abs(right-expected)>2e-5) return false;
    }
    output = result;
    return true;
}
template <std::size_t N> bool Normalized(const std::array<float,N>& in, std::array<float,N>& out) {
    double squared = 0;
    for (float v : in) { if (!std::isfinite(v)) return false; squared += double(v)*v; }
    if (squared == 0) return false;
    const double length = std::sqrt(squared);
    std::array<float,N> result;
    for (std::size_t n = 0; n < N; ++n) result[n] = static_cast<float>(in[n]/length);
    out = result;
    return true;
}
bool Normalize(const Vec3& in, Vec3& out) { return Normalized(in,out); }
bool Normalize(const Quaternion& in, Quaternion& out) { return Normalized(in,out); }
Matrix RotationQuaternion(const Quaternion& q) {
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    return {1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
            2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
            2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0,0,0,1};
}
Quaternion HamiltonProduct(const Quaternion& a, const Quaternion& b) {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
Vec3 TransformPoint(const Vec3& v, const Matrix& m) {
    Vec3 out; cpu_deform::TransformPosition(out.data(),v.data(),m.data()); return out;
}
Vec3 TransformDirection(const Vec3& v, const Matrix& m) {
    Vec3 out; cpu_deform::TransformNormal(out.data(),v.data(),m.data()); return out;
}
Matrix BoneLocal(const Vec3& rest, const Vec3& offset, const Quaternion& q) {
    return Multiply(Multiply(Multiply(Translation({-rest[0],-rest[1],-rest[2]}),
                                      RotationQuaternion(q)), Translation(offset)), Translation(rest));
}
}
