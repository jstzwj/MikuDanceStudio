# P2：完整 CPU 运行时

**P2 已完成。** 独立 CPU 运行时、Windows 共用算法与无窗口入口均已落地，并通过三平台运行验收。完整编辑器、GPU 呈现与 MME 分别继续按 P3／P4／P5 验收。

## 交付边界

`mikudancestudio_scene_runtime` 拥有模型资源、实例、关键帧游标、统一 Bullet 世界和场景时钟。上层传入磁盘文档并消费已完成的 CPU 快照，不需要 `MMDApp`、原版对象布局、COM、D3DX、窗口或图形设备。

Windows 兼容宿主与新宿主共用以下算法核；Windows 适配器仍保留 D3DX 数学及原版存储布局，供独立对照。

| 算法／资源 | 实现与行为 |
|---|---|
| 骨骼 | 五阶段局部／世界变换、CCD IK、限位、追加与固定轴、物理前后求值、外部父级 |
| 时间轴 | 同源 Seek、播放初始化及推进算法；保留 Seek 与连续播放原有差异及物理选择标志 |
| Morph／蒙皮 | PMD 基础 Morph 索引映射；PMX Group、Vertex、Bone、UV／追加 UV、Material；BDEF1／2／4 和 SDEF |
| 物理 | 同源刚体／关节创建、跟随、重置、关节过伸校正和读回；统一场景世界及追帧调度 |
| 编码 | 显式 UTF-16LE 与严格 Windows CP932；映射表可重复生成，并与 Windows 解码器穷举对照 |
| 附件 | 静态单 Mesh `.x` 文本、二进制及 MSZIP；显式法线／材质、分组、纹理路径及最终骨骼附着变换 |
| 场景 | PMM v1／v2 模型、相机、灯光、阴影、重力、附件及父级关系；同源求值和保存重载 |

不新增 QDEF、PMX 2.1、PMX UTF-8、带 Frame／动画的 `.x`、图形输出或 MME。这些能力继续明确拒绝。`ModelInstance` 是运动学宿主，跨模型关系与物理必须使用 `SceneRuntime`。

`MotionClip` 支持模型 VMD／IK／可见性和 VPD；场景全局 VMD 通过 `BindGlobalMotion` 合并进已加载 PMM 的相机、灯光和自阴影轨道。运行时轨道替换沿用 `SceneSession` 的保护：原始字节包络过期后拒绝保存，不能悄悄写出旧轨道。新增编辑轨道的 PMM 序列化属于 P4 编辑命令工作，不在这里伪装为已实现。

## 所有权与调度

资源在导入时复制为不可变对象，实例与求值不借用调用方文档。准备、物理前求值、统一世界推进、物理后求值和最终发布有明确阶段；非法顺序、资源身份不匹配、无效父级及非有限输出返回诊断。追帧子步准备次数与最终发布次数分开计数，每个最终帧只发布一次。

每个场景持有自己的随机序列，避免两个场景通过全局 `rand()` 相互影响。卸载时先删除关节再删除刚体，最后销毁世界；被相机、附件或外部父级轨道引用的模型拒绝卸载。附件可独立挂接／卸载，资源共享引用可安全跨越实例卸载。

数学保留行向量、矩阵布局、乘加顺序及浮点存储边界。向量归一化统一用双精度平方根计算倒数，再存为浮点，避免 CPU 厂商相关的 reciprocal-root 估计影响 IK 收敛。一般逆矩阵继续使用 P1 的检查型实现，不承诺 D3DX 位级相同。播放快照与场景轨道共用毫帧取整及范围检查，防止整数帧因截断落后一帧。

## 无窗口入口

```sh
cmake -S cmake/portable-baseline -B out/portable -DBUILD_TESTING=ON
cmake --build out/portable --config Release --parallel 2
ctest --test-dir out/portable -C Release --output-on-failure
```

新增 `mds_cpu_scene_cli`；原有仅操作文档的 `mds_scene_cli` 保留。

```sh
mds_cpu_scene_cli --input scene.pmm --frame 0 --frame 10 --play-frames 600 --output scene.json
mds_cpu_scene_cli --input model.pmx --motion motion.vmd --physics-mode 2 --frame 0 --play-frames 600 --output model.json
```

CLI 自动解析 PMM 的模型和附件引用，输出骨骼、顶点、刚体、附件变换和调度计数。`--save` 保留已有 PMM 轨道，要求输出到原场景目录且不覆盖输入；移动目录后的路径改写留给完整 Save As 服务。API 本身不决定文件系统或窗口策略。

## 验收规则

| 门禁 | 标准 |
|---|---|
| P1 数值基准 | 保留原始 `p1_model_v1.json`；单位姿态精确，其他分量 `2e-5 × max(1, abs(reference))` |
| P2 数值基准 | 公开生成 PMD／PMX／PMM，31 组场景和物理模式；Windows 验证后冻结 `p2_scene_v1.json`，各平台使用同一阈值，不自动重录 |
| 重复与保存重载 | 独立进程重复结果精确；相同配置、相同操作历史的 PMM 重载结果保持数值标准。PMM 不保存 Bullet 的瞬时仿真世界 |
| Windows 原生对照 | 324 个场景检查点；骨骼矩阵、顶点、法线、UV、刚体状态及附件变换使用同一严格阈值 |
| 生命周期 | 错误加载保留原场景；阶段错误、过期资源、父级引用、卸载、世界销毁及不可变资源留存 |
| 长时间运行 | 公开生成场景与本地真实模型；四种物理模式各 600 帧、有限值、相同实现精确重复、拖动、暂停、重置和卸载重载 |
| 编码与 `.x` | CP932 单／双字节穷举、表生成检查；文本／二进制／压缩附件对照及截断／损坏／上限拒绝 |
| 依赖 | 实际 CMake 图、项目包含闭包和 CLI 二进制导入审计；不链接 UI、GPU 或原版模型对象 |

依赖审计单独记录原始 Bullet `btQuickprof.h` 在 Windows 下使用系统性能时钟的条件包含；这是已固定的第三方平台适配，不是新核心的 Windows SDK 依赖。上游未启用的 DBVT benchmark 分支使用了 `btQuickProf.h` 拼写，审计只对该位置映射到真实文件，继续扫描其内容，不忽略其他未解析包含。最终二进制仍检查不存在 User32、GDI、COM 或图形库。

真实 Sour Miku 的无物理骨骼及顶点可严格对照。开启物理后的新数学路径与旧 D3DX 路径存在轨迹差异，单独测量，不能把“有限值及精确重复”写成“旧路径数值一致”。此差异不提高公开场景门槛，也不代表原版 MMD 长期物理兼容性已经解决。

真实模型验收从本地文件导入，并为其名字及骨骼生成内存 PMM 动画载体；先验证拖动和播放，再保存、重新加载并重放同一操作历史，比较刚体和顶点／法线／UV。仓库只保存生成器、样本哈希与验收结果，不包含私有模型、贴图或生成载体。

## 最终证据

实现提交为 `8d77ee1`、`5081e20`；跨平台数值与帧号修正为 `24b5728`；真实模型 PMM 门禁为 `6d4a3cb`、`fe150dc`。P1 复核修正为 `e23dc9c`，其数值快照保持不变。

- [最终四列 CI](https://github.com/jstzwj/MikuDanceStudio/actions/runs/41230055318) 对 `fe150dc` 的干净检出全部通过。Windows x64／x86 每列两轮各 62 项，Ubuntu 22.04 x64／macOS 15 arm64 每列两轮各 60 项；每轮仅跳过已有私有 PMM 外部样本测试，P2 门禁均实际执行。
- 每列均通过 31 组公开场景／物理模式、独立进程精确重复、PMM 保存重载及依赖审计。P2 冻结基准未重录；最大绝对分量差为 `5.603e-5`，按既定 `max(1, abs(reference))` 缩放后的最大差为约 `1.628e-5`，低于 `2e-5` 门槛。
- Windows x64／x86 Release 编辑器构建通过。相关原生 CPU 回归分别 8／7 项通过，包含各 324 个严格场景检查点、独立 D3DX 数学、原有 VMD／物理／CPU 场景及模型路径；x64 另有原有 PMD OpenMP 蒙皮测试。
- 本地 Windows x64 的 Sour Miku 通过四种物理模式各 600 帧生成动画、有限值及精确重复、拖动、暂停、重置、卸载重载和资源留存；真实名字及骨骼生成的 PMM 载体通过当前帧保存、重载及同历史重放。其他平台的长时间运行使用公开生成模型，未据此声明私有真实资产已在所有平台测试。
- Sour 的 30 帧旧 D3DX 动态对照仅作为测量：顶点分量最大差 `0.383911` MMD 单位，法线分量最大差 `0.152594`。无物理路径通过严格对照；动态差异保留为兼容性问题，不归入严格通过项。
- [版本化验收摘要](evidence/cross-platform-p2.json) 保存修订、工具链、计数、数值、样本哈希、依赖和本地日志哈希；原始双轮日志、JUnit、生成资源、快照和实际 CMake 图保留在 CI 工件中。工作机测试全部使用 CPU 路径，不创建窗口或图形设备。

真实模型本地验收入口：

```sh
scene_runtime_real_test /path/to/model.pmx
scene_runtime_real_test /path/to/model.pmx --session-only
scene_runtime_native_test --model /path/to/model.pmx
```

最后一个入口将严格运动学对照与动态测量分别报告；公开场景的严格门禁由 CTest 的 `scene_runtime_native` 执行。下一阶段为 P3：将新运行时快照接入共享帧描述，并补齐三平台的加载器、着色器工具链、Surface 和呈现。
