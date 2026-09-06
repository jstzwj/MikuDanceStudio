// model_data.cpp - see model_data.h
#include "model_data.h"

#include <cstdlib>
#include <cstring>

#include "MMDExport.h"   // ExpGetPmd* host exports (MikuMikuDance.exe)
#include "mmhack_api.h"  // GetMaterialName state query

#include "mme_util.h"

namespace mme {

// Accessory pseudo-bone table. The strings and indexes were byte-verified in
// the original PE at 0x1800b5928..0x1800b5950 (see the ctor decompile
// 180058c70 L222-339): X=0 Y=1 Z=2 XYZ=3 Rx=4 Ry=5 Rz=6 Rxyz=7 Si=8 Tr=9,
// with duplicate Rx/Ry/Rz inserts that overwrite the same entries (kept for
// behavioral fidelity of the map state).
struct AccessoryPseudoBone {
    const char* name;
    int         index;
};

static const AccessoryPseudoBone kAccessoryPseudoBones[] = {
    { "X",    0 },   // 0x1800b5928 (1 byte)
    { "Y",    1 },   // 0x1800b592c
    { "Z",    2 },   // 0x1800b5930
    { "XYZ",  3 },   // 0x1800b5934 (3 bytes)
    { "Rx",   4 },   // 0x1800b5938
    { "Ry",   5 },   // 0x1800b593c
    { "Rz",   6 },   // 0x1800b5940
    { "Rxyz", 7 },   // 0x1800b5944 (4 bytes)
    { "Rx",   4 },   // 0x1800b5938 duplicate insert [180058c70 L298]
    { "Ry",   5 },   // 0x1800b593c duplicate insert [L307]
    { "Rz",   6 },   // 0x1800b5940 duplicate insert [L316]
    { "Si",   8 },   // 0x1800b594c
    { "Tr",   9 },   // 0x1800b5950
};

ModelData::ModelData(IDirect3DDevice9* device,
                     unsigned long long objectId,
                     const char* filename,
                     int kind,
                     unsigned int materialCount,
                     IUnknown* reservedObject)
    : device_(device)
    , objectId_(objectId)
    , filename_(filename)
    , kind_(kind)
    , materialCount_(static_cast<int>(materialCount))
    , reserved_(nullptr)
    , cachedPerVertexValue_(-1)          // [180058c70 L77] +0x3c = -1
    , drawTypeIndex_(-1)                 // [L76] +0xe8 = -1
    , field358_(0)                       // [L363] +0x358 = 0
    , unknownFlag360_(0)
    , renderClass_(0)                    // +0x364
    , flag368_(1)                        // [L79] +0x368 = 1
    , shown_(true)                       // Phase 2: visible until an EMM row hides it
    , attached_(false)                   // Phase 2: GetAcsAttachedPmd resolution
    , attachedModelId_(0)
    , attachedBoneIndex_(-1)
{
    // [L42-45] device AddRef.
    if (device_ != nullptr) {
        device_->AddRef();
    }

    // [L48] +0x40 reserved object.
    // [L66-74] swap the reserved object when it differs from the current one:
    // AddRef the incoming pointer, Release the outgoing one.
    if (reservedObject != reserved_) {
        if (reservedObject != nullptr) {
            reservedObject->AddRef();
        }
        if (reserved_ != nullptr) {
            reserved_->Release();
        }
        reserved_ = reservedObject;
    }

    BuildName();
    BuildNameTable();
    BuildMaterialNames();

    // [L362] +0xec = 0 (the rest of the pass-plan scratch is reset per frame
    // by MME_RebuildRenderPassPlan).
    passPlanScratch_.state0 = 0;
    memset(&planMatrixCopy_, 0, sizeof(planMatrixCopy_));   // +0x190 copy
}

ModelData::~ModelData()
{
    // [0x1800595a0] MME_ModelData_Destructor: the original releases the name
    // map/vector/string storage, then the reserved COM object (+0x40) and the
    // device (+0x10). std containers release themselves here.
    if (reserved_ != nullptr) {
        reserved_->Release();      // [L29-31]
        reserved_ = nullptr;
    }
    if (device_ != nullptr) {
        device_->Release();        // [L32-34]
        device_ = nullptr;
    }
}

void ModelData::BuildName()
{
    // [0x180058c70 L80-103] _splitpath_s(filename, NULL, 0, NULL, 0, fname,
    // 0x100, ext, 0x100); name = fname + ext; on failure the name is "(null)"
    // (0x1800b4e60, byte-verified).
    char fname[256];
    char ext[256];
    errno_t err = _splitpath_s(filename_ != nullptr ? filename_ : "",
                               nullptr, 0, nullptr, 0, fname, sizeof(fname), ext, sizeof(ext));
    if (err == 0) {
        name_ = fname;
        name_ += ext;
    } else {
        name_ = "(null)";
    }
}

void ModelData::BuildNameTable()
{
    // [0x180058c70 L104-338]
    if (kind_ == 1) {
        // PMD/PMX model: find the host model index whose ExpGetPmdID matches
        // this object id, then register bone names (0..n-1) and morph names
        // (-1, -2, ...) into the name map [L105-168].
        int modelCount = ExpGetPmdNum();
        for (int i = 0; i < modelCount; ++i) {
            unsigned long long hostId =
                reinterpret_cast<unsigned long long>(ExpGetPmdID(i));
            if (hostId != static_cast<unsigned int>(objectId_)) {
                continue;
            }
            if (i >= 0) {
                int boneCount = ExpGetPmdBoneNum(i);
                for (int b = 0; b < boneCount; ++b) {
                    const char* boneName = ExpGetPmdBoneName(i, b);
                    // [L117-130] the original assigns a std::string then
                    // FindOrCreate-inserts the index (FUN_1800608f0).
                    nameToIndex_[boneName != nullptr ? boneName : ""] = b;
                }
                int morphCount = ExpGetPmdMorphNum(i);
                int morphIndex = -1;                       // [L140]
                for (int m = 0; m < morphCount; ++m) {
                    const char* morphName = ExpGetPmdMorphName(i, m);
                    nameToIndex_[morphName != nullptr ? morphName : ""] = morphIndex;
                    --morphIndex;                          // [L161]
                }
            }
            break;                                          // [L165]
        }
    } else {
        // Accessory: register the fixed pseudo-bone table (see the static
        // table above; the duplicate Rx/Ry/Rz rows are in the original).
        for (size_t i = 0; i < sizeof(kAccessoryPseudoBones) / sizeof(kAccessoryPseudoBones[0]); ++i) {
            nameToIndex_[kAccessoryPseudoBones[i].name] = kAccessoryPseudoBones[i].index;
        }
    }
}

void ModelData::BuildMaterialNames()
{
    // [0x180058c70 L170-218 (model) / L340-360 (accessory)].
    // For PMD models: GetMaterialName(objectId, i, 0/1) wide strings (the
    // original scans 2-byte chars and FUN_180006480 assigns wstrings; the
    // fallback pointer 0x1800b26a8 is an empty wide string). For accessories:
    // empty wstrings per material.
    static const wchar_t kEmptyName[] = L"";
    for (int i = 0; i < materialCount_; ++i) {
        if (kind_ == 1) {
            const wchar_t* jp = GetMaterialName(objectId_, static_cast<unsigned long>(i), 0);
            materialNamesJp_.push_back(jp != nullptr ? jp : kEmptyName);
            const wchar_t* en = GetMaterialName(objectId_, static_cast<unsigned long>(i), 1);
            materialNamesEn_.push_back(en != nullptr ? en : kEmptyName);
        } else {
            materialNamesJp_.push_back(kEmptyName);
            materialNamesEn_.push_back(kEmptyName);
        }
    }
}

const int* ModelData::findNameIndex(const std::string& name) const
{
    std::map<std::string, int>::const_iterator it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) {
        return nullptr;
    }
    return &it->second;
}

void ModelData::ResetPassPlanScratch()
{
    // [0x18005b9e0 L84-97] per-model scratch reset performed by
    // MME_RebuildRenderPassPlan: +0xec = 0, +0xf0 = -1, +0xf4 = 0,
    // zero 0xf8..0x137 then set +0xf8/+0x10c/+0x120/+0x134 = 1.0f.
    passPlanScratch_.state0 = 0;
    passPlanScratch_.passKey = -1;
    passPlanScratch_.flag = 0;
    memset(&passPlanScratch_.color0, 0, sizeof(passPlanScratch_.color0));
    memset(&passPlanScratch_.color1, 0, sizeof(passPlanScratch_.color1));
    memset(&passPlanScratch_.color2, 0, sizeof(passPlanScratch_.color2));
    memset(&passPlanScratch_.color3, 0, sizeof(passPlanScratch_.color3));
    passPlanScratch_.color0[0] = 1.0f;   // +0xf8
    passPlanScratch_.color1[0] = 1.0f;   // +0x10c
    passPlanScratch_.color2[0] = 1.0f;   // +0x120
    passPlanScratch_.color3[0] = 1.0f;   // +0x134
}

} // namespace mme
