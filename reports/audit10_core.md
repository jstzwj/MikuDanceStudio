# 第十轮审计：模型、文件 IO、物理和数学

日期：2026-09-19。范围为当前工作树 `src/model`、`src/io`、`src/physics`、`src/math` 及其布局头。业务源码未修改。本次新增报告和证据文件。

**结论：当前实现不是 MMD v9.32 x64 的严格一比一实现；即使不涉及 MME，也已有可复现的初始化和布局冲突。** 大量字段已有语义名称、物理部分已使用 Bullet API，但内存布局驱动的实现仍然占主导；不能把“有名字的 reinterpret_cast”当成恢复完成的源码。

## 证据和范围限制

- 本次独立读取原版 `MikuMikuDanceE_v932x64/MikuMikuDance.exe`，IDA session `436ba855`，image base `0x7FF7CB420000`。直接反编译并保存了初始化、初始化子函数、销毁函数；调用者 `0x7FF7CB4B66C0` 亦检查。
- 精查初始化、释放、PMD/PMX 头部与错误路径、相关布局。其余文件执行函数/分支/偏移/分配释放/可疑实现模式静态扫描和选段阅读，**不是全部函数逐指令等价证明**。下表明确区分精查与扫描。
- 独立编译当前 `model_init.cpp` 和实际头文件：`g++ -std=c++20 -D_M_X64 -fshort-wchar -I include reports/audit10_evidence/model_init_probe.cpp src/model/model_init.cpp -o <临时目录>/mmd-audit10-probe.exe`。使用 Cygwin g++，并非完整 MSVC/D3D 应用测试；头文件中的 x64 布局 static_assert 全部通过。
- [探针源码](audit10_evidence/model_init_probe.cpp)、[实测输出](audit10_evidence/model_init_probe_output.txt)、[原版初始化反编译](audit10_evidence/mmd_x64_model_init.c)、[原版子初始化](audit10_evidence/mmd_x64_morph_init.c)、[原版销毁](audit10_evidence/mmd_x64_model_dispose.c)。探针只观察非法指针，不执行故意崩溃的 free。
- 历史 `audit8_io.md` 的结论仅用作复核线索；本报告不会照抄为新证据。没有运行原版和移植版完整交互差分、长时间物理轨迹差分、全部文件格式往返。

## 确认缺陷

### C01 / P0：x86 四元数初始化覆盖 x64 两个拥有型指针

`src/model/model_init.cpp:113-120` 从 `m + 64` 写 17 个 `{0,0,0,1}`。当前 x64 `ModelRecord` 的 `materials` 位于 64、`boneKeyCursors` 位于 72、`morphKeyCursors` 位于 88、真正的 `localTransforms` 位于 120。此前置空指针的代码并不能保护它们，因为此循环在后。

实测：零内存调用 `ModelInitDefaults` 后，`boneKeyCursors` 和 `morphKeyCursors` 都成为 `0x3f80000000000000`；`materials` 仍是零，不能笼统声称所有头部指针都损坏。

可达失败链：`src/model/add_model.cpp:62-65` 初始化；`pmd_load.cpp:131-148` 文件打不开返回 false，或 `:164-179` 格式错误，或 `:183-190` 日文信息框取消；`add_model.cpp:155-158` 调 `ModelDispose`；`model_dispose.cpp:233-236` 无条件释放两个游标指针。由此可静态确定进入非法 free；本轮未启动完整 GUI 实际制造崩溃。成功加载有骨骼/表情的模型可通过后续重新分配遮蔽此问题，不代表初始化正确。

原版 `0x7FF7CB4C96B1/96B9` 将 +72/+88 置零，四元数初始化从 +120 开始（`0x7FF7CB4C98ED`），没有此覆盖。

建议：先恢复独立的具名四元数数组，消除硬编码 64；不能简单把整个 `localTransforms[67]` 当作 17 个四元数，因为 17×4=68，而当前声明只有 67 个 float，最后一个分量还落在 padding。

### C02 / P1：PMX 头部与 Kinect pose-trace 指针在 x64 完全重叠

`include/mikudancestudio/model_layout.hpp:67-75` 仍用 `uint32_t pmxVertexCount` 占原版实际为指针的槽；本次编译探针证实 `pmxTextEncoding` 实际偏移 8680。`include/mikudancestudio/model.hpp:259,286-287` 把 `PoseTraceBuffer` 也放在 8680 (`0x21E8`)。

`src/model/pmx_load.cpp:280-287` 正常读入八个头字段，正好覆盖这一完整指针。模拟合法头 `{encoding=0, addUV=0, vertexIndex=2, textureIndex=1, materialIndex=1, boneIndex=2, morphIndex=1, rigidIndex=1}` 后，探针读得 `PoseTraceBuffer=0x0101010201020000`。

消费者例子：`src/features/dialog_helpers.cpp:596-603` 开启 pose recording 时，如果该指针非空就先 free；这是有效 PMX 后正常功能操作可达的非法释放。`audio_pose_helpers.cpp:399-400` 亦有相同消费者。尚未通过真实 Kinect/GUI 路径执行。

原版初始化明确清 +8680 指针（`0x7FF7CB4C9AA3`），另在 +8689 清 additionalUV（`0x7FF7CB4C97E3`），说明真正头部位于 +8688，而不是 +8680。

建议：将 pose trace 指针和 PMX header 建模为真正成员，删除伪 `pmxVertexCount`；调整相邻 padding 时逐个守住后续真实锚点，不能只扩大成员让整个结构继续漂移。

### C03 / P1：初始化表与 -999 哨兵仍按 x86 地址写入

`model_init.cpp:140-149` 使用 `m+1056`，得到表起点 336；原版 x64 `0x7FF7CB4F9A84` 使用 +1112，表起点为 392。当前循环把 x64 四元数尾部的 +336..391 改成表项，整个 23×30×3 浮点表又错移 56 字节。

`model_init.cpp:24-25,106-111` 的 14296、14380、14392、14404 等 -999 地址在 x64 落入 `pmdToonFileNames`（x64 `modelDirectory` 为 13696，toon 文件名从 14208 起），而原版写的是 15232、15316、15328、15340 等槽。部分真实 -999 哨兵因此保持零。`model_init.cpp:91` 的 `memset(m+8632,0,0x88)` 也不同于原版 +8696、0xC0。

这是独立于失败释放的初始化状态不等价；哪些运动捕获/标准姿势操作最终表现异常，还需追踪各消费者和动态差分。不要将其简单表述为“所有模型加载必崩”。

## 结构性缺陷和高危待验证项

### C04 / P2：销毁仍有未迁移的 x86 指针槽

`model_dispose.cpp:215-217` 的 `FreeField(m,8732)` 在 x64 落入 `gap4`，本次探针定位该 gap 为 8688..8739。原版析构 `0x7FF7CB4C9311` 释放的是 +8816。当前零初始化模型的 8732 是零，所以不能仅凭此行声称稳定崩溃；若相关区域被使用，则存在将非指针当指针释放的风险。要恢复真实字段及其写入者后再决定删、迁移或替换，禁止猜测名字。

### C05 / P2：分配和释放 API 不配对

例：`bone_sort.cpp:44` 用 `operator new` 分配，`:40` 重建时 `std::free`；`add_model.cpp:58` 的对象同样以 `free` 释放。`model_dispose.cpp:82-84` 的 `FreeOwned` 把多种来源都统一成 free，PMD/PMX 大量表用 operator new。原版析构反编译则显示匹配 C++ delete/delete[] 调用。即使当前 Windows CRT 恰好共享堆而未出错，C++ 语言层仍不成立，换 allocator/诊断运行库后可能失效。

### C06 / P2：数组真实容量仍依赖 padding

`model_layout.hpp:101-104` 的 name/nameEn 声明为 20 字节，每个后跟 30 字节 RawPad；`pmx_load.cpp:306-309,328` 按 50 字节容量调用转换。总对象容量碰巧足够，不等于成员合法容量。应表达真实 50 字节名称存储，同时保持 PMD 磁盘只读 20 字节的格式边界；盲目把成员扩到 50 再保留 `pmd_load.cpp` 的 `sizeof(model.name)` 读取，会反过来破坏 PMD 文件消费。

### C07 / P3：诊断本身还有架构偏移与死辅助函数

`physics_frame.cpp:1093-1094` 诊断读 `world+0x2C`，`:1301-1302` 诊断写 `world+0xF0`，二者都标为 localTime；缺少架构条件，不能信赖其结果作为 x64 等价证据。当前都是 DIAG 条件编译，不能说正常发行版执行这两个偏移。

`keyframe_common.hpp:38-46` 的两个 `RdPtr` 只 memcpy 4 字节到未初始化的 x64 指针。本次 rg 未找到调用者，应删除死代码或改为正确 typed access，不能把它计成已触发运行时缺陷。

## 历史报告复核

- audit8 的 PMX displayRootBone 使用 special、facial 只填前两帧：当前 `pmx_load.cpp:1429-1460` 已换成遍历全部帧及检查首条目 type，不能继续列为现存缺陷。
- audit8 的无材质顶点 materialIndex 未初始化：当前 `pmx_load.cpp:692-715` 已把赋值移出材质数量分支，本轮静态复核认为对应旧问题已修。
- audit8 的 20 字节名称/50 字节写入结构问题仍在，见 C06。
- audit8 PMM v2 当前姿势“34B/31B”疑点：当前加载仍读 trans 12B + rotQuat 16B + 三个单字节（`:938-949`）；本轮未重新反汇编覆盖全部分支、也没有真实 v2 项目偏移日志，不能升级成已确认缺陷，更不能据旧报告直接改文件格式。
- `ported_funcs.hpp` 和 pmd_load 中若干 stub 注释已过时（例如骨骼排序已有实现），文字命中不等于缺实现。

## 逐文件覆盖

等级：A=本次源码精查并有原版/探针实证；B=源码关键路径阅读；S=静态全文件扫描与函数/模式清单，未逐语句等价核验。S 不是通过结论。

| 文件 | 等级 | 本次关注点 / 仍需验证 |
|---|---|---|
| model/add_model.cpp | A | 分配、初始化、失败释放链，IDA caller |
| model/model_init.cpp | A | C01/C03；原版两个初始化函数 |
| model/model_dispose.cpp | A | C01/C04/C05；原版释放顺序和槽 |
| model/pmd_load.cpp | B | 文件失败/取消路径，PMD固定长度与 sizeof |
| model/pmx_load.cpp | B | C02/C06、历史 display/material 修复复核 |
| model/bone_sort.cpp | B | 排序表、分配释放不配对 |
| model/post_load_init.cpp | B | UI表构造、零链与成员语义 |
| model/keyframe_common.hpp | B | typed display链、未用截断指针辅助函数 |
| model/track_apply.cpp | B | 重力/附件轨道插值分支，未原版差分 |
| model/model_skinning.cpp | B | BDEF/SDEF分派和缓冲区，浮点/UV完整性未动态核验 |
| model/morph_apply.cpp | B | 类型化材料/骨骼池，嵌套 group 与顺序仍需数值差分 |
| model/bone_edit_undo.cpp | S | 撤销入口和拥有关系，需跨模型回归 |
| model/bone_transform.cpp | S | IK、继承、物理前后分派；需极限角/顺序/浮点轨迹 |
| model/key_registrars.cpp | S | 注册/容量/链表路径；需池满及重帧差分 |
| model/model_frame_seek.cpp | S | seek 与 advance 独立实现；需相同帧交叉比较 |
| model/model_keyframe_advance.cpp | B | x64 slerp/浮点注释、调用和类型；未全函数复核 |
| model/model_keyframe_edit.cpp | S | 剪贴板裸记录、索引搬移和删除链 |
| model/model_labels.inc | S | 文本/常量，不代表所有语言字符串校验 |
| model/model_query_helpers.cpp | S | 查询和选择循环、旧裸指针辅助函数 |
| model/path_resolve.cpp | S | 路径回退和纹理索引，需目录/编码矩阵 |
| model/vmd_load.cpp | S | key/显示/IK导入，需多编码/超容量文件 |
| io/io_device_helpers.cpp | S | IO辅助、设备边界 |
| io/pmm_io_common.hpp | B | 独立流记录访问，结构与磁盘不可混用 |
| io/pmm_load_dispatch.cpp | B | 签名/version分派，历史 v1 哈希不是本轮实测 |
| io/pmm_load_v1.cpp | S | 模型迁移、全局轨道，需完整往返 |
| io/pmm_load_v2.cpp | B | 当前姿势读取、模型初始化触达；旧34B疑点未确认 |
| io/pmm_save.cpp | S | 各轨道和持久化字段，需原版交叉打开 |
| io/vmd_save.cpp | S | 格式边界和字段访问，需重导入差分 |
| io/vpd_file.cpp | S | 解析/导出，需编码/异常行测试 |
| io/vsq_load.cpp | S | MIDI/文本事件解析，需节拍/声母/边界样本 |
| physics/physics_create.cpp | B | Bullet公开API、矩阵/弹簧设置，未数值证明 |
| physics/physics_frame.cpp | B | pose前后门控、步进、DIAG偏移；需长轨迹 |
| physics/scene_create.cpp | B | Bullet world/重力/地面参数；仅源码检查 |
| physics/scene_gizmo_data.inc | S | 常量几何，未逐值原版比对 |
| math/color_lerp.cpp | B | 明确仍引用x87 double证据；x64算序需重核 |
| math/quat_to_mat3x4.cpp | B | 算序、全局倍率；x64边界浮点未差分 |
| math/math_kernel_ledger.cpp | B | 这是历史对应关系账本，不是本轮原版验证 |

相关头文件：`model.hpp/model_layout.hpp` 为 A；`bone_layout.hpp/subrecord_layout.hpp/undo_layout.hpp/physics_scene.hpp` 为 B；`raw_pad.hpp/ported_funcs.hpp/globals.hpp/layout_pins.hpp` 为 B（重点是偏移、占位和旧注释）；`mmd_app.hpp/app_layout.hpp/global_key_layout.hpp/accessory_layout.hpp/d3dx_dyn.hpp` 为 S 依赖扫描。本报告不代替其他子域对全部头文件的审计。

## 源码恢复建议

1. 首先修 C01/C02/C03 并增加“失败装载可安全销毁”和“PMX header 不改变 pose trace”两个针对性回归，然后才有意义扩大功能等价测试。
2. 原版 VA/RVA 保留在证据/映射表，不放业务表达式。数值常量要区分地址、磁盘格式长度、算法常量：后两者不应机械删去。
3. 把运行时 Model、PMX 输入记录、PMM/VMD 序列化记录和原版 ABI 对照分层。独立内建程序无需维持原版进程地址和 sizeof；若暂时保留 ABI 视图，也应限制到兼容边界。
4. 拥有型数组使用明确 owner/容器与一致释放方式；非拥有的 Bone/Material 指针或 span 可以保留。目标不是禁止所有指针，而是消除不透明所有权和地址算术。
5. typed 命名应有证据，不猜“原作者变量名”。原作者源代码不可由反编译唯一恢复，但可以恢复语义一致、可维护的实现形式。
6. 物理和数学要固定 Bullet 版本、编译浮点选项、D3DX 版本，然后建立原版与移植版的姿态/顶点/刚体轨迹比较；静态链接同名 Bullet 不是逐位等价证明。
