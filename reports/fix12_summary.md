# 第十二轮：已知差异修复

## 基线

用户要求先提交已有工作，再逐问题验证和修复。基线提交为 `4833705`（Restore typed MMD state and embed MME with verified parity fixes），包含进入本轮前全部173个文件改动；提交后工作树干净。无AI署名或尾注。

## 一问题一代理

| 编号 | 问题 | 负责代理 | 状态 |
|---|---|---|---|
| F12-01 | 缺标准效果时一次提示/失败返回，与原MMHack重试状态机不同 | fix12_effect_failure | 已重新IDA核验并修复；新增失败设备Cleanup修复；[报告](fix12_effect_failure.md) |
| F12-02 | 旧硬件32位索引软件拆三角回退未恢复 | fix12_mesh_fallback | 已恢复精确分流并调用原D3DX实现；[报告](fix12_mesh_fallback.md) |
| F12-03 | 缺D3DX时加载边界不同 | fix12_d3dx_startup | 已恢复普通PE导入；两架构独立入口前失败验证通过；[报告](fix12_d3dx_startup.md) |
| F12-04 | 模型骨骼顺序/外部亲绑定表仍使用void*及重复记录视图 | fix12_bone_binding_type | 已恢复共享记录类型、匹配数组释放及shutdown清理；[报告](fix12_bone_binding_type.md) |
| F12-05 | 排序对话框共享临时数组分配释放家族混用 | fix12_dialog_array_owner | IDA确认附件数组应存slot整数，已修复建表/交换/应用/选择，并恢复RAII所有权；[报告](fix12_dialog_array_owner.md)、[IDA证据](fix12_dialog_array_ida.md) |

主线负责协调、构建集成、跨架构回归与状态汇总。保留用户要求的MME静态内置；已核实的原版音频/DirectShow异常行为不当作移植缺陷擅改。尚未验证的庞大功能矩阵不能冒充已知缺陷，也不以猜测的源码类型覆盖未知字段。

## 根因与关键结果

- 标准效果失败只有一个初始化状态机：失败不提交ready，后续帧重试；恢复原弹窗、disabled粘滞、S_OK且不调用设备BeginScene的行为。另修复初始化失败时部分资源未Cleanup。
- 软件索引回退在相同设备能力/缓冲容量条件下调用原D3DX DrawSubset。原MMHack未拦截DrawPrimitiveUP，因此不复制私有算法、不hook即可保留微软实现。
- D3DX使用普通、非延迟的PE导入。生成器只生成正常导入库的编译元数据，没有产品替代DLL、补丁或SDK绝对路径。兼容Api facade保留既有float数组调用边界，其Load仅为无状态兼容入口，不再加载/解析DLL；全面数学类型迁移不计为本轮已完成。
- 绑定表生产、PMM/播放、UI副本和删除模型使用同一个BoneOrderEntry，不再用不同结构别名或20字节裸步长解释。
- 排序问题验证推翻最初“原版存对象指针”的假说：原版存slot索引。当前移植误存对象指针、按4字节移动以及从对象首字节选择，均已从根因修复；两类对话框工作区分别由unique_ptr<int32_t[]>管理。

## 集成验证

最终 x64、x86 Release 完整构建均退出0，CTest各16/16全部通过，共32次测试执行。宿主PE检查确认两架构使用对应D3DX普通导入，没有D3DX延迟导入，也不依赖MMEffect.dll/MMHack.dll。

本轮新增7项自动检查：初始化失败重试、软件回退分流、真实D3DX直接调用、宿主PE导入、缺导出入口前拒绝、绑定表生命周期/编辑、附件排序实际生产核心。结合原有9项回归，形成双架构16项套件。

命令：

```powershell
cmake --build build-x64/build --config Release
ctest --test-dir build-x64/build -C Release --output-on-failure
cmake --build build-x86/build --config Release
ctest --test-dir build-x86/build -C Release --output-on-failure
```

最终日志：build-x64/fix12-build.log、build-x86/fix12-build.log，以及各build/Testing/Temporary/LastTest.log。初次集成发现4处Api.module遗留门控，已经全部清除；初次失败日志另存fix12-build-initial.log。x64分目标构建曾遇MSBuild日志文件瞬时占用，单目标重跑已通过。

本轮新增修复保留在工作树，未并入用户要求先保存的基线提交4833705；未推送远程。

## 未覆盖及仍保留的边界

- MME静态内置是用户明确要求，不作为要恢复的DLL注入差异。
- 原音频线程创建失败等待、DirectShow等待/消息行为保留；本轮没有承诺消除原版自身的挂起。
- 未运行真实逐帧模态错误GUI、旧显卡画面或Win32排序对话框交互。测试直接覆盖生产核心/状态类型，不能替代GUI和硬件验收。
- 缺导出的入口前拒绝有双架构真实子进程证据；没有删除系统DLL测试完全缺失，后者结论基于正常PE导入结构。
- 附件排序回归覆盖合法非空列表；原空列表首项读取未扩张改写。
- 未恢复的全项目padding/裸字节句柄、全局初始化证据链、完整PMM往返、复杂效果、Kinect硬件与长期物理轨迹仍需后续逐项核验。本轮五个问题修复不等于全项目严格一比一证明。
