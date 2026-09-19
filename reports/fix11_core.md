# 第十一轮修复：模型初始化、布局和拥有型存储

日期：2026-09-19。基于本轮开始时的工作树，不覆盖既有修改；修复前快照由主线保存在 `build-x64/audit11-baseline`。本报告对应 `audit10_core.md` 的 C01—C07（物理诊断偏移由主线负责）。

## 实际修复

| 项目 | 改动 | 核验 |
|---|---|---|
| C01 指针被四元数初始化覆盖 | `ModelRecord` 使用完整 `StandardSkeletonPose`，17 个具名四元数；初始化仅写这些成员，不从 `m+64` 向外写 | 独立 x64 回归：失败装载前两个游标保持 null，全部17四元数为identity |
| C02 PMX头和pose trace重叠 | 真正声明 `poseTraceRecording` / `poseTraceBuffer`，PMX header自然排在指针后；原伪uint32槽删除 | x64指针8680、header8688独立断言；合法PMX头写入后trace仍null |
| C03 历史表错移/哨兵破坏toon路径 | 恢复 `SkeletonHistory`、`SkeletonJoints` 成员，690个历史样本和23个当前关节按语义初始化；去除143xx等地址表和冗余x86 memset | 初始化回归逐项验证历史sentinel、当前sentinel及toon文件名未修改 |
| C04 x86释放偏移 | 真正声明 `reservedMorphTable`，销毁通过该成员；PMX的UV计数/表、骨骼offset表、材料池全部成为成员 | x86/x64成员锚点断言；不再有FreeField(m,8732) |
| C05 分配释放不匹配 | 模型内部拥有型原始存储按其scalar operator new生产者配对scalar operator delete；模型失败/删除和PMMv2迁移路径同步修正 | 已核对PMD/PMX/VMD/undo生产者；实际生命周期测试已添加，等待主线完整工具链执行 |
| C06 名称越成员界 | name/nameEn真实声明50字节，删除伪padding；PMX转换容量取sizeof；PMD读取仍固定20字节 | 名称和后续成员锚点不变，PMD两个_read使用kPmdModelNameBytes=20 |
| C07 死RdPtr | 删除keyframe_common中两个仅复制4字节的未使用指针读取函数 | 仓库调用扫描无使用者；物理DIAG偏移另由主线处理 |

`skeleton_tracking.hpp` 的类型定义和应用层消费者由 `audit_app_ui` 负责；本包只负责将这些类型放进模型真实成员及初始化。标准姿态、当前关节、历史缓冲区都不再通过伪 PMM 字段或 `RawPad` 访问。`model.hpp` 的 pose-trace / morph accessors 访问真实成员；偏移保留在校验断言及逆向证据，而非业务取址。

仍未恢复语义的 `reservedMorphTable` 保留中性名称，依据原版析构 `0x7FF7CB4C9311` 的 +8816 槽确认所有权，不猜测元素类型。`pmxReserved` / `gap6` 等尚未恢复区域仍存在，未宣称整个 ModelRecord 已全部语义化。

## 分配/释放边界

- `pmd_load.cpp`、`pmx_load.cpp` 的模型表、文本、关键帧及查找表由 `operator new` 分配；`model_dispose.cpp` 统一用命名 `DeleteOwnedStorage` 配对释放，并置空。
- `bone_sort.cpp` 重建表、`bone_edit_undo.cpp` 快照、`model_keyframe_edit.cpp` 重分配、`vmd_load.cpp` 临时显示记录和 `src/io/vpd_file.cpp` 的导入撤销快照改正释放端。
- `add_model.cpp`、`DeleteModel`、`pmm_load_v2.cpp` 三个模型丢弃/迁移释放点与模型本体的 `operator new` 配对。
- 主线补充分派的 `src/window/physics_model_dialog.cpp` 同时修改两处 `delete[]` 和两处 `new[]`：物理编辑产生的新 rigid/joint 表也使用同一 raw-storage 分配族。只改生产/释放配对，不改变内容重建算法。
- `path_resolve.cpp` 的 `malloc` 临时文本及纹理缓存保持malloc/free；PMM迁移的NameMapping/IndexMapping保持malloc/free。没有全局机械替换所有free。
- PMM v1全局轨道的malloc与v2的operator new混用已通知主线；其统一由主线负责，不计作本包独立完成。
- 应用层的undo及pose trace生产/释放已与消费者agent协调统一operator new/delete；跨域最终构建和完整运行由主线验证。

本轮依旧保留模型loader所用的原始存储分配接口，没有谎称已把全部表迁移成vector/unique_ptr。相较整套容器迁移，先消除已证实的错误配对并明确语义成员，可以让后续拥有关系重构有可运行基线。

## 回归证据

新增 `tests/model_initialization_test.cpp`，使用二进制恢复的布局锚点读取结果，而非重复实现同一初始化算法。它覆盖六个相互独立的不变量。修复前六项全部失败，修复后六项全部通过：

- [修复前输出](fix11_evidence/model-init-before.txt)
- [修复后输出](fix11_evidence/model-init-after.txt)

本地独立编译命令（仓库根目录）：

```text
C:/cygwin64/bin/g++.exe -std=c++20 -D_M_X64 -fshort-wchar -I include tests/model_initialization_test.cpp src/model/model_init.cpp -o <temp>/mmd-fix11-init-test.exe
```

执行退出码从1变为0；真实头文件内全部x64布局静态断言通过。此独立探针用Cygwin g++模拟Windows短wchar布局，不代替MSVC完整构建。

新增 `tests/model_lifetime_test.cpp`：真实调用空模型初始化/销毁，随后覆盖部分已分配的游标、保留表、PMX文本和undo快照释放；需要链接 `model_init.cpp`、`model_dispose.cpp` 和Bullet，由主线接入CTest。本包没有为避免并行构建冲突而抢跑整个工程；本报告写入时该集成测试尚未执行，不能标记为通过。

本轮使用已有第十轮保存的原版初始化和销毁证据重新对照写入区域，见 `reports/audit10_evidence/mmd_x64_model_init.c`、`mmd_x64_morph_init.c`、`mmd_x64_model_dispose.c`。未重新声称全函数反编译或全部格式往返通过。

## 限制和后续

- 需要主线MSVC构建/CTest汇总，尤其校验应用消费者都不再引用被删除的localTransforms/matFloat/matColumn成员。
- PMD/PMX真实模型取消、失败后重试、PMM交叉打开以及Kinect录制仍需完整应用回归；独立测试证明对应内存不变量修复，不证明所有UI行为等价。
- PMX保留区域、原始模型字节句柄、部分void*资源以及全项目剩余拥有关系尚未全部完成源码级重构。
- 未更改文件格式版本接受范围、物理算法、关键帧算法或MME功能，不借本轮修复宣称MMD/MME全面一比一。

## D1 文档与维护链修复

恢复 [docs/X64_RECONSTRUCTION.md](../docs/X64_RECONSTRUCTION.md) 和 [docs/PORTING_STATUS.md](../docs/PORTING_STATUS.md)，明确原版SHA-256、x64基准、二进制证据层与运行时成员分离、人工维护布局流程、检查等级和未验证事项。模型/骨骼布局头不再声称由仓库里不存在的生成器生成。

`conanfile.py` 删除“只精确支持x86”和“已1:1恢复”的过时措辞；x64不再产生建议改x86的误导警告，x86构建提示其非行为基准。全局初始化、历史UI阶段TODO均明确为待核验证据，不用注释本身虚构缺陷或完成状态。

跨域释放链的追加只读核对确认：物理编辑scratch数组由calloc生产，free正确，应保留；PMM迁移name/index maps likewise malloc/free。已定位的模型本体退出/场景释放路径由主线统一operator delete。更广泛的RAII/容器恢复仍未声称完成。
