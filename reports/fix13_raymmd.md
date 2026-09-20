# RayMMD 专项：新核验与修复（2026-09-20）

## 结论与范围

当前代码不是已证实的 MMD 9.32 / MME 0.37 严格一比一实现。本轮由三个子代理分别检查效果引擎、宿主桥和原版 IDA，主线复核并运行实际 Direct3D9 HAL 测试。没有依据给全项目填写还原百分比，也没有完成所有源码/所有行为的穷尽审计。

本轮修复三个确凿缺陷，但**尚未复现用户具体场景的“加载 Ray 后画面挂掉”**。因此不能宣称已经确定唯一根因或修复全部 Ray 图像。原有未提交修改保留；MME 继续静态链接进宿主，没有恢复代理 DLL、注入或 vtable/IAT hook。

## 已修复

### 1. VIEWPORTPIXELSIZE 被中间渲染目标尺寸覆盖

`material_bind.cpp` 原先实时调用设备 GetViewport。Ray 的 `Shader/common.fxsub:29` 声明此语义，`ray.fx:535–559` 的降采样着色器又显式按 2/4/8 缩放尺寸。中间 pass 把 viewport 改小时，参数随之变小，形成重复缩放，影响采样偏移与模糊/重建结果。

原版 MMEffect.dll `0x18005EDD0` 没有调用 GetViewport：根回合读取缓存的 BeginScene 宽高；存在当前 offscreen wrapper 时读取 wrapper+0x40/+0x44。后两项来自 `0x18002D434/43B` 的资源 surface descriptor。

修复为 `g_beginViewport` 或 `currentBindingOffscreen` 的 surface 尺寸。生产 binder 实测：BeginScene=1024×512、当前 viewport=256×128 时仍绑定1024×512；offscreen=640×360、当前 viewport=160×90时仍绑定640×360。

### 2. Draw=Buffer 的两个 D3D 状态枚举译错

`pass_planner.cpp` 把原版状态7误译成 FILLMODE（实际8），把161误译成 SCISSORTESTENABLE（实际174）。原版实际保存/清零/恢复的是 ZENABLE(7)、LIGHTING(137)、STENCILENABLE(52)、MULTISAMPLEANTIALIAS(161)。

重新取得的汇编证据：`0x18005AACA` 读状态7，`0x18005AB24` 设置参数7，`0x18005ADDD` 恢复7；状态161对应 `0x18005AB0F/0x18005AB61/0x18005AE22`。

修复后深度测试/MSAA按原版处理，填充模式和 scissor 不再被额外改变。真实HAL测试调用同一生产函数，红色背景、depth=0、ZFUNC=LESS，shader不自行关闭深度测试：修复前中点为 `0xFFFF0000`，退出1；修复后为 `0xFF00FF00`，退出0。还检查 scissor外像素保留及六项状态、顶点声明、VB/offset/stride、IB恢复。

**触发限制：本地 Ray 的 Buffer passes 已显式设置 ZEnable=false。因此本缺陷不能单独解释用户首次加载 Ray 的失效。** 撤回 audit9 中“原版故意 SetRenderState(FILLMODE,0)”的错误说法。

### 3. 卸载对象后身份索引悬空

`mme_host.cpp` 释放对象时未移除 `objectById`。模型/附件地址被复用后，新对象被误判为已注册，并可能访问已释放记录。现先清除身份索引再释放，新增实际生产清理函数的生命周期回归。详见 [宿主缓存报告](fix13_host_object_cache.md)。它解释卸载后重载的风险，不证明首次加载失效。

## 验证

- x64、x86 Release 完整构建成功，CTest 各18/18通过，包括真实HAL像素与视口参数测试。
- x64分别加载构建目录及用户 OpenMMD 目录的真实 `ray-mmd/ray.fx`：各39资源，DeferredLighting暂停/恢复成功，31次Buffer绘制均S_OK，runFailed=0，主RT/深度/viewport恢复通过。
- x86加载同一构建目录Ray素材的独立smoke也通过：31次Buffer绘制，runFailed=0。
- `ClearSetStencil` 仍有 unsupported warning，但这两次脚本均完整运行；不能将此warning当成拒绝加载或卡死证据。
- 测试程序不打开可见编辑器、不修改用户场景。真实Ray smoke是空场景：未验证宿主所有回合、DefaultEffect子效果、模型、控制器、天空盒组合或最终图像一致性。
- Windows UI自动化两次连接均报 native pipe 不存在，未做GUI端到端复现。已有历史dump不能直接代表当前内置版本。

复现命令：

```powershell
cmake --build build-x64/build --config Release
ctest --test-dir build-x64/build -C Release --output-on-failure
cmake --build build-x86/build --config Release
ctest --test-dir build-x86/build -C Release --output-on-failure
./build-x64/build/Release/mme_ray_smoke_test.exe C:/absolute/path/to/ray-mmd/ray.fx
```

构建与原始证据在 `build-x64/ray-investigation/`（原版quad汇编、viewport反编译、前后像素结果、两套素材smoke、CTest日志）；构建日志为 `build-x64/ray-investigation-build-final.log`、`build-x86/ray-investigation-build.log`。非仓库Ray素材不作为无条件CTest依赖；独立smoke目标支持路径参数。

## 仍存在的差异及下一步定位

1. 每pass仍调用全量参数binder，原版仅调用B5B0子集。此次修复尺寸来源，没有宣称完成所有参数更新时机对齐。
2. Buffer回调原版对HRESULT非零退出，当前若为正值仍视为成功。尚无实际Ray正HRESULT失败证据。
3. `ViewportRatio` 在解析期换算后不保留比例，Reset重建继续使用旧 reqWidth/reqHeight；窗口/输出尺寸变化后的资源尺寸仍需修复与原版核验。资源重建失败也没有通过 `MmeEngineOnResetDevice` 返回值传播。
4. 若用户场景仍挂，应锁定具体加载顺序及首个 `SasNoteRunFailureFirstCause` 日志。runFailed锁存会停止该scene效果后续绘制；这必须以首个失败命令/HRESULT或实际挂起栈为证，不能从黑屏外观推断。

需要的剩余复现信息是“黑屏但窗口响应／整个程序无响应或崩溃／仍更新但画面错误”的区别，以及触发场景与加载顺序。
