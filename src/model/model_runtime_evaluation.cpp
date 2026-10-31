#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/cpu_bone_transform.hpp"
#include "mikudancestudio/cpu_deform.hpp"
#include "mikudancestudio/cpu_morph.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mikudancestudio::runtime {
namespace {
template <class T> bool Finite(const T& values) {
    for (float f : values)
        if (!std::isfinite(f))
            return false;
    return true;
}
cpu_deform::MaterialPool Base(const format::MaterialData& material) {
    cpu_deform::MaterialPool out;
    auto& c = out.channels;
    std::copy_n(material.diffuse, 4, c.diffuse);
    std::copy_n(material.specular, 3, c.specular);
    std::copy_n(material.ambient, 3, c.ambient);
    std::copy_n(material.edgeColor, 4, c.edgeColor);
    c.specularPower = material.specularPower;
    c.edgeSize = material.edgeSize;
    for (auto* p : {c.textureTint, c.sphereTint, c.toonTint})
        std::fill_n(p, 4, 1.f);
    return out;
}
cpu_deform::MaterialPool Multipliers() {
    cpu_deform::MaterialPool out;
    auto& c = out.channels;
    for (auto* p : {c.diffuse, c.edgeColor, c.textureTint, c.sphereTint, c.toonTint})
        std::fill_n(p, 4, 1.f);
    for (auto* p : {c.specular, c.ambient})
        std::fill_n(p, 3, 1.f);
    c.specularPower = c.edgeSize = 1;
    return out;
}
struct SdefOps {
    void QuatFromMatrix(float* out, const float* input) {
        cpu_math::Matrix m;
        std::memcpy(m.data(), input, 64);
        const auto q = cpu_math::QuaternionFromMatrix(m);
        std::memcpy(out, q.data(), 16);
    }
    void RotationMatrix(float* out, const float* input) {
        const auto m = cpu_math::RotationQuaternion({input[0], input[1], input[2], input[3]});
        std::memcpy(out, m.data(), 64);
    }
};
}
bool ModelEvaluation::Initialize(std::string& reason) {
    bones_ = resource_->skeleton_;
    chains_ = resource_->chains_;
    vertices_ = resource_->document_.vertices;
    const auto& model = resource_->document_;
    std::vector<cpu_deform::BoneMorphState> boneMorphs(bones_.size());
    std::vector<cpu_deform::MaterialPool> additive(model.materials.size()),
        multiplicative(model.materials.size(), Multipliers());
    const auto apply = [&](const format::PmxMorphDocument& morph, float weight, float reference) {
        const unsigned type = morph.header.type;
        for (const auto& offset : morph.offsets) {
            if (type == 1)
                cpu_deform::AccumulateVertexMorph(vertices_[offset.index].position, offset.values,
                                                  weight, reference);
            else if (type == 2) {
                cpu_deform::BoneMorphEntry entry;
                entry.boneIndex = offset.index;
                std::copy_n(offset.values, 3, entry.translation);
                std::copy_n(offset.values + 3, 4, entry.rotation);
                cpu_deform::AccumBoneEntry(boneMorphs.data(), entry, weight, reference);
            } else if (type >= 3 && type <= 7) {
                auto& vertex = vertices_[offset.index];
                if (type == 3)
                    for (int c = 0; c < 2; ++c)
                        vertex.uv[c] += offset.values[c] * weight * reference;
                else
                    for (int c = 0; c < 4; ++c)
                        vertex.additionalUv[c][type - 4] += offset.values[c] * weight * reference;
            } else if (type == 8) {
                cpu_deform::MaterialEntry entry;
                entry.operation = offset.operation;
                static_assert(sizeof(entry.channels) == 28 * sizeof(float));
                std::memcpy(&entry.channels, offset.values, sizeof entry.channels);
                const auto accumulate = [&](std::size_t n) {
                    cpu_deform::ApplyMatSlot(additive[n], multiplicative[n], entry,
                                             weight * reference);
                };
                if (offset.index < 0)
                    for (std::size_t n = 0; n < additive.size(); ++n)
                        accumulate(n);
                else
                    accumulate(offset.index);
            }
        }
    };
    // Preserve the original single group-reference level and multiplication
    // order. A referenced group's own references are not recursively applied.
    for (std::size_t n = 0; n < model.morphs.size(); ++n) {
        const auto& morph = model.morphs[n];
        const float weight = snapshot_.morphWeights[n];
        if (weight == 0)
            continue;
        if (morph.header.type != 0)
            apply(morph, weight, 1);
        else
            for (const auto& reference : morph.offsets)
                if (model.morphs[reference.index].header.type != 0)
                    apply(model.morphs[reference.index], weight, reference.values[0]);
    }
    std::size_t slots = 1;
    morphPose_.resize(bones_.size());
    for (std::size_t n = 0; n < bones_.size(); ++n) {
        auto& bone = bones_[n];
        const auto& pose = snapshot_.bones[n];
        const auto& morph = boneMorphs[n];
        std::copy(pose.translation.begin(), pose.translation.end(), bone.trans);
        std::copy(pose.rotation.begin(), pose.rotation.end(), bone.rotQuat);
        std::copy_n(morph.translation, 3, morphPose_[n].begin());
        std::copy_n(morph.rotation, 4, morphPose_[n].begin() + 3);
        if (bone.slotIndex >= 0)
            slots = std::max(slots, std::size_t(bone.slotIndex + 1));
    }
    bindings_.resize(slots);
    snapshot_.materials.resize(model.materials.size());
    for (std::size_t n = 0; n < model.materials.size(); ++n) {
        auto& material = snapshot_.materials[n];
        material.material = model.materials[n].fields;
        const auto& add = additive[n].channels;
        const auto& mul = multiplicative[n].channels;
        const auto base = Base(model.materials[n].fields).channels;
        const auto compose = [](float m, float b, float a) { return m * b + a; };
        for (int c = 0; c < 3; ++c) {
            material.material.diffuse[c] = compose(mul.diffuse[c], base.diffuse[c], add.diffuse[c]);
            material.material.specular[c] =
                compose(mul.specular[c], base.specular[c], add.specular[c]);
            material.material.ambient[c] = compose(mul.ambient[c], base.ambient[c], add.ambient[c]);
            material.material.edgeColor[c] =
                compose(mul.edgeColor[c], base.edgeColor[c], add.edgeColor[c]);
        }
        material.material.diffuse[3] = compose(mul.diffuse[3], base.diffuse[3], add.diffuse[3]);
        material.material.edgeColor[3] =
            compose(mul.edgeColor[3], base.edgeColor[3], add.edgeColor[3]);
        material.material.specularPower =
            compose(mul.specularPower, base.specularPower, add.specularPower);
        material.material.edgeSize = compose(mul.edgeSize, base.edgeSize, add.edgeSize);
        // The legacy compose pass consumes sixteen channels. Tint pools are
        // accumulated above but do not change the resulting material.
        if (!Finite(material.material.diffuse) || !Finite(material.material.specular) ||
            !Finite(material.material.ambient) || !Finite(material.material.edgeColor) ||
            !std::isfinite(material.material.specularPower) ||
            !std::isfinite(material.material.edgeSize)) {
            reason = "nonfinite-material-output";
            return false;
        }
    }
    reason.clear();
    return true;
}
cpu_model::Model ModelEvaluation::View() {
    for (auto& chain : chains_)
        chain.links = chain.storage.data();
    cpu_model::Model view;
    view.boneTable = bones_.data();
    view.boneCount = int(bones_.size());
    view.ikChains = chains_.data();
    view.ikChainCount = int(chains_.size());
    view.boneOrderTable = bindings_.data();
    view.boneOrderCount = int(bindings_.size());
    view.order = resource_ ? &resource_->order_ : nullptr;
    view.physicsMode = resource_ && resource_->sourceKind_ == format::ModelKind::Pmd ? 1 : 2;
    return view;
}
bool ModelEvaluation::SetBindings(const std::vector<cpu_model::Binding>& bindings,
                                  std::string& reason) {
    if (beforeSolved_ || bindings.size() != bindings_.size()) {
        reason = "invalid-evaluation-bindings";
        return false;
    }
    for (const auto& binding : bindings)
        if (binding.linkedModel < -1 || binding.linkedModel > 254 || binding.linkedBone < 0) {
            reason = "invalid-evaluation-parent";
            return false;
        }
    bindings_ = bindings;
    reason.clear();
    return true;
}
bool ModelEvaluation::SetIk(const std::vector<std::uint8_t>& enabled, std::string& reason) {
    if (beforeSolved_ || enabled.size() != chains_.size()) {
        reason = "invalid-evaluation-ik";
        return false;
    }
    for (std::size_t n = 0; n < chains_.size(); ++n)
        chains_[n].enabled = enabled[n] != 0;
    reason.clear();
    return true;
}
bool ModelEvaluation::Solve(bool afterPhysics, cpu_model::Model* const* slots, int physicsMode,
                            std::string& reason) {
    if (!resource_) {
        reason = "missing-evaluation-resource";
        return false;
    }
    if (finished_ || (afterPhysics ? !beforeSolved_ || afterSolved_ : beforeSolved_)) {
        reason = "invalid-evaluation-stage";
        return false;
    }
    for (const auto& binding : bindings_)
        if (binding.linkedModel >= 0) {
            const auto* parent = slots ? slots[binding.linkedModel] : nullptr;
            if (!parent || binding.linkedBone >= parent->boneCount || !parent->boneTable) {
                reason = "missing-evaluation-parent";
                return false;
            }
        }
    for (std::size_t n = 0; n < bones_.size(); ++n) {
        auto& bone = bones_[n];
        const bool dynamic =
            (physicsMode == 1 || (physicsMode >= 2 && !bone.physicsDisabled)) && bone.hasRigidBody;
        std::copy_n(dynamic ? bone.ikBackup : bone.trans, 3, bone.physicsOffset);
        std::copy_n(dynamic ? bone.ikBackup + 3 : bone.rotQuat, 4, bone.physicsQuat);
        if (resource_->sourceKind_ == format::ModelKind::Pmx) {
            for (int c = 0; c < 3; ++c)
                bone.physicsOffset[c] += morphPose_[n][c];
            const auto& q = morphPose_[n];
            cpu_model::Math{}.quatMultiply(bone.physicsQuat, bone.physicsQuat, q.data() + 3);
        }
    }
    auto view = View();
    std::vector<int> layers;
    for (const auto& bone : bones_)
        layers.push_back(bone.layer);
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    for (int layer : layers)
        cpu_bone::TransformKernel<cpu_model::Traits>::Run(&view, afterPhysics, layer, slots,
                                                          physicsMode);
    for (std::size_t n = 0; n < bones_.size(); ++n) {
        auto& pose = snapshot_.bones[n];
        const auto& bone = bones_[n];
        std::copy_n(bone.matLocal, 16, pose.local.begin());
        std::copy_n(bone.matInit, 16, pose.skin.begin());
        pose.worldPosition = cpu_math::TransformPoint(
            {bone.position[0], bone.position[1], bone.position[2]}, pose.skin);
        if (!Finite(pose.local) || !Finite(pose.skin) || !Finite(pose.worldPosition)) {
            reason = "nonfinite-bone-output:" + std::to_string(n);
            return false;
        }
    }
    (afterPhysics ? afterSolved_ : beforeSolved_) = true;
    reason.clear();
    return true;
}
bool ModelEvaluation::Finish(std::string& reason) {
    if (!resource_) {
        reason = "missing-evaluation-resource";
        return false;
    }
    if (!afterSolved_ || finished_) {
        reason = "invalid-evaluation-stage";
        return false;
    }
    constexpr cpu_math::Matrix zero{};
    std::vector<VertexPose> output;
    output.reserve(vertices_.size());
    for (std::size_t n = 0; n < vertices_.size(); ++n) {
        const auto& v = vertices_[n];
        VertexPose out;
        const auto matrix = [&](int b) {
            return v.bone[b] < 0 ? zero.data() : bones_[v.bone[b]].matInit;
        };
        if (v.weightType == 0) {
            cpu_deform::TransformPosition(out.position.data(), v.position, matrix(0));
            cpu_deform::TransformNormal(out.normal.data(), v.normal, matrix(0));
        } else if (v.weightType == 1) {
            for (int c = 0; c < 3; ++c) {
                out.position[c] = cpu_deform::BlendTransformComponent(
                    v.position, matrix(0), matrix(1), c, v.weight[0], true);
                out.normal[c] = cpu_deform::BlendTransformComponent(v.normal, matrix(0), matrix(1),
                                                                    c, v.weight[0], false);
            }
        } else if (v.weightType == 2) {
            const float* matrices[]{matrix(0), matrix(1), matrix(2), matrix(3)};
            cpu_deform::Bdef4(out.position.data(), out.normal.data(), v.position, v.normal,
                              matrices, v.weight);
        } else {
            SdefOps ops;
            cpu_deform::Sdef(out.position.data(), out.normal.data(), v.position, v.normal, v.center,
                             v.r0, v.r1, matrix(0), matrix(1), v.weight[0], ops);
        }
        std::copy_n(v.uv, 2, out.uv.begin());
        for (unsigned set = 0; set < resource_->document_.header.additionalUvCount; ++set)
            for (int c = 0; c < 4; ++c)
                out.additionalUv[set][c] = v.additionalUv[c][set];
        if (!Finite(out.position) || !Finite(out.normal) || !Finite(out.uv)) {
            reason = "nonfinite-vertex-output:" + std::to_string(n);
            return false;
        }
        for (const auto& uv : out.additionalUv)
            if (!Finite(uv)) {
                reason = "nonfinite-uv-output";
                return false;
            }
        output.push_back(out);
    }
    snapshot_.vertices = std::move(output);
    finished_ = true;
    reason.clear();
    return true;
}
}
