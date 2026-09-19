# Audit 10：渲染 / MME 宿主集成审计

审计日期：2026-09-19。审计当前工作区（含已有未提交修改），不修改业务源码。结论：**不能宣称 MMD 9.32 x64 + MME 0.37 严格一比一**。已有真实、可定位的控制对象选择错误，以及源级抽象不足。去 DLL 注入路线正确，不应补回 d3d9 代理、IAT patch 或设备 vtable hook。

## 证据等级与范围

- 本次重新调用 IDA，分析用户指定 `MMEffect.dll` 的 `0x18005B9E0`、`0x180059AA0`、`0x180057BC0`，不是仅引用已有源码注释。
- 直接检查宿主桥、设备初始化/重置、MME 上下文、模型矩阵、控制对象绑定、渲染快照、SAS 执行结构及实际绘制接入点。
- 下方逐文件清单明确区分重点人工审读与结构扫描。未逐指令证明全部函数，未执行原版/移植版画面 A/B；不能把“存在实现”计为“严格验证通过”。

## 已确认问题

### R1 / 高：CONTROLOBJECT 同名选择把绘制顺序误认为加载顺序

位置：`third_party/mmeffect/src/mmeffect/material_bind.cpp:2184`，`FindModelByName`（2190 起）；`mme_context.cpp:315` 为注册 vector 追加点。

当前通过 `ctx->models` 下标约束 `candidateIndex <= ownerIndex`，返回最后一个匹配；注释把原版 `ModelData+0xec` 描述为 load serial。**本次 IDA 证明注释错误：**

- `MMEffect.dll:0x180059AD6` 写 `*(int*)(model+236) = abs(ExpGetPmdOrder(index))`。
- 附件分支 `0x180059B35` 同样写 `abs(ExpGetAcsOrder(index))`。
- `0x18005BC6E` / `0x18005BDAC` 每次重建先刷新上述值，再以 `+236` 构造名称注册表的有序键。
- `0x180057BC0` 的名称解析比较 owner 与 candidate 的 `+236`，并按该树顺序遍历，不按 `ctx->models` 注册顺序。

因此加载两个同名对象后调整绘制顺序，使绘制顺序不同于加载顺序，效果的 CONTROLOBJECT 会选错对象。原函数还存在“没有 <= owner 命中时取树末端”的回退；现代码返回空也不等价。建议恢复名称→按 renderOrder 排序的注册表及精确 predecessor/末端回退，不应继续使用所谓 load serial。最小验证场景：两个同名 PMX、不同骨骼/形态值、第三个效果 owner，交换绘制次序，记录实际绑定值。

### R2 / 中：重置矩阵不是单位阵，原始偏移被错误翻译为四组颜色

位置：`third_party/mmeffect/src/mmeffect/model_data.cpp:215`（ResetPassPlanScratch），`model_data.h:154` / `:187`。

当前 `color0[0] = color1[0] = color2[0] = color3[0] = 1`。四个 `float[4]` 连续布局意味着写入矩阵线性索引 0、4、8、12，得到第一列全 1，其余为 0；原版 `0x18005BA8D/97/A1/AB` 写的是 +308/+288/+268/+248，相对 +248 为 60、40、20、0，即索引 15、10、5、0，是真正单位阵。

正常后续 `MmeRefreshObjectPlan`（`pass_planner.cpp:892`）会给可枚举宿主对象覆盖正确矩阵，因此不能宣称每帧所有对象都会错；确认的是重置后的中间状态及没有对应宿主 index 时不一致。`model_data.h` 的 color1/+0x10c 等注释与实际 `float[4]` 步长也不相符。建议把四个 color 字段恢复为有名字的 `D3DMATRIX world`，直接单位阵初始化，删除 `reinterpret_cast<D3DMATRIX*>` 跨字段类型重解释；这正是用户要求的源码级还原。

### R3 / 高：MME 附件 DrawSubset 替代路径在属性表超过 128 项时绘制整个网格

位置：`src/render/mme/mme_bridge.cpp:391` 至 `:430`。

该路径先把 faceCount/vertexCount 默认成整个 mesh；随后只在 `tableSize <= 128` 且第二次 GetAttributeTable 成功时读取材质 range。超过 128 或查询失败时沿用整网格范围，每次材质循环都提交整个 mesh，效果/透明度/阴影均可能重复。命中同一 AttribId 的第一段后 `break`，其余同 id range 也未遍历。

这是当前 C++ 控制流即可确认的缺陷；本次未反编译微软 D3DX DrawSubset，后者的全部边缘行为仍需验证。建议使用动态 attribute vector，按目标材质遍历全部 ranges；若网格尚无属性表，应读取 attribute buffer 形成 run，不能把“无优化表”一概视为“只有一个材质”。失败路径必须显式失败，不能退化成整网格多次绘制。验证：129 材质 X 模型、相同材质多个不连续面段、无优化属性表三种输入。

**后续 fix11 复核纠正（保留上文审计历史）：**进一步反编译本机 Microsoft `d3dx9_43.dll` 的 `GXTri3Mesh<ushort>::DrawSubset`（0x18006AB3C）及 32 位索引版本（0x18006C854）证明：优化属性表路径只取一个表项，优先 `table[AttribId]`，否则取第一匹配；不能绘制所有同 id 表项。无优化表时才按每面属性扫描全部连续 run。原审计的“遍历所有 table ranges”建议撤回。128 上限及无表整网格回退的问题成立，fix11 已据真实分支修正。

### R4 / 已承认的行为偏离：D3DX/标准效果缺失时继续运行

位置：`src/render/d3d_init.cpp:153`、`src/render/mme/mme_bridge.cpp:59`、`:221`。

当前 D3DX 可选加载，缺失标准 effect 时桥接直通且每设备周期最多弹一次。源码自身明确说明原路径会持续初始化失败，不调用真实 BeginScene；当前则继续调用。这不是严格一致。此条基于当前实现及其明确偏离声明，未在本次重新分析原 MMHack 缺 effect 分支。可以选择产品友好行为，但应建立批准的差异清单，不能叫一比一。

## 内置集成与源码形式

- `CMakeLists.txt` 使用 `add_library(MMEffect SHARED)`，并预造宿主导入库形成 exe↔DLL 导入关系。这是**编译期直接连接的独立 DLL**，并非单 exe 内的静态 MME。已经去除了注入，符合“不做 DLL hack”的核心要求；若“直接做进 MMD”要求单体交付，下一步应改成静态/对象库及明确宿主接口。
- `src/render/fx_slots.hpp:67` 仍从 `void***` 手工取 vtable，注释称槽号是唯一跨架构正确形式并不成立：正确的 `ID3DXEffect` 接口声明会由编译器处理两种架构。`third_party/mmeffect/include/d3dx9.h` 已提供类型化接口，宿主应统一采用相同定义，而非保留两套 ABI 调用体系。
- `mme_bridge.cpp:349` 的 `MeshSlot` 同理；改为 `ID3DXMesh` / `ID3DXBaseMesh` 类型化访问。
- `mmhack_getters.cpp:38` 把 accessory_id 当宿主 `AccessoryRecord*`，虽已用成员名而非裸偏移，却仍让效果引擎依赖宿主内存布局。适合通过宿主接口 `GetAccessoryAttachment(ObjectId)` 供给 parentModel/parentBone，不需要恢复任何 hack。
- `mme_host.cpp` 的设备窥探和 Exp* 查询可逐渐变成 `FrameContext` / `DrawContext` 值对象。必须先保存调用先后和失败语义，再做封装；不要为了“现代化”改变浮点顺序或绘制顺序。
- 原地址留在独立证据映射/文档中有价值；注释中的 VA 不是运行时硬编码。正常借用 COM 指针也不是错误。真正应清理的是无类型内存解释、对象布局耦合、vtable 槽访问，以及误把编译后的存储形态当原作者业务结构（R2）。
- `mme_ui.cpp` 仍使用线程级 Win32 消息 hook / WndProc subclass。这些是 UI 事件接线，不等于 d3d9 代理/IAT 注入；内置后也可改为宿主显式派发，但不能仅凭“hook”单词判为 DLL hack。

## 未验证，不得写成已通过

- SAS 解释器有真实资源解析、ScriptExternal 暂停/恢复、运行时循环、RT/DS 重定向代码，未发现可据“Remaining stubs”旧注释认定的整块空实现；`mme_globals.h:137` 的旧阶段说明不能代替代码检查。
- 同名控制对象、离屏多目标、render target 格式/尺寸调整、Cube/Volume 纹理、TextureValue、GIF/APNG、shader 编译失败、D3D device lost/reset、录制前 post-effect flush 均需原版执行对照。
- `material_bind.cpp` 的全部 semantic 类型转换、`sas_interpreter.cpp` 约三千行扫描/资源构建、`sas_exec.cpp` 所有错误/嵌套循环分支，本次没有穷尽证明。
- 未做性能等价判定，也未声称可还原原作者真实源码；目标应为业务结构合理、外部行为可证明的重建。

## 逐文件覆盖清单

下表由当前文件枚举生成。`重点` 表示本次人工检查了相关实现/调用链；`扫描` 表示仅结构/接入/风险扫描，**不代表逐行验收**；资源只登记。

| File | Lines | Coverage |
|---|---:|---|
| `src/exports/effect_api.cpp` | 600 | Focused review |
| `src/render/accessory.cpp` | 1030 | Structural scan |
| `src/render/bg_overlay.cpp` | 375 | Structural scan |
| `src/render/capture_downsample.cpp` | 151 | Structural scan |
| `src/render/d3d_init.cpp` | 473 | Focused review |
| `src/render/debug_geometry.cpp` | 634 | Structural scan |
| `src/render/device_reset.cpp` | 119 | Focused review |
| `src/render/draw_glyph.cpp` | 55 | Structural scan |
| `src/render/fill_panel.cpp` | 78 | Structural scan |
| `src/render/frame_scene.cpp` | 474 | Structural scan |
| `src/render/fx_slots.hpp` | 131 | Focused review |
| `src/render/mme/mme_bridge.cpp` | 459 | Focused review |
| `src/render/model_renderers.cpp` | 1817 | Structural scan |
| `src/render/overlay_producers.cpp` | 254 | Structural scan |
| `src/render/render_states.cpp` | 143 | Structural scan |
| `src/render/scene_font.cpp` | 181 | Structural scan |
| `src/render/sprite_overlay.cpp` | 767 | Structural scan |
| `src/render/stereo_nvapi.cpp` | 164 | Structural scan |
| `src/render/toon_textures.cpp` | 194 | Structural scan |
| `src/render/vb_dump.hpp` | 103 | Structural scan |
| `third_party/mmeffect/include/d3dx9.h` | 447 | Structural scan |
| `third_party/mmeffect/include/MMDExport.h` | 103 | Structural scan |
| `third_party/mmeffect/include/mme_abi.h` | 75 | Focused review |
| `third_party/mmeffect/include/mme_host_api.h` | 117 | Focused review |
| `third_party/mmeffect/include/mmhack_api.h` | 65 | Structural scan |
| `third_party/mmeffect/MMEffect.def` | 23 | Structural scan |
| `third_party/mmeffect/README.md` | 67 | Focused review |
| `third_party/mmeffect/res/MMEffect.rc` | 127 | Structural scan |
| `third_party/mmeffect/res/res_mme.ico` | - | Resource |
| `third_party/mmeffect/res/res_mme.manifest` | 9 | Structural scan |
| `third_party/mmeffect/src/d3dx9_dyn.cpp` | 522 | Structural scan |
| `third_party/mmeffect/src/mme_host.cpp` | 573 | Focused review |
| `third_party/mmeffect/src/mmeffect/anime_texture.cpp` | 1556 | Structural scan |
| `third_party/mmeffect/src/mmeffect/anime_texture.h` | 144 | Focused review |
| `third_party/mmeffect/src/mmeffect/callbacks.cpp` | 969 | Structural scan |
| `third_party/mmeffect/src/mmeffect/effect_engine.cpp` | 497 | Structural scan |
| `third_party/mmeffect/src/mmeffect/effect_engine.h` | 136 | Structural scan |
| `third_party/mmeffect/src/mmeffect/emm_manager.cpp` | 1815 | Structural scan |
| `third_party/mmeffect/src/mmeffect/emm_manager.h` | 488 | Structural scan |
| `third_party/mmeffect/src/mmeffect/ini_file.cpp` | 238 | Structural scan |
| `third_party/mmeffect/src/mmeffect/ini_file.h` | 71 | Structural scan |
| `third_party/mmeffect/src/mmeffect/material_bind.cpp` | 2587 | Focused review |
| `third_party/mmeffect/src/mmeffect/material_bind.h` | 320 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_context.cpp` | 503 | Focused review |
| `third_party/mmeffect/src/mmeffect/mme_context.h` | 463 | Focused review |
| `third_party/mmeffect/src/mmeffect/mme_dlg.cpp` | 2207 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_dlg.h` | 25 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_globals.cpp` | 195 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_globals.h` | 150 | Focused review |
| `third_party/mmeffect/src/mmeffect/mme_log.cpp` | 215 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_log.h` | 77 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_ui.cpp` | 766 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_ui.h` | 54 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_util.cpp` | 73 | Structural scan |
| `third_party/mmeffect/src/mmeffect/mme_util.h` | 26 | Structural scan |
| `third_party/mmeffect/src/mmeffect/model_data.cpp` | 234 | Focused review |
| `third_party/mmeffect/src/mmeffect/model_data.h` | 238 | Focused review |
| `third_party/mmeffect/src/mmeffect/pass_planner.cpp` | 2755 | Focused review |
| `third_party/mmeffect/src/mmeffect/pass_planner.h` | 129 | Structural scan |
| `third_party/mmeffect/src/mmeffect/render_snapshot.cpp` | 233 | Focused review |
| `third_party/mmeffect/src/mmeffect/render_snapshot.h` | 134 | Structural scan |
| `third_party/mmeffect/src/mmeffect/sas_exec.cpp` | 1593 | Structural scan |
| `third_party/mmeffect/src/mmeffect/sas_exec.h` | 506 | Focused review |
| `third_party/mmeffect/src/mmeffect/sas_interpreter.cpp` | 2912 | Structural scan |
| `third_party/mmeffect/src/mmeffect/sas_interpreter.h` | 215 | Structural scan |
| `third_party/mmeffect/src/mmhack/mmhack_getters.cpp` | 212 | Focused review |
| `third_party/mmeffect/src/mmhack/mmhack_lightmatrix.cpp` | 124 | Structural scan |
| `third_party/mmeffect/src/mmhack/mmhack_material.cpp` | 28 | Structural scan |
| `third_party/mmeffect/src/mmhack/mmhack_material_table.cpp` | 514 | Structural scan |
| `third_party/mmeffect/src/mmhack/mmhack_registry.cpp` | 190 | Focused review |
| `third_party/mmeffect/src/mmhack/mmhack_state.h` | 150 | Focused review |
