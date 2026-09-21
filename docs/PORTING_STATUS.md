# 移植与验证状态

更新：2026-09-22。行为基准及维护规则见 [X64_RECONSTRUCTION.md](X64_RECONSTRUCTION.md)。当前项目仍处于**行为对照与结构恢复阶段**，没有完整等价证明，不使用“100%”“严格一比一已完成”或估算百分比描述状态。

## 当前证据入口

| 记录 | 用途与边界 |
|---|---|
| [fix19_summary](../reports/fix19_summary.md) | audit18 已确认问题的修复、纠正的旧结论、当前验收与明确保留的边界 |
| [fix19_input](../reports/fix19_input.md) / [core](../reports/fix19_core.md) / [MME](../reports/fix19_mme.md) | 第十九轮逐项实现与原版 IDA 证据；MME 仍为宿主内建 |
| [audit18_summary](../reports/audit18_summary.md) | 第十九轮修复前的审计快照，不能作为当前未修列表 |
| [audit10_summary](../reports/audit10_summary.md) | 修复前工作树的全局审计结论、参考哈希、覆盖范围 |
| [audit10_inventory](../reports/audit10_inventory.json) | 当时文件/行数/哈希清单；不是逐行正确性证明 |
| [audit10_core](../reports/audit10_core.md) | 模型/IO/物理/数学缺陷及逐文件检查等级 |
| [audit10_app_ui](../reports/audit10_app_ui.md) | 应用/UI/媒体/Kinect的专项审计 |
| [audit10_mme_render](../reports/audit10_mme_render.md) | MME/渲染语义和源码结构审计 |
| [fix11_summary](../reports/fix11_summary.md) | 本轮最终构建、9项回归及修复验收汇总 |
| [fix11_checklist](../reports/fix11_checklist.md) | 本轮逐项修复状态、保留行为及剩余欠账 |
| [fix11_core](../reports/fix11_core.md) | 模型结构、初始化、释放与测试的实际修改 |
| [fix11_pose](../reports/fix11_pose.md) | Kinect、Undo所有权与帧引用、UI和媒体协议修复 |
| [fix11_mme](../reports/fix11_mme.md) | 名称树、矩阵、DrawSubset、COM与D3DX失败策略 |
| [fix11_mme_ida_evidence](../reports/fix11_mme_ida_evidence.md) | MME及D3DX语义的本轮原版证据 |
| [fix12_summary](../reports/fix12_summary.md) | 基线提交4833705之后，逐问题重新核验、修复及最终集成结果 |

第十二轮最终验证：x64/x86 Release完整构建成功，CTest各16/16通过。新增状态机、回退分流、真实D3DX导入/入口前失败、骨骼绑定表和排序工作区回归。模型/绑定对话框共享类型已恢复，排序数组的错误对象指针解释已按原版改为slot索引及RAII所有权；具体证据和未覆盖范围见第十二轮汇总。

历史audit8/audit9是线索，不是永久未修列表。查看当前代码并重新验证后才能继续引用一个旧缺陷；本轮核心报告已明确标注若干旧PMX display/material问题已经修正。

## 模型结构修复

第十一轮已经修改源码，恢复真实的标准姿态/关节历史/当前关节/pose-trace成员、PMX计数及表成员，修复初始化写入错误和释放配对。`tests/model_initialization_test.cpp` 的六项独立检查，修复前全部失败，修复后全部通过；证据保存在 [fix11_evidence](../reports/fix11_evidence/)。这是内存不变量验证，未覆盖完整GUI/Kinect操作。

`tests/model_lifetime_test.cpp` 提供真实初始化/销毁回归。主线最终确认x64和x86完整构建均退出0、CTest各9/9全部通过，包含新增undo_frame_test（实际撤销/重做帧引用）。真实模型取消/失败/重新加载和跨程序文件往返仍需补足，最终运行状态以主线日志和清单为准。

## globals init

第十九轮已读取原 x64 的 `.rdata` 和读取指令：`g_QuatScaleFactor=2.0f` 对应 RVA `0x132B08`，`g_FrameScale=30.0f` 对应 RVA `0x132A64`。它们是已证实的常量，不是尚待猜测的启动赋值；当前声明已改为 const。原始证据与独立 quaternion helper 的算序恢复见 [fix19_core](../reports/fix19_core.md)。

## UI及历史阶段注释

`src/window/ui_init.cpp` 仍保留PHASE A、字体尾部和纹理加载等历史TODO；部分功能可能已迁往其他文件。这些注释是**待核验维护项**，本状态页不把注释直接等同于运行时缺功能。应按调用图、实际资源和交互测试逐项判定，再同步源码说明。

同样，`ported_funcs.hpp` 内遗留stub措辞需按真实定义判断；已有函数体不能因为旧注释继续被统计为未实现，存在函数体也不代表完整语义匹配。

## MME与验证欠账

MME已通过STATIC库直接合入宿主，资源ID、HINSTANCE/资源归属、初始化与导入导出边界已完成集成，嵌入资源测试通过；不使用代理DLL、IAT patch或虚表注入。旧审计的SHARED库描述仅代表修复前状态。

DrawSubset常规路径按原版优化属性表采用index优先/第一匹配，无表绘制连续面属性run；“同ID优化表全部ranges都画”的旧建议已撤回。第十二轮补足32位索引软件回退分流，调用原D3DX实现而不复制库算法，见[回退报告](../reports/fix12_mesh_fallback.md)。

第十二轮已撤销R4的有意差异：standard effect缺失恢复原逐帧初始化重试、提示、S_OK且不调用真实BeginScene；失败设备也执行Cleanup，见[初始化报告](../reports/fix12_effect_failure.md)。D3DX由运行时解析改为正常PE导入，缺失运行库或导出在程序入口前失败，见[加载边界报告](../reports/fix12_d3dx_startup.md)。旧fix11缓存测试是历史状态，现已由入口前拒绝加载测试替代。

音频线程创建失败后潜在无限等待（U2）和DirectShow无超时/无WM_QUIT处理（U4）保留已核验的原版行为。不能据此宣称错误路径全面改进或全面严格等价。

需要持续验证：同名CONTROLOBJECT选择、矩阵/材质语义、注释默认值、effect脚本执行顺序、离屏目标、设备reset、效果切换和真实社区效果包。局部解析或getter测试不证明RayMMD及全部MME效果兼容。

其他尚需证据的领域包括：PMM v1/v2跨程序往返、seek与advance一致性、IK边界、SDEF/UV/材质morph、固定输入下长期Bullet轨迹、录制/音频和硬件相关Kinect路径。记录具体用例和数值/图像结果，不用“成功打开一个场景”替代覆盖矩阵。

## 更新规则

每条修复记录至少包含问题入口、源码改动、参考证据、已运行的命令/用例、结果和剩余限制。历史报告保持历史事实；在新修复报告注明已解决/部分解决/待验证，并链接到可重现测试。缺失的原始生成器不伪造替代；布局头按人工维护流程更新。
