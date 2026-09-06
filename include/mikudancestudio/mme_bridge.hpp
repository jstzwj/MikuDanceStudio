// ===========================================================================
// mme_bridge.hpp - 内置 MMEffect（third_party/mmeffect → MMEffect.dll）宿主桥
// ===========================================================================
// 桥职责：在渲染管线的对应点调用 MMEffect.dll 的 MmeHost* 导出（见
// third_party/mmeffect/include/mme_host_api.h）。设备/标准效果未就绪（无
// d3dx9_XX.dll 或着色器低于 SM2）时所有函数退化为直通设备调用，行为与
// 未集成 MME 时完全一致。
//
// 绘制约定：Draw* 交给桥后宿主不得再自行下发同一绘制——效果引擎要么经
// 分配的特效技术重发，要么原始转发（与原版 MMHack 拦截一致）。
// =========================================================================//
#pragma once

#include <d3d9.h>

namespace mikudancestudio {

class MMDApp;

namespace mme {

// 设备创建后调用（InitD3D 尾部）：注册主窗口与内置标准效果。标准效果为
// 空时 MME 保持不可用，全部直通。
void OnDeviceCreated(MMDApp* app);

// 设备销毁前调用（应用关闭/设备释放路径）。
void OnDeviceDestroyed(MMDApp* app);

// 设备 Reset 前后（PostDeviceReset 内部）。
void OnLostDevice(MMDApp* app);
void OnResetDevice(MMDApp* app);

// 帧级：Clear / BeginScene / EndScene / 主渲染目标回读前触发。
HRESULT ClearScene(MMDApp* app, IDirect3DDevice9* device, unsigned long flags,
                   D3DCOLOR color, float z, unsigned long stencil);
HRESULT BeginScene(MMDApp* app, IDirect3DDevice9* device);
HRESULT EndScene(MMDApp* app, IDirect3DDevice9* device);
void PreRenderTargetCopy(MMDApp* app, IDirect3DDevice9* device);

// 绘制转发（当前对象/材质/技术等状态由 Exp* 查询面提供）。
HRESULT DrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                      unsigned int start_vertex, unsigned int primitive_count);
HRESULT DrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                             int base_vertex_index, unsigned int min_vertex_index,
                             unsigned int vertex_count, unsigned int start_index,
                             unsigned int primitive_count);

// MME 已初始化且效果引擎未被禁用。宿主在需要绕开 ID3DXMesh 内部绘制
// （其持有真实设备、绕过绘制转发）的路径上以此为分路条件。
bool EffectsActive();

// 附件网格的 DrawSubset 等价物：MME 激活时手工设置流/索引/FVF 并经绘制
// 转发（复刻 ID3DXMesh::DrawSubset 的硬件路径）；未激活时直呼原
// DrawSubset。返回绘制 HRESULT。
HRESULT DrawAccessorySubset(MMDApp* app, void* accessory, unsigned long material);

// .pmm 工程 加载/保存 成功点通知（EMM 自动加载/自动保存）。
void NotifyPmmLoaded(MMDApp* app);
void NotifyPmmSaved(MMDApp* app);

// 纹理按路径登记（模型/附件/toon 纹理创建成功点；供 GetToonTexture 查询）。
void RecordTexture(const wchar_t* path, IDirect3DBaseTexture9* texture);

}  // namespace mme
}  // namespace mikudancestudio
