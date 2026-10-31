#pragma once
#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/cpu_model_state.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/vmd_format.hpp"
#include "mikudancestudio/vpd_format.hpp"
#include <memory>
#include <string>
#include <utility>

namespace mikudancestudio::runtime {
inline constexpr const char* SupportedCapabilities[]{"pmd-runtime",
                                                     "pmx-2.0-utf16le",
                                                     "basic-bone-hierarchy",
                                                     "cp932-motion-name-decoding",
                                                     "vmd-model-and-ik-tracks",
                                                     "vpd-pose",
                                                     "integer-frame-seek",
                                                     "bdef1",
                                                     "bdef2",
                                                     "bdef4",
                                                     "sdef",
                                                     "vertex-morph",
                                                     "bone-group-uv-material-morph",
                                                     "append-or-fixed-axis-bones",
                                                     "scene-ik-physics",
                                                     "pmm-camera-light-shadow-gravity",
                                                     "external-parent",
                                                     "static-x-text-binary-mszip"};
inline constexpr const char* UnsupportedCapabilities[]{
    "qdef", "pmx-2.1", "pmx-utf8", "animated-or-framed-x", "rendering", "mme"};
// ModelInstance is a kinematic host; SceneRuntime owns the shared Bullet world,
// playback state and inter-model bindings. Unsupported features fail at import.
class ModelResource final {
public:
    static bool Create(const format::ModelDocument&, std::shared_ptr<const ModelResource>&,
                       std::string& reason);
    const format::PmxDocument& Document() const { return document_; }
    const std::vector<std::string>& BoneNames() const { return boneNames_; }
    const std::vector<std::string>& MorphNames() const { return morphNames_; }
    format::ModelKind SourceKind() const { return sourceKind_; }
    const std::array<std::array<char, 100>, 10>& PmdToonNames() const { return pmdToonNames_; }

private:
    friend class ModelInstance;
    friend class MotionClip;
    friend class ModelTimeline;
    friend class ModelEvaluation;
    format::PmxDocument document_;
    format::ModelKind sourceKind_ = format::ModelKind::Pmx;
    std::array<std::array<char, 100>, 10> pmdToonNames_{};
    std::vector<cpu_model::Bone> skeleton_;
    std::vector<cpu_model::IkChain> chains_;
    std::vector<std::string> boneNames_, morphNames_;
    std::vector<std::size_t> order_;
    ModelResource() = default;
};
class MotionClip final {
public:
    static bool Create(std::shared_ptr<const ModelResource>, const format::VmdDocument&,
                       std::shared_ptr<const MotionClip>&, std::string& reason);
    static bool Create(std::shared_ptr<const ModelResource>, const format::VpdDocument&,
                       std::shared_ptr<const MotionClip>&, std::string& reason);
    const std::vector<std::string>& Warnings() const { return warnings_; }

private:
    friend class ModelInstance;
    friend class ModelTimeline;
    std::shared_ptr<const ModelResource> resource_;
    std::vector<std::vector<format::PmmBoneKey>> bones_;
    std::vector<std::vector<format::VmdMorphKey>> morphs_;
    std::vector<std::string> warnings_;
    std::vector<format::PmmDisplayKey> displays_;
    MotionClip() = default;
};
struct BonePose {
    cpu_math::Vec3 translation{}, worldPosition{};
    cpu_math::Quaternion rotation{0, 0, 0, 1};
    cpu_math::Matrix local{}, skin{};
};
struct VertexPose {
    cpu_math::Vec3 position{}, normal{};
    std::array<float, 2> uv{};
    std::array<std::array<float, 4>, 4> additionalUv{};
};
struct MaterialPose {
    format::MaterialData material;
    std::array<float, 4> textureTint{1, 1, 1, 1}, sphereTint{1, 1, 1, 1}, toonTint{1, 1, 1, 1};
};
struct ModelSnapshot {
    std::uint32_t frame = 0;
    std::vector<BonePose> bones;
    std::vector<float> morphWeights;
    std::vector<VertexPose> vertices;
    std::vector<MaterialPose> materials;
};
// One unpublished CPU evaluation. A scene can solve all models before physics,
// synchronize one shared world, then solve after physics and publish once.
class ModelEvaluation {
public:
    cpu_model::Model View();
    const std::shared_ptr<const ModelResource>& Resource() const { return resource_; }
    bool SetBindings(const std::vector<cpu_model::Binding>&, std::string&);
    bool SetIk(const std::vector<std::uint8_t>&, std::string&);
    bool Solve(bool afterPhysics, cpu_model::Model* const* slots, int physicsMode,
               std::string& reason);
    bool Finish(std::string& reason);
    std::vector<cpu_model::Bone>& Bones() { return bones_; }
    const ModelSnapshot& Snapshot() const { return snapshot_; }

private:
    friend class ModelInstance;
    std::shared_ptr<const ModelResource> resource_;
    ModelSnapshot snapshot_;
    std::vector<cpu_model::Bone> bones_;
    std::vector<cpu_model::IkChain> chains_;
    std::vector<cpu_model::Binding> bindings_;
    std::vector<format::PmxVertexData> vertices_;
    std::vector<std::array<float, 7>> morphPose_;
    bool beforeSolved_ = false, afterSolved_ = false, finished_ = false;
    bool Initialize(std::string& reason);
};
class ModelInstance final {
public:
    explicit ModelInstance(std::shared_ptr<const ModelResource> resource)
        : resource_(std::move(resource)) {}
    // Binding checks resource identity. Failed binding/seek preserves old state.
    bool BindMotion(std::shared_ptr<const MotionClip>, std::string& reason);
    bool Seek(std::uint32_t frame, std::string& reason);
    bool Prepare(std::uint32_t frame, ModelEvaluation&, std::string& reason) const;
    bool PreparePose(std::uint32_t frame, const std::vector<format::PmmEvaluatedBone>&,
                     const std::vector<float>& morphs, ModelEvaluation&, std::string& reason) const;
    bool Publish(ModelEvaluation&&, std::string& reason);
    const ModelSnapshot& Snapshot() const { return snapshot_; }

private:
    std::shared_ptr<const ModelResource> resource_;
    std::shared_ptr<const MotionClip> motion_;
    ModelSnapshot snapshot_;
};
}
