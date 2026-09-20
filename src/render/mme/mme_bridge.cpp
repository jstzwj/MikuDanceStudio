// mme_bridge.cpp - 内置 MMEffect 宿主桥实现。
//
// 见 include/mikudancestudio/mme_bridge.hpp。效果引擎静态内置在宿主中，
// 全部调用直达 MmeHost*，无需代理 DLL 或设备 hook。
//
// [核验结论 2026-09] 原 MMHack 对宿主 EXE 的 user32!GetKeyState IAT hook
// （[0x180004910]，DllMain 经 sub_18000ada0 无条件安装、不依赖 MME 初始
// 化状态）：查询键 == 'E'(69) 且 GetKeyState('E'/VK_CONTROL/VK_SHIFT) 三者
// 高位皆置位时返回 0。但 MMD v932（x64 与 x86 0x42D3A0 同源键表）对该 hook
// 无可见行为——GetKeyState 在 exe 内的唯一调用点是帧级按键轮询分发器
// （x64 sub_7FF7CB446AD0 -> sub_7FF7CB446F50），其固定键表（方向键/Shift/
// Ctrl/Alt/Space/Delete/Esc/Tab/Enter/鼠标/小键盘/字母 XZCV DABS GHKPUJFRL/
// VK 221,226）不含 'E'；且无加速键表（无 LoadAccelerators/Translate-
// Accelerator 导入）、无 GetAsyncKeyState/GetKeyboardState/RegisterHotKey。
// 故原版加载 MMHack 后 MMD 行为与不加载完全一致，该 hook 属行为学死代码
// （MME 自身热键走 WH_KEYBOARD + GetAsyncKeyState，见 mme_ui.cpp 的
// MmeKeyboardHookProc，不受此 IAT hook 影响）。内置版因此不需要、也不应
// 添加任何 Ctrl+Shift+E 屏蔽——本工程按键轮询键表与原版一致（frame_modes.
// cpp / model_query_helpers.cpp），亦无 'E' 查询点。
#include "mikudancestudio/mme_bridge.hpp"

#include <Windows.h>
#include <string.h>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mme_host_api.h"
#include "mesh_subset.hpp"

// src/exports/effect_api.cpp 的内部直取（extern "C"，非导出）：模型/附件
// 原始宽字符路径。经 MmeHostSetPathProviders 注入 MMEffect.dll（等价于
// 原版 MMHook 的 D3DXLoadMeshFromXW hook 宽路径登记，无 SJIS 往返）。
extern "C" const wchar_t* MmdModelPathW(int index);
extern "C" const wchar_t* MmdAcsPathW(int index);

namespace mikudancestudio {
namespace mme {
namespace {

// 设备已登记到内置效果引擎。标准效果是否齐备由惰性 Initialize 判断。
bool g_deviceRegistered = false;
// 已注册给 MME 的主窗口（帧级刷新用；初始化期间 WM_CREATE 的句柄可能
// 尚未写回 app->Hwnd()）。
HWND g_registeredWindow = nullptr;

// ---- sub_18000EFF0（MMHack.dll，每帧 BeginScene 调用）的等价状态 --------
// 0x18006E840（hWnd）：目标编辑框缓存。原版探测顺序：GetDlgItem(主窗口,
// 317) 且类名为 "EDIT"（stricmp）→ 否则回落 GetDlgItem(主窗口, 417)（同
// 样须为 EDIT）；两者皆非 EDIT 时保持空并逐帧重探。317 是旧版 MMD 的帧
// 编辑框 ID（跨版本兼容回落）：MMD v932x64 无 317 号控件（exe 内立即数
// 317 零引用），端口主窗口创建表 ui_controls.inc 也只覆盖 400..567，故
// 固定回落到 417（panel::kCurrentFrameEdit，"EDIT" 类）。
HWND g_editModeControl = nullptr;
// 0x18006E7BD（byte_18006E7BD）：RecWindow（AVI 录制窗）粘滞标志。
bool g_recWindowSticky = false;

// 0x18000EF60（fn，EnumThreadWindows 回调）：候选顶层窗口类名 strcmp
// 精确等于 "RecWindow" → 置 found 并终止枚举。判据仅类名——不是 #32770
// 模态对话框，也不查 IsWindowEnabled/父子/所有者关系。
BOOL CALLBACK FindRecWindowProc(HWND candidate, LPARAM found) {
    char className[32];
    if (GetClassNameA(candidate, className, 32) != 0 &&
        strcmp(className, "RecWindow") == 0) {
        *reinterpret_cast<bool*>(found) = true;
        return FALSE;
    }
    return TRUE;
}

// VA sub_18000EFF0：返回 notEditMode（调用方写 0x18006E796，IsEditMode
// 导出返回其取反；0x18006E79C 帧时间紧随其后由 mme_host.cpp 写入）。
// 语义：目标编辑框被禁用（MMD 播放开始即禁用 415..468，含 417——
// playback_state.cpp），或 RecWindow 存在且已粘滞。粘滞时序：RecWindow
// 出现的当帧仍算编辑态并置粘滞，自次帧起非编辑；消失当帧清除粘滞，
// 返回值只取决于控件启用状态。IsWindowEnabled(NULL)==FALSE：主窗口上
// 找不到 EDIT 目标控件时恒为非编辑态（原版同）。
int ProbeNotEditMode(HWND mainWindow) {
    if (g_editModeControl == nullptr) {
        char className[32];
        HWND probe = GetDlgItem(mainWindow, 317);
        if (probe == nullptr ||
            GetClassNameA(probe, className, 32) == 0 ||
            _stricmp(className, "EDIT") != 0)
            probe = nullptr;
        if (probe == nullptr) {
            // 原版此处顺带写 dword_18006CCB8 = 14208（无消费者的死存储，
            // 不移植）。
            probe = GetDlgItem(mainWindow, 417);
            if (probe != nullptr &&
                (GetClassNameA(probe, className, 32) == 0 ||
                 _stricmp(className, "EDIT") != 0))
                probe = nullptr;
        }
        g_editModeControl = probe;
    }
    bool recPresent = false;
    EnumThreadWindows(GetCurrentThreadId(), FindRecWindowProc,
                      reinterpret_cast<LPARAM>(&recPresent));
    if (!recPresent)
        g_recWindowSticky = false;
    if (IsWindowEnabled(g_editModeControl) == FALSE)
        return 1;
    if (recPresent) {
        if (g_recWindowSticky)
            return 1;
        g_recWindowSticky = true;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 设备生命周期
// ---------------------------------------------------------------------------
// [裁决定论 2026-09-14 二次核验，retract 第一轮审计的槽位错读] 原版
// MMHack.dll MyDirect3DDevice9 包装 vtable @0x18005c058 关键槽位
// （vtable 实读 + 反汇编转发目标双重确认）：
//   槽 15(0x78) 0x180003ed0 = GetNumberOfSwapChains 纯直通 jmp [real+0x78]
//                （早期审计误读为 "Reset 直通"）；
//   槽 16(0x80) 0x180003d30 = Reset 拦截：MME OnLostDevice(0x180058970，
//                打印 "Resetting MME...") → 真 Reset call [real+0x80] →
//                MME OnResetDevice(0x180058A20) → qword_18006E7C8 =
//                d3dpp.hDeviceWindow(+0x20)，空则 GetCreationParameters
//                [real+0x48] 的 hFocusWindow(+0x08)；
//   槽 17(0x88) 0x180003cf0 = Present 拦截：仅记录 qword_18006E7C0 =
//                hDestWindowOverride 非空 ? override : qword_18006E7C8
//                （数据源见 BeginScene 内注释），随后 call [real+0x88]，
//                不调任何 MME 通知；
//   槽 18(0x90) 0x180001ef0 = GetBackBuffer 直通 call [real+0x90]。
// MMD 侧（x64）：主循环 sub_7FF7CB4474F0 每帧 call [dev+0x88] Present；
// 仅当 Present 返回 D3DERR_DEVICELOST(0x88760868) 且 TestCooperativeLevel
// ([dev+0x18]) 轮询到 D3DERR_DEVICENOTRESET(0x88760869) 才经
// sub_7FF7CB4C6160 call [dev+0x80] Reset（其余 Reset 入口同走槽 16）。
// 故 MME OnLostDevice/OnResetDevice 只由真实设备 Reset 触发；
// "槽 16 = Present、每帧 Present 触发 Resetting MME" 的旧说法（16/17 相邻
// 槽位混淆）不成立。OnLost/OnReset 的调用条件是两个导出指针均非空
// （0x18006e828/0x18006e830）。本桥等价面：Present 路径零通知（见
// frame_driver.cpp 第 9 节），Reset 前后各一次通知（device_reset.cpp，
// OnResetDevice 紧贴 Reset、InitRenderStates 之前）。
void OnDeviceCreated(MMDApp* app, HWND hwnd) {
    g_deviceRegistered = false;
    g_registeredWindow = nullptr;
    D3DRenderer* renderer = app->Renderer();
    if (renderer == nullptr || renderer->device == nullptr)
        return;
    // Even with no standard effect, register the device and enter the same
    // lazy Initialize state machine as every other initialization failure.
    // MMHack 0x180002E35..0x180002E8F retries on each BeginScene, reports the
    // error, leaves initialization pending, and returns S_OK without starting
    // the real scene. There is deliberately no second failure latch here.
    // InitD3D 于 WM_CREATE 期间运行，app->Hwnd() 此时还是空——以参数句柄
    // 为准（回落到 app->Hwnd() 仅为防御）。
    HWND window = hwnd;
    if (window == nullptr)
        window = static_cast<HWND>(app->Hwnd());
    MmeHostSetPathProviders(&MmdModelPathW, &MmdAcsPathW);
    MmeHostSetMainWindow(window);
    MmeHostSetDrawnWindow(window);
    MmeHostSetStandardEffect(renderer->effect);
    g_registeredWindow = window;
    g_deviceRegistered = true;
}

void OnDeviceDestroyed(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    MmeHostDestroyDevice(renderer != nullptr ? renderer->device : nullptr);
    g_deviceRegistered = false;
}

void OnLostDevice(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr)
        MmeHostOnLostDevice(renderer->device);
}

void OnResetDevice(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr)
        MmeHostOnResetDevice(renderer->device);
}

// ---------------------------------------------------------------------------
// 帧级
// ---------------------------------------------------------------------------
HRESULT ClearScene(MMDApp* app, IDirect3DDevice9* device, unsigned long flags,
                   D3DCOLOR color, float z, unsigned long stencil) {
    if (!g_deviceRegistered)
        return device->Clear(0, nullptr, flags, color, z, stencil);
    return MmeHostClear(device, 0, nullptr, flags, color, z, stencil);
}

HRESULT BeginScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_deviceRegistered)
        return device->BeginScene();
    // 主窗口句柄刷新（初始化时序的兜底：若注册句柄与当前不符则更新，
    // 需在 MME 惰性 Initialize 之前生效）。
    HWND window = static_cast<HWND>(app->Hwnd());
    if (window != nullptr && window != g_registeredWindow) {
        MmeHostSetMainWindow(window);
        g_registeredWindow = window;
    }
    // [0x180003cf0] Present 拦截（设备 vtable 槽 17，call [r10+0x88]；审计
    // 记录误作 GetBackBuffer——那是槽 18，且"第 4 参"恰为 Present 的
    // hDestWindowOverride，非凭空读取）：原版每次 Present 用
    // qword_18006e7c0（GetDrawnWindow 数据源）= hDestWindowOverride 非空 ?
    // override : qword_18006e7c8（[0x180004040 CreateDevice / 0x180003d30
    // Reset] 跟踪的 d3dpp.hDeviceWindow ?: hFocusWindow，MMD 即主窗口）。
    // MMD 主泵（本工程 frame_driver.cpp 同源移植）窗口模式 Present 必传非
    // 空 override：AVI 窗口化录制传 RecWindow(0xA0D24)、浮动视口传浮动窗口
    // (0xA0D38)、常态传主窗口(0xA06B8)，仅全屏传 NULL（回落设备窗口=主窗
    // 口）。MME OnBeginScene [0x1800574e0] 消费 GetDrawnWindow：非主窗口时
    // 子类化该窗口挂鼠标尾巴（特效 MOUSE 语义需读实际呈现窗口）。帧内
    // BeginScene 到 Present 之间无消息泵，门控值不会变化，此处按主泵同一
    // 优先级预置本帧 override（相对原版"Present 时刻写、次帧 BeginScene
    // 消费"最多提前一帧，子类切换终态等价）。
    HWND presentOverride = window;
    if (app->FullscreenMode() == 0) {
        if (app->RecordingWindow() != nullptr)
            presentOverride = app->RecordingWindow();
        else if (app->FloatingWindow() != nullptr)
            presentOverride = app->FloatingWindow();
    }
    MmeHostSetDrawnWindow(presentOverride);
    // 原版语义（sub_18000EFF0 / MmhProbeEditMode）：帧编辑框（317 回落
    // 417）被禁用，或 RecWindow 类窗口（AVI 窗口化录制窗；全屏 3D Vision
    // 录制时 RecordingWindow==主窗口、类名非 RecWindow，不参与判定）出现
    // 满一帧粘滞 → 非编辑态。播放开始即禁用 415..468，故播放/暂停期间
    // notEditMode=1；模态对话框不参与判定（回调仅匹配 RecWindow 类）。
    const int notEditMode = ProbeNotEditMode(g_registeredWindow);
    return MmeHostBeginScene(device, notEditMode);
}

HRESULT EndScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_deviceRegistered)
        return device->EndScene();
    return MmeHostEndScene(device);
}

void PreRenderTargetCopy(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_deviceRegistered)
        return;
    MmeHostPreRenderTargetCopy(device);
}

// ---------------------------------------------------------------------------
// 绘制转发
// ---------------------------------------------------------------------------
HRESULT DrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                      unsigned int start_vertex, unsigned int primitive_count) {
    if (!g_deviceRegistered)
        return device->DrawPrimitive(type, start_vertex, primitive_count);
    return MmeHostDrawPrimitive(device, type, start_vertex, primitive_count);
}

HRESULT DrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                             int base_vertex_index, unsigned int min_vertex_index,
                             unsigned int vertex_count, unsigned int start_index,
                             unsigned int primitive_count) {
    if (!g_deviceRegistered)
        return device->DrawIndexedPrimitive(type, base_vertex_index, min_vertex_index,
                                            vertex_count, start_index, primitive_count);
    return MmeHostDrawIndexedPrimitive(device, type, base_vertex_index,
                                       min_vertex_index, vertex_count, start_index,
                                       primitive_count);
}

bool EffectsActive() {
    return g_deviceRegistered && MmeHostEffectsActive() != 0;
}

// ---------------------------------------------------------------------------
// 附件网格绘制（ID3DXMesh::DrawSubset 的可见等价物）
// ---------------------------------------------------------------------------
HRESULT DrawAccessorySubset(MMDApp* app, void* accessory, unsigned long material) {
    IDirect3DDevice9* device = app->Renderer()->device;
    auto* mesh = static_cast<ID3DXMesh*>(mdl::Accessory(accessory)->mesh);
    if (mesh == nullptr)
        return E_FAIL;
    if (!EffectsActive())
        return mesh->DrawSubset(material);

    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    HRESULT hr = mesh->GetVertexBuffer(&vb);
    if (FAILED(hr) || vb == nullptr)
        return FAILED(hr) ? hr : E_FAIL;
    hr = mesh->GetIndexBuffer(&ib);
    if (FAILED(hr) || ib == nullptr) {
        vb->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = DrawMeshSubsetWithFallback(*mesh, *device, *vb, *ib, material, [&] {
        MeshSubsetPlan plan;
        HRESULT planned = PlanMeshSubset(*mesh, material, plan);
        if (FAILED(planned))
            return planned;
        // Accessory loading clones to the host FVF (XYZ/NORMAL/TEX1). Setting
        // that FVF selects the same declaration as the mesh's DrawSubset.
        device->SetFVF(mesh->GetFVF());
        device->SetStreamSource(0, vb, 0, mesh->GetNumBytesPerVertex());
        device->SetIndices(ib);
        return DrawMeshSubsetPlan(plan, mesh->GetNumFaces(), (mesh->GetOptions() & 1) != 0,
            [device](const D3DXATTRIBUTERANGE& range) {
                return DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, 0,
                    range.VertexStart, range.VertexCount, range.FaceStart * 3,
                    range.FaceCount);
            });
    });
    vb->Release();
    ib->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// 文件通知
// ---------------------------------------------------------------------------
void NotifyPmmLoaded(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    MmeHostNotifyPmmLoaded(app->EnvFileName());
}

void NotifyPmmSaved(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    MmeHostNotifyPmmSaved(app->EnvFileName());
}

void RecordTexture(const wchar_t* path, IDirect3DBaseTexture9* texture) {
    if (!g_deviceRegistered || path == nullptr || texture == nullptr)
        return;
    MmeHostRecordTextureFile(path, texture);
}

}  // namespace mme
}  // namespace mikudancestudio
