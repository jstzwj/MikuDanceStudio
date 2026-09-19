# App / UI / Media / MMDxShow 审计（2026-09-19）

结论：当前不能称为 MMD v9.32 x64 一比一严格实现，也未达到自然 C++ 源码结构。业务源码未修改。

## 范围与证据

对 app/main/window/media/features/mmdxshow 全部 93 个文件进行全文词法扫描和函数清单检查。重点读取退出、Kinect、撤销所有权、音频线程、DirectShow 错误返回。扫描不代表逐分支验证，未完成全功能动态差分。没有用旧注释的“1:1”“0 stub”当证明。

本轮只读 IDA 会话 531542a9，原版 EXE 基址 0x7FF7CB420000；新取证 sub_7FF7CB42BD40、sub_7FF7CB422580、0x7FF7CB44C141..0x7FF7CB44C1B0。未 patch 原版。

## 已确认问题

### A1 高：物理退出将 x86 Bullet 偏移用于 x64 对象

src/app/shutdown_cleanup.cpp:174/175 将 world+8/+16 当碰撞对象数/数组；178 行起将对象 +244/+516/+204 当类型/motion state/shape。

原版 x64 sub_7FF7CB422580 明确为 world **+12/+24**、对象 **+264/+536/+208**，证据地址 0x7FF7CB4225F9、0x7FF7CB42260B、0x7FF7CB422616、0x7FF7CB422618、0x7FF7CB42262E。工程 src/physics/scene_create.cpp:120 已正常 new btDiscreteDynamicsWorld，旧布局可能导致错误释放/崩溃。

应使用 getNumCollisionObjects/getCollisionObjectArray、btRigidBody::upcast、getMotionState/getCollisionShape 和正常 delete，保持原版清理顺序，不再补第二套魔数。

### A2 高：Kinect x64 采集覆盖 toon 文件名

src/app/oni_skeleton_pump.cpp:495..514 以 float* model 的索引 3570..3636 写关节，即字节 14280..14556。include/mikudancestudio/model_layout.hpp:513 确认 x64 pmdToonFileNames 从 14208 起、长 1000 字节，因此明确写进文件名数组。

原版指令 0x7FF7CB44C154 为 add rdx,3B70h（15216），随后 0x7FF7CB44C177 为 3B88h、0x7FF7CB44C19C 为 3B94h，差 936 字节。

src/features/dialog_helpers.cpp:224 的 ModelVertexHistoryPush 同样使用 x86 current/history 索引；src/features/audio_pose_helpers.cpp:689 的 InitStandardSkeletonQuats 直接读 F(14568)、F(14440)、写 QOut(64) 等。应恢复 SkeletonTrackingState 命名关节/四元数/历史样本并贯通整条管线。只改采集端不够。

另有 core agent 独立发现 PoseTraceBuffer 与 PMX 头字段覆盖问题，见 core 报告，本报告不冒充独立验证。

### A3 高：typed 骨骼指针仍按旧字节 stride 前进

src/features/dialog_helpers.cpp:639 声明 const BoneRecord* p=bones，642 行却 p+=604；656/659 行 ankle 查找对 q 重复错误。实际每次跳过 604 个 BoneRecord。首骨非“左足”且骨数>1 时，下一次 memcmp 就可能越界；32/64 位都有问题。

应使用索引循环或 ++p/++q，并前置空数组边界。这是部分类型化遗留的确定错误。

### A4 中高：同一缓冲混用分配释放家族

- src/window/frame_line_edit.cpp:84/88 对 undo.bonePose 用 free/malloc。
- src/features/audio_pose_helpers.cpp:429/433 对同一字段用 free/operator new。
- src/app/oni_skeleton_pump.cpp:355/359 用 delete[]/new[]。
- src/model/model_dispose.cpp:161..166 最终用 free 释放 auxiliaryPose/bonePose。
- src/window/ui_init.cpp:57 的 NewZeroed 用 operator new；src/app/scene_ownership.cpp:56 起用 free 回收全局轨道。

MSVC CRT 目前可能共享底层堆，但 C++ 分配/释放配对仍不合法；ASan、替换分配器或增加析构会暴露。需统一所有权/deleter，沿调用链迁移。

### A5 低：WinMain 显式行为例外

src/main/winmain.cpp:40 用 nothrow new，空返回则 exit 0；文件明确注明原版 throwing new。OOM 路径不同，若保留应列兼容性例外。

## 待验证（不是已确认的原版差异）

1. src/mmdxshow/source_base.cpp:1888/1994 返回 E_NOTIMPL(0x80004001)，注释却说原始返回 0x8000FFFF(E_UNEXPECTED)。需打开原版 MMDxShow.dll 核对，不能仅凭注释改，也不能把所有 E_NOTIMPL 当缺失实现。
2. src/app/subsystem_init.cpp:62 和 src/window/ui_refresh.cpp:152 跨线程用普通 uint32_t stopFlag，仅等待端转 volatile，是标准 C++ 数据竞争风险；_beginthread 失败还可能永久等待 stopFlag==2，未做故障注入。
3. src/window/ui_mousemove.cpp:591 内层移动 acc 后外层再加 200，疑似行漂移，须核对原版循环及坐标定义。
4. TeardownDShowGraph 的无限等待、失败 HRESULT 后 FreeEventParams、消息泵吞 WM_QUIT 需要导出失败/中断差分。有些怪异行为可能就是原版，不能凭常识改。
5. 多处 TODO/stub 注释已过时，函数实际上已实现。accessory_paste 已用 AccessoryKey，scene_ownership 已用 kModelSlotCount。关键词命中不等于缺失功能。

## 源码恢复建议

Win32/COM 边界上的 HWND、IUnknown*、GetProcAddress 函数指针是正常 C++；问题是业务对象当 byte blob、固定偏移解引用、模拟编译器析构 thunk。

按职责恢复 App、TimelineTrack<T>、Scene、SkeletonTrackingState、RecorderGraph、WavePlayer。保留有证据的算法、舍入和消息顺序，用 typed track 消除业务处 0x3C/0x14 和字段 +8/+12。原版地址保留在审计映射文档。

无法唯一逆推出作者原始标识符/注释/抽象，目标应为类型正确、可维护源码和可验证行为等价。

## 逐文件覆盖

Focused=重点函数/调用链/所有权或 IDA 核验；Scan=全文词法和函数清单扫描，未逐分支证明；Declaration=声明/台账/数据表。所有项仍需动态验证，无“已认证 1:1”项。转换数量不代表每次转换有错。

| 文件 | 行数 | 级别 | 活跃代码 reinterpret_cast 数 |
|---|---:|---|---:|
| `src/app/app_ctor.cpp` | 25 | Scan | 0 |
| `src/app/app_defaults.cpp` | 245 | Scan | 0 |
| `src/app/app_utilities.cpp` | 849 | Scan | 18 |
| `src/app/avi_record_start.cpp` | 444 | Scan | 2 |
| `src/app/dshow_record_graph.cpp` | 821 | Scan | 30 |
| `src/app/dshow_record_graph_jp.inc` | 135 | Declaration | 0 |
| `src/app/frame_driver.cpp` | 714 | Scan | 2 |
| `src/app/frame_modes.cpp` | 834 | Focused | 6 |
| `src/app/frame_modes_bone.cpp` | 619 | Scan | 0 |
| `src/app/frame_state_dump.hpp` | 553 | Declaration | 18 |
| `src/app/globals.cpp` | 29 | Scan | 0 |
| `src/app/key_ladder.cpp` | 375 | Scan | 0 |
| `src/app/late_ports.cpp` | 361 | Scan | 0 |
| `src/app/mmd_app.cpp` | 10 | Focused | 0 |
| `src/app/oni_kinect.cpp` | 274 | Scan | 4 |
| `src/app/oni_skeleton_pump.cpp` | 599 | Focused | 6 |
| `src/app/playback_catchup.cpp` | 546 | Scan | 5 |
| `src/app/playback_state.cpp` | 461 | Scan | 2 |
| `src/app/pump_edit_keys.cpp` | 496 | Scan | 0 |
| `src/app/pump_navigation.cpp` | 531 | Scan | 0 |
| `src/app/record_readback.cpp` | 315 | Scan | 3 |
| `src/app/reset_app_state.cpp` | 525 | Scan | 25 |
| `src/app/scene_ownership.cpp` | 89 | Focused | 0 |
| `src/app/shutdown_cleanup.cpp` | 603 | Focused | 26 |
| `src/app/subsystem_init.cpp` | 266 | Focused | 0 |
| `src/app/timeline_advance.cpp` | 642 | Scan | 2 |
| `src/main/charset_conv.cpp` | 81 | Scan | 2 |
| `src/main/winmain.cpp` | 156 | Focused | 0 |
| `src/window/accessory_paste.cpp` | 278 | Focused | 3 |
| `src/window/command_dispatch.cpp` | 614 | Scan | 5 |
| `src/window/command_file_menu.cpp` | 2492 | Scan | 89 |
| `src/window/command_frame_edit.cpp` | 3168 | Scan | 23 |
| `src/window/command_frame_register.cpp` | 905 | Scan | 1 |
| `src/window/command_impl.cpp` | 634 | Scan | 3 |
| `src/window/command_panel_toggles.cpp` | 1177 | Scan | 27 |
| `src/window/command_view_menu.cpp` | 2899 | Scan | 23 |
| `src/window/dialog_procs.cpp` | 166 | Scan | 5 |
| `src/window/dialog_scaffold.hpp` | 76 | Declaration | 1 |
| `src/window/dialog_select_ops.cpp` | 642 | Scan | 9 |
| `src/window/enhance_model_io.cpp` | 936 | Scan | 9 |
| `src/window/frame_line_edit.cpp` | 596 | Focused | 0 |
| `src/window/frame_range_apply.cpp` | 360 | Scan | 5 |
| `src/window/localize_ui.cpp` | 240 | Scan | 1 |
| `src/window/mic_window.cpp` | 221 | Scan | 3 |
| `src/window/misc_dialogs.cpp` | 918 | Scan | 5 |
| `src/window/model_edge_dialog.cpp` | 479 | Scan | 15 |
| `src/window/path_utils.cpp` | 52 | Scan | 0 |
| `src/window/physics_model_dialog.cpp` | 1580 | Scan | 21 |
| `src/window/ui_control_text.cpp` | 82 | Scan | 0 |
| `src/window/ui_controls.inc` | 183 | Declaration | 0 |
| `src/window/ui_ctlcolor.cpp` | 172 | Scan | 2 |
| `src/window/ui_dropfiles.cpp` | 335 | Scan | 0 |
| `src/window/ui_edit_commit.cpp` | 566 | Scan | 14 |
| `src/window/ui_editor_click.cpp` | 1231 | Scan | 20 |
| `src/window/ui_font.cpp` | 77 | Scan | 1 |
| `src/window/ui_frame_refresh.cpp` | 362 | Scan | 17 |
| `src/window/ui_frame_step.cpp` | 225 | Scan | 1 |
| `src/window/ui_hscroll.cpp` | 355 | Scan | 3 |
| `src/window/ui_init.cpp` | 745 | Focused | 29 |
| `src/window/ui_model_reload.cpp` | 606 | Scan | 18 |
| `src/window/ui_mouse_misc.cpp` | 383 | Scan | 1 |
| `src/window/ui_mousemove.cpp` | 721 | Focused | 10 |
| `src/window/ui_notify.cpp` | 228 | Scan | 1 |
| `src/window/ui_palette.cpp` | 422 | Scan | 3 |
| `src/window/ui_panel_paint.cpp` | 661 | Focused | 7 |
| `src/window/ui_panel_sweep.cpp` | 478 | Scan | 3 |
| `src/window/ui_refresh.cpp` | 185 | Focused | 2 |
| `src/window/ui_scroll_mouse.cpp` | 237 | Scan | 7 |
| `src/window/ui_selection_reeval.cpp` | 271 | Scan | 1 |
| `src/window/ui_timeline_gfx.cpp` | 184 | Scan | 2 |
| `src/window/ui_view_refresh.cpp` | 282 | Scan | 9 |
| `src/window/ui_viewport_layout.cpp` | 101 | Scan | 0 |
| `src/window/ui_viewport_refresh.cpp` | 270 | Scan | 3 |
| `src/window/ui_windowsize.cpp` | 357 | Scan | 0 |
| `src/window/window_init.cpp` | 458 | Scan | 0 |
| `src/window/wm_paint.cpp` | 425 | Scan | 1 |
| `src/window/wm_paint_labels.inc` | 58 | Declaration | 0 |
| `src/window/wndproc.cpp` | 260 | Scan | 6 |
| `src/window/wndproc_aux_windows.cpp` | 286 | Scan | 1 |
| `src/media/media_load.cpp` | 410 | Scan | 0 |
| `src/media/wave_audio.cpp` | 558 | Focused | 5 |
| `src/features/audio_pose_helpers.cpp` | 1120 | Focused | 4 |
| `src/features/dialog_helpers.cpp` | 955 | Focused | 5 |
| `src/features/subsystem_ledger.cpp` | 118 | Declaration | 0 |
| `src/mmdxshow/crt_ledger.cpp` | 305 | Declaration | 0 |
| `src/mmdxshow/dll_main.cpp` | 593 | Scan | 5 |
| `src/mmdxshow/enumerators.cpp` | 411 | Scan | 0 |
| `src/mmdxshow/guids.cpp` | 26 | Scan | 0 |
| `src/mmdxshow/MMDxShow.def` | 7 | Declaration | 0 |
| `src/mmdxshow/mmdxshow.hpp` | 1018 | Declaration | 0 |
| `src/mmdxshow/push_pin.cpp` | 712 | Focused | 2 |
| `src/mmdxshow/push_source.cpp` | 358 | Scan | 13 |
| `src/mmdxshow/source_base.cpp` | 1995 | Focused | 5 |
