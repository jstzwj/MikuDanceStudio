# Ray + controller + 天空球的离屏回合修复

2026-09-20。用户复现顺序：ray.x、ray_controller、天空球PMX；表现是渲染结果与安装MME的原MMD不同。天空文件名未获进一步确认，本轮按现有日志中的Time of day fast检查，并补测Sky with box文件名匹配路径。

## 查实的原因

素材差异已排除：构建目录与用户指定OpenMMD目录内898个效果、配置、PMX/X及纹理文件按相对路径逐项SHA-256对比一致。

本轮查实的是移植的**回合调度与效果选择错误**。并不是DirectX报错才会导致错误图像：生产GPU探针在runFailed=0时也复现了错误。

1. MaterialMap回合开始，正确绑定MaterialMap并发布DefaultEffect规则。
2. 旧代码因ray.fx声明了MaterialMap，就误认ray.x在这个回合应继续使用主效果。实际原版按`(turnId, model, subset)`查找该回合的效果；MaterialMap里的`self=hide`也适用于ray.x。
3. 错误运行主ray脚本使RT0改成ScnMap；旧的脚本末尾推断又清空MaterialMap规则。
4. 天空球等对象找不到离屏分配规则，绘制被跳过。最终ray拿到的材质缓存不正确，因此即使shader编译、各pass调用都成功，也不能得到原版结果。

另一个独立误译发生在回合尾：原版`0x18005D18D`调用纹理vtable+0x80，即`GenerateMipSubLevels()`；旧代码却在这里再次执行ray主脚本，错误清空/重定向场景目标。

## 原版证据

用户提供的MMEffect 0.37 x64原DLL，IDA只读核验：

- `0x18005D17C–0x18005D18D`：wrapper → offscreen record → resource → texture → slot16，纹理mipmap生成。
- `0x18005A449–0x18005A4E5`：读取wrapper+56的turn id，精确查找绑定；没有“同名离屏资源则继承根效果”的规则。
- `0x18002C910`、`0x18002CA80`：每个turn建立模型绑定；资源枚举注册wrapper不等于复制根效果绑定。
- `0x18002ACE0`：DefaultEffect包含self/hide/path解析；`0x18002B981`的main_default递归查询同对象、同材质的主回合赋值，而非全局默认fx。

原始汇编/反编译JSON与详细解释保存在`build-x64/ray-investigation/ida_offscreen_turn_fix.md`及同目录`ida_bookkeeping_18005d130.json`、`ida_turn_assign_18002ca80.json`、`ida_turn_models_18002c910.json`、`ida_default_rows_18002ace0.json`、`ida_scene_step_18005a410.json`。

## 改动

- `pass_planner.cpp`：修正mipmap调用；scene step/resume/full-run使用当前回合绑定；删除按实时RT0改写DefaultEffect的推断；绑定保留至本回合全部resume结束。
- `material_bind.cpp/.h`、`callbacks.cpp`：删除“声明离屏资源即使用根效果”的捷径；从回合记录取规则及owner；修正main_default同对象/同材质语义。明确分配的scene子效果仍可被驱动。
- `tests/mme_offscreen_turn_test.cpp`：真实HAL、真实ray.fx、真实生产回合函数。使用文件名绑定实际加载material_2.0.fx和material_skybox.fx；检查self=hide、main_default、none、缺行；用ScnMap像素标记检出错误的回合尾Clear。
- CMake新增可选`MIKUDANCESTUDIO_RAY_TEST_EFFECT`路径；指定本地ray.fx后将真实素材两项GPU回归纳入CTest，不把用户素材或绝对路径写进仓库默认配置。

## 修复前后实测

| 生产函数探针 | MaterialMap为RT0 | ScnMap为RT0 | MaterialMap规则 | runFailed |
|---|---:|---:|---:|---:|
| 修复前，回合刚建立 | 1 | 0 | 1 | 0 |
| 修复前，首次绘制准备后 | 0 | 1 | 0 | 0 |
| 修复后，首次绘制准备后 | 1 | 0 | 1 | 0 |

日志：`offscreen-before.txt`、`offscreen-after.txt`。修复后回合尾ScnMap像素标记保持；天空文件名真实触发子效果编译与绑定，所有断言通过。

x64/x86 Release完整构建成功；启用本地Ray素材的CTest两边各20/20通过（无跳过）。新增第二种天空文件名后又单独重跑离屏测试。日志为`scene-build-final-x64.txt`、`scene-build-final-x86.txt`、`scene-ctest-x64.txt`、`scene-ctest-x86.txt`及`offscreen-final-x64.txt`、`offscreen-final-x86.txt`。

```powershell
cmake -S . -B build-x64/build -DMIKUDANCESTUDIO_RAY_TEST_EFFECT=C:/path/to/ray-mmd/ray.fx
cmake --build build-x64/build --config Release
ctest --test-dir build-x64/build -C Release --output-on-failure
```

## 验证边界

本轮已证明并修复会破坏该场景数据流的错误，但**没有完成原版和新版完整场景的逐像素A/B对照**。测试的天空ModelData用于名字/绑定，不冒充真实PMX几何、morph与控制器最终图像验证。

Windows Computer Use仍不可连接。另尝试了独立desktop上的完整宿主诊断，停在`InitMainWindowAndD3D`，120秒watchdog退出124，尚未加载Ray，也没有产生画面；不能把它解释为Ray渲染失败。该未完成诊断源码/记录仅保留于`build-x64/ray-investigation/scene-before/`，不纳入正式构建。

此前已记录的设备Reset尺寸、完整绑定时机及其它严格一致性缺口，本轮未扩大修改。MME仍直接内置，未增加代理DLL/hook；原有未提交工作保留，本轮未提交或推送。
