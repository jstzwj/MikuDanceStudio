# 架构与证据边界

本项目将 MMD 的 Win32/D3D9 宿主和 MME 效果引擎编译为同一可执行程序。`MMEffect` 是静态库；宿主在设备、绘制、场景和文件边界主动调用桥接接口，不加载代理 `d3d9.dll`，也不恢复 IAT 或虚表注入。文件格式结构与运行时对象布局分别维护。

## 子系统入口

应用与窗口在 `src/app`、`src/window`；模型与格式在 `src/model`、`src/io`；数学、物理、渲染分别在 `src/math`、`src/physics`、`src/render`；内置效果在 `third_party/mmeffect`。每个原版 VA 注释只用于定位，不证明源码原名或等价性。验证规则见 [x64 重建基准](X64_RECONSTRUCTION.md)。

## §8 有意偏差与待验证历史注释

遇到原版未定义行为、越界输入和资源泄漏，优先保持有效输入的行为，并记录安全偏差；不要故意复制内存破坏。具体当前项目见 [对齐记录](ALIGNMENT_WORKLOG.md)。旧源码注释提及的 Luka 模型、FLIRT 命名、Bullet 析构、PMM 中间态与播放时序是历史溯源线索，原始 `translated/` 与 `reports/` 不在当前工作树中，复用结论前必须重新从对应 x64 二进制和实际输入验证。
