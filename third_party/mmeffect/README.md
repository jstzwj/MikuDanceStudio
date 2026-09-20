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
| cmake/D3dxRuntime.cmake（仓库根目录） | 从共享声明生成正常 D3DX 导入库，无运行时转发 DLL |
| res/ | MME 菜单、对话框及图标 |

## 验证边界

本轮修复名称注册顺序和回退、单位矩阵重置、Mesh subset 范围和错误传播，并统一类型化 Mesh/Effect 调用。细节及 IDA 证据见[修复报告](../../reports/fix11_mme.md)和[证据记录](../../reports/fix11_mme_ida_evidence.md)。

第十二轮恢复普通 D3DX 导入：缺少运行库或必要导出时由 Windows 加载器在程序入口前拒绝启动。缺少标准效果时恢复原 MMHack 的逐帧初始化重试、逐次提示及失败返回 S_OK（不调用真实 BeginScene），详情见[失败状态机修复](../../reports/fix12_effect_failure.md)。

旧硬件 32 位索引回退按原能力/缓冲容量条件直接调用类型化 D3DX DrawSubset，保留微软库的软件绘制行为，详见[回退修复](../../reports/fix12_mesh_fallback.md)。尚未完成旧显卡实测、真实 GPU 图像、复杂效果包及设备切换的全面对照。

CTest 包含局部语义、真实 D3DX NULLREF Mesh/Effect ABI、EXE 内 MMD/MME 资源共存、初始化重试、软件回退分流及普通导入检查；最终结果见[第十二轮汇总](../../reports/fix12_summary.md)。
