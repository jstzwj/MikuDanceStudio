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

初始交付基线为 `0a7cce1`；最终补验基线为 `543d296`，补齐交换链图像、Surface 丢失恢复与性能分项。三平台记录见 [版本化证据](evidence/cross-platform-p3.json)，最终成功运行见 [P3 CI](https://github.com/jstzwj/MikuDanceStudio/actions/runs/41270434909)。同一提交的[四列基础 CI](https://github.com/jstzwj/MikuDanceStudio/actions/runs/41270434952)也全部通过。后续 `next` 推送继续运行同一工作流。

| 环境 | 实际运行结果 |
|---|---|
| Windows x64，NVIDIA TITAN Xp | 12／12 P3 CTest；Win32 从不显示的窗口呈现和性能门禁；额外 12 项资源／呈现回归及验证层检查通过 |
| Ubuntu 22.04 x64，Mesa 软件 Vulkan | 9／9 P3 CTest；Xvfb X11 和 headless Weston Wayland 的场景呈现、恢复及性能门禁各通过 |
| macOS 15 arm64，MoltenVK 1.4.2，Apple Paravirtual GPU | 12／12 P3 CTest，包含 Cocoa／CAMetalLayer 呈现与 CPU 性能门禁；GPU 计时限制单独记录；原生 arm64 DXC 及运行时编译通过 |

测试覆盖通用 VS／PS、矩阵与常量布局、透明采样、纹理 mip、自阴影生成／采样、多 Pass、MRT、RGBA16F 负值与大于 1 的值、浮点回读、色调映射、严格颜色复制，以及重复提交、缩放、暂停呈现、卸载、设备替换和原生对象生命周期。

Windows x64／x86 的 Release 完整应用和渲染 CLI 也已在 Vulkan 开启时重新构建通过；没有启动可见编辑器。x86 CLI 另完成生成 PMD 的 CPU 场景→Vulkan→严格后处理复制及三帧 GPU 时间戳检查。

RGBA16F 的可精确表示写入值逐值比较，HDR 合成保留一个 binary16 ULP 的原有门槛，色调映射和 mip 的 UNORM 运算保留每通道至多 1 的门槛；颜色／HDR 原样复制均逐字节比较。窗口宿主除程序生成材质帧外，还直接运行 P2 动画快照→场景生产者→共享 RHI→原生呈现，逐帧核对发布帧号和更新计数。

公开生成的 8 个 PMD／PMX／PMM 场景在每个平台运行 6 帧，包含 CP932／UTF-16 资源与 Unicode 路径、自阴影两种模式、变形模型和绑定场景。每例检查最终帧号、更新次数、设备替换、输出尺寸、非空画面、有限阴影值及严格后处理复制。

呈现测试在每次正常 `Draw` 前后断言 Vulkan 回读次数不变、呈现次数增加；随后另提交诊断帧，在 `Present` 前直接回读已获取的交换链图像，与相同后端的离屏 `finalColor` 逐字节比较。该比较覆盖最终 GPU blit、尺寸、方向、Alpha 和 BGRA／RGBA 转换，不以呈现前的离屏目标代替交换链证据。窗口缩放、暂停恢复、设备替换和卸载均经过该流程。软件 Linux 与虚拟 Mac 验收证明当前运行链路，实体 GPU 的性能与发行资格继续归 P7。

Surface 恢复覆盖三种情况：在 acquire、present 边界分别单次注入 `VK_ERROR_SURFACE_LOST_KHR`，以及不注入错误、直接销毁原生窗口并由宿主报告 Removed。错误注入是确定性的驱动返回值模拟，不声称驱动自然发生过丢失。三例均创建真实的新窗口及 Vulkan Surface，停用旧路径，拒绝继续提交，使用同一个 CPU 快照重建并严格比较交换链输出；恢复过程不推进动画。显示连接／Metal layer 的租约保留到旧 Vulkan Surface 销毁后，Windows 窗口从不显示。生产代码明确将 Surface 丢失标为 Removed，要求替换，不再把它当成 Unknown 后继续尝试旧 Surface。

依赖固定为 Vulkan-Headers v1.3.296、MoltenVK 1.4.2，以及官方 DXC v1.8.2505.1 的校验和固定二进制／源码修订 `b106a961d09221b3c5bdb37be45b679257da08b8`。本地 Windows 使用 Vulkan SDK 1.4.341.1。编译选项和工具定位可在 `.github/workflows/portable-render.yml`、`tools/fetch_dxc.py` 与 `cmake/VulkanShaders.cmake` 复现。

收尾复核修正了 P1／P2 二进制依赖审计的既有偶发误报：`ldd` 的 ASLR 地址可能恰含 `d3d`，现在只检查请求库名与解析后的库文件名，同时明确拒绝未解析依赖。禁用库清单不变，新增 Linux 地址、macOS 元数据、真实 GPU／UI 库及缺失库的回归；四列基础 CI 都执行该检查。

## 真实模型与性能基线

本地 Sour Miku 模型 SHA-256 为 `4542728612d1055186dc500bfc897cf458ce5530a87bb2c84de1dfeffeb6e5fa`。模型、纹理和私有输出只保存在忽略目录，不上传仓库或 CI。

初始基线 `0a7cce1`：i7-10700／TITAN Xp，640×480，物理关闭、自阴影开启，静态与根骨骼平移动画各运行 90 帧，最终帧 89。DX11／DX12／Vulkan 的最终 RGBA 均逐字节一致。动画最终画面与静态不同，三后端都只创建 5 个缓存缓冲、复用 87 次、没有缓冲等待；静态各创建 3 个缓冲。Vulkan 开启验证层，0 错误，90 帧仅最终诊断回读 3 次。

| 后端 | 静态温帧 CPU 墙钟均值 | 动画温帧 CPU 墙钟均值 |
|---|---:|---:|
| DX11 | 67.06 ms | 60.33 ms |
| DX12 | 58.40 ms | 67.24 ms |
| Vulkan，验证层开启 | 66.19 ms | 65.87 ms |

这些是新离屏宿主的 CPU 墙钟基线，包含 CPU 场景更新、帧描述生成、资源准备和异步提交，排除一次性资源加载与最后显式回读；不是 GPU 时间戳，也不是窗口编辑器 FPS。90 帧累计 CPU 求值约 1.44–1.63 s，帧描述生成约 3.73–4.32 s，资源准备约 0.08–0.23 s，提交／收集约 0.06–0.35 s。共享编辑器接入前应优先减少生产者的逐帧复制、扫描与封装，不能从当前异步提交耗时推导 GPU 性能。

## 补验性能分项与预算

`BackendOptions::profiling`／CLI `--profile 1` 为 Vulkan 提供可选采集，默认关闭。仅在选中队列支持时间戳时创建三个双查询池；起止时间戳覆盖一帧 GPU 命令，不包含离散的资源上传与最终诊断读回。按完成 token 非阻塞收集，处理 `timestampValidBits` 回绕及设备 `timestampPeriod`，不使用查询等待标志。普通呈现不引入图像回读或额外逐帧排空；关闭采集时不创建查询池。DX11／DX12 的 CPU 基线与图像对照保留，CLI 显式拒绝给它们开启尚未实现的这项 Vulkan 采集。

CPU 分别记录 acquire、`vkQueuePresentKHR` 和 timeline semaphore 等待；它们表示 API 调用墙钟耗时，不表示显示器扫描完成或输入到画面的延迟。原生性能门禁的 90 帧测量前后各排空一次以界定样本，末次排空计入等待总量，帧循环内没有诊断回读。另记录资源创建、复用、上传量、待回收资源，以及后端持有的 `VkDeviceMemory` 分配峰值；分配峰值包含主机可见缓冲，不包含交换链、查询池或驱动内部开销，不能称为整机峰值显存。

Apple Paravirtual CI 驱动声明支持时间戳，实际返回 90 个可用样本，但区间全部为零。该环境的 GPU 时间明确记录为 `null`／`virtual_driver_zero_intervals`，`gpu_budget_evaluated=false`，不把零值算作 GPU 预算通过；CPU、呈现、等待、资源及图像门禁照常执行。实体 GPU 和 Linux 软件驱动若返回同样的零区间仍失败，查询支持、完整样本数及严格图像标准没有放宽。P3 的实体 GPU 预算证据来自 Windows；macOS 实体 GPU 性能及该虚拟驱动限制继续归 P7，不能用虚拟设备不可观测的时间戳推断 Metal 性能。

预算在验收前固定于公开测试和本次本地记录，超出直接失败，不根据结果自动放宽。它们是当前基础渲染链的停顿／回归上限，不是编辑器 60 FPS 承诺。

| 场景／环境 | GPU 帧均值上限 | CPU 上限 | timeline 等待均值上限 | 后端分配峰值上限 |
|---|---:|---:|---:|---:|
| 640×480 公开材质＋后处理，实体 GPU | 20 ms | Draw P95 100 ms | 100 ms | 256 MiB |
| 同一公开场景，CI 软件／虚拟 GPU | 1,000 ms | Draw P95 1,500 ms | 1,500 ms | 512 MiB |
| 本地 Sour Miku，静态／动画、物理关闭、自阴影开启 | 20 ms | 温帧 CPU 均值 100 ms | 20 ms | 512 MiB |

Windows i7-10700／TITAN Xp 的公开场景静态／动态 GPU 均值约 0.050 ms，最终复跑的 Draw P95 为 95.23／94.85 ms，90 帧累计 timeline 等待为 5.40／5.46 s，峰值分配约 12.34 MiB。此前同一门禁的 Draw P95 约 31.8 ms，最终记录保留较慢复跑的实际值。隐藏窗口 VSync 路径主要消耗在等待，不应把 Draw 墙钟解释为 GPU 执行耗时。静态温帧没有新上传，动态顶点复用生效，常规帧回读为零。

本次 Sour Miku 六组复跑仍逐字节一致；Vulkan 静态／动画 GPU 均值为 1.247／1.217 ms，温帧 CPU 均值为 79.54／70.95 ms，后端分配峰值约 129 MiB。CPU 求值和帧封装仍是主要成本，优化继续属于 P4，不能把 P3 通过解读为已解决编辑器性能问题。完整 CPU、上传、GPU、显式读回、等待与预算结果保存在版本化证据中；私有模型、动作和输出图像不入库。

复现公开原生性能门禁：`portable_presentation_test benchmark-hardware report.json`；Linux 在隔离的 Xvfb／headless Weston 中分别使用 `benchmark-xcb-software` 和 `benchmark-wayland-software`。Windows／macOS 的 `p3_performance` CTest 和 Linux 两个显式步骤都检查预算并上传 JSON。真实模型使用 `mds_render_cli --input model.pmx --assets res/assets --output out/result --backend vulkan --frames 90 --width 640 --height 480 --shadow 1 --profile 1`，动画追加 `--motion motion.vmd`。CLI 分项中的 GPU 总量覆盖所有帧，等待总量还包含上传及最终显式读回；缩放／设备替换用例按各设备分别计数，不能将最后一个设备的 GPU 计数解释成整个会话。

## 留给后续阶段的工作

基础生产者当前不输出模型描边、投影到地面的模型阴影、附件自阴影或 `screen.bmp` 历史；后两类资源／场景需求及投影阴影返回明确诊断。纹理／球面／Toon 的非默认材质 Morph 色调、背景视频／图片和 PMM v1 父级相机也返回诊断。追加 UV／球面模式 3 等超出共享材质布局的情况不承诺支持。

P4 接入共享编辑命令、Qt 窗口和这些 C1 所需渲染能力，并优化 CPU 帧生产；P5 提取原始 `.fx` 的无 D3DX 前端；P6 接入完整媒体服务；P7 扩大实体驱动、长时间运行和安装包资格测试。现有 Windows 兼容路径及其回归门槛保留。
