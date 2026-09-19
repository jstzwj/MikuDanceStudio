#pragma once

#include <cstddef>
#include <cstring>

namespace mikudancestudio::mdl {

// Names describe the tracked sensor frame; mirroring into model space is
// performed when applying the standard pose, not when collecting samples.
enum class TrackedJoint : std::size_t {
    Center, Torso, Neck, Head,
    RightShoulder, RightElbow, RightWrist, RightHand,
    LeftShoulder, LeftElbow, LeftWrist, LeftHand,
    RightHip, RightKnee, RightAnkle, RightFoot,
    LeftHip, LeftKnee, LeftAnkle, LeftFoot,
    RightClavicle, LeftClavicle, HeadDirection,
    Count
};
inline constexpr std::size_t kTrackedJointCount =
    static_cast<std::size_t>(TrackedJoint::Count);
inline constexpr std::size_t kSkeletonHistoryLength = 30;

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
    float* operator[](TrackedJoint joint) {
        return positions[static_cast<std::size_t>(joint)];
    }
    const float* operator[](TrackedJoint joint) const {
        return positions[static_cast<std::size_t>(joint)];
    }
};

struct SkeletonHistory {
    float positions[kTrackedJointCount][kSkeletonHistoryLength * 3];
};

inline void PushSkeletonJointHistory(float current[3], float* history,
                                     int count) {
    if (count <= 0 || count > static_cast<int>(kSkeletonHistoryLength))
        return;
    if (count > 1)
        std::memmove(history, history + 3,
                     3 * sizeof(float) * static_cast<std::size_t>(count - 1));
    std::memcpy(history + 3 * (count - 1), current, 3 * sizeof(float));
    float sums[3] = {};
    int used = 0;
    for (int sample = 0; sample < count; ++sample) {
        const float* entry = history + 3 * sample;
        if (entry[1] == -999.0f)
            continue;
        ++used;
        for (int component = 0; component < 3; ++component)
            sums[component] = entry[component] + sums[component];
    }
    if (used != 0) {
        const double inverse = 1.0 / static_cast<double>(used);
        for (int component = 0; component < 3; ++component)
            current[component] = static_cast<float>(
                inverse * static_cast<double>(sums[component]));
    }
}

// The OpenNI adapter's export uses integer joint IDs. Keep that protocol
// mapping at this boundary, independently of ModelRecord's native layout.
template<class GetJoint>
void CaptureSkeletonJoints(SkeletonJoints& joints, unsigned version,
                           GetJoint&& getJoint) {
    constexpr TrackedJoint initial[] = {
        TrackedJoint::Center, TrackedJoint::Neck, TrackedJoint::Head,
        TrackedJoint::RightShoulder, TrackedJoint::RightElbow,
        TrackedJoint::RightWrist, TrackedJoint::LeftShoulder,
        TrackedJoint::LeftElbow, TrackedJoint::LeftWrist,
        TrackedJoint::RightHip, TrackedJoint::RightKnee,
        TrackedJoint::RightAnkle, TrackedJoint::LeftHip,
        TrackedJoint::LeftKnee, TrackedJoint::LeftAnkle,
        TrackedJoint::Torso, TrackedJoint::RightHand,
        TrackedJoint::LeftHand
    };
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

template<class Bone>
int FindSkeletonBone(const Bone* bones, std::size_t count,
                     const void* name, std::size_t nameBytes) {
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

}  // namespace mikudancestudio::mdl
