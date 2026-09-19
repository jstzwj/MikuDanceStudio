// model_data.h - ModelData (original: 0x370-byte object at ModelData ctor
// 0x180058c70 / dtor 0x1800595a0; layout in globals_structures.md section 2).
//
// PHASE 1 DIVERGENCE (documented): this port is not 0x370 bytes and drops the
// dual vftable subobjects (ModelData + RenderCallback RTTI at 0x1800D79B0 /
// 0x1800D79D0) and the SSO std::string raw layout. Field ORDER and SEMANTICS
// follow the original offsets; the raw offsets are noted per field.
#pragma once

#include "object_plan_state.h"

#include <map>
#include <string>
#include <vector>

#include <d3d9.h>

#include "render_snapshot.h"
#include "sas_exec.h"   // SasRunState (the +0x358 suspended-scene run state)

namespace mme {

class ModelData {
public:
    // [0x180058c70] MME_ModelData_Constructor port.
    //   device:           +0x10 (AddRef'd)
    //   objectId:         +0x18
    //   filename:         +0x20 (BORROWED pointer, like the original)
    //   kind:             +0x28 (1 = PMD/PMX model, else accessory)
    //   materialCount:    +0x38
    //   reservedObject:   +0x40 (IUnknown*, AddRef'd; swapped if differing)
    ModelData(IDirect3DDevice9* device,
              unsigned long long objectId,
              const char* filename,
              int kind,
              unsigned int materialCount,
              IUnknown* reservedObject);

    // [0x1800595a0] MME_ModelData_Destructor port: releases the name map,
    // material vectors, the reserved COM object and the device.
    ~ModelData();

    ModelData(const ModelData&) = delete;
    ModelData& operator=(const ModelData&) = delete;

    // --- field accessors (offsets noted for evidence) ---
    unsigned long long objectId() const { return objectId_; }            // +0x18
    int kind() const { return kind_; }                                   // +0x28
    int materialCount() const { return materialCount_; }                 // +0x38
    IDirect3DDevice9* device() const { return device_; }                 // +0x10
    const std::string& name() const { return name_; }                    // +0x48
    const char* filename() const { return filename_; }                   // +0x20 (borrowed)

    // +0x364 render-class flag: 0 = normal object, 1/2 = special pass
    // (post-effect / self-shadow class) [180059ba0 L36, 18005d340 L106].
    // Written when a scene/sceneorobject effect is assigned to the object:
    // +0x360 = the effect's scriptClass, +0x364 = its scriptOrder
    // (1 = preprocess -> passPlanA, 2 = postprocess -> passPlanB),
    // +0x368 = its drawsGeometry flag [FUN_18002ca80 L292-320].
    int renderClass() const { return renderClass_; }
    void setRenderClass(int value) { renderClass_ = value; }

    // +0x360: the assigned scene effect's scriptClass (SasScriptClass).
    unsigned long long unknownFlag360() const { return unknownFlag360_; }
    void setUnknownFlag360(unsigned long long value) { unknownFlag360_ = value; }

    // +0x358 [FUN_18005a410/FUN_18005a5c0]: the persisted 0x88 run state of
    // a scene-effect technique. Created unconditionally by the per-turn
    // step; retained whether the walk suspends at ScriptExternal or
    // completes (index past the last command - the later resume then runs
    // no commands); resumed + destroyed by the post walk. Also destroyed by
    // the plan-state reset / shutdown walks. A null state at the resume is
    // impossible in the original (FUN_18001bbc0 would dereference null).
    SasRunState* runState() const { return runState_; }
    void setRunState(SasRunState* value) { runState_ = value; }

    // Reset the SAS binding fields (effect unassigned / model unregistered):
    // destroy a suspended run state and restore the plain-object class flags
    // (+0x360 = 0, +0x364 = 0, +0x368 = 1).
    void ClearSasBinding();

    // +0xe8 current draw-type index (compared/updated by
    // MME_ApplyModelRenderSnapshot [kit 18005a1e0 L16-23]; init -1).
    int drawTypeIndex() const { return drawTypeIndex_; }
    void setDrawTypeIndex(int value) { drawTypeIndex_ = value; }

    // +0x3c cached per-vertex value (init -1; recomputed by MmeCachePerVertexValue).
    int cachedPerVertexValue() const { return cachedPerVertexValue_; }
    void setCachedPerVertexValue(int value) { cachedPerVertexValue_ = value; }

    // --- Phase 2 additions (effect assignment / EMM / attach state) ---
    // The assigned effect file (narrow CP 0 path; "" = none). Recorded by the
    // EMM manager / auto-assignment; consumed by the effect-owner bindings.
    const std::string& effectFile() const { return effectFile_; }
    void setEffectFile(const std::string& path) { effectFile_ = path; }

    // EMM shown/hide state (the ".show" rows of the [Effect] section).
    bool shown() const { return shown_; }
    void setShown(bool value) { shown_ = value; }

    // Per-subset effect paths ("Pmd0[2]=fx" / "Pmd0@Hair=fx" EMM rows).
    // Phase 3 seam: recorded for EMM round-trips and the dialog; the render
    // path applies whole-object bindings only (the subsets ride along).
    void setSubsetEffect(int subsetIndex, const std::string& path)
    {
        subsetEffects_[subsetIndex] = path;
    }
    void setSubsetEffectByName(const std::string& materialName, const std::string& path)
    {
        subsetEffectsByName_[materialName] = path;
    }
    void clearSubsetEffects() { subsetEffects_.clear(); subsetEffectsByName_.clear(); }
    const std::map<int, std::string>& subsetEffects() const { return subsetEffects_; }
    const std::map<std::string, std::string>& subsetEffectsByName() const
    {
        return subsetEffectsByName_;
    }

    // Accessory attach resolution (MMHack GetAcsAttachedPmd, filled by the
    // pass planner refresh for kind != 1 objects; feeds the CONTROLOBJECT
    // "(self)" / "(AttachedModel)" / "(AttachedBone)" naming in Phase 3).
    bool attachedToModel() const { return attached_; }
    unsigned long long attachedModelId() const { return attachedModelId_; }
    int attachedBoneIndex() const { return attachedBoneIndex_; }
    void setAttachInfo(bool attached, unsigned long long modelId, int boneIndex)
    {
        attached_ = attached;
        attachedModelId_ = modelId;
        attachedBoneIndex_ = boneIndex;
    }

    // +0x368 (1): the "object registered/valid" flag read by the plan
    // bookkeeping (FUN_18005c510's renderClass walk); the SAS wiring copies
    // the assigned scene effect's drawsGeometry flag here.
    unsigned char flag368() const { return flag368_; }
    void setFlag368(unsigned char value) { flag368_ = value; }

    // Original ModelData+0x3c WORD (FUN_18005c970 byte +0x3c gate write /
    // FUN_18005cac0 read + failure WORD write 0x100): the turn-boundary
    // post-effect snapshot gate. c970 recomputes the gate byte at every
    // repeat boundary (adaptive && the offscreen record's AntiAlias
    // annotation flag && !failed - no staged/suspended precondition, and a
    // record with no offscreen association skips the write, keeping the
    // previous value); the
    // FUN_18005da50 record branch (and FUN_18005e210's record branch) run
    // the main-RT snapshot only while it holds. A snapshot failure writes
    // the WORD 0x100: gate byte 0 (disabled) + failed byte 1 (latched for
    // the record's lifetime). The raw +0x3c slot aliases the port's
    // cachedPerVertexValue in the layout notes; the port keeps the two
    // bytes as separate named fields.
    unsigned char postEffectSnapshotGate() const { return postEffectSnapshotGate_; }
    void setPostEffectSnapshotGate(unsigned char value) { postEffectSnapshotGate_ = value; }
    unsigned char postEffectFailedFlag() const { return postEffectFailedFlag_; }
    void setPostEffectFailedFlag(unsigned char value) { postEffectFailedFlag_ = value; }

    // +0xf8..+0x137: accessory world matrix, filled by
    // MmeRefreshObjectPlan for accessories, identity for models.
    D3DMATRIX& planMatrix()
    {
        return passPlanScratch_.world;
    }
    const D3DMATRIX& planMatrix() const
    {
        return passPlanScratch_.world;
    }

    // +0x190: the second copy of the same matrix
    // [FUN_180059aa0 big-C 72338 memcpy(param_1+400, param_1+0xf8, 0x40)].
    const D3DMATRIX& planMatrixCopy() const { return planMatrixCopy_; }
    void setPlanMatrixCopy(const D3DMATRIX& m) { planMatrixCopy_ = m; }

    // +0x138 cached 0x220 render snapshot (only updated for render-class 1/2
    // models with subset_index < 0 [180059ba0 L36-41]).
    RenderSnapshot& snapshot() { return snapshot_; }
    const RenderSnapshot& snapshot() const { return snapshot_; }

    // +0x70 name -> index map. Bones 0..n-1, morphs -1, -2, ... (negative);
    // accessory pseudo-bones X/Y/Z/XYZ/Rx/Ry/Rz/Rxyz/Si/Tr (see ctor).
    // Returns nullptr when not found (read path used by Phase 2 binding).
    const int* findNameIndex(const std::string& name) const;

    // +0xa0 / +0xc0 per-material name vectors (Japanese / English).
    const std::vector<std::wstring>& materialNamesJp() const { return materialNamesJp_; }
    const std::vector<std::wstring>& materialNamesEn() const { return materialNamesEn_; }

    // Pass-plan scratch (+0xec..+0x137), reset by MME_RebuildRenderPassPlan
    // [18005b9e0 L84-97].
    using PassPlanScratch = ObjectPlanState;
    PassPlanScratch& passPlanScratch() { return passPlanScratch_; }
    const PassPlanScratch& passPlanScratch() const { return passPlanScratch_; }

    void ResetPassPlanScratch();  // [18005b9e0 L84-97]

private:
    void BuildName();      // [0x180058c70 L80-103] basename+extension via _splitpath_s
    void BuildNameTable(); // [0x180058c70 L104-361] bones/morphs or pseudo-bones
    void BuildMaterialNames(); // [0x180058c70 L170-218 / L340-360]

    // --- original fields (offset comments per globals_structures.md section 2) ---
    IDirect3DDevice9*     device_;              // +0x10 (AddRef'd)
    unsigned long long    objectId_;            // +0x18
    const char*           filename_;            // +0x20 (borrowed, like the original)
    int                   kind_;                // +0x28
    int                   materialCount_;       // +0x38
    IUnknown*             reserved_;            // +0x40 (AddRef'd)
    std::string           name_;                // +0x48 (basename+extension; "(null)" fallback)
    std::map<std::string, int> nameToIndex_;    // +0x70
    std::vector<std::wstring> materialNamesJp_; // +0xa0 (fun_18005fea0 pushes wstrings)
    std::vector<std::wstring> materialNamesEn_; // +0xc0
    int                   cachedPerVertexValue_;// +0x3c (init -1)
    int                   drawTypeIndex_;      // +0xe8 (init -1)
    SasRunState*          runState_;            // +0x358 (0)
    unsigned char         postEffectSnapshotGate_ = 0;  // orig +0x3c byte
    unsigned char         postEffectFailedFlag_ = 0;    // orig +0x3d byte
    unsigned long long    unknownFlag360_;      // +0x360 (scriptClass when a
                                                //   scene effect is assigned)
    int                   renderClass_;         // +0x364 (scriptOrder; 0)
    unsigned char         flag368_;             // +0x368 (1)
    PassPlanScratch       passPlanScratch_;     // +0xec..+0x137
    RenderSnapshot        snapshot_;            // +0x138 (0x220 bytes)
    D3DMATRIX             planMatrixCopy_;      // +0x190 (FUN_180059aa0 copy)
    std::string           effectFile_;          // Phase 2: assigned .fx path
    bool                  shown_;               // Phase 2: EMM shown state
    std::map<int, std::string> subsetEffects_;      // Phase 3 seam: [n] rows
    std::map<std::string, std::string> subsetEffectsByName_;  // @name rows
    bool                  attached_;            // Phase 2: GetAcsAttachedPmd
    unsigned long long    attachedModelId_;     // Phase 2: attached model id
    int                   attachedBoneIndex_;   // Phase 2: attached bone index
};

} // namespace mme
