# Issue #7：附件播放容量与原版 x64 对照

2026-10-30，对照 MikuMikuDanceE v9.32 x64 的附件播放行为。在独立测试进程中运行原版初始化、推进和停止逻辑，与移植实现作逐字节 A/B 对照；测试不启动原版 GUI。

参考文件 SHA-256：`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`。

## 根因与容量证据

正确容量是 **255 个附件槽，合法下标为 0..254**。初始化、推进和停止均须覆盖全部槽位；对象、关键帧、游标和活动标记使用同一槽号。

| 播放阶段 | 兼容性要求 | 实现位置 |
|---|---|---|
| 初始化 | 清理全部 255 个槽的活动标记，为非空槽建立关键帧游标 | `UpdateBoneFrames` / `InitAccessoryTracks` |
| 推进 | 同步推进游标、关键帧和活动标记，包含最后一个槽 | `PlaybackPoseAdvance` |
| 停止 | 扫描全部 255 个对象槽，为非空槽应用停止帧 | `StopPlayback` / `ApplyAccessoryTrack` |

移植有两处数组长度错误：

- `AppRuntimeState::accessoryTrackActive[55]`：初始化即会清零所有 255 个槽，场景没有附件也会越界。退出时崩溃可以是此前内存破坏的延迟表现。
- `MMDAppState::accessoryTrackCursor[55]`：后面的 800 字节实际对应其余 **200 个游标**。即便写入仍位于整个应用对象内，跨越已声明的数组边界仍属于 C++ 未定义行为。

修复让对象表、关键帧表、游标、活动标记及三个播放循环共同使用 `mdl::kAccessorySlotCount`。活动标记继续由现有 `AppRuntimeState` 持有；兼容区中的历史标记不再是生产代码的活动存储。访问器保留有效调用行为，并在调试构建中断言非法下标。

## 连续布局的进一步修正

完整恢复 255 个历史标记后，发现原来的 x64 填充少了一个字节，导致三个相邻成员的偏移也不正确。

相邻字段依次为：255 个四字节游标、255 个一字节历史活动标记、7 个控件的启用状态、模型透明模式、眨眼阶段和自然对齐后的捕获纹理指针。数组容量和相邻字段边界均由布局断言约束。

相应修正 x64 布局断言及捕获指针前的填充。捕获纹理及后续字段偏移、完整兼容对象大小 `0xA55E0` 保持原值；x86 原有相邻字段偏移不变。数组末项也加入偏移断言，防止再次把完整数组误切成数组与填充。

## 保留的附件轨道逻辑

1. 播放初始化：每个槽先清活动标记；空槽直接继续。非空槽设为活动、游标归零；沿 `next` 找到首个不早于当前帧的关键帧。越过末帧时停用轨道并复制末帧状态。
2. 推进：`advance != 0` 时在模型推进后返回；最终 settle 调用才更新附件。停用的附件直接跳过。沿链表前进；恰好命中末帧时仍保持活动，真正越过末帧后才停用。
3. 恰好命中关键帧或越过末帧：复制关键帧载荷。两个关键帧之间，仅缩放与不透明度作线性插值；显示、阴影、父模型／骨骼、位置和旋转取前一帧。
4. 停止：按停止选项确定编辑帧，再为所有非空附件应用该帧，恢复控件启用快照。

原版 x64 使用单精度完成秒数乘 30、千分之一帧量化和附件插值比例计算，停止时截断单精度的秒数乘 30。移植原先在部分阶段使用 double；仅修容量后的第一轮 A/B 检出 **9,583 个槽状态差异**。修复保留 x64 的 float 中间步骤，x86 保留既有 double 路径。

例如 `0.277925014f` 秒、关键帧 0/7/31、缩放 0.1/0.2/1.1：原版缩放结果位模式为 `0x3E8016F1`，已固定为不依赖原版文件的回归用例。

## 验证与复现

常规回归 `accessory_playback` 覆盖无附件、槽 0/54/55/254 的稀疏场景、全部 255 个附件、重复开始／停止、插值、末帧命中、越过末帧及从末帧后开始。测试兼容区标记和 UI 快照不被游标写入覆盖，并在编译时检查各表容量。

```powershell
cmake --build build-x64/build --config Release --target accessory_playback_test
ctest --test-dir build-x64/build -C Release -R '^accessory_playback$' --output-on-failure
cmake --build build-x86/build --config Release --target accessory_playback_test
ctest --test-dir build-x86/build -C Release -R '^accessory_playback$' --output-on-failure
```

可选原版 A/B 测试只在 x64 构建。它复用已有 `OriginalPhysicsModule`，校验原版文件 SHA-256，解析导入并调用原版函数；不启动原版 GUI，不分发原版二进制，也不修改原版文件。夹具创建隐藏的 Win32 窗口，无需 GPU 或第三方模型资产。

```powershell
cmake --build build-x64/build --config Release --target accessory_playback_reference_test
& build-x64/build/Release/accessory_playback_reference_test.exe `
  'C:/path/to/MikuMikuDanceE_v932x64/MikuMikuDance.exe'
```

对照两种槽密度、两种停止模式、5 个开始时间，以及每次 181 个推进调用（包括 `advance=1` 分支），共 **3,660 个观测点、933,300 次槽状态检查**。全部游标、活动标记及完整附件记录逐字节相同，7 个控件的启用状态和停止编辑帧一致，透明模式及眨眼阶段保持完整。

MSVC ASan 专项在 x86/x64 均通过：测试、`playback_state.cpp`、`timeline_advance.cpp`、`app_ctor.cpp` 使用 `/fsanitize=address /Zi` 编译，其余依赖沿用正常 Release 库。隔离副本仅把运行时活动标记改回 55 项，两架构均在 `InitAccessoryTracks` 的 `playback_state.cpp:228` 报告一字节堆越界写；这个复现不需要加载附件。副本位于 `out/issue7/asan-old-*`，没有回写工作区代码。

两架构完整 Release 构建通过。全量 CTest：x64 **97/97**（104.86 秒），x86 **94/94**（151.56 秒），零失败、零跳过。测试输出和 JUnit 为 `out/issue7/ctest-x{64,86}.{log,xml}`；新增 C++ 测试通过 clang-format 检查，`git diff --check` 通过。

本次对齐结论限定于上述有效附件轨道与播放路径；不扩展为整个应用、骨骼／物理轨迹或完整 GUI 的等价证明。本地构建、ASan 及测试日志保存在未纳入版本控制的 `out/issue7/`。
