# 内置 MMEffect

MME v0.37 效果引擎通过 MMEffect 静态库直接链接进 MikuMikuDanceE.exe。产品不依赖 MMEffect.dll、MMHack.dll 或代理 d3d9.dll，没有注入、IAT patch 或 vtable patch。系统 Direct3D 9 与对应架构的 Microsoft D3DX9 运行库仍是运行依赖。

## 集成和生命周期

- src/mme_host.cpp 提供宿主直接调用的 MmeHost* 接口；src/render/mme/mme_bridge.cpp 在实际渲染阶段调用。
- MMDExport.h 在 MIKUDANCESTUDIO_MME_EMBEDDED 下直接声明宿主函数，无 DLL 导入。宿主保留 37 个 Exp* 导出。
- WinMain 显式调用运行时初始化和关闭，管理 GDI+；效果引擎仍按渲染入口惰性初始化。
- 菜单和对话框编译进 EXE，使用宿主 HINSTANCE。资源 ID 见 include/mme_resources.h：菜单/对话框/图标组使用 501xx，图标子图从 50200 起，避免与原 MMD 资源冲突。控件和命令 ID 保持自身协议。
- scripts/gen_mme_icon.py 在构建时生成显式编号的图标资源，需要 Python 3。
- MMEffect.def 仅保留历史接口记录，不参与当前静态构建；没有 EXE/DLL 循环导入库生成步骤。

## 目录

| 目录 | 职责 |
|---|---|
| include/ | 宿主接口、效果接口、统一类型化 D3DX COM 声明、资源编号 |
| src/mmhack/ | 历史命名的状态查询层：对象注册、材质、光照矩阵；不实现 hook |
| src/mmeffect/ | SAS、pass 规划、材质绑定、EMM、UI、动画纹理 |
| src/mme_host.cpp | 宿主渲染和资源事件适配 |
| src/d3dx9_dyn.cpp | D3DX 自由函数运行时转发 |
| res/ | MME 菜单、对话框及图标 |

## 验证边界

本轮修复名称注册顺序和回退、单位矩阵重置、Mesh subset 范围和错误传播，并统一类型化 Mesh/Effect 调用。细节及 IDA 证据见[修复报告](../../reports/fix11_mme.md)和[证据记录](../../reports/fix11_mme_ida_evidence.md)。

缺少必要 D3DX 时初始化失败；缺少标准效果时提示一次并跳过绘制。后者有意区别于原版逐帧弹窗及伪成功返回，不能称作严格错误路径对齐。

CTest 包含局部语义、真实 D3DX NULLREF Mesh/Effect ABI，以及 EXE 内 MMD/MME 资源共存检查。尚未完成真实 GPU 图像、复杂效果包、设备切换及旧硬件软件索引回退的全面对照。
