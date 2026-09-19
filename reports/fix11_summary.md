# 第十一轮修复汇总

日期：2026-09-19。基准是用户给定的 MMD 9.32 x64 与 MME 0.37 English；x86 仅做兼容构建与回归。保留进入本轮时已有的工作树改动，没有创建提交。

## 详细目录与分工

完整条目、涉及文件、问题及最终状态见 [fix11_checklist.md](fix11_checklist.md)。三个子代理均先核验源码、原版反汇编或可执行复现，再修改；发现误判时更正审计记录。

| 负责者 | 范围 | 证据及改动 |
|---|---|---|
| audit_core | include/mikudancestudio、src/model、相关 src/io | [模型与布局](fix11_core.md) |
| audit_app_ui | src/app、src/features、src/window、src/mmdxshow | [姿态、撤销与 UI](fix11_pose.md) |
| audit_mme_render | third_party/mmeffect、src/render | [MME 修复](fix11_mme.md)、[IDA 证据](fix11_mme_ida_evidence.md) |
| 主线 | src/physics、跨域所有权、CMake、MME 生命周期与资源、集成回归 | 本报告 |

## 已落实的根本性修复

1. 模型恢复标准姿态、关节历史、当前关节、录制指针与 PMX 表等真实成员；不再用 x86 偏移初始化 x64 对象，避免指针及 toon 路径被覆盖。名称运行时容量与 PMD 磁盘宽度明确区分。
2. Kinect 生产者和消费者共享类型化结构；修复骨骼指针按错误步长遍历。撤销/重做帧号 API 改引用，菜单及键盘调用全链迁移。
3. 模型、撤销、全局及附件轨道统一匹配分配/释放家族；覆盖 PMM v1、重置、删除与退出。合法 malloc/free 剪贴板缓冲不做无差别替换。
4. Bullet 退出恢复正常类接口调用及原释放顺序；移至 scene_dispose.cpp。PhysicsWorld 用受保护成员访问诊断时间累积量，不再硬编码 Bullet 内部地址。
5. 框选行列独立遍历，并恢复三个 40000 元素行命中数组。跨线程音频停止标志使用 Interlocked。MMDxShow 四处 HRESULT 按原 DLL 修正。
6. MME 同名对象注册按绘制顺序及原树查找/回退规则；对象状态恢复真正的单位矩阵。Mesh subset 去除 128 项上限，按实际 D3DX 语义处理属性表/连续范围与错误。Mesh/Effect 使用统一类型化 COM 方法。
7. MME 从 SHARED DLL 改为静态库直接链接。WinMain 显式管理运行时，菜单/对话框归宿主实例，图标子资源显式编号，避免两个资源空间冲突。不再生成循环导入库。
8. D3DX 加载缓存记住真实可用性，避免缺少导出的模块第二次被误报成功。缺运行库不再进入半初始化渲染流程。

这是一组经过验证的结构恢复，不是声称找回原作者唯一的源码。部分 ABI/磁盘布局仍需要固定宽度、布局断言及借用指针；本轮不声称已清除全项目所有历史偏移访问。

## 验证

最终 x64、x86 Release 完整构建均成功，CTest 各 9/9 通过（共 18 次测试执行）。

```powershell
cmake --build build-x64/build --config Release
ctest --test-dir build-x64/build -C Release --output-on-failure
cmake --build build-x86/build --config Release
ctest --test-dir build-x86/build -C Release --output-on-failure
```

- 原 MMD 资源：python scripts/gen_resources.py verify，两份资源均 byte-identical。
- PE 导入检查：x64/x86 EXE 均不导入 MMEffect.dll 或 MMHack.dll，均保留 37 个 Exp* 导出。
- 回归涉及初始化不变量、模型销毁、姿态映射/历史、框选与线程协议、实际撤销/重做入口、Bullet 残留对象清理、MME 名称/矩阵/范围、真实 D3DX NULLREF ABI，以及宿主/MME 资源共存。
- 撤销用例是实际 Undo/Redo 的零骨骼空编辑，验证帧号写回和环游标；物理通知边界设置为误调用立即失败，不将其计为骨骼物理编辑覆盖。
- 损坏 D3DX 运行库重复加载由 MME 子代理在隔离测试目录验证，见专项报告；不把测试 DLL 放入产品目录。

构建日志：build-x64/fix11-build.log、build-x86/fix11-build.log。CTest 日志位于各 build/Testing/Temporary/LastTest.log。进入本轮前的追踪文件快照和哈希保存在 build-x64/audit11-baseline。

## 明确保留的差异与未验收范围

- 缺标准效果的错误路径采用一次提示并返回不可用，区别于原版逐帧模态框与伪成功；这是明确差异，不能计为严格对齐。
- DirectShow 部分失败等待、WM_QUIT 消费及波形线程创建失败等待经核验属于原版行为，本轮保留，未擅自“修正”为不同产品语义。
- 极旧硬件的 32 位索引软件拆三角回退尚未实现完整对照。
- GUI 自动化尝试因 Computer Use native pipe 不可用失败，未执行启动及菜单交互验收；资源测试不能代替此项。
- 未完成 Kinect 真硬件、真实 GPU 图像、RayMMD/复杂效果组合、设备 reset 场景矩阵、录制端到端、PMM 跨程序往返、长期物理数值轨迹的完整差分。
- 全局初始化时序等剩余证据欠账见 [PORTING_STATUS](../docs/PORTING_STATUS.md)。本轮修复不构成全代码严格一比一证明。
