# 第八轮差分审计报告 —— 模型 / 动作 / 工程文件 IO

- 审计子域：src/model/{pmd_load,pmx_load,vmd_load,path_resolve,post_load_init,model_init,add_model,model_dispose}.cpp、src/io/{pmm_load_dispatch,pmm_load_v1,pmm_load_v2,pmm_save,vmd_save,vpd_file,vsq_load}.cpp、src/window/enhance_model_io.cpp
- 参考原版：MikuMikuDanceE_v932x64（IDA database e02eac62，imagebase 0x7FF7CB420000，Hex-Rays）
- 对照函数：LoadPMX=sub_7FF7CB4C9AC0（0x89AB 字节）、ModelLoadPMD=sub_7FF7CB4D2670（0x3333）、LoadVmdMotion=sub_7FF7CB48D170（0x1706）、PMM 装载 shell+v2 体=sub_7FF7CB4A2C10（0x6069）、PMM 保存=sub_7FF7CB4950A0（0x3D8C）
- 结论概要：PMD/VMD/VPD/VSQ/辅助链与原版高度一致（仅 2 处边角偏差）；PMX 加载发现 1 条 P1（display frames 语义）与若干 P2；PMM v2 发现 1 条 P0 级待验证疑点（当前姿态块每骨骼字节数）。共 10 条。

---

### [P0-待验证] PMM v2「当前姿态」块每骨骼字节数：原版 LOAD 读 8 浮点+2 字节，我们与原版 SAVE 均为 7 浮点+3 字节
- 我们：src/io/pmm_load_v2.cpp:930-950（全状态加载 pose-display：每骨 `Rd(bone.trans,12); Rd(bone.rotQuat,16)` + 3×1B：physicsDisabled/bonePhysicsState/boneSelection，共 31B/骨；skip 路径同形，:592-596）；src/io/pmm_save.cpp:565-580（保存每骨写 7×4B + 3×1B，引用 x86 0x41C5EB..0x41C75D）
- 原版：**LOAD**（原始反汇编，非反编译）：sub_7FF7CB4A2C10 @ 0x7FF7CB4A4122..0x7FF7CB4A4279 —— 循环界 `cmp r12d,[model+3110h]`（=boneCount），每轮 **8 次** `_read(fd,[bones+624*i+off],4)`，位移依次 `148h,14Ch,150h,154h,158h,15Ch,160h,154h`（第 8 个再次写 +154h=rotQuat.x 槽位），随后仅 **2 次** 1 字节读（→m+3128h=12584 物理数组、→m+3120h=12576 选择数组），合计 34B/骨。**SAVE**：sub_7FF7CB4950A0 @ 0x7FF7CB4967E0..0x7FF7CB496941+ —— 每骨 **7 次** `_write`（148h..160h）+ 3 次 1 字节（bone+1F5h=+501、m+12584、m+12576），合计 31B/骨。
- 差异：原版 x64 二进制自身 LOAD(34B)/SAVE(31B) 不对称（读侧比写侧每骨多消费 3 字节，且第 8 浮点覆盖 rotQuat.x、不读 physicsDisabled 字节）。我们的 load/save 两侧统一 31B。三种可能：(a) 原版 LOAD 确实按 34B 消费（说明文件中该块实为 8 浮点，SAVE 侧我方尚有 3B/骨写入位置未定位）→ 我们的 v2 加载对真实 MMD 保存的 .pmm 每骨少读 3 字节，首个模型起全流错位，后续模型/附件/相机全部乱读——正是「加载工程后崩溃/乱套」级别的数据损坏；(b) 0x4A4103 循环属于某个非主流路径（如骨骼结构差异→重载模型分支），主流路径另有 7f+3B 读循环（该函数确有使用 `add rsi,270h` 增量指针的循环形态，imul 检索不覆盖）→ 无差异；(c) 反汇编证据链无误但需实文件定夺。本项目此前只对 v1 场景做过位级验证（pmm_load_dispatch.cpp:23-28 注明 v1 三场景 K 哈希一致、v1→save-as 往返一致），v2 实文件验证状态不明。
- 建议：用真实 MMD x64 保存的 v2 工程（含带骨骼模型）实测：开启 MIKUDANCESTUDIO_DIAG 的 LogPmmModelStage 跟踪流位置，比较我们 pose-display 前后 `_tell` 与按 34B/骨预算的位置；若证实 34B，则 v2 装载两路径（全状态+skip 消费）每骨补第 8 浮点读（丢弃或按原版写 +154h）并减去一个尾字节读，保存侧同步核对原版是否另在别处补写 3B/骨。

### [P1] PMX displayRootBone 判定条件颠倒：special 标志 vs 首条目类型
- 我们：src/model/pmx_load.cpp:1444-1446 `if (frameCount > 0 && frames[0].count > 0 && !frames[0].special) model.displayRootBone = frames[0].entries[0].index;`
- 原版：sub_7FF7CB4C9AC0 @ 0x7FF7CB4D03AB..0x7FF7CB4D03B9：
  ```c
  if ( *((int*)v474 + 4) > 0 )          // frames[0].count > 0
  { v503 = *((QWORD*)v474 + 3);          // frames[0].entries
    if ( !*(_BYTE*)v503 )                 // entries[0].type == 0（骨骼条目）
      m+15528 = *(int*)(v503 + 4); }      // displayRootBone = entries[0].index
  ```
  special 字节被读入临时后即丢弃（0x7FF7CB4D0058 `read(v5,v720,1)` 无存储），原版完全不使用它做此判定。
- 差异：PMX 规范中 Root 帧 special=1。按我们的条件 `!special` 恒假 → displayRootBone 永远保持 0（model_init 默认），displayGroups[0].name/nameEn（pmx_load.cpp:1459-1462）退化为 bones[0] 的名字；原版取 Root 帧第一条骨骼条目。凡 Root 帧首条目不是 0 号骨骼的模型（相当常见），骨骼组下拉第一组的组名显示错骨名。附带：当 frame[0] 首条目为 morph 条目时我们会把 morph 索引当骨骼索引用（Bones[morphIndex] 越界读风险，仅畸形文件触发）。
- 建议：改为 `frames[0].count > 0 && frames[0].entries[0].type == 0`（并删除 special 依赖）。

### [P2] PMX facialFrameCount 计数被 frames[0].special 门控（原版无门控）
- 我们：src/model/pmx_load.cpp:1418-1421 `const int morphGroupCount = (frameCount > 0 && frames[0].special) ? morphEntryTotal : 0;`
- 原版：0x7FF7CB4D022C-0x4D023E：`v473 = v668`（全帧 morph 条目总数）→ `*(BYTE*)(v4+12608) = v473`，无任何 special 检查。
- 差异：标准文件（Root/表情帧均 special=1）下行为一致；非标准文件（frame[0].special=0 但存在 morph 条目）下我们不建表情组、groupCount 少 +1、下拉无 Facial 项，原版照建。
- 建议：去掉 special 门控，直接 morphEntryTotal。

### [P2] PMX facial 组记录只填前两帧（原版填全部帧的 morph 条目）
- 我们：src/model/pmx_load.cpp:1428 `for (std::uint32_t f = 0; f < frameCount && f < 2; ++f)`（计数却来自全部帧 :1409-1414）
- 原版：0x7FF7CB4D0294..0x4D0395：填充循环遍历 `v494 < v646`（全部帧），凡 `entries[e].type==1` 即写入 46B 记录 {morph SJIS 名, nameEn, targetIndex, groupIndex=1}。
- 差异：若 morph 条目出现在第 ≥2 帧（非标准文件），我们按总数分配数组但尾部记录保持全零（name 空、targetIndex=0、groupIndex=0）→ 表情面板出现空白/指向 morph0 的幽灵条目；原版全部正常填充。
- 建议：填充循环去掉 `f < 2` 截断。

### [P2] PMX matCount==0 时顶点 materialIndex 未初始化（原版统一写 0）
- 我们：src/model/pmx_load.cpp:599-708 —— 顶点→材质分配整块位于 `if (matCount != 0)` 内；pmxVertices 仅前 40 字节被 memset（:389），materialIndex（记录 +184）不受覆盖。matCount==0 且 vertCount>0 时各顶点 materialIndex 为 operator new 的脏值。
- 原版：0x7FF7CB4CBFCC..0x7FF7CB4F0F2 —— 该块以 `if (vertexCount)` 为外层门控（m+8），vertMat 数组 memset 0 后无条件写 `pmxVertices[v].+184 = vertMat[v]`（含 0）。
- 差异：仅「有顶点无材质」的畸形 PMX 下，我们的 materialIndex 为垃圾值（原版为 0）；当前渲染路径 0 材质不绘制，暂无可见后果，但与原版位级不一致。
- 建议：把边长分配/写回移出 `if (matCount)`，保持以 vertexCount 为门控。

### [P2] PMX 纹理/球面/toon 索引增加原版没有的上界截断
- 我们：src/model/pmx_load.cpp:632/648 `if (idx < 0 || idx >= (int)texCount)` → 置空路径（toon 分支 :664-677 同理）
- 原版：0x7FF7CB4CBBC3..0x7FF7CB4CBD63 仅检查 `v114 < 0`；idx ≥ texCount 时直接 `v108[v114]` 越界解引用（读野指针，可能 AV 或拼出垃圾路径）。
- 差异：属我们新增的防御（按任务要求记录「多了原版没有的截断」）。行为仅在损坏文件上分叉；保留无碍，但需知与原版崩溃点不同。
- 建议：保留（或加注释声明为有意的加固偏差）。

### [P2] PMX 非法 index-size / additional-UV 档位的消费差异
- 我们：src/model/pmx_load.cpp:392-399/540-547/605-611/726-733/955-962 等 `readIdx` 系对 size∉{1,2} 一律按 4 字节读；:353 `kSpec[model.pmxAdditionalUvCount & 7]` 对 addlUV=5..7 越界读 5 元素静态数组
- 原版：各 switch 仅 case 1/2/4，其它取值不读任何字节（如 0x7FF7CB4CA427 权重类型、0x7FF7CB4CBBC3 纹理索引）；addlUV>4 走 default 分支（0x7FF7CB4CA1F6 `v38 = v702` 未初始化值）→ CreateVertexBuffer 大概率失败进错误框
- 差异：仅头部声明非法值（idxSize∈{0,3}、addlUV∈{5..7}）的畸形文件上流消费不同；两者后续都会错位或失败，失败形态不同。
- 建议：可不改；若追求位级一致，把 readIdx 的 default 改为不读。

### [P2-info] PMX 模型名/英名 SJIS 镜像以 50 字节宽度写入 20 字节成员（依赖 RawPad 填充）
- 我们：src/model/pmx_load.cpp:306-308 `kSjisSizes[4] = {0x32,0x32,0x100,0x100}` 配 `ModelRecord::name[20]/nameEn[20]`（model_layout.hpp:101-104，其后各跟 RawPad<30> gap8/gap9）
- 原版：0x7FF7CB4C9D46/0x7FF7CB4C9E44 `strcpy_s(m+8896,0x32,…)`/`sub_7FF7CB429290(...,m+8896,…,50)` —— x64 原版该字段有效宽度即 50（name@8896、nameEn@8946、comment@8996）
- 差异：我们的 20 字节成员 + 30 字节 RawPad 在内存上复刻了 x64 的 50 字节跨距，WideToSjis→strncpy_s(dst,50) 的写入落入 padding —— 与原版逐字节一致，但形式上是越界写（UB；ASAN/字段重排会炸）。gap8/gap9 标注 unrecovered、无读取方，当前安全。
- 建议：将 name/nameEn 显式加宽为 char[50]（保持偏移断言不变，用 char[20]+显式 30 字节尾巴或直接 50），消除 UB。

### [P2] PMD 英文模型信息对话框取消路径多关了文件句柄（原版泄漏）
- 我们：src/model/pmd_load.cpp:602-607 EN-comment `MessageBoxA(...)!=1` 分支执行 `_close(fh); return false;`
- 原版：sub_7FF7CB4D2670 @ 0x7FF7CB4D40B9：同分支 `return 0;` **无 close**（JP 分支 0x7FF7CB4D292B 则经 LABEL_52 close+return，我们 JP 路径一致）
- 差异：仅英文 UI + 用户在 model infomation 框点取消时，我们释放句柄、原版泄漏 fd。行为对我们更正确，但与原版资源语义不一致（原版泄漏后仍继续用模型半成品路径）。
- 建议：保留（无害偏差），在注释标注即可。

### [P2] VMD 打开失败对话框标题与原版不符（原版为空标题）
- 我们：src/model/vmd_load.cpp:155-160 `MessageBoxA(hwnd, text, kJpTitleMotionLoad, 0)`（EN/JP 均用「モーション読込」标题）
- 原版：sub_7FF7CB48D170 @ 0x7FF7CB48D1E4-0x7FF7CB48D323：`MessageBoxA(hwnd, Buffer, Locale, 0)` —— 标题为全局 `Locale`（0x7FF7CB54A2B0，即空串，与各加载器用它清缓冲的用法一致），EN/JP 皆然
- 差异：打开失败时错误框标题我们显示「モーション読込」，原版为空标题。纯可见文案偏差。
- 建议：标题传 `""`。

---

## 已核对无差异的要点（抽样记录）
- PMX：头 8 指针字节序（enc/addlUV/vert/tex/mat/bone/morph/rigid @0x7FF7CB4C9B75-C9C3C）、UTF8 拒绝与 physicsMode=2、四文本槽空值回填（Null_%02d/情報なし/NoInfo）、顶点 BDEF1/2/4/SDEF 全字段与 SDEF 中点重定基（0x7FF7CB4CA94F-CAA22 与我们 :420-436 逐算术一致）、非法权重类型不消费字节、VB/FVF 阶梯(32/48/64/80/96, 274/524818/2622226/11011090/44565778) 与 D3DPOOL_MANAGED、边 VB 0xFF000000、面索引 16/32 位分档、toon01..10 默认、材质 flag&0x10→doubleSided、共享 toon 0xFE/0xFF 语义、每顶点最小 edgeSize 材质归属（严格小于，初值 999.79999f=0x4479F333 位级验证）、骨 flags 位段（0x1/0x4/0x300/0x400/0x800/0x2000/0x20/0x8→InertTip）、IK 链 ×0.25 与 π/ang 迭代提升（3.141592 上限 360）、IK/附骨/移动层传播 sweep、morph 0-8 全类型与跨 morph 去重计数（0x7FF7CB4CDD2E-CDFAF/C EFA0-CF505 与我们逐条同构）、UV 家族 morph 基值（uv.xy+ZW 基、addlUV 四分量）、三材质 morph 池 1.0 初值、刚体 boneIndex<0 重定基（bones[0]）、Rz*Rx*Ry*T 链、逆变换链、joint 乱序限位（+84/+72/+108/+96 组序）与过拉伸半径 JointRadiusBound（仅前两组限位参与，physics_create.cpp:427-441 一致）、显示键 1000×40 池、插值默认 20/20/107/107 与 quat w=1。
- PMD：magic 分派（Pmd/PMX）、名字 20/注释 256 读宽、*_read 门控（rigid `read!=-1`）、材质 flag/sphere/toon 读序、texName "*" 分裂加载（球面仅成功才记路径、纹理路径无条件记）、骨/IK/表情（首 morph 表共享重映射 0x4D3B24）、英名区段（morph0 跳过、[19]=0 终结、toon 以 read 返回值门控 0x7FF7CB4D42B8）、hasFlag sweep（type==5 || (0x300 && physMode==2) && tail∈{2,4}）、动画池容量（x64 600000/20000/1000 = 0x2255100/0x61A80/0x9C40）。
- VMD：双格式（10/20 字节名）、3939 插值标记（ch0 双 u16）、四通道 +65/+69/+73/+77 布局（for 后置表达式时序与反编译表面差异已核实）、旧格式相机共享曲线 5 字节扩展、fov=45/view=0、光键 frame/color/pos 序、自阴影 ver2 门控、显示键 count>10000 break、entries scalar-new/vector-delete 原样保留、undo 快照 36B/骨（trans@+328/quat@+340/物理字节数组）。
- PMM 分派：strstr 定位 + 版本域在匹配点 +0x14（strncmp 5 字节与原版 strcmp 在含 NUL 比较上等价）、错误框三态。
- 辅助链（path_resolve/post_load_init/model_init/add_model/model_dispose/enhance_model_io/vsq_load/vpd_file/vmd_save）：头注 VA 锚点与行为描述完整，与前七轮审计覆盖一致，本轮抽查未见新偏差。
