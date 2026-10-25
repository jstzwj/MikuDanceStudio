#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

extern "C" {
int ExpGetPmdNum();
int ExpGetPmdOrder(int);
int ExpGetPmdMatNum(int);
float* ExpGetPmdMaterial(float*, int, int);
int ExpGetPmdBoneNum(int);
char* ExpGetPmdBoneName(int, int);
float* ExpGetPmdBoneWorldMat(float*, int, int);
int ExpGetPmdMorphNum(int);
char* ExpGetPmdMorphName(int, int);
float ExpGetPmdMorphValue(int, int);
int ExpGetPmdDisp(int);
void* ExpGetPmdID(int);
int ExpGetAcsOrder(int);
float* ExpGetAcsWorldMat(float*, int);
int ExpGetCurrentObject();
int ExpGetCurrentMaterial();
int ExpGetCurrentTechnic();
const wchar_t* MmdModelPathW(int);
}

using namespace mikudancestudio;
namespace {
int failures;
void Check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
void Identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1;
}
}

int main() {
    auto app = std::make_unique<MMDApp>();
    auto first = std::make_unique<mdl::ModelRecord>();
    auto last = std::make_unique<mdl::ModelRecord>();
    std::memset(&app->state, 0, sizeof app->state);
    g_Block = app.get();
    app->ModelSlot(3) = first.get();
    app->ModelSlot(kModelSlotCount - 1) = last.get();
    first->comboSelIndex = 2;
    last->comboSelIndex = 1;
    app->AccessoryRenderSplitOrder() = 1;
    mdl::AccessoryRecord before{}, after{};
    before.order = 0;
    after.order = 1;
    app->AccessorySlot(2) = &before;
    app->AccessorySlot(254) = &after;
    Check(ExpGetPmdNum() == 2 && ExpGetPmdID(0) == first.get() && ExpGetPmdID(1) == last.get() &&
              ExpGetPmdID(-1) == nullptr && ExpGetPmdID(2) == nullptr,
          "queries enumerate occupied slots, including the architecture's last slot");
    Check(ExpGetPmdOrder(0) == 3 && ExpGetPmdOrder(1) == 2 && ExpGetPmdOrder(2) == 0 &&
              ExpGetAcsOrder(0) == -1 && ExpGetAcsOrder(1) == 4,
          "draw order is independent of occupied-slot enumeration and includes accessories");
    app->AccessoryRenderSplitOrder() = 9;
    Check(ExpGetPmdOrder(1) == 3, "model order offset clamps to occupied accessory count");
    app->AccessoryRenderSplitOrder() = 1;

    std::array<mdl::ModelMaterialRecord, 3> materials{};
    last->materials = materials.data();
    last->materialCount = 2;
    // The reference admits index == count. Allocate a real extra record to
    // check this boundary without reading outside the fixture allocation.
    for (unsigned i = 0; i < materials.size(); ++i)
        materials[i].diffuse[0] = static_cast<float>(10 + i);
    float material[17];
    Check(ExpGetPmdMatNum(1) == 2 && ExpGetPmdMaterial(material, 1, 1) == material &&
              material[0] == 11,
          "material queries use the full runtime-record stride");
    ExpGetPmdMaterial(material, 1, 2);
    Check(material[0] == 12, "material query preserves the original inclusive upper boundary");
    const float zero[17]{};
    for (int index : {-1, 3}) {
        ExpGetPmdMaterial(material, 1, index);
        Check(std::memcmp(material, zero, sizeof zero) == 0,
              "unsigned material bounds reject negative and larger indices");
    }
    ExpGetPmdMaterial(material, 7, 0);
    Check(std::memcmp(material, zero, sizeof zero) == 0, "missing model zeroes material output");

    std::array<mdl::BoneRecord, 2> bones{};
    std::array<mdl::MorphRecord, 2> morphs{};
    last->boneTable = bones.data();
    last->boneCount = 2;
    last->morphs = morphs.data();
    last->morphCount = 2;
    last->loadComplete = 1;
    strcpy_s(bones[1].name, "last bone");
    strcpy_s(morphs[1].name, "last morph");
    morphs[1].value = 0.375f;
    wcscpy_s(last->path, L"C:\\models\\last.pmx");
    Check(ExpGetPmdBoneNum(1) == 2 && ExpGetPmdBoneName(1, 1) == bones[1].name &&
              ExpGetPmdMorphNum(1) == 2 && ExpGetPmdMorphName(1, 1) == morphs[1].name &&
              ExpGetPmdMorphValue(1, 1) == 0.375f && ExpGetPmdDisp(1) == 1 &&
              MmdModelPathW(1) == last->path,
          "bone, morph, display and path queries access the architecture-correct members");
    Identity(bones[1].matInit);
    bones[1].matInit[0] = 0;
    bones[1].matInit[1] = 1;
    bones[1].matInit[4] = -1;
    bones[1].matInit[5] = 0;
    bones[1].matInit[12] = 7;
    bones[1].matInit[13] = 11;
    bones[1].position[0] = 2;
    bones[1].position[1] = 3;
    bones[1].position[2] = 5;
    float world[16];
    Check(ExpGetPmdBoneWorldMat(world, 1, 1) == world && world[12] == 4 && world[13] == 13 &&
              world[14] == 5,
          "bone world query returns T(rest) times matInit in row-vector order");
    float identity[16];
    Identity(identity);
    ExpGetPmdBoneWorldMat(world, 8, 0);
    Check(std::memcmp(world, identity, sizeof world) == 0, "missing bone model returns identity");
    after.parentModel = kModelSlotCount - 1;
    after.parentBone = 1;
    after.scale = 0.1f;
    after.position[0] = 1;
    ExpGetAcsWorldMat(world, 1);
    Check(world[12] == 4 && world[13] == 14 && world[14] == 5,
          "accessory query resolves its parent by actual model slot before composing matrices");

    first->currentMaterial = 4;
    last->currentMaterial = 7;
    after.currentMaterial = 9;
    for (auto* model : {first.get(), last.get()}) {
        app->ActiveRenderObject() = model;
        Check(ExpGetCurrentObject() == model->comboSelIndex + 1 &&
                  ExpGetCurrentMaterial() == static_cast<int>(model->currentMaterial),
              "current render queries use the active model identity and material cursor");
    }
    app->ActiveRenderObject() = &before;
    Check(ExpGetCurrentObject() == -1, "pre-model accessory uses negative current order");
    app->ActiveRenderObject() = &after;
    Check(ExpGetCurrentObject() == 4 && ExpGetCurrentMaterial() == 9,
          "post-model accessory uses its own material cursor");
    app->ActiveRenderObject() = world;
    Check(ExpGetCurrentObject() == 0 && ExpGetCurrentMaterial() == -1,
          "unmatched active object has no current order or material");
    app->ActiveRenderObject() = nullptr;
    Check(ExpGetCurrentObject() == 0 && ExpGetCurrentMaterial() == -1,
          "idle render queries preserve sentinel values");
    app->ActiveRenderPass() = AccessoryRenderPass::ModelOutline;
    Check(ExpGetCurrentTechnic() == static_cast<int>(AccessoryRenderPass::ModelOutline),
          "technique query preserves the frame pass code");
    g_Block = nullptr;
    return failures ? 1 : 0;
}
