#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "pass_planner.h"
#include "sas_exec.h"
#include "material_bind.h"
#include "model_data.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <filesystem>
#include <fstream>

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"MME Ray script smoke test", WS_OVERLAPPED,
        0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
struct Runtime {
    explicit Runtime(IDirect3DDevice9* device) {
        mme::g_context = new mme::MmeContext(device);
        mme::MmeEngineInit(device, true);
    }
    ~Runtime() {
        mme::SasSetHostCallbacks({});
        delete mme::g_ownerManager;
        mme::g_ownerManager = nullptr;
        mme::MmeEngineTerm();
        if (mme::g_effectPool) { mme::g_effectPool->Release(); mme::g_effectPool = nullptr; }
        if (mme::g_offscreenSurface) {
            mme::g_offscreenSurface->Release(); mme::g_offscreenSurface = nullptr;
        }
        delete mme::g_context;
        mme::g_context = nullptr;
    }
};
}

int main() {
    Window window;
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window.handle || !d3d.p) return 77;
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window.handle;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    Com<IDirect3DDevice9> device;
    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.handle,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p))) return 77;
    Runtime runtime(device.p);
    mme::g_ownerManager = new mme::EffectOwnerManager();
    auto* ctx = mme::g_context;
    auto directory = std::filesystem::temp_directory_path() /
        ("mme_assignment_test_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    struct Files {
        std::filesystem::path directory;
        ~Files() {
            std::filesystem::remove(directory / "parent.fx");
            std::filesystem::remove(directory / "child.fx");
            std::filesystem::remove(directory);
        }
    } files{directory};
    auto writeEffect = [&](const char* file, const char* mapping) {
        std::ofstream out(directory / file);
        out << "float4x4 Self : CONTROLOBJECT < string name=\"(self)\"; >;\n"
            << "float4x4 Owner : CONTROLOBJECT < string name=\"(OffscreenOwner)\"; >;\n"
            << "texture SceneColor : OFFSCREENRENDERTARGET < int Width=16; int Height=16;"
            << " string Format=\"A8R8G8B8\"; string DefaultEffect=\"" << mapping << "\"; >;\n"
            << "technique T < string MMDPass=\"object\"; string Script=\"ClEaR=Color;Pass=P;\"; >"
            << " { pass P {} }\n";
        return (directory / file).string();
    };
    const auto childPath = writeEffect("child.fx", "*=none;");
    const auto parentPath = writeEffect("parent.fx", "leaf.pmx=child.fx;*=none;");
    auto addModel = [&](unsigned id, const char* name, float x) {
        auto* model = new mme::ModelData(device.p, id, name, 0, 2, nullptr);
        model->passPlanScratch().flag = 1;
        model->passPlanScratch().renderOrder = static_cast<int>(id);
        model->planMatrix().m[3][0] = x;
        ctx->models.push_back(model);
        return model;
    };
    auto* a = addModel(1, "a.pmx", 11);
    auto* b = addModel(2, "b.pmx", 22);
    auto* c = addModel(3, "c.pmx", 33);
    auto* leaf = addModel(4, "leaf.pmx", 44);
    a->setEffectFile(parentPath);
    b->setEffectFile(parentPath);
    c->setSubsetEffect(1, parentPath);
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::fprintf(stderr, "%s\n", message); }
    };
    mme::MmeSortPlanObjects(ctx, {{1,a},{2,b},{3,c},{4,leaf}});
    check(ctx->renderPassList.size() == 6,
        "Three assignments and their three recursively discovered child targets must render");
    if (ctx->renderPassList.size() != 6) return 1;
    std::set<unsigned int> ids;
    std::set<IDirect3DSurface9*> surfaces;
    std::vector<unsigned int> originalIds;
    auto selfX = [](mme::MaterialBinding* binding, const char* name) {
        D3DXMATRIX value{};
        binding->effect->GetMatrix(binding->effect->GetParameterByName(nullptr, name), &value);
        return value.m[3][0];
    };
    for (const auto& turn : ctx->renderPassList) {
        ids.insert(turn.turnId);
        originalIds.push_back(turn.turnId);
        surfaces.insert(turn.offscreen->surface);
        check(turn.assignment && turn.assignment->sas && !mme::SasHadErrors(turn.assignment->sas),
            "Mixed-case SAS commands must parse on every instance");
        check(turn.offscreen->name == "SceneColor", "Same-name targets must keep distinct identities");
    }
    check(ids.size() == 6 && surfaces.size() == 6, "Private targets must not alias across assignments");
    auto* ba = mme::MmeActiveModelBinding(a);
    auto* bb = mme::MmeActiveModelBinding(b);
    auto* bc = mme::MmeFindMaterialBinding(0,c,1,false);
    check(ba && bb && bc && ba->owner == bb->owner && bb->owner == bc->owner,
        "Assignments reuse one compiled-file cache entry");
    check(ba->effect != bb->effect && bb->effect != bc->effect,
        "D3DX effect instances must be independent");
    check(selfX(ba,"Self") == 11 && selfX(bb,"Self") == 22 && selfX(bc,"Self") == 33,
        "Base whole-object and subset self values must come from their assigned owner");
    for (size_t i=0; i<3; ++i) {
        const auto& parent = ctx->renderPassList[i];
        auto* child = mme::MmeResolveTurnEffectBinding(parent.turnId,
            *parent.offscreen, parent.carrier, leaf);
        check(child && selfX(child,"Self") == 44 &&
            selfX(child,"Owner") == parent.carrier->planMatrix().m[3][0],
            "Recursive assignments must carry their own self and OffscreenOwner");
    }
    mme::MmeSortPlanObjects(ctx, {{1,a},{2,b},{3,c},{4,leaf}});
    check(ctx->renderPassList.size() == originalIds.size(), "Rebuild must retain the complete graph");
    for (size_t i=0; i<ctx->renderPassList.size(); ++i)
        check(ctx->renderPassList[i].turnId == originalIds[i], "Assignment resource IDs must remain stable");
    // Refreshing a turn must not overwrite another assignment's controls.
    check(selfX(ba,"Self") == 11 && selfX(bb,"Self") == 22 && selfX(bc,"Self") == 33,
        "Offscreen resolution must preserve base assignment parameters");
    c->clearSubsetEffects();
    mme::MmeDropMaterialBindings(c);
    mme::MmeSortPlanObjects(ctx, {{1,a},{2,b},{3,c},{4,leaf}});
    check(ctx->renderPassList.size() == 4, "Removing a subset retires its target and its recursive child");
    auto previousSource = bb->owner;
    // The original compile branch supplies the first assignment directly.
    check(ba->instance == previousSource && bb->instance != previousSource,
        "Only the first assignment may use the compiled source instance");
    mme::MmeDropMaterialBindings(a);
    auto* rebound = mme::MmeResolveModelEffectBinding(a);
    check(rebound && rebound->owner == previousSource &&
        rebound->instance != previousSource && rebound->effect != bb->effect,
        "Reassignment after releasing the first binding must own a fresh instance");
    std::weak_ptr<mme::LoadedEffect> releasedClone = rebound->instance;
    mme::MmeDropMaterialBindings(a);
    check(releasedClone.expired(), "The instance registry must not retain released assignments");
    mme::MmeEngineClearCaches();
    auto* reloaded = mme::MmeResolveModelEffectBinding(a);
    check(reloaded && reloaded->owner != previousSource && selfX(bb,"Self") == 22,
        "Cache invalidation must not destroy still-bound instances");
    mme::MmeEngineOnLostDevice();
    check(previousSource->sas->resources.front().surface == nullptr,
        "Cache-retired sources must release their default-pool resources too");
    check(SUCCEEDED(mme::MmeEngineOnResetDevice(device.p)), "Instance device-resource recreation");
    check(bb->sas->resources.front().surface != nullptr,
        "An old source retained by a binding survives lost/reset after cache invalidation");
    std::weak_ptr<mme::LoadedEffect> finalSource = reloaded->owner;
    std::weak_ptr<mme::LoadedEffect> finalClone = bb->instance;
    previousSource.reset();
    mme::MmeEngineClearCaches();
    delete mme::g_ownerManager;
    mme::g_ownerManager = nullptr;
    check(finalSource.expired() && finalClone.expired(),
        "Source/instance ownership must not form a reference cycle");
    std::printf("Assignment graph/control isolation: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
