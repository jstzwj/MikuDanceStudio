// mme_bridge.cpp - 内置 MMEffect 宿主桥实现。
//
// 见 include/mikudancestudio/mme_bridge.hpp。MMEffect.dll 与本进程静态链接
// （导入库），无需运行时 LoadLibrary；全部调用直达 MmeHost* 导出。
#include "mikudancestudio/mme_bridge.hpp"

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mme_host_api.h"

namespace mikudancestudio {
namespace mme {
namespace {

// 标准效果已注册（MME Initialize 的前置条件齐备）。
bool g_available = false;
// 已注册给 MME 的主窗口（帧级刷新用；初始化期间 WM_CREATE 的句柄可能
// 尚未写回 app->Hwnd()）。
HWND g_registeredWindow = nullptr;

}  // namespace

// ---------------------------------------------------------------------------
// 设备生命周期
// ---------------------------------------------------------------------------
void OnDeviceCreated(MMDApp* app, HWND hwnd) {
    g_available = false;
    g_registeredWindow = nullptr;
    D3DRenderer* renderer = app->Renderer();
    if (renderer == nullptr || renderer->device == nullptr)
        return;
    // Initialize 需要宿主内置标准效果（17 个标准参数句柄的来源）；无
    // d3dx9_XX.dll / 着色器低于 SM2 时 r->effect 为空，MME 整体保持直通。
    if (renderer->effect == nullptr)
        return;
    // InitD3D 于 WM_CREATE 期间运行，app->Hwnd() 此时还是空——以参数句柄
    // 为准（回落到 app->Hwnd() 仅为防御）。
    HWND window = hwnd;
    if (window == nullptr)
        window = static_cast<HWND>(app->Hwnd());
    MmeHostSetMainWindow(window);
    MmeHostSetDrawnWindow(window);
    MmeHostSetStandardEffect(renderer->effect);
    g_registeredWindow = window;
    g_available = true;
}

void OnDeviceDestroyed(MMDApp* app) {
    if (!g_available)
        return;
    D3DRenderer* renderer = app->Renderer();
    MmeHostDestroyDevice(renderer != nullptr ? renderer->device : nullptr);
    g_available = false;
}

void OnLostDevice(MMDApp* app) {
    if (!g_available)
        return;
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr)
        MmeHostOnLostDevice(renderer->device);
}

void OnResetDevice(MMDApp* app) {
    if (!g_available)
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
    if (!g_available)
        return device->Clear(0, nullptr, flags, color, z, stencil);
    return MmeHostClear(device, 0, nullptr, flags, color, z, stencil);
}

HRESULT BeginScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_available)
        return device->BeginScene();
    // 主窗口句柄刷新（初始化时序的兜底：若注册句柄与当前不符则更新，
    // 需在 MME 惰性 Initialize 之前生效）。
    HWND window = static_cast<HWND>(app->Hwnd());
    if (window != nullptr && window != g_registeredWindow) {
        MmeHostSetMainWindow(window);
        MmeHostSetDrawnWindow(window);
        g_registeredWindow = window;
    }
    // 原版语义（MmhProbeEditMode）：帧编辑控件被禁用或 AVI 录制中视为非编
    // 辑态——本工程的对应判定为 AVI 录制窗口存在（播放中仍为编辑态，与
    // MMD 一致：播放不锁帧编辑控件）。
    const int notEditMode = (app->RecordingWindow() != nullptr) ? 1 : 0;
    return MmeHostBeginScene(device, notEditMode);
}

HRESULT EndScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_available)
        return device->EndScene();
    return MmeHostEndScene(device);
}

void PreRenderTargetCopy(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_available)
        return;
    MmeHostPreRenderTargetCopy(device);
}

// ---------------------------------------------------------------------------
// 绘制转发
// ---------------------------------------------------------------------------
HRESULT DrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                      unsigned int start_vertex, unsigned int primitive_count) {
    if (!g_available)
        return device->DrawPrimitive(type, start_vertex, primitive_count);
    return MmeHostDrawPrimitive(device, type, start_vertex, primitive_count);
}

HRESULT DrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type,
                             int base_vertex_index, unsigned int min_vertex_index,
                             unsigned int vertex_count, unsigned int start_index,
                             unsigned int primitive_count) {
    if (!g_available)
        return device->DrawIndexedPrimitive(type, base_vertex_index, min_vertex_index,
                                            vertex_count, start_index, primitive_count);
    return MmeHostDrawIndexedPrimitive(device, type, base_vertex_index,
                                       min_vertex_index, vertex_count, start_index,
                                       primitive_count);
}

bool EffectsActive() {
    return g_available && MmeHostEffectsActive() != 0;
}

// ---------------------------------------------------------------------------
// 附件网格绘制（ID3DXMesh::DrawSubset 的可见等价物）
// ---------------------------------------------------------------------------
namespace {

// 与 ID3DXMesh::GetAttributeTable 使用的表项布局（d3dx9mesh.h 的
// D3DXATTRIBUTERANGE：AttribId/FaceStart/FaceCount/VertexStart/VertexCount）。
struct D3DXATTRIBUTERANGE_SHIM {
    unsigned long AttribId;
    unsigned long FaceStart;
    unsigned long FaceCount;
    unsigned long VertexStart;
    unsigned long VertexCount;
};

// ID3DXBaseMesh/ID3DXMesh vtable 槽位（d3dx9mesh.h 顺序）。
using MeshFnNumFaces = unsigned long(__stdcall*)(void*);            // 槽 4
using MeshFnNumVertices = unsigned long(__stdcall*)(void*);         // 槽 5
using MeshFnFvf = unsigned long(__stdcall*)(void*);                 // 槽 6
using MeshFnBytesPerVertex = unsigned long(__stdcall*)(void*);      // 槽 8
using MeshFnGetVertexBuffer =
    HRESULT(__stdcall*)(void*, IDirect3DVertexBuffer9**);           // 槽 13
using MeshFnGetIndexBuffer =
    HRESULT(__stdcall*)(void*, IDirect3DIndexBuffer9**);            // 槽 14
using MeshFnGetAttributeTable =
    HRESULT(__stdcall*)(void*, D3DXATTRIBUTERANGE_SHIM*, unsigned long*);  // 槽 19

void* MeshSlot(void* mesh, int slot) {
    return (*reinterpret_cast<void***>(mesh))[slot];
}

}  // namespace

HRESULT DrawAccessorySubset(MMDApp* app, void* accessory, unsigned long material) {
    IDirect3DDevice9* device = app->Renderer()->device;
    void* mesh = mdl::Accessory(accessory)->mesh;
    if (mesh == nullptr)
        return E_FAIL;
    if (!EffectsActive()) {
        // 未激活：保持原 DrawSubset（vtable 槽 3）。
        auto drawSubset = reinterpret_cast<HRESULT(__stdcall*)(void*, unsigned long)>(
            MeshSlot(mesh, 3));
        return drawSubset(mesh, material);
    }
    // MME 激活：复刻 DrawSubset 的硬件路径（附件网格在加载时已克隆为
    // FVF 274），使绘制经桥转发被效果引擎接管。
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    auto getVertexBuffer = reinterpret_cast<MeshFnGetVertexBuffer>(
        MeshSlot(mesh, 13));
    auto getIndexBuffer = reinterpret_cast<MeshFnGetIndexBuffer>(
        MeshSlot(mesh, 14));
    if (FAILED(getVertexBuffer(mesh, &vb)) || vb == nullptr)
        return E_FAIL;
    if (FAILED(getIndexBuffer(mesh, &ib)) || ib == nullptr) {
        vb->Release();
        return E_FAIL;
    }
    const unsigned long numVertices =
        reinterpret_cast<MeshFnNumVertices>(MeshSlot(mesh, 5))(mesh);
    const unsigned long numFaces =
        reinterpret_cast<MeshFnNumFaces>(MeshSlot(mesh, 4))(mesh);
    const unsigned long fvf =
        reinterpret_cast<MeshFnFvf>(MeshSlot(mesh, 6))(mesh);
    const unsigned long stride =
        reinterpret_cast<MeshFnBytesPerVertex>(MeshSlot(mesh, 8))(mesh);

    // 属性表：无表（单子集网格）时按整网格绘制，与 DrawSubset 的内部
    // 回退一致。
    unsigned long tableSize = 0;
    auto getAttributeTable = reinterpret_cast<MeshFnGetAttributeTable>(
        MeshSlot(mesh, 19));
    getAttributeTable(mesh, nullptr, &tableSize);
    unsigned long faceStart = 0;
    unsigned long faceCount = numFaces;
    unsigned long vertexStart = 0;
    unsigned long vertexCount = numVertices;
    if (tableSize > 0) {
        D3DXATTRIBUTERANGE_SHIM table[128];
        if (tableSize <= 128 &&
            SUCCEEDED(getAttributeTable(mesh, table, &tableSize))) {
            bool found = false;
            for (unsigned long i = 0; i < tableSize; ++i) {
                if (table[i].AttribId == material) {
                    faceStart = table[i].FaceStart;
                    faceCount = table[i].FaceCount;
                    vertexStart = table[i].VertexStart;
                    vertexCount = table[i].VertexCount;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // 属性缺失：DrawSubset 对此不绘制。
                vb->Release();
                ib->Release();
                return S_OK;
            }
        }
    }

    HRESULT hr = device->SetStreamSource(0, vb, 0, stride);
    if (SUCCEEDED(hr))
        hr = device->SetIndices(ib);
    if (SUCCEEDED(hr))
        hr = device->SetFVF(fvf);
    if (SUCCEEDED(hr) && vertexCount > 0 && faceCount > 0) {
        hr = DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, 0, vertexStart,
                                  vertexCount, faceStart * 3, faceCount);
    }
    vb->Release();
    ib->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// 文件通知
// ---------------------------------------------------------------------------
void NotifyPmmLoaded(MMDApp* app) {
    if (!g_available)
        return;
    MmeHostNotifyPmmLoaded(app->EnvFileName());
}

void NotifyPmmSaved(MMDApp* app) {
    if (!g_available)
        return;
    MmeHostNotifyPmmSaved(app->EnvFileName());
}

void RecordTexture(const wchar_t* path, IDirect3DBaseTexture9* texture) {
    if (!g_available || path == nullptr || texture == nullptr)
        return;
    MmeHostRecordTextureFile(path, texture);
}

}  // namespace mme
}  // namespace mikudancestudio
