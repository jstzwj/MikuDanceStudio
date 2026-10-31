#pragma once
#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/vmd_format.hpp"
#include <memory>
#include <string>
#include <utility>

namespace mikudancestudio::runtime {
inline constexpr const char* SupportedCapabilities[]{"pmx-2.0-utf16le", "basic-bone-hierarchy",
    "vmd-ascii-track-names", "integer-frame-seek", "bdef1", "bdef2", "bdef4", "vertex-morph"};
inline constexpr const char* UnsupportedCapabilities[]{"pmd-runtime", "cp932-motion-name-decoding",
    "ik", "sdef", "qdef", "append-or-fixed-axis-bones", "external-parent", "physics",
    "bone-group-uv-material-morph", "global-camera-light-shadow-ik-tracks", "rendering", "mme"};
// P1 is an explicit subset. Unsupported deformation is rejected at import,
// even if a particular frame happens not to animate that feature.
class ModelResource final {
public:
    static bool Create(const format::ModelDocument&, std::shared_ptr<const ModelResource>&,
                       std::string& reason);
    const format::PmxDocument& Document() const { return document_; }
    const std::vector<std::string>& BoneNames() const { return boneNames_; }
    const std::vector<std::string>& MorphNames() const { return morphNames_; }
private:
    friend class ModelInstance;
    format::PmxDocument document_;
    std::vector<std::string> boneNames_, morphNames_;
    std::vector<std::size_t> order_;
    ModelResource() = default;
};
class MotionClip final {
public:
    static bool Create(std::shared_ptr<const ModelResource>, const format::VmdDocument&,
                       std::shared_ptr<const MotionClip>&, std::string& reason);
    const std::vector<std::string>& Warnings() const { return warnings_; }
private:
    friend class ModelInstance;
    std::shared_ptr<const ModelResource> resource_;
    std::vector<std::vector<format::PmmBoneKey>> bones_;
    std::vector<std::vector<format::VmdMorphKey>> morphs_;
    std::vector<std::string> warnings_;
    MotionClip() = default;
};
struct BonePose {
    cpu_math::Vec3 translation{}, worldPosition{};
    cpu_math::Quaternion rotation{0,0,0,1};
    cpu_math::Matrix local{}, skin{};
};
struct VertexPose {
    cpu_math::Vec3 position{}, normal{};
    std::array<float,2> uv{};
};
struct ModelSnapshot {
    std::uint32_t frame = 0;
    std::vector<BonePose> bones;
    std::vector<float> morphWeights;
    std::vector<VertexPose> vertices;
};
class ModelInstance final {
public:
    explicit ModelInstance(std::shared_ptr<const ModelResource> resource) : resource_(std::move(resource)) {}
    // Binding checks resource identity. Failed binding/seek preserves old state.
    bool BindMotion(std::shared_ptr<const MotionClip>, std::string& reason);
    bool Seek(std::uint32_t frame, std::string& reason);
    const ModelSnapshot& Snapshot() const { return snapshot_; }
private:
    std::shared_ptr<const ModelResource> resource_;
    std::shared_ptr<const MotionClip> motion_;
    ModelSnapshot snapshot_;
};
}
