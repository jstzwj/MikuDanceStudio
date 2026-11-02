# 内部接口类型整理：原版 x64 对照审计

## 结论与范围

使用 IDA-MCP 核对指定 MMD v9.32 x64 的反编译、关键汇编与源码。初次审计未发现类型整理引入的新偏差，但确认了已有的构造遍历顺序、防护、清理与错误处理差异。**2026-11-03 已按用户要求对齐本文列出的差异**，具体修改与验证见文末。

审计覆盖接口整理涉及的关键路径，不代表全程序等价证明，也不代替真实模型、MME 效果和 AVI 导出的完整验证。下文保留审计时的差异说明；修复后的实现不再采用所述逆序遍历和额外分支。

## 原版样本与定位方式

- 文件：`C:\Users\jstzw\Documents\github\OpenMMD\MikuMikuDanceE_v932x64\MikuMikuDance.exe`
- 大小：1,722,368 字节。
- SHA-256：`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`。
- 当前 IDA 数据库映像基址：`0x7FF7CB420000`。下文使用 RVA，避免地址受重定位影响。
- 对照基线：当前工作区及 `HEAD fa8914a4dbb69d5cdbb1666105925c817325ae23`；“已有差异”均另行检查 HEAD 实现。

| 原版函数／入口 | RVA | 当前对应实现 |
|---|---|---|
| 数组构造辅助函数 | `0x1220` | `src/window/accessory_paste.cpp`：`ConstructArrayElements` |
| PMD 加载 | `0xB2670` | `src/model/pmd_load.cpp`：`ModelLoadPMD` |
| PMX 加载 | `0xA9AC0` | `src/model/pmx_load.cpp`：`LoadPMX` |
| 模型析构 | `0xA8D50` | `src/model/model_dispose.cpp`：`ModelDispose` |
| 截图保存 | `0x96310` | `src/app/frame_driver.cpp`：截图分支 |
| 附件材质导出／查询辅助函数 | `0xDBB60`／`0xA7FC0` | `src/exports/effect_api.cpp`：`ExpGetAcsMaterial` |
| 附件加载／操作轴网格加载 | `0xDC560`／`0xDDAC0` | `src/render/accessory.cpp` |
| D3D 初始化 | `0x7A30` | `src/render/d3d_init.cpp` |
| NVAPI 立体句柄创建包装 | `0xE3B40` | `src/render/stereo_nvapi.cpp` |

## 初次审计确认的差异（2026-11-03 已对齐）

### 1. 数组构造回调访问方向相反

审计时，`src/window/accessory_paste.cpp` 的 `ConstructArrayElements` 从 `count - 1` 到 `0` 调用 `constructor(block + i * elementSize)`，即按地址递减访问元素。其注释称这是原版的顺序，注释也不准确。

原版 `RVA 0x1220` 虽然递减计数器，但地址独立递增：

```asm
; RVA 0x123D..0x124E，省略与循环无关的保存现场
dec  edi             ; 剩余计数
mov  rbx, rcx        ; 当前元素 = block
js   end
loop:
mov  rcx, rbx
call rsi             ; constructor(当前元素)
add  rbx, rbp        ; 当前元素 += elementSize
dec  edi
jns  loop
```

当 `count = 3` 时，原版回调顺序是 `0, 1, 2`，修复前复刻版是 `2, 1, 0`。不能用“计数器递减”推断“元素地址递减”。修复后保留原版的有符号计数退出条件，按元素地址递增调用；计数为零时不会调用回调。

十个生产调用点都传入 `IdentityCtor`，且忽略辅助函数返回值；该回调没有状态修改。因此这些调用没有观察到结果差异，但依赖顺序的回调会产生不同结果。原版调用点 `RVA 0x43BDA` 同样传入只返回输入地址的构造函数 `RVA 0x542E0`。HEAD 已存在逆序实现；最初的类型整理仅恢复回调指针类型，后续本次对齐才修正遍历顺序。

### 2. 原已存在的防护、清理和失败路径扩展

以下为审计时的已有差异；均已在本次对齐中移除或恢复原版判定：

- **零骨骼 PMD 分配。** 原版 `RVA 0xB31F1..0xB32AE` 按实际骨骼数分配表、四字节游标和单字节状态数组；旧实现至少分配一个元素。现在恢复按实际数量分配，保留原版选择数组首项写入。
- **中断的姿势追踪清理。** 旧 `ModelDispose` 额外释放 `poseTraceBuffer` 并重置追踪计数、录制状态；原版没有对应步骤。现在析构不再处理这三个字段。
- **附件父模型检查。** 原版 `RVA 0xA19F3` 只比较 `parentModel == -1`；旧 `AccessoryPlacement` 另有索引范围和空模型检查。现在只保留 `-1` 哨兵判断。
- **D3DX 效果错误输出。** 原版资源效果调用 `RVA 0x82ED`／`0x8321` 传入空错误输出；旧实现收集、记录和释放错误缓冲。现在传空指针；SM2 的格式查询和 `SetInt` 保持位于返回值检查前，不再额外检查效果指针。
- **HRESULT 判定。** PMD／PMX 的 VB／IB 创建、索引锁定，以及截图创建／两次 blit，恢复为精确零值判断；顶点锁定仍使用原版的符号判断。D3D 初始化的格式／深度匹配探测、HDR 纹理、阴影深度和效果创建也恢复精确零值判断；多重采样探测和设备创建仍保留符号判断。`GetDeviceCaps` 返回值按原版忽略。

原版的部分异常输入路径本身缺少边界保护，析构也保留姿势追踪存储；本次按兼容目标保留这些行为，没有宣称对畸形模型或无效父模型索引增加保护。

## 已核对一致的关键内容

| 项目 | 原版证据与当前实现的对应关系 |
|---|---|
| PMD 主／描边顶点 | 原版按 32／16 字节创建和推进 GPU 记录，描边颜色为 `0xFF000000`；当前 `SkinnedVertexBase`／`EdgeVertex` 布局及最终写入字节一致。原始 PMD 顶点读入 38 字节、CPU 记录步长 40 字节，不能与 GPU 步长混淆。 |
| PMD 索引 | 原版创建长度 `2 * indexCount`、格式 `101` 的 GPU 索引缓冲，按 16 位写入；CPU 展开的索引占四字节。当前类型分别表达这两种存储。 |
| PMX 附加 UV | 原版附加 UV 数为 0..4 时，主顶点步长分别为 32、48、64、80、96；描边保持 16。对应 FVF、usage、pool 与当前创建参数一致。 |
| 游标与状态数组 | 原版骨骼／表情关键帧游标按每项四字节分配、清零，活动／选择／物理状态按单字节；当前类型与 `sizeof` 保持这些宽度。零骨骼防护差异见上文。 |
| `englishUI` | 原版 PMD 入口将语言参数写入模型 `+0x3560`，后续用于选择语言分支，支持将旧名 `physicsFlags` 更正为 `englishUI`。 |
| COM 释放 | 原版 `RVA 0xA955A..0xA958F` 依次释放描边 VB、主 VB、IB，并各自清空；当前 `ReleaseCom` 顺序一致。不能由此推出整个析构函数完全相同。 |
| 附件材质 | 原版导出使用临时返回存储，查询辅助函数清零／复制 `0x44` 字节，材质索引比较为有符号严格小于；当前 `D3DMATERIAL9` 为 68 字节，保留有符号比较和常规导出结果。 |
| 截图未知扩展名 | 原版 `RVA 0x9656A` 用 `mov edx, dword ptr [surface-slot]` 读取表面指针低 32 位，已知 HDR 时再替换为 7。当前显式 `intptr_t → int → D3DXIMAGE_FILEFORMAT` 与固定 `int` 底层类型保留这个原始值。该分支是原版遗留行为，而不是合理格式选择。 |
| 网格及材质类型 | 原版操作轴网格加载选项为 `0x220`，读取 x64 的 `D3DXMATERIAL` 并复制 68 字节材质。附件材质缩放汇编为 `mulss`，支持当前单精度计算，不能从反编译的 `0.1` 文本推断为双精度。 |
| NVAPI 设备参数 | 原版包装查询 ID 为 `0xAC7E37F4`，设备与句柄输出槽作为两个指针参数传入；调用方在返回值非零时禁用立体功能。当前设备指针具体化未改变该调用约定。 |

以上“一致”指表中注明的字段、参数、条件或有效调用结果；不表示逐条机器指令、所有异常输入和整段函数均严格等价。

## 初次审计时的运行对照

使用当前 x64 Release 测试程序，直接加载上述同一原版 EXE：

- `native_host_query_test --original <EXE>`：**7536 次比较通过**，包含附件材质等宿主查询接口。
- `scene_render_gpu_test --original <EXE> --numeric-only 1`：生成 PMD 的加载数据与姿势数据比较通过；两个视角 × 三种描边强度，主顶点和描边缓冲均逐字节一致。
- 同一程序附加 `--features 1`：生成 PMX（含一个附加 UV、七个骨骼）的对应六组缓冲比较通过。

两组场景均只有三个顶点，属于有明确覆盖目标的生成夹具，不代表真实复杂模型的完整覆盖。本次原生 D3D9 设备创建仍报告 `HAL=0x8876086A`、`NULLREF=0x8876086C`，测试使用 CPU 缓冲设备；**没有执行纹理采样、像素渲染或 AVI 输出验证**。

本机证据位于 `out/typed-interface-ida-x64/`：PMD／PMX 完整汇编、相关函数反编译、`array_constructor.asm`、`native-host-query.log`、`pmd-numeric.log`、`pmx-numeric.log` 以及两组数值快照。PMD／PMX 的长函数不采用被工具截断的反编译文本，而按指令分页读取汇编。

## 2026-11-03：对齐实现与回归

除上述修正外，PMX 创建 VB／IB 时直接写入模型字段，与原版传入字段地址一致；即使 API 返回非零但写入了资源指针，后续生产析构仍能访问该指针。资源效果创建非零时，按原版顺序释放阴影表面、HDR 纹理、阴影深度，并清除后处理标志；不再以效果指针是否为空替代 HRESULT。

`model_ui_edit` 增加可观察调用顺序的构造回调，覆盖零次、三次及原版有符号计数退出条件；`model_lifetime_test` 检查析构保留姿势追踪指针和状态。`native_host_query_test` 在 x64 上直接调用原版 `RVA 0x1220`，比较计数 `0`、`1`、`3`、`0xFFFFFFFF` 的回调顺序，四组均通过；原有宿主接口 **7536 次比较通过**。

`pmx_buffer_failure` 使用 CPU 缓冲设备验证原有创建／锁定负值失败、失败加载槽位清理，新增五个 `S_FALSE` 案例（主 VB、描边 VB、IB、16／32 位索引锁定）。正值案例同时保留已创建资源，检查生产析构的资源清理；测试不再依赖 GPU。

两架构完整 Release 构建通过。最终选定 CPU 回归为 **x64 80/80（87.59 秒）、x86 76/76（62.06 秒）**，无失败或跳过；每架构另有 23 个依赖 GPU 的用例明确排除，不能将这些结果称为全量 CTest 通过。集合包含 PMX 原版数值对照；另跑 x64 PMD 原版数值对照，六组主／描边缓冲均逐字节相等。增加 API 输出指针归属断言后，又单独构建并复跑两架构 `pmx_buffer_failure`，均通过。

首轮 CPU 回归各有一个 `kinect_tracking` 旧断言要求析构释放追踪存储，与恢复后的原版行为冲突；已改为检查保留指针、计数及录制状态，由测试夹具在生产析构之外释放分配。最终上述集合包含修正后的用例。修改的 C++ 文件通过格式检查，`git diff --check` 通过。

本次未执行截图文件输出、D3D 初始化效果编译或最终像素／AVI 的运行对照；对应分支依据原版汇编核验。当前 GPU 设备仍不可用，后续图形验收应继续使用原版与同场景对照。本机日志为 `out/typed-interface-ida-x64/alignment-*.log`，汇总为 `alignment-summary.json`，JUnit 为各构建目录的 `alignment-cpu-final-{x64,x86}.xml`。
