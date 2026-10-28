#include "scene_render_fixture.hpp"
#include "native_mmd_queries.hpp"
#include "native_mmh_oracle.hpp"
#include "mme_host_api.h"
#include "mmhack_api.h"
#include "mmhack_state.h"
#include "mikudancestudio/mme_bridge.hpp"
#include <sstream>
#include <iomanip>

extern "C" const wchar_t* MmdModelPathW(int);
extern "C" const wchar_t* MmdAcsPathW(int);

namespace {
using NativeId = std::conditional_t<sizeof(void*) == 8, unsigned long long, unsigned>;
NativeMmhOracle* oracle = nullptr;
bool native = false;
bool pmxScene = false;
constexpr DWORD kManagedMesh = 0x220; // D3DXMESH_VB_MANAGED | D3DXMESH_IB_MANAGED
std::vector<std::string> events;
ID3DXEffect* suppliedEffect = nullptr;
IDirect3DBaseTexture9* stableTextures[2]{};
DWORD modelIdentity = 0, accessoryIdentity = 0;
int expectedObject = 0, expectedSubset = -1, expectedPass = 0;
int Identity(unsigned long long id) {
    const auto bits = static_cast<DWORD>(id);
    if (!bits)
        return 0;
    if (bits == modelIdentity)
        return 1;
    Check(bits == accessoryIdentity, "unknown callback object identity");
    return 2;
}
int TextureIdentity(IDirect3DBaseTexture9* texture) {
    if (!texture)
        return 0;
    if (texture == stableTextures[0])
        return 1;
    Check(texture == stableTextures[1], "unknown callback texture identity");
    return 2;
}

template <class R> R Query(const char* name, R(__cdecl* port)()) {
    return native ? oracle->Export<R(WINAPI*)()>(name)() : port();
}
bool Flag(const char* name, BOOL(__cdecl* port)()) {
    return native ? oracle->Export<bool(WINAPI*)()>(name)() : port() != FALSE;
}
void Snapshot(const char* event) {
    std::ostringstream line;
    const int object = Identity(Query("GetCurrentModelID", GetCurrentModelID));
    const int subset = Query("GetCurrentSubsetIndex", GetCurrentSubsetIndex);
    const int pass = Query("GetCurrentDrawType", GetCurrentDrawType);
    Check(object == expectedObject && subset == expectedSubset && pass == expectedPass,
          "callback object/subset/pass contract");
    Check(Query("GetCurrentEffect", GetCurrentEffect) == suppliedEffect,
          "standard effect remains registered");
    line << event << " id=" << object << " subset=" << subset << " pass=" << pass
         << " blend=" << Query("GetBlendMode", GetBlendMode)
         << " sphere=" << int(Query("GetSphereMapMode", GetSphereMapMode))
         << " toon=" << Flag("IsToonUsed", IsToonUsed)
         << " effect=" << Flag("IsEffectFileUsed", IsEffectFileUsed)
         << " edit=" << Flag("IsEditMode", IsEditMode)
         << " frame=" << Query("GetCurrentFrameTime", GetCurrentFrameTime)
         << " tex=" << TextureIdentity(Query("GetTexture", GetTexture))
         << " sph=" << TextureIdentity(Query("GetSphereMapTexture", GetSphereMapTexture));
    D3DMATRIX matrices[3]{};
    if (native)
        oracle->Export<void(WINAPI*)(D3DMATRIX*, D3DMATRIX*, D3DMATRIX*)>("GetLightViewProjMatrix")(
            matrices, matrices + 1, matrices + 2);
    else
        GetLightViewProjMatrix(matrices, matrices + 1, matrices + 2);
    line << " matrices=" << std::hex << std::setfill('0');
    for (const auto& matrix : matrices) {
        std::array<DWORD, 16> bits;
        std::memcpy(bits.data(), &matrix, sizeof matrix);
        for (DWORD value : bits)
            line << std::setw(8) << value;
    }
    events.push_back(line.str());
}
void Created(unsigned long long id, const char* filename, int kind, unsigned long materials) {
    events.push_back("create " + std::to_string(Identity(id)) + " " +
                     std::filesystem::path(filename).filename().string() + " " +
                     std::to_string(kind) + " " + std::to_string(materials));
}
void Deleted(unsigned long long id) {
    events.push_back("delete " + std::to_string(Identity(id)));
}

int WINAPI NInitialize(IDirect3DDevice9*) {
    events.push_back("initialize");
    return 0;
}
void WINAPI NBegin(IDirect3DDevice9*) {
    events.push_back("begin");
}
void WINAPI NEnd(IDirect3DDevice9*) {
    events.push_back("end");
}
void WINAPI NCleanup(IDirect3DDevice9*) {
    events.push_back("cleanup");
}
void WINAPI NLost(IDirect3DDevice9*) {
    events.push_back("lost");
}
void WINAPI NReset(IDirect3DDevice9*) {
    events.push_back("reset");
}
void WINAPI NCreate(IDirect3DDevice9*, NativeId id, const char* filename, int kind,
                    unsigned long materials, void*, IUnknown*) {
    Created(id, filename, kind, materials);
}
void WINAPI NDelete(IDirect3DDevice9*, NativeId id) {
    Deleted(id);
}
HRESULT WINAPI NIndexed(IDirect3DDevice9*, D3DPRIMITIVETYPE type, int base, UINT min, UINT vertices,
                        UINT start, UINT count) {
    Snapshot("indexed");
    events.back() += " geometry=" + std::to_string(type) + "," + std::to_string(base) + "," +
                     std::to_string(min) + "," + std::to_string(vertices) + "," +
                     std::to_string(start) + "," + std::to_string(count);
    return E_FAIL;
}
HRESULT WINAPI NPrimitive(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT) {
    Snapshot("primitive");
    return E_FAIL;
}
HRESULT WINAPI NClear(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD flags, D3DCOLOR color, float,
                      DWORD, int main) {
    events.push_back("clear " + std::to_string(flags) + " " + std::to_string(color) + " " +
                     std::to_string(main));
    return E_FAIL;
}
HRESULT WINAPI SupplyEffect(IDirect3DDevice9*, HMODULE, const char*, const D3DXMACRO*,
                            ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect** output,
                            ID3DXBuffer**) {
    *output = suppliedEffect;
    suppliedEffect->AddRef();
    return S_OK;
}

void WriteMesh(const std::filesystem::path& path) {
    std::ofstream(path) << R"X(xof 0303txt 0032
Mesh {6;
 0.0;0.0;0.0;,1.0;0.0;0.0;,0.0;1.0;0.0;,
 1.0;0.0;0.0;,2.0;0.0;0.0;,1.0;1.0;0.0;;
 2;3;0,2,1;,3;3,5,4;;
 MeshTextureCoords {6;0.0;0.0;,1.0;0.0;,0.0;1.0;,0.0;0.0;,1.0;0.0;,0.0;1.0;;}
 MeshMaterialList {2;2;0,1;;
  Material {1.0;0.3;0.1;1.0;;4.0;0.0;0.0;0.0;;0.1;0.1;0.1;;}
  Material {0.2;0.7;0.5;0.4;;8.0;0.1;0.2;0.3;;0.1;0.1;0.1;;}
 }
})X";
}
const wchar_t* ModelPath(int index) {
    return MmdModelPathW(index);
}
const wchar_t* AccessoryPath(int index) {
    return MmdAcsPathW(index);
}

void ConfigureNative(NativeMmhOracle& mmh, HWND window) {
    const std::size_t callbacks[] = {
        sizeof(void*) == 8 ? 0x6E7E8u : 0x51510u, sizeof(void*) == 8 ? 0x6E7F0u : 0x51514u,
        sizeof(void*) == 8 ? 0x6E7F8u : 0x51518u, sizeof(void*) == 8 ? 0x6E800u : 0x5151Cu,
        sizeof(void*) == 8 ? 0x6E808u : 0x51520u, sizeof(void*) == 8 ? 0x6E810u : 0x51524u,
        sizeof(void*) == 8 ? 0x6E818u : 0x51528u, sizeof(void*) == 8 ? 0x6E820u : 0x5152Cu,
        sizeof(void*) == 8 ? 0x6E828u : 0x51530u, sizeof(void*) == 8 ? 0x6E830u : 0x51534u,
        sizeof(void*) == 8 ? 0x6E838u : 0x51538u};
    const FARPROC functions[] = {
        reinterpret_cast<FARPROC>(NInitialize), reinterpret_cast<FARPROC>(NCreate),
        reinterpret_cast<FARPROC>(NDelete),     reinterpret_cast<FARPROC>(NBegin),
        reinterpret_cast<FARPROC>(NEnd),        reinterpret_cast<FARPROC>(NPrimitive),
        reinterpret_cast<FARPROC>(NIndexed),    reinterpret_cast<FARPROC>(NCleanup),
        reinterpret_cast<FARPROC>(NLost),       reinterpret_cast<FARPROC>(NReset),
        reinterpret_cast<FARPROC>(NClear)};
    for (int i = 0; i < 11; ++i)
        mmh.Set(callbacks[i], functions[i]);
    mmh.Set(sizeof(void*) == 8 ? 0x6E7E0 : 0x51508, window);
    mmh.Set(sizeof(void*) == 8 ? 0x6E7C0 : 0x514FC, window);
}

void CheckNames(mdl::ModelRecord& model) {
    for (unsigned subset : {0u, 1u, 1000u})
        for (int kind : {0, 1, 3}) {
            const wchar_t* name =
                native ? oracle->Export<const wchar_t*(WINAPI*)(NativeId, unsigned, int)>(
                             "GetMaterialName")(
                             static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&model)), subset,
                             kind)
                       : GetMaterialName(reinterpret_cast<std::uintptr_t>(&model), subset, kind);
            std::string text = name ? "" : "null";
            if (name)
                for (const wchar_t* p = name; *p; ++p)
                    text += std::to_string(unsigned(*p)) + ",";
            events.push_back("name " + std::to_string(subset) + " " + std::to_string(kind) + " " +
                             text);
        }
}

void Exercise(Fixture& f, mdl::ModelRecord& model, mdl::AccessoryRecord& accessory,
              ID3DXEffect& effect, NativeMmdQueries* queries, IDirect3DDevice9* wrapped) {
    auto& app = *f.app;
#if defined(_M_IX86)
    // The mapped VC9 CRT starts after device creation in this fixture. Real
    // MMD loads it before CreateDevice, which establishes 24-bit x87 precision.
    unsigned control;
    _controlfp_s(&control, _PC_24, _MCW_PC);
#endif
    D3DXMATRIX initialWorld, initialView;
    D3DXMatrixTranslation(&initialWorld, 1, 2, 3);
    D3DXMatrixRotationY(&initialView, .7f);
    Hr(f.device->SetTransform(D3DTS_WORLD, &initialWorld), "initial world");
    Hr(f.device->SetTransform(D3DTS_VIEW, &initialView), "initial view");
    D3DLIGHT9 initialLight{};
    initialLight.Type = D3DLIGHT_DIRECTIONAL;
    initialLight.Direction = {.3f, -.8f, .2f};
    Hr(f.device->SetLight(0, &initialLight), "initial light");
    const auto begin = [&] {
        if (queries)
            queries->Sync(app);
        Hr(native ? wrapped->BeginScene() : MmeHostBeginScene(f.device, 0), "host begin");
    };
    const auto end = [&] {
        Hr(native ? wrapped->EndScene() : MmeHostEndScene(f.device), "host end");
    };
    const auto clear = [&](DWORD flags) {
        Check((native ? wrapped->Clear(0, nullptr, flags, 0xFF010203, 1, 0)
                      : MmeHostClear(f.device, 0, nullptr, flags, 0xFF010203, 1, 0)) == E_FAIL,
              "host preserves clear callback HRESULT");
    };
    auto* textures = stableTextures;
    begin();
    clear(D3DCLEAR_TARGET);
    clear(D3DCLEAR_ZBUFFER);
    CheckNames(model);
    for (void* object :
         {static_cast<void*>(&model), static_cast<void*>(&accessory), static_cast<void*>(nullptr)})
        for (int pass : {5, 1, 2, 4, 3})
            for (int mode = 0; mode < 5; ++mode) {
                app.ActiveRenderObject() = object;
                app.state.activeRenderPass = pass;
                model.currentMaterial = accessory.currentMaterial = mode % 2;
                expectedObject = object == &model ? 1 : object == &accessory ? 2 : 0;
                expectedSubset = object ? mode % 2 : -1;
                expectedPass = object ? pass : 0;
                if (queries)
                    queries->Sync(app);
                Hr(f.device->SetVertexShader(nullptr), "fixed function path");
                Hr(f.device->SetRenderState(D3DRS_DESTBLEND,
                                            mode % 2 ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA),
                   "blend mode");
                for (DWORD stage = 0; stage < 3; ++stage) {
                    Hr(f.device->SetTexture(stage, textures[stage == 2 ? 1 : 0]),
                       "bind tracked texture");
                    Hr(f.device->SetTextureStageState(
                           stage, D3DTSS_COLOROP,
                           stage == 2 ? (mode == 2 ? D3DTOP_ADD : D3DTOP_MODULATE)
                                      : D3DTOP_MODULATE),
                       "texture cascade");
                    Hr(f.device->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS,
                                                      stage == 2 && mode ? D3DTTFF_COUNT2
                                                                         : D3DTTFF_DISABLE),
                       "sphere transform");
                    Hr(f.device->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX,
                                                      mode == 3 ? 0 : D3DTSS_TCI_CAMERASPACENORMAL),
                       "sphere coordinates");
                }
                Hr(native
                       ? wrapped->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1)
                       : MmeHostDrawIndexedPrimitive(f.device, D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1),
                   "intercepted indexed draw");
            }
    expectedObject = expectedPass = 0;
    expectedSubset = -1;
    Hr(native ? wrapped->DrawPrimitive(D3DPT_LINELIST, 0, 1)
              : MmeHostDrawPrimitive(f.device, D3DPT_LINELIST, 0, 1),
       "intercepted primitive");
    for (const auto& technique : g_tecTable) {
        const auto handle = effect.GetTechniqueByName(technique.name);
        Check(handle != nullptr, "standard technique exists");
        Hr(effect.SetTechnique(handle), "select tracked technique");
        for (BOOL additive : {FALSE, TRUE}) {
            Hr(effect.SetBool("spadd", additive), "additive sphere parameter");
            UINT passes = 0;
            Hr(effect.Begin(&passes, 0), "standard effect begin");
            Check(passes > 0, "standard effect passes");
            Hr(effect.BeginPass(0), "standard effect pass");
            for (DWORD stage = 0; stage < 3; ++stage)
                Hr(f.device->SetTexture(stage, textures[stage == 2 ? 1 : 0]),
                   "shader tracked texture");
            for (void* object : {static_cast<void*>(&model), static_cast<void*>(&accessory)})
                for (int pass : {5, 1, 2, 4, 3}) {
                    app.ActiveRenderObject() = object;
                    app.state.activeRenderPass = pass;
                    expectedObject = object == &model ? 1 : 2;
                    expectedSubset = 0;
                    expectedPass = pass;
                    if (queries)
                        queries->Sync(app);
                    Hr(native ? wrapped->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1)
                              : MmeHostDrawIndexedPrimitive(f.device, D3DPT_TRIANGLELIST, 0, 0, 3,
                                                            0, 1),
                       "shader intercepted draw");
                }
            Hr(effect.EndPass(), "standard effect end pass");
            Hr(effect.End(), "standard effect end");
        }
    }
    // Real D3DX mesh dispatch, including the original CloneMeshFVF wrapper.
    Hr(f.device->SetVertexShader(nullptr), "mesh fixed function path");
    app.ActiveRenderObject() = &accessory;
    for (int pass : {5, 1, 2, 4, 3})
        for (unsigned subset = 0; subset < 2; ++subset) {
            app.state.activeRenderPass = pass;
            accessory.currentMaterial = subset;
            expectedObject = 2;
            expectedSubset = subset;
            expectedPass = pass;
            if (queries)
                queries->Sync(app);
            const auto before = events.size();
            Hr(native ? static_cast<ID3DXMesh*>(accessory.mesh)->DrawSubset(subset)
                      : mikudancestudio::mme::DrawAccessorySubset(&app, &accessory, subset),
               "real mesh subset draw");
            Check(events.size() == before + 1, "real subset reaches engine exactly once");
        }
    end();
    for (int cycle = 0; cycle < 3; ++cycle) {
        app.ModelSlot(0) = cycle == 0 ? nullptr : &model;
        app.AccessorySlot(0) = cycle == 1 ? nullptr : &accessory;
        begin();
        end();
    }
    app.ModelSlot(0) = &model;
    app.AccessorySlot(0) = &accessory;
    app.ActiveRenderObject() = nullptr;
    // Exercise actual native Reset dispatch, including the failure path. The
    // callbacks are observers; the effect's own resources are managed here.
    for (bool invalid : {true, false}) {
        Hr(effect.OnLostDevice(), "standard effect lost");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = f.window;
        pp.BackBufferWidth = pp.BackBufferHeight = 96u;
        if (invalid)
            pp.SwapEffect = static_cast<D3DSWAPEFFECT>(0x7FFFFFFF);
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        HRESULT result;
        if (native)
            result = wrapped->Reset(&pp);
        else {
            MmeHostOnLostDevice(f.device);
            result = f.device->Reset(&pp);
            MmeHostOnResetDevice(f.device);
        }
        Check(invalid ? FAILED(result) : SUCCEEDED(result), "expected Reset outcome");
        events.push_back("reset result " + std::to_string(result));
        Hr(effect.OnResetDevice(), "standard effect reset");
    }
    Hr(f.device->SetTransform(D3DTS_WORLD, &initialWorld), "restored world");
    begin();
    end();
}

int Run(const std::filesystem::path& original, const std::filesystem::path& mmhack) {
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "host device caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF || (caps.PixelShaderVersion >> 8 & 255) < 3)
        return 77;
    auto& app = *f.app;
    app.state.currentFrame = 37;
    std::unique_ptr<OriginalPhysicsModule> module;
    std::unique_ptr<NativeMmdQueries> queries;
    std::unique_ptr<NativeMmhOracle> mmh;
    IDirect3DDevice9* wrapped = nullptr;
    if (!original.empty()) {
        module = std::make_unique<OriginalPhysicsModule>(original);
        queries = std::make_unique<NativeMmdQueries>(*module);
        mmh = std::make_unique<NativeMmhOracle>(mmhack, *queries);
        oracle = mmh.get();
        ConfigureNative(*mmh, f.window);
        wrapped = mmh->Device(f.device);
        f.renderer->device = wrapped;
    }
    complexScene = pmxScene;
    auto& model = f.LoadModel(0, 0);
    modelIdentity = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&model));
    f.renderer->device = f.device;
    const auto meshPath = f.directory / "fixture.x";
    WriteMesh(meshPath);
    Com<ID3DXMesh> mesh;
    Com<ID3DXBuffer> materials;
    DWORD count = 0;
    Hr(D3DXLoadMeshFromXW(meshPath.c_str(), kManagedMesh, f.device, nullptr, materials.Out(),
                          nullptr, &count, mesh.Out()),
       "real accessory mesh");
    Check(count == 2, "real mesh has two materials");
    mdl::AccessoryRecord accessory{};
    accessoryIdentity = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&accessory));
    accessory.mesh = mesh.p;
    accessory.order = 0;
    accessory.parentModel = -1;
    accessory.scale = 1;
    accessory.materialCount = 2;
    wcscpy_s(accessory.sourcePath, (f.directory / "fixture.x").c_str());
    app.AccessorySlot(0) = &accessory;
    struct ClearSlot {
        MMDApp& app;
        ~ClearSlot() { app.AccessorySlot(0) = nullptr; }
    } clearSlot{app};
    if (mmh) {
        mmh->RegisterModel(model);
        mmh->RegisterObject(&accessory.mesh, accessory.sourcePath);
        queries->Sync(app);
        const DWORD namePrefix = 7;
        std::memcpy(accessory.name, &namePrefix, sizeof namePrefix);
        unsigned long long portId = 0;
        int portBone = -1;
        const bool attached = GetAcsAttachedPmd(reinterpret_cast<std::uintptr_t>(&accessory),
                                                &portId, &portBone) != FALSE;
        NativeId nativeId = 0;
        int nativeBone = -1;
        const bool nativeAttached = mmh->Export<bool(WINAPI*)(NativeId, NativeId*, int*)>(
            "GetAcsAttachedPmd")(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&accessory)),
                                 &nativeId, &nativeBone);
#if defined(_M_X64)
        Check(attached == nativeAttached && static_cast<DWORD>(portId) == nativeId &&
                  portBone == nativeBone,
              "native x64 accessory attachment");
#else
        // MMHack 0.37 x86 reads +564/+568, whereas this original MMD uses
        // +560/+564. Record the original DLL defect without copying a model
        // name prefix into the port's live attachment output.
        Check(!attached && portId == 0 && portBone == 0 && nativeAttached &&
                  nativeId == modelIdentity && nativeBone == 7,
              "original x86 MMHack attachment layout discrepancy");
        std::puts("Recorded original x86 MMHack attachment +4-byte layout discrepancy");
#endif
        std::memset(accessory.name, 0, sizeof namePrefix);
    }
#if defined(_M_IX86)
    unsigned control;
    _controlfp_s(&control, _PC_24, _MCW_PC);
#endif
    D3DXMATRIX world, view;
    D3DXMatrixTranslation(&world, 1, 2, 3);
    D3DXMatrixRotationY(&view, .7f);
    Hr(f.device->SetTransform(D3DTS_WORLD, &world), "world matrix");
    Hr(f.device->SetTransform(D3DTS_VIEW, &view), "view matrix");
    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Direction = {.3f, -.8f, .2f};
    Hr(f.device->SetLight(0, &light), "light direction");
    Com<ID3DXEffect> effect;
    Com<ID3DXBuffer> errors;
    const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "res/assets/skin_effect_sm3.fx";
    Hr(D3DXCreateEffectFromFileW(f.device, path.c_str(), nullptr, nullptr, 0, nullptr, effect.Out(),
                                 errors.Out()),
       "standard effect");
    Hr(effect->SetMatrix("matLightViewProj", &view), "shadow matrix parameter");
    Com<IDirect3DTexture9> base, sphere;
    Hr(f.device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, base.Out(), nullptr),
       "persistent base");
    Hr(f.device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, sphere.Out(), nullptr),
       "persistent sphere");
    stableTextures[0] = base.p;
    stableTextures[1] = sphere.p;
    MmeHostSetMainWindow(f.window);
    MmeHostSetPathProviders(ModelPath, AccessoryPath);
    MmeHostSetStandardEffect(effect.p);
    f.renderer->effect = effect.p;
    mikudancestudio::mme::OnDeviceCreated(&app, f.window);
    suppliedEffect = effect.p;
    native = false;
    Exercise(f, model, accessory, *effect.p, nullptr, nullptr);
    mikudancestudio::mme::OnDeviceDestroyed(&app);
    const auto port = std::move(events);
    events.clear();
    if (mmh) {
        Com<ID3DXMesh> nativeMesh;
        Hr(mmh->LoadMesh(meshPath.c_str(), kManagedMesh, wrapped, nullptr, &count,
                         nativeMesh.Out()),
           "original MMHack .x loader");
        Check(count == 2 && nativeMesh->GetNumFaces() == 2, "native real mesh geometry");
        Com<ID3DXMesh> clone;
        Hr(nativeMesh->CloneMeshFVF(nativeMesh->GetOptions(),
                                    D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1, wrapped, clone.Out()),
           "native mesh clone wrapper");
        accessory.mesh = clone.p;
        mmh->RegisterObject(&accessory.mesh, accessory.sourcePath);
        suppliedEffect = effect.p;
        mmh->ReplaceImport("D3DXCreateEffectFromResourceA",
                           reinterpret_cast<FARPROC>(SupplyEffect));
        using Create =
            HRESULT(WINAPI*)(IDirect3DDevice9*, HMODULE, const char*, const D3DXMACRO*,
                             ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
        Com<ID3DXEffect> registered;
        Hr(mmh->At<Create>(sizeof(void*) == 8 ? 0x4440 : 0x40D0)(
               wrapped, nullptr, nullptr, nullptr, nullptr, 0, nullptr, registered.Out(), nullptr),
           "native effect registration");
        native = true;
        Exercise(f, model, accessory, *effect.p, queries.get(), wrapped);
        accessory.mesh = mesh.p;
        clone.p->Release();
        clone.p = nullptr;
        nativeMesh.p->Release();
        nativeMesh.p = nullptr;
        mmh.reset();
        Check(port.size() == events.size(), "native host event count");
        for (std::size_t i = 0; i < port.size(); ++i)
            if (port[i] != events[i]) {
                std::fprintf(stderr, "event %zu\nPORT %s\nMMD  %s\n", i, port[i].c_str(),
                             events[i].c_str());
                Check(false, "native MMHack callback differs");
            }
        std::printf("Native MMD/MMHack: %zu callback snapshots matched\n", port.size());
    } else {
        Check(port.size() >= 90, "host scenario coverage");
        std::printf("Host interception: %zu callback snapshots\n", port.size());
    }
    g_mmh = MmhState{};
    f.renderer->effect = nullptr;
    return 0;
}
} // namespace

extern "C" {
int __cdecl CaptureInitialize(IDirect3DDevice9* d) {
    return NInitialize(d);
}
void __cdecl CaptureCleanup(IDirect3DDevice9* d) {
    NCleanup(d);
}
void __cdecl CaptureBeginScene(IDirect3DDevice9* d) {
    NBegin(d);
}
void __cdecl CaptureEndScene(IDirect3DDevice9* d) {
    NEnd(d);
}
void __cdecl CaptureLost(IDirect3DDevice9* d) {
    NLost(d);
}
void __cdecl CaptureReset(IDirect3DDevice9* d) {
    NReset(d);
}
void __cdecl CaptureCreateModel(IDirect3DDevice9*, unsigned long long id, const char* name,
                                int kind, unsigned long count, void*, IUnknown*) {
    Created(id, name, kind, count);
}
void __cdecl CaptureDeleteModel(IDirect3DDevice9*, unsigned long long id) {
    Deleted(id);
}
HRESULT __cdecl CaptureIndexed(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, int b, UINT m, UINT v,
                               UINT s, UINT p) {
    return NIndexed(d, t, b, m, v, s, p);
}
HRESULT __cdecl CapturePrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT p) {
    return NPrimitive(d, t, s, p);
}
HRESULT __cdecl CaptureClear(IDirect3DDevice9* d, DWORD c, const D3DRECT* r, DWORD f,
                             D3DCOLOR color, float z, DWORD s, int main) {
    return NClear(d, c, r, f, color, z, s, main);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc >= 3 && std::wstring(argv[argc - 2]) == L"--pmx") {
            Check(std::wstring(argv[argc - 1]) == L"1", "expected --pmx 1");
            pmxScene = true;
            argc -= 2;
        }
        Check(argc == 1 || (argc == 5 && std::wstring(argv[1]) == L"--original" &&
                            std::wstring(argv[3]) == L"--mmhack"),
              "expected --original exe --mmhack dll [--pmx 1]");
        return Run(argc == 5 ? std::filesystem::absolute(argv[2]) : std::filesystem::path(),
                   argc == 5 ? std::filesystem::absolute(argv[4]) : std::filesystem::path());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
