// Regression values captured from d7425a5's original numeric implementations.
// The fixture includes all output bits (including signed zero and NaN payloads).
#include "LinearMath/btMmdMath.h"
#include "mmd_math_golden.hpp"
#include "mmd_math_x87_reference.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <initializer_list>
#include <intrin.h>
namespace math = btMmdMath;
namespace {
constexpr const char* operations[] = {"sinTimes",
                                      "cosine",
                                      "dot3",
                                      "squaredDistance3",
                                      "cross3",
                                      "edgeComponent",
                                      "blendTransformComponent",
                                      "boneVCoordinate",
                                      "boneMarkerBounds",
                                      "rotationBasis",
                                      "transformPoint",
                                      "lengthXYZ",
                                      "normalizeDirection",
                                      "determinant",
                                      "triangleWeights",
                                      "tetrahedronWeights",
                                      "lengthYZX",
                                      "lengthXZY",
                                      "normalize",
                                      "tailNormalize",
                                      "negativeDistance",
                                      "distanceMinusMargin",
                                      "capsuleDistance",
                                      "capsuleNormal",
                                      "planeSpace",
                                      "normalizeSupport",
                                      "normalizeSupportLength",
                                      "integrateRotation",
                                      "quaternionAngle",
                                      "quaternionRootParts",
                                      "gravityNoise",
                                      "inverseInertiaTensor"};
uint32_t state = 0x191234;
uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
uint32_t bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
float value(int domain) {
    uint32_t u = next();
    if (domain == 2) {
        const uint32_t edge[] = {0,          0x80000000, 1,          0x80000001, 0x007fffff,
                                 0x00800000, 0x3f800000, 0x3f7fffff, 0x3f800001, 0xbf800000,
                                 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc12345,
                                 0xffc23456, 0x7f812345};
        u = edge[u % 17];
    } else if (domain) {
        u = (u & 0x807fffff) | ((100 + (u % 50)) << 23);
    } else if ((u & 0x7f800000) == 0x7f800000)
        u ^= 0x00800000;
    float f;
    memcpy(&f, &u, 4);
    return f;
}

std::array<uint64_t, 32> evaluate(std::array<uint64_t, 32>& expected) {
    std::array<uint64_t, 32> hashes;
    hashes.fill(14695981039346656037ULL);
    auto record = [&](int operation, float value) {
        hashes[operation] ^= bits(value);
        hashes[operation] *= 1099511628211ULL;
    };
    // Only the three operations using FSIN/FCOS need a same-CPU historical
    // oracle. Keep the other 29 operations tied to the original golden hashes.
    for (int op : {0, 1, 27})
        expected[op] = 14695981039346656037ULL;
    auto reference = [&](int operation, float value) {
        expected[operation] ^= bits(value);
        expected[operation] *= 1099511628211ULL;
    };
    state = 0x191234;
    for (int domain : {0, 1, 2})
        for (int i = 0; i < 512; i++) {
            float a[16], b[16], p[3];
            for (auto& x : a)
                x = value(domain);
            for (auto& x : b)
                x = value(domain);
            for (auto& x : p)
                x = value(domain);
            float o[12];

            record(0, math::sinTimes(a[0], a[1]));
            record(1, math::cosine(a[0]));
            reference(0, mmd_math_x87_reference::sinTimes(a[0], a[1]));
            reference(1, mmd_math_x87_reference::cosine(a[0]));
            record(2, math::dot3(a, b));
            record(3, math::squaredDistance3(a, b));
            math::cross3(o, a, b);
            for (int j = 0; j < 3; j++)
                record(4, o[j]);
            record(5, math::edgeComponent(a[0], a[1], a[2]));
            record(6, math::blendTransformComponent(p, a, b, i % 3, 0.37f, i % 2));
            record(7, math::boneVCoordinate(1234, -567, a[0], a[1]));
            math::boneMarkerBounds(1234, -567, a[0], o, o + 1, o + 2, o + 3);
            for (int j = 0; j < 4; j++)
                record(8, o[j]);
            math::rotationBasis(o, a);
            for (int j = 0; j < 12; j++)
                record(9, o[j]);
            math::transformPoint(o, p, a);
            for (int j = 0; j < 3; j++)
                record(10, o[j]);
            btVector3 va(a[0], a[1], a[2]), vb(b[0], b[1], b[2]), vc(p[0], p[1], p[2]), vo = va;
            record(11, math::lengthXYZ(va));
            math::normalizeDirection(va, vo);
            for (int j = 0; j < 3; j++)
                record(12, vo[j]);
            record(13, math::determinant(va, vb, vc));
            const float l = std::abs(a[3]) + 0.1f;
            math::triangleWeights(va, vb, l, o);
            for (int j = 0; j < 3; j++)
                record(14, o[j]);
            math::tetrahedronWeights(a[0], a[1], a[2], l, o);
            for (int j = 0; j < 4; j++)
                record(15, o[j]);
            record(16, math::lengthYZX(va));
            record(17, math::lengthXZY(va));
            vo = va;
            math::normalize(vo, l);
            for (int j = 0; j < 3; j++)
                record(18, vo[j]);
            vo = va;
            math::tailNormalize(vo, l, l + 0.1f, o, o + 1);
            for (int j = 0; j < 3; j++)
                record(19, vo[j]);
            record(19, o[0]);
            record(19, o[1]);
            record(20, math::negativeDistance(va, vb));
            record(21, math::distanceMinusMargin(va, vb, a[3]));
            record(22, math::capsuleDistance(l, a[0], a[1]));
            math::capsuleNormal(vo, va, l);
            for (int j = 0; j < 3; j++)
                record(23, vo[j]);
            math::planeSpace(vo, a[0], a[1], i % 2);
            for (int j = 0; j < 3; j++)
                record(24, vo[j]);
            vo = va;
            math::normalizeSupport(vo);
            for (int j = 0; j < 3; j++)
                record(25, vo[j]);
            vo = va;
            math::normalizeSupportLength(vo, l);
            for (int j = 0; j < 3; j++)
                record(26, vo[j]);
            math::integrateRotation(a, .0166666675f, o, o + 3);
            for (int j = 0; j < 4; j++)
                record(27, o[j]);
            mmd_math_x87_reference::integrateRotation(a, .0166666675f, o, o + 3);
            for (int j = 0; j < 4; j++)
                reference(27, o[j]);
            math::quaternionAngle(a, o, o + 1);
            for (int j = 0; j < 2; j++)
                record(28, o[j]);
            math::quaternionRootParts(a[0], a[1], a[2], i % 2, o, o + 1);
            for (int j = 0; j < 2; j++)
                record(29, o[j]);

            math::gravityNoise(i % 32768, (i + 1) % 32768, (i + 2) % 32768, (i + 3) % 32768,
                               i % 101, a[0], b, o);
            for (int j = 0; j < 3; j++)
                record(30, o[j]);

            const btMatrix3x3 basis(a[0], a[1], a[2], a[4], a[5], a[6], a[8], a[9], a[10]);
            const btVector3 local(b[0], b[1], b[2]);
            const btMatrix3x3 inertia = math::inverseInertiaTensor(basis, local);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++)
                    record(31, inertia[r][c]);
        }
    return hashes;
}
int checkOutlineMath(int mode) {
    // Finite inputs include subnormals, signed zeros and values whose
    // intermediate float store overflows. The instruction oracle is frozen
    // separately; no expected bits are generated by the replacement helpers.
    constexpr uint32_t boundaries[] = {0,          0x80000000, 1,          0x80000001,
                                       0x007fffff, 0x00800000, 0x3f800000, 0x3f7fffff,
                                       0x3f800001, 0xbf800000, 0x7f7fffff, 0xff7fffff};
    uint32_t sampleState = 0x93224;
    const auto input = [&](int sample, int component) {
        sampleState = sampleState * 1664525u + 1013904223u;
        uint32_t raw = sample < 12
                           ? boundaries[(sample + component) % 12]
                           : (sampleState & 0x807fffff) | ((1 + (sampleState >> 24) % 254) << 23);
        float result;
        std::memcpy(&result, &raw, sizeof result);
        return result;
    };
    unsigned mismatches[2]{};
    const auto compare = [&](int operation, int sample, float actual, float expected) {
        if (bits(actual) != bits(expected)) {
            if (mismatches[operation]++ == 0)
                std::fprintf(stderr,
                             "MMD outline: mode=%d operation=%s sample=%d got=%08x expected=%08x\n",
                             mode, operation ? "edgeDistanceScale" : "pmxEdgeComponent", sample,
                             bits(actual), bits(expected));
        }
    };
    for (int i = 0; i < 100000; ++i) {
        const float position = input(i, 0), normal = input(i, 1), frameScale = input(i, 2),
                    vertexScale = input(i, 3), materialSize = input(i, 4);
        const float distance = input(i, 5), fov = input(i, 6);
        compare(0, i,
                math::pmxEdgeComponent(position, normal, frameScale, vertexScale, materialSize),
                mmd_math_x87_reference::pmxEdgeComponent(position, normal, frameScale, vertexScale,
                                                         materialSize));
        compare(1, i, math::edgeDistanceScale(distance, fov, materialSize),
                mmd_math_x87_reference::edgeDistanceScale(distance, fov, materialSize));
    }
    return (mismatches[0] != 0) + (mismatches[1] != 0);
}
} // namespace
int main() {
    int cpu[4];
    char vendor[13] = {};
    __cpuid(cpu, 0);
    memcpy(vendor, &cpu[1], 4);
    memcpy(vendor + 4, &cpu[3], 4);
    memcpy(vendor + 8, &cpu[2], 4);
    std::printf("MMD numeric parity CPU: %s\n", vendor);
    unsigned saved;
    _controlfp_s(&saved, 0, 0);
    int mode = 0;
    int failures = 0;
    int cpuDependentHashes = 0;
    for (unsigned rounding : {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP}) {
        for (unsigned precision : {_PC_24, _PC_53, _PC_64}) {
            unsigned current;
            _controlfp_s(&current, rounding | precision, _MCW_RC | _MCW_PC);
            std::array<uint64_t, 32> expected;
            std::copy(std::begin(mmd_math_golden::hashes[mode]),
                      std::end(mmd_math_golden::hashes[mode]), expected.begin());
            const auto hashes = evaluate(expected);
            for (int op : {0, 1, 27})
                if (expected[op] != mmd_math_golden::hashes[mode][op])
                    cpuDependentHashes++;
            for (size_t op = 0; op < hashes.size(); op++)
                if (hashes[op] != expected[op]) {
                    std::fprintf(stderr, "MMD numeric parity: mode %d %s: %016llx != %016llx\n",
                                 mode, operations[op], hashes[op], expected[op]);
                    failures++;
                }
            failures += checkOutlineMath(mode);
            mode++;
        }
    }
    unsigned restored;
    _controlfp_s(&restored, saved, _MCW_RC | _MCW_PC);
    std::printf("MMD numeric parity: %d CPU-dependent historical trig hashes; "
                "29 fixed and 3 same-CPU reference operations, 12 modes\n",
                cpuDependentHashes);
    std::puts("MMD outline: 2 instruction-reference operations, 100000 inputs per mode, 12 modes");
    return failures ? 1 : 0;
}
