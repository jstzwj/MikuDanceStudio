# Fix 12：标准效果缺失时的 MME 初始化失败路径

## 本轮先验证

基线提交：`4833705`。重新使用本机 IDA 9.1 的 `idapro` 打开原 MMHack 数据库的独立副本，反编译并核对指令；没有用 fix11 报告代替本轮核验。原二进制：

`C:/Users/jstzw/Documents/github/OpenMMD/MMEffect_v037x64_English/MMEffect_v037x64_English/MMHack.dll`

SHA-256：`e450b25e5cd2bcf125ca4b8eb9e57f858ec3ca379633c7d25f9dd40e864b390d`。

原 `BeginScene`（`0x180002DE0`）证据：

| 位置 | 核验到的控制流 |
|---|---|
| `0x180002E35` | 只检测设备的初始化完成标志；不以 effectsDisabled 阻止重试 |
| `0x180002E4B–0x180002E4F` | 调用 Initialize，任意非零返回均为失败 |
| `0x180002E65–0x180002E76` | currentEffect 为空选用缺失默认效果的长错误文案；否则使用 Initialize Error |
| `0x180002E7D` | 每次失败调用 MessageBoxA，MB_ICONERROR，无“一次提示”闩锁 |
| `0x180002E83` | 报错之后才置 effectsDisabled=1 |
| `0x180002E8A` | 跳到返回 0；不写完成标志、不调用真实设备 BeginScene |
| `0x180002E8F–0x180002E9A` | 仅成功后置完成标志，再调用真实 BeginScene；成功不会自动清除 disabled |

因此：失败的下一次 BeginScene 必须再次初始化、再次报告；0（S_OK）不是说明真实设备场景已经开始。返回 D3DERR_NOTAVAILABLE 和只提示一次都确实不对齐。

本轮原始反编译/指令与核验脚本在本地 `build-x64/fix12-effect-failure/reference.txt`、`read_reference.py`（构建证据目录，不提交整个第三方数据库）。

追加检查销毁调用链：`0x180001D50` 的 Release 进入 `0x180001DB0`，只判断 Cleanup 导出是否存在，不以初始化完成标志为条件；末尾清除完成标志。本轮 `destruction.txt` 记录调用者反编译。标准效果缺失前 Initialize 已经可能创建 context、owner manager 与 effect engine，所以失败设备同样必须进入 Cleanup。

## 根因与修复

问题来自两套状态：`mme_host.cpp` 原本已有重试状态机，但 bridge 以 effect 非空作为“MME 可用”的条件，绕过了它，另行维护缺失标志与一次性弹窗标志。

- `src/render/mme/mme_bridge.cpp`：设备存在即登记窗口、路径来源及可空标准效果。移除缺失效果特判和弹窗闩锁；所有有效设备统一进入 host 初始化路径。把 `g_available` 更名为 `g_deviceRegistered`，不再混淆设备登记与效果初始化成功。
- `third_party/mmeffect/include/mmeffect/device_initialization.h`：恢复有明确生命周期的 `DeviceInitialization` 类型。失败保持 pending，成功提交 ready，销毁重置；禁用效果是独立状态。
- `third_party/mmeffect/src/mme_host.cpp`：实际生产初始化、激活判断、lost/reset 通知与销毁使用该类型。保留原错误文案、弹窗后置 disabled、失败 S_OK 且不调用真实 BeginScene 的次序。移除销毁 Cleanup 的成功初始化门控，恢复原版失败设备也清理部分资源的路径；内置 Cleanup 必然存在，其内部逐个检查待释放对象。
- 相关 API 注释改为上述真实契约，移除“缺少标准效果时全部直通”的过期说明。MME 继续静态内置，没有恢复代理 DLL、IAT 或 vtable hook。

## 验证

`tests/mme_initialization_test.cpp` 使用生产路径同一个状态类型，注入初始化结果与报告回调；覆盖连续失败逐次报告、不触发真实 BeginScene、disabled 不阻止重试、任意非零失败、恢复成功、成功后不重复初始化、disabled 粘滞、销毁后重新尝试、正常首次成功不禁用效果。

独立 MSVC x64 `/std:c++17 /EHsc /W4` 编译运行通过，输出 `MME device initialization regression passed`。主线负责纳入 CTest 及完整 x64/x86 构建。

边界：回归测试不弹真实模态窗口，未执行缺失效果时的 GUI 逐帧演示。失败后反复提示是本次明确恢复的原版异常行为；没有据此宣称全部 MME 图像和其它异常路径均已严格一致。
