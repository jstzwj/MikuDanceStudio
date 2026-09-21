// mme_host.cpp - MmeHost* 集成层：原 MMHack.dll 设备拦截逻辑的去 hook 化
// 重宿主。
//
// 原版经由 d3d9.dll 代理 + IAT/vtable 包装器拦截 MMD 的设备调用；本文件把
// 每个拦截点的逻辑（[0x180002de0] BeginScene 状态机、[0x180003cb0] EndScene、
// [0x180003a20] Clear、[0x180002780] DrawPrimitive、[0x1800027f0]
// DrawIndexedPrimitive、[FUN_180001db0] 设备销毁，以及 hook 侧的标准效果登
// 记/PMM 跟踪/纹理记录）原样移植为宿主可直接调用的导出函数。状态捕获仍通
// 过设备状态窥探 + Exp* 宿主查询完成，与原版一致。
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

// src/exports/effect_api.cpp 的内部直取（非导出，C 链接）：模型/附件的原始
// 宽字符路径（model+0x24BC / acc+0x29C，即加载所用 D3DXLoadMeshFromXW 宽路
// 径）。原版 MMHack 全程保留该宽路径：[0x1800043b0] hook 把它原样登记进
// ObjData+0x30（wstring，sub_18000f150 直接拷贝），[sub_18000f340] 读回后传
// [FUN_18000e020]（wfopen_s 解析材质表/toon，无 ANSI 往返）。内置版同样直
// 取——ExpGetPmdFilename 的 Wide→SJIS 输出再经 CP_ACP 回宽对非 SJIS 字符
// 有损，会破坏材质表/toon 文件解析。MMEffect.dll 无法从 exe 导入库解析非
// 导出符号，故由宿主桥经 MmeHostSetPathProviders 注入函数指针（等价于原
// 版 MMHack 与 exe 间的进程内直连）。
static const wchar_t* (__cdecl* g_mmdModelPathW)(int) = nullptr;
static const wchar_t* (__cdecl* g_mmdAcsPathW)(int) = nullptr;

namespace {

// 原 MyDirect3DDevice9 包装器的自有字段（+0x1c/+0x20/+0x28/+0x2c 与三张对
// 象缓存图），本工程单设备假设与原版一致。
struct MmeHostDeviceState {
    mme::DeviceInitialization initialization;     // 包装器 +0x1c 的生命周期
    IDirect3DSurface9* trackedRT = nullptr;       // 包装器 +0x20（借用指针）
    int currentObject = 0;                        // 包装器 +0x28
    int endSceneFired = 0;                        // 包装器 +0x2c
    std::list<MmhCachedObject*> objectList;       // 包装器 +0x58 节点表
    std::unordered_map<unsigned long long, MmhCachedObject*> objectById;   // +0x30
    std::map<int, MmhCachedObject*> objectByOrder;                          // +0x60
};

MmeHostDeviceState g_host;

// [FUN_180002de0 创建路径 4936-4943 行] 新检出对象的每对象数据。材质表由
// FUN_18000e020 直接解析模型文件（PMX 内嵌名 / PMD 的 "<model>.txt" 行与
// toon 引用）；附件保持空表——GetMaterialName [0x180001130] 只服务
// isPmd == 1 的对象。
void MmhPopulateObjectData(unsigned long long id, bool isPmd, int hostIndex,
                           const wchar_t* modelPath)
{
    (void)hostIndex;
    MmhObjData& d = g_mmh.objData[id];
    d.id = id;
    d.isPmd = isPmd;
    // 宽字符模型路径（原版: ObjData+0x30 wstring，FUN_18000f340 读回并传给
    // FUN_18000e020）：直取宿主原始宽路径，不经窄字符往返。
    d.modelName = (modelPath != nullptr) ? modelPath : L"";
    d.mats.clear();
    if (isPmd) {
        // [FUN_18000e020] 原版在 OnCreateModel 之前、以 &ObjData.materials
        // 为输出调用。
        MmhMaterialTableLoadFromFile(d.modelName.c_str(), &d.mats);
    }
    // 附属信息 (+0x240/+0x244)：原版 ObjData 从不写入这两个字段，宿主填充
    // 亦不快照——GetAcsAttachedPmd [0x1800012d0] 每次调用时经 sub_18000F2B0
    // 的借位分支 [0x18000f331] 直接读宿主附件对象内存（本工程等价实现见
    // mmhack_getters.cpp：AccessoryRecord::parentModel/parentBone），保持关键
    // 帧驱动的附属变化实时可见。此处保留 -1/0 构造默认仅作布局锚点。
    d.attachPmdIndex = -1;
    d.attachBoneIndex = 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 窗口 / 标准效果 / 文件通知（原 hook 侧记录路径）
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

    // [原 MMHack DllMain] exe 目录下的 MMEffect.debug 存在则置调试模式
    // （x64 原版在 MMHack.dll 旁检查，与其同目录 = 宿主 exe 目录）。
    // 探测仅一次（宿主桥可能在帧级刷新时重复调用本函数）。
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
    // [0x180004440 Hooked_D3DXCreateEffectFromResourceA] 仅当
    // qword_18006E7A0（currentEffect）尚未设置且新建效果非空时登记一次
    // [0x1800044b6 条件 !qword_18006E7A0 && v8 && *v8]；此后的调用直接返回，
    // 不覆盖已缓存效果也不重登记 12 个技术句柄。宿主在设备初始化尾调用。
    if (effect == nullptr)
        return;
    if (g_mmh.currentEffect != nullptr)
        return;
    // [0x1800044c7 sub_180006350] 登记前清空技术缓存图（原版 hash 图
    // qword_18006CCD0/DAT_18006ccf0 族的节点释放）。
    g_mmh.tecCache.clear();
    ID3DXEffect* fx = static_cast<ID3DXEffect*>(effect);
    for (int i = 0; i < MMH_TEC_COUNT; ++i) {
        D3DXHANDLE handle = fx->GetTechniqueByName(g_tecTable[i].name);
        if (handle != nullptr)
            MmhRecordTecHandle(handle, i);
    }
    // [0x18000451d] 12 个句柄登记完成后才缓存当前效果。
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
// 帧级拦截点
// ---------------------------------------------------------------------------

// [0x180003a20] 设备 Clear 槽 43。
HRESULT __cdecl MmeHostClear(IDirect3DDevice9* device, unsigned long rect_count,
                               const D3DRECT* rects, unsigned long flags,
                               D3DCOLOR color, float z, unsigned long stencil)
{
    IDirect3DSurface9* surf = nullptr;
    device->GetRenderTarget(0, &surf);
    if (surf != nullptr)
        surf->Release();                         // 仅为借用比对
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

// [0x180002de0] 设备 BeginScene 槽 41 - 帧首状态机。
HRESULT __cdecl MmeHostBeginScene(IDirect3DDevice9* device, int not_edit_mode)
{
    g_host.endSceneFired = 0;                    // +0x2c = 0
    g_mmh.notEditMode = not_edit_mode;           // DAT_18006e796（宿主语义供给，
                                                 // 替代 MmhProbeEditMode 的
                                                 // 0x13D/0x1A1 控件探测）
    g_mmh.frameTime = ExpGetFrameTime();         // DAT_18006e79c

    // 惰性 MMEffect 初始化：首个 BeginScene 调用 Initialize。
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

    HRESULT hr = device->BeginScene();           // 真实槽 41 (+0x148)
    if (hr != 0)
        return hr;

    // --- 构建当前存活的宿主对象 id 集 ----------------------------------------
    int pmdNum = ExpGetPmdNum();
    int acsNum = ExpGetAcsNum();
    std::map<unsigned long long, bool> liveIds;
    for (int i = 0; i < pmdNum; i++)
        liveIds[(unsigned long long)(uintptr_t)ExpGetPmdID(i)] = true;
    for (int i = 0; i < acsNum; i++)
        liveIds[(unsigned long long)(uintptr_t)ExpGetAcsID(i)] = true;

    // --- 阶段 1+2: 清除已消失对象的缓存 (OnDeleteModel) -----------------------
    mme::RemoveMissingHostObjects(g_host.objectList, g_host.objectById,
        liveIds, [device](MmhCachedObject* obj) {
            OnDeleteModel(device, obj->id);
            if (obj->texRef != nullptr)
                static_cast<IUnknown*>(obj->texRef)->Release();
            g_mmh.objData.erase(obj->id);
            delete obj;
        });

    // --- 阶段 3: 为新对象建立缓存 (OnCreateModel) ------------------------------
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

    // --- 阶段 4: 重建绘制次序图 -----------------------------------------------
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

    // 世界矩阵与逆（光照矩阵计算使用）
    device->GetTransform(D3DTS_WORLDMATRIX(0), &g_mmh.worldMatrix);
    D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&g_mmh.worldInverse), nullptr,
                      reinterpret_cast<const D3DXMATRIX*>(&g_mmh.worldMatrix));

    // 跟踪当前渲染目标（借用；销毁路径不释放——与原版一致）。
    g_host.trackedRT = nullptr;                  // +0x20 = 0
    device->GetRenderTarget(0, &g_host.trackedRT);
    if (g_host.trackedRT != nullptr)
        g_host.trackedRT->Release();

    g_mmh.currentSubsetIndex = -1;               // DAT_18006ccc0 = -1
    g_host.currentObject = 0;                    // +0x28 = 0

    OnBeginScene(device);                        // DAT_18006e800
    return 0;
}

// [0x180003b50/0x180003bd0/0x180003c50] 槽 34/30/32：EndScene 之前回读跟踪
// 渲染目标时提前触发一次 OnEndScene（后台缓冲捕获/AVI 采样路径）。
void __cdecl MmeHostPreRenderTargetCopy(IDirect3DDevice9* device)
{
    if (device == nullptr)
        return;
    if (g_host.endSceneFired == 0) {
        OnEndScene(device);
        g_host.endSceneFired = 1;
    }
}

// [0x180003cb0] 设备 EndScene 槽 42。
HRESULT __cdecl MmeHostEndScene(IDirect3DDevice9* device)
{
    if (g_host.endSceneFired == 0) {
        OnEndScene(device);
        g_host.endSceneFired = 1;
    }
    return device->EndScene();
}

// ---------------------------------------------------------------------------
// 绘制拦截点
// ---------------------------------------------------------------------------

namespace {

// [DIP 4238-4281 行] 固定功能纹理捕获。
void MmhCaptureFixedFunctionTextureState(IDirect3DDevice9* device)
{
    (void)device;
    g_mmh.effectFileUsed = false;         // DAT_18006e795 = 0
    // 标志 dword 复位: 字节 0..2 = 0, 字节 3 = (kind != 1)
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
            break;                        // 级联已结束
        IDirect3DBaseTexture9* tex = nullptr;
        device->GetTexture(s, &tex);
        if (tex == nullptr)
            continue;
        tex->Release();                   // 借用捕获
        DWORD ttf = 0;
        device->GetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
        if (ttf == 2) {                   // D3DTTFF_COUNT2 -> 球贴图级
            g_mmh.sphereTexture = tex;    // DAT_18006e7b0
            g_mmh.sphereUsed = 1;         // byte @0x18006e789 = 1
            DWORD tci = 0;
            device->GetTextureStageState(s, D3DTSS_TEXCOORDINDEX, &tci);
            if (tci == 0x10000)
                g_mmh.sphereAdd = (colorOp == 7 /* D3DTOP_ADD */) ? 1 : 0;
            else {
                // [0x180002C8D] 普通UV驱动的球贴图级登记为子纹理(cd2)标志
                // -> GetSphereMapMode 返回 3。
                g_mmh.subTexUsed = 1;     // byte @0x18006e78a = 1
                g_mmh.sphereAdd = 0;      // DAT_18006e797 = 0
            }
        } else {
            g_mmh.texUsed = 1;            // byte @0x18006e788 = 1
            g_mmh.baseTexture = tex;      // DAT_18006e7a8
        }
    }
}

// [DIP 4282-4341 行] 效果（着色器）纹理捕获。
void MmhCaptureEffectTextureState(IDirect3DDevice9* device)
{
    g_mmh.baseTexture = nullptr;          // DAT_18006e7a8 = 0
    g_mmh.sphereTexture = nullptr;        // DAT_18006e7b0 = 0
    ID3DXEffect* effect = static_cast<ID3DXEffect*>(g_mmh.currentEffect);
    if (effect != nullptr) {
        // [0x1d8 = ID3DXEffect::GetCurrentTechnique()]
        D3DXHANDLE hTec = effect->GetCurrentTechnique();
        // [FUN_1800061a0] 句柄 -> 表索引；未知句柄映射为 0（ColorRenderTec），
        // 与新插入缓存节点的行为一致。
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
                    g_mmh.baseTexture->Release();    // 借用
            }
            if (g_mmh.sphereUsed != 0) {
                // [0x180004316] GetTexture(texUsed ? 2 : 1, &sphereTexture)
                device->GetTexture(g_mmh.texUsed != 0 ? 2 : 1, &g_mmh.sphereTexture);
                if (g_mmh.sphereTexture != nullptr)
                    g_mmh.sphereTexture->Release();  // 借用
            }
        } else {
            // [0x1800027f0 4334 行] 技术不在缓存: 清标志
            g_mmh.texUsed = 0;
            g_mmh.sphereUsed = 0;
            g_mmh.subTexUsed = 0;
            g_mmh.diffuseFlag = 0;
        }
        g_mmh.sphereAdd = 0;
        // [0xb8 = ID3DXBaseEffect::GetBool] 读取 "spadd" 注释；
        // DAT_18006e797 = (result != 0)。
        BOOL spadd = 0;
        if (effect->GetBool("spadd", &spadd) == 0 && spadd != 0)
            g_mmh.sphereAdd = 1;
        g_mmh.effectFileUsed = true;      // DAT_18006e795 = 1
    }
}

}  // namespace

// [0x180002780] 设备 DrawPrimitive 槽 81。效果激活时不转发：复位宿主状态后
// 改调 MMEffect!OnDrawPrimitive（由它重发绘制）。
HRESULT __cdecl MmeHostDrawPrimitive(IDirect3DDevice9* device,
                                       D3DPRIMITIVETYPE type,
                                       unsigned int start_vertex,
                                       unsigned int primitive_count)
{
    if (g_mmh.effectsDisabled || g_host.endSceneFired != 0) {
        return device->DrawPrimitive(type, start_vertex, primitive_count);   // 原始转发
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

// [0x1800027f0] 设备 DrawIndexedPrimitive 槽 82。
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

    // 取（并立即释放）当前顶点着色器以判定着色器路径。
    // [0x1800027f0: 经真实槽 93 (+0x2e8) GetVertexShader]
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
            // 绘制次序图查对象（缺项时保持空默认）。
            std::map<int, MmhCachedObject*>::iterator f =
                g_host.objectByOrder.find(objIdx);
            MmhCachedObject* obj = (f != g_host.objectByOrder.end()) ? f->second : nullptr;
            g_mmh.currentModelID = obj ? obj->id : 0;               // DAT_18006e868
            g_mmh.currentObjectKind = obj ? obj->isPmd : 0;         // DAT_18006e870
        }
        g_mmh.currentSubsetIndex = ExpGetCurrentMaterial();         // DAT_18006ccc0
        g_mmh.currentDrawType = ExpGetCurrentTechnic();             // DAT_18006e798
    }

    // 混合模式标志: GetRenderState(D3DRS_DESTBLEND = 0x14); 目标混合为
    // D3DBLEND_ONE(2) 时置 1。 [真实槽 58 (+0x1d0) GetRenderState]
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
// 设备生命周期
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

// [FUN_180001db0 + FUN_180001c90] 设备销毁簿记：MMEffect!Cleanup、释放每个
// 缓存对象捕获的纹理引用、清空对象缓存与 +0x1c 标志。跟踪渲染目标
// (+0x20) 是借用指针（BeginScene 中即已释放），这里刻意不再释放。
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
