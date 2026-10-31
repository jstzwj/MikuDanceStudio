#include "physics_tolerance.hpp"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

physics_test::Pose Rotated(double degrees) {
    physics_test::Pose pose;
    const double half = degrees * physics_test::kPi / 360;
    pose.rotation = {0, 0, std::sin(half), std::cos(half)};
    return pose;
}
} // namespace

int main() {
    using namespace physics_test;
    try {
        const Pose identity;
        const PoseTolerance limits{1, 0.1};
        Pose translated;
        translated.position = {0.6, 0.8, 0};
        Require(Difference(identity, translated).Within(limits), "position boundary rejected");
        translated.position[2] = 0.01;
        Require(!Difference(identity, translated).Within(limits),
                "Euclidean position error above the limit accepted");
        translated.position = {0.8, 0.8, 0};
        Require(!Difference(identity, translated).Within(limits),
                "small per-component errors hid excessive displacement");

        Require(Difference(identity, Rotated(0.099)).Within(limits), "small rotation rejected");
        Require(!Difference(identity, Rotated(0.101)).Within(limits),
                "rotation above the limit accepted");
        Require(Difference(Rotated(179.98), Rotated(-179.98)).Within(limits),
                "rotation wrap used the long arc");
        auto opposite = Rotated(73);
        for (auto& value : opposite.rotation)
            value *= -3;
        Require(Difference(Rotated(73), opposite).rotationDegrees < 1e-12,
                "quaternion sign or scale changed the orientation");

        const auto quarter = FromMatrix({0, -1, 0, 1, 0, 0, 0, 0, 1}, {0, 0, 0});
        const auto half = FromMatrix({-1, 0, 0, 0, -1, 0, 0, 0, 1}, {0, 0, 0});
        Require(Difference(quarter, Rotated(90)).rotationDegrees < 1e-12,
                "rotation matrix convention differs");
        Require(Difference(half, Rotated(180)).rotationDegrees < 1e-12,
                "half-turn matrix extraction failed");
        Pose axisHalf;
        axisHalf.rotation = {1, 0, 0, 0};
        Require(
            Difference(FromMatrix({1, 0, 0, 0, -1, 0, 0, 0, -1}, {}), axisHalf).rotationDegrees <
                1e-12,
            "X-axis half-turn extraction failed");
        axisHalf.rotation = {0, 1, 0, 0};
        Require(
            Difference(FromMatrix({-1, 0, 0, 0, 1, 0, 0, 0, -1}, {}), axisHalf).rotationDegrees <
                1e-12,
            "Y-axis half-turn extraction failed");
        Require(!Difference(identity, FromMatrix({}, {})).Within(limits),
                "singular rotation matrix accepted");
        Require(!Difference(identity, FromMatrix({-1, 0, 0, 0, 1, 0, 0, 0, 1}, {})).Within(limits),
                "reflection accepted as a rotation");

        Pose invalid;
        invalid.position[0] = std::numeric_limits<double>::infinity();
        Require(!Difference(identity, invalid).Within(limits), "infinite position accepted");
        invalid = identity;
        invalid.rotation[0] = std::numeric_limits<double>::quiet_NaN();
        Require(!Difference(identity, invalid).Within(limits), "NaN rotation accepted");
        invalid.rotation = {};
        Require(!Difference(identity, invalid).Within(limits), "zero quaternion accepted");
        Require(!PoseTolerance{-1, 0.1}.Valid() && !PoseTolerance{1, 181}.Valid() &&
                    !PoseTolerance{1, std::numeric_limits<double>::infinity()}.Valid(),
                "invalid tolerance accepted");
        std::puts("Physics pose thresholds: distance, angle, wrap, sign and invalid values passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Physics tolerance: %s\n", error.what());
        return 1;
    }
}
