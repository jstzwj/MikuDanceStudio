// ===========================================================================
// ModelDispose: x86 VA 0x0048F830 / x64 RVA 0xA8D50 (v9.32)
// ===========================================================================
// Releases model-owned resources; the caller still owns the ModelRecord.
// DeleteModel is the dispose-and-optionally-free wrapper.
//
// Original teardown order:
//   joints and their constraints; rigid bodies; paired undo/redo slots;
//   key-index tables; all display-key IK flags, then all selector states;
//   display/morph/bone key pools; selection and display-group tables;
//   morph records and shared morph tables; IK chains; animation cursors;
//   bone records; material/index/vertex tables; bone binding order;
//   edge/vertex/index COM buffers; the four PMX text buffers.
//
// In this port, model-owned tables use scalar ::operator new/delete.
// The bone binding table uses new[]/delete[]; D3D resources use Release().
// Keep each allocation paired with its matching release during migration.
//
// Physics objects are removed through Bullet's public API before their
// model records are freed. The former x86 private-field/vtable walks do
// not survive the x64 ABI. Existing null guards support empty and partially
// loaded models; this function does not reset scalar counts or defaults.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <unknwn.h>

#include <cstdint>
#include <new>

#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// Model-owned tables are raw storage from scalar ::operator new.
template <typename T> inline void DeleteOwnedStorage(T*& pointer) {
    ::operator delete(pointer);
    pointer = nullptr;
}

inline void ReleaseCom(void*& storage) {
    IUnknown*& object = mdl::ResourceAs<IUnknown>(storage);
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

// Remove scene objects through Bullet's public API.  The former x86 port
// walked Bullet's private vectors and virtual-table slots; those layouts do
// not survive the x64 ABI and left freed bodies in the world.
void ReleasePhysConstraint(PhysicsScene* physics, int constraintId) {
    if (physics == nullptr || physics->world == nullptr)
        return;
    btDiscreteDynamicsWorld* const world = physics->world;
    for (int i = world->getNumConstraints() - 1; i >= 0; --i) {
        btTypedConstraint* const item = world->getConstraint(i);
        if (item != nullptr && item->getUid() == constraintId) {
            world->removeConstraint(item);
            // CreatePhysJoint allocates this exact type with scalar new. The
            // Bullet 2.75 x86 virtual deleting destructor corrupts the CRT
            // heap here; dispatch the destructor explicitly and pair it with
            // the scalar allocator used at construction.
            auto* spring = static_cast<btGeneric6DofSpringConstraint*>(item);
            spring->~btGeneric6DofSpringConstraint();
            ::operator delete(static_cast<void*>(spring));
            return;
        }
    }
}

void ReleasePhysRigid(PhysicsScene* physics, btRigidBody* body) {
    if (physics == nullptr || physics->world == nullptr || body == nullptr)
        return;
    physics->world->removeRigidBody(body);
    delete body->getMotionState();
    delete body->getCollisionShape();
    delete body;
}

} // namespace

// VA 0x0048F830
void ModelDispose(mdl::ModelRecord* m) {
#ifdef MIKUDANCESTUDIO_DIAG
    ReleaseCpuModelPreview(m);
#endif
    if (g_Block)
        g_Block->CpuModelFrames().erase(m);
    mdl::ModelRecord& model = *m;
    PhysicsScene* scene = model.scenePtr;

    // Unlike the reference destructor, release an interrupted capture as well.
    // Resetting tracking only stops recording and retains storage for reuse.
    DeleteOwnedStorage(model.poseTraceBuffer);
    model.poseTraceSampleCount = 0;
    model.poseTraceRecording = 0;

    // ---- joints -----------------------------------------------------------
    if (model.jointCount > 0) {
        for (std::uint32_t i = 0; i < model.jointCount; ++i) {
            mdl::JointRecord* jt = &model.jointTable[i];
            ReleasePhysConstraint(scene, jt->constraint);
            DeleteOwnedStorage(jt->jpText);
            DeleteOwnedStorage(jt->enText);
        }
    }
    DeleteOwnedStorage(model.jointTable);

    // ---- rigid bodies -----------------------------------------------------
    if (model.rigidCount > 0) {
        for (std::uint32_t i = 0; i < model.rigidCount; ++i) {
            mdl::RigidRecord* rb = &model.rigidTable[i];
            ReleasePhysRigid(scene, rb->body);
            rb->body = nullptr;
            DeleteOwnedStorage(rb->jpText);
            DeleteOwnedStorage(rb->enText);
        }
    }
    DeleteOwnedStorage(model.rigidTable);

    // Visit the same slot in undo and redo before advancing (x64 RVA 0xA8EA0,
    // x86 0x48F96B): key snapshots precede the pose in each record.
    for (int slot = 0; slot < mdl::kUndoHistoryDepth; ++slot) {
        for (auto& ring : model.undoRings) {
            auto& undo = ring.slots[slot];
            DeleteOwnedStorage(undo.keySnapshots);
            DeleteOwnedStorage(undo.bonePose);
        }
    }

    DeleteOwnedStorage(model.morphTimelineRows);
    DeleteOwnedStorage(model.boneTimelineRows);

    // ---- animation pools --------------------------------------------------
    if (model.displayKeys != nullptr) {
        for (int i = 0; i < static_cast<int>(mdl::kDisplayKeyCapacity); ++i) {
            DeleteOwnedStorage(mdl::IkStates(model.displayKeys[i]));
        }
        // The original finishes the IK sweep before freeing any selectors
        // (x64 RVA 0xA8F70, x86 0x48FA3C).
        for (int i = 0; i < static_cast<int>(mdl::kDisplayKeyCapacity); ++i) {
            DeleteOwnedStorage(mdl::SelectorStates(model.displayKeys[i]));
        }
    }
    ::operator delete(model.displayKeys);
    model.displayKeys = nullptr;
    ::operator delete(model.morphKeys);
    model.morphKeys = nullptr;
    ::operator delete(model.boneKeys);
    model.boneKeys = nullptr;

    DeleteOwnedStorage(model.boneSelection);
    DeleteOwnedStorage(model.bonePhysicsState);
    DeleteOwnedStorage(model.displayBones);
    DeleteOwnedStorage(model.displayGroups);
    DeleteOwnedStorage(model.displayFrames);

    // ---- IK records --------------------------------------------------------
    if (model.morphs != nullptr && model.morphCount > 0) {
        const std::uint32_t n = model.morphCount;
        for (std::uint32_t i = 0; i < n; ++i) {
            mdl::MorphRecord& morph = model.morphs[i];
            DeleteOwnedStorage(morph.jpText);
            DeleteOwnedStorage(morph.enText);
            DeleteOwnedStorage(morph.vertexEntries);
            for (auto& entries : morph.uvEntries)
                DeleteOwnedStorage(entries);
            DeleteOwnedStorage(morph.boneEntries);
            DeleteOwnedStorage(morph.groupEntries);
            DeleteOwnedStorage(morph.materialEntries);
        }
    }

    DeleteOwnedStorage(model.morph0Table);
    for (auto& table : model.uvMorphTables.byFamily)
        DeleteOwnedStorage(table);
    DeleteOwnedStorage(model.boneMorphTable);
    DeleteOwnedStorage(model.reservedMorphTable);
    DeleteOwnedStorage(model.materialMorphPools.base);
    DeleteOwnedStorage(model.materialMorphPools.additive);
    DeleteOwnedStorage(model.materialMorphPools.multiplicative);

    DeleteOwnedStorage(model.morphs);

    // ---- IK chains ---------------------------------------------------------
    if (model.ikChains != nullptr && model.ikChainCount > 0) {
        const int n = model.ikChainCount;
        for (int i = 0; i < n; ++i)
            DeleteOwnedStorage(model.ikChains[i].links);
    }
    DeleteOwnedStorage(model.ikChains);

    DeleteOwnedStorage(model.morphKeyCursors);
    DeleteOwnedStorage(model.morphTrackActive);
    DeleteOwnedStorage(model.boneKeyCursors);
    DeleteOwnedStorage(model.boneTrackActive);

    // ---- bones -------------------------------------------------------------
    if (model.boneCount > 0) {
        mikudancestudio::mdl::BoneRecord* bones = model.boneTable;
        for (std::uint32_t i = 0; i < model.boneCount; ++i) {
            mikudancestudio::mdl::BoneRecord* bone = &bones[i];
            DeleteOwnedStorage(bone->jpText);
            DeleteOwnedStorage(bone->enText);
            DeleteOwnedStorage(bone->ikLinks);
        }
    }
    DeleteOwnedStorage(model.boneTable);

    DeleteOwnedStorage(model.materials);
    DeleteOwnedStorage(model.indices);
    DeleteOwnedStorage(model.rawVertices);
    DeleteOwnedStorage(model.pmxVertices);
    delete[] model.boneOrderTable;
    model.boneOrderTable = nullptr;

    // ---- D3D pool objects (vertex/index buffers) ---------------------------
    ReleaseCom(model.vertexBuffer2);
    ReleaseCom(model.vertexBuffer);
    ReleaseCom(model.indexBuffer);

    DeleteOwnedStorage(
        model.pmxTextBuffers[static_cast<std::size_t>(mdl::PmxTextBufferSlot::japaneseName)]);
    DeleteOwnedStorage(
        model.pmxTextBuffers[static_cast<std::size_t>(mdl::PmxTextBufferSlot::englishName)]);
    DeleteOwnedStorage(
        model.pmxTextBuffers[static_cast<std::size_t>(mdl::PmxTextBufferSlot::japaneseComment)]);
    DeleteOwnedStorage(
        model.pmxTextBuffers[static_cast<std::size_t>(mdl::PmxTextBufferSlot::englishComment)]);
}

// ---------------------------------------------------------------------------
// VA 0x0040A710 - DeleteModel(model, freeFlag): the
// dispose-and-maybe-free wrapper used by the model-delete command
// (0x47FEF1).  Returns the model pointer in the original; no caller reads
// it, kept void.
// ---------------------------------------------------------------------------
void DeleteModel(mdl::ModelRecord* model, int freeFlag) {
    if (g_Block && g_Block->PortableSceneSession()) {
        const auto& scene = g_Block->PortableSceneSession()->Document();
        if (scene.models.size() == 1 && g_Block->ModelSlot(scene.models[0].slot) == model)
            g_Block->ClearPortableSceneSession();
    }
    ModelDispose(model); // 0x40A713
    if ((freeFlag & 1) != 0)
        ::operator delete(model); // 0x40A720
}

} // namespace mikudancestudio
