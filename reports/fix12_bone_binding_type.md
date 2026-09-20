# F12-04：外部亲绑定表共享类型与副本生命周期

## 先验证

基线为 `4833705`。本轮重新打开原始 x64 MikuMikuDance.exe（IDA 会话 `cc212b7f`，imagebase `0x7FF7CB420000`），不是仅沿用旧代码注释。

原始文件：`C:/Users/jstzw/Documents/github/OpenMMD/MikuMikuDanceE_v932x64/MikuMikuDance.exe`。

| 原版指令位置 | 本轮取得的证据 |
|---|---|
| `0x7FF7CB4BA825` | 从当前模型 `+615544` 读取表项数。 |
| `0x7FF7CB4BA833..842` | 按 `20 * count` 分配对话框工作副本。 |
| `0x7FF7CB4BA86F..8AD` | 模型 `+615536` 到 app `+661104`，逐项完整复制五个 dword，然后步进 20 字节。不是第二种记录格式。 |
| `0x7FF7CB4BA995..9C9` | 副本首字段用于索引骨骼及匹配当前选中骨。 |
| `0x7FF7CB4BAC5C..C78` | `+12` 是目标模型槽；`-1` 对应无外部亲，`-2` 对应地面。 |
| `0x7FF7CB4BAD48..D51`、`0x7FF7CB4BADA0` | 选择无外部亲或地面时设置模型哨兵，并把 `+16` 骨索引清零。 |
| `0x7FF7CB4BADC4..DCF` | 选择模型后将实际模型槽写入记录 `+12`。 |
| `0x7FF7CB4BAF56` | `+16` 用于匹配目标骨列表的骨索引。 |

本地链路交叉核对：`InitBoneSortOrder` 生成表；`RefreshIkSelectors` 更新活动帧区间；seek/advance、骨变换及 PMM 读写消费同一记录；选择对话框复制、编辑、回写同一张表。因此旧 `SelectAttachRecord::reserved[2]` 实际保留的是 `windowStart/windowEnd`，不应再构造第二种别名类型。

确认存在的问题：

- `ModelRecord::boneOrderTable` 为 `void*`，`mdl::BoneOrder` 通过 `reinterpret_cast<BoneOrderEntry**>` 返回引用。
- 对话框重新声明 `SelectAttachRecord`，与模型记录形成不同 C++ 类型的别名访问。
- 对话框工作副本使用 `operator new` 分配却使用 `free` 释放。原版反编译呈现 `operator new` 与 `operator delete[]`，不能据此继续把 C 分配器混入重建源码。
- 删除模型仍以 `20 * k` 和 `rec[3]/rec[4]` 修改绑定，绕过已经确认的字段类型。

## 根因修复

- `include/mikudancestudio/bone_binding.hpp` 是唯一 `BoneOrderEntry` 定义：骨索引、活动帧区间、目标模型、目标骨。长度与字段偏移保留编译期断言。
- 模型及 app 的工作副本字段均改为 `BoneOrderEntry*`；`mdl::BoneOrder` 直接返回真实字段。保持现有 x86/x64 ABI，不改未知 padding。
- 模型表和工作副本分别使用类型化 `new[]/delete[]`，覆盖重建、销毁、对话框重新打开和正常结束等既有释放路径。
- 复核关闭链：`SelectNavDlgProc` 的普通取消命令实际 `EndDialog(..., 1)`，会走调用者的正常释放；但调用者仍存在 `result == 2` 的提前返回，不能把正常释放视为所有退出的保证。因此在真实 `ShutdownCleanup` 入口增加工作副本的 `delete[]` 与置空，覆盖残留副本，且与已执行的对话框释放安全衔接。`WinMain` 在销毁 app 前调用此清理。
- 删除 `SelectAttachRecord`。复制使用元素复制，编辑与渲染使用同一套字段名，帧区间随副本完整保留。
- 删除模型通过 `DetachBoneBindings` 清除匹配模型的外部亲；保持其他字段及其他模型、地面绑定不变。
- PMM 的相关链路原已通过 `mdl::BoneOrder` 消费类型化记录；日志函数接收 `void*` 只是地址输出，不进行别名读写，未为改动数量重写它。

没有修改 AccessoryOrderArray 的类型、分配或释放；该域由 F12-05 独立处理。

## 回归验证

新增 `tests/bone_binding_test.cpp`，链接 `src/model/bone_sort.cpp`，包含 `include/` 即可。用真实表生产函数验证：

1. root、可移动/IK 骨的顺序及 bone.slotIndex 回填。
2. 初始哨兵、零帧区间、目标骨初始化。
3. 对话框副本保留完整帧区间与地面哨兵，修改副本不串改模型，应用后回写。
4. 删除指定模型只清除匹配的目标模型/骨，不破坏帧区间或其他绑定。
5. 从非空表重建为空骨模型，仍正确保留 root。
6. 零项副本和空 detach 路径。

扩展既有 `model_lifetime_test.cpp`：为真实模型分配绑定表，再经生产 `ModelDispose` 销毁并验证置空。

独立 MSVC x64 编译与运行已通过（六条行为检查 PASS；输出位于 `build/fix12-bone-binding/`）。补充字段类型编译断言后的最终 x86/x64 全量构建及 CTest 交主线统一执行，避免并发占用共享构建目录。此子任务未修改 CMake、未提交。

本项不声称已经完成真实 UI 手动操作或 PMM 文件往返对照；也不改变原版对话框取消/退出的控制流。
这里的退出保证针对实际 app 正常清理链；未声称覆盖进程被强制终止，或整个应用尚未采用 RAII 的任意异常展开。
