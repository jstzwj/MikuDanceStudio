# CPU 完整帧与常驻预览

> 历史阶段说明：2026-10-29 已移除本文描述的 `Renderer Preview` 菜单、窗口、
> 环境入口及 `--resident-preview`／`--preview-ui` 参数。以下旧命令不适用于当前二进制，
> 旧验收证据按原范围保留。当前入口和隐藏窗口测试见
> [编辑器视口](RHI_EDITOR_VIEWPORT.md)与[共享控制器](RHI_PREVIEW_CONTROLLER.md)，
> 旧菜单验收详情见[历史记录](RHI_PREVIEW_CONTROLLER_HISTORY.md)。

本阶段将场景生产输入、共享渲染资源管理和原生后端执行分开。沿用 Unity 的职责边界，不复制其实现。Windows 主窗口继续使用 DX9；独立预览的菜单入口可用于正式构建，运行时默认关闭。共用工厂、控制器和菜单用法见 [预览控制层](RHI_PREVIEW_CONTROLLER.md)。

## 职责与数据流

1. `SceneSession` 供应相机、灯光和时间轴状态。旧运行时继续求解 IK、物理和最终骨骼姿态，CPU 蒙皮生产模型主顶点、描边顶点和索引。饰品从独立系统内存导入获得 CPU 网格和材质；纹理准备属于明确的后端兼容边界。
2. 固定场景遍历直接生产地面线、地面面、饰品、投影阴影、模型材质与描边片段，保留实际顺序。`SceneFrameDesc` 从应用的 CPU 参数声明目标尺寸、清屏和已初始化视口，不读取 GPU 对象。
3. `CpuSceneFrameAssembler` 独立组装清屏、资源发布与绘制事件；`SceneDrawReplacement.boundary` 仅供旧诊断观察者使用，组装器不读取它。DX9 捕获和 `ReplaceSceneDraws` 只负责字段、字节和图像验收，其输出不作为完整 CPU 帧的输入。
4. 常驻 `SceneRenderer` 消费完整帧，管理上传、复用、提交和延迟释放。DX11/DX12 执行命令、管理交换链和完成 fence。生产器不保存 GPU handle、设备身份或 epoch。

## 资源与失败语义

- 几何身份按对象命名空间隔离；模型的索引在材质、阴影与描边之间复用。地面与饰品使用独立身份。
- 纹理可声明规范 CPU 资源身份，共享 Toon 和重复材质引用只上传一份。未声明身份的纹理保留局部命名空间，不能凭内容相似推断为同一资产。
- 不变内容保留 ID/version，动画只更新变化的几何版本。资源隐藏后释放 CPU 字节，仅保留身份和版本墓碑；重新出现递增版本，避免提交已退休版本。
- 同帧同身份的矛盾内容阻断整帧；失败不提交资源版本，也不修改上次输出。整场景卸载重置帧资源命名空间，并释放对应渲染所有权；可见性切换保留版本墓碑。
- 检查每个场景原生绘制是否有 CPU 生产器。空饰品材质的 `DrawSubset` 明确表示无绘制；原生捕获开启时仍检查其真实范围。
- 当前仅支持一个 RGBA8/D24S8 单采样目标、无 MME 场景。背景图片/AVI、动态屏幕纹理、立体显示、线框、PMX 未验收路径、自阴影/回调合成及额外目标操作均阻断，不呈现部分帧。菜单打开的预览窗口显示阻断原因；仅由诊断环境启动的窗口在输入未准备好时隐藏。
- 空场景是一个清屏事件、零资源的完整帧。带资源发布却没有绘制的截断流仍被拒绝。

## 默认关闭的独立窗口

正式构建通过 **Renderer Preview → Direct3D 11 / Direct3D 12** 选择后端，无需捕获环境。诊断构建另支持 `MDS_RHI_CPU_SCENE_PREVIEW=dx11|dx12|both`。设备替换后保留同一 `ModelOptions`；`diagnosticAlpha12`、`diagnosticTextureFma`、`diagnosticSpecularFma` 分别默认关闭，预览开关不自动开启数学兼容策略。

普通预览直接消费完整 CPU 帧，无需 DX9 捕获。显式审计模式在当前进程的完整 CPU 帧字段/字节/逐绘制图像门禁通过后开放；检查点可以同时运行原生参考捕获。独立窗口缩放只调整显示视口，保持相机输入；图像验收使用原场景精确目标尺寸，DXGI 可将该图像缩放到窗口客户区。

最小化及恢复期间的零尺寸客户区均暂停提交，尺寸有效后继续。零尺寸不会触发 `ResizeBuffers` 或永久禁用预览。缩放由交换链 `Rebuild(Resize)` 管理，不清空纹理缓存；F5 触发独立预览设备替换，先停止/排空提交、释放旧原生所有权、重绑定，再从 CPU 数据上传。旧准备帧必须被拒绝。关闭预览窗口不退出主编辑器。

诊断日志 `cpu_resident.<pid>.csv` 记录每帧资源数、CPU 字节、身份数量、上传数、纹理上传数、退休资源数、设备/epoch 和窗口/图像验收。加载过程中不支持的临时状态记录为 `BLOCKED`，不提交给后端。

## 复验入口

```powershell
python tools/run_cpu_geometry_diagnostic.py `
  --gui build-x86/build/Release/pmm_v1_relocation_gui_test.exe `
  --application build-x86/build/Release/MikuDanceStudio.exe `
  --fixture C:/Users/jstzw/Downloads/MikuMikuDanceE_v932/UserFile/sample.pmm `
  --output exports/cpu-frame-resident-example-x86 `
  --preview --materials 12,13,14,15,16 --play --lifecycle --inputs audit `
  --history --history-export --ground --projected-shadows `
  --cpu-prefix full-scene --mip-prepare dx9 --alpha12 --texture-fma --specular-fma `
  --live-history --screen-source empty-background --resident-preview both --preview-ui
```

同一入口将 v1 缺失模型重选、编辑、跳帧、播放、Save As 与 v2 回读连接起来；检查窗口实际缩放、最小化恢复及设备替换。窗口验收要求两个场景进程都有图像、呈现、缩放、暂停和替换证据，且确实存在跨帧上传复用；每个后端的窗口图像数量必须与该进程的完整场景检查点数量一致。离屏成功不能代替窗口验收。

无界面 CTest：`rhi_scene_frame` 验证顺序、跨帧身份/版本、失败原子性、隐藏后恢复及清屏；`rhi_scene_renderer` 额外运行 1000 个 CPU 组装帧，只更新模型顶点，模型/饰品共用一份纹理，检查索引和纹理复用、设备替换与卸载释放。Windows x64/x86 和独立 Linux 资源测试运行同一可移植实现；macOS 由既有资源 CI 矩阵覆盖，不能据此宣称已在本机会话实测。

## 验收记录

2026-10-14：共用预览控制层与应用菜单完成新二进制的 x86/x64 audit → direct 四轮验收，全部 `PASS`。每轮九个检查点、1145 条图像/离屏生命周期记录、559 次 CPU 字段/字节对照一致。直接模式的 x86 196 条、x64 222 条几何记录全部来自 CPU，普通帧原生字段审计为零；同时验证同进程卸载/重载、每个后端三轮菜单打开关闭、缩放、最小化和设备替换。详细证据、配置与适用范围见 [预览控制层](RHI_PREVIEW_CONTROLLER.md) 的当日记录及 `exports/preview-controller-acceptance.json`。以下保留前期版本的验收记录，不沿用其旧二进制的审计许可。

2026-10-13 的通过记录为 `exports/cpu-frame-resident-x86e/run.json` 与 `exports/cpu-frame-resident-x64a/run.json`。新的工厂、控制器和菜单验收记录见 [预览控制层](RHI_PREVIEW_CONTROLLER.md)，不沿用旧二进制的审计许可。图形验收以完整 `run.json` 的九检查点和 `residentPreviewExact` 为准；首轮检查点的逐绘制通过不能代替持续预览与恢复验收。

2026-10-13：x86/x64 的真实 PMM v1→Save As→v2 回读流程全部通过。两种架构各完成九个实时检查点、1145 条逐绘制图像/离屏生命周期记录、559 次 CPU 字段/字节对照，覆盖全部六类绘制。DX11/DX12 独立窗口分别完成 v1 的五次、v2 的四次原生图像验收，均零像素差异；四组“场景进程×后端”均通过实际缩放、最小化恢复和设备替换。

| 常驻证据 | x86 | x64 |
|---|---:|---:|
| DX11 实际呈现帧（v1 / v2） | 1107 / 21 | 1803 / 23 |
| DX12 实际呈现帧（v1 / v2） | 1106 / 20 | 1803 / 22 |
| 最大驻留资源 / 身份数量 | 50 / 50 | 50 / 50 |
| 最大 CPU 缓存字节 | 2211558 | 2211558 |
| 最大退休资源积压 | 0 | 0 |
| 每个场景跨检查点保持单一版本/哈希的索引资源 | 18 | 18 |

两个原始 PMM 验收入口均保留原文件哈希。各进程启动时出现一个未准备好 Mip 的临时 CPU 帧，按契约整帧阻断；通过图像门禁后的持续提交均成功。上述 CPU 字节和资源数是渲染器可追踪的资源占用，不代表整个进程或驱动的总内存。实际帧数受运行时序影响，验收不依赖固定帧数。

本轮修复了最小化恢复时短暂零尺寸被误判为永久失败的问题；GUI 测试使用真正的窗口恢复操作。x86/x64 各实际记录四次零尺寸暂停，随后均成功恢复呈现。汇总 `exports/cpu-frame-acceptance.json` 已更新为本机 TITAN Xp、单采样、无 MME 范围的 `PASS`，预览及数学兼容开关仍默认关闭，主窗口继续使用 DX9。MSAA、MME、PMX 与其他 GPU，以及 macOS 实测，均不包含在该图形通过结论中。

以下保留前一轮的未完成状态，便于追溯：

2026-10-12：x64/x86 各 7 项 CPU/WARP 回归通过，新增的清屏卸载检查分别通过；Linux 13 项资源测试通过。`exports/cpu-frame-acceptance.json` 保存当前二进制哈希和证据索引，状态为 `GRAPHICS_PENDING`。

首轮 x86 真实 v1 PMM 的 57 次绘制，DX11/DX12 共 117 行逐边界/离屏生命周期记录零差异，两个独立窗口首次呈现也均与原生检查点一致。持续帧暴露了饰品空材质的观察者误判，已修正；后续 DX9 硬件创建设备再次失败，错误为 `0x8876086C/0x8876086A`。因此目前没有宣称修正后的持续窗口流程、全部九检查点或 x64 真实窗口验收通过，预览和数学兼容选项均维持默认关闭。macOS 实测仍待 CI。
