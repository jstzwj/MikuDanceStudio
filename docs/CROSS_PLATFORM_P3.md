# P3：三平台渲染与呈现

**P3 已完成。** 同一份生产 RHI、共享着色器和 P2 CPU 场景快照已在 Windows、macOS、Linux 实际执行。三平台的离屏门禁及各自原生窗口呈现通过；正常呈现帧没有 GPU→CPU→GPU 图像搬运。

本阶段交付渲染宿主和明确的基础场景子集。完整跨平台编辑器 C1 继续由 P4 验收，去 D3DX 的原始 MME 效果前端由 P5 验收。Windows 编辑器保留现有兼容宿主，本阶段没有把 Qt 前端或任意 Ray 效果标记为完成。

## 生产代码与边界

| 模块 | 已实现契约 |
|---|---|
| Vulkan 加载器 | 显式 UTF-8 库路径；Windows 系统加载器、Linux `libvulkan.so.1`、macOS 应用相对路径和 MoltenVK 候选；协商 portability enumeration／subset 扩展 |
| 原生 Surface | Windows HWND、XCB window＋connection、Wayland surface＋display、Cocoa CAMetalLayer；平台类型限制在适配器，宿主提供物理像素尺寸、健康状态和共享生命周期租约 |
| SPIR-V 编译 | 三平台通过无 shell 的 DXC 子进程执行相同编译参数；有超时、输入／输出上限、临时目录清理、错误诊断和有界缓存；保留预编译 SPIR-V 路径及绑定反射 |
| 深度附件 | 新增 attachment-only `DepthStencil`，协商 D24_UNORM_S8／D32_FLOAT_S8；不承诺可转移的字节布局，禁止采样和回读。精确 `Depth24Stencil8` 仍单独查询，缺失时明确拒绝 |
| 场景生产者 | `SceneFrameProducer` 消费已发布的 P2 快照，准备基本 PMD／PMX 材质、纹理、模型自阴影、PMM 相机灯光、地面和静态 `.x` 附件；不推进动画或物理 |
| 缓存与重建 | 稳定资源 ID、递增内容版本、共享几何与纹理、缩放保留可复用资源；资源替换要求重载，设备替换拒绝旧准备结果；失败不发布半帧 |
| 最终颜色 | 使用共享后处理到 `finalColor`，由交换链在 GPU 内复制／呈现；离屏 CLI 仅在最终验收时显式回读 |

Mac 的 D24S8 缺失曾导致设备创建成功后资源初始化失败。现在可移植深度附件选择实际支持的 D32F＋S8，同时测试精确 D24S8 的查询与拒绝行为，没有将缺失格式伪报为支持。[Vulkan 格式规范](https://docs.vulkan.org/spec/latest/chapters/formats.html)要求按设备查询可选格式能力。

默认材质采样继续使用可移植三线性；原生各向异性差异独立跟踪。后处理整矩形点采样采用整数像素中心映射 `floor((2*x+1)*sourceWidth/(2*destinationWidth))`，避免插值 UV 在恰好位于 texel 边界时产生跨 GPU 分歧；非整矩形与线性采样保留其原有路径。原样复制和 Alpha 继续逐字节比较，没有放宽门槛。

运行时编译需要展开后的 HLSL；尚未展开的 `#include` 返回诊断。缓存身份包含编译器实例、阶段、入口、固定 ABI 参数和源码；不依赖 Windows COM 或 D3DCompiler。Windows DX11／DX12 自身的原有编译路径继续保留。

## 运行入口

可独立构建生产模块和验收宿主：

```sh
python tools/fetch_dxc.py --output out/dxc
cmake -S cmake/portable-render -B out/p3 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMDS_VULKAN_INCLUDE=/path/to/Vulkan-Headers/include \
  -DMDS_DXC=/absolute/path/to/dxc
cmake --build out/p3 --parallel 3
ctest --test-dir out/p3 -L p3 --output-on-failure
```

Windows 可使用 VS 2022 x64 生成器及本地 Vulkan SDK。Linux CI 显式选择软件 Vulkan 设备，构建参数为 `-DMDS_P3_DRIVER=software`；X11／Wayland 测试另外在 Xvfb／headless Weston 中运行。macOS 原生 arm64 DXC 的构建和 MoltenVK 路径见工作流，不运行上述仅提供 Windows／Linux 二进制的下载器。

```sh
mds_render_cli --input model.pmx --assets /path/to/res/assets \
  --output out/render --backend vulkan --frames 90 --shadow 1
```

CLI 支持 PMD／PMX／PMM、VMD、Seek、播放、奇数尺寸缩放和设备替换，输出原始 RGBA、浮点阴影和 JSON 计数／耗时。`--vulkan-library` 可指定加载库，`--debug 1` 可请求验证层，`--replace-at 3 --resize 1` 可复现生命周期门禁。Windows 另可选择 `dx11`／`dx12`。

播放通过 `SceneRuntime::Advance` 推进绝对时钟，再消费该帧快照；测试检查 6 帧从 Seek 3 到最终帧 8，恰好更新 5 次。帧描述生成前后检查 CPU 更新及发布计数，避免重复更新或只重复画静态帧。

## 退出条件与证据

完整生产代码验收基线为 `0a7cce1`。三平台记录见 [版本化证据](evidence/cross-platform-p3.json)，远程运行见 [P3 CI](https://github.com/jstzwj/MikuDanceStudio/actions/runs/41234951924)。后续 `next` 推送继续运行同一工作流。

| 环境 | 实际运行结果 |
|---|---|
| Windows x64，NVIDIA TITAN Xp | 11／11 P3 CTest；Win32 从不显示的窗口呈现；额外验证层呈现、资源同步及三后端离屏门禁通过 |
| Ubuntu 22.04 x64，Mesa 软件 Vulkan | 9／9 P3 CTest；Xvfb X11 和 headless Weston Wayland 呈现各通过 |
| macOS 15 arm64，MoltenVK 1.4.2，Apple Paravirtual GPU | 11／11 P3 CTest，包含 Cocoa／CAMetalLayer 呈现；原生 arm64 DXC 源码构建及运行时编译通过 |

测试覆盖通用 VS／PS、矩阵与常量布局、透明采样、纹理 mip、自阴影生成／采样、多 Pass、MRT、RGBA16F 负值与大于 1 的值、浮点回读、色调映射、严格颜色复制，以及重复提交、缩放、暂停呈现、卸载、设备替换和原生对象生命周期。

RGBA16F 的可精确表示写入值逐值比较，HDR 合成保留一个 binary16 ULP 的原有门槛，色调映射和 mip 的 UNORM 运算保留每通道至多 1 的门槛；颜色／HDR 原样复制均逐字节比较。窗口宿主除程序生成材质帧外，还直接运行 P2 动画快照→场景生产者→共享 RHI→原生呈现，逐帧核对发布帧号和更新计数。

公开生成的 8 个 PMD／PMX／PMM 场景在每个平台运行 6 帧，包含 CP932／UTF-16 资源与 Unicode 路径、自阴影两种模式、变形模型和绑定场景。每例检查最终帧号、更新次数、设备替换、输出尺寸、非空画面、有限阴影值及严格后处理复制。

呈现测试在每次正常 `Draw` 前后断言 Vulkan 回读次数不变、呈现次数增加；随后单独执行诊断回读，与相同后端的离屏 `finalColor` 逐字节比较。窗口缩放、暂停恢复、设备替换和卸载均经过该流程。软件 Linux 与虚拟 Mac 验收证明当前运行链路，实体 GPU 的性能与发行资格继续归 P7。

依赖固定为 Vulkan-Headers v1.3.296、MoltenVK 1.4.2，以及官方 DXC v1.8.2505.1 的校验和固定二进制／源码修订 `b106a961d09221b3c5bdb37be45b679257da08b8`。本地 Windows 使用 Vulkan SDK 1.4.341.1。编译选项和工具定位可在 `.github/workflows/portable-render.yml`、`tools/fetch_dxc.py` 与 `cmake/VulkanShaders.cmake` 复现。

## 真实模型与性能基线

本地 Sour Miku 模型 SHA-256 为 `4542728612d1055186dc500bfc897cf458ce5530a87bb2c84de1dfeffeb6e5fa`。模型、纹理和私有输出只保存在忽略目录，不上传仓库或 CI。

i7-10700／TITAN Xp，640×480，物理关闭、自阴影开启：静态与根骨骼平移动画各运行 90 帧，最终帧 89。DX11／DX12／Vulkan 的最终 RGBA 均逐字节一致。动画最终画面与静态不同，三后端都只创建 5 个缓存缓冲、复用 87 次、没有缓冲等待；静态各创建 3 个缓冲。Vulkan 开启验证层，0 错误，90 帧仅最终诊断回读 3 次。

| 后端 | 静态温帧 CPU 墙钟均值 | 动画温帧 CPU 墙钟均值 |
|---|---:|---:|
| DX11 | 67.06 ms | 60.33 ms |
| DX12 | 58.40 ms | 67.24 ms |
| Vulkan，验证层开启 | 66.19 ms | 65.87 ms |

这些是新离屏宿主的 CPU 墙钟基线，包含 CPU 场景更新、帧描述生成、资源准备和异步提交，排除一次性资源加载与最后显式回读；不是 GPU 时间戳，也不是窗口编辑器 FPS。90 帧累计 CPU 求值约 1.44–1.63 s，帧描述生成约 3.73–4.32 s，资源准备约 0.08–0.23 s，提交／收集约 0.06–0.35 s。共享编辑器接入前应优先减少生产者的逐帧复制、扫描与封装，不能从当前异步提交耗时推导 GPU 性能。

## 留给后续阶段的工作

基础生产者当前不输出模型描边、投影到地面的模型阴影、附件自阴影或 `screen.bmp` 历史；后两类资源／场景需求及投影阴影返回明确诊断。纹理／球面／Toon 的非默认材质 Morph 色调、背景视频／图片和 PMM v1 父级相机也返回诊断。追加 UV／球面模式 3 等超出共享材质布局的情况不承诺支持。

P4 接入共享编辑命令、Qt 窗口和这些 C1 所需渲染能力，并优化 CPU 帧生产；P5 提取原始 `.fx` 的无 D3DX 前端；P6 接入完整媒体服务；P7 扩大实体驱动、长时间运行和安装包资格测试。现有 Windows 兼容路径及其回归门槛保留。
