# 编辑器 RHI 视口

主编辑器的 **Renderer → Direct3D 9 / Direct3D 11 / Direct3D 12 / Vulkan**
选择实际显示视口的后端。默认 DX9；未编译的后端置灰。选择保存在可执行文件旁的
`Data/renderer.ini`，不写入 PMM：

```ini
[Renderer]
Backend=vulkan
```

可选值为 `dx9`、`dx11`、`dx12`、`vulkan`。恢复设置时，未编译的后端回退到 DX9。
`Renderer → Recreate device` 重建当前 RHI 设备；菜单末行报告当前实际使用的后端和
兼容回退原因。`Renderer Preview` 仍是独立的旧预览窗口入口，其限制见
[预览控制器](RHI_PREVIEW_CONTROLLER.md)。

## 数据与呈现

`MMDApp` 持有 `EditorViewport`。原来的编辑器继续负责输入、动画、物理和 CPU 蒙皮；
`EditorSceneProducer` 借用当前 `ModelRecord` 及其已发布的 CPU 几何，读取本帧相机、
灯光、材质和附件位置，生成共享 `RenderFrame`。不重新加载一份模型或运行第二套姿态求解。
CPU 材质变化逐帧读取，纹理按路径签名准备并复用；GPU 对象由 `SceneRenderer` 管理。

```text
原编辑器的实时状态与 CPU 几何
  → 自阴影 Pass
  → 地面、附件、投影阴影、PMD／PMX、轮廓
  → 共享复制后处理 → finalColor
  → screen.bmp 捕获（操作轴及 HUD 之前）
  → 操作轴、文本、骨骼线与精灵
  → 当前后端的交换链
```

内嵌子窗口对齐编辑器视口，命中测试透明、不获取输入焦点。原主窗口处理鼠标、键盘和
骨骼选择；界面控件保持在视口上方。浮动视口使用浮动窗口作为父窗口。
正常显示没有 CPU 回读；回读只由测试显式请求。三个新后端默认使用可移植三线性采样。

这一步仍需要编辑器原有的 DX9 设备与绘制准备过程。部分 CPU 覆盖层在原来的顶点生产
阶段、DX9 缓冲解锁前复制，未读取 GPU 缓冲；原有 DX9 场景也仍然执行，便于保留尚未
迁移功能。因此这不是移除 DX9 依赖，也不承诺这一阶段降低 GPU 开销。无 DX9 启动的
独立宿主是另一条入口，见 [独立启动](RHI_DX9_INDEPENDENT_STARTUP.md)。

## 范围与兼容回退

| 功能 | 主编辑器 RHI 视口 |
| --- | --- |
| 实时 PMD／PMX、材质编辑、相机与灯光 | 使用当前编辑器 CPU 数据 |
| 模型自阴影模式 1／2、模型投影阴影、轮廓 | 共享 Pass 与内置着色器 |
| 附件 | 当前 CPU X 导入器支持的结构；使用原有挂接变换及前后绘制分组 |
| screen.bmp | 复用共享历史与捕获模式，在操作轴和 HUD 前捕获 |
| 文本、精灵、骨骼线、操作轴 | CPU 覆盖层；操作轴使用内嵌 X 资源与原有变换 |
| 缩放、零尺寸恢复、浮动视口、设备重建 | 宿主与共享控制器管理 |
| 已分配且启用的 MME 效果 | 自动显示原有 DX9 画面 |
| 图片／AVI 背景、线框、立体显示、物理调试、深度摄像头合成 | 自动显示原有 DX9 画面 |
| 带阴影的附件、模型特殊显示覆盖、CPU 导入不支持的附件 | 自动显示原有 DX9 画面 |
| 录制、全屏 | 保留原有 DX9 路径 |

回退保留用户选择，隐藏 RHI 子窗口并恢复 DX9 呈现。条件解除后自动恢复 RHI。
设备创建或绘制失败也显示 DX9，菜单提供状态；使用 `Recreate device` 或重新选择后端重试。
各新后端是单采样视口，本次未建立全部编辑辅助图形相对 DX9 的逐像素一致性门禁。
MME 的后端无关效果描述与着色器迁移仍是后续工作。

## 切换与资源生命周期

跨 API 切换先创建新的子窗口、设备和交换链；创建失败保留原设备及选项。成功后停止旧
设备提交，等待并释放 GPU 资源、退役交换链，再将 `SceneRenderer` 绑定新设备。
CPU 资产版本及原场景身份保留，旧准备帧因设备身份变化失效，下一帧重新上传。
跨帧 `screen.bmp` 历史随设备重置，不引用已销毁的 GPU 图像。

同后端设备替换在原表面重建；失败时尝试恢复此前配置。缩放检查交换链状态并按新尺寸
重建，零尺寸暂停绘制。浮动窗口换父窗口时重建表面。新建项目／载入 PMM 前卸载场景，
退出时先关闭视口，避免 GPU 资源或借用的模型记录跨越场景销毁。

## 无桌面测试

新增测试的所有顶层父窗口从未显示；不调用激活、最小化／恢复窗口或桌面自动化。
子窗口仅在隐藏父窗口中呈现。日常回归可运行：

```powershell
cmake --build build-x64/build --config Release --target MikuDanceStudio editor_viewport_test editor_scene_import_test
ctest --test-dir build-x64/build -C Release -R '^editor_(viewport|scene)_(dx11|dx12|vulkan)$' --output-on-failure
```

`editor_viewport_*` 覆盖子窗口输入透明、实时材质变化、操作轴、自阴影、重复提交、缩放、
零尺寸恢复、浮动视口、卸载、设置保存／恢复、跨后端重建与失败恢复。
`editor_scene_*` 使用公开生成的 PMD／PMX，通过实际 CPU 导入器加载为编辑器记录，
验证两种自阴影、严格重复输出、附件前后分组、`screen.bmp` 历史与设备重建，以及
切换后的场景身份。

外部素材只读且不纳入仓库，配置后运行独立的真实模型用例；未配置返回 CTest skip 77：

```powershell
$env:MDS_SELF_SHADOW_PMD = 'C:\assets\model.pmd'
$env:MDS_SELF_SHADOW_PMX = 'C:\assets\model.pmx'
ctest --test-dir build-x64/build -C Release -R '^editor_scene_(pmd|pmx)_' --output-on-failure
```

构建条件为 `MIKUDANCESTUDIO_RHI_PREVIEW=ON` 且至少启用一个 DX11／DX12／Vulkan
后端；独立于诊断构建。诊断构建也会编译 CPU 渲染功能。关闭新后端及诊断时，主应用
使用原有 DX9 路径。

2026-10-29 验证：x64 相关视口／CPU 导入／后处理／屏幕捕获／隐藏呈现回归 38/38，
x86 视口与 CPU 导入回归 9/9；真实 PMD／PMX 的三后端检查 6/6，独立资源契约 16/16。
扩展的附件及 `screen.bmp` 编辑器导入用例在 x64／x86 各通过三后端。
关闭诊断的三后端主应用、以及关闭诊断和新渲染功能的主应用均编译链接通过。
原生 X 网格严格比较暴露了导入器默认矩阵未清零的问题，现已显式建立单位矩阵；
x64／x86 的顶点、法线、UV、材质比较通过，未放宽比较标准。
