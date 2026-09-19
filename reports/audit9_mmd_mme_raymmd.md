# 第九轮审计：MMD/MME 1:1 程度 + RayMMD 渲染专项（合并报告）

- 基线：工作树未提交状态（2026-09-15）。
- 参照：MikuMikuDanceE_v932x64（IDA `d5fe9858`，基址 0x7FF7CB420000）、MMEffect v0.37 x64（IDA `2109c52a`，基址 0x180000000）、MME REFERENCE.txt、OpenMMD/bullet-2.75、逆向笔记（OpenMMD/MMEffect逆向）。
- 方法：4 个并行审计面（MME 二进制比对 / MMD 渲染管线比对 / RayMMD 全链路侦查 / 数据·物理·时间轴比对），关键结论经主线二次核验。

## 总体判定

| 子系统 | 1:1 程度 | 结论 |
|---|---|---|
| MMD 数据面（PMM/PMX/PMD/VMD/VPD/VSQ） | 字节级一致 | 可宣称 1:1；audit8 的 PMM v2 姿态块 P0 系**误归属**（v2 体实为 sub_7FF7CB498E30，非 0x7FF7CB4A2C10 外壳），我们的 v1(8F+2B)/v2(7F+3B 经映射) 两侧均正确 |
| MMD 运行时（贝塞尔/IK/物理/时间轴） | 语义一致 | 内嵌 bullet 与 MMD 修改版逐字节相同；剩余浮点精度链 P2 |
| MMD 渲染管线 | ≥98% 指令级吻合 | 含配件 10 倍漫射光、toon02 暗色表、stale 纹理等原版怪癖的刻意保留 |
| MME 对象/材质路径 | 覆盖度 ~90% | 语义表/CONTROLOBJECT 主体/offscreen 格式表/EMM 均逐地址对上 |
| **MME 场景效果路径（scene/postprocess + offscreen + Draw=Buffer）** | **正确度 ~75-80%** | **系统性缺口，恰是 RayMMD 生存的路径** |

## RayMMD「渲染结果不对」根因（按可能性排序）

### R1 [P0·已三重验证] DefaultEffect 行键不支持通配符 `*`/`?` 与 `self`
- 我们：`third_party/mmeffect/src/mmeffect/material_bind.cpp:694-716` 仅 `_stricmp` 精确匹配全路径/basename；全工程无 glob 实现。
- 资产事实（主线核验）：ray-mmd 的 DefaultEffect 行几乎全是通配——`Shader/textures.fxsub` 的 `"* = hide;"`、`"sky*box*.* = ..."`、`"GroundFog*.* = ..."`，以及 MaterialMap 的 `"*.pmx = ./materials/material_2.0.fx"` 等。精确行（`DirectionalLight.pmx = ...`）能匹配，通配行**永远失配**。
- 原版：REFERENCE.txt 明示「オブジェクトファイル名には "*" と "?" によるワイルドカードが指定できる」+ `self` 语义；MMEffect.dll 无 PathMatchSpec 导入（自写匹配）。
- 后果：失配对象落入"无行"路径→不经隐藏、无子效果，被**原生 MMD 着色画进 offscreen RT**。MaterialMap（G-buffer）里是普通彩色画面、PSSM1-4 应为 R32F 深度矩却是着色图、SSAOMap/FogMap/EnvLightMap 全空或被污染，ray.fx 拿它们当 G-buffer 解码。
- 症状：整体材质错误/偏色发暗、无 PBR 材质感、太阳阴影乱纹或缺失、无 SSAO/雾/天空光——"原生 MMD 被错误后处理"的观感。

### R2 [P0·二进制+资产双验证] Draw=Buffer 全屏三角形顶点约定整体错误
- 我们：`pass_planner.cpp:34-98`——`D3DFVF_XYZRHW|D3DFVF_TEX1`（stride 24）、`TRIANGLESTRIP`、**像素坐标**(-0.5..vp)；注释自认 `UNCERTAIN(0x18001bf80 slot1)`。
- 原版 0x18005A9C0：`SetFVF(274)`（XYZ|NORMAL|TEX1，stride 32）、图元 **6=TRIANGLEFAN**、顶点 **NDC ±(1+扩边)**：四角 (1,-1)、(-1-2/W,-1)、(-1-2/W,1+2/H)、(1,1+2/H)，normal=(0,0,0)，UV 带 -1/W、-1/H 半像素偏移。
- 主线核验：ray.fx `ScreenSpaceQuadVS` 为 `return Position;` 且 `oTexcoord1 = -mul(Position, matProjectInverse).xyz`（Position 当 NDC 重建视线）——像素坐标输入轻则全画在屏幕外；且顶点着色器绑定 + XYZRHW(POSITIONT) 在 D3D9 直接 `D3DERR_INVALIDCALL`，错误返回还会按原版语义锁存 sas+0x39 runFailed，**连锁杀死该效果后续所有绘制**。
- 症状：ray.fx 数十个 `Draw=Buffer` 后处理 pass 全部不出或半途熄火，最终合成缺失。

### R3 [P1·RayMMD] scene/postprocess 类效果的每帧语义参数绑定时机缺失
- 我们：`pass_planner.cpp:470-559/570-624/631-686`（step/finish/full-run）与 `:100-162`（SasHostRunPass）均不做参数绑定；唯一绑定入口 `material_bind.cpp:486-550` 仅在载体对象被绘制（DrawSubset 路径，`callbacks.cpp:185`）时触发。
- 原版：FUN_18005a410（step，经 sub_18005A020→sub_18001B340 矩阵/bool/int 族）、FUN_18005A740（slot-0）与 0x18005A9C0（slot-1）逐 pass 调 sub_18001B5B0（材质色族/名字表色族/ANIMATEDTEXTURE），FUN_18005a2c0（full-run）同理。
- 后果：ray.x 是 scene 类载体且不参与原生绘制（`callbacks.cpp:189-194` 直接 return）→ ray.fx 的 TIME、CONTROLOBJECT（SunLight/SSAO 等 morph）、LightDir、鼠标语义**永不更新**，冻结在编译期默认值——ray_controller 拖动无反应。
- 同族 P2（同函数一并修）：0x18005A9C0 绘制前 GetRenderState(7/137/52/161) 清零、stream/indices/decl 保存恢复，我们全缺。

### R4 [P1·RayMMD] offscreen 遍中 `(self)` 失效 → 灯光/雾/天空子效果自参数全默认
- 我们：offscreen DefaultEffect 的临时绑定存 `ctx->offscreenDefaultBindings`（`material_bind.cpp:718-779`）但无 owner；CONTROLOBJECT 求值 `material_bind.cpp:2036-2063` 只查 `ownerManager->bindings` → `(self)` 分支（:1866-1870）拿不到目标 → 落 `SetControlDefaults`（0/单位阵）。
- 原版：sub_18002ca80 行展开时把 owner 写入绑定记录（0x18002d0a2）。
- 症状：LightMap 内容错（多光源黑/位置错）、雾与天空参数冻结。RayMMD 的 Lighting/Fog/Skybox 子 fx 全部经由 offscreen DefaultEffect 分配，全部命中此缺陷。

### R5 [P1·RayMMD] `(OffscreenOwner)` 恒 null → 带阴影光源的投影矩阵全错
- 我们：`material_bind.cpp:1884-1891`（注释自认 divergence）。
- 原版：0x180057BC0 求值器 0x180057d58 从 setter+24 取持有 offscreen 的对象。
- RayMMD 15 个 fxsub 使用（DirectionalLight/PSSM、point_shadow、spot_shadow 等，读 `Position/Direction/Range±` 骨骼）→ 阴影方向/范围错误或整块丢失。

### R6 [P1·待验证] 同名 shared RT 被后编译效果无条件覆盖
- 我们：`sas_interpreter.cpp:1595-1700` 对每个效果的 OFFSCREENRENDERTARGET 声明无条件新建 RT 并 SetTexture，不检查 EffectPool 中已有 shared 值；ray.fx 声明 `PSSM1 2048² R32F`，其他 fx 无注解声明同名参数时会被换成屏幕尺寸 A8R8G8B8。
- 待办：IDA 核查 0x180011960/0x1800143D0 是否跳过已绑定 shared 值。
- 症状：阴影图精度/分辨率骤降、MaterialMap 条带。

### R7 [P1] SAS 解析/资源创建失败仍静默加载成功
- 我们：`effect_engine.cpp:338-348`（SasParse 返回 nullptr 不拒绝、不写 errorText）；`sas_interpreter.cpp:2604-2608` 忽略 SasEnsureResourceTexture 失败。
- 原版：0x18000BC90 0x18000c417-0x18000c42a 任一失败 → MessageBox + 卸载。
- 症状：资产目录移动/大小写敏感/非 ANSI 路径时 ray.fx 静默作废——"RayMMD 完全没生效"且无报错。

### R8 [P1] SAS Clear 命令缺 per-target staged-clear 登记 / buffer pass 缺反登记
- 我们：`sas_exec.cpp:1117-1140` 只做 device->Clear；`pass_planner.cpp:1143-1159/1917-1920` 仅注解值两处写 map。
- 原版：sub_18005AEE0 在载体 scene 类时经 sub_18005E4F0/E570 把 ClearSetColor **运行时值**登记 per-target map；0x18005A9C0:0x18005aa2f 每次 buffer pass 前经 sub_18005E5D0 erase。
- 影响：turn 边界快照重建 offscreen 时 Clear/StretchRect 决策被 stale 值误判。

## 其余发现（与 RayMMD 关系较弱）

### MMD 渲染管线（原版还原度 ≥98%）
- [P2] 自阴影深度图/地面投影影的材质跳过门读错字段：`src/render/model_renderers.cpp:903`、`:1076-1077` 用 `record.edgeSize(+1212) <= 0`，原版（sub_7FF7CB4D8520 0x4D8593、sub_7FF7CB4D82A0 0x4D839E）用 `edgeColor[3](+1208)`（与 EgColor.w 同一 float）。→ 改读 `record.edgeColor[3]`。
- [P2] 效果路径材质循环多发 `SetMaterial`（`model_renderers.cpp:999`；原版 sub_7FF7CB4D87C0 全函数无 SetMaterial）→ 改变 MME draw 时刻现场合成的 EgColor/SpcColor/DifColor（`material_bind.cpp:1203-1240` 走 LIVE GetMaterial），旧式 Object/Accessory 特效高光/漫反射色与原版不一致。→ 删除该 SetMaterial。
- [P3] `SetTexture(1/2,NULL)` 预清空 vs 原版保留 stale 绑定（`model_renderers.cpp:668-669/716-717/725-726/737-739`）；影响 MMHack 捕获流语义 [待验证]。
- [P3] EffectRenderEnabled 门提升出 pass 循环（`frame_scene.cpp:397-410` vs 0x7FF7CB44A502 每迭代重评）；帧内状态不变，行为等价。

### MME 其余
- [P2] VIEWPORTPIXELSIZE 绑定时刻实时 GetViewport（`material_bind.cpp:1639-1641`）；原版仅 select 时机求值（sub_18001B340 case 33），offscreen 重定向下返回 RT 尺寸 ≠ 原版。
- [P2] audit8 遗留未修：效果热重载缺失（effect_engine.cpp:252 fileStamp 无消费者；原版 FUN_18000B7F0 100ms 轮询）；加载成功不记录视口宽高；`mme_bridge.cpp:355-357` 属性表 128 上限。

### 数据/物理/时间轴
- [P1] PMX displayRootBone 判定：`src/model/pmx_load.cpp:1443-1446` 用 `!frames[0].special`，原版（0x7FF7CB4D03AB）按首条目 type==0（骨骼）判定；标准 Root 帧 special=1 → displayRootBone 恒 0，骨骼组下拉首组名错 + morph 条目误当骨索引的越界风险。
- [P2] facialFrameCount 被 special 门控（`pmx_load.cpp:1418-1421` vs 0x4D022C 无条件）；facial 组填充截断前两帧（`:1428` vs 0x4D0294 遍历全帧）；matCount==0 时顶点 materialIndex 未初始化（`:684-707`）。
- [P2] VMD 打开失败对话框标题（`vmd_load.cpp:159`）。
- [P2] 精度链：timeline_advance 四全局轨道 double（`src/app/timeline_advance.cpp:365-399/450-464/542-558`，重力噪声迭代数整数截断可差 1 → Bullet 迭代数可观测变化，0x7FF7CB48A4DE 为 float）；PmdEdgeDistance 全链 double（`model_skinning.cpp:587-612`，原版 0x4E29D3 单精度）。`src/model/track_apply.cpp:129-149` 已是 float，两处不一致。
- [P3·文档] pmm_load_v2.cpp 头注与 audit8_io.md 需订正 v2 体归属（sub_7FF7CB498E30），防再诱发同类误判。

### 已验证一致（免重查清单，摘录）
- MME：SAS 命令集/状态机（FUN_180018200/FUN_18001bbc0 逐 case）、六基矩阵+后缀、CONTROLOBJECT 主体（名字注册表/伪骨骼/morph 权重/默认值/"."后缀 kind 猜测；**"&" 前缀当帧值在原版二进制中不存在**）、offscreen 格式表/尺寸解析/深度注册表、MRT 0-3 与恢复解绑、选择表、EMM/EMD、runFailed 锁存、ScriptExternal=Color 挂起恢复、同名 .fx 自动指派（ray.x→ray.fx ✓）、ray_controller.pmx 17 字节头（pmx_load 恰好丢弃填充字节）、repeat 轮转、MMDPass/Use* 过滤。
- 渲染：固定帧/效果帧/阴影图/edge/地面/配件/相机投影逐指令一致；pass 变量 18 写点、蒙皮 VB 同帧同源、DrawGeometry 桥接面吻合。
- 数据：VMD 贝塞尔（线性判定 X1==Y1&&X2==Y2，无独立线性标志位）、IK CCD（twist 折半/半角钳制/三平面限位）、bullet 库逐字节相同、物理帧序、PMM 保存计数语义（显示计数=总骨数）。

## 修复优先级建议
1. R1 DefaultEffect 通配符/self 匹配（material_bind.cpp:694-716）——RayMMD G-buffer 链的命门。
2. R2 Draw=Buffer quad（pass_planner.cpp:34-98，照抄 0x18005A9C0：FVF 274/TRIANGLEFAN/NDC±扩边/stride 32，并补状态保存清零恢复）。
3. R3 scene 类每帧绑定（step/full-run + 两个 slot 时机）+ R4 offscreen (self) owner + R5 (OffscreenOwner)——三者合起来恢复 RayMMD 参数系统。
4. R8 staged-clear、R7 失败拒绝、R6 shared RT（先 IDA 定谳）。
5. 渲染 P2 两项（edgeColor[3] 门、删 SetMaterial）与 displayRootBone P1。
6. 其余 P2/P3 按需。

---

## 修复状态（2026-09-15 修复波，全部先 IDA 核验后实施）

### 已修复（每项均由独立 agent 以"核验→1:1 修复"流程完成）

| 项 | 状态 | 修复要点（核验补充的事实） |
|---|---|---|
| R1 通配符/self | ✅ 已修 | 找到原版行查找 sub_18002ACE0 + 自写递归 glob sub_1800676F0：只匹配 basename+扩展名（从不匹配全路径）、glob 大小写不敏感、`self` 大小写敏感 4 字节 + 与 owner 纯指针相等、`\` 转义、first-match-wins。已 1:1 移植 MmeWildcardMatch；保留全路径精确分支为防回归的故意分歧 |
| R2 Draw=Buffer quad | ✅ 已修 | 0x18005A9C0 全函数照抄：FVF 274、TRIANGLEFAN、NDC±(1+2/W,1+2/H) 半像素 UV quad（V0=(1,-1)/uv(1,1)…V3=(1,1+2/H)/uv(1,-1/H)）、stride 32；状态保存（decl/stream0/indices/RS×4）→ 清零（含 FILLMODE 字面置 0）→ 绘制 → 恢复；失败路径不恢复状态（runFailed 锁存怪癖）；null effect 返回 S_OK。核验纠正审计两处转录误差（GetVertexDeclaration/GetIndices 槽位对调、0x1800B3AA0={0,0,1,1}） |
| R3 scene 每帧绑定 | ✅ 已修 | 厘清原版 host=载体 ModelData+8 穿透结构：sub_18005A020（step/full-run 的 B340 族，runFailed 门之前）与 slot-0/slot-1 的 B5B0 族（Begin 之后、SetFVF/BeginPass 之前）；resume/finish 不重绑（0x18005a6c9 仅发布载体上下文）。落地为 sceneWalkCarrier 窗口 + 统一绑定器 |
| R4 offscreen (self) | ✅ 已修 | 核验定谳：`(self)` = setter+0x10 = **正被绘制/分配的对象**（offscreen 行分配时即行匹配到的 model，非行 owner）。窗口内以 model 为 self 源重求值 |
| R5 (OffscreenOwner) | ✅ 已修 | 核验定谳：= setter+0x18 = 声明该 offscreen 的效果所挂对象（ray.x），仅 offscreen 遍窗口内非空（原版 ctx+0xB8 map 记录+0x30 承载），窗口外落 not-found 默认。复用 R1 的 owner 反查，不加平行结构 |
| R6 shared RT 覆盖 | ✅ 定谳一致，无需修 | 五条指令级证据：原版同为 last-writer-wins 无条件覆盖（分发无预检/创建门仅为抛弃型探针标志/创建函数无 GetTexture 回退/尾部无条件 SetTexture/进程级 EffectPool）。附带发现：原版有抛弃型尺寸探针加载（CEffect+0x3A=1）未移植，记录为独立对齐项 |
| R7 失败拒绝 | ✅ 已修 | 0x18000BC90 拒绝链 + 0x18000B880 弹窗文案（"Failed to load effect file:" + path + 整个 a1+0x70 日志）对齐：SasParse nullptr 与 SasHadErrors 两分支均写 errorText、卸载、不缓存成功。SasParse 增 outFailureLog 参数带出硬失败的确切错误行（逐字 1:1），占位文案仅兜底 |
| R8 staged-clear | ✅ 已修 | sub_18005AEE0：scene 类（+0x364==2）时 E4F0/E570 把 ClearSetColor/ClearSetDepth **运行时值**登记 per-RT map；两个 slot 的 E5D0 **仅 erase color map**（depth 永不 erase）且在 Begin 之前。注解值登记（MME_PassRecordApply）是独立写点，保持不变 |
| 渲染 P2×2 | ✅ 已修 | 门改 record.edgeColor[3]（0x4D8593/0x4D839E，mat+1208=EgColor.w 同源）；删效果路径 SetMaterial（sub_7FF7CB4D87C0 调用面清点零 SetMaterial，残留材质是 MME LIVE GetMaterial 合成数据源） |
| PMX/VMD×5 | ✅ 已修 | displayRootBone 按首条目 type==0；facialFrameCount 无 special 门（字节截断照抄）；facial 组遍历全部帧（>255 加防溢出钳制，唯一有意偏差——不复刻原版堆溢出）；matCount==0 时 materialIndex=0；VMD 失败对话框空标题 |
| 精度链 | ✅ 已修 | 相机/灯光/重力轨道 + PmdEdgeDistance 全转 float；噪声迭代数精确 `(int)((float)(cur-prev)*t)`（0x48A4DE mulss+cvttss2si）。新发现遗留：frame 变量本身原版为 float 域（0x489A81..0x489B26），我们的帧修整/游走比较仍 double——后续项 |

### 构建验证
- ~~Wave1/Wave2 后"双绿"~~ **作废**：当时命令为 `cmake --build build-x64 | tail`，管道退出码掩盖了真实失败（build-x64/build-x86 顶层不是有效 CMake 树，真正的 conan 树在 build-x64/build/、build-x86/build/）。
- **终验（真实）**：`cmake --build build-x64/build --config Release` 与 `build-x86/build --config Release` 均以退出码 0 完成，MMDxShow.dll / MMEffect.dll / mikudancestudio_core.lib / **MikuMikuDanceE.exe** 双架构全部重新链接（产物时间戳 2026-09-15 15:25）。仅一条既有警告（subrecord_layout.hpp:220 C4200 零长数组，非本轮引入）。

### 实测回归修复（2026-09-15 15:39，用户拖入 ray.x 报错后定位）

R7 拒绝链启用后暴露两个**一直存在**的扫描路径 bug（正是此前"RayMMD 加载成功但渲染不对"的直接根因——ray.fx 的脚本命令从未被识别执行）：

| 项 | 根因 | 修复 |
|---|---|---|
| [P0] 全部 SAS 命令报 "unsupported script command" | `sas_exec.cpp` tokenizer 保留命令原文参与小写字面量比较（移植注释错误认定原版不做 tolower）；原版 FUN_180018200 在 0x180018d50 经 sub_1800231B0（C-locale std::transform tolower）对命令 token 小写化后才 dispatch | tokenizer 提取 m[4] 后 ASCII tolower；value 保持 raw（参数名查找大小写敏感、枚举比较本就 case-blind） |
| [P0] 每个 pass 报 "vs_3_0 or ps_3_0 may not be used with any other shader versions" | `sas_interpreter.cpp` 混版本检查未做 16 位截断；原版 MME_SasScanPass 0x180017bb1/0x180017bbf 用 movzx eax,ax 截断（vs_3_0=0xFFFE0300/ps_3_0=0xFFFF0300 → 都是 0x0300 相等合法），谓词 (vs>=3.0||ps>=3.0)&&vs!=ps | 版本取 `(unsigned short)` 后比较，谓词照抄 0x180017bc6 |

- 已知保留分歧：原版该错误会弹去重 MessageBox（0x180017ef8，byte_1800D99D8 门控），本移植仅写日志（UI 归宿主）。
- 构建验证：x64+x86 Release 重建通过（MMEffect.dll 时间戳 15:39，0 error）。

### 遗留（未修，按优先级）
1. F5 新发现：timeline 帧域 double vs 原版 float（0x489A81..0x489ACF、0x489B26）——双重取整角落可差 1 ulp。
2. F7 附带：抛弃型尺寸探针加载（CEffect+0x3A=1，即载即毁只为提取视口宽高/AA）未移植。
3. audit8 遗留 P2：效果热重载（100ms stamp 轮询）、加载成功记录视口宽高、附件属性表 128 上限。
4. F9 残留：同一 .fx 兼任正式对象效果与 offscreen 子效果时参数覆写顺序为绘制序而非原版遍序（别名共享效应极罕见）。
5. F2 残留：全路径精确匹配分支为防回归保留（原版绝不匹配带路径行键），要 1:1 收紧可删。
6. 建议 RayMMD 实测验证链：DirectionalLight 光位、带阴影光源投影方向、LightMap/PSSM 内容、ray_controller morph 拖动响应。
