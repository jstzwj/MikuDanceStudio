#pragma once
#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/cpu_bone_types.hpp"
#include "mikudancestudio/pmm_document.hpp"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#endif
class btRigidBody;

namespace mikudancestudio::cpu_model {
// Algorithm state with natural C++ ownership. No ABI offsets, padding, GPU
// pointers, UI selection state or original executable record is retained.
struct Bone {
    float position[3]{}, physicsOffset[3]{}, physicsQuat[4]{0, 0, 0, 1};
    float trans[3]{}, rotQuat[4]{0, 0, 0, 1}, ikBackup[7]{0, 0, 0, 0, 0, 0, 1};
    float rotQuat2[4]{0, 0, 0, 1}, axis[3]{}, ikLimitMin[3]{}, ikLimitMax[3]{};
    float matInit[16]{}, matLocal[16]{}, matWorld[16]{}, matExtra[16]{};
    float inheritRatio = 0;
    float ikWorkingPos[3]{}, ikWorkingQuat[4]{0, 0, 0, 1};
    std::int32_t rigidIdx = -1;
    int parent = -1, tailBone = -1, tailIdx = -1, layer = 0, slotIndex = -1;
    cpu_bone::BoneType type = cpu_bone::BoneType::RotateMove;
    std::uint16_t flags = 0;
    bool hasFlag = false, hasRigidBody = false, physicsDisabled = false, twistEnable = false;
    char name[20]{}; // raw CP932 knee identifiers; UTF-8 display names live in the resource
    Bone() {
        const auto id = cpu_math::Identity();
        for (auto* matrix : {matInit, matLocal, matWorld, matExtra})
            std::memcpy(matrix, id.data(), 64);
    }
};
struct IkChain {
    int boneIndex = -1, targetBone = -1;
    std::vector<std::uint16_t> storage;
    const std::uint16_t* links = nullptr;
    int iterations = 0, linkCount = 0;
    bool enabled = true;
    float maxAngle = 0;
};
struct Binding {
    int linkedModel = -1, linkedBone = 0;
    std::uint32_t windowStart = 0, windowEnd = 0;
};
struct BoneKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    unsigned char interpolation[16]{};
    float position[3]{}, rotation[4]{0, 0, 0, 1};
    unsigned char physicsDisabled = 0;
};
struct MorphKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    float value = 0;
};
struct Morph {
    float value = 0;
};
struct DisplayKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    unsigned char visible = 1;
    std::vector<unsigned char> ik;
    std::vector<format::PmmRigidState> selectors;
};
struct Rigid {
    int boneIndex = -1;
    unsigned char mode = 0, staticFlag = 0, kinematicFlag = 0;
    float position[3]{}, rotation[3]{}, invTransform[16]{};
    btRigidBody* body = nullptr;
};
struct Joint {
    int rigidA = -1, rigidB = -1;
    float radiusBound = 0;
};
struct Model {
    Bone* boneTable = nullptr;
    IkChain* ikChains = nullptr;
    Binding* boneOrderTable = nullptr;
    int boneCount = 0, ikChainCount = 0, physicsMode = 2;
    const std::vector<std::size_t>* order = nullptr;
    Rigid* rigidTable = nullptr;
    Joint* jointTable = nullptr;
    int rigidCount = 0, jointCount = 0, centerBone = 0;
    int boneOrderCount = 0, morphCount = 0;
    BoneKey* boneKeys = nullptr;
    MorphKey* morphKeys = nullptr;
    Morph* morphs = nullptr;
    DisplayKey* displayKeys = nullptr;
    unsigned char *boneTrackActive = nullptr, *morphTrackActive = nullptr;
    std::uint32_t *boneKeyCursors = nullptr, *morphKeyCursors = nullptr, displayKeyCursor = 0;
    unsigned char loadComplete = 1, displayTrackActive = 0;
};
struct KeyframeTraits {
    using Model = cpu_model::Model;
    using Bone = cpu_model::Bone;
    using BoneKey = cpu_model::BoneKey;
    using Morph = cpu_model::Morph;
    using MorphKey = cpu_model::MorphKey;
    using DisplayKey = cpu_model::DisplayKey;
    using IkChain = cpu_model::IkChain;
    using Binding = cpu_model::Binding;
    using Rigid = cpu_model::Rigid;
    static auto IkStates(const DisplayKey& key) { return key.ik.data(); }
    static auto SelectorStates(const DisplayKey& key) { return key.selectors.data(); }
};
// Raw matrix view used only at the algorithm boundary. Storage remains four
// contiguous rows and is copied explicitly into the public math value type.
struct Matrix {
    float m[4][4];
};
struct Math {
    static cpu_math::Matrix Load(const Matrix* m) {
        cpu_math::Matrix value;
        std::memcpy(value.data(), m, 64);
        return value;
    }
    static void Store(Matrix* m, const cpu_math::Matrix& value) {
        std::memcpy(m, value.data(), 64);
    }
    bool inverse(Matrix* out, const Matrix* in) {
        cpu_math::Matrix value;
        if (!cpu_math::Inverse(Load(in), value))
            return false;
        Store(out, value);
        return true;
    }
    void rotX(Matrix* m, float v) { Store(m, cpu_math::RotationX(v)); }
    void rotY(Matrix* m, float v) { Store(m, cpu_math::RotationY(v)); }
    void rotZ(Matrix* m, float v) { Store(m, cpu_math::RotationZ(v)); }
    void mul(Matrix* out, const Matrix* a, const Matrix* b) {
        Store(out, cpu_math::Multiply(Load(a), Load(b)));
    }
    void translation(Matrix* m, float x, float y, float z) {
        Store(m, cpu_math::Translation({x, y, z}));
    }
    void quatFromMatrix(float* out, const Matrix* m) {
        const auto q = cpu_math::QuaternionFromMatrix(Load(m));
        std::memcpy(out, q.data(), 16);
    }
    void matrixRotationQuaternion(Matrix* m, const float* q) {
        Store(m, cpu_math::RotationQuaternion({q[0], q[1], q[2], q[3]}));
    }
    void quatMultiply(
        float* out, const float* a,
        const float* b) { // b*a, retaining the pair grouping used by the legacy SSE kernel.
        const float q[]{(b[3] * a[0] + b[0] * a[3]) + (b[1] * a[2] - b[2] * a[1]),
                        (b[3] * a[1] - b[0] * a[2]) + (b[1] * a[3] + b[2] * a[0]),
                        (b[3] * a[2] + b[0] * a[1]) - (b[1] * a[0] - b[2] * a[3]),
                        (b[3] * a[3] - b[0] * a[0]) - (b[1] * a[1] + b[2] * a[2])};
        std::memcpy(out, q, 16);
    }
    void normalize(float* out, const float* in) {
        const float squared = in[0] * in[0] + in[1] * in[1] + in[2] * in[2];
        float inverse = 0;
        if (squared > 0) {
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
            // The legacy CPU runtime uses the SSE reciprocal-root estimate
            // with one Newton step. Preserve this rounding on SSE machines;
            // other CPUs use the scalar path under the scene numeric budget.
            const float estimate = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(squared)));
            inverse = (3.f - (estimate * squared) * estimate) * (estimate * .5f);
#else
            inverse = 1.f / std::sqrt(squared);
#endif
        }
        const float v[]{in[0] * inverse, in[1] * inverse, in[2] * inverse};
        std::memcpy(out, v, 12);
    }
    static void transformPoint(float* out, const float* in, const float* m) {
        for (int c = 0; c < 3; ++c)
            out[c] = float(double(in[0]) * m[c] + double(in[1]) * m[c + 4] +
                           double(in[2]) * m[c + 8] + m[c + 12]);
    }
    static float dot3(const float* a, const float* b) {
        return float(double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2]);
    }
    static float squaredDistance3(const float* a, const float* b) {
        const double x = double(a[0]) - b[0], y = double(a[1]) - b[1], z = double(a[2]) - b[2];
        return float(y * y + x * x + z * z);
    }
    static void cross3(float* out, const float* a, const float* b) {
        const float v[]{float(double(a[1]) * b[2] - double(a[2]) * b[1]),
                        float(double(a[2]) * b[0] - double(a[0]) * b[2]),
                        float(double(a[0]) * b[1] - double(a[1]) * b[0])};
        std::memcpy(out, v, 12);
    }
};
struct Traits {
    using Bone = cpu_model::Bone;
    using Model = cpu_model::Model;
    using IkChain = cpu_model::IkChain;
    using Binding = cpu_model::Binding;
    using Math = cpu_model::Math;
    using Matrix = cpu_model::Matrix;
    static int Index(const Model& model, int index) {
        return model.order ? int((*model.order)[index]) : index;
    }
    static const Bone& Parent(const Bone* bones, int index) {
        static const Bone root;
        return index < 0 ? root : bones[index];
    }
    template <class... T> static void Probe(T&&...) {}
    template <class... T> static void Precheck(T&&...) {}
};
}
