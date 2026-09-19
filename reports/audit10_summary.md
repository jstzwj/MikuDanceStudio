# MMD / MME 第十轮审计：行为一致性与源码级重建

## 结论

**当前工作树不是 MMD v9.32 x64 + MME v0.37 的严格一比一实现，也尚未完成源码级结构恢复。** 本轮发现的初始化指针破坏、骨骼遍历步长错误、x64 物理对象偏移错误与 MME 对象选择错误，已足以否定严格一致性；不需要也不应以估计百分比表示完成度。

项目已有大量真实实现和已命名的数据结构，不能称为纯空壳。问题是类型化结构与旧字节访问并存，局部重命名并没有消除跨架构错误。仅匹配结构总大小、部分 static_assert、能编译或能打开普通模型，都不足以证明行为一致。

## 基线和覆盖边界

- 基线提交：`117095139794325a2e1dfe5d5d295d12a90613b3`，**实际审计对象是含既有未提交修改的工作树**，不是该提交的纯净版本。审计日期按用户环境为 2026-09-19。
- 3 个 subagent 分别负责数据/模型/物理、应用/UI/媒体、渲染/MME；主线复核高风险发现、结构边界、构建和资源。
- [文件清单](audit10_inventory.json) 对 256 个源代码/脚本/资源文本/构建文件记录路径、行数和 SHA-256，共 121,507 行；[生成脚本](audit10_inventory.py) 可重跑。全部枚举到的 `.cpp` 均显式出现于 CMake 源文件列表。
- 清单是全量盘点，不是 12 万行均完成逐函数反编译对照。各分报告区分重点审读与结构扫描。预编译二进制资源、外部依赖库本体、未纳入清单的资产不算本次逐行审查覆盖。
- 原版 MMD SHA-256：`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`。
- 原版 MME SHA-256：`74b3f5882a3bad28131cbc57994e2f960938e6ca850fc7139018ce4822bd87dc`。
- 本次实际调用 IDA 分析用户指定目录中的二进制。主线保存 [IDA 原始证据](audit10_ida_evidence.json)，分报告另给函数地址和探针结果。反编译变量名不是原作者源码名。

## 已确认的关键问题

| 优先级 | 问题与入口 | 当前实现证据 | 影响与验证边界 |
|---|---|---|---|
| P0 | x64 模型默认初始化覆盖指针 | `src/model/model_init.cpp:115` 仍从 `m+64` 写四元数；原版 x64 `0x7FF7CB4C9610` 从 +120 开始 | 独立布局探针证明 boneKeyCursors/morphKeyCursors 变成 `0x3f80000000000000`。失败加载/取消之后释放路径可访问非法指针；成功加载可能重新赋值掩盖问题 |
| P1 | PMX 头与姿势录制指针重叠 | `include/mikudancestudio/model_layout.hpp:67` 的伪 `uint32_t pmxVertexCount`；`model.hpp:259` 的 8 字节 PoseTraceBuffer | x64 PMX 头当前从 +8680 开始，恰与指针重叠；原版头从 +8688 开始。PMX 加载后启用姿势录制可能 free 由头字段拼出的值 |
| P1 | 初始化表错移、哨兵写进文件名 | `src/model/model_init.cpp:140` 和 `:106` 起的裸偏移 | morph 表比原 x64 起点早 56 字节；若干 -999 写到 toon 文件名，真实状态没有正确初始化。与首行指针覆盖是同类但不同写点 |
| P1 | 标准姿势遍历把字节数当元素数 | `src/features/dialog_helpers.cpp:642`、`:659` 的 `BoneRecord* += 604` | 应逐骨骼前进，当前每次跳 604 个骨骼；进入身高归一化分支后可越界。不表示所有普通播放都会经过该分支 |
| P1 | 物理世界销毁仍读 x86 对象字段 | `src/app/shutdown_cleanup.cpp:174` 起，world +8/+16，obj +244/+516/+204 | 原版 x64 `0x7FF7CB422580` 为 +12/+24、+264/+536/+208。当前数组指针与计数读取错误；严重度取决于退出时对象残留，不能只凭空场景退出判定正常 |
| P1 | Kinect 坐标链保留旧模型偏移 | `src/app/oni_skeleton_pump.cpp`、`src/features/dialog_helpers.cpp` | `float*` 数字索引直接解释模型，无法随 x64 字段扩展迁移。详见应用报告的具体写点与条件 |
| P1 | MME 同名 CONTROLOBJECT 选择依据错误 | `third_party/mmeffect/src/mmeffect/material_bind.cpp:2184` 起 | 按 ctx->models 注册顺序近似原版；IDA 确认原版用 abs(ExpGetPmdOrder/ExpGetAcsOrder) 的绘制顺序，还有不同回退行为。重排同名对象后参数源可能错误 |
| P1 | 附件材质属性表硬上限 128 | `src/render/mme/mme_bridge.cpp:400` | 超过上限或表查询失败时沿用整个 mesh 的范围，可能每材质重绘整网格；这是当前控制流确认的问题，未穷尽 D3DX DrawSubset 的全部边缘语义 |
| P2 | MME 矩阵重置写成第一列全 1 | `third_party/mmeffect/src/mmeffect/model_data.cpp:228` | 原版 `0x18005B9E0` 写单位阵对角线。常规后续刷新会覆盖，确认的是重置/缺宿主对象的状态差异，不夸大成所有画面必错 |
| P2 | 拥有型缓冲分配/释放家族混用 | `frame_line_edit.cpp:84`、`audio_pose_helpers.cpp:429`、`oni_skeleton_pump.cpp:355` 及 `model_dispose.cpp` | 同一 undo.bonePose 跨入口使用 malloc/operator new/new[] 与 free/delete[]；当前 CRT 底层偶然兼容不等于 C++ 配对合法 |

详尽证据和次级风险分别见 [核心报告](audit10_core.md)、[应用与 UI 报告](audit10_app_ui.md)、[渲染与 MME 报告](audit10_mme_render.md)。同一根因在多个调用点出现时不重复计算完成率。历史 audit8/9 的已修清单不能代替当前工作树证据，其“字节级一致”“≥98%”不应继续用作验收结论。

## “像原作者写的源码”应如何落实

可以恢复有证据支持的成员、类型、算法、控制流和调用时序，但不能从机器码唯一还原原作者的命名、类组织、模板或注释。合理目标是**语义明确、正常 C++ 可维护、外部行为可对照的源码重建**。

1. **运行时对象与磁盘格式分开。** PMM/VMD/PMX 使用明确的读写函数和文件记录，不再让业务对象为凑原版 sizeof 而长期背负占位数组。文件字节布局、公开 ABI 必须保留；私有堆对象不必永久锁死原版物理布局。迁移时先确保所有裸偏移调用点都已消除，再移动成员。
2. **恢复真实类型，整体迁移生产者与消费者。** 例如 PoseTraceBuffer 就应是指针字段，不应以伪计数加跨字段强转表示；矩阵就应是 matrix，而非四段名称错误的 color；骨骼用 `BoneRecord&` 与元素遍历。此次 `+=604` 正是只改指针类型、不改步长的后果。
3. **消除业务层无类型访问。** `mdl::At<T>(m, n)` 只是裸偏移的语法包装；`MMDApp` 的 typed accessor 也不能抵消底层 `void*`、镜像字段和别名读取风险。当前 `MMDApp` 的 x64 mirror（如 `m_sceneLight`、`m_pathWorkspace`）是过渡方案，并非已经恢复原始类设计。
4. **通过真实接口调用 Bullet/COM。** 用 `btDynamicsWorld`、`btCollisionObject`、`btRigidBody` 的 API 表达清理；宿主与 MME 统一 D3DX 类型声明，通过 `effect->BeginPass()` 等方法访问，移除手工 vtable 槽取函数。编译器自然处理 x86/x64 的虚调用，槽号并非唯一正确写法。
5. **区分拥有关系与借用。** Win32/COM/Bullet 借用指针可以保留；拥有的分配统一到命名对象/容器/合适 deleter，保持原版可观察的析构顺序。不能为形式整齐批量改 shared_ptr，也不能把裸指针数量直接视为缺陷数量。
6. **逆向证据留在证据层。** 地址、RVA、原偏移、对应函数适合独立 ledger 和验证工具。注释里的 `0x7FF...` 不等于程序运行时硬编码地址。真正有问题的是业务代码用字节偏移读写状态，或按虚表槽手工调用函数。

文件词法盘点发现 reinterpret_cast 1,457 处、RawPad 174 处、At<T> 22 处、手工虚表相关匹配 14 处。**这些含注释/声明匹配，仅是定位指标，不是缺陷统计。** 应按类型与生命周期归并修复，不能机械替换。

另有证据链维护问题：`CMakeLists.txt` 引用的 `docs/X64_RECONSTRUCTION.md`、`docs/PORTING_STATUS.md` 在本仓库不存在；布局头指向的 `scripts/gen_model_layout.py`、`scripts/gen_bone_layout.py` 也不存在。必须恢复可重现的证据/生成流程，或改为真实维护说明；不能把“GENERATED”注释当成来源已验证。

## MME 内置路线

当前已经是宿主主动调用 `MmeHost*`，没有必要补回 d3d9.dll 代理、IAT patch 或设备虚表注入。`mmhack` 目录内的材质/光照/状态查询职责仍需要，但可以改名并纳入宿主接口。

目前 `CMakeLists.txt:324` 明确是 `add_library(MMEffect SHARED)`：它是随产品编译的独立 DLL，通过导入库链接，并且反向导入 EXE 的 Exp*。文档“静态链接内置 DLL”的说法容易与静态合并误混。要达到直接做进 MMD 的源码结构，建议最终以 STATIC/OBJECT 库并入宿主，内部用类型化接口直连；Exp* 只作为需要保留的兼容边界。

这一迁移还须处理 MME 菜单/对话框资源 ID 与宿主资源冲突、HINSTANCE 资源归属、初始化/退出顺序、D3DX 定义统一以及重复符号。不能仅把 SHARED 改成 STATIC 就宣称完成。`FrameContext` / `DrawContext` 应显式承载原拦截链提供的信息，并保留同一采样时机；不需要还原注入机制。

## 本次验证及下一步验收

- `cmake --build build-x64/build --config Release`：退出码 0，四个目标通过。这是当前已有构建树的增量构建检查，非全新环境重建，也不是行为等价证明。
- `python scripts/gen_resources.py verify`：退出码 0；x86 `.res` 369,228 字节、x64 `.res` 369,672 字节均与入库产物 byte-identical。只证明资源源码→当前资源产物重建一致，本次未逐项比较原版 PE 所有资源。
- 模型初始化独立探针与 IDA 对照见核心报告。没有运行完整 GUI/录制/Kinect/RayMMD A/B 矩阵，不把源码推导表述成用户界面的实测崩溃。
- 本轮只新增审计报告、盘点与证据；没有更改业务代码、实施 DLL hack 或提交 commit。

建议按以下验收单推进修复，每项必须留可复现输入及原版/重建版结果：

| 顺序 | 修复包 | 最小验收 |
|---|---|---|
| 1 | ModelRecord 初始化/PMX 头/pose trace 类型恢复 | PMD/PMX 成功、失败、取消、零骨骼/零 morph、启用录制；检查初始化后所有拥有指针与释放路径 |
| 2 | BoneRecord 遍历与 Bullet 类型化销毁 | ≥2 骨骼标准姿势；有/无刚体模型加载删除、场景重置与退出；堆检查并核对释放顺序 |
| 3 | MME 名称树/矩阵/动态附件 ranges | 同名对象重排、缺 owner、129 材质与不连续 range；记录控制参数和 draw 调用 |
| 4 | MME 编译进宿主与 typed host context | 无 MME 外置 DLL 启动；模型效果/scene/offscreen/设备丢失/录制/UI/EMM 生命周期对照 |
| 5 | 全部余留模块逐函数对照 | 文件往返字节、每帧骨矩阵/形态/物理轨迹、渲染状态/RT/截图、UI/undo/redo/声音/录制，记录已验证与未验证分支 |

“严格一致”只能在明确定义的行为范围内由证据建立。用户排除的 DLL 注入机制属于明确排除项；崩溃保护、缺依赖回退等其他有意差异要单列，不能默认为已经获准的一比一例外。
