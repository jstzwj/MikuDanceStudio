# 描边粗细对话框崩溃与原版核对

2026-10-25，针对 [issue #2](https://github.com/jstzwj/MikuDanceStudio/issues/2)。修复前基线为 `84621b5`；入口为加载模型后的 View → thickness of edge line。

## 根因与修复

编辑框安装回调时，移植代码使用了 `(LONG)(LONG_PTR)EdgeThicknessEditSubclassProc`。Windows x64 的 `LONG` 仍为 32 位，外层转换截掉函数地址的高 32 位，随后 `SetWindowLongPtrA` 收到的已经是无效地址。初始化紧接着通过 `EM_REPLACESEL` 填充文本，Windows 调用损坏的窗口过程，产生执行访问违例；无需用户输入就能崩溃。

回归程序在旧实现上确实以 `0xC0000005` 退出。调试捕获的一次实例中，模块基址为 `0x7FF70ABF0000`，实际回调为 `0x7FF70ABF2110`，执行访问违例地址却为 `0x0ABF2110`，与地址截断吻合。这一崩溃路径不依赖具体 PMX 数据。

修复使用 `reinterpret_cast<LONG_PTR>(EdgeThicknessEditSubclassProc)`，保存和安装均保持指针宽度。对话框移入 `src/window/edge_thickness_dialog.cpp`，菜单仍负责创建窗口。英文名称对话框四个编辑框存在同样的安装错误，一并修复。扫描 `src/window` 后没有剩余的 `(LONG)(LONG_PTR)` 回调安装。

## 原版证据

使用 IDA MCP 核对原版 `MikuMikuDanceE_v932x64/MikuMikuDance.exe`：

- SHA-256：`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`。
- 本次 IDA 映像基址：`0x7FF7CB420000`。以下使用 RVA，避免受加载地址影响。
- 描边对话框：RVA `0x56E50`；编辑框回调：RVA `0x57190`。
- RVA `0x57031` 将完整回调地址装入 `R8`，RVA `0x57040` 调用 `SetWindowLongPtrA`。原版同样直接替换 `GWLP_WNDPROC`，先安装回调，再填充文本。
- 英文名称对话框：RVA `0x8B780`；四处 `SetWindowLongPtrA` 调用为 `0x8BBA0`、`0x8BBC3`、`0x8BBE6`、`0x8BC09`，均传递完整指针。
- 同时核对 x86 原版：描边对话框 VA `0x44C7F0`，编辑框回调 VA `0x42E270`，getter/setter 为 `0x41E950` / `0x41E980`。x86 的 32 位窗口过程地址本身合法。

| 行为 | 原版及修复后实现 |
|---|---|
| 打开窗口 | 保存 EDIT 原过程并安装回调，随后填入 `%3.2f`；不修改场景 |
| 滑块 | 范围 0..200，刻度间隔 100；滚动立即按位置 / 100 更新模型及文本 |
| 编辑框提交 | 仅 `WM_KEYDOWN + VK_RETURN`；`GetWindowTextA` 容量 8，最多读取 7 个字符 |
| 数值解析 | `atof` 后转 float；模型值不强制限制在 0..2，滑块自行限位 |
| 当前模型 | 每次提交查询当前选择；相机模式不写模型，也不设置修改标志 |
| 修改标志 | 提交时设置 `messageSeen` 和 `SceneModified` |
| 关闭 | `IDCANCEL` 销毁窗口并清空保存的句柄；未按 Enter 的文本不自动提交 |
| 其他编辑消息 | 转发给保存的 EDIT 窗口过程 |

数值还须按架构区分。x64 原版在 RVA `0x5712F`、`0x57243` 使用 `mulss` 后 `cvttss2si`，即 float 乘 100 后截断；滑块除法在 RVA `0x56EED` 使用 `divss`。x86 原版使用双精度表达式。例：存储的 `0.57f` 在 x64 得到滑块位置 57，x86 得到 56。修复分别保留两种算序，避免把 x86 表达式直接用于 x64。

issue 中的 `SetWindowSubclass` 示例可以安全安装回调，但其中 `WM_CHAR` 提交、增大读取长度和关闭时提交文本均改变原版行为。本次保留原版的窗口过程替换方式及上述提交规则，只修复指针宽度和已确认的架构数值差异。

## 验证与边界

新增的 `edge_thickness_dialog_test` 加载构建产物中的英、日真实对话框资源，调用生产窗口过程及原生 EDIT/trackbar。覆盖初始化、滑块同步、Enter、字符数限制、负数、超范围、非法文本、模型切换、相机模式、修改标志、关闭及重新打开。旧实现会在初始化时崩溃，修复后两架构通过。原资源编辑框较窄且没有 `ES_AUTOHSCROLL`，初始化文本可能被控件按可见宽度截断；测试保留这次初始化，随后仅加宽测试控件以覆盖较长输入，产品资源不变。

另外执行原版 x64 未修改的对话框及编辑回调机器码，用 Unicorn 运行分支和 SSE 运算，Win32 控件操作桥接到原版资源创建的真实隐藏窗口。两种语言各取 7 个快照：初始化、滑块位置 137，以及 `0.57`、`1.23456789`、`-0.25`、`2.50`、`abc` 的 Enter 提交。与生产实现的 14 个快照在模型 float 位模式、滑块位置、两项修改标志及编辑框文本上全部一致。此核对观察回调安装地址；CRT 解析/格式化经过桥接，默认窗口过程转发不在机器码差分覆盖内。

使用用户提供的 `sour miku/miku/miku.pmx`（139692 个顶点、331 个骨骼、153 个 morph），在 x64 和 x86 上通过生产加载器、真实 HAL D3D9 设备和 Bullet 世界加载。每架构执行英、日各 5 轮打开、Enter、滑块、关闭，共 20 轮；同时创建两种语言的英文名称对话框并检查四个编辑框回调，均通过，模型和物理资源正常释放。此程序直接调用生产加载器与窗口过程，没有执行完整应用的菜单点击流程。

本地 Release 完整构建及 CTest：

| 架构 | 结果 |
|---|---|
| x64 | 55/55 通过，无跳过 |
| x86 | 54/54 通过，无跳过 |

包含真实 D3DX/GPU 用例及本地可用的 ray-mmd 用例。可单独复现对话框回归：

```powershell
ctest --test-dir build-x64/build -C Release -R '^edge_thickness_dialog$' --output-on-failure
ctest --test-dir build-x86/build -C Release -R '^edge_thickness_dialog$' --output-on-failure
```

完整应用的交互式菜单点击和图像对照尚未完成。本记录确认该崩溃的根因、修复及上述局部行为一致性，不据此宣称整个程序已与原版全面等价。调试器、机器码差分和实际模型探针的本地输出存放于忽略目录 `out/edge-dialog-issue2/`，外部模型不纳入仓库。
