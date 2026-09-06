# 内置 MMEffect（MikuMikuEffect v0.37 效果引擎）

MikuMikuEffect v0.37 的效果引擎，以**直接内置**方式集成进 MikuDanceStudio——
不经 d3d9.dll 代理、不经 IAT/vtable hook、不经运行时注入，编译为本工程自
有的 `MMEffect.dll` 并由 `MikuMikuDanceE.exe` 静态链接，在渲染管线的对应
位置被宿主直接调用。

来源与证据链见逆向重建工程（功能级还原，源码逐函数可对应反编译体）：
`MMEffect逆向/MMEffect`（含完整反编译证据、阶段笔记与验收工具）。

## 去注入化改造

原版套件由三个模块经 hook 链注入 MMD：

```
d3d9.dll(代理) ──LoadLibrary──▶ MMHack.dll ──11 回调──▶ MMEffect.dll
      │ IAT patch: Direct3DCreate9 / CreateFileW / D3DX* / GetKeyState
      └ vtable 包装: IDirect3D9(17) / Device(119) / ID3DXMesh(29)
```

本目录的对应关系：

| 原版机制 | 本工程等价物 |
|---|---|
| d3d9.dll 代理 | 删除（仅为把 MMHack 带进进程） |
| MMHook IAT/vtable hook | `src/mme_host.cpp` 的 `MmeHost*` 导出，由宿主 `src/render/mme/mme_bridge.cpp` 在管线对应点直接调用（BeginScene/EndScene/Clear/Draw*/Reset/销毁逐一移植自原拦截逻辑，状态捕获仍走设备窥探 + Exp* 查询） |
| Hooked_CreateFileW 的 .pmm 跟踪 | `MmeHostNotifyPmmLoaded/Saved` ← `pmm_load_v2.cpp` / `pmm_save.cpp` 成功点 |
| Hooked_D3DXCreateEffectFromResourceA 记录标准效果 | `MmeHostSetStandardEffect` ← `d3d_init.cpp`（资源 117/118 编译的内置效果） |
| Hooked_D3DXCreateTextureFromFileEx* 记录纹理 | `MmeHostRecordTextureFile` ← `path_resolve.cpp` / `toon_textures.cpp` |
| MMHack 22 个状态查询导出 | 同模块内直连（`src/mmhack/`，逐字移植） |
| MMEffect 11 个回调导出 | 同模块内直连（`src/mmeffect/`，逐字移植；惰性 Initialize 保持原首帧时机） |
| d3dx9_XX.dll 构建期依赖 | 无：`include/d3dx9.h` 为自写最小 ABI 镜像（vtable 槽位与宿主 `fx_slots.hpp` 的原版实证一致），自由函数经 `src/d3dx9_dyn.cpp` 转发到运行时加载的 d3dx9_XX.dll |

## 链接方向与循环导入

`MMEffect.dll` 导入宿主的 37 个 `Exp*` 导出。exe 的导入库在其自身链接时才
生成，两边直接互链构成文件级循环，故 CMake 先用
`lib /def:exports/MikuMikuDance.def /NAME:MikuMikuDanceE.exe` 预生成导入库
（`mme_host_importlib` 目标）；加载器随后按模块名对已映射的 exe 解析导入。

## 目录

```
include/    mme_abi.h / mmhack_api.h / MMDExport.h（ABI 头，随逆向工程）
            mme_host_api.h（宿主集成面，唯一新增 ABI）
            d3dx9.h（自写最小 D3DX9 ABI 镜像）
src/mmhack/  状态层（registry/材质表/光照矩阵/22 查询）
src/mmeffect/ 引擎本体（SAS 解释器、pass 规划、材质绑定、EMM、UI、动画纹理）
src/mme_host.cpp   MmeHost* 集成层（原设备拦截逻辑的重宿主）
src/d3dx9_dyn.cpp  D3DX 自由函数运行时转发
res/        菜单 101/107/109/110、对话框 105/108、图标（原版资源重建）
MMEffect.def 宿主集成导出表（16 个 MmeHost*）
```

## 宿主接入点速查

- 帧：`frame_scene.cpp`（Clear/BeginScene/回读前触发/EndScene）
- 绘制：`model_renderers.cpp` / `accessory.cpp` / `debug_geometry.cpp`
  （模型材质/描边/地面影/深度图/地面网格/背景 quad/附件 DrawSubset 复刻/
  Gizmo 轴网）
- 生命周期：`d3d_init.cpp`（创建）、`device_reset.cpp`（丢失/重置）、
  `shutdown_cleanup.cpp`（销毁）
- 编辑态语义：`MmeHostBeginScene(notEditMode)`——非编辑态 = AVI 录制窗口
  存在（替代原版对 MMD 帧编辑控件 0x13D/0x1A1 的探测）

标准效果未就绪（无 d3dx9_XX.dll 或着色器低于 SM2）时所有桥接退化为直通，
行为与未集成 MME 完全一致。
