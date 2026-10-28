#pragma once

#include <cstddef>
#include <cstring>

namespace mikudancestudio::mdl {

// Names describe the tracked sensor frame; mirroring into model space is
// performed when applying the standard pose, not when collecting samples.
enum class TrackedJoint : std::size_t {
    Center,
    Torso,
    Neck,
    Head,
    RightShoulder,
    RightElbow,
    RightWrist,
    RightHand,
    LeftShoulder,
    LeftElbow,
    LeftWrist,
    LeftHand,
    RightHip,
    RightKnee,
    RightAnkle,
    RightFoot,
    LeftHip,
    LeftKnee,
    LeftAnkle,
    LeftFoot,
    RightClavicle,
    LeftClavicle,
    HeadDirection,
    Count
};
inline constexpr std::size_t kTrackedJointCount = static_cast<std::size_t>(TrackedJoint::Count);
inline constexpr std::size_t kSkeletonHistoryLength = 30;
inline constexpr float kInvalidJointY = -999.0f;
inline constexpr std::size_t kPoseTraceCapacity = 12600;

// Pose conversion follows the reference's architecture-specific intermediates.
#if defined(_M_X64)
using SkeletonScalar = float;
#else
using SkeletonScalar = double;
#endif

// Trace storage uses WXYZ; BoneRecord/BoneKey and D3DX use XYZW.
struct PoseTraceQuaternion {
    float w, x, y, z;
    void Store(const float xyzw[4]) {
        w = xyzw[3];
        x = xyzw[0];
        y = xyzw[1];
        z = xyzw[2];
    }
    void Load(float xyzw[4]) const {
        xyzw[0] = x;
        xyzw[1] = y;
        xyzw[2] = z;
        xyzw[3] = w;
    }
};

struct PoseTraceIkPose {
    float position[3];
    PoseTraceQuaternion rotation;
};

// MMD 9.32: 77 floats per sample on both architectures. Missing model bones
// leave their channels unwritten; registration probes the same bone names.
struct PoseTraceSample {
    float center[3];
    PoseTraceQuaternion upperBody, neck;
    PoseTraceQuaternion leftArm, leftElbow, rightArm, rightElbow;
    PoseTraceQuaternion lowerBody, leftLeg, rightLeg, leftKnee, rightKnee;
    PoseTraceIkPose leftLegIk, rightLegIk;
    PoseTraceQuaternion leftWrist, rightWrist, leftShoulder, rightShoulder;
};

static_assert(sizeof(PoseTraceQuaternion) == 16);
static_assert(sizeof(PoseTraceIkPose) == 28);
static_assert(sizeof(PoseTraceSample) == 308);
static_assert(offsetof(PoseTraceSample, upperBody) == 12);
static_assert(offsetof(PoseTraceSample, neck) == 28);
static_assert(offsetof(PoseTraceSample, leftArm) == 44);
static_assert(offsetof(PoseTraceSample, leftElbow) == 60);
static_assert(offsetof(PoseTraceSample, rightArm) == 76);
static_assert(offsetof(PoseTraceSample, rightElbow) == 92);
static_assert(offsetof(PoseTraceSample, lowerBody) == 108);
static_assert(offsetof(PoseTraceSample, leftLeg) == 124);
static_assert(offsetof(PoseTraceSample, rightLeg) == 140);
static_assert(offsetof(PoseTraceSample, leftKnee) == 156);
static_assert(offsetof(PoseTraceSample, rightKnee) == 172);
static_assert(offsetof(PoseTraceSample, leftLegIk) == 188);
static_assert(offsetof(PoseTraceSample, rightLegIk) == 216);
static_assert(offsetof(PoseTraceSample, leftWrist) == 244);
static_assert(offsetof(PoseTraceSample, rightWrist) == 260);
static_assert(offsetof(PoseTraceSample, leftShoulder) == 276);
static_assert(offsetof(PoseTraceSample, rightShoulder) == 292);
static_assert(kPoseTraceCapacity * sizeof(PoseTraceSample) == 0x3B3760);

struct SkeletonCalibration {
    float legLength;
    float heightScale;
    float heightOffset;
    float legIkXOffset;
};

struct StandardSkeletonPose {
    float upperBody[4];
    float neck[4];
    float leftArm[4];
    float leftWrist[4];
    float leftElbow[4];
    float rightArm[4];
    float rightWrist[4];
    float rightElbow[4];
    float lowerBody[4];
    float leftLeg[4];
    float leftKnee[4];
    float leftFoot[4];
    float rightLeg[4];
    float rightKnee[4];
    float rightFoot[4];
    float leftShoulder[4];
    float rightShoulder[4];
};

struct SkeletonJoints {
    float positions[kTrackedJointCount][3];
    float* operator[](TrackedJoint joint) { return positions[static_cast<std::size_t>(joint)]; }
    const float* operator[](TrackedJoint joint) const {
        return positions[static_cast<std::size_t>(joint)];
    }
};

struct SkeletonJointHistory {
    float samples[kSkeletonHistoryLength][3];
};

struct SkeletonHistory {
    SkeletonJointHistory joints[kTrackedJointCount];
};

inline void PushSkeletonJointHistory(float current[3], SkeletonJointHistory& history, int count) {
    if (count <= 0 || count > static_cast<int>(kSkeletonHistoryLength))
        return;
    if (count > 1)
        std::memmove(history.samples, history.samples + 1,
                     sizeof(history.samples[0]) * static_cast<std::size_t>(count - 1));
    std::memcpy(history.samples[count - 1], current, sizeof(history.samples[0]));
    float sums[3] = {};
    int used = 0;
    for (int sample = 0; sample < count; ++sample) {
        const float* entry = history.samples[sample];
        if (entry[1] == kInvalidJointY)
            continue;
        ++used;
        for (int component = 0; component < 3; ++component)
            sums[component] = entry[component] + sums[component];
    }
    if (used != 0) {
        // Both references store the reciprocal as float before multiplying.
        const float inverse = 1.0f / static_cast<float>(used);
        for (int component = 0; component < 3; ++component)
            current[component] = inverse * sums[component];
    }
}

// The OpenNI adapter's export uses integer joint IDs. Keep that protocol
// mapping at this boundary, independently of ModelRecord's native layout.
template <class GetJoint>
void CaptureSkeletonJoints(SkeletonJoints& joints, unsigned version, GetJoint&& getJoint) {
    constexpr TrackedJoint initial[] = {
        TrackedJoint::Center,        TrackedJoint::Neck,       TrackedJoint::Head,
        TrackedJoint::RightShoulder, TrackedJoint::RightElbow, TrackedJoint::RightWrist,
        TrackedJoint::LeftShoulder,  TrackedJoint::LeftElbow,  TrackedJoint::LeftWrist,
        TrackedJoint::RightHip,      TrackedJoint::RightKnee,  TrackedJoint::RightAnkle,
        TrackedJoint::LeftHip,       TrackedJoint::LeftKnee,   TrackedJoint::LeftAnkle,
        TrackedJoint::Torso,         TrackedJoint::RightHand,  TrackedJoint::LeftHand};
    for (int id = 0; id < 18; ++id)
        getJoint(id, joints[initial[id]]);
    if (version == 14 || version == 15) {
        getJoint(18, joints[TrackedJoint::RightFoot]);
        getJoint(19, joints[TrackedJoint::LeftFoot]);
    }
    if (version == 15) {
        getJoint(20, joints[TrackedJoint::RightClavicle]);
        getJoint(21, joints[TrackedJoint::LeftClavicle]);
        getJoint(22, joints[TrackedJoint::HeadDirection]);
    }
}

template <class Bone>
int FindSkeletonBone(const Bone* bones, std::size_t count, const void* name,
                     std::size_t nameBytes) {
    if (bones == nullptr)
        return -1;
    for (std::size_t i = 0; i < count; ++i)
        if (std::memcmp(bones[i].name, name, nameBytes) == 0)
            return static_cast<int>(i);
    return -1;
}

static_assert(sizeof(StandardSkeletonPose) == 17 * 4 * sizeof(float));
static_assert(sizeof(SkeletonJoints) == 23 * 3 * sizeof(float));
static_assert(sizeof(SkeletonHistory) == 23 * 30 * 3 * sizeof(float));

} // namespace mikudancestudio::mdl
