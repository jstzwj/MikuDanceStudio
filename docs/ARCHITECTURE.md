# 架构与兼容边界

MikuDanceStudio 使用 C++、Win32 和 DirectX 9，内置 Bullet 2.75 与 MME。行为目标及参考版本见 [x64 兼容性基准](X64_RECONSTRUCTION.md)，当前验证范围见 [移植状态](PORTING_STATUS.md)。

跨平台内核拆分的首个实际边界和后续验收条件见 [M1 可移植内核](M1_PORTABLE_CORE.md)。

## next 的可移植边界

`mikudancestudio_formats` 持有独立的磁盘文档、场景会话和轨道求值，不依赖 Win32、DirectX 或原版对象布局。`mikudancestudio_physics` 持有 Bullet 世界和 CPU 调度；Windows 适配层通过带类型的模型槽位调用现有姿态、IK、Morph 和物理算法。渲染输入、资源生命周期及 DX9／DX11／DX12 预览的边界见 [RHI 架构](RHI_ARCHITECTURE.md)。

原版兼容修正通过这些公共边界传播：

- Windows 适配层统一使用 `ModelRecord*` 和具名字段；`RuntimeCpuModel` 拥有实际的模型对象，CPU 几何缓存以模型类型指针为键。磁盘文档继续按字段转换，不将运行时对象当作字节缓冲区。
- PMD 和 PMX 的 CPU 索引均为连续的 32 位整数；GPU 索引及磁盘编码按各自格式转换。
- 贝塞尔曲线和帧定位四元数插值由编辑器与可移植求值器共用数值内核，保留原版各架构的插值舍入；普通骨骼的保存物理标志由原生运行时维护。PMM v1/v2 共用播放选项应用逻辑，先更新运行时相机参考模式，再通知控件。
- 编辑器与独立 CPU 预览共用蒙皮和描边数值内核；描边距离在固定视点坐标下计算。背景清屏在原生绘制、场景生产器和诊断预览中均保留零 alpha。
- x86 数学通过 Bullet 配方中的独立 C++ 单元保留舍入边界；应用、独立 CPU 测试和 Bullet 使用相同的 C++17 分配约定。

## 模块

| 目录 | 职责 |
|---|---|
| `src/app`、`src/window` | 应用状态、播放、窗口消息与编辑操作 |
| `src/model`、`src/io` | 模型、关键帧、蒙皮和文件读写 |
| `src/physics` | 刚体创建、姿态同步与物理步进 |
| `src/render` | DirectX 9 设备、模型绘制、阴影与覆盖层 |
| `src/media`、`src/mmdxshow` | 音频、视频录制与独立的 MMDxShow.dll |
| `third_party/mmeffect` | 静态链接的效果引擎 |
| `recipes/bullet275` | 未修改的原始 Bullet 2.75 源码、校验清单，以及独立的 CMake / Conan 构建配置 |

帧复制粘贴由 `src/window/frame_clipboard.cpp` 处理；相机、灯光、阴影和重力轨道注册位于 `src/model/global_key_registration.cpp`。

## 所有权与边界

应用仍包含具有兼容布局约束的运行时记录。新业务代码优先使用具名成员和明确的拥有关系；磁盘记录、运行时对象和外部 COM/插件 ABI 分开维护。`RawPad` 表示保留布局区域，不能根据位置猜测语义后直接用于业务逻辑。

`MMDApp` 在 x86/x64 上使用同一份 `AppRuntimeState`（[定义](../include/mikudancestudio/app_runtime_state.hpp)）。对话框句柄、完整宽度的回调、临时缓冲区、剪贴板、选区、矩阵和设备灯光均通过具名接口访问。`MMDAppState` 中对应的 `reserved*` 成员只保留旧布局的尺寸和偏移；`layout_pins.hpp` 防止意外改变兼容记录，不代表这些保留槽位仍是业务状态，也不构成全程序等价证明。PMM 继续按字段读写，不能整体序列化这两个对象。

运行时状态的维护规则：

- 构造时初始化指针和工作区；`InitDefaults`、`ResetAppState` 只更新各自负责的状态，重置面板时直接清理实际使用的 204 个标记。选区范围用起止列、起止行表示，右侧和下侧不包含在范围内，不再用缓冲区地址表达。
- 帧复制与物理编辑保留共享窗口句柄；地面阴影颜色继续通过原有暂存区访问。窗口创建、输入回调、关闭和模式切换都使用同一组接口。
- 剪贴板的数量与指针由一个对象维护。重置工程和退出通过 `ReleaseClipboardBuffers` 先释放显示记录的嵌套数组，再释放各类缓冲区，最后清零。保留各缓冲区现有分配器，并配对使用 `free`、`operator delete` 或 `delete[]`。
- 八类时间轴选区各自持有数量和原生指针，收集新选区、松开鼠标和退出使用共同的释放路径，不依赖全局旁表。原版七类选区的释放次序保留，并补上重力数组释放。
- `SceneLight` 是完整的 `D3DLIGHT9`；`LightColor()` 直接返回它的 `Ambient.rgb`，供渲染与 PMM 共用。`ResetSceneLight` 供窗口初始化、工程重置和灯光重置按钮调用；矩阵及 PMM 灯光方向各有独立存储。

灯光重置和选区范围分别维护，避免状态存储相互覆盖。原版用列、行及各自加一保存单元格范围，移植中曾将命中类型乘记录长度及截断的关键帧地址写入此处，现已修正。初始化、实例隔离、完整灯光默认值和释放见 [运行时状态回归](../tests/app_runtime_state_test.cpp)；Shift 与选区替换、各模型轨道清理见 [选区回归](../tests/timeline_selection_test.cpp)。

`MMEffect` 是静态库，宿主在设备、绘制、场景和文件边界调用桥接接口。历史 `mmhack` 目录名不意味着需要恢复代理 DLL、IAT patch 或虚表注入。

## 有意偏差与历史注释（旧注释中的 §8）

有效输入下保持原版行为；对于越界输入、未初始化读取和资源泄漏，不为字面复刻而重新引入内存破坏。品牌标题和版本使用本项目身份。

历史注释、测试模型和旧报告不能替代当前验证。复用结论前，应重新核对相应架构、参考版本、相关读写路径和实际输入。
