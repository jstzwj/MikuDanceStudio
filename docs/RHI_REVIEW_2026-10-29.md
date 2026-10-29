# RHI 现状审查与后续优先级（2026-10-29）

当前 RHI 已具备 DX11／DX12／Vulkan 的实际编辑器视口、离屏设备、版本化资源、
多 Pass 读写依赖校验、内置自阴影、动态 `screen.bmp`、共享后处理、最终颜色输出、
交换链缩放和设备替换。可移植三线性是默认采样档位。
它已经能执行这些共享场景，但仍是围绕 MMD 内置绘制建立的受限接口，尚未成为
能承载任意 MME 效果的通用渲染层。下列结论来自当前代码；性能影响没有新测量时明确
列为待验证项，不把静态场景耗时当成动画或整帧性能证据。

本批已移除旧 `Renderer Preview` 菜单及窗口，保留正式视口、独立 RHI 宿主、共享
`ScenePreviewController` 和原生对照所需的诊断组装。详情见
[共享控制器](RHI_PREVIEW_CONTROLLER.md)。

## 1. 优先：明确单帧的渲染所有者，停止常态双重绘制

证据：[frame_scene.cpp](../src/render/frame_scene.cpp) 的 `RenderModelsFixed/Effect`
仍先执行 DX9 场景，随后才调用 `ViewportRenderer().Render()`。
[editor_viewport.cpp](../src/render/editor_viewport.cpp) 在 MME、背景、录制、全屏、
线框、立体及物理调试等情况下回退；[editor_scene.cpp](../src/render/editor_scene.cpp)
还会因附件阴影或特殊模型显示状态回退。

因此选中 DX11／DX12／Vulkan 后，编辑器仍依赖 DX9 初始化和原有绘制准备，
也可能执行两套 GPU 场景。这是宿主集成的缺口，并不表示新后端还没有呈现能力。

建议先拆开 CPU 更新／覆盖层生产与 DX9 执行，在帧边界完成可用性检查并确定
实际渲染所有者。支持的 RHI 帧只执行共享流程，需要兼容回退的帧执行 DX9；
失败恢复和回退必须使用本帧状态。逐项迁移字体、背景、录制等仍依赖旧设备的准备过程。

验收：隐藏宿主记录原生绘制次数，正常 RHI 帧的 DX9 场景绘制次数为零；
覆盖播放、拖动、模型卸载、兼容模式进入／退出和设备失败恢复，比较完整输出。

## 2. 优先：动画几何的上传与物理分配复用

证据：[rhi_scene_renderer.cpp](../src/render/rhi_scene_renderer.cpp) 的 `PrepareImpl`
将新的资源版本交给 `Upload`，后者为几何调用 `CreateBuffer`；旧版本进入退休列表。
当前逻辑版本的正确性已经建立，但物理 GPU 缓冲没有形成面向连续变形的复用池。

[rhi_vulkan.cpp](../src/render/rhi_vulkan.cpp) 的 `CreateBuffer` 使用独立的可映射
HOST_VISIBLE／HOST_COHERENT 分配；`Upload`、`Destroy` 调用 `Idle()`，纹理
`BeginTransfer/FinishTransfer` 也等待完成。尤其 `SceneRenderer::Collect()` 已确认
某个旧版本完成后，`Destroy` 仍会等待设备最新提交，可能把后续无关工作一起排空。
三个在途帧和 timeline semaphore 已存在，问题集中在更新／回收路径的等待范围。

DX12 已有帧上下文、待上传任务和 fence，不应将其描述成每帧统一等待 GPU；
但每个新几何版本创建 DEFAULT 缓冲及 staging 分配的成本同样需要动画测量。

建议将逻辑 ID/version 与可复用物理存储分开，以 completion token 为复用前提，
为变形顶点、覆盖层和常量建立环形上传或缓冲池，并按各资源最后使用的提交回收。
纹理流式加载之后再评估专用 transfer 队列；单 graphics 队列仍可正确实现首轮优化。

验收：程序生成的逐帧变形模型与本地真实动画模型，预热后统计创建／分配次数、
上传字节、退休积压、等待原因和 CPU／GPU 分段耗时。静态零重复上传、严格重复输出、
过期版本拒绝及跨设备重建门禁继续保留；性能门禁优先使用计数和有界积压。

## 3. 通用效果：着色器与资源绑定契约尚缺

证据：[rhi.hpp](../include/mikudancestudio/rhi.hpp) 的 `Device` 暴露
`SetFixedPipeline/SetModelPipeline`，没有通用 ShaderModule、PipelineLayout 或
绑定布局接口。[rhi_model_pipeline.hpp](../include/mikudancestudio/rhi_model_pipeline.hpp)
将材质、内置阴影和后处理模式集中在 `ModelPipeline`；
[rhi_model_replay.hpp](../include/mikudancestudio/rhi_model_replay.hpp)
及 `SceneRenderer` 使用三个材质纹理槽和独立阴影绑定。

当前共享 HLSL／SPIR-V 已存在，但“后端有着色器”不等于能执行任意 `.fx`。
需要效果 Pass、参数类型／语义、常量布局、纹理／采样器绑定、反射及编译诊断契约，
再将 MME 的技术选择、SAS、脚本和目标切换映射进去。

建议以可公开的最小 MME 效果驱动这层接口，依次覆盖材质着色、单 Pass 后处理、
两级目标和动态屏幕输入。着色器迁移同时验证 D3D 与 SPIR-V 的矩阵布局、
常量对齐、采样和裁剪约定；不能仅靠文件格式转换代替 MME 行为迁移。

## 4. 能力查询不够细，格式与附件范围仍小

证据：[rhi.hpp](../include/mikudancestudio/rhi.hpp) 的基础 `Capabilities` 只有
四个布尔字段，`ModelCapabilities` 针对内置模型；
[rhi_backend.cpp](../src/render/rhi_backend.cpp) 的 `BackendAvailable` 只报告编译可用。
缺少格式 × 用途 × 采样数 × 回读的统一查询，部分不支持情况只能在创建或执行时获知。
例如 DX12 和 Vulkan 的 `ReadRenderTexture` 显式拒绝 D24S8。

[rhi_render_pass.hpp](../include/mikudancestudio/rhi_render_pass.hpp) 每 Pass 只有一个
颜色附件；`ValidRenderTextureDesc` 限定单 mip／单层／单采样、最大 4096，
格式只有 RGBA8、R32Float 和 D24S8，并拒绝深度采样。
[rhi_frame_passes.cpp](../src/render/rhi_frame_passes.cpp) 的普通目标纹理及后处理
路径进一步要求 RGBA8。当前 R32 阴影颜色图不等于通用深度纹理采样。

建议先补带明确原因的格式／用途和限制查询，让效果可用性在分配前确定；
随后按首批 MME 的实际需求补 MRT、RGBA16F 与深度采样。MSAA 必须同时定义
resolve、初始化和读写规则。浮点颜色目标还需要与当前 UNORM 范围不同的清屏契约。

最终输出目前是 RGBA8，交换链描述也没有 HDR 色域／传递函数。
材质 sRGB 解码已有实现，但后处理的线性工作空间、最终编码与 Alpha 约定仍需成为
可查询、可测试的完整输出契约；现有严格复制路径继续保持字节一致。

## 5. Pass 依赖已经正确表达，尚无通用图编译与子资源视图

证据：[rhi_frame_passes.hpp](../include/mikudancestudio/rhi_frame_passes.hpp) 的
`RenderFrame` 是顺序执行的绘制 Pass 列表；
[rhi_render_pass.cpp](../src/render/rhi_render_pass.cpp) 已拒绝反馈读写、
未初始化读取、无效 Load／Discard 和过期句柄。原生后端已有单队列 barrier，
不应将这些已有能力列为缺失。

当前依赖粒度是整张纹理，没有 mip／layer／aspect 视图，也没有通用 Copy／Resolve／
Compute Pass、临时目标别名或根据生命周期安排物理资源的编译阶段。
目标数量／Pass 数量还被限制为 64。MME 的 MRT、深度和更多中间目标接入时，
应先完善子资源与用途，再按测量需求增加临时资源复用；异步 Compute 可以后置。

## 6. 恢复、兼容性与观测仍需扩大覆盖

- Vulkan 要求 1.3、dynamic rendering、Synchronization2、timeline、
  shader demote、特定 float controls 和各向异性，即使默认使用可移植三线性，
  创建仍检查原生各向异性能力。首轮应改进能力／拒绝原因查询，再决定是否分层降级。
- Vulkan Win32 呈现已接入，`WindowSystem` 中的 Cocoa／Xcb／Wayland 枚举
  不代表已实现这些平台的 WSI；工厂在非 Windows 上拒绝呈现。
- `SceneRenderer` 明确限定单所有者／渲染线程。当前无需提前扩成多队列接口，
  后续可先将资源准备与 GPU 上传排程分开。
- 编辑器借用的实时材质会读取当前材质值；独立 CPU 导入／变形入口则仍拒绝
  QDEF 和活动材质 morph。需分别测试两个入口，避免把某个入口的限制泛化到全部 PMX。
  UV1、附件自阴影及 MME 等行为的迁移范围仍需要逐项完成。
- 已有设备身份、epoch、句柄 generation、提交令牌和设备替换测试。
  硬件设备丢失／驱动重启的覆盖仍有限；已有 DX12 WARP 注入测试只能证明其限定场景。
- 当前资源／上传计数和 CPU 基准可用，仍缺统一 GPU timestamp、Pass 标记、
  分配／等待统计、p50／p95／p99 与显存预算。固定 draw／descriptor 容量也应
  纳入查询和超限负例。主要图像证据集中在本机 GPU，需增加其他厂商和 CI 覆盖。

## 建议的接续批次

| 批次 | 交付 | 核心门禁 |
| --- | --- | --- |
| A | 帧渲染所有者、CPU 更新与 DX9 绘制拆分 | RHI 帧零 DX9 场景绘制；回退／恢复输出正确 |
| B | 动态几何物理缓冲复用、Vulkan 按资源完成回收、耗时与分配观测 | 连续动画分配趋稳、无常态全队列等待、无过早复用 |
| C | 通用效果／着色器绑定、能力查询、首批 MME 最小效果 | 三后端效果对照；非法绑定和不支持能力在执行前拒绝 |
| D | 按效果需求扩展 MRT、浮点颜色、深度采样和子资源 | 各格式读写／初始化／恢复契约；现有严格复制不退步 |

先完成 A／B 能直接改善日常编辑器的结构和播放开销；C／D 再把已有多后端执行能力
扩展到 MME。每批都继续使用离屏设备或从不显示的隐藏窗口，真实模型只读且不入库。
