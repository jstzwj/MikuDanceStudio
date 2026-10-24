# 文档索引

构建步骤见项目 [README](../README.md#building-from-source)，版本、打包与发布流程见 [RELEASES](RELEASES.md)。

## 当前维护文档

| 文档 | 内容 |
|---|---|
| [架构](ARCHITECTURE.md) | 子系统、模块边界与有意保留的兼容偏差 |
| [移植与验证状态](PORTING_STATUS.md) | 最近验证结果和仍未覆盖的行为 |
| [x64 重建基准](X64_RECONSTRUCTION.md) | 参考二进制身份、证据规则与布局维护方式 |
| [源码格式](FORMATTING.md) | clang-format、VS Code formatter 和批量格式检查命令 |
| [源码重构与数值兼容](reconstruction/source_refactoring.md) | 汇编替换、浮点算序、帧定位和编辑器重构的约束与验证 |
| [描边粗细对话框调查](reconstruction/edge_thickness_dialog.md) | issue #2 根因、原版窗口回调与数值行为核对、回归范围 |
| [D3D9 / D3DX 运行时调查](reconstruction/d3dx_runtime_failure.md) | 历史失败的已知证据、会话环境影响与诊断方法 |

## 历史归档

以下材料保留原分析的时间与适用架构，不代表当前覆盖率。旧源码行号、绝对地址、阶段名称和“已覆盖”结论须重新核对；历史测试结果也不能代替当前测试。

| 记录 | 用途 |
|---|---|
| [2026 年 9—10 月对齐记录](archive/alignment_worklog_2026-09_10.md) | 修复依据、当时的测试结果及未完成验证 |
| [x86 子系统台账](archive/x86/subsystem_ledger.md) | 附件接口与调试绘制的旧地址对应 |
| [x86 数学台账](archive/x86/math_kernel_ledger.md) | Bullet 内联函数与应用数学函数的旧归属分析 |
| [x86 MMDxShow / CRT 台账](archive/x86/mmdxshow_crt_ledger.md) | DirectShow 与编译器运行库代码的旧边界分析 |

维护文档记录仍需遵守的规则；调查记录写明日期、输入、结果和限制。新的修复应更新相关说明，避免重复复制整段源码注释或追加没有结论的流水账。
