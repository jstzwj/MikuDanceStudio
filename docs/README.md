# 文档索引

构建步骤见项目 [README](../README.md#building-from-source)，版本、打包与发布流程见 [RELEASES](RELEASES.md)。

## 当前维护文档

| 文档 | 内容 |
|---|---|
| [架构](ARCHITECTURE.md) | 子系统、模块边界与有意保留的兼容偏差 |
| [可移植内核](M1_PORTABLE_CORE.md) | 格式文档、场景会话和 Windows 适配边界 |
| [RHI 架构](RHI_ARCHITECTURE.md) | 资源生命周期、CPU 场景输入及 DX9／DX11／DX12 预览 |
| [移植与验证状态](PORTING_STATUS.md) | 最近验证结果和仍未覆盖的行为 |
| [x64 重建基准](X64_RECONSTRUCTION.md) | 参考二进制身份、证据规则与布局维护方式 |
| [源码格式](FORMATTING.md) | clang-format、VS Code formatter 和批量格式检查命令 |
| [诊断工具](../tools/README.md) | 保留的手动审计、数据对比和 PageHeap 工具及其适用范围 |

维护规则集中在上述文档中，最新验证结果更新到移植状态。历史调查、重构过程和对齐台账可通过 Git 历史查阅；复用旧结论前须重新核对架构、二进制和实际输入。
