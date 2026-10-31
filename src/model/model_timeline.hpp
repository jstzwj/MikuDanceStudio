#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/model_physics_runtime.hpp"
#include "mikudancestudio/cpu_keyframe.hpp"
#include <algorithm>
namespace mikudancestudio::runtime {
class ModelTimeline {
public:
    std::vector<cpu_model::Bone> bones;
    std::vector<cpu_model::IkChain> chains;
    std::vector<cpu_model::Binding> bindings;
    std::vector<cpu_model::BoneKey> boneKeys;
    std::vector<cpu_model::MorphKey> morphKeys;
    std::vector<cpu_model::Morph> morphs;
    std::vector<cpu_model::DisplayKey> displayKeys;
    std::vector<unsigned char> boneActive, morphActive;
    std::vector<std::uint32_t> boneCursors, morphCursors;
    unsigned char visible = 1, displayActive = 0;
    std::uint32_t displayCursor = 0;
    using Kernel = cpu_keyframe::Kernel<cpu_model::KeyframeTraits>;
    bool Initialize(ModelInstance& instance, ModelPhysics& physics, std::string& reason) {
        ModelEvaluation state;
        if (!instance.Prepare(0, state, reason))
            return false;
        physics.Restore(state);
        bones = state.Bones();
        auto view = state.View();
        if (view.ikChainCount)
            chains.assign(view.ikChains, view.ikChains + view.ikChainCount);
        else
            chains.clear();
        std::size_t count = 1;
        for (const auto& b : bones)
            if (b.slotIndex >= 0)
                count = std::max(count, std::size_t(b.slotIndex + 1));
        bindings.resize(count);
        boneKeys.resize(bones.size());
        morphs.resize(state.Snapshot().morphWeights.size());
        morphKeys.resize(morphs.size());
        boneActive.resize(bones.size());
        morphActive.resize(morphs.size());
        boneCursors.resize(bones.size());
        morphCursors.resize(morphs.size());
        displayKeys.resize(1000);
        for (auto& key : displayKeys) {
            key.ik.resize(chains.size(), 1);
            key.selectors.resize(bindings.size(), {-1, 0});
        }
        return true;
    }
    cpu_model::Model View(ModelPhysics& physics) {
        cpu_model::Model view;
        view.boneTable = bones.data();
        view.boneCount = int(bones.size());
        view.ikChains = chains.data();
        view.ikChainCount = int(chains.size());
        view.boneOrderTable = bindings.data();
        view.boneOrderCount = int(bindings.size());
        view.boneKeys = boneKeys.data();
        view.morphKeys = morphKeys.data();
        view.morphs = morphs.data();
        view.morphCount = int(morphs.size());
        view.displayKeys = displayKeys.data();
        view.boneTrackActive = boneActive.data();
        view.morphTrackActive = morphActive.data();
        view.boneKeyCursors = boneCursors.data();
        view.morphKeyCursors = morphCursors.data();
        view.loadComplete = visible;
        view.displayKeyCursor = displayCursor;
        view.displayTrackActive = displayActive;
        physics.Attach(view);
        return view;
    }
    void Save(const cpu_model::Model& view) {
        visible = view.loadComplete;
        displayCursor = view.displayKeyCursor;
        displayActive = view.displayTrackActive;
    }
    void Seek(unsigned frame, int mode, ModelPhysics& physics) {
        auto view = View(physics);
        Kernel::SeekModelFrame(view, int(frame), mode);
        Save(view);
    }
    void Start(float seconds, int mode, ModelPhysics& physics) {
        auto view = View(physics);
        Kernel::InitModelTrackCursors(&view, seconds, mode);
        Save(view);
    }
    void Advance(float seconds, int mode, ModelPhysics& physics) {
        auto view = View(physics);
        Kernel::AdvanceModelKeyframes(&view, seconds, mode);
        Save(view);
    }
    bool Bind(const MotionClip& clip, std::string& reason) {
        const auto copy = [&](const auto& tracks, auto& output, auto convert) {
            output.assign(tracks.size(), {});
            for (std::size_t root = 0; root < tracks.size(); ++root) {
                std::size_t previous = root;
                for (std::size_t n = 0; n < tracks[root].size(); ++n) {
                    const auto index = n ? output.size() : root;
                    if (n)
                        output.emplace_back();
                    auto& out = output[index];
                    convert(out, tracks[root][n]);
                    if (n) {
                        output[previous].next = std::uint32_t(index);
                        out.previous = std::uint32_t(previous);
                    }
                    previous = index;
                }
            }
        };
        copy(clip.bones_, boneKeys, [](auto& out, const auto& key) {
            out.frame = key.frame;
            out.physicsDisabled = key.physicsDisabled;
            std::copy(key.position.begin(), key.position.end(), out.position);
            std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
            std::copy(key.interpolation.begin(), key.interpolation.end(), out.interpolation);
        });
        copy(clip.morphs_, morphKeys, [](auto& out, const auto& key) {
            out.frame = key.frame;
            out.value = key.value;
        });
        for (std::size_t n = 0; n < clip.displays_.size(); ++n) {
            const auto& key = clip.displays_[n];
            auto& out = displayKeys[n];
            out.frame = key.frame;
            out.visible = key.visible;
            out.ik = key.ikEnabled;
            out.previous = n ? std::uint32_t(n - 1) : 0;
            out.next = n + 1 < clip.displays_.size() ? std::uint32_t(n + 1) : 0;
        }
        reason.clear();
        return true;
    }
    void Publish(const format::PmmSceneModel& source, const std::vector<std::size_t>& boneMap,
                 const std::vector<std::size_t>& morphMap, const std::vector<std::size_t>& ikMap,
                 const std::vector<std::size_t>& selectorMap) {
        const auto each = [](const auto& track, auto fn) {
            for (std::size_t n = 0; n < track.dense.size(); ++n)
                fn(n, track.dense[n]);
            for (const auto& k : track.sparse)
                fn(std::size_t(k.index), k.value);
        };
        const auto grow = [&](const auto& track, auto& keys) {
            std::size_t size = track.dense.size();
            for (const auto& k : track.sparse)
                size = std::max(size, std::size_t(k.index) + 1);
            keys.assign(size, {});
        };
        grow(source.boneKeys, boneKeys);
        grow(source.morphKeys, morphKeys);
        each(source.boneKeys, [&](std::size_t n, const auto& key) {
            const auto index = n < boneMap.size() ? boneMap[n] : n;
            auto& out = boneKeys[index];
            out.frame = key.frame;
            out.previous =
                key.previous < boneMap.size() ? std::uint32_t(boneMap[key.previous]) : key.previous;
            out.next =
                key.next && key.next < boneMap.size() ? std::uint32_t(boneMap[key.next]) : key.next;
            std::copy(key.position.begin(), key.position.end(), out.position);
            std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
            std::copy(key.interpolation.begin(), key.interpolation.end(), out.interpolation);
            out.physicsDisabled = key.physicsDisabled;
        });
        each(source.morphKeys, [&](std::size_t n, const auto& key) {
            const auto index = n < morphMap.size() ? morphMap[n] : n;
            auto& out = morphKeys[index];
            out.frame = key.frame;
            out.value = key.value;
            out.previous = key.previous < morphMap.size() ? std::uint32_t(morphMap[key.previous])
                                                          : key.previous;
            out.next = key.next && key.next < morphMap.size() ? std::uint32_t(morphMap[key.next])
                                                              : key.next;
        });
        each(source.displayKeys, [&](std::size_t n, const auto& key) {
            auto& out = displayKeys.at(n);
            out.frame = key.frame;
            out.previous = key.previous;
            out.next = key.next;
            out.visible = key.visible;
            std::fill(out.ik.begin(), out.ik.end(), 1);
            std::fill(out.selectors.begin(), out.selectors.end(), format::PmmRigidState{-1, 0});
            for (std::size_t i = 0; i < ikMap.size(); ++i)
                out.ik[ikMap[i]] = key.ikEnabled[i];
            for (std::size_t i = 0; i < selectorMap.size(); ++i)
                out.selectors[selectorMap[i]] = key.rigidStates[i];
        });
    }
};
}
