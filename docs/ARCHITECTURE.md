# 架构与兼容边界

MikuDanceStudio 使用 C++、Win32 和 DirectX 9，内置 Bullet 2.75 与 MME。行为目标及参考版本见 [x64 重建基准](X64_RECONSTRUCTION.md)，当前验证范围见 [移植状态](PORTING_STATUS.md)。

## 模块

| 目录 | 职责 |
|---|---|
| `src/app`、`src/window` | 应用状态、播放、窗口消息与编辑操作 |
| `src/model`、`src/io` | 模型、关键帧、蒙皮和文件读写 |
| `src/physics` | 刚体创建、姿态同步与物理步进 |
| `src/render` | DirectX 9 设备、模型绘制、阴影与覆盖层 |
| `src/media`、`src/mmdxshow` | 音频、视频录制与独立的 MMDxShow.dll |
| `third_party/mmeffect` | 静态链接的效果引擎 |
| `recipes/bullet275` | 带数值兼容修正的本地 Bullet 源码和 Conan 配方 |

帧复制粘贴由 `src/window/frame_clipboard.cpp` 处理；相机、灯光、阴影和重力轨道注册位于 `src/model/global_key_registration.cpp`。

## 所有权与边界

应用仍包含与原二进制布局对应的运行时记录。新业务代码优先使用具名成员和明确的拥有关系；磁盘记录、运行时对象和外部 COM/插件 ABI 分开维护。`RawPad` 表示未恢复区域，不能根据位置猜测语义后直接用于业务逻辑。

`MMDApp` 在 x86/x64 上使用同一份 `AppRuntimeState`（[定义](../include/mikudancestudio/app_runtime_state.hpp)）。对话框句柄、完整宽度的回调、临时缓冲区、剪贴板、选区、矩阵和设备灯光均通过具名接口访问。`MMDAppState` 中对应的 `reserved*` 成员只保留旧布局的尺寸和偏移；`layout_pins.hpp` 防止意外改变兼容记录，不代表这些保留槽位仍是业务状态，也不构成全程序等价证明。PMM 继续按字段读写，不能整体序列化这两个对象。

运行时状态的维护规则：

- 构造时初始化指针和工作区；`InitDefaults`、`ResetAppState` 只更新各自负责的状态，重置面板时直接清理实际使用的 204 个标记。选区范围用起止列、起止行表示，右侧和下侧不包含在范围内，不再用缓冲区地址表达。
- 帧复制与物理编辑保留共享窗口句柄；地面阴影颜色继续通过原有暂存区访问。窗口创建、输入回调、关闭和模式切换都使用同一组接口。
- 剪贴板的数量与指针由一个对象维护。重置工程和退出通过 `ReleaseClipboardBuffers` 先释放显示记录的嵌套数组，再释放各类缓冲区，最后清零。保留各缓冲区现有分配器，并配对使用 `free`、`operator delete` 或 `delete[]`。
- 八类时间轴选区各自持有数量和原生指针，收集新选区、松开鼠标和退出使用共同的释放路径，不依赖全局旁表。原版七类选区的释放次序保留，并补上重力数组释放。
- `SceneLight` 是完整的 `D3DLIGHT9`；`LightColor()` 直接返回它的 `Ambient.rgb`，供渲染与 PMM 共用。`ResetSceneLight` 供窗口初始化、工程重置和灯光重置按钮调用；矩阵及 PMM 灯光方向各有独立存储。

上述灯光关系按参考 x64 的重置指令 RVA `0x745ED..0x74697` 核对；范围写入按表情分支 `0x3BBF0..0x3BC60`、骨骼分支 `0x3C3B0..0x3C420` 核对。原版用列、行及各自加一保存单元格范围，移植中曾将命中类型乘记录长度及截断的关键帧地址写入此处，现已修正。初始化、实例隔离、完整灯光默认值和释放见 [运行时状态回归](../tests/app_runtime_state_test.cpp)；Shift 与选区替换、各模型轨道清理见 [选区回归](../tests/timeline_selection_test.cpp)。

`MMEffect` 是静态库，宿主在设备、绘制、场景和文件边界调用桥接接口。历史 `mmhack` 目录名不意味着需要恢复代理 DLL、IAT patch 或虚表注入。

## 有意偏差与历史注释（旧注释中的 §8）

有效输入下保持原版行为；对于越界输入、未初始化读取和资源泄漏，不为字面复刻而重新引入内存破坏。品牌标题和版本使用本项目身份。

源码中的原版 VA、FLIRT 命名、测试模型和旧报告路径是定位线索，不证明原作者命名或当前等价性。复用历史结论前，应重新核对相应架构、二进制、读写者和实际输入。
