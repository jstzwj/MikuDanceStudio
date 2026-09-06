// ===========================================================================
// Accessory object, loader and render helpers
//   0x00460B30  application slot/UI loader
//   0x004C4A10  fixed-function renderer
//   0x004C52D0  shadow-map renderer
//   0x004C55C0  effect renderer
//   0x004C5F40  .x/.vac object loader
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

#include "fx_slots.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
void RefreshRequest(int area);
void PostLanguageSweep2(MMDApp* app);  // VA 0x0040D070, was declared here as
                                       // 0x40D070 (real body: ui_view_refresh.cpp)

namespace {

using Matrix = d3dx::D3DXMATRIXF;

// D3DXLoadMeshFromX* returns an array of this COM-facing record through its
// ID3DXBuffer.  Its trailing filename is a native pointer: 72 bytes in the
// x86 reference process, but 80 bytes on x64 after alignment.  Keeping it as
// a real type avoids silently applying the x86 stride to x64 material data.
struct D3dxMaterialRecord {
    D3DMATERIAL9 material;
    char* textureFileName;
};

static_assert(offsetof(D3dxMaterialRecord, textureFileName) ==
                  (sizeof(void*) == 8 ? 72 : sizeof(D3DMATERIAL9)),
              "D3DX material filename placement");

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(base) + offset);
}

// Effect calls go through the shared slot-numbered helpers in
// fx_slots.hpp; the old byte-offset spells (232/88/128/152/156/252/256/
// 264/268) were x86-only and silently landed on halved slots on x64.

void Identity(Matrix* value) {
    std::memset(value, 0, sizeof(*value));
    value->m[0][0] = value->m[1][1] = value->m[2][2] =
        value->m[3][3] = 1.0f;
}

void ReleaseCom(void* object) {
    if (object == nullptr)
        return;
    reinterpret_cast<IUnknown*>(object)->Release();  // vtable slot 2
}

// Porting-era trace under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void TraceAccessoryLoadStage(const char* stage, const void* value) {
    const char* directory = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (directory == nullptr || directory[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream, "stage=accessory-load-%s value=%p\r\n", stage, value);
    fclose(stream);
}
#else
inline void TraceAccessoryLoadStage(const char*, const void*) {}
#endif

IDirect3DTexture9* CachedTexture(D3DRenderer* sub, const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0')
        return nullptr;
    for (int i = 0; i < 10000; ++i) {
        const wchar_t* name = static_cast<const wchar_t*>(
            sub->resourcePool[i].heapBuffer);
        if (name == nullptr)
            return nullptr;
        if (wcscmp(name, path) == 0)
            return reinterpret_cast<IDirect3DTexture9*>(
                sub->resourcePool[i].comObject);
    }
    return nullptr;
}

using MeshDrawSubset = HRESULT(__stdcall*)(void*, DWORD);

void DrawSubset(void* accessory, DWORD index) {
    void* mesh = mdl::Accessory(accessory)->mesh;
    if (mesh != nullptr)
        reinterpret_cast<MeshDrawSubset>(
            (*reinterpret_cast<void***>(mesh))[3])(mesh, index);
}

void AccessoryPlacement(MMDApp* app, void* accessory, Matrix* world) {
    auto& api = d3dx::Get();
    Matrix part;
    const float scale = mdl::Accessory(accessory)->scale * 10.0f;
    api.scaling(world, scale, scale, scale);
    api.rotZ(&part, mdl::Accessory(accessory)->rotation[2]);
    api.multiply(world, world, &part);
    api.rotX(&part, mdl::Accessory(accessory)->rotation[0]);
    api.multiply(world, world, &part);
    api.rotY(&part, mdl::Accessory(accessory)->rotation[1]);
    api.multiply(world, world, &part);
    api.translation(&part, mdl::Accessory(accessory)->position[0],
                    mdl::Accessory(accessory)->position[1], mdl::Accessory(accessory)->position[2]);
    api.multiply(world, world, &part);

    const int modelSlot = mdl::Accessory(accessory)->parentModel;
    // x64 0x7FF7CB4C19F3 只判 parentModel == -1（cmp dword[r10+240h],-1），
    // 无上界检查；这里的上界取槽容量防止越界索引槽数组
    if (modelSlot < 0 || modelSlot >= kModelSlotCount)
        return;
    auto* model = app->ModelSlot(modelSlot);
    if (model == nullptr)
        return;
    auto* bones = mikudancestudio::mdl::Bones(model);
    const int boneIndex = std::max(mdl::Accessory(accessory)->parentBone, 0);
    auto* bone = &bones[boneIndex];
    api.translation(&part, bone->position[0], bone->position[1],
                    bone->position[2]);
    api.multiply(world, world, &part);
    api.multiply(world, world, reinterpret_cast<Matrix*>(bone->matInit));
}

// Faithful transcription of sub_4C4A10's per-material texture setup
// (0x4C4D86..0x4C4FFC).  The original runs NO preamble here - the device
// cascade is restored by the post-draw reset in RenderAccessoryFixedOne.
// Sphere/sub textures sample via CAMERASPACENORMAL (0x10000) through the
// scale-only matrix diag(0.5, -0.5, 0) built once in the prologue; the
// old reconstruction used CAMERASPACEREFLECTIONVECTOR and no transform.
void ConfigureFixedTexture(void* accessory, D3DRenderer* sub,
                           IDirect3DDevice9* device, DWORD material,
                           IDirect3DTexture9* screenTexture) {
    auto* paths = reinterpret_cast<unsigned char*>(mdl::Accessory(accessory)->texturePaths);
    auto* types = mdl::Accessory(accessory)->textureTypes;
    const unsigned char type = types != nullptr ? types[material] : 0;
    const wchar_t* first = paths != nullptr
        ? reinterpret_cast<const wchar_t*>(paths + 2048 * material) : L"";
    const wchar_t* second = paths != nullptr
        ? reinterpret_cast<const wchar_t*>(paths + 2048 * material + 1024)
        : L"";

    D3DMATRIX sphereTexel{};
    sphereTexel._11 = 0.5f;
    sphereTexel._22 = -0.5f;
    sphereTexel._33 = 0.0f;
    sphereTexel._44 = 1.0f;

    switch (type) {
    case 1:  // 0x4C4D94: sphere modulate
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                     D3DTTFF_COUNT2);
        device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0x10000);
        device->SetTransform(D3DTS_TEXTURE0, &sphereTexel);
        device->SetTexture(0, CachedTexture(sub, first));
        break;
    case 2:  // 0x4C4E2A: sphere add
        device->SetTransform(D3DTS_TEXTURE0, &sphereTexel);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADD);
        device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
                                     D3DTTFF_COUNT2);
        device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0x10000);
        device->SetTexture(0, CachedTexture(sub, first));
        break;
    case 3:  // 0x4C4EBC: screen texture
        device->SetTexture(0, screenTexture);
        break;
    case 4:  // 0x4C4EE0: sub texture, stage-1 modulate
    case 5:  // 0x4C4F25: sub texture, stage-1 add
        device->SetTexture(0, CachedTexture(sub, first));
        device->SetTransform(D3DTS_TEXTURE1, &sphereTexel);
        device->SetTextureStageState(1, D3DTSS_COLOROP,
            type == 5 ? D3DTOP_ADD : D3DTOP_MODULATE);
        device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS,
                                     D3DTTFF_COUNT2);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0x10000);
        device->SetTexture(1, CachedTexture(sub, second));
        break;
    default:  // 0x4C4FE6: plain texture
        device->SetTexture(0, CachedTexture(sub, first));
        break;
    }
}

// 0x4C505F..0x4C50D9: after every DrawSubset the original restores the
// fixed cascade (stage 0 back to MODULATE/passthrough, stage 1 disabled)
// so the next material - and later passes - start from a known state.
void ResetAccessoryTextureStages(IDirect3DDevice9* device) {
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
}

IDirect3DTexture9* AccessoryScreenTexture(MMDApp* app) {
    return app->CaptureMode() == ScreenCaptureMode::BackgroundRefresh
        ? app->AviBackgroundTexture()
        : app->CaptureTexture();
}

// The original order lookup dereferences the slot pointer directly; the
// plain null check keeps the empty slots out of the walk exactly like the
// reachable original behaviour (same form Wave1-C left in
// accessory_paste.cpp's ApplyAccessoryTrack).
void* FindAccessoryByOrder(MMDApp* app, int order) {
    for (int slot = 0; slot < 255; ++slot) {
        void* accessory = app->AccessorySlot(slot);
        if (accessory != nullptr &&
            mdl::Accessory(accessory)->order == order)
            return accessory;
    }
    return nullptr;
}

void SetAccessoryLight(MMDApp* app, IDirect3DDevice9* device,
                       bool accessoryPass) {
    D3DLIGHT9& light = app->SceneLight();
    light.Diffuse.r = accessoryPass ? 10.0f : 0.0f;
    light.Diffuse.g = accessoryPass ? 10.0f : 0.0f;
    light.Diffuse.b = accessoryPass ? 10.0f : 0.0f;
    const float delta = accessoryPass ? -0.300000011920929f
                                      : 0.300000011920929f;
    light.Ambient.r += delta;
    light.Ambient.g += delta;
    light.Ambient.b += delta;
    device->SetLight(0, &light);
}

void RenderAccessoryFixedOne(MMDApp* app, void* accessory,
                             bool projectedGroundShadow) {
    if (accessory == nullptr || mdl::Accessory(accessory)->visible == 0)
        return;
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    if (device == nullptr || !d3dx::Get().Load())
        return;

    Matrix base;
    Matrix world;
    device->GetTransform(D3DTS_WORLD, reinterpret_cast<D3DMATRIX*>(&base));
    AccessoryPlacement(app, accessory, &world);
    d3dx::Get().multiply(&world, &world, &base);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND,
        !projectedGroundShadow && mdl::Accessory(accessory)->additiveBlend != 0
            ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
    device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(143), TRUE);

    const DWORD count = mdl::Accessory(accessory)->materialCount;
    auto* materials = static_cast<unsigned char*>(mdl::Accessory(accessory)->materials);
    mdl::Accessory(accessory)->currentMaterial = -1;
    if (projectedGroundShadow) {
        D3DMATERIAL9 shadow{};
        const float gray = app->ProjectedShadowAmbientIntensity();
        shadow.Diffuse.a = 0.699999988079071f;
        shadow.Ambient.r = gray;
        shadow.Ambient.g = gray;
        shadow.Ambient.b = gray;
        shadow.Ambient.a = gray;
        shadow.Specular.a = 1.0f;
        device->SetMaterial(&shadow);
        device->SetTexture(0, nullptr);
        for (DWORD i = 0; i < count; ++i) {
            ++mdl::Accessory(accessory)->currentMaterial;
            mme::DrawAccessorySubset(app, accessory, i);
            ResetAccessoryTextureStages(device);
        }
    } else {
        for (DWORD i = 0; i < count; ++i) {
            ++mdl::Accessory(accessory)->currentMaterial;
            // 0x4C4D86: texture states first, then the material copy
            // (alpha scaled by +1184) immediately before the subset draw.
            ConfigureFixedTexture(accessory, sub, device, i,
                                  AccessoryScreenTexture(app));
            D3DMATERIAL9 material =
                *reinterpret_cast<D3DMATERIAL9*>(materials + 68 * i);
            material.Diffuse.a *= mdl::Accessory(accessory)->opacity;
            device->SetMaterial(&material);
            mme::DrawAccessorySubset(app, accessory, i);
            ResetAccessoryTextureStages(device);
        }
    }
    mdl::Accessory(accessory)->currentMaterial = -1;
    // 0x4C50FD..0x4C5141: color write off, world restore - and no texture
    // unbinding (the original leaves the last bindings in place).
    device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(143), FALSE);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&base));
}

void SelectEffectTechnique(void* accessory, D3DRenderer* sub,
                           IDirect3DDevice9* device, void* effect,
                           DWORD material, IDirect3DTexture9* screenTexture) {
    auto* paths = reinterpret_cast<unsigned char*>(mdl::Accessory(accessory)->texturePaths);
    auto* types = mdl::Accessory(accessory)->textureTypes;
    const unsigned char type = types != nullptr ? types[material] : 0;
    const wchar_t* first = paths != nullptr
        ? reinterpret_cast<const wchar_t*>(paths + 2048 * material) : L"";
    const wchar_t* second = paths != nullptr
        ? reinterpret_cast<const wchar_t*>(paths + 2048 * material + 1024)
        : L"";
    device->SetTexture(1, nullptr);
    device->SetTexture(2, nullptr);
    if (type == 3) {
        device->SetTexture(1, screenTexture);
        fx::SetTechnique(effect, "DiffuseBSTextureTec");
        return;
    }
    IDirect3DTexture9* firstTexture = CachedTexture(sub, first);
    if (firstTexture == nullptr) {
        fx::SetTechnique(effect, "DiffuseBufferShadowTec");
        return;
    }
    device->SetTexture(1, firstTexture);
    if (type == 1 || type == 2) {
        fx::SetTechnique(effect, "DiffuseBSSphiaTec");
        fx::SetBool(effect, "spadd", type == 2);
        return;
    }
    if (type == 4 || type == 5) {
        IDirect3DTexture9* secondTexture = CachedTexture(sub, second);
        if (secondTexture != nullptr) {
            device->SetTexture(2, secondTexture);
            fx::SetTechnique(effect, "DiffuseBSSphiaTexTec");
            fx::SetBool(effect, "spadd", type == 5);
            return;
        }
    }
    fx::SetTechnique(effect, "DiffuseBSTextureTec");
}

bool ExtractXTexture(const wchar_t* xFile, int material, char out[256]) {
    FILE* stream = nullptr;
    out[0] = '\0';
    if (_wfopen_s(&stream, xFile, L"r") != 0 || stream == nullptr)
        return false;
    char line[256];
    int seen = 0;
    while (fgets(line, sizeof(line), stream) != nullptr) {
        char* word = strstr(line, "Material");
        if (word != nullptr && word != line &&
            (word[-1] == ' ' || word[-1] == '\t')) {
            bool onlySpace = true;
            for (char* p = line; p < word - 1; ++p)
                onlySpace = onlySpace && (*p == ' ' || *p == '\t');
            if (onlySpace && seen++ > material)
                break;
        }
        if (seen <= material)
            continue;
        char* texture = strstr(line, "TextureFilename");
        if (texture == nullptr)
            continue;
        char* quote = strchr(texture, '"');
        if (quote == nullptr && fgets(line, sizeof(line), stream) != nullptr)
            quote = strchr(line, '"');
        if (quote != nullptr) {
            char* end = strrchr(quote + 1, '"');
            if (end != nullptr) {
                *end = '\0';
                strcpy_s(out, 256, quote + 1);
            }
        }
        break;
    }
    fclose(stream);
    return out[0] != '\0';
}

void WideToAnsi(const wchar_t* source, char* destination, int count) {
    destination[0] = '\0';
    WideCharToMultiByte(CP_ACP, 0, source, -1, destination, count,
                        nullptr, nullptr);
}

bool LoadOneTexture(D3DRenderer* sub, void* accessory, DWORD material,
                    const char* name, int pathOffset) {
    wchar_t converted[256] = {};
    wchar_t* directory = mdl::Accessory(accessory)->directory;
    auto* wrapperBytes = reinterpret_cast<unsigned char*>(sub);
    ConvertMaterialName(wrapperBytes, name, converted,
                        0x100, directory);
    auto* paths = reinterpret_cast<unsigned char*>(mdl::Accessory(accessory)->texturePaths);
    wchar_t* destination = reinterpret_cast<wchar_t*>(
        paths + 2048 * material + pathOffset);
    swprintf_s(destination, 0x200, L"%s%s", directory, converted);
    if (LoadTextureShared(wrapperBytes, destination))
        return true;
    char fallback[256] = {};
    const wchar_t* xFile = mdl::Accessory(accessory)->sourcePath;
    if (!ExtractXTexture(xFile, static_cast<int>(material), fallback))
        return false;
    ConvertMaterialName(wrapperBytes, fallback, converted,
                        0x100, directory);
    swprintf_s(destination, 0x200, L"%s%s", directory, converted);
    return LoadTextureShared(wrapperBytes, destination) != 0;
}

// ---- "Cannot find xfile!!" (x64 sub_7FF7CB4FC560 not-found paths) -------
// 0x7FF7CB551448: JP caption "アクセサリ読込"; 0x7FF7CB552858: JP text
// "xファイルが見つかりません" (SJIS, byte-exact).
constexpr char kCaptionLoadAccessoryJp[] =
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x93\xC7\x8D\x9E";
constexpr char kMsgCannotFindXfileJp[] =
    "x\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xAA\x8C\xA9\x82\xC2\x82\xA9"
    "\x82\xE8\x82\xDC\x82\xB9\x82\xF1";
// 0x7FF7CB551458: JP twin of the caller's "This is not x_file." text
// ("正しいｘファイルではありません").
constexpr char kMsgNotXFileJp[] =
    "\x90\xB3\x82\xB5\x82\xA2\x82\x98\x83\x74\x83\x40\x83\x43\x83\x8B"
    "\x82\xC5\x82\xCD\x82\xA0\x82\xE8\x82\xDC\x82\xB9\x82\xF1";

// The original object loader pops this box (uType 0) on every not-found
// path - empty resolved path, missing directory separator, and a failed
// open of the resolved file - before returning 0 to the caller (which then
// adds its own "This is not x_file." box, mirroring the double prompt of
// the original).
void ShowCannotFindXfile(MMDApp* app) {
    const bool english = app->state.englishUI != 0;
    MessageBoxA(static_cast<HWND>(app->Hwnd()),
                english
                    ? "Cannot find xfile!!\n\nIf Japanese font is "
                      "included in the filename,please rewrite it in "
                      "English font."
                    : kMsgCannotFindXfileJp,
                english ? "load accessory" : kCaptionLoadAccessoryJp,
                MB_OK);
}

}  // namespace

// VA 0x004C4700 - moved out of the anonymous namespace so the PMM loaders
// (0x459221 / 0x4541D9) and the shutdown chain (0x462F9C / 0x46324D) bind to
// this one definition instead of the old no-op stub.
void DisposeAccessory(void* accessory) {
    if (accessory == nullptr)
        return;
    mdl::AccessoryRecord& record = *mdl::Accessory(accessory);
    ReleaseCom(record.mesh);
    record.mesh = nullptr;
    // 0x4C4717..0x4C4755: original free order is +4, then +12, then +8
    // (the loop order here used to be +4, +8, +12) and uses free(), not
    // operator delete.
    free(record.materials);
    record.materials = nullptr;
    free(record.textureTypes);
    record.textureTypes = nullptr;
    free(record.texturePaths);
    record.texturePaths = nullptr;
}

bool LoadAccessoryObject(MMDApp* app, void* accessory, const wchar_t* path) {
    auto* sub = app->Renderer();
    TraceAccessoryLoadStage("entry", accessory);
    PathResolutionWorkspace& paths = app->PathWorkspace();
    const wchar_t* resolved = ResolveUserFilePath(paths, path);
    if (resolved[0] == L'\0') {
        ShowCannotFindXfile(app);  // x64 sub_7FF7CB4FC5CC path
        return false;
    }

    wchar_t source[256];
    wcscpy_s(source, resolved);
    wchar_t* tail = wcsrchr(source, L'\\');
    if (tail == nullptr) {
        ShowCannotFindXfile(app);  // x64 sub_7FF7CB4FC768 path
        return false;
    }
    mdl::AccessoryRecord& record = *mdl::Accessory(accessory);
    WideToAnsi(tail + 1, record.name, sizeof record.name);
    tail[1] = L'\0';
    wcscpy_s(record.directory, source);
    SetCurrentDirectoryW(source);
    wcscpy_s(record.sourcePath, resolved);

    // The original opens the resolved file before the .vac split; a failed
    // open is the third "Cannot find xfile!!" path (x64 sub_7FF7CB4FC71F).
    FILE* probe = nullptr;
    if (_wfopen_s(&probe, resolved, L"r") != 0 || probe == nullptr) {
        ShowCannotFindXfile(app);
        return false;
    }
    fclose(probe);

    wchar_t meshPath[256];
    wcscpy_s(meshPath, resolved);
    if (wcsstr(meshPath, L".vac") != nullptr) {
        FILE* vac = nullptr;
        if (_wfopen_s(&vac, resolved, L"r") != 0 || vac == nullptr)
            return false;
        char line[256] = {};
        fgets(line, sizeof(line), vac);
        fgets(line, sizeof(line), vac);
        if (char* lf = strchr(line, '\n'))
            *lf = '\0';
        if (!ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(sub),
                                 line, meshPath, 0x100,
                                 paths)) {
            fclose(vac);
            return false;
        }
        const wchar_t* meshName = wcsrchr(meshPath, L'\\');
        WideToAnsi(meshName != nullptr ? meshName + 1 : meshPath,
                   record.name, static_cast<unsigned>(sizeof record.name));
        fscanf_s(vac, "%f", &record.scale);
        fscanf_s(vac, "%f,%f,%f", &record.position[0],
                 &record.position[1], &record.position[2]);
        fscanf_s(vac, "%f,%f,%f", &record.rotation[0],
                 &record.rotation[1], &record.rotation[2]);
        fgets(line, sizeof(line), vac);
        fgets(line, sizeof(line), vac);
        std::int32_t shadowDisabled = 0;
        fscanf_s(vac, "%d", &shadowDisabled);
        record.shadowEnabled = shadowDisabled == 0;
        fclose(vac);
    }

    auto& api = d3dx::Get();
    if (!api.Load())
        return false;
    void* materialBuffer = nullptr;
    DWORD materialCount = 0;
    void* mesh = nullptr;
    if (FAILED(api.loadMeshFromXW(meshPath, 544,
            sub->device, nullptr, &materialBuffer,
            nullptr, &materialCount, &mesh)))
        return false;
    TraceAccessoryLoadStage("mesh-loaded", mesh);
    record.mesh = mesh;
    record.materialCount = materialCount;
    record.materials = ::operator new(68 * materialCount);
    record.texturePaths = static_cast<char (*)[2048]>(
        ::operator new(2048 * materialCount));
    record.textureTypes = static_cast<std::uint8_t*>(
        ::operator new(materialCount));
    std::memset(record.texturePaths, 0, 2048 * materialCount);
    std::memset(record.textureTypes, 0, materialCount);

    if (materialBuffer != nullptr) {
        TraceAccessoryLoadStage("material-buffer", materialBuffer);
        using GetPointer = void*(__stdcall*)(void*);
        auto* sourceMaterials = static_cast<D3dxMaterialRecord*>(
            reinterpret_cast<GetPointer>(
                (*reinterpret_cast<void***>(materialBuffer))[3])(
                    materialBuffer));
        for (DWORD i = 0; i < materialCount; ++i) {
            auto* destination = static_cast<unsigned char*>(
                mdl::Accessory(accessory)->materials) + 68 * i;
            const D3dxMaterialRecord& source = sourceMaterials[i];
            std::memcpy(destination, &source.material, 68);
            auto* material = reinterpret_cast<D3DMATERIAL9*>(destination);
            material->Ambient = material->Diffuse;
            material->Diffuse.r *= 0.1f;
            material->Diffuse.g *= 0.1f;
            material->Diffuse.b *= 0.1f;
            material->Specular.r *= 0.1f;
            material->Specular.g *= 0.1f;
            material->Specular.b *= 0.1f;
            char* texture = source.textureFileName;
            if (texture == nullptr || texture[0] == '\0')
                continue;
            auto* types = mdl::Accessory(accessory)->textureTypes;
            if (char* star = strchr(texture, '*')) {
                *star = '\0';
                LoadOneTexture(sub, accessory, i, texture, 0);
                const char* sphere = star + 1;
                types[i] = strstr(sphere, ".sph") != nullptr ? 4
                    : strstr(sphere, ".spa") != nullptr ? 5 : 0;
                LoadOneTexture(sub, accessory, i, sphere, 1024);
            } else {
                types[i] = strstr(texture, "screen.bmp") != nullptr ? 3
                    : strstr(texture, ".sph") != nullptr ? 1
                    : strstr(texture, ".spa") != nullptr ? 2 : 0;
                if (types[i] != 3)
                    LoadOneTexture(sub, accessory, i, texture, 0);
            }
        }
        ReleaseCom(materialBuffer);
    }

    TraceAccessoryLoadStage("materials-complete", mesh);

    using MeshGetDword = DWORD(__stdcall*)(void*);
    using MeshCloneFvf = HRESULT(__stdcall*)(void*, DWORD, DWORD,
                                              IDirect3DDevice9*, void**);
    if (reinterpret_cast<MeshGetDword>(
            (*reinterpret_cast<void***>(mesh))[6])(mesh) != 274) {
        DWORD options = reinterpret_cast<MeshGetDword>(
            (*reinterpret_cast<void***>(mesh))[9])(mesh);
        void* clone = nullptr;
        if (SUCCEEDED(reinterpret_cast<MeshCloneFvf>(
                (*reinterpret_cast<void***>(mesh))[11])(
                    mesh, options, 274, sub->device,
                    &clone))) {
            ReleaseCom(mesh);
            mdl::Accessory(accessory)->mesh = clone;
            api.computeNormals(clone, nullptr);
        }
    }
    TraceAccessoryLoadStage("complete", mdl::Accessory(accessory)->mesh);
    return true;
}

void SetEditFloat(HWND hwnd, int id, const char* format, float value) {
    char text[256];
    sprintf_s(text, format, value);
    SetDlgItemTextA(hwnd, id, text);
}


void DeleteAccessory(mdl::AccessoryRecord* accessory,
                     int releaseObject) {                              // 0x40A6F0
    DisposeAccessory(accessory);
    if ((releaseObject & 1) != 0)
        ::operator delete(accessory);
}

void LoadAccessoryFile(const wchar_t* path) {                   // 0x460B30
    MMDApp* app = g_Block;
    if (app == nullptr || path == nullptr)
        return;
    int slot = 0;
    while (slot < 255 && app->AccessorySlot(slot) != nullptr)
        ++slot;
    if (slot >= 255) {
        app->state.enterKeyState = 1;
        static const char kJpAccLimit[] =
            "\x92\xc7\x89\xc1\x82\xc5\x82\xab\x82\xe9\x83\x41\x83\x4e"
            "\x83\x5a\x83\x54\x83\x8a\x82\xcc\x8d\xc5\x91\xe5\x90\x94"
            "\x82\xcd%d\x8c\xc2\x82\xdc\x82\xc5\x82\xc5\x82\xb7";
        static const char kJpAccTitle[] =
            "\x83\x41\x83\x4e\x83\x5a\x83\x54\x83\x8a\x93\xc7\x8d\x9e";  // アクセサリ読込
        const bool english = app->EnglishUI() != 0;
        char text[256];
        sprintf_s(text,
                  english ? "You cannot add accessory over %d"
                          : kJpAccLimit,
                  255);
        MessageBoxA(static_cast<HWND>(app->Hwnd()), text,
                    english ? "load accessory" : kJpAccTitle, MB_OK);
        return;
    }

    void* accessory = ::operator new(sizeof(mdl::AccessoryRecord),
                                     std::nothrow);
    if (accessory == nullptr)
        return;
    std::memset(accessory, 0, sizeof(mdl::AccessoryRecord));
    InitAccessoryRecord(accessory);
    app->AccessorySlot(slot) = static_cast<mdl::AccessoryRecord*>(accessory);
    if (!LoadAccessoryObject(app, accessory, path)) {
        const bool english = app->state.englishUI != 0;
        MessageBoxA(static_cast<HWND>(app->Hwnd()),
            english
                ? "This is not x_file.\n\nIf Japanese font is included in "
                  "the filename,please rewrite it in English font."
                : kMsgNotXFileJp,
            english ? "load accessory" : kCaptionLoadAccessoryJp, MB_OK);
        DisposeAccessory(accessory);
        ::operator delete(accessory);
        app->AccessorySlot(slot) = nullptr;
        return;
    }

    HWND hwnd = static_cast<HWND>(app->Hwnd());
    app->state.sceneModified = 1;
    const char* name = mdl::Accessory(accessory)->name;
    LRESULT displayIndex = SendDlgItemMessageA(
        hwnd, panel::kAccessoryCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    mdl::Accessory(accessory)->order =
        static_cast<std::uint8_t>(displayIndex);
    if (displayIndex >= 0)
        app->AccessoryRenderSplitOrder() = std::max(
            app->AccessoryRenderSplitOrder(),
            static_cast<std::int32_t>(displayIndex + 1));
    SendDlgItemMessageA(hwnd, panel::kAccessoryCombo, CB_SETCURSEL, displayIndex, 0);
    app->SelectedAccessorySlot() = static_cast<std::uint8_t>(slot);
    SendDlgItemMessageA(hwnd, panel::kRegisterScopeCombo, CB_ADDSTRING, 0,
                        reinterpret_cast<LPARAM>(name));
    SendDlgItemMessageA(hwnd, panel::kMainComboGround, CB_SETCURSEL, 0, 0);
    SendDlgItemMessageA(hwnd, panel::kAttachBoneCombo, CB_SHOWDROPDOWN, 0, 0);
    CheckDlgButton(hwnd, panel::kAccessoryVisibleCheckbox, BST_CHECKED);
    mdl::Accessory(accessory)->parentModel = -1;
    SetEditFloat(hwnd, 478, "%3.4f", mdl::Accessory(accessory)->position[0]);
    SetEditFloat(hwnd, 479, "%3.4f", mdl::Accessory(accessory)->position[1]);
    SetEditFloat(hwnd, 480, "%3.4f", mdl::Accessory(accessory)->position[2]);
    constexpr float kRadToDeg = 180.0f / 3.141592025756836f;
    SetEditFloat(hwnd, 481, "%3.4f", mdl::Accessory(accessory)->rotation[0] * kRadToDeg);
    SetEditFloat(hwnd, 482, "%3.4f", mdl::Accessory(accessory)->rotation[1] * kRadToDeg);
    SetEditFloat(hwnd, 483, "%3.4f", mdl::Accessory(accessory)->rotation[2] * kRadToDeg);
    SetEditFloat(hwnd, 484, "%3.4f", mdl::Accessory(accessory)->scale);
    SetEditFloat(hwnd, 485, "%3.2f", mdl::Accessory(accessory)->opacity);
    EnableMenuItem(GetMenu(hwnd), 0xF9, MF_ENABLED);
    CheckDlgButton(hwnd, panel::kAccessoryAddBlendCheckbox, BST_UNCHECKED);
    CheckDlgButton(hwnd, panel::kAccessoryShadowCheckbox,
                   mdl::Accessory(accessory)->shadowEnabled ? BST_CHECKED
                                                     : BST_UNCHECKED);

    auto* frame = app->AccessoryKeys(slot);
    if (frame != nullptr) {
        std::memcpy(frame[0].position,
                    reinterpret_cast<unsigned char*>(mdl::Accessory(accessory)->position),
                    sizeof(frame[0].position));
        std::memcpy(frame[0].rotation,
                    reinterpret_cast<unsigned char*>(mdl::Accessory(accessory)->rotation),
                    sizeof(frame[0].rotation));
        frame[0].scale = mdl::Accessory(accessory)->scale;
        frame[0].opacity = mdl::Accessory(accessory)->opacity;
        frame[0].shadowEnabled = mdl::Accessory(accessory)->shadowEnabled;
    }
    RefreshRequest(app->SelectedAccessorySlot());
    PostLanguageSweep2(app);
}

// VA 0x00413CB0 - register the accessory's current state at
// one frame into its 10000-record key list (visible/shadow/parent fields +
// pos/rot/scale/opacity), the same walk/overwrite/splice insert as the
// other registrars.
void RegisterAccessoryKey(MMDApp* app, int frameArg, int slot) {  // 0x413CB0
    if (app == nullptr || slot < 0 || slot >= 255)
        return;
    auto* object = reinterpret_cast<unsigned char*>(app->AccessorySlot(slot));
    auto* keys = app->AccessoryKeys(slot);
    if (object == nullptr || keys == nullptr)
        return;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameArg);
    const mdl::AccessoryRecord& accessory = *mdl::Accessory(object);

    const auto fill = [&](int index) {
        mdl::AccessoryKey& key = keys[index];
        key.visible = accessory.visible;
        key.shadowEnabled = accessory.shadowEnabled;
        key.parentModel = accessory.parentModel;
        key.parentBone = accessory.parentBone;
        std::memcpy(key.position, accessory.position, sizeof key.position);
        std::memcpy(key.rotation, accessory.rotation, sizeof key.rotation);
        key.scale = accessory.scale;
        key.opacity = accessory.opacity;
        key.selected = 1;
    };

    int current = 0;
    if (keys[0].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[current].next);
            if (next == 0)
                break;
            current = next;
            if (keys[current].frame >= frame)
                break;
        }
    }
    if (keys[current].frame == frame) {
        fill(current);
        return;
    }

    int freeIndex = 1;
    while (freeIndex < 10000 && keys[freeIndex].frame != 0)
        ++freeIndex;
    if (freeIndex >= 10000) {
        char text[256];
        sprintf_s(text, 0x100,
                  "You cannot regist over %dpoint.\n"
                  "Please execute 'delete unused frame'", 10000);
        MessageBoxA(static_cast<HWND>(app->Hwnd()), text,
                    "register frame", 0);
        return;
    }

    if (keys[current].frame < frame) {
        keys[current].next = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].previous = static_cast<std::uint32_t>(current);
    } else {
        const int previous = static_cast<int>(keys[current].previous);
        keys[previous].next = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].previous = static_cast<std::uint32_t>(previous);
        keys[current].previous = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].next = static_cast<std::uint32_t>(current);
    }
    keys[freeIndex].frame = frame;
    fill(freeIndex);
    if (frame > app->LastRegisteredFrame())
        app->LastRegisteredFrame() = frame;
}

void RenderAccessoriesFixed(MMDApp* app) {                      // 0x4C4A10
    RenderAccessoriesFixedRange(app, 0, 255);
}

void RenderAccessoriesFixedRange(MMDApp* app, int firstOrder,
                                 int lastOrder) {
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    if (device == nullptr || !d3dx::Get().Load())
        return;
    app->ActiveRenderPass() = AccessoryRenderPass::FixedFunction;
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    SetAccessoryLight(app, device, true);
    for (int order = firstOrder; order < lastOrder; ++order) {
        void* accessory = FindAccessoryByOrder(app, order);
        if (accessory == nullptr || mdl::Accessory(accessory)->visible == 0)
            continue;
        app->ActiveRenderObject() = accessory;
        RenderAccessoryFixedOne(app, accessory, false);
        app->ActiveRenderObject() = nullptr;
    }
    SetAccessoryLight(app, device, false);
}

void RenderAccessoriesProjectedGroundShadow(MMDApp* app) {
    const bool cameraGate = app->PlaybackActive() != 0 ||
        app->UsesViewportTool();
    if (app->GroundShadowEnabled() == 0 || !cameraGate ||
        app->LightDirection()[1] >= 0.0f)
        return;
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    if (device == nullptr || !d3dx::Get().Load())
        return;

    Matrix base;
    Matrix projection{};
    Matrix projected;
    device->GetTransform(D3DTS_WORLD, reinterpret_cast<D3DMATRIX*>(&base));
    projection.m[0][0] = 1.0f;
    projection.m[1][0] = -app->LightDirection()[0] /
                          app->LightDirection()[1];
    projection.m[1][2] = -app->LightDirection()[2] /
                          app->LightDirection()[1];
    projection.m[2][2] = 1.0f;
    projection.m[3][1] = 0.10000000149011612f;
    projection.m[3][3] = 1.0f;
    d3dx::Get().multiply(&projected, &projection, &base);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&projected));
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    device->SetTexture(0, nullptr);
    RenderAccessoriesProjectedGroundShadowGeometry(app);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&base));
}

void RenderAccessoriesProjectedGroundShadowGeometry(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    if (device == nullptr)
        return;
    app->ActiveRenderPass() =
        AccessoryRenderPass::ProjectedGroundShadow;
    for (int slot = 0; slot < 255; ++slot) {
        void* accessory = app->AccessorySlot(slot);
        if (accessory == nullptr)
            continue;
        if (mdl::Accessory(accessory)->shadowEnabled == 0)
            continue;
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,
                               app->ProjectedShadowBlendEnabled());
        app->ActiveRenderObject() = accessory;
        RenderAccessoryFixedOne(app, accessory, true);
        app->ActiveRenderObject() = nullptr;
    }
    const D3DMATERIAL9 shadowMaterial = app->ProjectedShadowMaterial();
    device->SetMaterial(&shadowMaterial);
    device->SetTexture(0, app->ProjectedShadowRestoreTexture());
    // x64 0x7FF7CB4C06FF：配件投射循环结束、SetMaterial/SetTexture 复位
    // （0x7FF7CB4C06A5..0x7FF7CB4C06DD）之后、模型影走查（0x7FF7CB4C0710..）
    // 之前，按"transparent ground shadow(&T)"开关（app+0x9FCA2）重写
    // ALPHABLENDENABLE；pass 起始段不写 RS(27)。该字节即 PMM 存档里的
    // projectedShadowBlendEnabled（保存点 0x7FF7CB498408）。
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,
                           app->ProjectedShadowBlendEnabled());
}

void RenderAccessoriesShadow(MMDApp* app) {                     // 0x4C52D0
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    void* effect = sub->effect;
    if (device == nullptr || effect == nullptr || !d3dx::Get().Load())
        return;
    for (int slot = 0; slot < 255; ++slot) {
        void* accessory = app->AccessorySlot(slot);
        if (accessory == nullptr || mdl::Accessory(accessory)->visible == 0 ||
            mdl::Accessory(accessory)->shadowEnabled == 0 ||
            mdl::Accessory(accessory)->additiveBlend != 0)
            continue;
        Matrix world;
        Matrix light;
        AccessoryPlacement(app, accessory, &world);
        d3dx::Get().multiply(
            &light, &world,
            reinterpret_cast<const Matrix*>(&app->LightViewProjection()));
        fx::SetMatrix(effect, "matLightViewProj", &light);
        fx::SetMatrix(effect, "matWorldViewProj", &light);
        D3DMATERIAL9 material{};
        material.Diffuse.a = 1.0f;
        material.Specular.a = 1.0f;
        device->SetMaterial(&material);
        device->SetTexture(0, nullptr);
        fx::BeginPass(effect);
        auto* materials = static_cast<const D3DMATERIAL9*>(mdl::Accessory(accessory)->materials);
        const DWORD count = mdl::Accessory(accessory)->materialCount;
        mdl::Accessory(accessory)->currentMaterial = -1;
        for (DWORD i = 0; i < count; ++i) {
            ++mdl::Accessory(accessory)->currentMaterial;
            if (materials[i].Diffuse.a != 0.9800000190734863f)
                mme::DrawAccessorySubset(app, accessory, i);
        }
        fx::EndPass(effect);
        mdl::Accessory(accessory)->currentMaterial = -1;
    }
}

void RenderAccessoriesEffect(MMDApp* app) {                     // 0x4C55C0
    RenderAccessoriesEffectRange(app, 0, 255);
}

void RenderAccessoriesEffectRange(MMDApp* app, int firstOrder,
                                  int lastOrder) {
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    void* effect = sub->effect;
    if (device == nullptr || effect == nullptr || !d3dx::Get().Load())
        return;
    const float* light = reinterpret_cast<const float*>(&app->SceneLight());
    // 0x4C593B..0x4C594F: the effect accessory renderer does not feed the
    // raw ambient/emissive sum to EgColor.  Its RGB channels are attenuated
    // by app+0xA0CF0 (the projected/self-shadow colour factor copied into
    // the caller's stack immediately before sub_4C55C0).  Omitting this
    // factor makes additive light meshes and screen/reflection accessories
    // accumulate far too brightly, which is especially obvious in the
    // recursive mirror of sample_v2.pmm.
    const float edgeFactor = app->ProjectedShadowAmbientIntensity();
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    SetAccessoryLight(app, device, true);
    for (int order = firstOrder; order < lastOrder; ++order) {
        void* accessory = FindAccessoryByOrder(app, order);
        if (accessory == nullptr || mdl::Accessory(accessory)->visible == 0)
            continue;
        app->ActiveRenderObject() = accessory;
        if (mdl::Accessory(accessory)->shadowEnabled == 0) {
            app->ActiveRenderPass() =
                AccessoryRenderPass::FixedFunction;
            RenderAccessoryFixedOne(app, accessory, false);
            app->ActiveRenderObject() = nullptr;
            continue;
        }
        app->ActiveRenderPass() = AccessoryRenderPass::Effect;
        device->SetTexture(0, sub->hdrTexture);
        device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
        Matrix world;
        Matrix value;
        AccessoryPlacement(app, accessory, &world);
        d3dx::Get().multiply(
            &value, &world,
            reinterpret_cast<const Matrix*>(&app->LightViewProjection()));
        fx::SetMatrix(effect, "matLightViewProj", &value);
        d3dx::Get().multiply(
            &value, &world,
            reinterpret_cast<const Matrix*>(&app->WorldViewProjection()));
        fx::SetMatrix(effect, "matWorldViewProj", &value);
        fx::SetMatrix(effect, "matWorld", &world);
        Matrix inverseScale;
        const float inv = 1.0f / (mdl::Accessory(accessory)->scale * 10.0f);
        d3dx::Get().scaling(&inverseScale, inv, inv, inv);
        d3dx::Get().multiply(&value, &inverseScale, &world);
        // x64 0x7FF7CB4FE29F: "matRotate" goes through slot 39, GetMatrix -
        // the original reads the parameter (set from ViewRotationTransform
        // by the effect pass head, model_renderers.cpp 0x7FF7CB4C22FD)
        // back into the buffer between the two multiplies; it is not a
        // transpose write (SetMatrixTranspose is the untouched slot 44).
        fx::GetMatrix(effect, "matRotate", &inverseScale);
        d3dx::Get().multiply(&value, &value, &inverseScale);
        fx::SetMatrix(effect, "matWRotate", &value);
        device->SetRenderState(D3DRS_DESTBLEND,
            mdl::Accessory(accessory)->additiveBlend != 0
                ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);

        const DWORD count = mdl::Accessory(accessory)->materialCount;
        auto* materials = static_cast<unsigned char*>(mdl::Accessory(accessory)->materials);
        mdl::Accessory(accessory)->currentMaterial = -1;
        for (DWORD i = 0; i < count; ++i) {
            ++mdl::Accessory(accessory)->currentMaterial;
            auto* material = reinterpret_cast<D3DMATERIAL9*>(
                materials + 68 * i);
            D3DMATERIAL9 copy = *material;
            copy.Diffuse.a *= mdl::Accessory(accessory)->opacity;
            device->SetMaterial(&copy);
            float edge[4] = {
                (material->Ambient.r * light[9] + material->Emissive.r) *
                    edgeFactor,
                (material->Ambient.g * light[10] + material->Emissive.g) *
                    edgeFactor,
                (material->Ambient.b * light[11] + material->Emissive.b) *
                    edgeFactor,
                copy.Diffuse.a};
            float specular[4] = {
                material->Specular.r * light[5],
                material->Specular.g * light[6],
                material->Specular.b * light[7], material->Power};
            if (specular[3] == 0.0f)
                specular[3] = 0.1f;
            float diffuse[4] = {
                material->Diffuse.r * light[9],
                material->Diffuse.g * light[10],
                material->Diffuse.b * light[11], 1.0f};
            fx::SetFloatArray(effect, "EgColor", edge, 4);
            fx::SetFloatArray(effect, "SpcColor", specular, 4);
            fx::SetFloatArray(effect, "DifColor", diffuse, 4);
            SelectEffectTechnique(accessory, sub, device, effect, i,
                AccessoryScreenTexture(app));
            fx::Begin(effect);
            fx::BeginPass(effect);
            mme::DrawAccessorySubset(app, accessory, i);
            fx::EndPass(effect);
            fx::End(effect);
        }
        mdl::Accessory(accessory)->currentMaterial = -1;
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        app->ActiveRenderObject() = nullptr;
    }
    SetAccessoryLight(app, device, false);
}

}  // namespace mikudancestudio
