#include "mikudancestudio/model_layout.hpp"
#include "mikudancestudio/bone_layout.hpp"
#include "mikudancestudio/skeleton_tracking.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace mikudancestudio::mdl;

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::fprintf(stderr, "%s\n", message);
            ++failures;
        }
    };
    auto model = std::make_unique<ModelRecord>();
    std::memset(model->pmdToonFileNames, 0x5a, sizeof model->pmdToonFileNames);
    for (unsigned version : {13u, 14u, 15u}) {
        for (auto& joint : model->currentJoints.positions)
            for (float& component : joint)
                component = -999.0f;
        int calls = 0;
        CaptureSkeletonJoints(model->currentJoints, version, [&](int id, float* destination) {
            ++calls;
            destination[0] = static_cast<float>(id);
            destination[1] = static_cast<float>(id + 100);
            destination[2] = static_cast<float>(id + 200);
        });
        check(calls == (version == 13   ? 18
                        : version == 14 ? 20
                                        : 23),
              "OpenNI version must gate optional joints");
        check(model->currentJoints[TrackedJoint::Center][0] == 0.0f &&
                  model->currentJoints[TrackedJoint::Torso][0] == 15.0f &&
                  model->currentJoints[TrackedJoint::RightHand][0] == 16.0f,
              "OpenNI joint IDs must map to the matching sensor joint");
        check(model->currentJoints[TrackedJoint::RightFoot][0] == (version == 13 ? -999.0f : 18.0f),
              "Old OpenNI must leave unsupported foot samples unchanged");
        check(model->currentJoints[TrackedJoint::HeadDirection][0] ==
                  (version == 15 ? 22.0f : -999.0f),
              "Head direction is only available in OpenNI 1.50");
    }
    for (const auto& name : model->pmdToonFileNames)
        for (char byte : name)
            check(byte == 0x5a, "Capture must not write into toon filenames");

    BoneRecord bones[3]{};
    std::strcpy(bones[0].name, "root");
    std::strcpy(bones[1].name, "leg");
    std::strcpy(bones[2].name, "ankle");
    check(FindSkeletonBone(bones, 3, "leg", 4) == 1, "Find leg must walk one typed bone at a time");
    check(FindSkeletonBone(bones, 3, "ankle", 6) == 2, "Find ankle must reach the final bone");
    check(FindSkeletonBone(bones, 3, "absent", 7) == -1 &&
              FindSkeletonBone(bones, 0, "root", 5) == -1 &&
              FindSkeletonBone<BoneRecord>(nullptr, 0, "root", 5) == -1,
          "Missing and empty bone arrays must not be dereferenced");

    SkeletonJointHistory history{};
    history.samples[1][0] = 2.0f;
    history.samples[1][1] = 4.0f;
    history.samples[1][2] = 6.0f;
    history.samples[2][1] = -999.0f;
    float current[3] = {6.0f, 8.0f, 10.0f};
    PushSkeletonJointHistory(current, history, 3);
    check(current[0] == 4.0f && current[1] == 6.0f && current[2] == 8.0f,
          "Rolling history must exclude sentinel samples");
    check(history.samples[2][0] == 6.0f && history.samples[2][1] == 8.0f &&
              history.samples[2][2] == 10.0f,
          "History stores the raw sample before averaging");
    float unchanged[3] = {1.0f, 2.0f, 3.0f};
    PushSkeletonJointHistory(unchanged, history, 0);
    check(unchanged[0] == 1.0f && unchanged[2] == 3.0f,
          "Zero-length history must not underflow its buffer");
    PushSkeletonJointHistory(unchanged, history, 30);
    check(history.samples[29][0] == 1.0f && history.samples[29][2] == 3.0f,
          "Maximum history length must fit its declared storage");
    SkeletonJointHistory thirds{};
    float value[3] = {5, 0, 0};
    PushSkeletonJointHistory(value, thirds, 3);
    std::uint32_t bits;
    std::memcpy(&bits, value, sizeof bits);
    check(bits == 0x3fd55556u, "Both references round the reciprocal to float before multiplying");
    return failures == 0 ? 0 : 1;
}
