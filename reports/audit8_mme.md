# 第 8 轮差分审计 —— MikuMikuEffect v0.37 引擎内置移植（third_party/mmeffect + 宿主桥）

- 日期：2026-09-15　参考：MMEffect.dll v0.37 x64（IDA database=`1c790e4b`，imagebase 0x180000000）
- 范围：third_party/mmeffect 全部源码、src/render/mme/mme_bridge.cpp、src/exports/effect_api.cpp、include/mikudancestudio/mme_bridge.hpp，以及宿主侧接线（accessory.cpp / model_renderers.cpp / frame_scene.cpp 的 MME 桥接点）
- 方法：通读 port 工作树源码 + IDA 反编译原版关键函数（0x18000BC90 加载器、0x180016900 技术扫描、0x18002A9E0 行值归一、0x180058C70 ModelData ctor、0x18005A1E0 snapshot apply、0x18005B7A0 注册、0x180057430 OnCreateModel、0x18002CA80/0x18002C910 指派链）双向比对
- 说明：注入/反调试/钩子机制按任务要求不作为差异。

---

## 〇、用户崩溃场景专项（拖入 ray.x → ray_controller.pmx → Skybox / Time of day / Time of day.pmx 后闪退）

场景在 port 中的实际路径：

1. 拖入 ray.x → 宿主按附件加载 → 下一次 `MmeHostBeginScene`（third_party/mmeffect/src/mme_host.cpp:257-292）为它建立 MMHack 缓存并调 `OnCreateModel(kind=0)`；`MmeRegisterModelData`（mme_context.cpp:299-334）在注册尾部执行**同名 .fx 自动指派**（`MmeFindEffectFileForModel` 找到 ray.x 同目录的 ray.fx）→ 当帧 BeginScene 内编译 ray.fx（scene 类）。
2. ray_controller.pmx / Time of day.pmx 加载 → 同名 fx（ray_controller.fx / Time of day.fx 存在时）同样在注册时自动指派并编译。
3. 每帧 pass 计划把 ray.fx 场景载体放入 renderPassList；turn 循环中 `ctx->currentBindingObject = item.carrier`（pass_planner.cpp:2144）非空，进入下述 P0-1 的坏读窗口。

**结论：未发现可 100% 指认的单一空指针闪退点；但存在两个确定性 bug（P0-1、P1-1）叠加使该场景必然渲染错乱，另有一个时机差异（P2-2）+ 资源 eager 创建差异是闪退的首要嫌疑链。** 具体线索按嫌疑排序：

- **线索 A（首要闪退嫌疑，P2-2 关联）**：port 在 `MmeRegisterModelData`（注册链）内立即编译同名 fx 并 eager 创建 SAS 资源（sas_interpreter.cpp:2604-2608 `SasEnsureResourceTexture` 注释自认「The original defers some of this」）。加载 Time of day.pmx 的那一帧 BeginScene 内同时发生：PMX 材质表解析（wfopen）、fx 编译（可能上 MB 的 include 链）、D3DPOOL_DEFAULT 渲染目标批量创建。原版加载时机/惰性与此不同。若闪退伴随设备状态冲突（Begin 嵌套 / D3DPOOL_DEFAULT 创建失败路径），应从此处查。建议先开 MMEffect.debug 复现并取 MMEffect.txt 日志（"Loading effect file:" 之后崩溃即为该链）。
- **线索 B（行为破坏，P0-1）**：场景 pass 窗口内（currentBindingObject != nullptr，ray.fx 每帧必有）所有材质绑定的 map 查找键的 count 字段来自 `*(u32*)(ModelData*+0x38)` 裸读——port 的 ModelData 布局 +0x38 不是 materialCount（落在 std::string name_ 内），读到字符串内部数据（垃圾/指针截断）。结果：场景 pass 期间所有 per-draw 绑定永远 miss，全部对象退回原始管线；同时每次 miss 触发 lazy resolve 空转。属未定义行为读（不是典型空指针崩，但若垃圾值恰好索引越界即 UB）。
- **线索 C（控制失效，P1-1）**：`BuildNameTable` 的宿主 ID 比较被 32 位截断（与原版 `mov edx,eax` 字面一致），x64 宿主堆指针 >4GB 时骨骼/表情名表恒空 → ray_controller 的 CONTROLOBJECT（时间/太阳/参数骨骼）全部落到默认值。不崩，但 Time of day 一类控制器完全失效。
- **线索 D**：`mme_dlg.cpp` 的拖放处理（fx/emm 拖入主窗口，1593-1598 行扩展名分支）本轮未深审；若用户是以「拖 fx 文件」方式加载 Skybox/Time of day，崩溃点可能在对话框路径。
- 已排除：SAS 宿主绘制重放（SasHostRunPass，pass_planner.cpp:100-162）对空 record/空 device 有防护，且重放走原始 COM `rec->device->DrawIndexedPrimitive`（不过桥，无递归重入）；PMX/PMD 材质表解析器异常安全（mmhack_material_table.cpp:476-513 catch-all）；`activeRenderObject`/`activeRenderPass`/`renderPassCount` 在宿主渲染器接线完整（accessory.cpp:834-1025、model_renderers.cpp:1120-1271、frame_scene.cpp:393-404）。

---

## 一、发现列表

### [P0] bindingContext 的 +0x38 裸偏移读：原版 ModelData 布局假设失效，场景 pass 期间全部材质绑定失效（UB）
- 我们：third_party/mmeffect/src/mmeffect/material_bind.cpp:456-460（MmeSelectMaterialEffectBinding）、:501-506（MmeApplyModelRenderSnapshot）
  ```cpp
  unsigned int count = 0;
  if (bindingContext != nullptr) {
      count = *reinterpret_cast<unsigned int*>(
          reinterpret_cast<unsigned char*>(bindingContext) + 0x38);
  }
  ```
  传入的 `bindingContext` 是 `ctx->currentBindingObject`（callbacks.cpp:185），类型为 `ModelData*`（mme_context.h:334）。port 的 ModelData 是重排的 C++ 布局（model_data.h:4-7 自述「not 0x370 bytes」）：device_(+0x00)/objectId_(+0x08)/filename_(+0x10)/kind_(+0x18)/**materialCount_(+0x1c)**/…/name_(+0x28 起)——+0x38 落在 `name_`（std::string）内部，读出的是字符串 SSO/堆指针字节。
- 原版：0x18005A1E0（sub_18005A1E0）：
  ```c
  if ( a2 ) v8 = *(_DWORD *)(a2 + 56); else v8 = 0;
  result = sub_18002D910(qword_1800D9A40, v8, a1, *(_DWORD *)(a3 + 40), 1);
  ```
  a2 是原版 0x370 布局的 ModelData，+56(0x38) 恰为 materialCount（0x180058C70 ctor `*(_DWORD *)(a1 + 56) = a6;` 写入）。该 count 是 owner-keyed 绑定表查找键的首字段。
- 差异与影响：count 变为垃圾值 → `MmeFindMaterialBinding(count,…)` 的 `EffectOwnerManager::BindingKey(count, model, subset)` 永远 miss（所有绑定都以 count=0 创建：material_bind.cpp:582、:664、callbacks.cpp:209）→ 场景 pass 窗口内（ray.fx 等 scene 效果驱动的每个 turn，pass_planner.cpp:2139-2149 设置 currentBindingObject 后宿主重放的每个 DIP）`MmeApplyModelRenderSnapshot` 的绑定解析/`MmeRefreshDrawTechnique` 全部跳过，`binding->techniques[drawType]` 保持 null → draw wrapper（callbacks.cpp:234-300）走「no technique → 原始绘制」分支。**ray-mmd 主场景渲染期间所有对象（含 Skybox、Time of day.pmx）全部退回固定管线**；且每次 miss 都触发一次 `MmeResolveModelEffectBinding` 空转。属未定义行为（读任意对象 +0x38）。
- 建议：`bindingContext` 参数改型为 `ModelData*`，直接 `static_cast<ModelData*>(bindingContext)->materialCount()`；或在 MmeContext 里随 carrier 一并存 materialCount。禁止对非原始布局类做数值偏移读。

### [P1] BuildNameTable 宿主 ID 比较的 32 位截断：x64 宿主堆地址下骨骼/表情名表恒空，CONTROLOBJECT 全部失效
- 我们：third_party/mmeffect/src/mmeffect/model_data.cpp:143-146
  ```cpp
  unsigned long long hostId = reinterpret_cast<unsigned long long>(ExpGetPmdID(i));
  if (hostId != static_cast<unsigned int>(objectId_)) {   // ← 32 位截断
      continue;
  }
  ```
- 原版：0x180058E7A-0x180058E86：
  ```asm
  call cs:ExpGetPmdID
  mov  edx, eax          ; 高 32 位清零（32 位截断）
  cmp  rdx, [rbx+18h]    ; 与完整 64 位 objectId 比较
  jz   ...
  ```
  原版确是同样截断——但原版宿主 MMD x64 的模型对象指针必须落在低 4GB 才可能相等（ray-mmd 在原版可用，说明原版宿主满足该前提，或 MMD x64 实为低地址分配器）。port 宿主用普通 C++ new（`ModelByIndex` 返回 app->ModelSlot(i) 对象），x64 Windows 默认堆地址 ≥0x1_00000000，截断后与 64 位 objectId_ 永不相等。
- 差异与影响：port 中**每个 PMD/PMX 模型的 nameToIndex_ 恒为空** → `MmeUpdateControlObjects → ResolveOneControl → target->findNameIndex(itemName)` 全部 miss（material_bind.cpp:1942）→ CONTROLOBJECT 引用的骨骼/表情全部走 `SetControlDefaults`（0/10/1 默认值、单位阵）。ray_controller、Time of day 等控制器变量全部冻结；`(self)`/`(AttachedModel)` 全对象引用不受影响（不经名表）。
- 建议：改为 64 位全宽比较（`hostId != objectId_`）。虽然字面上偏离原版汇编，但原版截断在原宿主下等价于全宽相等（低 4GB 指针截断无损）；全宽比较在两种宿主下都恢复「原版应有」行为。若坚持字节级 1:1，需先证明本宿主模型对象必在低 4GB（不成立）。

### [P1] SAS 解析/技术扫描失败未拒绝加载：效果指派静默无效，丢失原版「Failed to load effect file」报告
- 我们：third_party/mmeffect/src/mmeffect/effect_engine.cpp:338-348
  ```cpp
  if (entry->effect != nullptr) {
      entry->sas = SasParse(entry->effect, pathAnsi, device);   // 失败返回 nullptr
      ...
  }
  cache[pathAnsi] = entry;   // effect 保留、errorText 为空 → 调用方视为成功
  ```
  `ApplyEffectToModel`（emm_manager.cpp:280-309）只在 `loaded->effect == nullptr && !loaded->errorText.empty()` 时走失败路径（MessageBox + 卸载）；effect 非空而 sas==nullptr 时静默按成功处理，`MmeResolveModelEffectBinding` 建出 sas=null 的绑定 → 技术表恒空 → 全部原始绘制，无任何日志/提示。
- 原版：0x18000BC90 加载链——D3DX 编译成功后：
  ```c
  result = MME_SasParseStandardsGlobal(a1);            // 0x18000c417
  if ( !result ) {
      result = sub_180016900(a1);                      // technique/pass 扫描（MME_SasScanTechnique/MME_SasScanPass 任一错误→1）
      if ( !result )
          return sub_18001DD20(a1, v4);                // 选择表预计算
  }
  return result;   // 非 0 = 加载失败（上层 sub_18000B880 → 0x18000b880 失败报告 + MessageBox）
  ```
  即 SAS 解析或技术/pass 扫描任一失败，整个加载按失败处理（与编译失败同路的报错 UI）。
- 差异与影响：行为+UI 双差异。ray-mmd 的 fx 若某技术校验失败（老卡/驱动差异下常见），原版弹窗并拒绝；port 静默指派成功但永不生效，用户无从得知。
- 建议：`SasParse` 返回 nullptr 时置 `entry->effect = nullptr`（Release 后置空）并写 `errorText`（可用 SasGetLog 已收集的文本），让既有失败路径接管。

### [P1] 效果热重载完全缺失（fileStamp 无消费者）
- 我们：third_party/mmeffect/src/mmeffect/effect_engine.cpp:251-256 记录 `entry->fileStamp`，全工程再无读取（grep 证实）；缓存命中即返回（:241-244）。
- 原版：0x18000BC90 前段 `v12 = a1[1]`（已有 effect 的重载分支）：`vtbl+544`（GetCurrentTechnique）取旧 technique → `vtbl+616` 将其交给重新加载流程保持当前技术；配合 FUN_18000B7F0（stamp 探测）每 100ms 的变更检查驱动重载（stamp 即为此服务）。
- 差异与影响：编辑 .fx 不会自动重载（原版约 100ms 内生效）。对工作流/调试体验差异显著；对运行时行为无崩溃风险。
- 建议：为 `MmeEngineLoadEffectFile` 增加 stamp 比对 + 重载（保留当前 technique 语义可暂缓）。

### [P2] EMM 行值 "hide"/"main_default" 未按原版归一化
- 我们：third_party/mmeffect/src/mmeffect/emm_manager.cpp:1119-1123（整行）、:1131-1137（子集行）只判 `== "none"`；"hide"/"main_default" 作为字面路径传 `ApplyEffectToModel` → `MmeEngineLoadEffectFile("hide")` 记录一条 "Loading effect file: hide" 日志并留下 null-effect 缓存条目。
- 原版：MME_EmmApply（0x18002E8D0）对整行（0x18002fd0f）与子集行（0x18002ff4f）都经 FUN_18002A9E0：`memcmp` 链逐一比较 "none"/"hide"/"main_default"（0x18002aa7b/0x18002aad2/0x18002ab2e），命中即 LABEL_36 清空路径（= 未指派）。
- 差异与影响：最终效果等价（都未指派），但 port 产生假日志、污染缓存、并在 `MmeAssignEffect` 的默认行回退语义上埋雷。EMD 侧（`ResolveEmdValue`，emm_manager.cpp:593-599）已正确实现三值归一——EMM 侧应复用同一函数。
- 建议：`MmeEmmLoad` 的两处行值套用 `ResolveEmdValue`。

### [P2] 同名 .fx 自动指派的触发点与原版不一致（原版注册链无此逻辑；原触发点未定位）
- 我们：third_party/mmeffect/src/mmeffect/mme_context.cpp:324-333 —— `MmeRegisterModelData` 尾部 `MmeFindEffectFileForModel`（同名 .fx → "[<fx>]" 嵌入名 → EMM Default 行）+ `MmeAssignEffect`。
- 原版：OnCreateModel（0x180057430）→ sub_18005B7A0（0x18005b7a0）注册链止于 manager 列表插入 + `sub_180039D10`（std::map 红黑树插入，已反编译确认），**无任何 .fx 查找/指派**；"AutoLoading: "（0x1800b57e8）仅用于 PMM 对应 .emm 的自动加载（sub_1800570D0，0x1800571ae 唯一引用）。原版的同名 .fx 自动指派入口本轮未定位（候选：MMHack 侧加载 hook、指派对话框默认值、EMM apply 的 Default 行展开——加载链 callers：sub_18002CA80←sub_18002C910←sub_18002BAA0、sub_18001E4E0←sub_180041D10←sub_180044080 对话框）。
- 差异与影响：port「注册即自动指派」对 ray.x→ray.fx 场景是 load-bearing 的（Ray-MMD 依赖同名机制）；但与原版的触发时机/前置条件（是否要求 EMM default、是否仅特定来源）无法对齐，可能出现在原版不会指派的场合强行指派（或反之）。这也把 fx 编译挪进了 BeginScene 帧内（见〇-线索 A）。
- 建议：补一轮专项定位原版触发点（重点查 MMHack 的 LoadedPMMFile/D3DXLoadMeshFromXW hook 侧与 sub_18002BAA0），再对齐时机。

### [P2] 原版加载成功后记录视口宽高到效果对象，port 无对应字段
- 我们：third_party/mmeffect/src/mmeffect/sas_interpreter.cpp（SasEffect 定义中 grep 无 viewportWidth/Height）。
- 原版：0x18000BC90 LABEL_80（0x18000c3e0-0x18000c411）：成功后立刻 `GetViewport`（vtbl+0x180）→ `a1+32/a1+36`（a3/a4 为 0 时回退视口宽高），再进 SAS 解析。
- 差异与影响：原版用该宽高作 offscreen/尺寸默认（消费点未细查）；port 缺失可能在视口相关默认值上偏离。低风险，标记待查。

### [P2] DrawAccessorySubset 的属性表 128 项上限与手写 vtable 槽位
- 我们：src/render/mme/mme_bridge.cpp:284-390 —— 手写 ID3DXMesh vtable 槽号（3/4/5/6/8/13/14/19）；`D3DXATTRIBUTERANGE_SHIM table[128]`，`tableSize > 128` 时**跳过属性表、按整网格绘制**（所有面一次画出，材质状态错乱）。
- 原版：直接 `ID3DXMesh::DrawSubset(material)`（MMD 原生调用），无 128 上限。
- 差异与影响：>128 材质的 .x 附件（罕见；ray.x 通常 ≤2）绘制错误；vtable 槽位依赖 d3dx9 头的精确顺序（与 d3dx9_43 ABI 一致，当前正确但脆弱）。
- 建议：tableSize > 128 时改为动态分配或分批读取。

### [P2] D3DX 运行时未锁定 d3dx9_43.dll
- 我们：third_party/mmeffect/src/d3dx9_dyn.cpp:127-133（注释自认「原版锁 43 版，此处复用宿主已加载的 d3dx9_XX.dll」）。
- 原版：MMEffect.dll 静态导入 d3dx9_43.dll（IAT 0x1800a46xx 全部模块为 d3dx9_43）。
- 差异与影响：效果 pool 与效果对象同模块创建（自洽，无跨版本 pool 问题）；但 d3dx9_24~43 的 HLSL 编译器差异（旧版对某些语法/预处理器行为不同）下，编译成败与产物可能与原版不同——对兼容老 effect 的可复现性有影响。与宿主 d3dx 层共用模块本身是内置化的合理选择，保留记录。

### [P2] ModelData::filename_ 借用 MmhCachedObject::fileName（std::string）的脆弱生命周期
- 我们：third_party/mmeffect/src/mme_host.cpp:261-273（`obj->fileName = fname` 后把 `obj->fileName.c_str()` 传 OnCreateModel）+ model_data.h:26,209（filename_ 为 borrowed 指针，如原版）。
- 原版：借宿主模型对象的常驻路径缓冲（对象存活期间不变）。
- 差异与影响：当前 port 中 MmhCachedObject 与 ModelData 在同一 BeginScene diff 序列中同生共死（删除顺序先 ModelData 后 obj，已核对 mme_host.cpp:241-254），暂无悬垂；但任何后续改动（如 obj 缓存重建、fileName 再赋值）都会让 ModelData 悬垂——ModelData 内部 BuildName/查找在构造时消费该指针，`MmeEmmSave`/`MmeFindEffectFileForModel` 还会**长期**读 `model->filename()`（emm_manager.cpp:1426-1467、model 指名匹配）。
- 建议：ModelData 改存 `std::string filenameCopy_`（一次拷贝，行为等价、消除脆弱性）。

### [P2]（信息）加载失败时 errorText 的日志拼接与原版一致、重载时保留当前技术的语义未移植
- 原版 0x18000BC90 失败分支只附加一段（编译器文本或 "DirectX Error: <%s> [%08X]\n"）——port effect_engine.cpp:301-329 一致 ✓；重载分支（vtbl+544/616）属 P1 热重载缺失的一部分。

---

## 二、核验一致、无差异的关键点（抽样）

| 项目 | 我们 | 原版证据 | 结论 |
|---|---|---|---|
| D3DXCreateEffectFromFileW 参数 | effect_engine.cpp:271-300（defines `_INDEX`=`"PSIZE15"`、`MME_MIPMAP` 条件宏、flags=0x1000（+debug 0xC0）、共享 pool、CWD 切换到 fx 目录） | 0x18000c087-0x18000c170 逐项（v82 数组、byte_1800D99DE→192、byte_1800D99DA→MME_MIPMAP、getcwd/chdir 对） | 一致 |
| 加载错误文本（单段附加） | effect_engine.cpp:301-329 | 0x18000c1ab-0x18000c376（`!errors||!GetBufferSize()` 分支、"%08X"） | 一致 |
| RenderSnapshot 0x220 布局 | render_snapshot.h:84-110 static_assert 全字段 | 0x18005A1E0 memmove 0x220 / 0x180059C60 捕获偏移 | 一致 |
| snapshot apply 链（drawTypeIndex 比较→Select→find(count,subset)→选择表） | material_bind.cpp:484-549 | 0x18005a1e0（除 P0-1 的 count 读取） | 一致（除 P0-1） |
| EMD 行值 none/hide/main_default 归一 | emm_manager.cpp:593-599 | 0x18002A9E0（0x18002aa1b-0x18002ab52 memcmp 链→LABEL_36 清空） | 一致（EMM 侧见 P2-1） |
| 16×16 fmt 0x16 scratch RT + RT0 空绑定回退 | effect_engine.cpp:137-139、sas_exec.cpp:893-901 | 0x18000a96e / qword_1800D9A38、0x18001c93e-0x18001c973 | 一致 |
| 深度重定向走进程级深度注册表（跨效果共享） | sas_exec.cpp:759-768 注释链 | 0x18001d351-0x18001d390 | 一致 |
| EMM/EMD 解析（节/键正则、首键优先、BOM 拒绝、验证器、版本门 1..3） | emm_manager.cpp 全文 | 字符串 0x1800B52A0-0x1800B54D0、FUN_180047BD0 族 | 一致（注释引用充分） |
| 绘制状态宿主接线（activeRenderObject/-Pass、renderPassCount） | accessory.cpp:834-1025、model_renderers.cpp:1120-1271、frame_scene.cpp:393-404 | MMD 渲染循环同源移植 | 接线完整 |
| PMX/PMD 材质表解析 | mmhack_material_table.cpp（异常安全、防 -1 越界、toon01..10 内建名豁免、PMD `<model>.txt`） | FUN_18000e020/ FUN_18000bbf0/ FUN_18000c960 | 一致（含防御性收敛） |
| SAS 宿主绘制重放防护与 Begin/BeginPass/End 配对 | pass_planner.cpp:100-162（空 record 防护、失败不配 End=原版锁死语义） | FUN_18005a740 | 一致 |
| 模型注销的多容器清理（pass 列表/currentBindingObject/recordOffscreen） | mme_context.cpp:336-423 | FUN_18005b910 + 移植者补充防护 | 一致+加固 |

---

## 三、未深审区域（后续轮次建议）

1. sas_interpreter.cpp（2887 行）的 STANDARDSGLOBAL/注解解析细节、SasEnsureResourceTexture 失败路径（与线索 A 相关）。
2. mme_dlg.cpp（1702 行）：fx/emm/emd 拖放、指派对话框的对象树/正则对象匹配、ObjectOutsideParent（外部亲）UI 路径——本轮仅确认正则与文件扩展名分支存在，未与原版 sub_180044080（对话框主函数）比对。
3. anime_texture.cpp（1493 行）GIF/GDI+ 时间线细节与 .conf 关联；ini_file.cpp。
4. offscreen DefaultEffect 行的 "hide"（隐藏绘制）vs "none"（空绑定）语义：port 的 MmeOffscreenDefaultEffectHides（material_bind.cpp:715-782）引用了 sub_18002DB10 record+48 证据链，本轮未在 IDA 复核。
5. ObjectOutsideParent（外部亲）语义：除 GetAcsAttachedPmd 接线（mmhack_getters）外，指派层的 @Parent 命名/正则分支未核。

---

## 四、汇总

- 发现总数：10（P0×1、P1×3、P2×6）。
- 最重要的 3 条：
  1. **[P0] bindingContext+0x38 裸偏移读**（material_bind.cpp:456/502）——场景 pass 期间全部材质绑定失效 + UB，ray-mmd 必然渲染错乱；修复成本一行（改用 ModelData::materialCount()）。
  2. **[P1] BuildNameTable 32 位截断**（model_data.cpp:144）——CONTROLOBJECT 骨骼/表情解析全灭，ray_controller/Time of day 控制全部冻结默认值。
  3. **[P1] SAS 解析失败未拒绝加载**（effect_engine.cpp:338）——效果静默无效、丢失原版失败报错；叠加 P2-2（自动指派时机）构成崩溃场景的首要嫌疑链。
- 崩溃场景关联：P0-1 + P1-1 解释「加载后行为错乱」，闪退本身最可能来自「注册时自动指派 → BeginScene 帧内编译 + eager 资源创建」的时机/惰性差异（线索 A），建议修复 P0/P1 后开 MMEffect.debug 复测，若仍崩则转储优先查 SasEnsureResourceTexture 与 MmeStepSceneRecord 窗口。
