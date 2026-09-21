# x64 重建基准和证据规则

本项目的行为基准是 **MikuMikuDanceE v9.32 x64**，效果语义基准是 **MMEffect v0.37 x64 English**。x86 反编译和旧移植代码可帮助恢复算法与含义，但不是 x64 字段、指针宽度、浮点求值或容量的替代证据。目标是可维护的行为重建，不是声称取得原作者源码。

## 二进制身份

2026-09-22 审计重新核验了用户本地 OpenMMD 目录中的以下文件；二进制不由本说明授权再分发：

| 参考文件 | SHA-256 |
|---|---|
| `MikuMikuDanceE_v932x64/MikuMikuDance.exe` | `07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1` |
| `MMEffect_v037x64_English/MMEffect_v037x64_English/MMEffect.dll` | `74b3f5882a3bad28131cbc57994e2f960938e6ca850fc7139018ce4822bd87dc` |

IDA 导出的 MMD 地址以 `0x7FF7CB420000` 为 image base。重开数据库或受到 ASLR 影响时应记录 image base/RVA，不把一次会话的 VA 当作永久运行时地址。session ID 是工具运行状态，不是长期证据标识。

证据入口：[第20轮总报告](../reports/audit20_summary.md)、[应用原始 IDA 导出](../reports/audit20_app_ida_evidence.json)、[MME 原始 IDA 导出](../reports/audit20_mme_ida_evidence.json)、[第21轮修复汇总](../reports/fix21_summary.md)。历史审计描述的是当时工作树；当前修复状态见 [PORTING_STATUS.md](PORTING_STATUS.md)。

## 如何判定一致

1. 每项行为明确输入、前置状态、参考版本和可观察输出：文件消费、窗口状态、骨骼姿态、顶点/材质、物理轨迹、效果脚本执行顺序等。
2. 反编译用于导航。涉及有符号比较、浮点中间值、分支合并、对象大小和调用约定时，再看反汇编和调用者/消费者。Hex-Rays变量名不视为原作者命名。
3. 静态对应、编译通过、布局断言、独立测试、原版A/B回归分别记录；不能相互替代。“扫描过文件”也不等于逐函数对照。
4. 差异需说明适用范围：普通有效文件、边界有效文件、损坏文件、硬件/插件缺失、释放失败等。不为追求字面一致复制C++未定义行为。
5. 严格数值比较须记录编译选项、D3DX运行库、Bullet版本和时序输入。x64基准的D3DX为`d3dx9_43`；x86材料里出现的`d3dx9_32`和x87算序不能直接外推。实时物理比较需控制步长与初始状态。

## 运行时类型和兼容边界

- 新业务代码使用具名对象、成员和明确的拥有关系。不要以 `At<T>(bytes, offset)`、旧虚表槽或地址表作为运行时模型。
- 原版布局断言是校验证据工具，不是要求独立可执行程序永远保留原版对象布局。现有平面布局仍用于迁移期间的对应检查；应逐子系统迁移生产者与消费者，避免只有外观类型化而底层仍取旧偏移。
- 磁盘格式、运行时内存和外部COM/插件ABI分开维护。例：模型运行时名称容量50字节，PMD名称磁盘字段仍20字节；不能将运行时`sizeof`直接套用到格式读取。
- `RawPad` 只表示未恢复区域。恢复具名成员必须核对所有读写者、大小、对齐、所属对象和释放方式。不能随意将padding命名为未经证明的功能。
- 非拥有的Win32/COM/Bullet指针可以是显式借用；拥有的数组/对象要有配对分配释放与清晰生命周期。不要把裸指针数量当作错误数量。
- MME 的渲染语义、效果参数、pass/script时序纳入宿主。宿主已经能主动提供上下文，无需代理d3d9.dll、IAT patch或虚表注入。历史 `mmhack` 目录名不构成重新实现注入的需求。

## 布局头的真实维护方式

`model_layout.hpp`、`bone_layout.hpp` 是**人工维护**的运行时结构及断言。历史注释提到的 `scripts/gen_model_layout.py`、`scripts/gen_bone_layout.py` 不在本仓库，不能宣称可运行它们重建。

`scripts/regen_layout_pins.py` 是旧pin表整理脚本，读取已存在的输入并带特定历史字段映射；它不是二进制布局发现器，也不是上述两个头文件的生成器。不要因文件名带regen就把当前结构交给它盲目重写。

修改布局时应：记录参考锚点 → 恢复语义成员 → 同步全部消费者 → 更新准确断言 → 运行相关独立测试和完整构建 → 用真实输入补足A/B行为测试。不要先改断言去迁就一个没有证据的结构。

布局修复实例见 [相机连续向量修复](../reports/fix21_camera_position.md)：依据原版初始化指令恢复连续 XYZ 及相邻状态，统一 PMM 和运行时消费者，并分别验证 x64/x86。仅移动一个 padding 来通过局部测试不足以证明布局正确。

## 构建与验证记录

Conan的基准profile为 `profiles/x64`。可从Windows开发者命令行运行 `conan install . --profile:all profiles/x64 --build=missing -s build_type=Release`，再按该次Conan生成的toolchain/preset配置CMake；输出路径应以实际生成结果为准。

完整构建、CTest和资源验证的最新实际运行结果由当轮修复报告记录。本文件不以一个固定的历史增量构建结果保证全新环境可构建，也不将局部测试通过称为MMD/MME全面等价。
