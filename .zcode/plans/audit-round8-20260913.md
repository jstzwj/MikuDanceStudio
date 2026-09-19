# 第八轮全量审计（2026-09-13）：MMD/MME 一比一符合度核查

审计方式：7 个并行子代理，对照四个原版二进制（MikuMikuDanceE_v932x64.exe、MMEffect.dll v0.37 x64 EN、MMHack.dll、d3d9.dll 代理）逐函数反编译核查工作树现状。子代理已向各 .i64 沉淀重命名与审计注释。

## 总判定

主体高度忠实（大量逐指令/逐位级一致，无 P0），但**不是严格 1:1**：已证实 **8 处 P1** 级行为偏差、约 15 处 P2、若干待确认项。

## P1 清单（已证实、两边有证据）

| # | 模块 | 差异 | 移植证据 |
|---|---|---|---|
| 1 | MME/SAS | STANDARDSGLOBAL 含 Script 注解时原版清空 techniqueOrder（0x18000D072），未列出的技术对选择器不可见；移植保留声明序全表 | sas_interpreter.cpp:2408-2413, 1322, 2769 |
| 2 | MME/SAS | 技术/pass 脚本命令分派原版大小写敏感（命令不做 tolower，仅 STANDARDSGLOBAL 编译器做）；移植统一小写化 | sas_exec.cpp:607 |
| 3 | MME/绑定 | "Time" 语义应绑 D9904(frameTimeBase，编辑模式=墙钟秒)；移植绑了 D98FC(lastFrameTime)，编辑模式下时间冻结 | material_bind.cpp:1382 |
| 4 | MME/绑定 | 语义匹配原版全程 _stricmp 不敏感；移植矩阵族/MATERIAL 族/TIME 等用区分大小写 ==，小写语义名静默不绑 | material_bind.cpp:1259-1466 |
| 5 | MMD/PMX | 附加 UV1-4 morph 基线表：原版每族取 additionalUvByComponent SOA 块；移植所有族统一取 uv.xy+uvMorphBaseZW | pmx_load.cpp:1268-1272 |
| 6 | MMD/物理 | 重力 y：x64 原版 -98.0f(0xC2C40000)；移植 0xC2C3FFFF（-97.99999237，x86 残留），1 ULP 系统性偏差 | scene_create.cpp:133-134 |
| 7 | MMD/渲染 | 球面/toon TCI：原版全二进制无 0x30000，一律 0x10000；移植 render_states/model_renderers 写 D3DTSS_TCI_CAMERASPACENORMAL(0x30000)，accessory.cpp 又是 0x10000，内部自相矛盾 | render_states.cpp:111-112, model_renderers.cpp:1306-1307, 484-577 |
| 8 | MMD/渲染 | 背景图片/AVI 四边形：原版 TRIANGLELIST × 6 顶点（v2==v3）；移植 TRIANGLESTRIP 只消费 4 顶点 → 背景只画出半个屏幕 | model_renderers.cpp:1257 |

## P2 清单（择要）

- MME：sas_interpreter 表A最后一道门 Flags/Elements 待定夺（0x18000E66C）；表B分支移植多加数组拒绝；SasNormalizeScriptExec 删全部空白 vs 原版仅折叠分号。
- MME：DifColor/SpcColor/EgColor 固定管线合成路径缺失（0x18005F830 固定管线路径先取色再乘/加合成，待确认）；About 作者名 GBK vs Shift-JIS；callbacks/material_bind/mme_dlg 残留自加调试日志；OnLostDevice 未释放 ctx+0x18 cachedTargetSet（待确认可达性）。
- 集成桥：原版 Present 拦截每帧触发 MME OnLostDevice/OnResetDevice（Reset 反而纯直通）；内置版仅真实 Reset 触发——常规场景预计无影响，建议长播放/AVI 输出动态验证 RT 残留。Ctrl+Shift+E 屏蔽缺失；模型路径 SJIS 双重编码有损。
- 模型IO：bone/UV morph 基线表 count 未按原版加载期去重；PMX 主/边 VB 用 SYSTEMMEM（原版 MANAGED，PMD 侧正确）；PMM 版本字段用缓冲区头基准而非 strstr 匹配点；VMD 加载路径 CP_ACP 有损（原版 x64 直接宽路径）。
- 动画：morph 骨骼四元数重缩放用 double 链（原版 x64 全单精度 acosf/sqrtf/sinf/cosf）。
- 渲染：线批前缺 SetTexture(0,null)（0x7FF7CB44AF3A）；toon 回退默认表 n/256 近似（原版精确小数 0.8/0.98/0.9/0.62/0.99/0.937/0.921/…）。

## 待确认（下轮补验）

- 渲染：cameraGate [13E4]>=2 语义映射；固定帧模型 pass WORLD 来源；材质混合三分支与阴影图斜率公式逐指令；device_reset 流程 x64 定位。
- 动画：CreatePhysJoint(6DofSpring) 参数映射 x64 对照；SDEF/BDEF4 worker 加法序位级；readback (dist−limit≥2) 传送门数学。
- MME：资源对象构建（0x180011960/0x1800143D0）；快照族（0x180001880 族）；anime_texture 帧时间线/APNG。
- pmm_load_v1/v2、pmm_save、三个大命令家族文件（frame_edit/view_menu/file_menu）仅抽查通过，建议抽样加密。

## 模块判定汇总

| 模块 | 判定 |
|---|---|
| MME ini/emm 读侧、SAS 执行器运行期、pass_planner 核心链 | 严格 1:1 |
| MME STANDARDSGLOBAL 扫描/编译器、emm 写侧、effect_engine、mme_context、mme_dlg、anime_texture、callbacks | 基本一致（含 P1/P2） |
| MME material_bind | 部分偏差（2×P1+1×P2） |
| 集成桥（mmhack getters、BeginScene 状态机、DIP/DP 捕获、d3dx9_dyn、effect_api） | 高度等价（P2 级通知面差异） |
| PMD 加载、path_resolve | 严格 1:1 |
| PMX 加载 | 基本一致（1×P1+2×P2） |
| VMD/PMM/VPD/VSQ | 基本一致（文档化编码偏差） |
| 骨骼/蒙皮/关键帧/物理核心 | 严格 1:1 至基本一致（1×P1 重力常量） |
| frame_scene 帧编排、d3d_init、capture_downsample | 严格 1:1 |
| model_renderers/toon/render_states | 基本一致（2×P1+2×P2） |
| ui_hscroll、command_dispatch、dialog_procs、播放追赶、撤销环 | 严格 1:1 |
| 其余 UI/命令/帧驱动 | 基本一致（仅 P3） |

---

# 附：七份子代理完整报告

## 报告一：MME SAS/EMM/Pass 规划

审计基准:MMEffect v0.37 x64 English(MD5 6a5e133b…,基址 0x180000000),按工作树现状。本轮反编译核对了 0x18000C470、0x1800169D0、0x180018200、0x18001B7B0、0x18001BBC0、0x18001C760、0x18001DB50、0x180034390、0x180047BD0、0x180049EB0(定点)、0x18004B6E0、0x1800564A0、0x18005B9E0(尾部)、0x18005C970、0x18005D130,并对 0x18000E4xx-E6xx 做了指令级核对。已向 IDA 沉淀 19 个函数重命名与 6 条审计注释(含两个 P1 证据点)。

### A. 模块判定表

| 子模块 | 判定 | 说明 |
|---|---|---|
| ini_file(47BD0 驱动/492E0/true 比较) | 严格 1:1 | 四个 regex 的行为(含 section 先于 pair、拒绝行格式、BOM、验证器总结日志)与直写实现逐点吻合 |
| emm_manager 读侧(结构/行门/验证器/错误恢复) | 严格 1:1(抽样) | 验证器顺序、Version 先存后判(0x18004A02B)、错误串归属全部吻合;2E8D0 双报告串位置吻合(未逐行核对 0x1CB4 字节的全部分支) |
| emm_manager 写侧(EMM/EMD 保存) | 基本一致(细微偏差) | 头注释已声明的省略:不输出 [Effect@base(n)] 属主节、.show 行合成策略、无对话框直写。属结构性/文档化差异 |
| sas_interpreter:STANDARDSGLOBAL 扫描 | 基本一致(细微偏差) | 版本/Output/Class/Order/脚本规范化与 Technique= 链全部吻合;缺 Script 存在时的 techniqueOrder 清空(见 P1-1) |
| sas_interpreter:参数校验(表A/B) | 基本一致(细微偏差) | 全部类/型/数量例外逐条吻合;最后一道门(0x18000E66C)字段归属待确认(见 P2-1) |
| sas_interpreter:资源对象/纹理创建(11960/143D0) | 未完全验证 | 本轮未反编译(超预算);代码内引用地址自洽,且同类转录错误率低,列为待下轮 |
| sas_interpreter:技术/通道扫描(169D0) | 严格 1:1 | MmdPass 值、Use* 字节、Subset 状态机(含哨兵对)、-256 记录头、ValidateTechnique 标志全部逐条吻合 |
| sas_interpreter:选择器(1DB50) | 严格 1:1 | sas+0xF8 迭代、有序子集范围、Use* 通配、SkipValidation 选 +9、last-invalid 回退、found 语义全部吻合 |
| sas_exec:编译器(18200) | 基本一致(细微偏差) | 规范化/分词/全部命令分支/类型检查/错误继续编译/语法错误尾部吻合;命令分派大小写与空白处理有偏差(见 P1-2、P2-2) |
| sas_exec:运行期(1BBC0/1B7B0/1C760/BF80) | 严格 1:1 | 步进/恢复、循环栈(回跳=idx+1、空栈写 0)、恢复尾声(仅 mask≠0)、c760 全部 case(含 case6 的 ARGB 截断打包、case8/9 的 flags 1/6)、错误路径均吻合 |
| pass_planner:b9e0 尾部/c510/d130/c970 | 严格 1:1 | repeat=N+1(空或禁用→1)、d130 全链(e210→step→repeat0 恢复缓存集→c510→EndScene→绑定→0xA1→c970→BeginScene)、c970 的 RT0-only/无条件深度/注解驱动 Clear/暂存门全部吻合 |
| pass_planner:快照族(1880/1E70/2100/2440/da50/e210 深层) | 基本一致(以引用自洽+外围吻合判定) | 未逐指令核对;其外围调用链(d130/c970/b9e0)全部吻合,端口注释引用的地址自洽 |
| sas_exec:标准变量(LightingColor0..2/TimeSpan/ElapsedTime/ControlPoint/UsingShareFile) | 缺失(本组文件内) | 属 material_bind/effect_engine 侧;5 个受审文件不承载,不在本轮范围 |

### B. 发现清单(按严重度)

**P1-1 STANDARDSGLOBAL Script 存在时未清空 techniqueOrder(技术选择顺序不同)**
- 原版证据:0x18000D072 调用 sub_180034390(已重命名 MME_VectorClearTrivial,即 vector::clear:memmove 0 字节 + end=begin)清空 sas+0xF8;该调用仅在 Script 注解读取成功分支内。随后 Script=Technique= 名单 push 进同一向量。选择器 0x18001DB50 的 v11/v12 = a1[31]/a1[32],只迭代该向量——Script 存在时未被列出的技术对选择器完全不可见。
- 移植证据:sas_interpreter.cpp:2408-2413(先 push 全部声明序)+ 1322(追加 Script= 名单),SasSelectTechnique(2769)迭代全部。
- 差异:含未列出技术的效果(如声明序里靠前的 object 辅助技术 + Script="Technique=Main")在原版对 object 绘制回退到宿主管线,移植却选中该未列出技术并绘制。头部注释"Script= order first, then remaining declaration order"与二进制不符。注意:仅无 Script 注解的效果两者一致(声明序)。

**P1-2 技术/pass 脚本命令分派大小写敏感(移植做了小写化)**
- 原版证据:0x180018200 的命令比较是原始文本 memcmp/memcmp 风格("pass"@~0x18001896C、"rendercolortarget"系列、"clearsetcolor"@~0x18001A0xx、"draw"、"scriptexternal"、"loopbycount/getindex/end"、"renderport"),函数内唯一的 ctype tolower(sub_1800231B0)只作用于 VALUE(槽位 5→v218),命令(槽位 4→Buf1)不经小写化;调用方 0x1800169D0 LABEL_194 以 std::string::assign 原样传入脚本文本。对照:STANDARDSGLOBAL 编译器(0x18000C470)对命令确有 tolower(sub_1800233C0)——两处语义不同,移植混用了后者。
- 移植证据:sas_exec.cpp:607 `SasCompileToken(c, ToLowerAscii(command), value, whole, out)`。
- 差异:DXSAS 风格大写脚本("Pass=P0;")原版走"Warning: unsupported script command"且不产生任何命令,移植则正常编译执行;反之亦然(该类脚本在原版 MME 中本就不工作,移植更宽容——但对"行为严格一一对应"目标是偏差)。

**P2-1 表A校验最后一道门的字段:desc.Flags vs pd.Elements(待确认)**
- 原版证据:0x18000E66C `cmp [rbp+0C70h+var_C10],0` + `cmp ecx,33h/jnz 错误`。var_C10 是 {Class(var_C20), Type(var_C1C), Rows(var_C18), Columns(var_C14)} 连续溢出槽之后第 5 个 dword;按 D3DXPARAMETER_DESC 字段顺序其后应为 Flags(D3DXP_LITERAL)。但该槽也可能是编译器重排拷贝的 Elements——栈布局无法 100% 定夺。
- 移植证据:sas_interpreter.cpp:1226-1228 `pd.Elements != 0 && e.id != 0x33`。
- 差异:若原版是 Flags,则带语义的数组参数(如 `bool use_texture[2]`)原版接受、移植报"type of parameter '…' is invalid."并置 hasErrors;literal 参数则相反。两者对常规效果均无影响。

**P2-2 表B分支移植附加了数组检查**
- 原版证据:0x18000C470 表B分支(LABEL_458 起)只有 class/type/count 精确比较 + Rows 形状检查 + id 0x3F-0x42 报错,无任何 Elements/Flags 门。
- 移植证据:SasValidateAgainstEntry(sas_interpreter.cpp:1226)对 strict(表B)同样执行数组拒绝。
- 差异:名字表命中的数组参数(如 `float4 EgColor[2]`)原版通过校验并注册,移植静默落回纹理检查(BOOL/FLOAT 非纹理→不匹配→无操作);use_* 四键则直接报错置 hasErrors。罕见路径。

**P2-3 18200 规范化移除了全部空白(词内空白行为不同)**
- 原版证据:0x1800B46B8 `\s*(;\s*)+`→";"(仅折叠分号串)+ `^;`→"";token 正则 `^\s*((\w+)\s*=\s*([^=;\s]*))\s*;\s*(.*)`——value 类 `[^=;\s]` 不含空白,含空白的 value 或命令词内空白 → 正则失配 → "Error: script syntax error"。('=' 两侧的空白两边都接受。)
- 移植证据:sas_exec.cpp SasNormalizeScriptExec(150-177)无条件剔除所有 isspace 字符。
- 差异:value 含空白的脚本(如 "RenderColorTarget=tex ture;")原版报语法错误终止编译剩余部分,移植拼接后查找参数名(通常 unknown texture name 报错,但错误行不同;若拼接后恰有同名参数则行为实质不同)。命令词内空白同理。

**P3(无害/已文档化,列出备查)**
1. RT0 未绑定回退面:原版用进程级 16×16 全局 qword_1800D9A38(0x18001C760),移植每次新建(0x18001c970 处证据;观察等价)。
2. 移植在重定向时懒捕获 savedColor(sas_exec.cpp:942-945),原版仅 ctor 捕获(0x18001B7B0)——仅 ctor 捕获失败时有别。
3. 恢复尾声对 RT0-null:原版仍调 SetRenderTarget(0,NULL)(D3D9 拒绝、返回值被弃),移植跳过该调用——设备状态等价。
4. clear= 走宿主派发对象(0x18001C760 LABEL_222 经 a5 vtable+16),移植直调 device->Clear——文档化结构差异,flags/颜色/z 语义已核对等价。
5. 渲染目标失败报告:原版发全局 sink + 去重 MessageBox(0x18001C760 LABEL_44),移植进效果日志——文档化;消息文本布局(技术名的拼接位置)可能略有出入,未逐字节比对。
6. c970 视口 W/H 来源:原版读 wrapper+0x40/+0x44 暂存值(0x18005C970),移植从离屏表面 desc 推导——移植已标 UNCERTAIN,两者在"wrapper 暂存表面尺寸"假设下等价。
7. "空技术(0 pass)不绘制"约定:移植在 SasExecuteTechnique 拦截;原版机制未在本轮定位(169D0/1DB50 均不检查)——待确认,风险低。
8. EmmFile IniFile::Load 的打开失败日志(flag 1):原版 Initialize 仅在 access() 通过后才加载 MMEffect.ini,若移植调用方未做存在性预检会对缺失 ini 弹出原版没有的错误框——属调用方(不在本 5 文件内),待确认。

### C. 已验证一致项简表

| 验证项 | 原版函数/地址 |
|---|---|
| IniFile 驱动全链(rt 打开/0x400 fgets/BOM/注释/修剪/节先值后/拒绝行 "(line N):\n 'raw'"/EOF 验证器+总结/a1[8] 日志门) | 0x180047BD0 |
| "true" 精确比较、[System] EMMAutoSave 默认 1/SkipValidation 语义 | 0x1800564A0 |
| EMM 验证器顺序与全部错误串(Info→Version→Object→Effect→Default/Owner→unknown object)、280 字节节记录 | 0x18004B6E0 |
| Version 越界先存后拒(MessageBox 后加载仍可成功) | 0x180049EB0 @0x18004A023-0x18004A038 |
| EMM/EMD 两报告串归属 | 0x18002E8D0 / 0x180031980 |
| STANDARDSGLOBAL:版本 0.8 门/%g 文案/Output=color/Class/Order 值映射(sas+0x30/+0x34)/`\s+`→"" 规范化(与 18200 不同)/命令 tolower/A?B:C 链/语法错误致命 | 0x18000C470 |
| 表A/B 全部例外(0x1E 标量、0x28 标量/矩阵/BOOL、5←7、float4←float3、0x30-32 任意维、矩阵 4 行/向量 1 行、0x3F-42 报错、纹理 5/7/8/9→0x2C) | 0x18000C470 @0x18000E4E9-0x18000E804 |
| 技术扫描:-256 头、MmdPass{object0,object_ss1,edge3,shadow2,zplot4}、Use* GetBool→+5/6/7、Subset 状态机(全消费空集哨兵 {MAX,MAX})、编译错误 OR 进 v6 | 0x1800169D0 |
| 选择器:仅迭代 sas+0xF8、有序范围提前 break、Use* 有符号通配、SkipValidation→record+9、首个有效即返、末个无效回退+found 语义 | 0x18001DB50 |
| 编译器:规范化/tokenizer m[3..6]/全部命令 id(rt0-3=1-4,depth=5,clearsetcolor=6/depth=7,clear color=8/depth=9,draw g=10+b=11,external=12,loop=13/14/15)/元素下标 [0-5] 立方体面/类型门(Class≤1&&Cols==1&&Type∈123;VECTOR×4)/错误继续编译/尾部截断 | 0x180018200 |
| 运行期:ctor 捕获 4+深度+视口、当前视口全拷贝、GetClearColor/1.0 初值;步进/恢复/ScriptExternal 存下标返回/循环帧回跳 idx+1/空栈索引写 0/恢复尾声仅 mask≠0/sas+0x39 锁存 | 0x18001B7B0、0x18001BBC0 |
| c760 全部 case:2D/立方体面表面获取、null 重置路径(重绑保存槽;RT0 空保存→合成 1;RT0 空纹理→16×16)、视口仅槽 0 改 XYWH/重置全拷贝、每次切换后 SetViewport、mask 置/清、深度注册表(count/operator[]/null→绑 NULL)、case6 clamp+截断 ARGB 打包、case7 原样、case8/9 flags 1/6+staged 值、code==1 无 [%08X] 后缀 | 0x18001C760 |
| b9e0 尾部:repeat=N+1/空或禁用→1 不调宿主;视口变更存储+0x18A 置位 | 0x18005B9E0 @0x18005C343-0x18005C3CA |
| d130 全链:e210→list[repeat].step(vtable 0x80)→repeat==0&&mask→恢复缓存集→c510→EndScene(0x150)→绑定 list[repeat-1]→GetRenderState(0xA1)→c970→BeginScene(0x148) | 0x18005D130 |
| c970:门=adaptive&&AntiAlias&&!失败锁;仅槽 0 SetRenderTarget;无条件 SetDepthStencilSurface(配对 D24S8);Clear flags=Z|T(hasClearColor)|S(hasClearDepth);staged 值同门 | 0x18005C970 |

**总结**:五文件整体转录质量很高——本轮逐条核对的所有运行期语义(pass_planner 核心链、sas_exec 执行器、ini/emm 驱动与验证器)均为严格 1:1。需要处理的是两个 P1(techniqueOrder 清空缺失、脚本命令大小写),二者都只影响特定输入的效果文件,但对"行为严格一一对应"的目标是真实偏差;P2 三项均罕见路径(其中 P2-1 建议下轮用动态或更深的静态手段定夺 0x18000E66C 读的是 Flags 还是 Elements)。资源对象构建(0x180011960/0x1800143D0)与快照族(0x180001880 族)本轮未独立反编译,留待下轮。

## 报告二：MME 效果引擎与绑定

### A. 模块判定表

| 模块 | 判定 | 依据摘要 |
|---|---|---|
| effect_engine.cpp/.h | 基本一致 | 加载器宏/flags/错误路径/池初始化逐指令核对一致;戳算法与缓存字段有轻微偏差 |
| mme_context.cpp/.h | 基本一致(结构性重构=设计决定) | 注册/注销/资源释放路径一致;OnLostDevice 缺一项释放(见 B-6) |
| material_bind.cpp/.h | 部分偏差 | 分发 id→槽位映射核对一致;2 处 P1(Time 全局、大小写)+1 处 P2 待确认 |
| model_data.cpp/.h | 一致(抽样) | 伪骨骼表/名称表/材质名流程与 0x180058c70 证据链吻合(本轮未重汇编) |
| anime_texture.cpp/.h | 基本一致 | 错误字符串、GDI+ 流程、"Speed"/"Offset" 注解核对一致;时间线/APNG 未逐指令验证 |
| callbacks.cpp | 基本一致 | 11 个导出全部反汇编逐序核对;存在移植自加调试日志(B-5)与 P3 顺序差 |
| render_snapshot.cpp/.h | 一致(抽样) | 快照捕获/绑定上下文与 0x180059c60/0x180059ba0 注释证据链吻合 |
| mme_util / mme_log | 一致 | sub_180009080 完整反汇编逐行为核对一致 |
| mme_globals.cpp/.h | 一致 | D98F8/D98FC/D9900/D9904 全局引用位置与帧时机一一对应 |
| mme_ui.cpp | 部分偏差 | 菜单/钩子/热键一致;About 编码不同(B-4);自述扩展 40020 |
| mme_dlg.cpp | 基本一致 | 字符串/行模型/OK-Cancel-Apply/EMM 保存调用图一致;自加日志行与 default 行扩展(B-5/B-14) |

### B. 发现清单

#### P1

**B-1. "Time" 语义绑定了错误的全局变量(编辑模式行为分歧)**
- 原版证据:语义表(0x1800b2fa0,步长 32)中 0x22→"Time"(字符串池化于 0x1800b2ccc,"ElapsedTime" 尾部)、0x23→ElapsedTime、0x24→Time2、0x25→ElapsedTime2;绑定函数 sub_180057B30(0x180057b30-0x180057bb7):`id==0x22→movss xmm2, [0x1800D9904]`(frameTimeBase,编辑模式下=墙钟秒)、`0x23→D9900`、`0x24→D98FC`、`0x25→D98F8`。
- 移植证据:material_bind.cpp:1382 `sem == "TIME"` → `effect->SetFloat(h, g_lastFrameTime)`(g_lastFrameTime=D98FC)。D9904 对应移植的 g_frameTimeBase(mme_globals.h:92)。
- 差异:播放模式两者等价;**编辑模式**下原版 Time 持续走秒表,移植冻结在宿主帧时钟。ElapsedTime/Time2/ElapsedTime2 三项移植正确。移植 1379 行注释"TIME (semantic 0x24)"的 id 也是错的(0x24 是 Time2)。

**B-2. 语义匹配大小写敏感(原版全程 _stricmp)**
- 原版证据:sub_18000C470 的语义查表循环(0x18000e481-0x18000e4a8):`lea rbx, off_1800B2FA8; ... call _stricmp`,步长 0x20,扫描到名称表前;名称表(0x1800b36a8 起)同函数另有三处引用(0x18000e449/0x18000e80a/0x18000ea29),同为不敏感匹配。
- 移植证据:material_bind.cpp:1259-1466 对矩阵族、POSITION/DIRECTION、DIFFUSE/AMBIENT/SPECULAR/EMISSIVE/TOONCOLOR/EDGECOLOR/SPECULARPOWER/GROUNDSHADOWCOLOR、TIME/ELAPSEDTIME、VIEWPORTPIXELSIZE、MOUSEPOSITION 用区分大小写的 `std::string ==`;仅 MaterialTexture/Time2/ElapsedTime2/鼠标/Adding·Multiplying 族用了 `_stricmp`。
- 差异:fx 中声明小写语义(如 `: worldviewprojection`、`: diffuse`)原版会绑定,移植静默跳过。混合处理还导致同一 fx 内不同语义行为不一致。

#### P2

**B-3. 遗留名称参数(DifColor/SpcColor/EgColor)固定管线合成路径缺失(待确认)**
- 原版证据:sub_18005F830(0x18005f830)id 57/58/59(=EgColor/SpcColor/DifColor 名称):效果路径(快照+85 非零)直接 SetVector 快照 +0x198/+0x1B8/+0x1C8(与移植一致✓);**固定管线路径**(0x18005f986 起)先经 a1+16 对象 vtable+400/+416 两次取色再做乘/加合成(DifColor=c2.rgb*c1.rgb w=1;EgColor 含 a1+40==1 双分支与 0.1/0.03125 类系数),非原始材质色。
- 移植证据:material_bind.cpp:1112-1128 kNamedSemantics 把 DifColor/EgColor 一律路由到 MmeBindMaterialParameter(材质原始 Diffuse/Edge)。
- 差异:宿主无 .fx(MME 常态)时,使用这些遗留名称的经典 MMD 风格效果取值不同。合成公式的取色对象未完全反汇编,标待确认。

**B-4. About 对话框作者名编码不同**
- 原版证据:0x1800b5708 字节流 `... (by 95 91 97 CD 89 EE 93 FC 82 6F )` = Shift-JIS "舞力介入Ｐ"。
- 移植证据:mme_ui.cpp:341-343 使用 GBK 字节 `"\xce\xe8\xc1\xa6\xbd\xe9\xc8\xeb\xa3\xd0"`。
- 差异:CP932 主机(原 MMD 目标环境)上移植 About 显示乱码;GBK 主机则原版乱码。文本内容字节不等价。

**B-5. 移植自加的调试/诊断日志行(原版无对应字符串)**
- 原版证据:二进制字符串表无 "DrawWrap"/"SceneBinding"/"BindResolve"/"Open the effect mapping dialog"(find_regex 全量反查为空)。
- 移植证据:callbacks.cpp:234/277/286("DrawWrap: id=%I64u..."、"DrawWrap Begin ok/FAILED")、material_bind.cpp:576/612("BindResolve:..."、"SceneBinding:...")、mme_dlg.cpp:1249("Open the effect mapping dialog\n")。
- 差异:MMEffect.txt 与日志窗口内容与原版不等价(WIP 痕迹)。

**B-6. OnLostDevice 未释放 ctx+0x18 cachedTargetSet(待确认)**
- 原版证据:sub_18005E640(0x18005e655)第一步 `sub_180067680(ctx+24)` 释放 ctx+0x18 保存的目标集合,随后 sub_180001660(ctx+0x240)、sub_180055780(ctx+0x68)、逐模型 run-state 销毁(+0x358,0x18005e693-0x18005e6a4)、sub_18002DD40(效果 OnLostDevice)。
- 移植证据:callbacks.cpp:805-835 只做 run-state 销毁→MmeEngineOnLostDevice→MmeReleaseBindingContextPool(后者释放状态块/快照缓存/bgVB,不含 cachedTargetSet,见 mme_context.cpp:193-220)。
- 差异:若 pass_planner 填充了 cachedTargetSet(ctx 头注释称 ==N 分支会填),悬挂 surface 引用可能令 Reset 失败。是否可达取决于他文件,标待确认。

#### P3

**B-7. 文件戳算法不同**:原版 sub_18000B7F0(0x18000b7f0)= CreateFileA(GENERIC_READ, share=1, OPEN_EXISTING, 0x80)→GetFileTime(LastWrite) 返回 64 位 FILETIME;移植 effect_engine.cpp:210-235 用 size^mtime 32 位哈希。极端场景(同 mtime 改内容、哈希碰撞)热重载判定不同。

**B-8. 加载成功后未记录视口宽高到缓存条目**:原版 sub_18000BC90 LABEL_80(0x18000c3e0-0x18000c411)GetViewport,将 w/h(或调用者 a3/a4 传参覆盖)写入条目 +0x20/+0x24 并传给 sub_18001DD20;移植 LoadedEffect 无此字段。下游用途未查明,待确认。

**B-9. 虚构的 GetMaterial 缓存**:原版 Initialize 尾部(0x180056bcb)与 OnBeginScene(0x1800574e0 全函数)均无 GetMaterial/Diffuse.b·a 强制;移植 callbacks.cpp:544-552(Initialize)与 669-677(OnBeginScene)各做一次。且 mme_globals.h 把 g_cachedMaterial 和 g_beginViewport 都标注为 DAT_1800d9878——原版 D9878 实为 GetViewport 目标。无消费者,纯文档/无害偏差。

**B-10. OnResetDevice 失败消息去重机制不同**:原版(0x180058b4a-0x180058bf7)D99D8 门控 set 查找/插入(阶段外无条件显示);移植 callbacks.cpp:881-885 用静态字符串锁存,第二次失败可能被吞。另原版 CreateRenderTarget 前不释放旧指针(移植多一次 Release,无害,因 OnLostDevice 已置空)。

**B-11. 主窗口子类化时机**:原版在 Initialize 内、菜单安装(sub_180055890)**之前**(0x180056873-0x18005687a);移植在 MmeUiInstallHooks 内菜单插入之后(mme_ui.cpp:529-531)。初始化期间消息时序略异。

**B-12. 设备丢失释放顺序**:原版 E640:目标集合→0x240 管理器→状态块池→run state→效果;移植:run state→效果→状态块池(+0x240)。全部在 Reset 前完成,可观察性低。

**B-13. OnBeginScene 多余 GetMaterial(同 B-9);Initialize 少一次 GetViewport 种子**(原版 0x180056bd5 有,移植无)。

**B-14. mme_dlg 两处自述扩展**:(a) 40020(EN 菜单 Enable Effect id)移植保持功能(mme_ui.cpp:372-377 自述原版 dispatch 忽略);(b) 40007/按钮 1007 对 "(default)" 行也启用且可写默认效果(mme_dlg.cpp:646-648、438-441 自述原版 FUN_180041620 的 tab-rect 门把 default 行排除在收集外)——有意偏离,原版行为待确认。

**B-15. mme_ui 安装守卫差异**:原版 FUN_180055890 无空主窗口守卫(解引用 DAT_1800d9b00),0x104 语言探测失败时跳过安装;移植 MmeUiInstallHooks 加 null 守卫并继续安装(mme_ui.cpp:470-494 自述)。防御性偏差。

### C. 已验证一致项(简表)

1. **引擎初始化** sub_18000A8E0:teardown→Release pool/surface→D3DXCreateEffectPool→CreateRenderTarget(16,16,fmt 22=X8R8G8B8,MS0,lockable0,池共享句柄 null)→mip 锁存仅双成功(effect_engine.cpp:117-143 逐参数一致;"0x16 UNCERTAIN"注释可定案为 22)。
2. **加载器** sub_18000BC90:先卸载→FILETIME 戳门(0=静默失败)→"Loading effect file: "+路径→getcwd/splitpath/chdir 目录切换→宏 {"_INDEX","PSIZE15"}+条件 {"MME_MIPMAP",""}(byte_1800D99DA)→flags=0x1000|(byte_1800D99DE?0xC0:0)→D3DXCreateEffectFromFileW(共享池 qword_1800D9A30)→失败恰附加一段(编译文本 或 "DirectX Error: <desc> [%08X]\n"," ["/"]\n" 拼接顺序一致)→成功时错误缓冲未读即释放→随后 SAS 解析链。
3. **OnBeginScene** 全序:SavedPMMFile 循环→LogFlush(1)→帧时间→GetViewport+零宽高矫正→drawn 窗口子类切换(==主窗口置空/旧过程还原)→WORLD→Inverse→VIEW→world*view→PROJ→(world*view)*proj→GetLight(0)→RebuildRenderPassPlan→insideModelDraw=0。
4. **OnResetDevice**:sub_18002DE40→CreateRenderTarget(16,16,22)→首非零失败链→"done."/"failed."→ExpGetEnglishMode()==0 选日文消息→D99D8 门控去重→MessageBoxA(main,…,MB_ICONERROR,"MikuMikuEffect")。
5. **OnLostDevice**:"Resetting MME..."→E640(含逐模型 run-state 销毁,证实移植做法)→A990→释放 16x16 表面。
6. **Initialize**:IsDebugMode/EMMAutoSave=1/exe 目录与 ini 路径/"true" 精确比较的 EMMAutoSave·SkipValidation/GetProcAddress(ExpGetEnglishMode)/55890 失败返 1/WH_KEYBOARD(=2) 钩子/timeGetTime 基准/D9904=0/D9911=0/鼠标数组清零/GetSamplerState(1,7)/ctx new(0x480)/mgr new(0x190=400)/banner "MikuMikuEffect  ver.0.37\n\n"(双空格逐字节一致)/设备信息行/17 个参数名与顺序逐一相同/失败返 1。
7. **技术选择** sub_18001B940:subset*8+tex|sph<<1|toon<<2(mode≤1)、表越界→null、runFailed(+57) 门控、无效果/无 op→(**a7)(a7,0,0) 原始绘制——与 MmeRefreshDrawTechnique + callbacks.cpp 绘制包装一致。
8. **每 Begin 分发** sub_18001B5B0:材质语义 0x18-0x1E/0x4E(light 注解项跳过材质绑定=移植 light 分支);纹理语义 41-43;Eg/Spc/Dif 名称←快照 +0x198/+0x1B8/+0x1C8;调制族 'F'-'M':effect_file_used&&kind==1 时 +0x1D8/1E8/1F8/208、否则 Add→0.0 / Mul→1.0 splat——移植逐槽一致。
9. **语义集合**:原版实际集合=矩阵 6 族×4 变体 + Diffuse/Ambient/Emissive/Specular/ToonColor/EdgeColor/SpecularPower/Position/Direction/ViewportPixelSize/Time/ElapsedTime/Time2/ElapsedTime2/RenderColorTarget/RenderDepthStencilTarget/ControlObject/MaterialTexture·SphereMap·ToonTexture/AnimatedTexture/OffScreenRenderTarget/MousePosition·三键/TextureValue/Adding·Multiplying×4/GroundShadowColor + 名称表 17+ 项。**任务单中的 LightingColor0..2/LightDirection/LightAmbient/CameraPosition/ViewDirection/MaterialDiffuse·Specular·Ambient·Emissive·Power·Toon·EdgeColor/parMMTime/usingShareFile/TimeSpan 在原版二进制中均不存在**(字符串与表全查证);移植同样未绑定,集合一致、无缺失无多出(多出的仅 B-1 的错源 Time 已在原版集合内)。
10. **日志** sub_180009080:\n/\r 行拆分、\r\n 吞并、挂起列表、对话框镜像(GetDlgItem(1001)+EM_SETSEL(len,len)+EM_REPLACESEL)、MessageBox 仅显式请求时、D99D8=0 无条件显示否则 set 去重——MmeLogWrite 逐行为一致。
11. **卸载** sub_18000B210:`if(*a1)` 活效果才日志 "Unload effect file: "+path+"\n\n"→按 kind 销毁资源向量→Release 效果→+0x39 WORD=0x100 复位——移植门控一致。
12. **EMM 自动保存调用图**:仅 OnCreateModel/OnDeleteModel/OnBeginScene;移植对话框 Apply 不触发保存的决策正确。
13. **对话框字符串**:"(default)"/"(hide)"/"(none)"/"Map Effect File"/"Set Effect"/"Effect File"/"UserFile"/"Two or more files cannot be specified."/"Please specify .fx file or .emm file." 逐字一致;Cancel 恢复默认效果快照 + 1x1 InvalidateRect(FUN_180041550)一致。
14. **anime_texture**:错误字符串与 GIF(sub_180004810)/PNG(sub_1800068A0) 构造对应、"Speed"/"Offset" 注解名、GDI+ 走向一致(帧时间线 1/30 与 APNG 解码未逐指令验证)。
15. **Time 族全局映射**:D98F8=frameDelta、D98FC=lastFrameTime、D9900=deltaSeconds、D9904=frameTimeBase 的写入点(sub_180056F80 xref)与移植 MmeUpdateFrameTime 一一对应(Time 的错绑见 B-1)。

**结论**:无 P0。两处 P1(Time 语义错源、语义大小写)均为对特定 fx 写法/运行模式才显形的取值错误,建议优先修复;B-6 需与 pass_planner 负责人交叉确认 cachedTargetSet 的填充路径;B-5 的调试日志建议在开源前清除。

## 报告三：MME 内置集成桥接与 mmhack

审计方法:内置版 10 个文件全部精读;MMHack.dll/MMEffect.dll/d3d9.dll/MikuMikuDance.exe 四个 IDA 会话逐函数反编译验证(关键函数均到汇编级),不轻信源码注释。以下所有"原版证据"均指本次反编译独立确认的行为。

### A. 抓取与通知面对照表

#### A1. d3d9.dll 代理(原版 database=2e29e2ba)

| 原版行为 | 原版证据 | 内置版对应 | 结论 |
|---|---|---|---|
| DllMain:加载 `x64\d3d9.dll`(回落 system32)+ 同目录 `MMHack.dll` | DllMain 0x1800014b0 | 静态链接,无加载过程 | 等价(注入手法豁免) |
| 15 个导出(Direct3DCreate9/Ex、D3DPERF* 等)全部懒 GetProcAddress 转发真 d3d9 | 0x1800016ab→0x180001410 | 宿主直接用真 d3d9 | 等价 |
| 菜单注入不在代理内(在 MMEffect.dll:SetWindowsHookExA/InsertMenuItemA/GetMenu 等) | MMEffect USER32 导入面 | 桥接层供给 GetMMDMainWindow/GetDrawnWindow/IsDebugMode/ExpGetEnglishMode(均已验证) | 桥接面等价;菜单本体属引擎侧文件,不在本次 10 文件范围 |

#### A2. MMHack.dll(de1427b0)—— IAT hook 面(sub_18000AED0,共 11 项)

| 原版 hook | 原版证据 | 内置版对应 | 结论 |
|---|---|---|---|
| d3d9!Direct3DCreate9 → MyDirect3D9 包装;CreateDevice(0x180004040):hDlg 上溯根属主、hDeviceWindow(pParam+0x20)跟踪、debug 模式强制 REF+软件顶点、无条件清 0x10 行为位 | 0x180004160/0x180004040 | mme_bridge.cpp OnDeviceCreated(d3d_init.cpp:469);主窗口=WM_CREATE 句柄+帧级刷新 | 基本等价;debug 的 REF 强制未移植(见 B-7) |
| kernel32!CreateFileW → PMM 跟踪(0x1800041e0):GENERIC_READ 任意文件→currentOpenFile;.pmm+READ→LoadedPMMFile 源串(one-shot 消费 0x180001790);.pmm+WRITE→SavedPMMFile 尾插 | 0x1800041e0 | MmeHostNotifyPmmLoaded/Saved(pmm_load_v2.cpp:2609 / pmm_save.cpp:1014);one-shot 语义逐字段一致 | 等价(currentOpenFile 字段保留但无写入方,其原版唯一消费者 0x180002d40 是惰性路径) |
| d3dx9_43!D3DXLoadMeshFromXW → MyD3DXMesh 包装(0x180001920,magic -666918069)+ 网格→文件名登记(0x18000f150,ppMesh 邻域 8×32位截断键) | 0x1800043b0 | 宿主自己的网格加载;模型名改由 ExpGetPmdFilename 在 BeginScene 取得 | 等价(见 B-5 编码差异) |
| d3dx9_43!D3DXCreateEffectFromResourceA → 首个标准效果登记:清技术缓存(sub_180006350)→GetTechniqueByName(槽13)登记 12 技术(off_18005BDF0)→qword_18006E7A0 缓存,仅一次 | 0x180004440 | MmeHostSetStandardEffect(mme_host.cpp:108-129) | 逐字段等价 |
| d3dx9_43!D3DXCreateTextureFromFileExA/ExW/InMemoryEx + D3DXCreateTexture → FUN_18000f3d0 纹理缓存登记(toon 裸名别名/斜杠规范化) | 0x180004670 等 | MmeHostRecordTextureFile;调用点 path_resolve.cpp:263 + toon_textures.cpp:186 | 模型/toon 覆盖等价;MME 自建纹理不登记(B-9) |
| d3dx9_43!D3DXLoadMeshFromXInMemory →(同上网格包装) | IAT 0x1800553c0 | 不需要 | 等价 |
| user32!GetKeyState → 屏蔽 MMD 的 'E'+Ctrl(17)+Shift(16)(0x180004910,返回 0) | 0x180004910 | **无对应** | 差异(B-3) |

#### A3. MMHack.dll —— MyDirect3DDevice9 拦截槽(vtable @0x18005c058 实测)

| 槽 | 原版行为 | 内置版对应 | 结论 |
|---|---|---|---|
| 41 BeginScene (0x180002de0) | endSceneFired=0;notEditMode=sub_18000EFF0(317/417 探测+RecWindow 粘滞);frameTime=ExpGetFrameTime;惰性 Initialize(**返回 0=成功**,失败→MessageBox+effectsDisabled);live 集合→OnDeleteModel(+texRef Release)→OnCreateModel(参数6/7恒0,D3DXCreateBuffer 死代码)→材质表 FUN_18000e020→order 图→world/inv→trackedRT(借用)→subset=-1→OnBeginScene | MmeHostBeginScene(mme_host.cpp:191-311)+ mme_bridge.cpp ProbeNotEditMode | 逐字段等价(汇编级确认 Initialize 分支) |
| 42 EndScene (0x180003cb0) | endSceneFired 防重→OnEndScene→真 EndScene | MmeHostEndScene | 等价 |
| 43 Clear (0x180003a20) | GetRenderTarget 借用→trackedRT/mmeInitFlag/effectsDisabled 判直通→目标==trackedRT 且 TARGET 位→clearColor 捕获+mainTarget=1→OnClear(8参) | MmeHostClear | 等价 |
| 82 DrawPrimitive (0x180002780) | 禁用/endSceneFired 直通→复位 5 状态+effectFileUsed=0→OnDrawPrimitive→返 0 | MmeHostDrawPrimitive | 逐字段等价 |
| 83 DrawIndexedPrimitive (0x1800027f0) | GetVertexShader 判路径;对象切换时 order 图取 id/isPmd;ExpGetCurrentMaterial/Technic;DESTBLEND==2→blendMode;固定功能捕获(起始采样器=(isPmd?1:0)、COLOROP 级联、TTF==2 球贴图、TCI==0x10000→spadd==ADD 否则 cd2);效果捕获(GetCurrentTechnique→12 技术表 flags→GetTexture(1)/(texUsed?2:1)、spadd GetBool、effectFileUsed=1) | MmeHostDrawIndexedPrimitive + MmhCapture{FixedFunction,Effect}TextureState | **逐分支等价**(含全部魔数) |
| 30/32/34 UpdateTexture/GetFrontBufferData/ColorFill (0x180003bd0/c50/b50) | 目标==trackedRT 且未触发→提前 OnEndScene+endSceneFired=1 | MmeHostPreRenderTargetCopy(frame_scene.cpp:415,GetBackBuffer 前) | 等价(缺 trackedRT 比对,见 B-6) |
| 15 Reset (0x180003ed0) | **纯直通**(无 OnLost/OnReset) | device_reset.cpp:69/92 有 OnLost/OnReset | 差异(见 B-1 解读) |
| 16 Present (0x180003d30) | **OnLostDevice→真 Present→OnResetDevice**(每帧)+hDeviceWindow 回落更新 | **无 Present 挂钩** | 差异(B-1,核心发现) |
| 17 GetBackBuffer (0x180003cf0) | qword_18006E7C0(GetDrawnWindow)=第4参?:hDeviceWindow;参数透传 | GetDrawnWindow 恒=主窗口 | 差异(B-2) |
| 27 CreateRenderTarget (0x180002d40) | RT 输出指针邻域 8 槽登记"当前打开文件名" | 无 | 惰性路径,等价(B-9) |
| 1/2/6 AddRef/Release/GetDeviceCaps | 身份注册表簿记(0x180006120 把裸对象换回包装器) | 不需要 | 等价(去 hook 化) |

#### A4. MMHack.dll —— 22 个导出 getter(MMEffect 全量消费,IAT 0x1800a4318..43c0)

| Getter | 原版数据源/语义 | 内置版 | 结论 |
|---|---|---|---|
| GetLightViewProjMatrix | effect->GetMatrix("matLightViewProj")→drawType==5 且附件时 inv(AcsWorldMat)·LVP→GetDevice→GetLight(0)→GetTransform(VIEW)→viewInv 平移=eye、at=eye-dir·50、up=(0,0,1)→LookAtLH(at,eye,up)→world·lookAt→奇异分支死存储→无条件尾部覆盖 | mmhack_lightmatrix.cpp | **整函数逐指令等价**(含 eye/at 互换写法与未定义值语义;内置版多加空效果防御分支,原版会崩) |
| GetAcsAttachedPmd | sub_18000F2B0 未命中 objData 图→[0x18000f331] return id 借位→读 +0x240/-0x244→ExpGetPmdID 零扩展→setnz | mmhack_getters.cpp:16-51 | 等价(借位读取语义保留) |
| GetMaterialName / GetToonTexture / 纹理缓存 | node isPmd==1 且 idx<end/0x78;+0x28/+0x50 双名;toon 裸名别名、空串回退、toon10 次级回退 | mmhack_material.cpp / mmhack_registry.cpp | 等价(结构与回退链一致;材质表 PMD/PMX 解析器与 FUN_18000e020 对应,字段/路径改写逻辑一致) |
| LoadedPMMFile / SavedPMMFile | one-shot 拷贝清源 / 队首弹出 | 同名导出 | 等价 |
| IsToonUsed / GetSphereMapMode / GetBlendMode / GetClearColor / GetCurrent* / IsEditMode / IsDebugMode / IsEffectFileUsed / GetTexture / GetSphereMapTexture / GetMMDMainWindow / GetDrawnWindow / GetCurrentFrameTime / GetCurrentModelID / GetCurrentSubsetIndex / GetCurrentEffect | .data 槽位直读(0x18006e788 族) | mmhack_state.h 同名状态 | 等价(槽位注释与反编译一致) |

#### A5. MMEffect.dll(46cc74d9)接口面

- 11 导出(Cleanup/Initialize/OnBeginScene/OnClear/OnCreateModel/OnDeleteModel/OnDIP/OnDP/OnEndScene/OnLostDevice/OnResetDevice):内置版 mme_host.cpp **全部调用** ✓。MMHack DllMain 以同名 GetProcAddress 解析且任一缺失即整体失败(0x18000b160)→ 内置版静态链接等价。
- MMEffect→MMHack 22 个 getter 全量导入;MMEffect→MMD 27 个 Exp*;d3dx9_43 导入 24 个。
- OnLostDevice(0x180058970)="Resetting MME...":销毁各效果 runState(+856)、MmeEngineOnLostDevice、释放 16x16 offscreen RT;OnResetDevice(0x180058a20):每效果 OnResetDevice+重建 16x16 RT(Format 22)。内置版 callbacks.cpp:804-860 为逐行为移植(含 CreateRenderTarget(16,16,0x16))✓。

#### A6. MMD 侧(563316c1)与 effect_api.cpp

- MMD x64 导出 37 个 Exp*(0x4fb5xx thunk 区)与 effect_api.cpp 的 37 个 `__declspec(dllexport)` 一一对应;抽验 ExpGetAcsOrder(0x7ff7cb4c7750:occupied 计数、order+1、`<PreAcsNum+1→负`、`+PmdNum`)与 effect_api.cpp:372-381 逐字段一致。

#### A7. d3dx9 动态层

- MMEffect 的 24 个 d3dx9_43 静态导入(矩阵/向量/着色器工具/纹理与效果创建/Fill*TX)在 d3dx9_dyn.cpp 中 **24/24 全覆盖**;额外 4 个(LookAtLH=MMHack 用、Identity=原版内联、SaveSurfaceToFileA=调试、FromFileExW=镜像完整性)有注释依据。版本策略差异(原版锁 _43,内置版复用宿主任意 d3dx9_XX)已在源码声明为有意取舍。

### B. 发现清单

**P0**:无。

**P1/P2(合并定级,含待确认项)**

1. **每帧 Present 的 MME OnLostDevice/OnResetDevice 通知缺失**(最重要发现)
   - 原版证据:MMHack vtable 槽 16(0x80=Present,字节事实)= 0x180003d30,该拦截器以 `OnLostDevice→真调用→OnResetDevice` 包裹并更新 hDeviceWindow;而槽 15(0x78=Reset)= 0x180003ed0 纯直通。即 MMD 每帧 Present 都触发 MMEffect 的"Resetting MME..."链(销毁/重建各效果设备相关资源 + 16x16 offscreen RT),真正的设备 Reset 反而不触发。
   - 移植证据:mme_bridge.cpp 无 Present 挂钩;MmeHostOnLostDevice/OnResetDevice 仅由 device_reset.cpp:69/92(真实设备事件)调用。
   - 差异:通知面相反。基于 D3D9 语义(窗口模式 Present 不使 DEFAULT 池资源失效),常规场景预计无功能影响,且内置版对真 Reset 的处理比原版更正确;但若原版 MME 引擎依赖每帧重置掩盖状态泄漏(如 0x2D 动画纹理/绑定池),内置版可能表现漂移。**定级 P2,待确认**(建议动态验证:长播放/AVI 输出场景下 MME RT 内容是否残留)。
2. **GetDrawnWindow 更新时机**:原版槽 17(0x180003cf0)在调用时以第 4 参数(hDestWindowOverride 语义)更新 qword_18006E7C0;内置版恒为主窗口(SetMainWindow 时设一次)。原版该拦截器挂位的参数语义本身可疑(读 GetBackBuffer 的 Type 位),**待确认**其实际效果;MMD 单窗口常态下两者返回相同句柄。P3。
3. **Ctrl+Shift+E 屏蔽缺失**:原版 GetKeyState hook(0x180004910)对 'E'+Ctrl+Shift 返回 0,防止 MMD 自身快捷键与 MME 主快捷键冲突。内置版无任何等价屏蔽。若内置版宿主/MME 菜单沿用该快捷键,MMD 侧原功能会同时触发。P2。
4. **对象 id 宽度**:原版 x64 全链以 `(unsigned int)` 截断宿主指针作对象 id(ExpGetPmdID 结果截 32 位,网格登记键亦为 32 位截断),内置版用完整 64 位。两侧各自自洽(产生与比较一致),碰撞概率可忽略;行为等价,记录性差异。P3。
5. **模型路径双重编码**:原版 ObjData.modelName 直接取 D3DXLoadMeshFromXW 的原始宽字符路径(无损);内置版经 ExpGetPmdFilename(Wide→SJIS)再 CP_ACP 转回宽字符(mme_host.cpp:51-56),非 SJIS 可表示字符有损 → 材质表/toon 解析失败风险(MikuDanceStudio/材质名显示、GetToonTexture 命中)。日文/ANSI 环境基本等价。P2~P3。
6. **PreRenderTargetCopy 缺 trackedRT 比对**:原版三拦截器(0x180003bd0/b50/c50)均要求"目标表面==trackedRT"才提前 OnEndScene;内置版只查 endSceneFired。调用点收敛于主 RT 回读前(frame_scene.cpp:415),实际等价;若未来在其它 surface 操作前调用会产生过度触发。P3。
7. **debug 模式设备参数修改未移植**:原版 Hooked_CreateDevice 在 IsDebugMode 时强制 D3DDEVTYPE_REF+软件顶点处理(并无条件清 0x10 行为位——MMD 本不传该位,恒为 no-op)。内置版 InitD3D 有自己的 HAL/REF 回退链,不读 MME debugMode。IsDebugMode 导出本身等价。仅影响放置 MMEffect.debug 的调试场景。P3。
8. **每对象 texRef 引用保护缺失**:原版 BeginScene 创建对象时从宿主对象内存 8 个 qword 槽探测 COM 对象(附件侧要求 +8 dword==-666918069 即 MMHack 包装器 magic)AddRef 存 node[4],删除时 Release;内置版 texRef 恒 nullptr。生命周期由宿主管理,功能等价;引用计数保护语义缺失。P3。
9. **MME 自建纹理不进纹理缓存**:原版 CreateRenderTarget 与 4 个 D3DX 创建入口都把产物登记进 MMHack 纹理缓存;内置版 RecordTexture 只覆盖宿主加载的模型纹理与 toon。仅当 fx 的 ResourceName 恰为 toon 文件名等边缘场景有差。P3。
10. **DrawAccessorySubset 属性表上限 128**:超过 128 表项的附件网格按整网格绘制(真 DrawSubset 无上限)。P3。
11. **d3dx9 版本不锁定**:原版静态锁 d3dx9_43;内置版复用宿主加载的任意 d3dx9_XX。已声明的有意取舍,fx 编译行为在版本间可能有细微差异。P3(记录)。

**P3 补充**:MMEffect.debug 探测目录(原版=MMHack.dll 同目录;内置版=exe 目录,发行包两者同目录,等价);OnCreateModel 的 D3DXCreateBuffer 创建即释放死代码未移植(合理)。

### C. 已验证一致项简表

1. MMHack 22 个 getter 的名称/序号/数据源/语义(4 个深验 + 1 个整函数反编译 + 其余经状态槽与消费方核对)。
2. BeginScene 状态机全序列(含 Initialize"返回 0=成功"的汇编级分支、MessageBox 文案、effectsDisabled、对象增删、order 图、world/inv、trackedRT、subset=-1)。
3. DIP/DP/Clear/EndScene 拦截器逐分支(含固定功能/效果两路纹理捕获的全部魔数:起始采样器、COLOROP=1 级联、TTF==2、TCI==0x10000、D3DTOP_ADD==7、DESTBLEND==2、GetTexture(1)/(texUsed?2:1)、spadd)。
4. 编辑态探测(317→417 回落、RecWindow 类名匹配、粘滞一帧语义)与 frameTime 供给。
5. PMM 跟踪(one-shot LoadedPMMFile、SavedPMMFile 队列、.pmm 扩展名判定、GENERIC_READ/WRITE 区分)。
6. 标准效果登记(仅首个、12 技术 GetTechniqueByName、登记前清缓存)与技术 flags 表(0x18005bdf0 与 g_tecTable 逐项一致)。
7. MyD3DXMesh::DrawSubset 直通 + 设备层拦截架构 ≙ 内置版 DrawAccessorySubset 复刻 DrawSubset 硬件路径(SetStreamSource/SetIndices/SetFVF/DIP、无表整网格、属性缺失不画)。
8. MMEffect 11 导出 MMHack↔MMEffect 接口全量;OnLost/OnResetDevice 引擎侧逐行为移植(含 16x16 offscreen RT)。
9. effect_api.cpp 37 个 Exp* 与 MMD x64 导出面对应(ExpGetAcsOrder 抽验逐字段一致)。
10. d3dx9_dyn 24/24 覆盖 MMEffect 的 d3dx9_43 静态导入;包装参数面一致(纯转发,失败返回 E_NOTIMPL/nullptr)。

**总评**:抓取的数据集合、字段布局、更新时机与传递语义总体高度等价(核心绘制/状态机/光照矩阵/材质表均达逐分支级一致)。唯一实质性通知面差异是 Present 周期的 OnLostDevice/OnResetDevice(B-1,原版每帧、内置版仅真 Reset,常规场景预计无影响但建议动态确认),以及 Ctrl+Shift+E 屏蔽缺失(B-3)与模型路径双重编码(B-5)两个可复现差异点。

## 报告四：MMD 模型与文件IO

IDA 会话 563316c1(MikuMikuDanceE_v932x64)。本轮为空库起步,已自行定位并重命名关键函数:`LoadPMX_x64`(0x7FF7CB4C9AC0,即 x86 0x4B77E0)、`LoadVmdMotion_x64`(0x7FF7CB48D170)、`ModelLoadPMD_x64`(0x7FF7CB4D2670);PMM shell 为 0x7FF7CB4A2C10、v2 体为 0x7FF7CB498E30、save 为 0x7FF7CB4950A0。工作树未提交修改(pmx_load.cpp、path_resolve.cpp 等)已按现状审计。

### A. 模块判定表

| 子模块 | 判定 |
|---|---|
| pmx_load.cpp(头/顶点/SDEF/面/纹理/材质/骨骼/IK/帧/刚体/关节) | 基本一致(含 1 处明显偏差,见 B-1) |
| pmd_load.cpp(PMD 1.3 全解析 + 英文名区 + toon + 物理) | 严格 1:1 |
| vmd_load.cpp(模型/相机/照明/自阴影/表示帧) | 基本一致(1 处已文档化偏差 B-5) |
| path_resolve.cpp(ResolveUserFilePath / ConvertMaterialName / LoadTextureShared) | 严格 1:1(含原版怪行为保留) |
| add_model.cpp / model_init.cpp / post_load_init.cpp | 基本一致(抽查+注释 VA 证据吻合) |
| model_dispose.cpp | 基本一致(Bullet 释放改用公共 API 的等价重写,已注释) |
| pmm_load_dispatch.cpp | 基本一致(版本字段定位基准差异 B-4) |
| pmm_load_v1.cpp / pmm_load_v2.cpp / pmm_save.cpp | 未深度重验(注释带逐 VA 阶段映射,抽查吻合:头 30 字节、0x238 记录、0x32E/0x32F/0x330/0x331 对话框、minFrame 重基准均在) |
| vmd_save.cpp / vpd_file.cpp / vsq_load.cpp / charset_conv.cpp | 基本一致(关键常量与怪行为抽查吻合) |

### B. 发现清单

**P1 — PMX 附加 UV1-4 morph 基线表取值源错误**
- 原版:LoadPMX_x64 基线表构建中,family 0(type 3)记录 4 分量取 顶点 +24/+28/+176/+180(uv.xy + uvMorphBaseZW);family 1..4(type 4..7)分别取 +32/+48/+64/+80、+36/+52/+68/+84、+40/+56/+72/+88、+44/+60/+76/+92(即 additionalUvByComponent[0..3][k-1] 的 SOA 块)。
- 移植:`src/model/pmx_load.cpp:1268-1272` —— kUvFamilies 循环内**所有族**统一写 `vertex.uv[0]/uv[1]/uvMorphBaseZW[0]/[1]`。
- 影响:任何含附加 UV morph 的 PMX 2.0 模型,UV1-4 morph 插值基线错乱。

**P2 — PMX bone/UV morph 基线表 count 未按原版去重**
- 原版:读取每条 bone morph entry 时内联扫描先前 type==2 morph,重复 bone 既不递增计数也不入表;基线表 count(m+8780)= 去重后条数。UV 五族同理(m+8760..8776)。
- 移植:`pmx_load.cpp:1062`(`++boneMorphTotal` 每条计)、`pmx_load.cpp:1162`(`BoneMorphOffsetCount = boneMorphTotal` 总条数)、`pmx_load.cpp:1234`(UvMorphCounts 同);去重在填充时才做,表尾残留全零记录(boneIndex=0),且 `pmx_load.cpp:1204` 的 entry 重写循环 `r < boneMorphTotal` 可能匹配垃圾槽。运行时多数场景因恒等基线而无可见差异,但表尺寸/count 与原版不等。

**P2 — PMX 主/边 VB 的 D3D Pool 与原版不同**
- 原版:LoadPMX_x64 `CreateVertexBuffer(Length, Usage=8, FVF, Pool=1(D3DPOOL_MANAGED), ppVB, 0)`(x86 0x4B7806 段对应的 5 参为 1)。
- 移植:`pmx_load.cpp:355-356、368-369` 传 `D3DPOOL_SYSTEMMEM`。注:同文件 PMD 路径(pmd_load.cpp:203/218)用 MANAGED 与原版一致,仅 PMX 侧不同;蒙皮层每帧直接 Lock 该 VB(model_skinning.cpp:760)。待确认渲染链是否有补偿拷贝。

**P2 — PMM 版本字段定位基准**
- 原版:0x7FF7CB4A2D8A/0x7FF7CB4A2DA5 `lea rsi, [rax+0x14]`,rax 为 strstr("Polygon Movie maker") 匹配点,5 字节比较 "0001"/"0002"。
- 移植:`src/io/pmm_load_dispatch.cpp:140/146` 用 `strncmp(hdr + 0x14, ...)`(缓冲区头基准)。标准文件两者等价;头部有前缀垃圾时原版仍能定位版本、移植会误报 "Cannot read this version of pmm!"。

**P2 — VMD 加载路径编码(已文档化偏差)**
- 原版:LoadVmdMotion_x64 直接 `_wsopen_s(fd, widePath)`(x64 原生宽路径,Unicode 文件名可用)。
- 移植:`src/model/vmd_load.cpp:497-502` 先 `WideCharToMultiByte(CP_ACP)` 转 ANSI 再 `_sopen_s`;系统代码页外的文件名打开失败。源码注释自述为 stub 时代偏差。

**P3 — PMX 模型名空槽 Null_%02d 计数时机**
- 原版:名称槽 0 空时 `swprintf_s(buf, 0x14, L"Null_%02d", 0)` 且**不递增**计数器(0x7FF7CB4C9D62 `xor r9d,r9d` 后无 inc);槽 1 空时用当前值并递增;骨骼/形态/帧名则每次递增。
- 移植:`pmx_load.cpp:326` 槽 0 即 `nullIdx++`。两名称槽皆空时原版均为 Null_00,移植为 Null_00/Null_01。

**P3 — PMX 纹理索引越界防护(移植比原版多)**
- 原版:材质 tex/sphere/toon 索引仅检查 `< 0`,无 texCount 上界(越界索引读野指针,UB)。
- 移植:`pmx_load.cpp:620/636` 增加 `idx >= texCount → 空路径`。属防御性修复,非法文件下行为不同。

**P3 — LoadTextureShared 底部像素取样 LockRect flags**
- 原版:0x7FF7CB428E39 `LockRect(0, &rect, 0, 16)`(D3DLOCK_READONLY)。
- 移植:`path_resolve.cpp:266` flags 0。功能等价,MANAGED 纹理 dirty 语义略异。

**P3 — VMD 相机分支返回值**
- 原版:`return sub_7FF7CB47D320(app)`(链式 ApplyGravityTrack 返回值)。
- 移植:`vmd_load.cpp:301` 固定 `return 0`。

**P3 — IK/表示帧条目释放器不匹配**
- 原版:`operator new(21*n)` + `operator delete[]`。移植:`vmd_load.cpp:466-481` `operator new` + `free`。MSVC 同堆无实际影响。

**P3 — 已自述的不可复现偏差(pmm_save/v2/vsq 注释)**:原版 `%s` 无参 swprintf_s 读栈垃圾、PMM 头 NUL 后 4 字节栈残留等,移植以确定性值代替。

### C. 已验证一致项(抽样对照原版函数)

- **VMD(0x7FF7CB48D170)**:双 magic 判定与 10/20 名长;相机/照明分支 ver1 单曲线字节 20 次展开(rec+33/39/45/51 各复制 +1..+5、rec+57 不触碰、fov=45);ver2 六轮交错 4×(33,45,39,51);光照帧 rgb→rec+16..24 / xyz→rec+4..12 的交错读取;骨骼键 rec+32/52-60/36-48 交错序与**插值第 4 控制字节存 rec+77+nn**(0x7FF7CB48E355 `inc rdi` 后 `mov [..+var_2E0+0Ch]`,反编译的 nn+12 为假象);ch0 两 u16 且第二值 ==3939 线性标志;undo ring 30 槽、36B 骨骼快照(trans@328/quat@340/物理标志@+12584);三个 key 池清标志的等价循环;IK/表示帧 21B 记录、count>0x2710 break;模型名 strcmp 严格匹配 + カメラ・照明 13 字节拒载;morph 值 _finite 钳 0。
- **PMX(0x7FF7CB4C9AC0)**:version!=2.0 与 UTF8 双错误框(JP/EN 分支、标题);8 索引字节乱序存储但文件序一致;名称槽 0x32/0x32/0x100/0x100 镜像与"情報なし"/"NoInfo"默认;showInfo 0x40001 对话框;FVF 阶梯 274/524818/2622226/11011090/44565778 与 32/48/64/80/96 stride;188B 顶点 ABI(weightType@96/bone@100/weight@116/SDEF C@132 R0@144 R1@156/edgeScale@168/标记@172)与 SDEF 中点重定基 `R0-=w*R0+(1-w)*R1`;SOA→AOS 的 GPU 填充;材质 flags&0x10 双面、edgeColor/edgeSize、toon 0xFE/0xFF 与共享 toon 字节、per-vertex 材质分配 edgeKey=0x4479F333(=999.79999f);骨骼 flags 位 1/4/8/0x100/0x200/0x400/0x800/0x2000/0x20、tailOffset+=pos、轴 normalize、中心骨(操作中心优先/センター回退);IK 链 24B 记录、angle*0.25 与 360 迭代钳制;hasFlag 双 pass(PMX 版含 layer 提升、PMD 版无);group/vertex/bone/UV/material morph 全类型布局与负索引错误框("load pmx model");材质 morph 三池 128B 与基线通道;显示帧→表情组/Root/Disp/IK/OP/Facial 分组表 46B 骨架帧;刚体 192B 全字段、mode 2/0 标志、负 boneIndex 用 bones[0] 重定基、正逆变换矩阵链与 CreateRigidBody 参数序、hasRigidBody@+500;关节 152B、jointType 读后丢弃、limits 四组乱序(+84 区→+72 区→+108 区→+96 区)与 radiusBound=distA+distB+‖max(|lo|,|hi|)‖;1000 帧 display 池与插值默认 20/20/107/107、PostLoadInit 调用;**PMX 2.1(version!=2.0)直接弹错拒载、qdef(4) 落入 BDEF1 分支(文件错位行为也一致)**。
- **PMD(0x7FF7CB4D2670)**:"Pmd" 3 字节判定与 PMX 分派传参;目录截取+SetCurrentDirectoryW;材质字段序(diffuse→power@+64→spec→ambient→toon→sphere→faceCount→tex)、`*` 拆分双纹理(sphere 加载成功才存路径);骨骼 type@+492 直读、kBoneFlagVisible 全置、IK 链 24B;morph 无 group 字节直读 16B 表、首 morph 共享表与 entry 重映射(0x4C05EB);显示帧/表情组/分组表/RB 组;英文名区判定(read 返回值+标志值)、无英文警告框与名字回拷、nameEn[19]=0;toon 表 enRead!=0 条件、非匹配回退用**循环尾**的 "toon10.bmp"(连怪行为一致);刚体段 `read(...)!=−1` 门控与物理 hasFlag sweep(tailType==2||4,无 layer 提升)。
- **path_resolve(0x7FF7CB429C00 / 0x7FF7CB428BE0)**:UserFile 探测序(exec 目录→项目目录→原路径→按扩展名 Model/Wave/Accessory)、尾 3 宽字符比较、X/x 双字比较永假的原样保留、回扫到串首即失败;纹理缓存 10000 项 24B stride、1024 mip→1x1 重试 0 mip→失败重试 0 mip 三级回退、名字拷贝与左下像素取样。
- **add_model(0x7FF7CB4B66C0)**:模型槽上限 **255**(`mov r9d, 0FFh` @0x7FF7CB4B670E)、"you cannot add models over %d!"("add model" 标题)、JP 分支;菜单启用/禁用序列、SetMenuItemInfoA physicsMode==2 灰化。任务提到的"重复加载限制 32"未在原版发现(undo 环为 30 槽,两处均确认)。
- **PMM/其他**:shell 头 0x1E 读、strstr 判定、错误三分支(含 v2 fd 所有权);save 头 `sprintf_s("Polygon Movie maker 0002")`+write 30 字节、W4 渲染宽高接续;vsq 的 'M'→'u'(0x4D→0x75)与子音链、'%7.3f -%7.3f   %-4s %1s %c' 输出行;vmd_save 的 minFrame 重基准与分类键写出序;VPD 的 pose 源选择条件与格式串。

### 备注(未验证项原因)
pmm_load_v1/v2、pmm_save、vpd_load 体积合计约 25 万字符,本轮仅做头部注释对拍与关键点抽查(版本判定、记录 stride、对话框 ID、写序),未逐字段重验;其源码注释带逐 VA 证据链且抽查点均吻合,残余风险评级低。建议后续对 pmm_load_v2 的 per-model 状态装载段(0x4504BA..0x45410A)做一次独立的逐字段复核。

## 报告五：MMD 骨骼动画与物理

审计对象:原版 MikuMikuDance.exe v932 x64(IDA 会话 563316c1,基址 0x7FF7CB420000;函数原名以 0x140000000 基址记录,换算规则 0x140xxxxxx −0x140000000 +0x7FF7CB400000)。工作树未提交修改不涉及本次负责的文件(均在 render/mme/window/pmx_load 等其他区域)。

### A. 模块判定表

| 子模块 | 判定 | 要点 |
|---|---|---|
| model_keyframe_advance.cpp(AdvanceModelKeyframes/BoneEase/NotifyBonePhysicsMode) | **严格 1:1** | 贝塞尔 12 步二分逐指令验证(x64 sub_7FF7CB4EDB00) |
| physics_create.cpp 之 SetPhysicsMode/CreateRigidBody | **严格 1:1** | sub_7FF7CB4D9F70/sub_7FF7CB426180 逐字段对照 |
| physics_frame.cpp(sync/reseat/readback/pump 物理段) | **基本一致** | settle×3、10 substeps、255 槽 walk 逐指令验证;readback 浮点链未逐位复核 |
| scene_create.cpp(SceneConstruct) | **明显偏差(1 处 P1)** | 重力常量位级错误;其余(solverMode&=~1、gizmo、结构)一致 |
| bone_transform.cpp(BoneFrameTransform 五阶段) | **基本一致** | callees 特征(strncmp 膝盖/asinf+atan2f euler/D3DX 链)与 IK 半角数学抽样严格一致;五阶段为单函数连续代码,顺序未逐块复核 |
| model_skinning.cpp(BDEF/SDEF/顶点 morph) | **基本一致~严格** | SDEF nlerp(0x7FF7CB4E2D20)逐位一致;5 个 _vcomp_fork worker 结构一致;BDEF4 累加序未逐位复核 |
| morph_apply.cpp(ModelApplyMorphs) | **基本一致(1 处 P2)** | sub_7FF7CB4DD780 类别顺序/常量/操作类型一致;quat 重缩放精度路径偏差 |
| model_frame_seek.cpp / track_apply.cpp | **未验证(预算)** | 与 advance 共享常量(0x3F7FFFEF/0x3FC90FD8 已在 .rdata 定位);seek 重算范围注释自洽 |
| key_registrars.cpp / model_keyframe_edit.cpp / bone_edit_undo.cpp / bone_sort.cpp / model_query_helpers.cpp | **未验证(预算)** | 簿记/UI 类,注释证据密度高但本轮未对照 |
| color_lerp.cpp / quat_to_mat3x4.cpp | **基本一致(间接)** | 0x401000 对应 sub_7FF7CB421000 确被 CreateRigidBody 调用;公式未复算 |
| math_kernel_ledger.cpp | **一致(纯判定文档)** | 所列 VA 的 bullet-2.75 对应物在 x64 均找到(setRotation、operator new(560) 等) |

### B. 发现清单

**P1-1 重力常量 1 ULP 位级偏差(唯一实质 P0/P1 级发现)**
- 原版证据:sub_7FF7CB4227B0(SceneConstruct x64)@0x7FF7CB4257CF `movss xmm0, cs:dword_7FF7CB552CA8`,值 **0xC2C40000 = -98.0f 精确**;随后 `and [rax+0DCh],0FFFFFFFEh`(solverMode&=~1,与移植一致)+ vtable+0x68 setGravity(0,-98,0)。全二进制搜索无 0xC2C3FFFF 字节。
- 移植证据:src/physics/scene_create.cpp:133-134 `setGravity(btVector3(0.0f, -97.99999237060546875f, 0.0f))`(=0xC2C3FFFF)。
- 差异:注释自认的 C2C3FFFF 来自 **x86** 0x405EAB;x64 原版是 -98.0f。该值每 pass 经 applyGravity 进入所有动态刚体速度积分,是系统性偏差(注释自己也指出此 1 ULP "propagates into every body's initial velocity")。按 x64 行为基准应改为 -98.0f。

**P2-1 morph 骨骼四元数重缩放用 double 而非单精度链**
- 原版证据:sub_7FF7CB4DD780(ModelApplyMorphs x64)@0x7FF7CB4DD979 `call acosf`(comiss clamp ±1.0 于 0x4DD95A-0x4DD970),@0x4DD987 `subss xmm6, [0x4048F5C3]`,后续 sqrtf/sinf/cosf 全单精度 SSE。
- 移植证据:src/model/morph_apply.cpp:87-104 `std::acos(double)`、`double scaled/sin` 等全程 double,无 _M_X64 单精度分支(不同于 bone_transform/model_keyframe_advance 的双路径写法)。
- 差异:x86 x87 路径写法用于全部平台;对 x64 基准最终 float 权重存在 ULP 级偏差。常量本身(3.140000104904175、FLT_EPSILON)正确。

**P3(待确认,未完成独立复核)**
- CreatePhysJoint(6DofSpring)spring 参数 rot/lin 顺序、setUid(counter) 与原版 constraint+0x60 等价性、canonicalZero workaround——x64 侧未定位到函数逐条验证(x86 注释详尽)。
- SDEF 主公式 ((U+Pc)+Pc)×0.5 折叠与 BDEF4 "bone1 先累加、normal 从 bone0 起" 的加法序——worker 函数过大未逐位复核。
- readback part1 的 (dist−limit≥2) 传送门与 mode==2 塌缩数学——仅结构核对。
- flip morph(type 9):移植无实现;原版 morph 函数抽样只见 type 0/2/8 分支,推测两侧一致地不支持(低置信)。

### C. 已验证一致项

1. **贝塞尔 easing**(sub_7FF7CB4EDB00):线性早退 x1==y1&&x2==y2 返回 t;**12 步**减半二分(6 步展开×2 轮,step 先减半再更新);精确相等早退;控制字节 ×0.023622001(0x3CC182ED@0x7FF7CB552988);x64 尾部先乘 y 字节再乘 scale——与 BoneEase x64 分支逐条一致。
2. **slerp 常量**:±0.999999f(0x3F7FFFEF@0x7FF7CB552C24);π/2 截断常量(0x3FC90FD8)存在于 .rdata;advance 版权重 reciprocal-multiply、seek 版除法(双版本差异在两文件均有记录)。
3. **IK CCD**(sub_7FF7CB4DA210 D 段 @0x4DC0A8-0x4DC15E):dot 三乘二加后 clamp **±1.0(非 0.999999)**→acosf→×0.5(0x7FF7CB55298C)→×align;wlim=(li+1)×chainAngle×**2**(0x7FF7CB552C44)双侧 clamp;sinf 构轴——与 bone_transform.cpp:989-1009 及注释的单精度细节完全吻合。膝盖 strncmp、euler asinf/atan2f 均在原版函数 callees。
4. **截断 pi 0x4048F5C3**(3.140000104904175f)@0x7FF7CB552B90,被 morph/bone-transform/readback 三处引用——移植 kPiF 正确。
5. **SetPhysicsMode**(sub_7FF7CB4D9F70):选择门 `((pm==1||(pm>=2&&bone+501==0))&bone+500)`、物理/运动学姿态源拷贝偏移、PMX bone-morph(pos+=、quat=D3DXQuaternionMultiply)、**do-while 层循环**调 BoneFrameTransform——逐字段一致。
6. **CreateRigidBody**(sub_7FF7CB426180 尾部):addRigidBody(body,1<<grp,u16 mask)(shl+movzx+call[+0x118]);setDamping;restitution/friction 直写 +0xF8/+0xF4(移植 API 等价);mode==0 → `or [rbx+0E0h],2`(CF_KINEMATIC);`mov edx,4`+setActivationState(0x7FF7CB50D320)——**bullet-2.75 枚举 DISABLE_DEACTIVATION=4**(recipes/bullet275 头文件证实:WANTS_DEACTIVATION=3 占位),移植同包同枚举,一致;out[0]=m_debugBodyId、out[1]=body。
7. **物理泵 settle**(sub_7FF7CB4474F0 @0x44C770-0x44C82C):主步 `lea r8d,[rdi+0Ah]`=**maxSubSteps 10**、moved 双 dt 选择+extra 步;settle 标志→`mov esi,3` **恰 3 轮**[255 槽 reseat(sub_7FF7CB4E45D0)+step]→清标志→readback 255 槽——与 PhysicsFrame 逐条对应(含 gate A/B 双跳转点 0x44C6E5/0x44C73D 布局)。
8. **ModelApplyMorphs**(sub_7FF7CB4DD780):调用点 0x7FF7CB44C6A8/0x44C8A8 恰为双 pose pass(morph+SetPhysicsMode);类别顺序 bone→material→compose;add `+=d*w` / mul `(1-(1-d)*w)*m`;value==0 跳过。
9. **SDEF nlerp**(0x7FF7CB4E2D20=sub_1400C2D20):dot 加法序 ((x·x+w·w)+y·y)+z·z、dot<0 时 t 取负且 delta 用和、sqrt 顺序 (x²+y²)+z²+w²、**先算 1/len 再逐分量乘**、退化置 (0,0,0,1)——BlendSdefQuaternions 逐位一致。
10. **PMX VB 更新**:sub_7FF7CB4E13C0(=0x1400E13C0)含 **5 个 _vcomp_fork**(附加 UV 0..4 worker 梯)+ 紧随其后的 SDEF helper,注释地址经基址换算全部落位。
11. **SceneConstruct 其余项**:solverMode `and [rax+0DCh],0FFFFFFFEh` ✓;gizmo 缓冲区尺寸序列 ✓。

**总评**:核心骨骼/蒙皮/关键帧/物理流水线与 x64 原版高度忠实(多处逐指令、逐位级一致,注释中的 x64 地址经换算后全部在本 IDB 落位且抽查无误);唯一可证伪的数值级偏差是 scene_create.cpp 的重力 y(P1,改一个字面量即可修复),外加 morph quat 重缩放的单精度路径缺失(P2)。建议后续轮补验 CreatePhysJoint 参数映射与 SDEF/BDEF4 worker 的位级公式。

## 报告六：MMD 渲染管线

**基准说明**:IDA 会话 563316c1 中原版 MikuMikuDance.exe 已重定位到基址 0x7FF7CB420000(PE ImageBase=0x140000000,实测自磁盘 PE 头)。下述地址均为 IDB 地址;换算原 VA 需减 0x7FF68B420000。移植源码注释中的 `0x46xxxx/0x49xxxx` 式 VA 是 x86 版地址,与本 x64 二进制不对应(移植后半段注释同时引用了 x64 IDB 地址,证明作者实际对照过本 IDB)。关键定位:帧驱动 = `sub_7FF7CB4474F0`(0xFDB4 字节,WinMain 调用,全程序唯一 BeginScene 调用点 0x7FF7CB44A419);固定管线帧 = `sub_7FF7CB4BFB20`;阴影图 = `sub_7FF7CB4C1030`;效果帧 = `sub_7FF7CB4C1E60`;toon 初始化 = `sub_7FF7CB4BA130`;D3D 初始化 = `sub_7FF7CB427A30`;渲染状态 = `sub_7FF7CB428550`;捕获降采样 = `sub_7FF7CB4BF6A0`。

### A. 帧渲染顺序对照(原版 sub_7FF7CB4474F0 / sub_7FF7CB4BFB20 vs 移植)

| # | 原版(IDB 地址) | 移植(文件:行) | 判定 |
|---|---|---|---|
| 1 | 世界变换/精灵准备/立体更新(Clear 前区段) | frame_scene.cpp:361-363 | 一致(顺序性核对) |
| 2 | 门控 A1E18==0 → 跳过 VB 更新+Clear+场景;A166D 仅门控 VB 更新(0x44A1A3/0x44A3F1) | frame_scene.cpp:370-379(messageSeen/accessoryEditDialogOpen) | 一致 |
| 3 | Clear:flags = 3 或 7(3AA78 模板位),颜色 0xFFFFFF 或 0(A1104 黑底),Z=1.0(X=0x3F800000@0x552984)(0x44A266) | frame_scene.cpp:381-388 | 一致 |
| 4 | renderPassCount=1 → BeginScene,失败即弃帧(0x44A3FF/0x44A419) | frame_scene.cpp:390-392 | 一致 |
| 5 | 阴影图 pass(效果渲染器时):4C1030 | model_renderers.cpp RenderShadowMap | 一致(结构核过,见 D/待确认) |
| 6 | pass 循环 `while(--passCount>0)`:效果帧 4C1E60 / 固定帧 4BFB20(0x44A500-0x44A5C7) | frame_scene.cpp:400-407 | 一致(同一门控两处) |
| 7 | 自影子合成:RS(27)=0 → 全屏 quad(FVF 0x144, stride 28, TRIANGLELIST×2, 纹理=渲染器+3AA48, VB=A1070) → RS(27)=1(0x44A5CD-0x44A6B5) | frame_scene.cpp ComposeSelfShadow(256-266) | 一致 |
| 8 | MME 深度纹理合成 quad(回调 A1350, VB=A1078)(0x44A6B5-0x44A779) | ComposeCallbackTexture(268-281) | 一致 |
| 9 | 录制窗时:GetBackBuffer(懒) + 降采样(0x44A779-0x44A7CB → sub_4BF6A0) | frame_scene.cpp:419-426 | 一致 |
| 10 | 捕获纹理:mode∈{1,2},CreateTexture 1024² RT X8R8G8B8,OUTOFVIDEOMEMORY/E_OUTOFMEMORY→512² 回退;mode2 的 4:3 裁剪(常量 4.0/3.0/0.5 实测吻合);StretchRect LINEAR→NONE 回退(0x44A7D0-0x44ACA2) | CaptureAccessoryScreenTexture(283-348) | 一致(逐指令核) |
| 11 | 录制窗路径:直接 EndScene,无调试/overlay(0x44ACA2 jnz→0x44B05B) | frame_scene.cpp:432-436 | 一致 |
| 12 | 调试几何:RS(137)=0, Tex0=null, RS(7)=0 → 附属对话框(4B11C0)/物理(426830) → RS(7)=1, RS(137)=1(0x44ACB0-0x44AD91) | frame_scene.cpp:439-450 | 一致 |
| 13 | 骨骼操作轴(门控 [368]==0 && [328]==0 && A1BC8==0 且模型已载 → sub_457490)(0x44AD91) | frame_scene.cpp:453-458 | 一致(语义待确认见 C-6) |
| 14 | RS(27)=1, RS(19)=5, RS(20)=6(0x44AE1C-0x44AE78) | frame_scene.cpp:460-462 | 一致 |
| 15 | overlay 三批:文本(Tex=9FD70, FVF 0x144, TRIANGLELIST) → 线(**先 SetTexture(0,null)**, FVF 0x44, LINELIST, VB=渲染器+3A9D8) → 精灵(Tex=9FD58→绘制, FVF 0x144)(0x44AE7E-0x44B055) | frame_scene.cpp:464-466 | 基本一致(缺一处 SetTexture,见 C-3) |
| 16 | EndScene(0x44B05B) | frame_scene.cpp:467 | 一致 |

**固定帧 sub_7FF7CB4BFB20 内部**(对 RenderModelsFixed):帧头矩阵(shadowProj×帧矩阵, 0x4BFB5F) → TSS(2,TCI,0x10000)(0x4BFC08) → 模板三连(ALWAYS/1/REPLACE) → 图片背景 quad(**TRIANGLELIST**, 0x4BFC74)→ AVI quad(0x4BFD4A, 门控 13EC==1)→ 地面(门控 355, 0x4BFE20)→ FILLMODE(线框开关)+LIGHTING=1(0x4BFE88)→ 灯光 SetLight(0)(Diffuse=10/Ambient−0.3,0x4BFED0-0x4BFF50,对应移植 SetAccessoryLight)→ 配件段1(order 排序)→ 投射 pass(pass=3, FILLMODE=3, 模板 GREATER/2/REPLACE, CULL=1, ZFUNC=2, Tex0=null, 0x4C0319-0x4C0402)→ 地面影门控(lightY<0 && (13E4≥2||368) && 13E8, 0x4C044E)→ WORLD=阴影矩阵 → 配件影 → 模型剪影(0x4C0710, sub_4D82A0 flag=1)→ WORLD 恢复(0x4C0795)→ 状态恢复(FILLMODE/模板 ALWAYS/1/REPLACE/CULL=3/ZFUNC=4)→ 模型主体遍历(0x4C0880)→ edge 前导(Tex0=null, LIGHTING=0, ALPHABLEND=1, CULL=CW, ZFUNC=LESS)→ edge 遍历 → 第二配件前导(CULL=3, ZFUNC=4, LIGHTING=1, stage0 复位, WRAP 采样, ALPHABLEND=1)→ 配件段2 → 尾(DESTBLEND=6, FILLMODE=1)。**移植的调用顺序与此逐项吻合**(唯一偏差见 C-1/C-2;另移植把两处模型遍历注释的 x64 地址互换,代码顺序正确)。

### B. 模块判定表

| 模块 | 判定 | 依据 |
|---|---|---|
| frame_scene.cpp | 基本一致(细微偏差) | A 表 1-16;偏差 C-3 |
| model_renderers.cpp | 基本一致 | 帧头/投射/恢复/edge/尾段逐项核过;偏差 C-1、C-2;材质混合分支与 RenderShadowMap 斜率公式仅按注释抽查(待确认 C-7/C-8) |
| accessory.cpp | 基本一致 | TCI 用了正确的 0x10000 字面量;±0.3 灯光、放置矩阵、阴影材质 0.7α 均与原版片段吻合;含疑似死代码 C-9 |
| toon_textures.cpp | 基本一致(细微偏差) | 结构/回退/像素读取逐指令一致;默认表数值偏差 C-4 |
| d3d_init.cpp | 严格 1:1 | 格式阶梯、7 级 CreateDevice、MSAA 8/4/2/1、SM3/SM2 资源、SKII1 600/500、锁定捕获 RT、立体探测反转逻辑全部与 sub_427A30 反编译一致 |
| render_states.cpp | 基本一致 | 与 sub_428550 逐条一致(含按状态分组的采样器写法、sampler2 MAXANISO=1),仅 TCI 值偏差(归入 C-1) |
| device_reset.cpp | 未验证 | 未在 x64 中定位对应 Reset 流程(0x440DB0 为 x86 注释);代码结构与注释自洽 |
| capture_downsample.cpp | 严格 1:1 | 与 sub_4BF6A0 逐指令一致(减法 GCD、两遍有理 box 滤波、相位累加、B\|G<<8\|R<<16\|A<<24、UpdateSurface) |
| stereo_nvapi.cpp | 基本一致 | nvapi64 加载/QueryInterface/0x150E828 初始化/0xAC7E37F4 建柄(2893953012 实测吻合)/-3 语义一致;省略 trace 钩子(已注释,C-10) |
| bg_overlay.cpp | 基本一致 | 6 顶点 LIST 布局(v2==v3)与原版一致;矩形数学未逐指令核 |
| sprite_overlay.cpp / overlay_producers.cpp | 快扫通过 | 顶点布局与对应 draw 调用自洽(文本 LIST×2、线 LINELIST×4);未对原版逐条核 |
| draw_glyph.cpp / fill_panel.cpp / scene_font.cpp | 快扫通过 | GDI 序列/图集布局细节充分,未逐条核 |
| debug_geometry.cpp | 快扫通过 | 未逐条核 |

### C. 发现清单(按严重度)

**C-1 (P1) 纹理坐标 TCI 值系统性偏差:0x10000 vs 0x30000**
原版证据:全二进制**无任何 0x30000 立即数**;所有"相机空间法线"语义的 `SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX=11, X)` 的 X 均为 0x10000(D3D9 SDK 中为保留值,非 CAMERASPACENORMAL):初始化 0x7FF7CB428A5C、固定帧头 0x7FF7CB4BFBFF(字节 `41 B9 00 00 01 00` 已核)、材质级联 sub_7FF7CB4D6D70 四处(0x4D70C9/0x4D7283/0x4D75E4/0x4D795C)、配件路径 sub_7FF7CB4FD350 三处。
移植证据:render_states.cpp:111-112 与 model_renderers.cpp:1306-1307、484-577(ConfigureMaterialStages 各球面贴图分支)写 `D3DTSS_TCI_CAMERASPACENORMAL`(0x30000);render_states.cpp:112 注释甚至写着 `(2,11,0x10000)` 却写了 0x30000 常量。
差异:驱动收到的 TCI 值不同。原版 0x10000 在多数驱动上仍按法线解释(否则 MMD 球面/toon 贴图不会工作),但两者发给设备的状态字节不同,且行为由驱动决定。注意 accessory.cpp:186/195/209 用了正确的字面量 0x10000——移植内部自相矛盾,应统一为 0x10000。

**C-2 (P1) 背景(图片/AVI)四边形图元类型错误:TRIANGLESTRIP vs TRIANGLELIST**
原版证据:0x7FF7CB4BFD24(图片)与 0x7FF7CB4BFDFA(AVI)均为 `DrawPrimitive(edx=4=TRIANGLELIST, 0, 2)`(edx=`lea edx,[r9+2]`, r9=2)。
移植证据:model_renderers.cpp:1257 `mme::DrawPrimitive(device, D3DPT_TRIANGLESTRIP, 0, 2)`;而顶点缓冲按原版 6 顶点 LIST 布局填充(bg_overlay.cpp WriteOverlayQuad,v2 与 v3 完全相同,0xA8 字节锁定)。
差异:STRIP 只消费 4 顶点,第二个三角形 (v1,v2,v3) 因 v2==v3 退化 → 背景图/AVI 背景只画出右上三角,左下半缺失。

**C-3 (P2) DrawLineOverlay 缺少 SetTexture(0, nullptr)**
原版证据:0x7FF7CB44AF3A 在线框批次前显式 `SetTexture(0, NULL)`。
移植证据:frame_scene.cpp:62-75(DrawLineOverlay 无 SetTexture 调用)。
差异:线批继承前一个文本批绑定的字体图集纹理;固定管线 stage0 COLOROP=MODULATE 时线色可能被 (0,0) 处 texel 调制。文本批不存在时无影响。

**C-4 (P2) toon 边缘色表默认值数值偏差(仅回退路径)**
原版证据:sub_7FF7CB4BA130 于 0x7FF7CB4BA24C-0x7FF7CB4BA388 写入的默认表(dword 实测换算):[0..2]=0.8, [3]=0.98, [4..5]=0.9, [6..8]=0.62, [9]=0.99, [10]=0.937, [11]=0.921, [12]=1.0, [13]=0.88, [14]=0.83, [15]=0.67, [16]=0.56, [17]=0.01, [18..29]=1.0。
移植证据:toon_textures.cpp:122-137 使用 n/256 近似(0.800781/0.957031/0.878906/0.601563/0.96875/0.933594/0.917969/0.902344/0.863281/0.761719/0.671875/0.011719)。
差异:仅在 toonNN.bmp 加载失败(走内嵌 PNG 回退)时表项不被像素色覆盖、默认值直接生效;成功路径(读左下角像素 /256 写表,B/G/R 顺序、stride 3、0x7FF7CB4BA522)移植完全一致。另:未发现任务所说的"toon02.bmp 特殊处理"存在于本 x64 的 toon 初始化/名字表(0x551EB8 表仅用于共享 toon 解析,port ToonTexture 的 -1→slot0 / 名字匹配→slot N+1 / PMD 目录拼接逻辑与表引用位置吻合)。

**C-5 (P3) 两条模型遍历的注释地址互换**
原版证据:0x4C0710 遍历位于地面阴影门控块内(WORLD=阴影矩阵 0x4C04A0 之后、WORLD 恢复 0x4C0795 之前,门控不满足时 0x4C0480 直接跳过),即投射阴影剪影遍历。
移植证据:model_renderers.cpp:1331 注释把 0x4C0710 标为主体遍历、1064 把 0x4C0880 标为剪影遍历。代码调用顺序本身正确,仅注释反了。

**C-6 (待确认) cameraGate 标志位语义**
原版证据:效果渲染器门控 = ([13E4]>=2 || [368]!=0) && [A1DD0]>0 && [A10F8]!=0(0x44A427);地面影门控 = lightY<0 && ([13E4]>=2 || [368]) && [13E8](0x4C045F)。骨骼轴门控用 [368]==0 && [328]==0(0x44AD97)。
移植证据:EffectRenderEnabled = (PlaybackActive||UsesViewportTool) && selfShadowMode>0 && selfShadowEnabled(model_renderers.cpp:1274-1279)。
[13E4] 为 dword 且比较为 >=2(非 !=0),[368]/[328] 在两处门控中的移植映射(optflag[0]/PlaybackActive/UsesViewportTool)无法在渲染文件内闭环验证;若 [13E4] 可取值 1,移植会在原版不启效果渲染器时启用它。需对照 mmd_app.hpp 访问器定义。

**C-7 (待确认) 固定帧模型 pass 的 WORLD 来源**
原版证据:0x4C041D/0x4C0440 在投射 pass 前取 GetTransform(PROJECTION/VIEW);模型主体经 sub_7FF7CB4D82A0(model, renderer, flag) 绘制。
移植证据:RenderModelsFixed/DrawModelMaterials 从不写 D3DTS_WORLD(模型空间处理应移植进了 UpdateModelVertexBuffers 的 CPU 蒙皮,不在本次受审文件内)。无法在此闭环;移植注释称经 AB 截图验证过,未独立复核。

**C-8 (待确认) 材质混合分支与自影子斜率公式仅抽查**
DrawModelMaterials 的三分支(postLoadFlag2 加算双 pass CW→CCW、alpha<1 单 pass CULL_NONE、不透明默认 cull)与 RenderShadowMap 的 slope/scale/part 公式(0.9/0.8 阈值、base*3、0.15 深度缩放、±0.5/±1 重定向矩阵)移植注释详尽且与已核片段风格一致,但未对 sub_7FF7CB4D6D70/sub_7FF7CB4C1030 逐指令复核。已核部分:RenderShadowMap 的 R32F(114) RT、D24X8(77) 深度面、Clear(0xFFFFFFFF, Z=1.0)、ZValuePlotTec、恢复目标 multisampleAvailable/RecordingWindow 选择、尾部 RS(27)=1/RS(22)=3,与原版结构注释吻合。

**C-9 (P3) accessory.cpp 内 RenderAccessoriesProjectedGroundShadow 与 model_renderers.cpp 的 RenderProjectedGroundShadowPass 逻辑重复**(门控+矩阵+CULL/ZFUNC 全套),后者才是被调用的路径,前者疑似遗留死代码。

**C-10 (P3) NVAPI trace 钩子(0x33C7358C/0x593E8644 → 0x7FF7CB563D00/D08)被移植省略**,已在 stereo_nvapi.cpp:55-57 注释说明;纯 NVIDIA 侧插桩,无行为影响。

### D. 已验证一致项(要点)

1. **帧信封**:Clear 参数(flags 3/7、白/黑、Z=1.0、Stencil=0)、messageSeen 双重门控(跳过 VB+Clear+场景)、renderPassCount 循环可被回调增发、BeginScene 失败弃帧、录制窗短路(无调试/overlay)、EndScene。
2. **三 overlay 批**:FVF(0x144/0x44)、stride(28/20)、图元(TRIANGLELIST/LINELIST)、纹理/VB 槽位、顺序(文本→线→精灵)、前置混合三连 RS(27)=1/RS(19)=5/RS(20)=6。
3. **自影子合成/深度纹理合成/捕获纹理**:槽位、尺寸 1024→512 回退、HRESULT 码(0x8876017C/0x8007000E)、4:3 裁剪数学(4.0/3.0/0.5 常量实测)、StretchRect LINEAR→NONE。
4. **固定帧状态机**:帧头 TSS(2)、模板三连两套(ALWAYS/1/REPLACE 与 GREATER/2/REPLACE)、背景/地面顺序与 FILLMODE 位置(线框下背景地面仍实体)、投射 pass 五连、恢复四连、edge 前导五连、第二配件前导、尾段 DESTBLEND/FILLMODE——全部与 0x4BFC08-0x4C0FE3 逐条对上。
5. **d3d_init 全链**:backbuffer 21→22 探测、深度 75→77→80、PP 字段(含 0x80000000 即时呈现)、MSAA 8/4/2/1 双格式探测、CreateDevice 七级阶梯(HAL/64 21→22→锁定标志 21→22→HAL/32→REF/64→REF/32)、caps 偏移(MaxAnisotropy=+0x6C、VS/PS 版本字节)、2048 上限、R32F(114) HDR 纹理、PNG 0x67 精灵、SM3(118)/SM2(117)+GetLevelDesc==114→SKII1=600 否则 500、PI/4 透视、InitRenderStates、锁定捕获 RT 失败→multisampleAvailable=1、立体探测成功→stereoEnabled=0。
6. **render_states**:ZENABLE/LightEnable/LIGHTING/SPECULAR、级联 MODULATE/TEXTURE/DIFFUSE/CURRENT 序列、按状态分组的采样器写(各向异性分支值 3/3/3/ caps,线性分支 2/2/(1)/1,sampler2 MAXANISO=1)、半纹叶矩阵到 TEXTURE1/2、stage2 COUNT2、STENCILENABLE/MASK(255)、SEPARATEALPHABLEND(206)=1、BLENDOPALPHA(209)=5、ALPHAREF/TEST/FUNC 的 caps+52&0x40 门控。
7. **capture_downsample**:算法级一致(见 B 表)。
8. **stereo_nvapi**:DLL 名按架构选 nvapi64/nvapi、错误码 -2/-1/-3、一次性解析缓存、接口 ID 全部吻合。
9. **toon 纹理**:11 槽释放、PNG 0x67、data\toon%02d.bmp、PNG(i+103) 回退、左下像素/256 写表(B,G,R 顺序、3 浮点步进)。

**总体结论**:移植质量很高,帧编排、设备/状态初始化、捕获与立体路径达到逐指令级一致。需要修复的是两个 P1(背景四边形图元类型、TCI 常量统一为 0x10000)和两个 P2(线批解绑纹理、toon 默认表精确小数);另有 4 项待确认(cameraGate 标志语义、模型 pass WORLD 来源、材质混合分支、阴影图斜率公式)建议后续补验。

## 报告七：MMD UI命令与主循环

原版基准:MikuMikuDanceE_v932x64(IDA 会话 563316c1,base 0x7FF7CB420000)。下述"原版证据"均为本轮在 IDA 中实际反汇编/反编译核对。

### A. 命令覆盖对照结论

原版 WM_COMMAND 分发器 = x64 `sub_7FF7CB45F550`(0x14D8C 字节):`add eax,-0xC8; cmp eax,0x16F` → 命令 id 范围 **200..567 共 368 个位**,其中实际处理 **208 个 case**(跳转表 `jpt_7FF7CB45F5DD`@0x7FF7CB473E28 + 二级索引 byte_7FF7CB47416C),其余 160 个 id(303-399、409/410/417、425-428、433/434/436、443、447-450、455-466、471、474/475、478-485、499、504-506、509-511、514-516、519-521、530、534、544-550、554、560、561)落入默认分支 `def_7FF7CB45F5DD`@0x7FF7CB472C40(CBN_SELCHANGE 控件 HWND 链)。

移植版(command_dispatch + 5 个家族文件):
- **208/208 原版 case 全部有实现,0 缺失,0 行为性多余**。
- 12 个原版默认位(530/534/544-550/554/560/561)在 command_frame_register.cpp 写成显式 `break` no-op,注释标明 "no jump-table target"——与原版默认分支等价(且 port 的 default 先走 DefaultSelChangeChain 再进家族,保序正确)。
- case 436(0x1B4,原版位于默认链链首)有意放在 400 家族实现,语义等价,文件头有说明。
- 默认链 12 个控件(0x1BB/0x1D7/0x1C1/0x1C2/0x1DA/0x1DB/0x1F8/0x1FD/0x202/0x207/0x1B1/0x1B2)在 command_dispatch.cpp `DefaultSelChangeChain` 齐全,顺序与原版一致。
- 菜单灰显规则:ui_model_reload.cpp 的 `kModeCommands={0xFD,0xD9,0xDC,0xDA,0xCA,0xCB,0xDB,0xDE,0xFB,0xFC}` + 250(0xFA)按剪贴板 + 0xE0..0xE7/0x111..0x113/0xED..0xF2 区间,与原版 `sub_7FF7CB486B10`@0x7FF7CB486E38 起的启用序列逐 id 一致;播放时菜单禁用(playback_state.cpp 0x429790:202-210/212-213/217-220/222-232/237-242/251/252/276 灰 + 物理子菜单禁用)结构一致(未逐 id 对照)。

### B. 模块判定表

| 模块 | 判定 | 依据 |
|---|---|---|
| ui_hscroll.cpp(本轮重点) | **严格 1:1** | 全函数对照 x64 sub_7FF7CB45E060(见 D) |
| command_dispatch.cpp | 严格 1:1(About 文本见 C-1) | case 集合、默认链、0xCF/0xD3 直接 case 抽查 |
| command_file_menu.cpp | 基本一致 | 抽查 case 212、0xCF quick-save guard、GroundShadow 子类(未提交新增实现)全部逐指令一致;其余未逐条 |
| command_view_menu.cpp | 基本一致 | case 276/277 的未提交 vtable 修复与原版 0x7FF7CB46F9EE `call [rax+128h]`、gate `wrapper+0x3A9E0==0` 核对正确;2948 行未逐条 |
| command_panel_toggles.cpp | 基本一致 | case 494 空槽 guard 为防御性偏离(C-2) |
| command_frame_edit.cpp | 未逐条(覆盖完整) | case 424 对话框流、0xFA/0x120/0x121 灰显抽查一致 |
| command_frame_register.cpp | 未逐条(覆盖完整) | no-op 位对齐、case 553 goto-frame、566/567 抽查无异常 |
| frame_line_edit.cpp / frame_range_apply.cpp | 未验证(细节) | 结构与拒绝条件(scale<1e-5/==1.0 拒绝)有注释,未对原版 0x43E970 抽查 |
| dialog_procs.cpp | **严格 1:1** | FrameRangeDlgProc/SelectNavDlgProc 全文对照 sub_7FF7CB475F50(见 D) |
| misc_dialogs / model_edge_dialog / physics_model_dialog / dialog_select_ops | 基本一致(快扫) | 均带 x86↔x64 双锚点映射表与行为注释;physics 编辑器 100000(x64)/10000(x86) 容量分架构;未逐条 |
| frame_driver.cpp | 基本一致 | 帧段 7(AVI 记录启动)、Present 四分支、FPS 计数与 29→30/59→60 吸附一致;Kinect 泵移位有注释论证 |
| timeline_advance.cpp | 基本一致 | 四全局轨道+附件轨道、相机贝塞尔(x86 x87/x64 SSE 双精度路径)、光轨 gate 修正均有 x64 证据锚点 |
| playback_state.cpp / playback_catchup.cpp | 基本一致(追赶核心严格核对) | past-end 循环/停止分支逐指令对照 0x7FF7CB44BBF0 区(见 D) |
| frame_modes.cpp / frame_modes_bone.cpp / reset_app_state.cpp | 基本一致(快扫) | 头部偏移表+流程注释完备;一处常量待确认(C-4) |
| 优先级2 全部(ui_*.cpp、wndproc*、app 其余、features、media、io_device_helpers、winmain) | 快扫无异常 | 全部为带 VA 的完整移植;stubs.cpp 已消灭(late_ports.cpp 账本 "418 ported / 0 stub");DIAG 钩子均为环境变量门控 |

### C. 发现清单(按严重度)

本轮**未发现 P0/P1/P2 级差异**。以下为 P3 级与观察项:

- **C-1 (P3) About 对话框产品名**:原版 0x7FF7CB54F930/0x54F980 = `"MikuMikuDance Ver.%4.2f\n  (64bitOS Version)..."`;移植 `command_dispatch.cpp:463-466` 写 `"MikuDanceStudio Ver.%4.2f"`。版本号 9.32、"(64bitOS Version)"(x64 分支)、作者名(EN/JP Shift-JIS 字节)均一致——应为开源项目有意改名,非行为错误,但与"严格 1:1"不符,需产品层确认。
- **C-2 (P3) case 494 空槽防御 guard(未提交新增)**:原版 0x7FF7CB460817 直接 `mov rsi,[rbx+slot*8+0BE8h]; cmp [rsi+3110h],...`,选槽为空即空指针解引用(崩溃);移植 `command_panel_toggles.cpp:692-699` 新增 `model==nullptr` 时跳过骨骼扫描、保留尾部 BM_SETCHECK(0x1EA)+WM_COMMAND(0x111,0x1EA)+双语言扫尾。不可达路径上的防崩溃偏离:崩溃 vs 静默跳过。判定为可接受的防御,但属行为差异。
- **C-3 (P3) 光方向 echo 的手动舍入**:`ui_hscroll.cpp:136-143 FormatLegacyOneDecimal` 用 floor(|v|*10+0.5)/10 预舍入再 `%+3.1f`,模拟 VC9 CRT;原版 x64 直接 `sprintf_s("%+3.1f")`。二者在可精确表示的 half 值上结果相同,理论上极端双精度中间值可能有 1 ULP 文本差异。防御性,影响可忽略。
- **C-4 (P3, 待确认) frame_modes.cpp 运行时常量**:`app/frame_modes.cpp:27` 记录 `0x52D738 = best-evidence PI/360 (unknown, TODO(port))`,取 g_MouseScaleC=0.05000000074505806;同文件后文已修正 0x52E8C0 旧猜测为 0.002f。原版该 float 在运行时初始化、初始化器未定位——按现有证据无矛盾,但此常量的原始出处未闭环。
- **观察-1**:D3DX 改为运行时动态加载(d3dx_dyn),dll 缺失时跳过投影重建/截图保存;原版为 load-time import(缺 dll 无法启动)。仅在异常环境下行为弱化,正常环境等价。
- **观察-2**:frame_driver.cpp 的 `MIKUDANCESTUDIO_DIAG` A/B 工具(约 180 行)全部由环境变量门控、默认零作用,不改变行为,但体积上混入主驱动文件。

### D. 已验证一致项(两边均有证据)

1. **HandleHScroll(ui_hscroll.cpp ↔ sub_7FF7CB45E060 全函数)**:四条 morph 滑条(gate `idx>0 || physics==2` + `idx>=0`、写 192 stride+56、"%5.4f");RGB 455-457(×0.00390625 双槽、EM_SETSEL+EM_REPLACESEL、"%3d" of ×256);光方向 458-460(/100、mirror、vtable+408=槽51 SetLight(0, light@+0x9E17C));FOV 447(×π/180、fov/near1/far100000、SetTransform 槽44);560 物理倍率((10000-pos)/100000、dirty 0xA1C61、时间轴刷新);**时间轴 428**:SB_LINEUP/DOWN 的 `浮动窗!=0 || 鼠标X<=0xA1DB4` 门(否则相机距离±1.0)、PAGEUP/DOWN 按 nPage(5184)、SB_THUMBPOSITION `HIWORD-nPos(5188)` 增量式、无符号回绕 >0xFFFEF920 归零、PanelPaint(sub_7FF7CB480EA0)、wave 门(0xA16CC)下 TimelineDrawTicks+InvalidateRect{6,95,宽-3,146}。
2. **滑条尾部的"刷新标志"折叠**:原版内联 `DWORD(app+0xA1378)=256/1/0x10000`(=四 track 标志字节 (0,1,0,0)/(1,0,0,0)/(0,0,1,0))+guard+清 255 对象 flag+PostLanguageSweep,与 port `RefreshRequest(-2/-1/-3)`(sub_440AC0 语义)逐位等价。
3. **FrameRangeDlgProc ↔ sub_7FF7CB475F50**:INITDIALOG 全序列(浮动窗 0xA1DA0→SetWindowPos flags 3——Hex-Rays "HWND_MESSAGE|2" 渲染错误已被 port 正确识别为 HWND_TOPMOST;425/426→686/687 atol-"%d"-EM_REPLACESEL 镜像;605 预置 "1.0";688/689/690 勾选;焦点 605→EM_SETSEL(0,3)→焦点 686→全选);OK→ApplyFrameRangeScale(sub_7FF7CB4BD470)→EndDialog(1),Cancel→EndDialog(2)。
4. **GroundShadow 颜色编辑子类(未提交新增实现 ↔ sub_7FF7CB476D30)逐指令一致**:guard(WM_KEYDOWN+VK_RETURN+GetDlgItem(0xA1B98 对话框,0x272))→CallWindowProcA(0xA1BA0 旧 proc);GetWindowTextA(…,8)→atof→同一 float 写 0xA1D88..0xA1D94 四槽→TBM_SETPOS 发往控件 **458(0x1CA)**(该控件在模板中不存在、GetDlgItem 返 NULL、消息空发——原版 quirk 原样保留)。
5. **case 212 屏幕尺寸对话框流**:[app+0xC0]=1→DialogBoxParamA(模板 0x28D(EN)/0x25F(JP) 按 0xA1B90)→ret==2 退出→[0xA1B31]=1→视口刷新→AVI 背景(0x13EC==1)/图片背景(0x9F310!=0)分别刷新→布局→InvalidateRect,顺序一致。
6. **播放追赶 past-end 分支(playback_catchup.cpp ↔ 0x7FF7CB44BBF0..0x7FF7CB44BD09)**:循环态重读 0x199 编辑框(8 字符)→atoi→cvtsi2ss/30.0f→起始秒;恢复保存的物理模式(0xA167C);cursor 复位;UpdateBoneFrames;音频 reseek 门 `0xA16EC!=0 && 0xA137D==0`;T0=0xA1BB8 重锚。停止态:0x368=0→StopPlayback(sub_7FF7CB48A980)→BM_SETCHECK(0x198,0)→**跳过**末次 settle(port 注释与跳转 0x7FF7CB44BE88 一致)。
7. **60fps 追赶循环**:cursor+=1/60(x64 addss 0x3C888889 单精度,port 从之)、while cursor<target 内 PlaybackPoseAdvance(1)+有序 morph/物理 pass(comboSelIndex2=model+0x3109 排序,255 界)+运动学同步+stepSimulation(1/60,10,1/60)+dt 预算扣减;BLOCK1(帧步)与 BLOCK2(播放,含选中模型跳过门 `0x328==0 && 0x13E0==slot`) 及 `物理计数>=1` 门(0x7FF7CB44BA6A/0x7FF7CB44BD0E)一致;帧步目标公式 N·30/fps 单精度路径与 x64 一致。
8. **撤销/重做(bone_edit_undo.cpp ↔ 0x7FF7CB447020 区)**:EnableWindow(0x190 undo,TRUE)/(0x191 redo,FALSE);model+0x3558=1/+0x3559=0;环游标 +0x3550 `inc→>=0x1E(30)→清 0`、镜像 +0x3554;槽基址 model+0x27A8、stride 40(`lea [rax+rax*4]×8`)、槽内 +0 operation=1、+4 选中数、+0x10 快照指针(delete[]→new 0x24×count);快照 36 字节/骨(索引+trans+quat+物理脏)。**深度 = 30 步**,注册时机=骨骼编辑入口(选中数>0 才入栈)。
9. **case 276/277 未提交修复**:原版 x64 `call [rax+128h]`(0x7FF7CB46F9EE)/`[rax+1C8h]` 与 x86 +0x94/+0xE4 同槽(296/8=228/4=37;456/8=228/4? → 57),gate `wrapper+0x3A9E0==0`;port 改为 `device->SetRenderTarget(0,surf)` / `SetRenderState(0xA1,on)` 类型化虚调用——参数序与槽位语义吻合,是 x64 构建下裸偏移错槽的正确性修复。
10. **命令 case 0xCF**:入口 guard `[app+0x54]=1`@0x7FF7CB46E041 → 有存储路径走快速保存、否则 save-as 流,port dialogFlags[8] 一致。

**总体结论**:所辖窗口/命令/对话框/帧驱动子系统与原版 v932x64 行为对应质量很高——命令面 100% 覆盖、重点函数(hscroll、dlgproc、追赶、撤销环)逐指令级核对通过;未发现功能性缺失或行为错误级差异。所有差异均为 P3(品牌字符串、不可达路径防崩溃、防御性舍入、一个未闭环常量)。未逐条核对的三个大命令家族文件(frame_edit/view_menu/file_menu 各 2-3 千行)建议后续轮补做抽样加密。
