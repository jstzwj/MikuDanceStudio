# 第八轮差分审计 —— 运行时模型驱动（蒙皮/变形/物理/时间轴/播放）

- 日期：2026-09-15。审计对象：工作树当前内容（未提交改动为准）。
- 参照：MikuMikuDanceE_v932x64 主程序，IDA MCP 会话 `e02eac62`（实际加载基址
  0x7FF7CB420000；本报告地址均为该基址下的运行时地址。注：源码注释里的
  "0x140xxxxxx" 系列来自早前一次基于 0x140020000 的会话，与本会话地址存在
  固定 -0x20000 的 RVA 偏差，本轮已逐一对齐核对，不影响结论）。
- 范围：src/model/{model_skinning, morph_apply, model_frame_seek,
  model_keyframe_advance, model_keyframe_edit, track_apply, bone_transform,
  bone_sort, bone_edit_undo, key_registrars, model_query_helpers}.cpp、
  src/app/{timeline_advance, playback_catchup, playback_state, frame_driver,
  frame_modes, frame_modes_bone, key_ladder}.cpp、
  src/physics/{physics_create, physics_frame, scene_create}.cpp。

## 结论概览

经过前 7 轮修复，本轮在运行时驱动主链路（物理步进/复位/读回、morph 叠加、
时间轴推进、播放启停、关键帧登记）上**未发现 P0/P1 级行为偏差**。新发现
2 条 P2（均为与 x64 基准的浮点精度链差异，其中 1 条可影响重力迭代数整数
截断的边界值）与 1 条 P2 级文档失实；另有 2 条 P3 备注。以下先列发现，再
附本轮逐段核对通过的清单（含地址锚点，供后续轮次免重查）。

---

## 发现

### [P2] PmdEdgeDistance 全链双精度，x64 原版为单精度（PMD/PMX 两条路径共用）

- 我们：`src/model/model_skinning.cpp:587-612`
  ```cpp
  const double dx = static_cast<double>(worldPoint[0] - camera[0]); ...
  const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  return static_cast<float>(distance * 0.00004500000068219379 *
      (static_cast<double>(app->CameraFov()) * 0.6000000238418579 + 1.0) *
      static_cast<double>(record.edgeScale));
  ```
  距离平方和、sqrt、三段乘积全部在 double 域，仅最终一次舍入回 float。
  该函数同时喂给 PMD（`SkinPmd`，:837）与 PMX（`UpdatePmxModelVertexBuffers`，:769）
  两条边框缩放路径。
- 原版：`sub_7FF7CB4E13C0` 内 0x7FF7CB4E29D3..0x7FF7CB4E2A4E（本会话实测；
  即源码注释里的 "x64 0x1400E13C0" 函数）：
  ```asm
  mulss xmm3, xmm3          ; 分量平方（单精度）
  addss xmm3, xmm2 / xmm5   ; 平方和累加（单精度）
  sqrtss（0x7FF7CB4E2A1B..区间）
  mulss xmm9, cs:[0x7FF7CB552C38]   ; fov * 0.6f
  addss xmm9, cs:X          ; +1.0f
  mulss xmm0, cs:[0x7FF7CB552C34]   ; dist * 4.5e-05f（0x383CBE62）
  mulss xmm0, xmm9          ; (dist*k) * (fov*0.6+1)
  mulss xmm0, [rbx+355Ch]   ; * edgeScale
  ```
  常量在 .rdata 仅存在 float 镜像（0x7FF7CB552C34 = 4.5e-05f、
  0x7FF7CB552C38 = 0.6f；全镜像中不存在对应 double 位型
  `01 00 00 40 CC 97 07 3F` / `00 00 00 40 33 33 E3 3F`，已用 find_bytes
  全段扫描证实）。
- 差异与影响：乘法次序两端一致（(dist*k)*(fov*0.6+1)*edgeScale），但原版
  每步都舍入到 float、sqrt 为 sqrtss；端口为 double 链一次舍入。每帧边框
  缩放存在 1-2 ULP 漂移，与端口自己在 bone_transform/physics_frame 等
  处严格执行的 "x64 单精度基准" 方针不一致（文件头亦声明 PMX 算术以 x64
  SSE 次序为行为基准）。
- 建议：按 x64 改为全 float 链（`float dx=...; float d2=(dx*dx+dy*dy)+dz*dz;
  float dist=std::sqrt(d2); return ((dist*4.5e-05f)*(fov*0.6f+1.0f))*edgeScale;`），
  保留乘法次序；若坚持双架构，可仿 keyframe_advance 的 `#if` 分叉。

### [P2] timeline_advance 四条全局轨道插值使用 double，x64 原版全程单精度；重力噪声迭代数为整数输出，存在边界值可差 1

- 我们：`src/app/timeline_advance.cpp`
  - 相机 6 通道 eased lerp（:365-399）：`t`、`eased*(curV-prevV)+prevV` 全
    double（`static_cast<float>((double)e * (curV - prevV) + prevV)`）；
  - 灯光（:450-464）、重力（:542-558）方向/颜色/强度 lerp 全 double；
  - **重力噪声迭代数**（:546-548）：
    ```cpp
    s.GravityNoise() = previous.noise +
        static_cast<int>(static_cast<double>(current.noise - previous.noise) * t);
    ```
- 原版：`sub_7FF7CB489A20`（PlaybackPoseAdvance x64）：
  - 相机（0x7FF7CB489D4D..0x7FF7CB489EC8）：`v19=(float)(frame-prev)/(cur-prev)`
    为 divss 单精度，eased 分数（sub_7FF7CB47A8B0 返回 float）与 delta 的
    乘加全部 mulss/addss；
  - 灯光（0x7FF7CB48A14D..0x7FF7CB48A227）、重力（0x7FF7CB48A4C9..0x7FF7CB48A560）
    同为单精度；
  - 噪声迭代数（0x7FF7CB48A4DE）：
    ```c
    noise = prev + (int)( (float)(cur - prev) * v45 );   // float 乘积后 cvttss2si
    ```
- 差异与影响：相机/灯光/重力通道 ULP 级漂移（视觉不可辨，但违背本项目
  x64 位级基准）；更重要的是噪声迭代数是**整数**输出——double 乘积与
  float 乘积恰落在整数两侧时 (int) 截断可差 1，直接改变 Bullet 求解器迭代
  次数，属可观测行为差异（仅边界值触发，故仍定 P2）。另注意同仓库
  `src/model/track_apply.cpp:129-149`（ApplyGravityTrack，seek/对话框路径）
  已是 float 写法，两处不一致。
- 建议：timeline_advance 的 4A/4B/4D 三段按 x64 改为 float 运算（或至少
  统一为与 track_apply 相同的 float 形态）；相机 eased lerp 的分数与乘加
  逐项改 mulss 语义。

### [P2] physics_frame.cpp 头注释"catch-up 运动学同步为逆槽序"失实（代码与二进制均为正序）

- 我们：`src/physics/physics_frame.cpp:10-13` 注释
  “kinematic sync 0x4B22F0 per model (reverse slot order)”；
  实际代码在 `src/app/playback_catchup.cpp:167-185`（KinematicSyncPass），
  BLOCK 1/BLOCK 2 均以 `reverse=false`（0→254 正序）调用（:381、:492）。
- 原版：0x7FF7CB44BB16..0x7FF7CB44BB38（BLOCK 1 的 4B22F0 同步循环）：
  ```asm
  lea rbx, [r12+0BE8h]   ; rbx = &modelSlots[0]
  mov edi, 0FFh          ; 计数 255（倒计数）
  loop: cmp [rbx], r15 / mov rcx,[rbx] / call sub_7FF7CB4E31D0
        add rbx, 8 ; dec rdi ; jnz loop
  ```
  指针前向步进 + 计数器倒计数 = **正槽序**遍历（与端口一致）。
- 差异与影响：纯文档错误，无行为差异；但会误导后续审计/移植者反向"修"
  代码。
- 建议：订正 physics_frame.cpp:11-13 注释为 forward slot order（x64 0x7FF7CB44BB16）。

### [P3] PmxSkinMatrix 增加 boneIndex 上界检查（port 侧防护，已声明）

- 我们：`src/model/model_skinning.cpp:169-174` —— `boneIndex >= 0 && boneIndex < boneCount`
  双条件，越界返回全零矩阵；注释自认 "The upper bound is a port-side guard"。
- 原版：x64 PMX workers（sub_7FF7CB53F3A0/540D20/542720/544150/545BF0 一族）
  仅对引用骨骼索引做**符号测试**，负索引贡献全零矩阵，无上界检查。
- 差异与影响：仅对损坏文件（索引越界）行为不同；加载器产物恒在表内。
- 建议：维持现状（防护性），但可考虑在注释中补 x64 地址锚点以便复核。

### [P3] TimelineAdvance 窗口尺寸检查变量命名对调（纯可读性）

- 我们：`src/app/frame_driver.cpp:337-338`
  ```cpp
  const bool tooWide = r->screenHeight > winH;   // 实为“过高”
  const bool tooHigh = r->screenWidth > winW;    // 实为“过宽”
  ```
- 原版：0x4603B2..0x460410 比较语义与端口一致（行为无误）。
- 建议：交换两个变量名或改名 `tooTall`/`tooWide`，避免误读。

---

## 本轮逐段核对一致的路径（附地址，供后续轮次免重查）

以下各项均在 x64 数据库中以反编译/反汇编逐段比对通过：

1. **物理步进参数**：catch-up 与 settle 的 `stepSimulation(timeStep, 10, 1/60)`
   —— maxSubSteps=10 由 0x7FF7CB44BB41 `lea r8d,[rdi+0Ah]`（rdi 循环归零后）
   实锤；vtable 槽 +0x38。settle 主步 timeStep=var_14C8 余量、moved&&count==3
   双步、settle 3×(reseat+step) 均与端口 physics_frame.cpp 一致（前轮锚点复核）。
2. **BLOCK 1/2 catch-up 结构**（0x7FF7CB44BA60..0x7FF7CB44BE4A）：每子步
   PlaybackPoseAdvance(1)→有序 morph+SetPhysicsMode（comboSelIndex2 排序，
   BLOCK 2 选中模型跳过）→正序运动学同步→step→dt 预算扣 1/60→cursor+1/60
   （addss 0x3C888889）。端口 playback_catchup.cpp 逐项吻合。
3. **NotifyBonePhysicsMode**（sub_7FF7CB4E3140）：跳过 rec+94、按 rec+40 骨骼
   匹配；mode!=0 → body+224 |= 2 且 rec+92=0；mode==0 → &=~2 且
   rec+92 = rec+93?2:1；**无任何 activation 调用**（端口早前撤除
   DISABLE_DEACTIVATION 的决定正确）。
4. **ModelKinematicSync**（sub_7FF7CB4E31D0）：rec+92==0 门控；跟随矩阵
   Rz*Rx*Ry*T(recPos)*T(bonePos)*boneMat（Rz 外层、平移顺序均一致）；
   motion state 取 body+536 槽 vtable+16 setWorldTransform。
5. **ModelDynamicReseat**（sub_7FF7CB4E45D0）：(signed)rec+92>0 门控；
   boneIndex<0 时用 centerBone（a1+615548）；写集=当前 worldTransform
   （body+0x10..0x4F）+ 四个 16 字节速度槽清零（+336/+352/+144/+160），
   **不**改写插值位姿、不 clearForces、不激活——端口 ReseatRigidBody 语义精确。
6. **ModelPhysicsReadback**（sub_7FF7CB4E3470）：
   - 关节过拉伸：`sqrt((x²+y²)+z²) - limit >= 2.0f`（0x7FF7CB4E36A8，
     限位 joint+148），bodyB 传送=B 原旋转+A 新平移，仅清当前两速度槽
     （0x7FF7CB4E381F/+384C，插值速度槽不动）——与端口 part1/StopBodyAt 一致；
   - part2：boneWorld(+60) = rec 逆矩阵(+128) * bodyWorld（0x7FF7CB4E3962）；
   - part3：parent<0 时 world→local 直拷（0x7FF7CB4E3FCA）；局部=world*inv(parent)；
     姿态 quat=QuatFromMatrix(local)；T(p)*local*T(-p) 取平移写 +400..408；
     type5 修正 conj(tb.quat)*q（0x7FF7CB4E3BEF）；0x100 旋转继承
     acos/截断π/轴长 eps 全套与端口逐常量一致（3.1400001f、1.1920929e-7f），
     且 parent>=0 用 rot*q、parent<0 用 q*rot（0x7FF7CB4E42F0）与端口分支
     完全对应；0x200 平移继承减法、mode==2 塌缩、+252(+244) 拷贝的
     执行/跳过条件（含逆矩阵奇异路径仍执行 +252 拷贝）均一致。
7. **SetPhysicsMode**（sub_7FF7CB4D9F70）：姿态源门
   `((mode==1||(mode>=2&&bone+501==0)) & bone+500)`；物理源=+400/+412 备份、
   运动学源=+328/+340；PMX 时骨 morph 工作记录加算（D3DXQuaternionMultiply
   q*=rec.quat）；层循环 `layer 0..maxBoneLayer`（a1+8892，含 >=0 门）。
8. **ModelApplyMorphs**（sub_7FF7CB4DD780）：
   - 工作记录复位（pos 0 / quat xyz 0 / w=1，0x7FF7CB4DD7C7..DD81E）；
   - 骨 morph group 路径权重为**两次连乘** `(angle*w)*refW`、`(t*w)*refW`
     （0x7FF7CB4DD9EA/DDA9D-DDAF2），direct 路径单乘（0x7FF7CB4DDC49/DDD08），
     与端口 AccumBoneEntry 的 (angle*w)*refW 逐位一致；
   - 材质 morph：加法 `add += delta*w`、乘法
     `mul = (1-(1-delta)*w)*mul`（0x7FF7CB4DF8C8）一致；group 路径
     预乘 `v71 = refW*w`（0x7FF7CB4DE087，乘法交换故与端口 w*ref.weight 位同值）；
   - **compose 尾部**（0x7FF7CB4E1039..0x7FF7CB4E13A3）：每材质恰好 16 通道
     回写 `mul*base+add`，端口 16 通道写回数量与布局吻合；
   - **impulse（type 9）/flip**：x64 运行时 morph 应用只分派 0/2/8（骨/材质）
     与 PMX 顶点路径的 0/1/3..7——不存在 impulse/flip 处理；端口同样不处理，
     行为一致（回应审计要点"impulse morph"）。
9. **PMX 顶点/UV morph 通道**（sub_7FF7CB4E13C0 前段）：restore 基表
   （morph0 聚合位置表 + 5 张 UV 族基表）→ 权重!=0 门 → direct 分派
   type1（+0/+4/+8 位置）与 type3..7（UV 族通道偏移 +24/+28、+32..+95
   按 component-major 布局）→ group 路径仅对目标 type==1 或 3..7 生效
   （**不做 group→group 递归**，两端一致），权重折叠 `(offset*value)*refW`
   （0x7FF7CB4E18F4）与端口 AccumPmxVertexMorph 左结合一致。
10. **PlaybackPoseAdvance**（sub_7FF7CB489A20）：
    - 帧值：cursor*30f 单精度 + *1000 0.5 进位尾（0x7FF7CB489A9B..ACF）；
    - 相机轨道：terminal/exact 原样拷贝；**相邻帧吸附** `curFrame-1 ==
      prevFrame → prev 原样拷贝**（0x7FF7CB489C9D）与端口一致；插值通道
      0/1/2=eye、3=三欧拉角共用、4=distance、5=fov（0x7FF7CB489D7F..EC8）；
      viewOffset 清零、投影重建 SetTransform(D3DTS_PROJECTION)；
    - 灯光轨道 editGate（0x2F8||0x9ED98）再查 active（0x7FF7CB489F71），
      播放期灯光轨道停靠——端口 gate 顺序一致；
    - 自影/重力轨道 terminal/exact/prev 语义一致；附件轨道 between 分支
      仅 +52/+56（scale/opacity）lerp、离散字段取 prev（0x7FF7CB48A847..A941）
      与端口一致；255 槽倒计数正序遍历。
11. **UpdateBoneFrames**（sub_7FF7CB489210）：saved=物理模式 → alwaysOnOff
    强制 2 → 255 槽 InitModelTrackCursors（子步 12）→
    PlaybackFrameChanged ? 清零 : PhysicsResetPending=1（0x7FF7CB489295..2A7）
    → editGate 下相机/灯光/自影/重力四轨 init（含 viewOffset 清零、终末键
    拷贝、投影/SetLight）→ 附件轨 init → EnableWindow/菜单禁用区间
    （400-401,409-410,415-468,471-487,490-501,504-527,536-550）→
    DisablePlaybackMenus + PostLanguageSweep2。全吻合。
12. **InitModelTrackCursors**（sub_7FF7CB4F0320）：IK 主轨 + morph 轨 + 骨轨
    各自的 root/停放变体；type==7（InertTip）跳过；运动学通知门
    `hasRigidBody && physicsMode>=2` 且模式字节无条件回写
    （0x7FF7CB4F07CA..0892，内联 notify 与 sub_7FF7CB4E3140 同形）——
    端口一致。
13. **帧驱动/物理帧门控**（pump sub_7FF7CB4474F0 相应区段）：messageSeen
    3→0 衰减门、selActive 上升沿 settle 闩锁（0xA137C）、gate B（附件编辑
    对话框）、settle 门内 readback+moved 清零、第二姿态遍历 afterPhysics=1
    ——与端口 physics_frame.cpp/frame_driver.cpp 的前轮结论复核无出入。
14. **常量抽查**：slerp dot 钳制 ±0.999999f（0x3F7FFFEF）、截断 π
    3.1400001f（0x4048F5C3）、FLT_EPSILON 1.1920929e-7、CCD 对齐阈值 1e-7f、
    过拉伸 2.0f、边框 0.005f（0x7FF7CB552C2C，PMD 收缩与 PMX 边宽共用，
    24 处 xref 与五个 stride worker 对应）、重力 -98.0f（0xC2C40000）、
    stepSimulation 1/60（0x3C888889）——均与源码一致。

## 数值/结构上未发现新偏差的高风险点（本轮复核通过）

- BDEF2/BDEF4/SDEF 的乘加次序与负索引全零矩阵策略（第 6 轮已并行化对齐）；
- SeekModelFrame/AdvanceModelKeyframes 的 slerp 权重形成方式差异
  （advance 乘 1/s、seek 除以 s）与 eased 旋转分数喂入；
- 关键帧登记三注册器的链表 splice、空闲槽扫描游标只增、maxFrame 的
  RAW frameOffset 回退 quirk、溢出上限（600000/20000/1000）与 EN/JP 文案；
- undo/redo 双环 30 深、type4 链式递归、pose/key 快照 0x40 记录布局；
- 删除标记键的 root 记录保形复位（display root 保留、morph/bone 非根摘链）。

## 统计

- P0：0；P1：0；P2：3（其中 2 条浮点精度链、1 条文档失实）；P3：2。
- 逐段核对一致路径：14 大项（见上）。
