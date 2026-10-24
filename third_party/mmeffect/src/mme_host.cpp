// mme_host.cpp - MmeHost* integration layer: rehost the original MMHack.dll device interception logic
// without hooks.
//
// The original intercepts MMD device calls through a d3d9.dll proxy + IAT/vtable wrappers. This file ports
// each interception point's logic ([0x180002de0] BeginScene state machine, [0x180003cb0] EndScene,
// [0x180003a20] Clear、[0x180002780] DrawPrimitive、[0x1800027f0]
// DrawIndexedPrimitive, [FUN_180001db0] device destruction, plus hook-side standard-effect registration,
// PMM tracking and texture records) unchanged into exports the host can call directly. State capture still uses
// device-state inspection + Exp* host queries, matching the original.
#include "mme_host_api.h"

#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <string>
#include <unordered_map>

#include "mmhack_state.h"
#include "mme_abi.h"
#include "mmeffect/mme_ui.h"
#include "mmeffect/mme_util.h"
#include "mmeffect/mme_context.h"
#include "mmeffect/model_data.h"
#include "mmeffect/device_initialization.h"
#include "mmeffect/host_object_cache.h"

extern "C" void __cdecl MmeHostInitializeRuntime(HINSTANCE instance) {
    mme::MmeUiInitializeRuntime(instance);
}

extern "C" void __cdecl MmeHostShutdownRuntime() {
    mme::MmeUiShutdownRuntime();
}

// Internal direct access from src/exports/effect_api.cpp (not exported, C linkage): original model/accessory
// wide-character paths (model+0x24BC / acc+0x29C, the paths used by D3DXLoadMeshFromXW).
// Original MMHack preserves these wide paths throughout: hook [0x1800043b0] registers them unchanged in
// ObjData+0x30 (wstring, copied directly by sub_18000f150); [sub_18000f340] reads them and passes them to
// [FUN_18000e020] (wfopen_s parses materials/toon files without an ANSI round trip). The embedded version also accesses them
// directly: round-tripping ExpGetPmdFilename's wide->SJIS output back through CP_ACP loses non-SJIS characters
// and corrupts material/toon parsing. MMEffect.dll cannot resolve non-exported symbols through the EXE's import
// library, so the host bridge injects function pointers with MmeHostSetPathProviders (equivalent to the original
// in-process direct connection between MMHack and the EXE).
static const wchar_t* (__cdecl* g_mmdModelPathW)(int) = nullptr;
static const wchar_t* (__cdecl* g_mmdAcsPathW)(int) = nullptr;

namespace {

// Original MyDirect3DDevice9 wrapper's own fields (+0x1c/+0x20/+0x28/+0x2c and three object-cache
// maps); this project shares the original's single-device assumption.
struct MmeHostDeviceState {
    mme::DeviceInitialization initialization;     // Wrapper +0x1c lifecycle
    IDirect3DSurface9* trackedRT = nullptr;       // Wrapper +0x20 (borrowed pointer)
    int currentObject = 0;                        // Wrapper +0x28
    int endSceneFired = 0;                        // Wrapper +0x2c
    std::list<MmhCachedObject*> objectList;       // Wrapper +0x58 node table
    std::unordered_map<unsigned long long, MmhCachedObject*> objectById;   // +0x30
    std::map<int, MmhCachedObject*> objectByOrder;                          // +0x60
};

MmeHostDeviceState g_host;

// [FUN_180002de0 creation path, lines 4936-4943] Per-object data for newly detected objects. Materials are
// parsed directly from the model file by FUN_18000e020 (embedded PMX names / PMD "<model>.txt" lines and
// toon references). Accessories retain empty tables: GetMaterialName [0x180001130] serves only
// objects with isPmd == 1.
void MmhPopulateObjectData(unsigned long long id, bool isPmd, int hostIndex,
                           const wchar_t* modelPath)
{
    (void)hostIndex;
    MmhObjData& d = g_mmh.objData[id];
    d.id = id;
    d.isPmd = isPmd;
    // Wide-character model path (original: ObjData+0x30 wstring, read by FUN_18000f340 and passed to
    // FUN_18000e020): obtain the host's original wide path directly, without a narrow-character round trip.
    d.modelName = (modelPath != nullptr) ? modelPath : L"";
    d.mats.clear();
    if (isPmd) {
        // [FUN_18000e020] The original calls this before OnCreateModel, passing &ObjData.materials
        // as the output.
        MmhMaterialTableLoadFromFile(d.modelName.c_str(), &d.mats);
    }
    // Attachment information (+0x240/+0x244): original ObjData never writes these fields, nor does host population
    // snapshot them. GetAcsAttachedPmd [0x1800012d0] directly reads the host accessory object on each call through sub_18000F2B0's
    // borrowed-data branch [0x18000f331] (equivalent implementation in this project:
    // mmhack_getters.cpp, AccessoryRecord::parentModel/parentBone), keeping keyframe-driven
    // attachment changes immediately visible. Retain construction defaults -1/0 here only as layout anchors.
    d.attachPmdIndex = -1;
    d.attachBoneIndex = 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Window / standard effect / file notifications (original hook-side recording paths)
// ---------------------------------------------------------------------------
void __cdecl MmeHostSetPathProviders(const wchar_t* (__cdecl* model_path)(int),
                                     const wchar_t* (__cdecl* acs_path)(int))
{
    g_mmdModelPathW = model_path;
    g_mmdAcsPathW = acs_path;
}

void __cdecl MmeHostSetMainWindow(HWND window)
{
    g_mmh.mainWindow = window;
    if (g_mmh.presentWindow == nullptr)
        g_mmh.presentWindow = window;

    // [Original MMHack DllMain] Enable debug mode if MMEffect.debug exists in the EXE directory
    // (the x64 original checks beside MMHack.dll, which shares the host EXE directory).
    // Probe only once (the host bridge may call this function repeatedly during per-frame refresh).
    static bool debugProbed = false;
    if (!debugProbed) {
        debugProbed = true;
        if (!g_mmh.debugMode) {
            char modulePath[MAX_PATH];
            if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH) > 0) {
                char* slash = strrchr(modulePath, '\\');
                std::string debugFile =
                    (slash != nullptr)
                        ? std::string(modulePath, slash - modulePath + 1) +
                              "MMEffect.debug"
                        : std::string("MMEffect.debug");
                FILE* fp = nullptr;
                if (fopen_s(&fp, debugFile.c_str(), "rb") == 0 && fp != nullptr) {
                    g_mmh.debugMode = true;
                    fclose(fp);
                }
            }
        }
    }
}

void __cdecl MmeHostSetStandardEffect(void* effect)
{
    // [0x180004440 Hooked_D3DXCreateEffectFromResourceA] Register once only if
    // qword_18006E7A0 (currentEffect) is unset and the newly created effect is non-null
    // [condition !qword_18006E7A0 && v8 && *v8 at 0x1800044b6]. Later calls return directly,
    // without replacing the cached effect or re-registering the 12 technique handles. The host calls this at the end of device initialization.
    if (effect == nullptr)
        return;
    if (g_mmh.currentEffect != nullptr)
        return;
    // [0x1800044c7 sub_180006350] Clear the technique cache map before registration (original hash-map
    // node releases for the qword_18006CCD0/DAT_18006ccf0 family).
    g_mmh.tecCache.clear();
    ID3DXEffect* fx = static_cast<ID3DXEffect*>(effect);
    for (int i = 0; i < MMH_TEC_COUNT; ++i) {
        D3DXHANDLE handle = fx->GetTechniqueByName(g_tecTable[i].name);
        if (handle != nullptr)
            MmhRecordTecHandle(handle, i);
    }
    // [0x18000451d] Cache the current effect only after registering all 12 handles.
    g_mmh.currentEffect = effect;
}

void __cdecl MmeHostSetDrawnWindow(HWND window)
{
    g_mmh.presentWindow = window;
}

void __cdecl MmeHostNotifyPmmLoaded(const wchar_t* path)
{
    if (path == nullptr || path[0] == L'\0')
        return;
    g_mmh.loadedPMMFile = path;
    g_mmh.loadedPmmFlag = true;
}

void __cdecl MmeHostNotifyPmmSaved(const wchar_t* path)
{
    if (path == nullptr || path[0] == L'\0')
        return;
    g_mmh.savedPmmList.push_back(path);
}

void __cdecl MmeHostRecordTextureFile(const wchar_t* path,
                                        IDirect3DBaseTexture9* texture)
{
    if (path == nullptr || path[0] == L'\0')
        return;
    MmhRecordTextureFile(std::wstring(path), texture);
}

int __cdecl MmeHostEffectsActive(void)
{
    return (g_host.initialization.IsReady() && !g_mmh.effectsDisabled) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Per-frame interception points
// ---------------------------------------------------------------------------

// [0x180003a20] Device Clear slot 43.
HRESULT __cdecl MmeHostClear(IDirect3DDevice9* device, unsigned long rect_count,
                               const D3DRECT* rects, unsigned long flags,
                               D3DCOLOR color, float z, unsigned long stencil)
{
    IDirect3DSurface9* surf = nullptr;
    device->GetRenderTarget(0, &surf);
    if (surf != nullptr)
        surf->Release();                         // Borrowed only for comparison
    if (g_host.trackedRT == nullptr || !g_host.initialization.IsReady() ||
        g_mmh.effectsDisabled) {
        return device->Clear(rect_count, rects, flags, color, z, stencil);
    }
    int mainTarget = 0;
    if (surf == g_host.trackedRT && (flags & D3DCLEAR_TARGET) != 0) {
        g_mmh.clearColor = color;                // DAT_18006ccc4
        mainTarget = 1;
    }
    OnClear(device, rect_count, rects, flags, color, z, stencil, mainTarget);
    return S_OK;
}

// [0x180002de0] Device BeginScene slot 41 - frame-start state machine.
HRESULT __cdecl MmeHostBeginScene(IDirect3DDevice9* device, int not_edit_mode)
{
    g_host.endSceneFired = 0;                    // +0x2c = 0
    g_mmh.notEditMode = not_edit_mode;           // DAT_18006e796 (semantics supplied by host,
                                                 // replacing MmhProbeEditMode's
                                                 // 0x13D/0x1A1 control probing)
    g_mmh.frameTime = ExpGetFrameTime();         // DAT_18006e79c

    // Lazy MMEffect initialization: the first BeginScene calls Initialize.
    if (!g_host.initialization.EnsureReady(
        [device] { return Initialize(device); },
        [] {
            const char* msg = (g_mmh.currentEffect != nullptr)
                ? "Initialize Error"
                : "Initialize Error: failed to load default effect file. "
                  "Please check video card capability.";
            MessageBoxA(g_mmh.mainWindow, msg, "MikuMikuEffect", MB_ICONERROR);
            g_mmh.effectsDisabled = 1;           // DAT_18006e794
        }))
        return S_OK; // Original failure returns 0 without real BeginScene.

    HRESULT hr = device->BeginScene();           // Real slot 41 (+0x148)
    if (hr != 0)
        return hr;

    // --- Build the set of currently live host object IDs ----------------------------------------
    int pmdNum = ExpGetPmdNum();
    int acsNum = ExpGetAcsNum();
    std::map<unsigned long long, bool> liveIds;
    for (int i = 0; i < pmdNum; i++)
        liveIds[(unsigned long long)(uintptr_t)ExpGetPmdID(i)] = true;
    for (int i = 0; i < acsNum; i++)
        liveIds[(unsigned long long)(uintptr_t)ExpGetAcsID(i)] = true;

    // --- Stages 1+2: remove caches for disappeared objects (OnDeleteModel) -----------------------
    mme::RemoveMissingHostObjects(g_host.objectList, g_host.objectById,
        liveIds, [device](MmhCachedObject* obj) {
            OnDeleteModel(device, obj->id);
            if (obj->texRef != nullptr)
                static_cast<IUnknown*>(obj->texRef)->Release();
            g_mmh.objData.erase(obj->id);
            delete obj;
        });

    // --- Stage 3: cache new objects (OnCreateModel) ----------------------------------------------
    for (int i = 0; i < pmdNum; i++) {
        unsigned long long id = (unsigned long long)(uintptr_t)ExpGetPmdID(i);
        if (g_host.objectById.find(id) != g_host.objectById.end())
            continue;
        MmhCachedObject* obj = new MmhCachedObject();
        obj->id = id;
        obj->isPmd = 1;
        // The embedded engine uses the Windows ANSI code page for paths,
        // not the exported API's fixed-size Shift-JIS buffer. Convert directly
        // from the loaded wide path so non-SJIS names cannot become empty.
        const wchar_t* widePath = g_mmdModelPathW ? g_mmdModelPathW(i) : nullptr;
        if (widePath) {
            obj->fileName = mme::MmeWideToAnsi(widePath);
        } else {
            const char* fname = ExpGetPmdFilename(i);
            if (fname) obj->fileName = fname;
        }
        MmhPopulateObjectData(id, true, i,
                              (g_mmdModelPathW != nullptr) ? g_mmdModelPathW(i) : nullptr);
        obj->data = MmhGetObjectData(id);
        g_host.objectList.push_back(obj);
        g_host.objectById[id] = obj;
        OnCreateModel(device, id, obj->fileName.c_str(), obj->isPmd,
                      (unsigned int)ExpGetPmdMatNum(i), nullptr, nullptr);
        if (widePath) {
            if (auto* model = mme::MmeFindOrCreateModelEntry(id))
                model->setDisplayFilename(widePath);
        }
    }
    for (int i = 0; i < acsNum; i++) {
        unsigned long long id = (unsigned long long)(uintptr_t)ExpGetAcsID(i);
        if (g_host.objectById.find(id) != g_host.objectById.end())
            continue;
        MmhCachedObject* obj = new MmhCachedObject();
        obj->id = id;
        obj->isPmd = 0;
        // The embedded engine uses the Windows ANSI code page for paths,
        // not the exported API's fixed-size Shift-JIS buffer. Convert directly
        // from the loaded wide path so non-SJIS names cannot become empty.
        const wchar_t* widePath = g_mmdAcsPathW ? g_mmdAcsPathW(i) : nullptr;
        if (widePath) {
            obj->fileName = mme::MmeWideToAnsi(widePath);
        } else {
            const char* fname = ExpGetAcsFilename(i);
            if (fname) obj->fileName = fname;
        }
        MmhPopulateObjectData(id, false, i,
                              (g_mmdAcsPathW != nullptr) ? g_mmdAcsPathW(i) : nullptr);
        obj->data = MmhGetObjectData(id);
        g_host.objectList.push_back(obj);
        g_host.objectById[id] = obj;
        OnCreateModel(device, id, obj->fileName.c_str(), obj->isPmd,
                      (unsigned int)ExpGetAcsMatNum(i), nullptr, nullptr);
        if (widePath) {
            if (auto* model = mme::MmeFindOrCreateModelEntry(id))
                model->setDisplayFilename(widePath);
        }
    }

    // --- Stage 4: rebuild the draw-order map ----------------------------------------------------
    g_host.objectByOrder.clear();
    for (int i = 0; i < pmdNum; i++) {
        unsigned long long id = (unsigned long long)(uintptr_t)ExpGetPmdID(i);
        std::unordered_map<unsigned long long, MmhCachedObject*>::iterator f =
            g_host.objectById.find(id);
        if (f != g_host.objectById.end())
            g_host.objectByOrder[ExpGetPmdOrder(i)] = f->second;
    }
    for (int i = 0; i < acsNum; i++) {
        unsigned long long id = (unsigned long long)(uintptr_t)ExpGetAcsID(i);
        std::unordered_map<unsigned long long, MmhCachedObject*>::iterator f =
            g_host.objectById.find(id);
        if (f != g_host.objectById.end())
            g_host.objectByOrder[ExpGetAcsOrder(i)] = f->second;
    }

    // World matrix and inverse (used for lighting matrix calculations)
    device->GetTransform(D3DTS_WORLDMATRIX(0), &g_mmh.worldMatrix);
    D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&g_mmh.worldInverse), nullptr,
                      reinterpret_cast<const D3DXMATRIX*>(&g_mmh.worldMatrix));

    // Track the current render target (borrowed; not released on destruction, matching the original).
    g_host.trackedRT = nullptr;                  // +0x20 = 0
    device->GetRenderTarget(0, &g_host.trackedRT);
    if (g_host.trackedRT != nullptr)
        g_host.trackedRT->Release();

    g_mmh.currentSubsetIndex = -1;               // DAT_18006ccc0 = -1
    g_host.currentObject = 0;                    // +0x28 = 0

    OnBeginScene(device);                        // DAT_18006e800
    return 0;
}

// [0x180003b50/0x180003bd0/0x180003c50] Slots 34/30/32: reading the tracked render target before EndScene
// triggers OnEndScene once, early (back-buffer capture/AVI sampling path).
void __cdecl MmeHostPreRenderTargetCopy(IDirect3DDevice9* device,
                                        IDirect3DSurface9* source)
{
    if (device == nullptr || source == nullptr)
        return;
    // MMHack's copy interceptors only fire when the copy reads the render
    // target captured at BeginScene (wrapper+0x20).
    if (g_host.endSceneFired == 0 && source == g_host.trackedRT) {
        OnEndScene(device);
        g_host.endSceneFired = 1;
    }
}

// [0x180003cb0] Device EndScene slot 42.
HRESULT __cdecl MmeHostEndScene(IDirect3DDevice9* device)
{
    if (g_host.endSceneFired == 0) {
        OnEndScene(device);
        g_host.endSceneFired = 1;
    }
    return device->EndScene();
}

// ---------------------------------------------------------------------------
// Draw interception points
// ---------------------------------------------------------------------------

namespace {

// [DIP lines 4238-4281] Fixed-function texture capture.
void MmhCaptureFixedFunctionTextureState(IDirect3DDevice9* device)
{
    (void)device;
    g_mmh.effectFileUsed = false;         // DAT_18006e795 = 0
    // Reset flag dword: bytes 0..2 = 0, byte 3 = (kind != 1)
    g_mmh.texUsed = 0;
    g_mmh.sphereUsed = 0;
    g_mmh.subTexUsed = 0;
    g_mmh.diffuseFlag = (g_mmh.currentObjectKind != 1) ? 1 : 0;
    g_mmh.sphereAdd = 0;                  // DAT_18006e797 = 0
    g_mmh.baseTexture = nullptr;          // DAT_18006e7a8 = 0
    g_mmh.sphereTexture = nullptr;        // DAT_18006e7b0 = 0

    int startSampler = (g_mmh.currentObjectKind == 1) ? 1 : 0;
    for (int s = startSampler; s < 3; s++) {
        DWORD colorOp = 1;
        device->GetTextureStageState(s, D3DTSS_COLOROP, &colorOp);
        if (colorOp == D3DTOP_DISABLE)
            break;                        // Texture-stage cascade has ended
        IDirect3DBaseTexture9* tex = nullptr;
        device->GetTexture(s, &tex);
        if (tex == nullptr)
            continue;
        tex->Release();                   // Borrowed capture
        DWORD ttf = 0;
        device->GetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
        if (ttf == 2) {                   // D3DTTFF_COUNT2 -> sphere-map stage
            g_mmh.sphereTexture = tex;    // DAT_18006e7b0
            g_mmh.sphereUsed = 1;         // byte @0x18006e789 = 1
            DWORD tci = 0;
            device->GetTextureStageState(s, D3DTSS_TEXCOORDINDEX, &tci);
            if (tci == 0x10000)
                g_mmh.sphereAdd = (colorOp == 7 /* D3DTOP_ADD */) ? 1 : 0;
            else {
                // [0x180002C8D] Register the regular-UV-driven sphere-map stage as the subtexture (cd2) flag
                // -> GetSphereMapMode returns 3.
                g_mmh.subTexUsed = 1;     // byte @0x18006e78a = 1
                g_mmh.sphereAdd = 0;      // DAT_18006e797 = 0
            }
        } else {
            g_mmh.texUsed = 1;            // byte @0x18006e788 = 1
            g_mmh.baseTexture = tex;      // DAT_18006e7a8
        }
    }
}

// [DIP lines 4282-4341] Effect (shader) texture capture.
void MmhCaptureEffectTextureState(IDirect3DDevice9* device)
{
    g_mmh.baseTexture = nullptr;          // DAT_18006e7a8 = 0
    g_mmh.sphereTexture = nullptr;        // DAT_18006e7b0 = 0
    ID3DXEffect* effect = static_cast<ID3DXEffect*>(g_mmh.currentEffect);
    if (effect != nullptr) {
        // [0x1d8 = ID3DXEffect::GetCurrentTechnique()]
        D3DXHANDLE hTec = effect->GetCurrentTechnique();
        // [FUN_1800061a0] Handle -> table index; unknown handles map to 0 (ColorRenderTec),
        // matching newly inserted cache-node behavior.
        int idx = MmhTecCacheIndex((const char*)hTec);
        if (idx < 0)
            idx = 0;
        if (idx < MMH_TEC_COUNT) {
            DWORD flags = g_tecTable[idx].flags;
            g_mmh.texUsed = (unsigned char)(flags & 0xFF);
            g_mmh.sphereUsed = (unsigned char)((flags >> 8) & 0xFF);
            g_mmh.subTexUsed = (unsigned char)((flags >> 16) & 0xFF);
            g_mmh.diffuseFlag = (unsigned char)((flags >> 24) & 0xFF);
            if (g_mmh.texUsed != 0) {
                // [0x180004310] GetTexture(1, &baseTexture)
                device->GetTexture(1, &g_mmh.baseTexture);
                if (g_mmh.baseTexture != nullptr)
                    g_mmh.baseTexture->Release();    // Borrowed
            }
            if (g_mmh.sphereUsed != 0) {
                // [0x180004316] GetTexture(texUsed ? 2 : 1, &sphereTexture)
                device->GetTexture(g_mmh.texUsed != 0 ? 2 : 1, &g_mmh.sphereTexture);
                if (g_mmh.sphereTexture != nullptr)
                    g_mmh.sphereTexture->Release();  // Borrowed
            }
        } else {
            // [0x1800027f0 line 4334] Technique absent from cache: clear flags
            g_mmh.texUsed = 0;
            g_mmh.sphereUsed = 0;
            g_mmh.subTexUsed = 0;
            g_mmh.diffuseFlag = 0;
        }
        g_mmh.sphereAdd = 0;
        // [0xb8 = ID3DXBaseEffect::GetBool] Read the "spadd" annotation;
        // DAT_18006e797 = (result != 0)。
        BOOL spadd = 0;
        if (effect->GetBool("spadd", &spadd) == 0 && spadd != 0)
            g_mmh.sphereAdd = 1;
        g_mmh.effectFileUsed = true;      // DAT_18006e795 = 1
    }
}

}  // namespace

// [0x180002780] Device DrawPrimitive slot 81. When effects are active, do not forward: reset host state and
// call MMEffect!OnDrawPrimitive instead (it reissues the draw).
HRESULT __cdecl MmeHostDrawPrimitive(IDirect3DDevice9* device,
                                       D3DPRIMITIVETYPE type,
                                       unsigned int start_vertex,
                                       unsigned int primitive_count)
{
    if (g_mmh.effectsDisabled || g_host.endSceneFired != 0) {
        return device->DrawPrimitive(type, start_vertex, primitive_count);   // Forward unchanged
    }
    g_mmh.currentSubsetIndex = -1;        // DAT_18006ccc0 = -1
    g_host.currentObject = 0;             // +0x28 = 0
    g_mmh.currentModelID = 0;             // DAT_18006e868 = 0
    g_mmh.currentObjectKind = 0;          // DAT_18006e870 = 0
    g_mmh.currentDrawType = 0;            // DAT_18006e798 = 0
    g_mmh.effectFileUsed = false;         // DAT_18006e795 = 0
    OnDrawPrimitive(device, type, start_vertex, primitive_count);
    return 0;
}

// [0x1800027f0] Device DrawIndexedPrimitive slot 82.
HRESULT __cdecl MmeHostDrawIndexedPrimitive(IDirect3DDevice9* device,
                                              D3DPRIMITIVETYPE type,
                                              int base_vertex_index,
                                              unsigned int min_vertex_index,
                                              unsigned int vertex_count,
                                              unsigned int start_index,
                                              unsigned int primitive_count)
{
    if (g_mmh.effectsDisabled || g_host.endSceneFired != 0) {
        return device->DrawIndexedPrimitive(type, base_vertex_index, min_vertex_index,
                                            vertex_count, start_index, primitive_count);
    }

    // Get (and immediately release) the current vertex shader to identify the shader path.
    // [0x1800027f0: GetVertexShader through real slot 93 (+0x2e8)]
    IDirect3DVertexShader9* vs = nullptr;
    device->GetVertexShader(&vs);
    if (vs != nullptr)
        vs->Release();

    int objIdx = ExpGetCurrentObject();
    if (objIdx == 0) {
        g_host.currentObject = 0;
        g_mmh.currentDrawType = 0;        // DAT_18006e798
        g_mmh.currentSubsetIndex = -1;    // DAT_18006ccc0 = -1
        g_mmh.currentModelID = 0;         // DAT_18006e868
        g_mmh.currentObjectKind = 0;      // DAT_18006e870
    } else {
        if (g_host.currentObject != objIdx) {
            g_host.currentObject = objIdx;
            // Look up the object in the draw-order map (missing entries retain null defaults).
            std::map<int, MmhCachedObject*>::iterator f =
                g_host.objectByOrder.find(objIdx);
            MmhCachedObject* obj = (f != g_host.objectByOrder.end()) ? f->second : nullptr;
            g_mmh.currentModelID = obj ? obj->id : 0;               // DAT_18006e868
            g_mmh.currentObjectKind = obj ? obj->isPmd : 0;         // DAT_18006e870
        }
        g_mmh.currentSubsetIndex = ExpGetCurrentMaterial();         // DAT_18006ccc0
        g_mmh.currentDrawType = ExpGetCurrentTechnic();             // DAT_18006e798
    }

    // Blend-mode flag: GetRenderState(D3DRS_DESTBLEND = 0x14); set 1 when destination blending is
    // D3DBLEND_ONE(2). [GetRenderState through real slot 58 (+0x1d0)]
    {
        DWORD destBlend = 0;
        g_mmh.blendMode = 0;              // DAT_18006e7b8 = 0
        device->GetRenderState(D3DRS_DESTBLEND, &destBlend);
        if (destBlend == 2)
            g_mmh.blendMode = 1;
    }

    if (vs == nullptr)
        MmhCaptureFixedFunctionTextureState(device);
    else
        MmhCaptureEffectTextureState(device);

    OnDrawIndexedPrimitive(device, type, base_vertex_index, min_vertex_index,
                           vertex_count, start_index, primitive_count);
    return 0;
}

// ---------------------------------------------------------------------------
// Device lifecycle
// ---------------------------------------------------------------------------
void __cdecl MmeHostOnLostDevice(IDirect3DDevice9* device)
{
    if (!g_host.initialization.IsReady())
        return;
    OnLostDevice(device);
}

void __cdecl MmeHostOnResetDevice(IDirect3DDevice9* device)
{
    if (!g_host.initialization.IsReady())
        return;
    OnResetDevice(device);
}

// [FUN_180001db0 + FUN_180001c90] Device destruction bookkeeping: MMEffect!Cleanup, release texture references
// captured by each cached object, clear object caches and the +0x1c flag. The tracked render target
// (+0x20) is borrowed (already released inside BeginScene) and is deliberately not released again here.
void __cdecl MmeHostDestroyDevice(IDirect3DDevice9* device)
{
    // Original Release (0x180001D50 -> 0x180001DB0) calls Cleanup even when
    // Initialize failed: it may already have allocated the context/engine.
    Cleanup(device);
    for (MmhCachedObject* obj : g_host.objectList) {
        if (obj->texRef != nullptr)
            static_cast<IUnknown*>(obj->texRef)->Release();
        delete obj;
    }
    g_host.objectList.clear();
    g_host.objectById.clear();
    g_host.objectByOrder.clear();
    g_host.currentObject = 0;
    g_host.initialization.Reset();
    g_host.endSceneFired = 0;
    g_host.trackedRT = nullptr;
}
