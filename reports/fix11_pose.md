# 第11轮：Kinect / 撤销所有权 / UI与媒体协议修复

## 已修复

### Kinect 全链类型恢复（与 core agent 协作）

新增 include/mikudancestudio/skeleton_tracking.hpp：StandardSkeletonPose（17个具名四元数）、SkeletonJoints（23个关节）、SkeletonHistory（每关节30个样本）。core agent 将它们恢复为 ModelRecord 的真实成员并恢复初始化/poseTrace指针槽。本 agent 将以下消费者迁移到这些成员：

- oni_skeleton_pump.cpp：采集通过 CaptureSkeletonJoints 按 OpenNI 协议ID写入 typed currentJoints，无 model+14280 等地址计算。
- dialog_helpers.cpp：平滑通过 typed history/current 数组；标准骨骼应用使用具名四元数，删除 model[8616]；查找左足/左足首使用有边界的 FindSkeletonBone，删除 BoneRecord* +=604。
- audio_pose_helpers.cpp：标准四元数构建删除 F(旧偏移)/QOut(旧偏移)、裸写 matMisc；17组完整四元数消除原 localTransforms[67] 少一个 float 的问题。
- 修复额外查明的 C++ 栈布局假设：vec3Normalize(&ax,&ax) 等不能假设独立 ax/ay/az 连续；现用实际 float[3] 往返，同样运算顺序。

证据：原版 x64 0x7FF7CB44C154 的关节写入目标为 model+0x3B70，而旧实现的 model+14280 落入 toon 文件名；新实现不依赖体系结构数值偏移。17四元数和23历史通道来自原版消费者及构造清零区域，实际布局断言由 core 所有。

### 撤销缓冲所有权

undo.bonePose / auxiliaryPose 与 poseTraceBuffer 统一 ::operator new / ::operator delete。生产者覆盖 features、Kinect、frame_line_edit、command_frame_edit、command_file_menu、command_panel_toggles、misc_dialogs、ui_editor_click、playback_state；core 配合覆盖 src/model 销毁和生产者。没有笼统替换所有 free，App clipboard 等 malloc 池保留原配对。

### U1：MMDxShow HRESULT 错误

本轮读取原版 x64 Data/MMDxShow.dll：0x180003C90 是 mov eax,8000FFFFh;ret。IPin 槽 0x1800083C8/3D0/3D8 和 CSourceStream 默认槽 0x180008BD0 指向该体，前一槽0x1800083C0指向0x180002CB0（E_NOTIMPL）。因此仅将 EndOfStream、BeginFlush、EndFlush、默认 GetMediaType1 改为 E_UNEXPECTED；QueryInternalConnections 继续 E_NOTIMPL。

### U2：音频线程协议

原版 CloseDataFile 0x7FF7CB4FA250 使用 0=运行/1=请求停止/2=完成轮询。保留状态机与 Sleep 时序，用 Interlocked 操作让 C++ 实现具有线程间发布语义，避免普通 uint32_t 和局部 volatile 转换混合的数据竞争。

原版 WaveSeekAndFeed 0x7FF7CB4FAB80 在 0x7FF7CB4FAC0A 把 _beginthread 返回值直接存入句柄，确实不检查失败。该失败后的潜在无限等待是原版行为，本轮未加新恢复策略，不能描述为已消除所有音频挂起。

### U3：多行选框列漂移及数组真实容量

原版附件框选 0x7FF7CB459875 每一行重新设置列游标、0x7FF7CB4598BC 只推进行基址；骨模式 0x7FF7CB459A6C 为 r8=r10，0x7FF7CB459BB9 推进行基址。旧代码内外循环增同一指针，第二行开始错误偏移。现以 VisitTimelineSelectionCells 逐行独立定位。

app_layout.hpp 三个 hit grid 原是 [200]+padding/伪模型字段，却被40000元素访问。已恢复 rowHitBone/Morph/Ik[40000]，扫描确认伪字段无 App 业务消费者，去掉它们的误导性 pins，保留三个起点及后续真实锚点。ui_mousemove 清除/高亮改为各自数组索引，不再跨数组 map[-40000/+40000]。

## 核验后明确保留

U4：原版 DirectShow teardown 0x7FF7CB42B8D0 的 0x42B936/0x42B95A 忽略 GetEvent 返回后直接 FreeEventParams；0x42B984 的消息循环确实无 WM_QUIT 处理，并且等事件无超时。本轮据证据关闭为原版行为，不修改 shutdown_cleanup.cpp。初始化为零等确定性差异也不能据此认证逐位一致。

## 回归与限制

新增 tests/skeleton_tracking_test.cpp：在真实 ModelRecord 中注入OpenNI采集，不污染toon；13/14/15版本关节门；第二/最后骨遍历、空和缺失名字；平滑排除无效样本、历史边界。

新增 tests/ui_protocol_test.cpp：多行矩形选择不漂移，空选择不访问，真实App三grid末元素独立，实际线程的0→1→2协议和数据发布。

全工程统一构建/CTest由主线执行，最终结果见主线验证记录。本报告不把测试文件存在等同测试通过。源码定向扫描和 git diff --check 无新增格式错误。没有 Kinect/OpenNI 硬件实测，没有录制导出端到端对比，没有完成全部四元数输出的原版数值差分；本轮不能宣称所有Kinect/MME/UI功能100%一致。

## 追加修复：撤销/重做把帧值当指针

本轮统一编译暴露C4312后复核：菜单400/401把currentFrame整数reinterpret_cast成指针，callee在type2/3/4路径写*frame，导致帧0静默退出或非零帧非法写。原版0x7FF7CB4649CB和0x7FF7CB464B0D均明确lea rdx,[rbx+1450h]，传的是帧变量地址。

已将UndoModelEdit/RedoModelEdit API改为std::int32_t&，迁移声明、实现、递归、菜单和键盘全部调用。tests/undo_frame_test.cpp驱动实际Undo/Redo实现，验证type2轨道编辑恢复调用方帧及环游标；编译期函数签名断言阻止旧指针接口回归。测试构建结果由主线统一记录。

### Undo 回归的链接边界

`tests/undo_frame_test.cpp` 调用真实 `UndoModelEdit` / `RedoModelEdit`，使用零骨骼的空编辑验证调用者帧恢复及环游标。该翻译单元还引用 `NotifyBonePhysicsMode`，真实实现位于 `src/model/model_keyframe_advance.cpp:224` 并依赖 Bullet 动画子系统；本测试提供明确输出错误并 `std::abort()` 的边界函数，任何意外物理调用都会失败，不会静默模拟成功。因此该测试不覆盖带骨骼/刚体的撤销，也不构成物理模式一致性的验证。构建与运行结果由主线统一记录。
