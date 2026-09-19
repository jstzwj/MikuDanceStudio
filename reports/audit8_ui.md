# 第八轮差分审计 · 子审计报告：UI / 窗口 / 应用框架 / 命令分发

- 日期：2026-09-15（第 8 轮 UI 范围专项子审计）
- 基线：工作树未提交状态（含 MMEffect 内置集成与第八轮修复波）
- 参照：MikuMikuDanceE_v932x64.exe（IDA database `e02eac62`，imagebase 0x7FF7CB420000）；
  MMEffect_v037x64_English.dll（database `1c790e4b`，imagebase 0x180000000）
- 结论速览：本范围主体高度忠实。全量核对了拖放路由、消息泵、wndproc 消息开关、
  app 默认值、启动命令行分派、以及本轮工作树内全部 UI 改动；新发现 **1 项 P2、
  2 项 P3**，另有 1 项审计前提澄清（MME 拖放路由）与 1 项已达标的验证记录。

## 审计方法与覆盖面

- `ui_dropfiles.cpp` 全文 vs x64 `sub_7FF7CB4B78B0`（HandleDropFiles）逐分支对照。
- `wndproc.cpp` vs x64 `sub_7FF7CB4FBBF0`（MainWndProc）逐消息对照；`winmain.cpp`
  vs x64 `WinMain`（含 0x7FF7CB4FB4B0 处 64 位减法与 sleep 量化的反汇编核对）。
- `app_defaults.cpp` vs x64 `sub_7FF7CB42C550`（InitDefaults）数值逐项对照（经
  layout_pins x64 偏移表映射字段）。
- `window_init.cpp` 启动文件分派 vs x64 `sub_7FF7CB42CB50` 0x7FF7CB42ED20 起反汇编。
- 工作树改动（misc_dialogs/ui_scroll_mouse/ui_timeline_gfx/command_file_menu/
  command_view_menu/command_frame_edit/command_panel_toggles/frame_line_edit/
  frame_range_apply/ui_hscroll）vs 对应 x64 双子逐一复核。
- MMEffect.dll：`MME_Initialize`（0x1800564A0）、主窗口子类 `sub_180055B10`、
  分配对话框 proc `sub_180044080` 的 WM_DROPFILES 段（0x180045686..0x180045B66）。
- 其余范围文件（dialog_procs、dialog_select_ops、model_edge_dialog、
  physics_model_dialog、ui_editor_click、ui_edit_commit、ui_selection_reeval、
  ui_mousemove、ui_palette、ui_panel_paint、refresh 族、mic_window、oni_kinect、
  accessory_paste、app 其余文件）做抽样与结构核对，未见新的行为偏差。

---

## 发现

### [P2] Mic 窗口 WM_MOUSEWHEEL 仍用 x86 双精度链，与本轮已修的主窗口滚轮不一致
- 我们：`src/window/mic_window.cpp:130-137`
  ```cpp
  case WM_MOUSEWHEEL:
      // x87 double math then float store: angle += wheel * 0.05.
      s.CameraDistance() = static_cast<float>(
          static_cast<double>(static_cast<short>(HIWORD(wParam))) *
              0.05000000074505806 +
          static_cast<double>(s.CameraDistance()));
  ```
- 原版：MMEffect 会话无关；MMD x64 `MicWndProc`（`sub_7FF7CB4C48A0`）case 0x20A，
  0x7FF7CB4C4B1A：
  ```c
  *(float *)(app + 661760) = (float)((float)SWORD1(wParam) * 0.050000001)
                           + *(float *)(app + 661760);
  ```
  纯 SSE 单精度：`(float)wheel * 0.05f + field`，全程 float。
- 差异与影响：主窗口 `HandleMouseWheel`（ui_scroll_mouse.cpp）在本轮修复波已按
  x64 改为单精度链，但分离（Mic）窗口的同名分支漏改，仍是 x86 x87 的
  double 中转 + 一次 float 舍入。两条路径对同一 CameraDistance 字段的更新精度
  不同；典型值下两者一致，但特定值（视距接近 2^n 量级边界）会出现 1 ULP 差异，
  且长会话滚轮累积时两种舍入的漂移轨迹不同。
- 建议：改为
  `s.CameraDistance() = static_cast<float>(wheel) * 0.05f + s.CameraDistance();`
  与 ui_scroll_mouse.cpp:187/221 同形（`static_cast<std::int16_t>(HIWORD(wParam))`
  先转 int 再转 float）。

### [P3] RegisterBoneUndoSnapshot 释放用了 std::free，原版是 operator delete[]/delete
- 我们：`src/window/misc_dialogs.cpp`（工作树新增的 `RegisterBoneUndoSnapshot`，
  约 486-494 / 530-536 行）：
  ```cpp
  if (undo.bonePose != nullptr) { std::free(undo.bonePose); ... }
  ...
  if (undo.auxiliaryPose != nullptr) { std::free(undo.auxiliaryPose); ... }
  ```
  而分配两处均为 `::operator new(...)`。
- 原版：x64 `sub_7FF7CB4EEBD0`：bonePose 旧指针走 `operator delete[]`
  （`??_V@YAXPEAX@Z` @0x7FF7CB4EECC8），auxiliaryPose 旧指针走
  `operator delete`（`??3@YAXPEAX@Z` @0x7FF7CB4EEE62）。
- 差异与影响：MSVC 下 new 底层即 malloc、POD 数组无 cookie，行为等价；但
  配对不严格（operator new ↔ free 属技术性 UB），且与原版释放形式不一致。
  同文件/undo 家族其它分配点应一并核对配对。
- 建议：bonePose 改 `::operator delete[](p)`、auxiliaryPose 改 `::operator delete(p)`
  （分配已是 operator new，仅需改释放侧两处）。

### [P3] 角度"度→弧度"换算的除法精度与 x64 差一个舍入路径
- 我们：`src/window/misc_dialogs.cpp:394-396`（相机 apply）与新增骨骼 apply 的
  `add[3..5]`：
  ```cpp
  add[3] = static_cast<float>(add[3] * -3.141592) / 180.0f;   // 单精度除法
  ```
- 原版：x64 `sub_7FF7CB4BC330` 0x7FF7CB4BC555 / `sub_7FF7CB4BC9C0` 0x7FF7CB4BCBE2：
  ```c
  v19 = (float)(v18 * -3.141592) / 180.0;   // 乘法 double→float 后，除法在 double 域，赋值再回 float
  ```
- 差异与影响：原版是 `fl32(fl64(fl32(x*π)) / 180.0)`，我们是
  `fl32(fl32(x*π) / 180.0f)`。两个双舍入路径在边界值可差 1 ULP，随后进入
  角度 scale/add 链。仅极限保真度问题，无可见行为差异。
- 建议：除法写成 `/ 180.0`（double 字面量）再依赖赋值转换，或显式
  `static_cast<float>(static_cast<float>(x * 3.141592) / 180.0)`。

### [P2-澄清] 审计前提".x 在 MME 激活时归 MME、否则归 accessory"不成立——原版无此路由，我们的现状正确
- 原版证据链（MMEffect.dll，database `1c790e4b`）：
  - `MME_Initialize`（0x1800564A0）在 0x18005687A 对 MMD 主窗口安装的子类 proc 是
    `sub_180055B10`，不是 `sub_180044080`。
  - `sub_180055B10` 只拦 WM_COMMAND（MME 菜单 40001/0x9C41-0x9C46/0x9C56 与宿主
    0x104）和鼠标位置换算（SAS mouse），**其余消息全部 CallWindowProcA 透传**，
    WM_DROPFILES 原样到达 MMD 的 HandleDropFiles——.x/.vac 始终按 accessory 加载。
  - 真正消费拖放的是 MME 自己的分配对话框 proc `sub_180044080`（由菜单 0x9C45 经
    CreateDialogParamA 模板 0x6C 创建，自己调 DragAcceptFiles）：多文件弹
    "Two or more files cannot be specified."；单文件按扩展名尾串 `_stricmp`
    .emm（0x18004579A）/ .fx（0x180045824）/ .fxm（0x180045919）/ .fxsub
    （0x180045A0E）分派，其余弹 "Please specify .fx file or .emm file."。
- 我们：`third_party/mmeffect/src/mmeffect/mme_dlg.cpp:1578-1620` 的对话框级
  WM_DROPFILES 已 1:1 复刻上述行为；主窗口 `ui_dropfiles.cpp` 不含任何 MME 分支。
- 差异与影响：无差异。记录此结论是为了防止后续轮次依据错误前提"补出"主窗口
  MME 拖放拦截（那反而会引入偏差：原版多文件拖 .pmd 到主窗口是批量加载，
  只有拖到 MME 对话框上才报错）。
- 建议：无需改动；建议将该结论沉淀到 ui_dropfiles.cpp 头注。

### [验证记录] 本轮工作树 UI 改动复核全部与 x64 一致（无新问题）
逐项反汇编/反编译复核通过，列出以供归档：
1. **骨骼帧控制 apply**（`ApplyBoneFrameScaleAdd`，x64 `sub_7FF7CB4BC9C0`）：
   12 编辑 686-697、X 正 π/Y·Z 负 π、旋转门条件、万向节补丁
   （`1e-6 > |cosf(x)|`，±π 取值与 `_12`/`_31` 符号判定 0x7FF7CB4BCF3B-F84）、
   Rz*Rx*Ry 重建序、600000 容量与 +0x38 gate、尾序列（PanelPaint/CurvePanelRepaint/
   SeekModelFrame(currentFrame, physicsMode)/dirty/PostViewRefresh）全部一致。
2. **骨骼 undo 快照注册**（x64 `sub_7FF7CB4EEBD0`）：键计数、400/401 按钮态、
   环游标 30 模、WORD {undoDirty=1, redoDirty=0}、槽 {op=2, count=0, frame}、
   36B 骨姿快照（bone+328 位移、+340 四元数、physics 字节）、64B 辅助记录
   （keyVisitMap 去重、count 复用 dirty）——除上述 P3 释放形式外一致。
3. **相机帧控制 apply 重写**（x64 `sub_7FF7CB4BC330`）：10000 容量、+0x48 选择
   gate、target.x 负 π、**fov int 字段全 float 链**（cvtsi2ss/mulss/addss/
   cvttss2si）与尾序列（ReloadModels=x64 0x7FF7CB479C30）一致。
4. **GroundShadowColorEditSubclassProc 实体化**（x64 `sub_1800576D30…
   sub_7FF7CB476D30`）：VK_RETURN 守卫（0x272==GetDlgItem(dlg,626)）、
   GetWindowTextA(…,8)、atof→4 个同值 float（app+662920..662932）、
   TBM_SETPOS 到不存在控件 458 的原版怪癖——一致。
5. **HandleMouseWheel 相机行切换**（x64 `sub_7FF7CB45F300`）：两分支 DWORD 写
   660344（置相机行、清 light/shadow/gravity）+ 255 槽 rowSelected(+0x4BC) 清零 +
   PostLanguageSweep；已有相机行时只 PostViewRefresh；**无 RefreshRequest**——一致。
6. **SetFrameNormalized 槽位勘误**（x64 0x7FF7CB4FB2DC `call [rax+0x78]`）：
   slot 15 = SetFrequency（原版错槽无效操作），DWORD 位语义——一致。
7. **帧范围缩放单精度就近舍入**（x64 `sub_7FF7CB4BD470`）：cvtsi2ss→mulss→
   cvttss2si——一致（scale=1.05/diff=20 差一帧的场景已对齐）。
8. **case 400 起始帧等于当前帧才置 changed**（x64 0x7FF7CB46B6DA cmp/jnz）——一致。
9. **case 494 空槽守卫**：x64 无守卫（会走空记录），我们加了防御性早退+保留
   radio/扫尾——属有意的加固偏差（原版该路径崩溃），维持现状可接受。
10. **SelectFrameGroup/CopyBoneKeyPayload 字节化写**（x64 0x7FF7CB465460/540/4D0、
    `sub_7FF7CB4AA110`）：只碰 +0x38/+0x18/+0x10 标志字节，不再扫掉
    physicsDisabled/padding——一致。
11. **case 285 地面阴影 btRigidBody +0xE0**（x64 Bullet 指针加宽后成员位）——一致。
12. **虚调用槽位修正**：SetRenderTarget（槽 37）、SetRenderState(0xA1)（槽 57）、
    SetTransform(D3DTS_PROJECTION)（槽 44）——均与 x64 vtable+8*n 偏移一致。
13. **结构性核对通过**：WinMain 泵（64 位 tick 减法 0x7FF7CB4FB4B0 `sub rax,rdi`、
    sleep 量化 cvttss2si r64、`(recwin==0 && sleep>0)` 合取序）、MainWndProc 全
    消息开关（WM_TIMER 100/101 边界、WM_CLOSE 双语与 0x40001、WM_DESTROY 的
    mmconfig.ini 34 行写出序）、InitDefaults 数值（-45/10/60/9.8/-1/0.01125/
    physicsMode=2/aviStereoWidth=2/kinectMirror=1/track[0]=1…）、HandleDropFiles
    分派序与针表（含 `.vac` 第三针无点、.pmm 取消提前 return 不 DragFinish 的
    原版怪癖、.vsq→0x435FE0）、启动命令行分派（两针变体、引号剥离、
    L"%s%s" 空参原版怪癖 0x7FF7CB54A3BC/3C0）、window_init 三窗口类/JP 标题/
    主题色表/ini 解析链、DragAcceptFiles 仅在 CreateUIControls（x64
    0x7FF7CB431DA5）、oni_kinect DLL 探测/版本门/菜单恢复、mic_window
    创建/销毁几何往返、accessory_paste 插值与溢出语义——均一致。

---

## 统计
- 新发现：P2 × 1（Mic 滚轮精度）、P3 × 2（释放配对、弧度换算舍入）
- 审计前提澄清：1（MME 拖放路由——现状正确，勿改）
- 复核通过的工作树改动/结构项：13 大项 + 若干子项
- 建议修复优先级：P2 一处小改；P3 两处随手改
