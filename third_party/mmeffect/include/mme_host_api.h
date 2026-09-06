// ===========================================================================
// mme_host_api.h - 内置 MMEffect.dll 的宿主集成 API
// ===========================================================================
// 原版 MME 通过 d3d9.dll 代理 + IAT/vtable hook 拦截 MMD 的设备调用；本工程
// 自带完整渲染管线，宿主（MikuDanceStudio.exe）在渲染管线的对应位置直接
// 调用本 API。语义与原 MMHack 拦截层逐点对应：
//
//   宿主调用点                        原版对应（MMHack 拦截）
//   ------------------------------    -------------------------------------
//   MmeHostSetMainWindow              CreateDevice 的属主链主窗口探测
//   MmeHostSetStandardEffect          Hooked_D3DXCreateEffectFromResourceA
//   MmeHostSetDrawnWindow             Present 记录的呈现窗口
//   MmeHostClear                      设备 Clear 槽 43
//   MmeHostBeginScene                 设备 BeginScene 槽 41（含惰性 Initialize
//                                     与对象增删差异、OnBeginScene）
//   MmeHostDrawPrimitive              设备 DrawPrimitive 槽 81
//   MmeHostDrawIndexedPrimitive       设备 DrawIndexedPrimitive 槽 82
//   MmeHostPreRenderTargetCopy        UpdateSurface/GetRenderTargetData/
//                                     StretchRect 槽 30/32/34 的提前触发
//   MmeHostEndScene                   设备 EndScene 槽 42（含 OnEndScene）
//   MmeHostOnLost/OnResetDevice       设备 Reset 槽 16 前后
//   MmeHostDestroyDevice              设备 Release 归零销毁路径
//   MmeHostNotifyPmmLoaded/Saved      Hooked_CreateFileW 的 .pmm 跟踪
//   MmeHostRecordTextureFile          Hooked_D3DXCreateTextureFromFileEx* 记录
//
// 绘制约定与原版一致：宿主把 Draw* 交给本 API 后不再自行下发绘制——
// 效果引擎要么经分配的特效技术重发，要么原始转发。
// =========================================================================//
#ifndef MIKUDANCESTUDIO_MME_HOST_API_H_
#define MIKUDANCESTUDIO_MME_HOST_API_H_

#include <windows.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

// 记录 MMD 主窗口（MME 菜单注入与子类化目标）。在设备创建后、首帧前调用。
void __cdecl MmeHostSetMainWindow(HWND window);

// 记录宿主内置标准效果（资源 117/118 编译的 ID3DXEffect*，以不透明指针传
// 入）并注册 12 个 MME 标准技术句柄。等价于原版对
// D3DXCreateEffectFromResourceA 成功路径的拦截记录。
void __cdecl MmeHostSetStandardEffect(void* effect);

// 记录当前呈现窗口（GetDrawnWindow 的来源；普通渲染为主窗口，AVI 输出时
// 为录制窗口——MME 会子类化它以取鼠标坐标）。
void __cdecl MmeHostSetDrawnWindow(HWND window);

// 帧清除。内部先与跟踪渲染目标比对判定主目标，主目标清除记录清除色并经
// OnClear（效果启用时抑制清除，由后处理链自行 Clear）。返回设备 Clear 结果。
HRESULT __cdecl MmeHostClear(IDirect3DDevice9* device, unsigned long rect_count,
                               const D3DRECT* rects, unsigned long flags,
                               D3DCOLOR color, float z, unsigned long stencil);

// 帧 BeginScene 状态机：编辑模式采样、惰性 Initialize、对象增删差异
// （OnCreateModel/OnDeleteModel）、世界矩阵缓存、渲染目标跟踪、OnBeginScene
// （渲染遍数规划在此发生）。notEditMode != 0 表示非编辑态（AVI 输出/模态）。
// 内部调用真实 BeginScene 并返回其结果。
HRESULT __cdecl MmeHostBeginScene(IDirect3DDevice9* device, int not_edit_mode);

// 绘制转发：效果引擎接管（经分配特效重发）或原始转发。返回绘制 HRESULT。
HRESULT __cdecl MmeHostDrawPrimitive(IDirect3DDevice9* device,
                                       D3DPRIMITIVETYPE type,
                                       unsigned int start_vertex,
                                       unsigned int primitive_count);
HRESULT __cdecl MmeHostDrawIndexedPrimitive(IDirect3DDevice9* device,
                                              D3DPRIMITIVETYPE type,
                                              int base_vertex_index,
                                              unsigned int min_vertex_index,
                                              unsigned int vertex_count,
                                              unsigned int start_index,
                                              unsigned int primitive_count);

// 在 EndScene 之前回读主渲染目标（后台缓冲捕获/AVI 采样）前调用：若本帧
// OnEndScene 尚未触发则先触发（保证读到的是后处理后的结果）。
void __cdecl MmeHostPreRenderTargetCopy(IDirect3DDevice9* device);

// 帧 EndScene：触发一次 OnEndScene（后处理链运行）后调用真实 EndScene。
HRESULT __cdecl MmeHostEndScene(IDirect3DDevice9* device);

// 设备丢失/重置对（宿主 Reset 前后各一次，对应原 Reset 槽 16 拦截）。
void __cdecl MmeHostOnLostDevice(IDirect3DDevice9* device);
void __cdecl MmeHostOnResetDevice(IDirect3DDevice9* device);

// 设备销毁：Cleanup + 释放全部跟踪状态（原设备 Release 归零路径）。
void __cdecl MmeHostDestroyDevice(IDirect3DDevice9* device);

// .pmm 工程 加载/保存 通知（原 Hooked_CreateFileW 跟踪）。loaded 为一次性，
// saved 入队依次弹出。
void __cdecl MmeHostNotifyPmmLoaded(const wchar_t* path);
void __cdecl MmeHostNotifyPmmSaved(const wchar_t* path);

// 纹理创建记录（原 Hooked_D3DXCreateTextureFromFileEx* 跟踪；toon01..10
// 裸名别名自动登记）。
void __cdecl MmeHostRecordTextureFile(const wchar_t* path,
                                        IDirect3DBaseTexture9* texture);

// 效果引擎已初始化且未被禁用（宿主用于 Accessory 手动 DrawSubset 路径等
// 需要“绕开 ID3DXMesh 内部绘制”的决策点）。仅当标准效果已注册后可能为 1。
int __cdecl MmeHostEffectsActive(void);

#ifdef __cplusplus
}
#endif

#endif  // MIKUDANCESTUDIO_MME_HOST_API_H_
