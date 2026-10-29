# RHI 共享控制器

日常编辑器通过 **Renderer → Direct3D 9 / Direct3D 11 / Direct3D 12 / Vulkan**
选择主视口后端，具体范围见 [编辑器视口](RHI_EDITOR_VIEWPORT.md)。
2026-10-29 已删除旧 `Renderer Preview` 菜单和额外的独立预览窗口。

## 保留的职责

- `EditorSceneProducer` 借用编辑器当前 CPU 场景并生产 `RenderFrame`。
- `CreateBackendDevice` 选择已编译的后端并创建设备及交换链。
- `ScenePreviewController` 管理提交、缩放、暂停、设备替换、卸载与关闭。
- `EditorViewport` 管理 Win32 子窗口、用户设置和兼容回退。
- `SceneRenderer` 管理资源版本、GPU 上传、完成令牌和延迟释放。

共享控制器接受单目标命令流及显式多 Pass 帧，继续用于正式视口和独立 RHI 宿主。
`mds_rhi_preview` 及无 DX9 的可选启动分流仍然保留，见
[独立启动](RHI_DX9_INDEPENDENT_STARTUP.md)；它们与已删除的菜单窗口是不同入口。
为兼容现有构建配置，`MIKUDANCESTUDIO_RHI_PREVIEW` 选项名称保留，
描述已更新为正式视口和独立宿主，默认主视口仍为 DX9。

## 清理范围与诊断

已移除旧菜单命令、`MDS_CPU_RHI_PREVIEW` 窗口宿主、F5 窗口重建、
`MDS_RHI_CPU_SCENE_PREVIEW` 和 `MDS_RHI_CPU_PREVIEW_UI_TEST` 环境入口，
以及 `scene_preview_gui_test` 和诊断脚本的 `--resident-preview`／`--preview-ui`。
旧的 `cpu_resident.*.csv` 窗口验收日志不再产生。

`scene_cpu_frame.cpp` 只保留 CPU 命令流组装及显式诊断检查点所需的顺序检查，
不再创建设备或窗口。完整场景的原生字段、逐绘制图像及生命周期比较仍由
`model_cpu_preview.cpp` 中的显式诊断执行；生产构建不会因旧预览环境变量启动绘制。
旧证据和适用范围保留在 [历史记录](RHI_PREVIEW_CONTROLLER_HISTORY.md)。

## 隐藏窗口验收

`rhi_preview_dx11/dx12_warp/hardware` 现在只使用从不显示的非激活窗口，
覆盖三轮打开／关闭、未绘制即卸载、连续缩放、零尺寸暂停和恢复、设备替换、
过期准备帧拒绝、纹理复用、场景卸载和失败恢复，不调用最小化或恢复窗口。
这些用例不再申请桌面资源锁，也不宣称执行了真实窗口最小化。

`rhi_hidden_presentation_*` 覆盖三个后端的 GPU 最终画面复制、呈现、缩放和替换。
`editor_scene_*` 覆盖生成 PMD／PMX 的实际导入、重复输出、附件及跨 API 重建。
`editor_viewport_*` 覆盖实际子视口、设置、覆盖层与兼容回退；其中字体对照需要
隐藏 DX9 设备，应独立报告该设备的创建失败。

```powershell
ctest --test-dir build-x64/build -C Release --output-on-failure -R '^(rhi_preview_.+|rhi_hidden_presentation_.+|editor_scene_(dx11|dx12|vulkan))$'
```

2026-10-29 移除验收：x64 全量、x86 主应用及相关目标、无诊断三后端主应用、
无诊断且关闭 RHI 的主应用均构建通过；四种主应用二进制中均未发现旧菜单、窗口类
或环境入口字符串。x64 离屏／隐藏窗口回归 36/36 通过，涵盖控制器、呈现、生成模型、
后处理、屏幕捕获及 CPU 绑定。原生诊断证据解析的四项单元测试通过。

扩展用例 x64 为 4/10，包含本地 Sour Miku PMX 在三个后端的导入，以及 DX11
实际视口通过；x86 为 7/10，控制器四项和生成模型三个后端通过。余下共九项均在
DX9 `CreateDevice` 失败处终止（字体对照、自阴影参考及原生 X 对照），尚未执行对应
图像比较。本轮未运行可见 GUI 自动化，也未改变比较阈值或失败／跳过规则。
