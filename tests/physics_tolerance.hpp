#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace physics_test {
constexpr double kPi = 3.14159265358979323846;

struct Pose {
    std::array<double, 3> position{};
    std::array<double, 4> rotation{0, 0, 0, 1}; // x, y, z, w
};

struct PoseTolerance {
    double position = 0.01; // Euclidean distance in model units.
    double rotationDegrees = 0.1;

    bool Valid() const {
        return std::isfinite(position) && position >= 0 && std::isfinite(rotationDegrees) &&
               rotationDegrees >= 0 && rotationDegrees <= 180;
    }
};

struct PoseDifference {
    double position = 0, rotationDegrees = 0;
    bool finite = false;

    bool Within(const PoseTolerance& tolerance) const {
        return finite && tolerance.Valid() && position <= tolerance.position &&
               rotationDegrees <= tolerance.rotationDegrees;
    }
};

inline PoseDifference Difference(const Pose& a, const Pose& b) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(a.position[i]) || !std::isfinite(b.position[i]))
            return {};
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(a.rotation[i]) || !std::isfinite(b.rotation[i]))
            return {};
    const auto norm = [](const std::array<double, 4>& q) {
        return std::hypot(std::hypot(q[0], q[1]), std::hypot(q[2], q[3]));
    };
    const double lengthA = norm(a.rotation), lengthB = norm(b.rotation);
    if (!(lengthA > 0) || !(lengthB > 0) || !std::isfinite(lengthA) || !std::isfinite(lengthB))
        return {};
    std::array<double, 4> qa{}, qb{}, minus{}, plus{};
    double dot = 0;
    for (int i = 0; i < 4; ++i) {
        qa[i] = a.rotation[i] / lengthA;
        qb[i] = b.rotation[i] / lengthB;
        dot += qa[i] * qb[i];
    }
    for (int i = 0; i < 4; ++i) {
        if (dot < 0)
            qb[i] = -qb[i]; // q and -q encode the same orientation.
        minus[i] = qa[i] - qb[i];
        plus[i] = qa[i] + qb[i];
    }
    PoseDifference result;
    result.position = std::hypot(a.position[0] - b.position[0], a.position[1] - b.position[1],
                                 a.position[2] - b.position[2]);
    // Chord lengths retain precision near zero; acos(dot) loses small angles.
    result.rotationDegrees = 4 * std::atan2(norm(minus), norm(plus)) * 180 / kPi;
    result.finite = std::isfinite(result.position) && std::isfinite(result.rotationDegrees);
    return result;
}

// Row-major rotation matrix for column vectors. Use double for the measurement
// so float quaternion extraction does not become the comparison's error floor.
inline Pose FromMatrix(const std::array<double, 9>& matrix, const std::array<double, 3>& position) {
    Pose result;
    result.position = position;
    auto& q = result.rotation;
    const double determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
                               matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
                               matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    if (!(determinant > 0) || !std::isfinite(determinant)) {
        q = {};
        return result;
    }
    const double trace = matrix[0] + matrix[4] + matrix[8];
    if (trace > 0) {
        const double s = 2 * std::sqrt(trace + 1);
        q = {(matrix[7] - matrix[5]) / s, (matrix[2] - matrix[6]) / s, (matrix[3] - matrix[1]) / s,
             s / 4};
    } else {
        int i = matrix[4] > matrix[0] ? 1 : 0;
        if (matrix[8] > matrix[i * 3 + i])
            i = 2;
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        const double s = 2 * std::sqrt((std::max)(0.0, 1 + matrix[i * 3 + i] - matrix[j * 3 + j] -
                                                           matrix[k * 3 + k]));
        if (!(s > 0)) {
            q = {};
            return result;
        }
        q[i] = s / 4;
        q[j] = (matrix[j * 3 + i] + matrix[i * 3 + j]) / s;
        q[k] = (matrix[k * 3 + i] + matrix[i * 3 + k]) / s;
        q[3] = (matrix[k * 3 + j] - matrix[j * 3 + k]) / s;
    }
    return result;
}
} // namespace physics_test
