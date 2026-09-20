# Fix13：Ray 离屏回合错误继承主效果

## 可复现的故障链

本轮检查真实 `ray.fx` 与 `Shader/textures.fxsub`：主技术在
`ScriptExternal=Color` 前将 RT0 绑定到 `ScnMap`；ScnMap 是
`RENDERCOLORTARGET`，并非拥有 DefaultEffect 的 OFFSCREENRENDERTARGET。

原移植却把“效果声明了与当前回合同名的离屏纹理”等同于“该对象在当前
回合仍使用自身主效果”。`MmeApplyPassRecord` 先建立 MaterialMap 回合，
但首次绘制的 `MmeReportDrawError` 又执行 ray 的主技术，令 RT0 变成
ScnMap，并清空 MaterialMap 的 DefaultEffect 行。随后普通对象在离屏回合
查不到行，被绘制回调吞掉。因此可以在 **runFailed=0、无 DirectX 错误**
的情况下丢失 G-buffer 内容。

主线运行修复前生产函数探针，观察到：

```text
After repeat boundary: MaterialMap RT=1, MaterialMap rows=1
After first-draw choreography: MaterialMap RT=0, ScnMap RT=1, MaterialMap rows=0, runFailed=0
```

用户指定顺序为 `ray.x → ray_controller → 天空球PMX`；具体天空文件名尚未确认。
本轮根据已有日志选择 `Time of day fast.pmx`。该故障与日志中仅加载
ray.fx 和天空自身效果、没有加载 MaterialMap 的 `material_2.0.fx` 的日志
现象相符。这里证明的是实际离屏数据流错误，尚不是全场景图像等价证明。

## 绑定侧修复

- `material_bind.cpp` / `callbacks.cpp` 删除声明资源即继承根绑定的捷径。
  非零回合按该回合的 DefaultEffect 行选择；缺行或 hide 不绘制，none
  使用宿主裸几何，文件路径使用相应子效果。
- 行来源直接使用当前回合的资源记录；`self` / `OffscreenOwner` 使用
  当前回合载体，不从临时设备 RT 状态猜测归属。
- `main_default` 根据本轮 IDA `0x18002b981` 证据，查询同一对象及同一
  材质在主回合的配置，保留逐材质覆盖，不再替换成全局默认效果路径。
- 子效果具备 scene 技术时，使用与根绑定相同的技术选择规则；交给
  pass planner 的实际回合绑定处理。pass planner 的修复由并行审计合入，
  包括纠正将 `GenerateMipSubLevels` 误译成主脚本 step 的问题。

## 回归测试与限制

`tests/mme_offscreen_turn_test.cpp` 使用真实 HAL、生产 Ray loader、生产
`MmePassBookkeeping` 和 `MmeReportDrawError`：

- 检查首次绘制前后 MaterialMap 的 RT0 和 DefaultEffect 行保持。
- 验证 ray 的 self=hide 不会因声明资源而继承主效果。
- 用真实天空文件名匹配 `*.pmx`，实际加载 `material_2.0.fx` 子效果。
- 检查 main_default 的整对象及逐材质映射、none 与缺行区别。
- 在 ScnMap 写入像素标记，再执行回合尾路径；标记必须保持，防止回合
  尾错误重跑主 ray 的 Clear 命令，即使后面的 RT 恢复掩盖了错误。

测试源码已通过 MSVC x64 `/W4` 编译；主线x64 HAL执行通过，修复后
首次绘制仍为 `MaterialMap RT=1, ScnMap RT=0, MaterialMap rows=1`，
回合尾ScnMap像素标记保持。两架构完整结果见 fix14_ray_scene.md。
天空 ModelData 在本最小测试中仅承担名字和绑定，不加载其 PMX 几何、
骨骼或形态；完整场景及原版图像 A/B 仍需另外验证。
