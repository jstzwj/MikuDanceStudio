#include "scene_render_fixture.hpp"
#include "native_mmd_queries.hpp"
#include "MMDExport.h"
#include <limits>

namespace {
unsigned checks = 0;
void Equal(const std::string& port, const std::string& native, const char* name) {
    ++checks;
    Check(port == native, name);
}
template <class T> void Equal(const T& port, const T& native, const char* name) {
    ++checks;
    if (std::memcmp(&port, &native, sizeof(T))) {
        std::fprintf(stderr, "query differs: %s\n", name);
        throw std::runtime_error(name);
    }
}
void CompareQueries(MMDApp& app, NativeMmdQueries& native) {
    native.Sync(app);
#define QUERY0(name) Equal(name(), native.Call<decltype(name())>(#name), #name)
#define QUERY1(name, index)                                                                        \
    Equal(name(index), native.Call<decltype(name(index))>(#name, index), #name)
    QUERY0(ExpGetPmdNum);
    QUERY0(ExpGetAcsNum);
    QUERY0(ExpGetPreAcsNum);
    QUERY0(ExpGetFrameTime);
    QUERY0(ExpGetCurrentObject);
    QUERY0(ExpGetCurrentMaterial);
    QUERY0(ExpGetCurrentTechnic);
    QUERY0(ExpGetRenderRepeatCount);
    QUERY0(ExpGetEnglishMode);
    for (int count : {-2, 0, 1, 7}) {
        const auto previous = app.state.renderPassCount;
        ExpSetRenderRepeatCount(count);
        native.Call<void>("ExpSetRenderRepeatCount", count);
        QUERY0(ExpGetRenderRepeatCount);
        app.state.renderPassCount = previous;
        native.Sync(app);
    }
    const auto compareId = [&](const char* name, void* port, int index) {
        const auto id = native.Call<std::uintptr_t>(name, index);
        // Both originals expose int IDs. Compare identity bits at that ABI;
        // retain the port's full pointers for its embedded engine consumers.
        Equal(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(port)), static_cast<DWORD>(id),
              name);
    };
    for (int i : {-1, 0, 1, 2, 254, 255, std::numeric_limits<int>::max()}) {
        compareId("ExpGetPmdID", ExpGetPmdID(i), i);
        compareId("ExpGetAcsID", ExpGetAcsID(i), i);
        QUERY1(ExpGetPmdOrder, i);
        QUERY1(ExpGetPmdMatNum, i);
        QUERY1(ExpGetPmdBoneNum, i);
        QUERY1(ExpGetPmdMorphNum, i);
        QUERY1(ExpGetPmdDisp, i);
        QUERY1(ExpGetAcsOrder, i);
        QUERY1(ExpGetAcsMatNum, i);
        QUERY1(ExpGetAcsDisp, i);
        QUERY1(ExpGetAcsX, i);
        QUERY1(ExpGetAcsY, i);
        QUERY1(ExpGetAcsZ, i);
        QUERY1(ExpGetAcsRx, i);
        QUERY1(ExpGetAcsRy, i);
        QUERY1(ExpGetAcsRz, i);
        QUERY1(ExpGetAcsSi, i);
        QUERY1(ExpGetAcsTr, i);
        for (bool accessory : {false, true}) {
            const char* p = accessory ? ExpGetAcsFilename(i) : ExpGetPmdFilename(i);
            const char* n =
                native.Call<char*>(accessory ? "ExpGetAcsFilename" : "ExpGetPmdFilename", i);
            Equal(p != nullptr, n != nullptr, "filename availability");
            if (p && n)
                Equal(std::string(p), std::string(n), "filename bytes");
        }
        for (int subset : {-1, 0, 1, 2, 3}) {
            const auto p = ExpGetPmdMaterial(i, subset);
            D3DMATERIAL9 n{};
            native.Call<D3DMATERIAL9*>("ExpGetPmdMaterial", &n, i, subset);
            Equal(p, n, "ExpGetPmdMaterial");
            const auto a = ExpGetAcsMaterial(i, subset);
            native.Call<D3DMATERIAL9*>("ExpGetAcsMaterial", &n, i, subset);
            Equal(a, n, "ExpGetAcsMaterial");
        }
        const auto a = ExpGetAcsWorldMat(i);
        D3DMATRIX n{};
        native.Call<D3DMATRIX*>("ExpGetAcsWorldMat", &n, i);
        Equal(a, n, "ExpGetAcsWorldMat");
        // Bone/morph element indices are unchecked in the originals: only
        // pass valid elements, or any element when the model is absent.
        const bool model = ExpGetPmdID(i) != nullptr;
        for (int j : {0, 1}) {
            if (!model || j < ExpGetPmdBoneNum(i)) {
                Equal(ExpGetPmdBoneName(i, j), native.Call<char*>("ExpGetPmdBoneName", i, j),
                      "bone name");
                const auto b = ExpGetPmdBoneWorldMat(i, j);
                native.Call<D3DMATRIX*>("ExpGetPmdBoneWorldMat", &n, i, j);
                Equal(b, n, "bone world matrix");
            }
            if (!model || j < ExpGetPmdMorphNum(i)) {
                Equal(ExpGetPmdMorphName(i, j), native.Call<char*>("ExpGetPmdMorphName", i, j),
                      "morph name");
                Equal(ExpGetPmdMorphValue(i, j), native.Call<float>("ExpGetPmdMorphValue", i, j),
                      "morph weight");
            }
        }
    }
#undef QUERY0
#undef QUERY1
}
int Run(const std::filesystem::path& original) {
    if (original.empty())
        return 0; // The always-on semantic checks remain in model_effect_query.
    OriginalPhysicsModule module(original);
    NativeMmdQueries native(module);
    auto app = std::make_unique<MMDApp>();
    auto renderer = std::make_unique<D3DRenderer>();
    app->Renderer() = renderer.get();
    g_Block = app.get();
    auto first = std::make_unique<mdl::ModelRecord>();
    auto last = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 2> bones{};
    std::array<mdl::MorphRecord, 2> morphs{};
    std::array<mdl::ModelMaterialRecord, 3> materials{};
    std::array<D3DMATERIAL9, 3> accessoryMaterials{};
    mdl::AccessoryRecord before{}, after{};
    for (auto* model : {first.get(), last.get()}) {
        model->boneTable = bones.data();
        model->boneCount = 2;
        model->morphs = morphs.data();
        model->morphCount = 2;
        model->materials = materials.data();
        model->materialCount = 2;
        model->loadComplete = 1;
        model->currentMaterial = 1;
    }
    for (unsigned i = 0; i < 3; ++i) {
        materials[i].diffuse[0] = float(i) + .25f;
        accessoryMaterials[i].Diffuse.r = float(i) + .75f;
    }
    for (int i = 0; i < 2; ++i) {
        D3DXMATRIX translation, rotation, result;
        D3DXMatrixTranslation(&translation, .3f + i, -.8f, 2.4f);
        D3DXMatrixRotationY(&rotation, .17f);
        D3DXMatrixMultiply(&result, &rotation, &translation);
        std::memcpy(bones[i].matInit, &result, sizeof result);
        bones[i].position[0] = .7f;
        bones[i].position[1] = -1.3f;
        bones[i].position[2] = 3.2f;
        morphs[i].value = .375f + i;
    }
    first->comboSelIndex = 2;
    last->comboSelIndex = 1;
    wcscpy_s(first->path, L"C:\\models\\first.pmx");
    wcscpy_s(last->path, L"C:\\models\\\x65E5\x672C\x8A9E.pmd");
    for (auto* accessory : {&before, &after}) {
        accessory->materials = accessoryMaterials.data();
        accessory->materialCount = 2;
        accessory->parentModel = 3;
        accessory->parentBone = 1;
        accessory->scale = .1234567f;
        accessory->position[0] = 1.7f;
        accessory->position[1] = -2.3f;
        accessory->rotation[0] = .4f;
        accessory->rotation[1] = -.7f;
        accessory->rotation[2] = 1.1f;
        accessory->opacity = .6f;
        accessory->visible = 1;
        accessory->currentMaterial = 2;
    }
    before.order = 0;
    after.order = 1;
    wcscpy_s(before.sourcePath, L"C:\\accessories\\\x53F0.x");
    wcscpy_s(after.sourcePath, L"C:\\accessories\\last.x");
    app->ModelSlot(3) = first.get();
    app->ModelSlot(kModelSlotCount - 1) = last.get();
    app->AccessorySlot(2) = &before;
    app->AccessorySlot(254) = &after;
    app->state.currentFrame = 37;
    app->state.englishUI = 1;
    // Keep active-pointer packing independent of pass/repeat values.
    for (int split : {0, 1, 2, 9}) {
        app->AccessoryRenderSplitOrder() = split;
        for (void* active : {static_cast<void*>(nullptr), static_cast<void*>(first.get()),
                             static_cast<void*>(last.get()), static_cast<void*>(&before),
                             static_cast<void*>(&after), static_cast<void*>(bones.data())}) {
            app->ActiveRenderObject() = active;
            app->state.activeRenderPass = 4;
            app->state.renderPassCount = 3;
            CompareQueries(*app, native);
        }
    }
    app->state.playbackActive = 1;
    app->state.playbackCursorSeconds = 1.234567f;
    CompareQueries(*app, native);
    for (int slot : {3, kModelSlotCount - 1})
        app->ModelSlot(slot) = nullptr;
    app->AccessorySlot(2) = app->AccessorySlot(254) = nullptr;
    app->ActiveRenderObject() = nullptr;
    CompareQueries(*app, native);
    g_Block = nullptr;
    std::printf("Original host queries: %u comparisons passed\n", checks);
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 1 || (argc == 3 && std::wstring(argv[1]) == L"--original"),
              "expected --original path");
        return Run(argc == 3 ? std::filesystem::absolute(argv[2]) : std::filesystem::path());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
