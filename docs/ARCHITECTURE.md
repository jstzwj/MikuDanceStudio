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

`MMEffect` 是静态库，宿主在设备、绘制、场景和文件边界调用桥接接口。历史 `mmhack` 目录名不意味着需要恢复代理 DLL、IAT patch 或虚表注入。

## 有意偏差与历史注释（旧注释中的 §8）

有效输入下保持原版行为；对于越界输入、未初始化读取和资源泄漏，不为字面复刻而重新引入内存破坏。品牌标题和版本使用本项目身份。

源码中的原版 VA、FLIRT 命名、测试模型和旧报告路径是定位线索，不证明原作者命名或当前等价性。复用历史结论前，应重新核对相应架构、二进制、读写者和实际输入。
