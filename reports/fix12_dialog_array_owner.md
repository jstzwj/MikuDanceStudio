# F12-05 对话框排序数组：先验证，再恢复索引与所有权

基线：`4833705`。范围：共享 `AccessoryOrderArray` 的四种对话框及其建表、移动、应用、释放。未提交；未修改其他代理负责的骨骼绑定字段。

## 验证结果

问题确实存在，但不能仅将 `free` 换成 `delete[]`。

1. `command_file_menu.cpp` 原先按 `4 * count` 分配，建表却经 `void**` 写附件指针，应用经 `AccessoryRecord**` 取对象；上移、下移仍按 `int32_t` 交换。x64 存在八字节写入四字节元素分配的越界；选中对象还错误读取第一个对象的首字节。
2. `misc_dialogs.cpp`、`command_view_menu.cpp` 使用 `new int32_t[]`；`dialog_select_ops.cpp` 与附件设置使用裸 `operator new`；各关闭分支和 `command_frame_edit.cpp` 却使用 `free`。C++ 分配/释放协议不一致。
3. 初始根据现有代码推测附件数组应持有对象指针。**随后 IDA 验证否定了这个假说**，没有以该假说作为最终实现：原版数组保存的始终是 32 位 slot 索引，不是对象指针。

本次直接重新读取用户指定 MMD 9.32 x64 二进制，函数证据保存在 [IDA 完整记录](fix12_dialog_array_ida.md)：

| 原版函数 | 验证内容 |
|---|---|
| `0x7FF7CB4772C0` | 初始化分配 `4 * count`；上移/下移使用 DWORD 交换；OK/取消通过 `operator delete[]` 释放 |
| `0x7FF7CB4A96E0` | 在 255 个附件 slot 中按 order 查找，向数组写入 DWORD **slot 号** |
| `0x7FF7CB4A9760` | 用数组 slot 号索引附件表，再更新 order/name；选中对象字段取数组首项的低字节 |

## 根因修复

新增 `DialogOrderArrays`，使用两个 `std::unique_ptr<std::int32_t[]>`：`modelIndices` 与 `accessoryIndices`。两个业务索引空间各有独立所有者；没有把原错误对象指针改成 `uintptr_t`，也没有保留 `void*&` 或类型重解释赋值。

附件建表恢复写入 slot 号；应用通过 `AccessorySlot(slot)` 找到实际对象；选中字段恢复为首个 slot 号。建表、交换、应用核心由生产对话框与测试共同调用。附件对象本身仍由场景拥有，排序数组只拥有索引存储。

| 文件 | 处理 |
|---|---|
| `include/mikudancestudio/dialog_order_arrays.hpp` | 两个 RAII 索引数组及附件建表、交换、应用核心 |
| `include/mikudancestudio/mmd_app.hpp` | 删除 `void*& AccessoryOrderArray()` 和 x64 `void*` 成员，新增拥有成员和 `DialogOrders()` |
| `include/mikudancestudio/app_layout.hpp` | x86 原 scratch 槽保留四字节布局占位，不再作为运行期所有者；后续字段不移动 |
| `src/window/command_file_menu.cpp` | 附件排序分配、建表、交换、命名/排序应用、选中 slot、OK/取消释放全部迁移 |
| `src/window/command_view_menu.cpp` | 模型显示/计算顺序的读写、交换、OK/取消迁移到模型索引 owner |
| `src/window/misc_dialogs.cpp` | 保留模型计算顺序 `count + 1` 分配及值初始化语义 |
| `src/window/dialog_select_ops.cpp` | 模型绑定列表重新分配、填充和查询迁移到模型索引 owner |
| `src/window/command_frame_edit.cpp` | 原有完成关闭分支释放对应模型索引 owner |

## 生命周期复核

- 全库搜索字段与 accessor 的所有生产者/消费者；旧 accessor、`state.accessoryOrderArray` 业务访问及对应 `free` 已不存在。旧名称仅作为布局占位与历史函数名保留。
- `WinMain` 使用 `new MMDApp()`；随后只赋值清空 `app->state`，不是 `memset(this)`。`app_ctor.cpp` 不覆盖新增非平凡成员，新增 owner 构造安全。
- 正常退出在 `ShutdownCleanup` 后 `delete victim`，隐式析构会释放仍存活的排序数组；不需要手工析构 RAII 成员。
- 初始化使用 `reset(new ...)`，重复打开/重新建表自动释放上一份数组。其他对话框原有 OK/取消清理位置保持，只改为 `reset()`。
- 绑定对话框 `result == 2` 的提前返回语义保持：该分支没有新增强制关闭/提交逻辑；仍存活的 scratch 由下一次替换或 MMDApp 析构释放。
- 保留模型计算顺序原有首项零初始化、`count + 1` 分配、其他数组不额外清零，以及既有索引协议。没有修改模型计算顺序自身的其他可疑行为。
- 对附件空列表时原应用路径读取首项的问题没有引入额外 UI 行为变更；本项测试覆盖有效非空排序协议，不把空列表行为宣称为已验证安全。

## 测试与边界

`tests/dialog_order_arrays_test.cpp` 实际调用生产核心 `BuildAccessoryOrderIndices`、`SwapAccessories`、`ApplyAccessoryOrderIndices`，不是只测试另写的算法。

- 两个真实对象位于稀疏 slot 7/254，验证末尾 slot 扫描、按原顺序建表和重排。
- 移动只修改工作数组，应用时才修改对象；命名跟随新顺序，选中对象是 slot 7，不能取对象首字节。
- 模型与附件索引独立，关闭一方不破坏另一方；覆盖重开、替换分配、模型值初始化。
- 使用真实分配对象；本次 x64 日志为 `object pointers above 32 bits: 1, 1`，确实覆盖两个高于 4 GiB 的对象；x86 为 `0, 0`。测试不解引用伪造地址。
- MSVC C++17 独立编译运行：x64 PASS、x86 PASS。测试只需项目 `include`，无额外链接库；由主线接入 CTest 和全量双架构构建。
- 未声称运行真实 Win32 对话框；未模拟列表控件消息、用户点击或 GUI 全生命周期。测试覆盖同一生产核心与存储协议。
