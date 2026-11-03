# sdPBR480、AVI 与重复加载 PMM 的追加核查

## 最新结论：已确认两处原版差异并修正代码

本轮确认对象缓存遗漏了**几何资源生命周期检查**。PMM 在两个渲染帧之间完成卸载和重载时，新模型 / 附件可能复用旧对象地址；旧实现只检查该地址是否仍在场景中，因而漏发 `OnDeleteModel` / `OnCreateModel`，保留旧材质、效果和离屏分配。这是复刻版的明确差异。

已在工作树中修复：缓存持有模型顶点缓冲 / 附件网格的 COM 引用；每帧同时核对对象 ID 和当前几何资源，资源更换时先删除旧 MME 对象，再重新注册。不重建整个 MME，不清空仍存活对象的效果。

同一 Sour Miku + sdPBR480 工程的硬件顶点处理复测：当前引擎连续重载 10 次，原版 MMEffect.dll 接入后重载 3 次，每次播放预热后的图像都分别与首次稳定预览逐像素一致；两组均为 0 次错误提示。详见文末“对象生命周期修复”。

随后将两套引擎的每个绘制调用对照到 Sour 的头发材质，发现第二处差异：`GetSphereMapMode` 原版返回 32 位枚举，我们误声明为 8 位；原版 MMEffect.dll 按 32 位返回类型读取，导致球面贴图模式读取错误。已修正返回类型，并通过原版 DLL 的真实导入入口验证 0/1/2/3 四种模式。

**用户恢复桌面后，已完成最终 GPU 复测：两套 MME 的稳定预览、全部 31 个 AVI 录制帧和各 3 次重载后的稳定图像，共 37 对 RGB 样本逐像素相同；头发材质差异消失。x64、x86 定向回归各 4/4 通过、无跳过。** 本结论针对该 Sour + sdPBR480 场景及复刻宿主中的两套 MME 引擎，不扩大为所有效果包或完整原版 MMD 的兼容性保证。

以下保留修复前各轮记录；其中“尚未确认原版差异”“没有修改生产代码”等表述描述的是当时进度，最新判断以本节及文末为准。

核查版本：`11a76cb`（2026-11-01，已推送到 `origin/main`）。反馈者所用版本未知。以下结果不能追溯证明其当时使用的版本是否有相同问题。

用户指定 Sour Miku 后，已补充与正式程序首选设备模式一致的硬件顶点处理测试，见文末“Sour Miku 硬件顶点处理复测”。**换模型后仍复现了重载导致的持续颜色变化；未复现整幅全黑、AVI 额外丢失效果或正常结束后自动播放。** 早期软件顶点处理中的偏黑不能直接用作正式程序表现的证据。

## 初次测试结论（软件顶点处理）

| 反馈 | 本轮结果 | 判定边界 |
|---|---|---|
| sdPBR480 的某些效果在 AVI 中禁用 | **本轮未复现导出额外丢失效果** | 实际生成了三段 512×512、31 帧、30 fps 的未压缩 AVI。解码后的每一帧 RGB 都与各自送入导出流程的渲染图相同。但预览与播放 / 录制状态之间有明显外观变化，不能据此宣称完整兼容。 |
| AVI 完成后自动播放 | **本轮未复现** | 正常收尾后 `playbackActive=0`、`frameStepPlayback=0`、`recordPlaybackStartPending=0`。没有覆盖完整 GUI 消息循环、所有输出设置、取消和失败路径。 |
| 多次加载 PMM 后偶发全黑 | **复现了相关的重载图像异常，尚未确认是原版差异** | 当前引擎连续加载同一 PMM 后出现不同输出和模型大面积变黑；接入原版 MMEffect.dll 的对照也出现异常，部分重载为全黑。两组仍共用复刻版的场景加载器和桥接宿主，不能写成“原版 MMD 也有此 bug”。 |

**没有依据给这三项打上“已修复”或“全部不存在”的结论。** 本轮没有基于猜测修改渲染器或重载逻辑。

## 场景与执行方式

使用用户提供的 `sdPBR480.zip`。压缩包包含 2,082 项，没有 PMM / EMM 示例；解压后没有运行包内的 EXE、DLL 或批处理工具。

按包内 `html/basic.html` 搭建并在编辑器保存场景：

- 普通模型：本机 `sdef_miku.pmx`。
- 控制模型：`sdPBR.pmx`，自动关联同名 `sdPBR.fx`。
- `sdPBRGBuffer.x` 排在模型之前；附件与模型的分隔位置为 1。
- 普通模型分配 `material/body/sdPBR_skinEX.fx`。
- `skybox/skybox.x` 和 `posteffect/ToneMapper/sdToneMapper.x` 排在模型之后。

当前 v480 实际的 ToneMapper 路径包含 `ToneMapper` 子目录；教程中旧的 `posteffect/sdToneMapper.x` 路径不存在。材质应用到整模型的测试分配只用于核查管线，并非逐材质美术配置。

保存的 PMM / EMM、解压资源、临时 GPU 集成程序源码、构建脚本、逐帧 PNG、AVI 及日志均在本地忽略目录 `out/sdpbr480-audit/`；未将第三方模型和效果包提交到仓库。`baseline.pmm` / `baseline.emm` 是保存后的场景副本。

GPU 集成程序使用项目已有的真实 D3D9 测试夹具及生产代码，不替换着色器执行或帧读回。设备核验为 HAL，MME 日志报告 NVIDIA TITAN Xp、vs_3_0 / ps_3_0。它不是完整应用 GUI 测试。

## AVI 的具体证据

第一组通过生产 MME 场景回调、阴影与模型渲染、`BuildRecordingGraph`、`RecordingReadbackPass`、`FinishAviRecord`，生成 `port-avi/capture.avi`。

第二组额外使用生产 `RenderFrameScene`，设置录制窗口状态，并走 `multisampleAvailable=0` 的 `DownsampleCaptureSurface` / 拷贝前 MME 回调分支，生成 `production-avi/capture.avi`。

前两组为固定时间的管线诊断，未完整模拟编辑模式探测。第三组在第二组基础上补上 `RecWindow` 类窗口、禁用当前帧编辑框，并逐帧推进 `PlaybackCursorSeconds`，使 MME 按播放 / 录制状态获取时间，生成 `production-timed-avi/capture.avi`。

三段输出均由 ffprobe 确认：`rawvideo`、512×512、31 帧，时长约 1.033332 秒。逐帧解码与对应的 `record_N.png` 比较，每组 31/31 帧每个 RGB 通道的最大误差均为 0。数据见各输出目录的 `avi-pixels.json`；第一组另有目录外的早期 `avi-pixels.json`。

第一组预览、31 个录制帧和收尾后的图像哈希均为 `D0B910F559EDF934`。第二组预览与录制状态之间存在像素差异，因此“编码结果与输入一致”不能被扩大为“整个预览与录制完全一致”；应继续隔离录制表面切换、地面 / 叠加层及效果历史。两组在测试前的预览中均已出现模型大面积偏黑，不能作为正确 sdPBR 外观的基准。

第三组从偏黑的预览进入播放 / 录制状态后，模型恢复明显着色，录制第 3 至 30 帧稳定为 `3E2349D27556D135`。收尾后的预览仍保留着色，哈希为 `3C46C9FF6CF5B755`。这提示时间输入或效果历史参与了外观变化；还没有把变化定位到某一个 sdPBR 子效果。不能将前两组的固定时间诊断当成真实播放时的最终外观。

额外的 `port-play.log` / `native-play.log` 不创建 AVI，直接从编辑状态切入播放时间输入，连续采样 5 帧后停止。当前引擎与原版 MMEffect.dll 的这 5 帧及停止后图像**逐像素完全一致**；两边均从初始偏黑恢复着色，第 3 / 4 帧及停止后的哈希均为 `A6E81B6E4BAB6090`。该现象无需 AVI 即可出现，在这组公开宿主桥接条件下没有发现两套 MME 引擎的差异。

三组正常结束时均记录：

```text
finished active=0 step=0 pending=0
```

## 重载与原版 MME 对照

`port-reload.log`：同一进程完成初始加载及 10 次完整生产 `LoadSceneFile` 重载，每次保存前两帧。相同 PMM、相同材质分配和相同帧号的输出不稳定。例如第二次重载的两帧哈希分别为 `0EC320BF28FF4F90`、`D3929D7E554DF0B2`；图像中模型从大面积黑色变为明显着色。没有弹出错误对话框。

`native-reload.log`：将 MME 引擎换为指定原版 `MMEffect.dll`，保留同一夹具、加载器和公开宿主查询桥接。初始稳定预览与当前引擎具有相同哈希 `D0B910F559EDF934`；完成 5 次重载后，多次输出为全黑（`lit=0, unique=1`），并出现 MikuMikuEffect 错误对话框。因此这一组存在运行错误，不能作为通过的原版基准。

原版 DLL SHA-256：`74b3f5882a3bad28131cbc57994e2f960938e6ca850fc7139018ce4822bd87dc`。

`port-recreated.log`：诊断性地在每次加载前销毁并重建整个 MME 宿主后，5 次重载的第二帧均稳定为 `AB6222D9F66254C2`，但仍偏黑。这说明保留状态与重载不稳定有关，尚不足以区分对象 ID 复用、旧材质 / 离屏目标分配、效果历史或场景状态的问题。**重启整个 MME 不是本轮提交的修复方案。**

补充骨骼变换阶段的 `pose-preview.log` 仍复现 SDEF 场景相同的初始偏黑与重载变化。

另一套本机模型 `sour miku/miku/miku.pmx` 的 `sour-preview.log` 完成初始加载及 3 次重载。稳定预览及每次重载前两帧均为 `8C163DF559A87C40`，没有新增图像变化或错误对话框，但预览仍偏黑。该组仅替换 PMM 固定宽度模型路径，并使用生产加载器进行骨骼等数据协调，不是反馈者的原始工程。

接入原版 MMEffect.dll 不等于运行完整原版 MMD + MMHack。完整原版 GUI 在本轮未成功启动并完成场景 A/B，因此原版宿主的差异仍未定案。

## 其他检查

- `mme_ray_smoke_test.exe sdPBR.fx` 在真实设备上通过，执行 13 次 Buffer 绘制、无脚本执行失败。该测试不含完整场景，不能单独证明 sdPBR 兼容。
- 本轮 `scene_reset`、`mme_buffer_pass_gpu` 两项 CTest 均通过。此前审计中出现的 GPU 不可用 / 测试异常仍保留历史记录，不据本轮通过倒推原因。
- 本地构建的 `MMDxShow.dll` 已放入 EXE 的 `Data/` 目录以进行实际 AVI 建图；没有改动系统 COM 注册。

后续定位应从能稳定恢复外观的播放时间输入继续，分别隔离完整原版宿主、效果分配、离屏目标历史和录制表面切换；在找到可重复且与原版不同的因果链后再做针对性修复。

## Sour Miku 硬件顶点处理复测

本节对应用户要求改用 `C:\Users\jstzw\Downloads\sour miku\miku\miku.pmx` 再测。模型内部名称为 `Sour_Miku_White`，139,692 个顶点、131,141 个三角形、25 个材质；16 条引用的贴图路径全部存在。源模型和贴图没有修改。

使用 D3D9 HAL、`D3DCREATE_HARDWARE_VERTEXPROCESSING`（`0x40`），与生产 `d3d_init.cpp` 的首选创建模式相同。前一轮夹具默认使用软件顶点处理，不能把两种模式的结果混成同一设备条件。硬件模式下 Sour Miku 在预览中即正常着色，8 帧预热后稳定。

为与旧模型保持可比，仍将 `sdPBR_skinEX.fx` 分配给整个普通模型，保留 sdPBR 控制模型、GBuffer、Skybox、ToneMapper 及原有绘制顺序。本轮是兼容性场景，不是按皮肤 / 头发 / 衣物分别调参的最终美术工程。

所有本节文件在 `out/sdpbr480-audit/`：

| 测试 | 输出 | 结果 |
|---|---|---|
| Sour 首次 AVI 导出 | `sour-avi/`、`sour-avi.log` | 31 帧实际输出；正常收尾；无错误对话框。 |
| 预热、AVI、连续 10 次完整 PMM 重载；每次再播放 5 帧并停止 | `sour-full/`、`sour-full.log` | AVI 成功；无整幅全黑，但材质颜色 / 明暗在重载后变化，部分变化播放后仍持续；无错误对话框。 |
| 同场景接入原版 MMEffect.dll，AVI 后重载 3 次 | `sour-native-full/`、`sour-native-full.log` | AVI 成功；首次稳定图像的头发颜色等与当前引擎不同；重载出现 6 次 MME 错误提示，测试退出码 2，不能作为通过的原版基准。 |
| 通过生产保存函数重新保存 Sour 工程，另起进程重载 3 次并播放 | `SourMiku-sdPBR480.pmm` / `.emm`、`sour-resaved/`、`sour-resaved.log` | 仍出现持续颜色变化；没有整幅全黑或错误对话框，排除了仅由旧 SDEF 场景模型路径替换 / 数据协调造成异常的解释。 |

三个 Sour AVI 均为 512×512、31 帧、约 30 fps 的未压缩视频，ffprobe 核验时长约 1.033332 秒。各自解码后的 31/31 帧 RGB 与对应 `record_N.png` 完全一致，最大通道误差为 0。各输出目录包含 `avi-pixels.json`。

两套引擎在正常 AVI 收尾后均记录 `active=0 step=0 pending=0`。本轮依旧通过 GPU 集成程序调用生产代码，没有覆盖完整应用的 GUI 消息循环及失败 / 取消路径。

当前引擎在 `sour-full` 的稳定预览为 `8A1908940BD20E48`，录制帧为 `CFDCB7ACA7E94938`，收尾后预览回到前者。连续重载时，第 9 次重载并播放后的图像为 `FA7E6DF02877D651`，出现明显偏亮 / 颜色变化；第 10 次为 `128436B5D1D9C4E1`，并未恢复初始外观。重新保存的工程在第 3 次重载播放后为 `E8984C88FE2B1583`，同样不同于初始稳定图像。

### 预览与录制之间的 1 像素位移

在本轮选择的备用录制表面 / CPU 下采样路径中，稳定预览与录制图看似相同，但 RGB 比较不完全相等。进一步对齐后确认：对于已预热的 `sour-full` 和 `sour-native-full`，排除第一行及边界，**预览第 x+1 列与录制第 x 列逐像素完全相等**。即多数行左移 1 像素，而非某项效果消失。

当前 `capture_downsample.cpp` 的水平采样相位跨行保留，因此该路径会产生上述位移。本轮没有改动下采样行为。

### 可查看的产物与判断边界

- `sour-comparison.png`：预热预览、AVI 帧、重载异常、原版 MME 接入图的四格对照。
- `sour-full/capture.avi`：当前引擎的 Sour Miku 实际输出。
- `SourMiku-sdPBR480.pmm` / `.emm`：生产保存函数生成的工程；仍引用本机模型目录和本地解压后的 sdPBR 资源。
- `sour-results.json`：逐帧验证、位移核验和重载采样统计。

可以确认 Sour 模型已在硬件顶点处理下完成实际 AVI 和重复 PMM 加载测试，且重载后的效果状态不稳定仍然存在。**尚不能宣称已复现反馈中的整幅黑屏，也不能凭接入原版 DLL 的夹具把异常认定为原版 MMD 的行为。** 完整原版宿主对照、原版 DLL 重载错误及两套引擎的初始材质外观差异仍需隔离，原版差异未定案。本轮只修改临时测试程序和审计文档，没有据此修改生产渲染代码。

## 对象生命周期修复

旧代码从未给 `texRef` 赋值，且仅检查对象 ID 是否仍在场景中。ID 是宿主对象地址；PMM 重载可能在两个渲染帧之间复用地址。因此 MME 漏掉对象替换，保留旧材质、效果和离屏分配。

修复位于 `third_party/mmeffect/src/mme_host.cpp`、`third_party/mmeffect/src/mmhack/mmhack_state.h` 和 `third_party/mmeffect/include/mmeffect/host_object_cache.h`：

- 将 `texRef` 更名为 `geometryRef`，注册时持有模型顶点缓冲 / 附件网格的 COM 引用，删除和设备清理时释放。
- 每帧建立“宿主 ID → 当前几何资源”快照。对象消失或资源更换时先删除旧 MME 对象，再重建材料表、重新注册。
- 比较资源身份而非真实 D3D 资源的引用计数，避免设备流绑定或状态块持有的额外引用干扰判断。缓存的拥有引用保证旧资源地址在判别之前不能被重新分配。

这是逐对象生命周期修复，没有重建整个 MME。

### 修复前后的控制实验

本节使用真实 HAL、硬件顶点处理、生产 PMM 加载器，以及生产保存函数生成的 `SourMiku-sdPBR480.pmm/.emm`。

| 实验 | 结果 |
|---|---|
| 从 `11a76cb` 提取旧宿主，仅适配缓存字段名称和辅助函数签名，运行新增同地址附件替换回归 | 退出码 1。替换后仍输出旧效果图，失败于 `same-address accessory replacement retained its old effect assignment`。日志 `cache-before-regression.log`。 |
| 上述旧宿主接入原版 MMEffect.dll，加载同一重新保存的 Sour 工程并重载 | 首次重载提示 `Failed to load effect mapping for the following objects: skybox.x`，随后进程以 `0xC0000005` 退出。日志 `sour-native-before-control.log`。这是复刻宿主中的原版 DLL 控制实验，不能称为完整原版 MMD 崩溃。 |
| 修复宿主，当前 MME：预热、实际 AVI、完整重载 10 次，每次播放 5 帧后停止 | 10/10 次停止图与首次稳定预览 RGB 逐像素相同，哈希均为 `8A1908940BD20E48`；错误提示 0 次。输出 `sour-lifetime-fixed/`。 |
| 修复宿主，原版 MMEffect.dll：同样导出 AVI、完整重载 3 次并播放 | 3/3 次停止图与该引擎首次稳定预览 RGB 逐像素相同，哈希均为 `75B8AFCB16CF59D7`；错误提示 0 次。输出 `sour-native-lifetime-fixed/`。此时尚未修正下节的返回值宽度。 |

`verify_lifetime.py` 实际读取 PNG 比较 RGB，结果在 `lifetime-results.json`。`lifetime-comparison.png` 展示首次预览、修复前第 9 次重载偏色、修复后第 10 次重载图像。

重载最初几帧仍有 sdPBR 效果历史的收敛过程。本修复针对预热 / 播放后持续存在的错误状态，不以第一帧与稳定预览不同作为失败依据。

两组修复后的 AVI 均为 31 帧，解码后的每帧 RGB 与送入导出的 PNG 相同；正常结束后均为 `active=0 step=0 pending=0`。该修复不能表述为“AVI 自动播放已修复”，也不证明反馈者的全部黑屏场景已被覆盖。

### 回归覆盖

- `mme_host_object_cache_test` 覆盖“ID 一直存在而几何已替换”，模拟旧 D3D 绑定额外持有资源，检查引用平衡、重复帧及替换对象重新注册。
- `scene_mme_gpu_test` 覆盖同地址附件网格替换、模型原地卸载 / 重载，验证旧分配消失、重新分配后像素恢复；保留原来的“先渲染空场景再加载”测试。
- 返回值宽度修正前，完整 GPU 回归在 x64、x86 上通过；接入原版 MMEffect.dll 的 x64 回归也通过。x64 `scene_reset` 通过。

## 球面贴图返回类型的原版 ABI 差异

为排查头发外观差异，临时捕获器记录实际 shader、常量、纹理规格、渲染状态和目标像素。sdPBR 使用 `D3DFMT_A16B16G16R16`，已补齐测试捕获器对此 8 字节格式的支持。

`sour-port-trace/preview.passes` 和 `sour-native-trace/preview.passes` 各有 335 次绘制：

- 顶点着色器、渲染状态相同；部分实时 `TIME` 常量不同，不能将这些时间值直接判为错误。
- 第 222、223 次绘制的像素着色器和采样纹理不同，对应两个头发材质：材质索引 22、23，球面模式 2，贴图 `spa/hair.png`。
- 后续相同材质的球面贴图布尔参数也不同，将外观差异缩小到球面模式输入。

修复前的声明和实现返回 `unsigned char`。8 位返回契约不保证高 24 位有效；原版 DLL 的 32 位调用者会读到这些无效位。内嵌 MME 的 C++ 调用者按同一个 8 位声明读取并零扩展，所以只比较低 8 位的早期测试发现不了问题。此前接入原版 DLL 的头发差异不能直接归咎于材质渲染算法。

已改为 32 位 `MMH_SPHERE_MAP_MODE` 返回类型，在 `mmh_host_parity_test` 加入 ABI 宽度断言。无需 GPU 的实测通过原版 MMEffect.dll 的真实导入表入口，以 DWORD 调用生产函数，完整返回值依次为 0、1、2、3，见 `sphere-abi.log`。

### 图形会话中断期间的验证记录

两处修正后的 x64、x86 Release 主程序及测试目标均构建成功；对象缓存 CPU 回归均通过。最终重跑时，D3D9 设备创建不可用，HAL 返回 `0x8876086A` / `0x8876086C`，GPU / 原版宿主对照 CTest 明确跳过。仍可枚举 NVIDIA TITAN Xp，但 HAL 对 A8R8G8B8 / X8R8G8B8 的 `CheckDeviceType` 都返回不可用。

上述记录描述中途无法继续 GPU 复测的状态。用户随后恢复桌面，最终结果见下一节；这里保留发生过的设备错误，不将当时的跳过记成通过。

## 恢复桌面后的最终 GPU 复测

使用两处修正后的同一 x64 GPU 集成程序，在 NVIDIA TITAN Xp HAL、硬件顶点处理下顺序执行原版 MMEffect.dll 和内嵌 MME。场景、相机、分辨率、材质分配及绘制顺序相同；每边预热 8 帧，实际导出 31 帧 AVI，再完整重载 PMM 3 次，每次播放 5 帧后停止。

执行及核验脚本为本地 `out/sdpbr480-audit/rerun_final.py`、`verify_final.py`，汇总数据为 `final-run-results.json`、`final-verification.json`。

| 核验 | 最终结果 |
|---|---|
| 两套引擎的稳定预览、重复预览、AVI 收尾图、31 个录制帧、3 个重载停止图 | 共 37 对 RGB 图像逐像素一致，最大通道误差 0。稳定预览和所有重载停止图哈希均为 `8A1908940BD20E48`。 |
| 原版 DLL 对照中的头发材质 | 恢复球面贴图表现，与内嵌 MME 相同，确认此前差异由返回值宽度错误引起。 |
| 每边 3 次 PMM 重载 | 预热后均与该引擎初次稳定预览逐像素一致，无持续偏色，无错误提示。 |
| 实际 AVI 文件 | 每边均为 rawvideo、512×512、31 帧、约 30 fps、1.033332 秒；逐帧解码后 RGB 与各自导出 PNG 相同，最大通道误差 0。两边对应录制帧也相同。 |
| 正常 AVI 收尾 | 两边均为 `active=0 step=0 pending=0`；错误提示 0 次；进程退出码均为 0。 |
| x64 定向 CTest | `mmh_host_parity`、`mmh_host_pmx_parity`、`scene_mme_gpu`、`mme_host_object_cache`，4/4 通过、无跳过，19.18 秒。 |
| x86 定向 CTest | 同样四项 4/4 通过、无跳过，25.38 秒。Sour 完整双引擎 AVI 对照在 x64 执行。 |

实际绘制捕获各 335 次：两边顶点 / 像素着色器、采样纹理规格、渲染状态和深度目标规格相同。此前头发材质的球面模式 / 布尔参数差异消失。仍有实时 `TIME` 常量差异（例如 3.181 秒和 19.868 秒），以及部分中间目标原始字节哈希不同；本次最终 RGB 相同的结论不意味着所有内部状态逐字节相同。

可查看 `final-native-port-comparison.png`（接口修正前、修正后原版 DLL、修正后内嵌 MME），以及 `sour-final-native/capture.avi`、`sour-final-port/capture.avi`。

此次补测完成了此前受 HAL 不可用阻碍的验证。已证实的对象生命周期和返回类型差异均已修正并获得 GPU 证据；本场景未复现 AVI 额外禁用效果或正常结束后自动播放。测试仍通过生产代码集成程序完成，没有覆盖完整原版 MMD + MMHack GUI、所有效果配置及取消 / 失败操作路径。
