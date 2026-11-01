# 2026-11-01 用户反馈核查：与原版 MMD v9.32 x64 对照

用户补充 sdPBR480 压缩包后的真实 GPU、AVI 逐帧及 PMM 重载测试，见 [追加核查](SDPBR480_AVI_AUDIT_2026-11-01.md)。下文保留初次核查及已提交修复的历史记录。

Issue #12 后续补充的“日语名称栏缺少骨骼名”在 v10.16 中仍存在，原版使用的全角冒号格式串曾被 IDA 截断显示，导致移植遗漏。原版字节证据、修复及回归结果见文末追加记录。

追加 IDA 核查已确认对象缓存缺少几何生命周期判别、球面贴图模式返回值宽度错误两处差异，并修正工作树代码。用户恢复桌面后的最终 GPU 对照中，Sour + sdPBR480 两套 MME 的稳定预览、31 个 AVI 录制帧及重载停止图逐像素一致；x64、x86 定向回归各 4/4 通过，详见追加核查的最新结论。

## 范围与结论

初次核查对象是 `v10.15`，HEAD `35d99c7`。反馈者使用的版本未知，不能倒推反馈发生时使用的版本。下文核查表、差异依据和初次测试记录描述修复前状态；随后按用户要求完成的修复见文末“已完成修复与验证”。

通过 IDA MCP 检查用户指定目录 `../OpenMMD/MikuMikuDanceE_v932x64/` 中的原版 EXE、MMEffect.dll 和 MMHack.dll，并与当前源码逐项比较。以下“确认”表示找到了可以解释反馈的具体实现差异；不表示已经完成原版与复刻版的整套 GUI / AVI 图像 A/B。

| 用户反馈 | 修复前判定 | 原版与修复前实现的关系 |
|---|---|---|
| 顶部“显示 / IK / 外部亲”没有关键帧 | **尚未确认** | 两边都有此轨道的关键帧绘制与命中映射，不能认定它本来就不应有关键帧。需要触发时的模型、PMM、帧号及列表状态。 |
| MME 界面是中文，希望支持多语言 | **确认日语分支资源有差异** | 原版日语菜单是日文；复刻版日语资源中写入了中文。当前已有英语分支，所以不是完全没有语言切换。 |
| 部分效果存在延迟，可能是顺序问题 | **尚未确认** | 已检查帧开始、效果计划及场景结束路径；没有具体效果和场景，无法判断是哪一层的顺序或时序问题。 |
| Morph 左右箭头索引错误 | **确认实现错误** | 四类箭头都调用同一 helper，helper 却始终筛选眉类别；原版分别筛选 1、2、3、4。 |
| 点击骨骼 / 表情名称，选中但没有变红 | **确认缺少刷新调用** | 普通名称行改变选中状态后没有重建名称位图；原版在进入行类型分支前有一次公共重建调用。 |
| 必须放置 `Data\MMDxShow.dll`，否则报错 | **属实，但原版也有相同要求** | 两边 manifest 和建图失败提示相同。发布脚本已正确打包；直接构建输出没有自动创建该运行目录布局。 |
| AVI 输出时部分效果禁用 | **尚未确认具体效果故障** | 当前录制路径仍调用 MME 场景结束及拷贝前效果处理；不能将录制时跳过调试 / 操作轴叠加层等同于禁用 MME。 |
| AVI 完成后开始播放 | **尚未复现；正常收尾与原版一致** | 两边收尾均清除播放状态并调用停止播放。仍需实际导出操作序列排查其他消息 / 待处理启动状态。 |
| 多次加载 PMM 后偶发黑屏 | **尚未复现** | 需要完整场景及效果资源。本轮场景重置测试异常、GPU 测试跳过，不能作排除结论，也不能把测试异常直接当成该黑屏的复现。 |

## 二进制与地址

SHA-256 已同时通过 IDA 元数据及本地文件哈希核验：

| 文件 | SHA-256 |
|---|---|
| `MikuMikuDance.exe` | `07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1` |
| `MMEffect.dll` | `74b3f5882a3bad28131cbc57994e2f960938e6ca850fc7139018ce4822bd87dc` |
| `MMHack.dll` | `e450b25e5cd2bcf125ca4b8eb9e57f858ec3ca379633c7d25f9dd40e864b390d` |

本轮 IDA 中 EXE 基址为 `0x7FF7CB420000`，两个 DLL 基址为 `0x180000000`。下文统一给 RVA，定位时加各自基址；函数名采用当前 IDB 自动名称。大窗口过程无法完整反编译的部分，使用 IDA MCP 指令范围查询核对，未凭源码中的历史地址注释代替原版证据。

## 已确认的三处差异

### Morph 类别箭头

当前 [command_frame_register.cpp](../src/window/command_frame_register.cpp) 的 `MorphStep` 接收 `lane=0..3`，但退出筛选循环的条件固定为：

```cpp
if (m->morphs[idx].panel == mdl::MorphPanel::eyebrow)
    break;
```

八个左右按钮都经过该 helper。因此眼、口、其他类别可能把 `selectedMorphs[lane]` 写成眉类别索引，同时 `SyncMorphRow` 仍在原类别下拉框中移动光标，造成名称、值及内部选中索引不一致。眉类别本身不受此类别错误影响。

原版 EXE 窗口过程 `sub_7FF7CB45F550` 内的类别比较如下，`+0x60` 是 morph 的 panel 字节：

| 类别 | 原版选中索引字段 | 代表性指令 RVA | 比较值 |
|---|---|---|---|
| 眉 | 模型 `+0x3130` | `0x510F7` / `0x5130E` | `1` |
| 眼 | 模型 `+0x3134` | `0x5148E` / `0x516AE` | `2` |
| 口 | 模型 `+0x3138` | `0x5182E` / `0x51A4E` | `3` |
| 其他 | 模型 `+0x313C` | `0x51BCE` / `0x51DEE` | `4` |

修复方向是按调用者的类别筛选，并验证内部索引、下拉框光标、滑块 / 文本三者一致。`tests/morph_navigation_test.cpp` 覆盖的是时间轴前后关键帧跳转，不覆盖这里的类别箭头，不能用该测试通过证明此行为正确。

### 普通名称行缺少选中色重绘

当前 [ui_editor_click.cpp](../src/window/ui_editor_click.cpp) 的 `HandleLButtonDown_NameColumnHit` 会修改骨骼选中数组或表情显示项的 `selected`。随后普通行 `type <= 0` 仅调用 `InvalidateSelectionReadouts` 并返回；只有若干标题 / 分组行分支调用 `RebuildTimelineTrackPanel`。

`InvalidateSelectionReadouts` 在 [ui_view_refresh.cpp](../src/window/ui_view_refresh.cpp) 中更新视口读数区域，不重建时间轴名称位图。[ui_panel_sweep.cpp](../src/window/ui_panel_sweep.cpp) 已正确使用 `themeColors[34]` 绘制选中项，默认配置为红色；问题是点击普通行后没有执行该绘制路径。

原版点击处理 `sub_7FF7CB459C60` 的选择路径汇合后：

```asm
; EXE RVA 0x3A299
mov rcx, rdi
call sub_7FF7CB47F7F0  ; RVA 0x3A29C，重建名称 / 时间轴面板
; 此后才读取模型行类型并分支
```

原版重建函数 RVA `0x5F7F0` 也按选中标志选择文字颜色。当前遗漏了这次公共重建调用，可以解释“选中已生效、名称未变红”；其他操作触发重建后可能又显示正确颜色。修复时应保留普通行及 Shift 多选的即时刷新，并检查不重复触发分组切换。

### MME 日语资源实际上是中文

[MMEffect.rc](../third_party/mmeffect/res/MMEffect.rc) 中标为 `LANG_JAPANESE` 的菜单包含“特效配置”“自动更新”“文件”“编辑”等中文，映射窗口还混有日文标题。英语菜单及运行时英语控件切换已经存在。

本轮用 `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)` 只读加载二进制资源，并用 `LoadMenuW` / `GetMenuStringW` 验证了实际编译结果：

| 资源 / 项目 | 原版 DLL | 当前 EXE |
|---|---|---|
| 日语菜单：效果映射 | `エフェクト割当(&M)` | `特效配置(&M)` |
| 日语菜单：自动更新 | `自動更新(&O)` | `自动更新(&O)` |
| 英语菜单：效果映射 | `Effect Mapping(&M)` | `Effect Mapping(&M)` |

原版资源 ID 为 101 / 107；嵌入复刻版 EXE 后资源 ID 为 50101 / 50107。原版 `MMEffect.dll` RVA `0x55890` 的菜单初始化根据 `ExpGetEnglishMode` 选择 101 或 107；当前 [mme_ui.cpp](../third_party/mmeffect/src/mmeffect/mme_ui.cpp) 保留该选择逻辑。

因此可以确认日语体验与原版不同；“完全没有多语言支持”则不准确。修复方向是恢复日语分支的日文，若增加中文，应使用独立资源 / 语言选择，避免占用日语分支。

## 其余反馈的核查依据与边界

### 顶部显示 / IK / 外部亲轨道

原版 EXE 的面板绘制函数 RVA `0x60EA0`，在 RVA `0x61BCD..0x61D41` 遍历模型 `+0x27A0` 的 40 字节显示关键帧链，以模型 `+0x3158` 作为列表行号，绘制图标并写入命中表；零号记录使用 `-10` 哨兵。

当前 [ui_panel_paint.cpp](../src/window/ui_panel_paint.cpp) 的 `displayKeys` 分支保留了同样的遍历、`boneListSelLine != 0` 门槛和命中哨兵。[ui_panel_sweep.cpp](../src/window/ui_panel_sweep.cpp) 也为第 1 显示组设置该行号。静态检查没有找到“该轨道完全未实现”的证据，但尚未覆盖反馈所用模型的加载结果和 UI 状态。

### MMDxShow 的目录要求

只读提取两边 EXE 的 `RT_MANIFEST`，均包含 `Data\\MMDxShow.dll` 及 CLSID `{2F1713B8-DD1F-4186-93BE-FDA50BF687C7}`。原版 EXE 建图函数 RVA `0xAAB0` 在源过滤器创建失败后，RVA `0xABF7` 使用与当前相同的英文错误提示，随后清理并返回失败。

当前对应 [dshow_record_graph.cpp](../src/app/dshow_record_graph.cpp) 的录制图构建。编辑器能继续使用不代表 AVI 输出也能成功。CMake 将 DLL 生成到 `Release/MMDxShow.dll`，没有自动复制到 EXE 下的 `Data/`；[release.py](../scripts/release.py) 已将其打包为 `Data/MMDxShow.dll`。这是直接构建后的运行目录整理问题，不是原版没有的 DLL 依赖。

原版 x64 `Data/MMDxShow.dll` 通过了本轮重新构建的 `mmdxshow_protocol_test.exe`，退出码 0；本项目 DLL 的同项 CTest 也通过。协议测试不验证完整编码器建图 / AVI 输出。

### 效果延迟与 AVI 中效果失效

原版 `MMEffect.dll` RVA `0x574E0` 在 `OnBeginScene` 中更新帧时间、缓存变换并重建效果计划；RVA `0x58400` 的 `OnEndScene` 按全局效果开关执行后处理函数 RVA `0x5E210`。当前 [callbacks.cpp](../third_party/mmeffect/src/mmeffect/callbacks.cpp) 保留这些阶段。

原版 `MMHack.dll` 的 `GetRenderTargetData` 包装 RVA `0x3C50`，在源表面是所跟踪 RT 且本帧尚未触发时，先调用结束场景效果回调，再转发读取；`EndScene` 包装 RVA `0x3CB0` 用同一标志避免重复调用。当前 [mme_host.cpp](../third_party/mmeffect/src/mme_host.cpp) 的 `MmeHostPreRenderTargetCopy` / `MmeHostEndScene` 保留该机制。[capture_downsample.cpp](../src/render/capture_downsample.cpp) 在录制表面读回前调用该桥接。

[frame_scene.cpp](../src/render/frame_scene.cpp) 的录制分支仍执行 `mme::EndScene`，随后跳过调试几何、操作轴和屏幕叠加层。这本身不能解释某个 sdPBR 材质 / 后处理消失，也不能证明 sdPBR480 完整兼容。必须提供具体效果文件及依赖、分配顺序、导出设置和同帧预览 / AVI 结果后，才能检验时间输入、RT 切换、设备重置及效果脚本执行的实际差异。本轮没有完成这样的 A/B。

### AVI 结束后的播放

原版 EXE 收尾函数 RVA `0x894C0` 的关键状态写入是：

```text
0x894DB  playbackActive = 0
0x894E2  调用 StopPlayback（函数 RVA 0x6A980）
0x894EE  frameStepPlayback = 0
```

当前 [late_ports.cpp](../src/app/late_ports.cpp) 的 `FinishAviRecord` 有相同操作。帧循环中另有 `recordPlaybackStartPending` 启动分支，用于开始录制播放；仅凭该分支存在，不能认定导出完成后一定误启动。需要记录正常完成 / 中止、起止帧、是否循环及完成后的消息序列。本轮没有复现反馈现象。

### 重复 PMM 后黑屏

当前 PMM 加载会重置场景并在尾部通知 MME。MME 对象缓存的清退 / 注册发生在帧开始。原版 `MMHack.dll` RVA `0x2DE0` 同样在 `BeginScene` 收集存活 ID、清退消失对象并注册新对象；不能把“有对象缓存”本身判为复刻版特有错误。

尚无触发黑屏的 PMM 和效果资源，也未完成相同场景重复加载的图像对照。现有 `mme_host_object_cache` 测试覆盖消失对象的缓存清退及清退后 ID 复用，不等于完整 PMM 重载 / RT / 效果恢复验证；`scene_reset` 主要覆盖重置后的状态和控件，不是画面黑屏检测。

## 初次核查验证结果（修复前）

- VS2022 / CMake x64 Release 主程序及以下相关 C++ 测试目标已从当前源码重新构建。构建完成；有现存的零长度数组、有符号比较等编译警告。
- 重建后的定向 CTest 共选择 19 项：**14 项通过，1 项失败，4 项跳过**，用时 8.88 秒。
- 通过项：`key_registrars`、`mmdxshow_protocol`、`pmm_display_group_save`、`pmm_playback_options`、`model_ui_edit`、`model_ui_navigation`、`morph_navigation`、`mme_anime_timeline`、`mme_anime_recovery`、`mme_posteffect_message`、`mme_host_object_cache`、`gui_export_comparison`、`mme_initialization_test`、`embedded_mme_resources`。
- 失败项：`scene_reset`，CTest 报告 `Exception: SegFault`，没有正常测试输出；单独重跑也在约 3.4 秒后发生同样异常。尚未定位异常栈；不能宣称场景重置验证通过，也不能把该异常直接归因为反馈中的偶发黑屏。
- 跳过项：`mme_buffer_pass_gpu`、`mme_pass_readback_gpu`、`mme_assignment_graph_gpu`、`mme_snapshot_cache_gpu`。日志出现 D3D9 HAL 创建失败 `0x8876086C` / `0x8876086A`，因此本轮不具备这四项的有效 GPU 验证结果。
- `gui_export_comparison` 是图像比较工具的 Python 测试，并未操作原版 / 复刻版生成 AVI。`embedded_mme_resources` 检查资源存在，不检查翻译内容。

建议后续先修复已定位的两个 UI 行为错误及日语资源，再对反馈中的完整场景做原版 / 复刻版同帧导出与重复加载对照。未知版本及缺少素材的五项应保持“待复现”，不要记为已修复或原版同样有故障。

## 已完成修复与验证

2026-11-01，根据用户要求，依次修复上述已确认差异，未更改尚未复现问题的实现：

1. `MorphStep` 按 `lane + 1` 对应的实际类别筛选，不再固定筛选眉。保留原版双向循环、完整一圈后的返回、未选中索引和 PMD 基础表情的早退规则。
2. 名称点击处理恢复原版在选择后、行类型分支前的公共 `RebuildTimelineTrackPanel` 调用。普通骨骼、表情、根骨骼立即刷新颜色，组标题仍保留原版切换后的第二次刷新；颜色继续使用主题配置。
3. MME 日语菜单和映射对话框恢复原版日文。进一步核验发现映射对话框模板初始尺寸为原版 `373 × 215`、复刻版 `388 × 244`，一并恢复原版尺寸和字体名。英语分支和命令 ID 保持原版定义。

针对性验证：

- 新增类别箭头用例：通过生产 `CommandDispatch` 操作真实 Win32 控件，覆盖四类表情交错排列、两端循环、其他类别索引保留、下拉框 / 滑块 / 数值同步、单一条目及基础表情边界。添加用例后旧实现失败，修复后通过。
- 新增名称刷新用例：执行生产点击处理和面板重建，仅观察最终绘制参数；覆盖日英名称、自定义主题颜色、骨骼 / 表情 / 根骨骼、Shift 增选 / 取消及骨骼组 / 表情组 / 显示标题。旧实现检出漏刷，恢复公共调用后通过。标题用例检查最终绘制结果，保留原版两次重建的顺序。
- 扩展嵌入资源测试：核验日英菜单关键文字、日语映射对话框文字及原版模板尺寸。旧中文资源失败，恢复后通过。
- x64 与 x86 Release 主程序及相关目标均构建成功。定向 CTest 各 **8/8 通过、无跳过**，用时分别为 10.21 秒和 11.43 秒：`key_registrars`、`input_sequence`、`model_ui_edit`、`model_ui_navigation`、`bone_pick`、`model_viewport`、`morph_navigation`、`embedded_mme_resources`。
- 直接提取两架构新 EXE 的四份 `RT_MENU` 资源，与原版 MMEffect v0.37 x64 的 101 / 107 / 109 / 110 逐字节比较全部相同。日志对话框 105 原始字节相同；映射对话框 108 仅将嵌入 EXE 的私有菜单引用 50109 归一为原版 109 后，全部字节相同（包括控件位置、文字、字体和样式）。

以上证明本次修复范围内的行为与资源恢复了原版规则，不代表完整 MME、sdPBR480 或 AVI 输出已完成端到端兼容性验收。初次核查中的 `scene_reset` 异常、GPU 不可用及其余待复现反馈仍按原记录保留；本次没有修复或排除这些问题。

## Issue #12 追加核查：日语名称栏缺少骨骼名

用户补充的第二个现象属实：v10.16 的日语界面选中骨骼后，视口上方仍只显示模型名；英文分支可以显示模型名与骨骼名。这与前述“时间轴名称选中后没有变红”是两个独立的绘制问题。前者位于 `HandlePaletteChanged2`，后者已在 v10.16 恢复名称位图刷新。

### 原版 IDA MCP 证据

本次仍使用上文 SHA-256 为 `07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1` 的原版 EXE，基址 `0x7FF7CB420000`。状态栏绘制函数为 `sub_7FF7CB4404D0`（RVA `0x204D0`）。

| 界面 | 原版选中骨骼时的格式串 | 格式串 RVA | 格式化调用 RVA |
|---|---|---|---|
| 英语 | `%s : %s` | `0x12D118` | `0x208B4` |
| 日语 | `%s：%s`，使用全角冒号 | `0x12D17C` | `0x20ACA` |

关键陷阱是：IDA 的字符串条目及 `get_string` 把日文格式串显示为 `%s`，反编译结果因此也不能直观呈现完整文本。继续通过 `get_bytes` 读取并核对 EXE 文件偏移 `0x12C17C`，实际零结尾字节为：

```text
25 73 81 46 25 73 00
%  s [ ： ] %  s \0
```

`81 46` 在 CP932 中是 `：`，不是字符串终止符。原版的 RVA `0x20A99` 把该格式串传入 `r8`；`r9` 指向模型 `+0x22C0` 的日语名，第五个参数是骨骼表 `+0x2748` 加选中索引乘 `0x270`，指向骨骼日语名。原版先检查 `selectedBone`（模型 `+0x311C`）是否为负；未选中时才使用单独的 `%s`。

因此原版并不存在“传了骨骼参数但将其忽略”的行为。`ui_palette.cpp` 中此前这样解释的注释有误，连同只输出模型名的移植代码一起修正。

### 修复与验证

日语分支恢复选中索引判断及完整 CP932 格式串 `%s\x81\x46%s`，显示“模型名：骨骼名”；未选中骨骼时继续仅显示模型名。保留原版英文的空格、半角冒号，以及两种语言各自的字号、位置和播放状态文字。

扩展 `model_ui_navigation` 回归，执行生产时间轴点击处理和 `HandlePaletteChanged2`，只在最终 `DrawGlyph` 入口观察输出。覆盖 CP932 模型名与骨骼名、日英切换、切换骨骼、固定根骨骼、未选中且骨骼表为空的状态，以及播放状态文字。

- 添加用例后，未修复的 x64 实现出现三次日语骨骼名称断言失败；英文与未选中状态通过。
- 修复后，x64 与 x86 Release 主程序及测试目标均构建成功；`model_ui_navigation` 各 **1/1 通过，无跳过**，同时保留原有的名称变红回归覆盖。
- 此追加修复纳入 v10.17；此前已经发布的 v10.16 资产不包含这一修复。
