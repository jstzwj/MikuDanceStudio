# 第八轮审计·修复执行记录（2026-09-13/14）

流程：22 个子代理分两波（12 修复 + 10 深验/裁决），每个问题"先 IDA 核验、后修复"；主代理终审 3 处代理间矛盾并收尾。双架构（x64/x86）Release 构建通过。所有改动在工作树未提交。

## 一、第一波修复（P1/P2 落地）

| # | 问题 | 结果 |
|---|---|---|
| 1 | STANDARDSGLOBAL Script 清空 techniqueOrder（P1）+ 表B移除数组拒绝（P2）+ 表A末门定论 | 修复×2；末门定论=Elements（栈槽偏移 0x20 证实），现状本就正确 |
| 2 | SAS 脚本命令大小写敏感（P1）+ 预处理只折叠分号串（P2） | 修复×2（命令原文比较、分词按原版正则跨度，词内空白→语法错误） |
| 3 | Time 语义绑 g_frameTimeBase（P1）+ 语义匹配全程 _stricmp（P1）+ 删调试日志 | 修复×3 |
| 4 | PMX 附加 UV1-4 morph 基线按族取 additionalUv（P1）+ count 加载期去重（P2）+ VB/IB Pool→MANAGED（P2）+ LockRect READONLY（P3） | 修复×4；Null_%02d 审计误报（原版槽0后置1，移植本就正确） |
| 5 | 重力 y → -98.0f（0xC2C40000，x64 0x7FF7CB552CA8 实测；P1） | 修复 |
| 6 | morph 四元数重缩放 x64 单精度链（acosf/sqrtf/sinf/cosf；P2） | 修复（_M_IX86 双路径惯例） |
| 7 | 背景四边形 TRIANGLELIST（P1 真实bug）+ TCI 字面量 0x10000 钉死 + 线批 SetTexture(0,null)（P2）+ 遍历注释换位 + 死代码删除 | 修复×5；TCI 审计误报（D3DTSS_TCI_CAMERASPACENORMAL 本就=0x10000，0x30000 是 SPHEREMAP） |
| 8 | toon 默认表 | 审计误报：移植字面量与原版 30 项逐位一致（原审计"精确小数"系粗读错误）；仅修注释（14→30） |
| 9 | About Shift-JIS 字节 + 删调试日志 + 子类化先于菜单（含递归防护）+ 40020 dispatch 无操作对齐 | 修复×4；default 行门——原审计前提被推翻（原版用 TCM_GETITEM 读 lParam，Main 项 lParam=0 时 default 行可选中可写），移交第二波 |
| 10 | OnLostDevice 五步释放序（cachedTargetSet→快照缓存→状态块池→run-state→效果）+ OnResetDevice 去重改共享 set 语义 | 修复×2 |
| 11 | VMD 宽路径 _wsopen_s + IK 条目 new/delete[] 配对（另修 2 处同类错配）+ PMM 版本 strstr 基准 | 修复×4；相机分支返回值受阻（ApplyGravityTrack 为 void、遍及 17 文件；返回值无消费者=无可观察差异，仅注释） |

## 二、第二波深验与修复

| 代理 | 结果 |
|---|---|
| J（桥接重试） | Ctrl+Shift+E：审计前提推翻——MMHack hook 是行为学死代码（MMD 全 exe 唯一 GetKeyState 调用点的 62 项键表不含 'E'，无加速键表），核验结论入注释，不加屏蔽。模型路径宽字符直通：新增 MmdModelPathW/MmdAcsPathW 内部直取（model+0x24BC/acc+0x29C），材质表 _wfopen_s 全程宽字符。GetDrawnWindow：槽位勘误（0x180003cf0=Present 槽17，"第4参"=hDestWindowOverride），桥接 BeginScene 按主泵门控每帧预置 override |
| W2（分配对话框） | 门函数重写为 TCM_GETITEM(TCIF_PARAM) 读选中项 lParam（Main=0）；default 行恢复可选中/可写；-101 同步启用 40006/07/08/12+1007/1008；头部错误自述注释更正 |
| W3（SAS 残留） | Script= 未知技术：单名形式致命（原版 return 1 拒载）、A?B:C 链式仅告警——精确实现；只处理第一个 STANDARDSGLOBAL（加 break）；选择器 found 语义——原审计新说法又被推翻（setz 误读，移植本就正确）；资源对象深验：0x27 重建前擦深度注册表键（修复），另报告 4 项非行为差异（磁盘纹理缓存/加载模式门/Description 填充/ViewportRatio 时机） |
| W4（pass_planner/anime/engine） | 快照族 3 修复：色彩探测 familySpecial 时传 {0,0,W,H} 目标矩形、多重采样 GetDesc 改读持久集（mgr+0x10/+0x30）、e210 记录门改 MmeRecordOffscreen；anime_texture 3 修复：PNG IEND 门失败走 "failed to open"+清零帧数、时间线改 gcd/LCM 精确有理数（2^48 锁存退 double）、SetFrame 零帧门；删除无引用 MmeReleaseBindingContextPool；effect_engine 视口缓存与 ini 预检核验为无需修改 |
| W5（名称表机制） | 24 项名称表全量落地：枚举 + _stricmp + 形状门 + 枚举句柄（弃 GetParameterByName）；ToonColor 改材质路径；删除 _INDEX 运行时绑定（原版仅编译期宏）；Place 保持只解析不设值。⚠ 其"DifColor→DiffColor"拼写与 Time id 质疑均为主代理终审推翻（见三） |
| W6（渲染深验） | 阴影图斜率/缩放/重定向矩阵逐指令一致；材质三分支 3 修复：模型头 RS(137)+SetTransform(TEXTURE1, halfTexel) 缺失、PMD sphere 分支补 SetTransform(TEXTURE2, halfTexel)、COLOROP 条件修正（.sph→MODULATE，其余含 .spa→ADD——旧移植相反）；cameraGate 标志语义验证通过（[13E4]=editMode 枚举，UsesViewportTool 为 >=None 正确）；device_reset x64 定位（sub_7FF7CB4C6160）逐项一致；PMX IB Pool→MANAGED |
| W7（物理/蒙皮深验） | CreatePhysJoint（限位/弹簧/平衡点/uid/参数序）、readback 传送门与三段回写、SDEF 主公式与 BDEF4 加法序——全部位级一致无需修改；组合 morph 权重改原版两连乘 ((angle*w)*refW)；flip morph type 9 双侧一致不支持 |
| W8（PMM v2 装载段） | 逐字段复核挖出 4 个新 bug 并修复：【高】IK 重映射缺 boneMappings 间接层（骨表不匹配时 IK 状态全丢）；【高】跳过路径 displayOrder/previousDisplayOrder 字节被丢弃（跳模型后显示顺序整体错位）；【中】死键链修复未清 next；【低】r==3 重载后状态文本 "too" 变体缺失 |
| W9（命令家族抽样） | 4 处 P2 修复：帧范围缩放改 float 就近舍入+截断（x64 mulss 语义，scale=1.05/diff=20 即差一帧）；case 408 条件写反（== 误成 !=）；case 285 地面阴影 btRigidBody 字段偏移 0xD4→0xE0（x64 Bullet 指针加宽 12 字节，写错成员）；SelectFrameGroup/CopyBoneKeyPayload 多清 physicsDisabled 活数据（抹掉逐帧物理 OFF 标志） |
| W10（通知面裁决，后台） | vtable 定论：槽16(0x80)=0x180003d30=Reset（OnLost→真Reset→OnReset 包裹+设备窗口跟踪）、槽17(0x88)=0x180003cf0=Present（仅 drawn-window 记录，零 MME 通知）、槽15=GetNumberOfSwapChains 直通、槽18=GetBackBuffer 直通。第一轮"每帧 Present 触发 Resetting MME"正式撤回。移植通知面本就等价；唯一顺序偏差已修（OnResetDevice 提前至 Reset 后、InitRenderStates 前） |

## 三、主代理终审裁决

1. **Time/ElapsedTime 表序**（两代理矛盾）：语义表 A（0x1800B2FA8 起，步长 0x20）槽 0x22→0x1800B2CCC="Time"、槽 0x23→0x1800B2CC0="ElapsedTime"（"Time" 是 "ElapsedTime" 的尾串复用）；绑定函数 sub_180057B30：0x22→D9904(g_frameTimeBase)、0x23→D9900(g_deltaSeconds)、0x24(Time2)→D98FC、0x25(ElapsedTime2)→D98F8。**第一波 Time→g_frameTimeBase 修复正确**；W5 的"id 互换"质疑系把字符串地址顺序当表序，误报。
2. **DifColor 拼写**（W5 改错，已回滚）：名称表 B 全 24 项 get_bytes 解码，id 0x3B 的名字指针→0x1800B2AA0，get_string 实读 **"DifColor"（单 f）**。W5 的 "DiffColor（双 f）" 为误读；material_bind.cpp 的 kNameParams/注释/SetVector 已全部回滚为 DifColor；mme_globals.cpp g_paramNames[9] 本就正确无需改。
3. **EgColor/SpcColor/DifColor 合成路径**（遗留 B-3，已补齐）：sub_18005F830 解码——快照存在（effect_file_used +0x55）时 SetVector 快照 +0x198/+0x1B8/+0x1C8；否则取活设备 GetMaterial（槽50）+GetLight(0)（槽52）合成：DifColor=光漫射×材质漫射(α=1)、SpcColor=光镜面×材质镜面(α=Power==0?0.1f:Power)、EgColor=环境光×(kind==1?材质漫射:材质环境)+自发光(α=材质漫射α)。已在 MmeBindStandardParameters 按同门（effect_file_used）实现双路径。
4. **链接修复**：MMEffect.dll 无法解析 exe 内部符号 MmdModelPathW/MmdAcsPathW——新增 MmeHostSetPathProviders 注册函数（mme_host_api.h / mme_host.cpp / MMEffect.def / mme_bridge.cpp OnDeviceCreated），函数指针注入，不加 exe 导出面（保持原版 37 个 Exp* 导出对齐）。
5. 注释修正：sas_exec.h SasResource 析构"原版泄漏键"说法（已被 W3 推翻，改为 0x180014434 擦键事实）；model_skinning.cpp 五个 worker 注释地址 +0x2000 笔误。

## 四、遗留（已报告未修，按影响排序）

- sas_interpreter：磁盘纹理缓存（{路径,类型,mtime} 键，性能级非行为级）；sas+0x3A 加载模式门；Description 缺省以效果文件名填充；ViewportRatio 设备重建时用当前屏幕重算（移植为解析时固化）。
- mme_dlg：离屏对象 tab 项动态重建未实现（移植无离屏对象注册表；门函数已按 lParam 语义写好，未来补数据源即自动正确）。
- effect_engine：文件戳 32 位哈希 vs 原版 FILETIME（P3）。
- VMD 相机分支链式返回值：需 ApplyGravityTrack 改 int 返回（17 文件联动），返回值无消费者、无可观察差异。
- readback mode==2 尾乘 parent<0：原版读野内存（UB），移植保留守卫（有意不复刻 UB）。
- 命令家族 case 247/219 未逐指令 dump；PMM v2 相机/光照/自阴影/附件区段未达 per-model 段同深度。
- W9 P3：atol vs atoi、帧缩放 NaN scale 放行（输入 "nan" 才可达）。
- x86 原版二进制无 IDB：x86 分支的少数取舍以 x64 为基准（VMD 宽路径、重力、morph 精度路径的 x86 侧保持现状并注明）。
