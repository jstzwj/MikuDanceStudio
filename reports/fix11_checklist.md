# 第十一轮：不对齐目录清单与修复验收

对象：当前工作树，保留第十轮之前的用户修改。执行规则：每个负责者先重新核验，再根治；不能把待核验猜测直接改进程序。未列出的行为不自动视为已对齐。

## 第一组：模型、布局、文件边界（audit_core）

| ID | 文件/目录 | 问题 | 修复与验收要求 | 状态 |
|---|---|---|---|---|
| C01 | src/model/model_init.cpp；include/mikudancestudio/model_layout.hpp | x86四元数偏移覆盖x64拥有指针，67-float声明不足68分量 | 恢复真实四元数数组；初始化后指针为空；失败加载可安全销毁 | 已修复并验证：17个完整具名四元数、空游标及空模型销毁回归通过 |
| C02 | model.hpp/model_layout.hpp；src/model/pmx_load.cpp | pose trace指针与PMX头重叠 | 真指针与明确header成员；写合法PMX头不得改变指针 | 已修复并验证：真实指针与头成员分离，合法PMX头不改变trace指针 |
| C03 | src/model/model_init.cpp | 历史表错移56B，-999写入toon路径，裸memset范围错误 | 恢复具名历史/关节字段；原版初始化差分；保持toon不被误写 | 已修复并验证：690个历史sample、23当前关节sentinel及toon隔离通过 |
| C04 | src/model/model_dispose.cpp | FreeField(8732)保留x86槽 | 查明真实拥有字段与生产者，类型化释放 | 已修复并验证：具名reservedMorphTable释放；元素语义仍未知，不捏造类型 |
| C05 | src/model/各分配者与model_dispose.cpp | operator new/free等混用 | 全链统一分配释放家族，保留释放顺序 | 已修复核对到的链路：模型/表/undo/VPD/物理编辑表统一new/delete；malloc池保留free |
| C06 | model_layout.hpp；pmd_load.cpp/pmx_load.cpp；必要src/io调用者 | 20B名称成员被50B写入 | 恢复50B运行时容量；PMD磁盘读取仍严格20B | 已修复：运行时50B，PMD固定读取20B；完整多编码往返待补 |
| C07b | src/model/keyframe_common.hpp | 未使用RdPtr读取4B到x64指针 | 核实无调用后删除，或正确类型化 | 复核无调用后删除两个RdPtr |

## 第二组：姿势捕获与撤销（audit_app_ui）

| ID | 文件/目录 | 问题 | 修复与验收要求 | 状态 |
|---|---|---|---|---|
| A2 | src/app/oni_skeleton_pump.cpp；src/features/dialog_helpers.cpp、audio_pose_helpers.cpp | Kinect采集/历史/四元数消费者使用x86数字索引 | 与第一组共享真实tracking类型，整链迁移，不只改写入端；合成捕获不污染toon/指针 | 已修复并定向验证：tracking生产/消费者整链迁移；合成OpenNI测试通过，硬件待测 |
| A3 | src/features/dialog_helpers.cpp | BoneRecord* +=604 | 按元素遍历、空数组边界；含多骨骼与未命中回归 | 已修复并验证：元素遍历及空/末骨/未命中回归通过 |
| A4a | include/undo_layout.hpp；src/window/frame_line_edit.cpp；src/features/audio_pose_helpers.cpp；src/app/oni_skeleton_pump.cpp | 同一undo.bonePose混用malloc/new/new[]与free/delete[] | 明确统一拥有规则并检查全部消费者 | 已修复：跨域undo/poseTrace统一scalar new/delete，clipboard malloc池不混改 |

## 第三组：MME与渲染（audit_mme_render）

| ID | 文件/目录 | 问题 | 修复与验收要求 | 状态 |
|---|---|---|---|---|
| R1 | third_party/mmeffect/src/mmeffect/material_bind.cpp、mme_context.cpp、pass_planner.cpp | 同名CONTROLOBJECT按注册顺序而非绘制顺序查找，缺原回退 | IDA核验名称树规则，精确选择与重排回归 | 已修复并定向验证：原名称树/order/首项/前驱回绕语义通过 |
| R2 | third_party/mmeffect/src/mmeffect/model_data.h/.cpp | 四color数组冒充矩阵，重置第一列全1 | 真实矩阵成员，重置单位阵与刷新测试 | 已修复并定向验证：真实world矩阵及单位阵重置通过 |
| R3 | src/render/mme/mme_bridge.cpp | 128项上限、失败整网格回退；旧多range定论经原版复核撤回 | 动态表；优化表index优先/第一匹配，无表连续面属性run；129项/重复项/失败/16与32位错误传播回归 | 原版复核后修复；撤回同ID优化表应画全部ranges的误报，实际ABI及边界回归通过 |
| S1 | src/render/fx_slots.hpp；third_party/mmeffect/include/d3dx9.h；mesh调用点 | 手工COM虚表与双ABI定义 | 统一类型声明和正常虚方法调用，保留调用时序 | 已修复并实际ABI验证：统一类型、正常COM调用，真实D3DX9_43 + NULLREF通过 |

## 主线与第二波：物理、宿主集成和待核验项

| ID | 文件/目录 | 问题 | 修复与验收要求 | 状态 |
|---|---|---|---|---|
| A1 | src/app/shutdown_cleanup.cpp | Bullet退出读x86偏移/虚表 | 用Bullet类型与API，保持原释放顺序；带残留对象的清理验证 | 已修复并定向验证：Bullet API清理，残留对象回归通过 |
| C07a | src/physics/physics_frame.cpp | DIAG localTime两处不一致裸偏移 | 查明目的后改成真实接口/诊断派生访问，不引入release行为变化 | 已修复：PhysicsWorld诊断接口替代裸偏移，不增加release物理行为 |
| A4b | src/window/ui_init.cpp；src/app/scene_ownership.cpp；全局轨道生产者 | 全局轨道new/free混用 | 统一轨道拥有规则、检查重置/退出 | 已修复核对到的链路：轨道统一new/delete，含PMMv1生产/删除附件/场景/退出；wrapper配对同步修正 |
| A5 | src/main/winmain.cpp | nothrow new与原throwing new的OOM行为不同 | 对照原版失败语义后处理 | 原版静态核验后修复为throwing new；未执行真实OOM故障注入 |
| U1 | src/mmdxshow/source_base.cpp | E_NOTIMPL与注释E_UNEXPECTED矛盾 | 原版MMDxShow.dll反编译确认后决定是否修 | 原版核验后修复：三个IPin方法与默认GetMediaType1为E_UNEXPECTED；QueryInternalConnections保留E_NOTIMPL |
| U2 | src/app/subsystem_init.cpp；src/window/ui_refresh.cpp | 跨线程stopFlag及启动失败等待 | 核验线程协议，正确同步；失败等待按原版证据裁定 | 同步已修复并测试；_beginthread失败后潜在无限等待保留原版，不宣称挂起全部消除 |
| U3 | src/window/ui_mousemove.cpp | acc递增疑似重复行推进 | 核对原版循环，误报则明确关闭 | 原版核验确为缺陷，已修复并测试逐行定位及三个真实40000元素grid |
| U4 | src/app/dshow_record_graph.cpp | 无限等待/失败事件参数/WM_QUIT处理 | 原版差分确认；不凭常识改掉原语义 | 核验后保留原版：无超时/无WM_QUIT处理/忽略GetEvent结果后FreeEventParams，不作常识性改写 |
| R4 | src/render/d3d_init.cpp；src/render/mme/mme_bridge.cpp | 无D3DX/标准effect的产品回退不同 | 核验原版依赖失败语义，不混淆正常D3D初始化失败 | 明确有意差异：缺D3DX启动失败；缺standard effect只提示一次并返回D3DERR_NOTAVAILABLE跳过绘制，区别原版逐帧模态重试/伪成功 |
| S2 | CMakeLists.txt；third_party/mmeffect/include/MMDExport.h；mme_host.cpp；MME资源/UI | 原SHARED与EXE循环导入需改为宿主内建 | 静态/对象库直接合入，处理资源ID/HINSTANCE/初始化；无代理或注入 | 已完成STATIC直链与资源ID/HINSTANCE集成，资源测试通过；无代理/注入，复杂效果A/B待补 |
| D1 | CMakeLists.txt；布局头；conanfile.py；README与缺失docs | 不存在的生成器/证据链接，x86/x64说明过时 | 写准确可重现维护说明，不虚构原始生成器 | 已修复：两份docs恢复、布局人工维护说明、Conan x64基准及MME集成描述更新 |

## 追加核验项

| ID | 范围 | 实际结果与状态 |
|---|---|---|
| U5 | UndoModelEdit/RedoModelEdit与菜单/键盘调用 | 已修复帧整数冒充指针，改为int32_t&；原版明确传帧变量地址。新增undo_frame_test驱动实际type2撤销/重做和环游标，x64/x86均通过。 |
| R5 | D3DX Api::Load缓存 | 已修复并定向验证：缓存真实available状态，不完整fixture连续两次Load均false，真实runtime随后再次通过；fixture仅在临时构建目录，不替换系统或产品DLL。 |

## 验证和状态原则

主线最终确认：**x64和x86完整构建均退出0，CTest各9/9全部通过**，包含新增undo_frame_test。完整结果见 [fix11_summary.md](fix11_summary.md)。

证据和复现： [核心修复](fix11_core.md)、[姿势/UI/协议修复](fix11_pose.md)、[MME修复](fix11_mme.md)、[MME IDA证据](fix11_mme_ida_evidence.md)。修复后测试不等于MMD/MME全面严格一比一。

仍需补足：真实PMD/PMX/PMM多编码与失败/取消/重载往返；Kinect/OpenNI硬件；完整Undo种类与长序列；录制/音频端到端和故障注入；IK、seek/advance、SDEF/UV/材质morph与长期Bullet数值轨迹；真实GPU/RayMMD、多效果组合、设备reset/效果切换。DrawSubset极旧硬件32位索引软件拆三角回退仍未纳入宿主桥覆盖；全项目裸字节句柄、未恢复padding与拥有关系也未全部迁移。

U2失败等待和U4事件等待明确保留原版；R4为明确记录的有意差异，不能用通过构建抹去这些语义边界。

初始工作树快照位于本地 `build-x64/audit11-baseline`，包含受Git跟踪文件原样副本、哈希和初始diff，用于把本轮修改与用户既有修改区分。
