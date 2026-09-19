# Fix 11：MME 控制对象、矩阵与附件绘制

## 先验证

针对 audit10 的 R1/R2/R3，本次重新通过 IDA 核验：

- 原 MME `0x180059AA0` 的 +0xec 为 `abs(ExpGetPmd/AcsOrder)`；`0x18005B9E0` 按宿主 PMD、附件顺序重建名称树，并登记隐藏对象。
- 名称树插入 `0x18003BA30` 是 unique insert：同名、同 order 保留首项，与渲染列表 orderMap 的覆盖语义不同。
- `0x180057BC0` 按树序选取最后一个不大于 owner 的对象；无此前驱时取树末端；tree 插入使用 unsigned 比较，选择循环使用 signed 比较。
- `0x18005BA8D/97/A1/AB` 为单位矩阵四个对角写入，而不是四组颜色首元素。
- 本机 Microsoft `C:/Windows/System32/d3dx9_43.dll` 复制到 `build-x64/audit11-mme/` 后只读分析。`GXTri3Mesh<ushort>::DrawSubset`（0x18006AB3C）、`<uint>::DrawSubset`（0x18006C854）及 GetAttributeTable（0x18006CF38）证明优化表仅选择一项，无表则扫描连续面属性 run。**撤回 audit10 的“同 id 优化表全部 ranges 都画”建议**，已在旧报告追加纠正。

## 根治改动

- `ModelNameRegistry` 把原名称树恢复成有业务含义的类型。每帧刷新绘制顺序时显式重建；查找不再使用加载 vector 下标。删除对象会清除对应借用引用。
- `ObjectPlanState` 包含 `renderOrder`、宿主 index、visibility 与真正的 `D3DMATRIX world`。重置为单位阵；取消四个 color 数组及跨字段矩阵 reinterpret_cast。渲染快照直接复制矩阵成员。
- `PlanMeshSubset` 动态读取实际属性表大小，取消 128 上限；优化表精确实现 index 优先/第一匹配；无表读取 attribute buffer 产生连续 run；查询/锁失败返回 HRESULT，不画整个网格兜底。
- `DrawMeshSubsetPlan` 保留 D3DX 的实际错误传播差异：16 位索引未优化路径忽略中间 run 错误，仅末尾匹配 run 的 HRESULT 成为结果；32 位索引路径在首个 draw 失败处中止。
- 根据本机 June 2010 DirectX SDK `d3dx9mesh.h` 声明，补充类型化 ID3DXBaseMesh/ID3DXMesh 接口。桥接及附件 loader 的调用不再手工取 vtable 槽。
- 宿主 `fx_slots.hpp` 统一用现有类型化 ID3DXEffect 接口；设备 lost/reset、矩阵、技术/pass 调用由编译器处理 ABI。不同矩阵结构之间通过值复制，避免类型别名访问。

## 已运行验证

`tests/mme_regression.cpp` 独立编译及 `--runtime` 执行通过：

```text
cl /std:c++17 /EHsc /W4 /I third_party/mmeffect/include /I include tests/mme_regression.cpp ... /link d3d9.lib user32.lib
build-x64/audit11-mme/mme_regression.exe --runtime
MME regression tests passed
```

用例覆盖：加载顺序与绘制顺序相反、同 order 首项保留、无前驱回绕、大小写、空名称结果、删除及重建、unsigned/signed 边界、单位矩阵保持普通齐次点、129 项属性表、重复属性表的 index 优先/第一匹配、空范围、查询/锁失败、无表分离 run、16/32 位 draw 错误传播。

`--runtime` 使用真实 Microsoft D3DX9_43 + NULLREF 设备创建 129 面网格，验证手写 COM 声明的 GetNumFaces/Vertices/FVF/stride、Lock/UnlockAttributeBuffer、Set/GetAttributeTable 路径；并编译真实效果，验证 typed Set/GetMatrix、SetBool、SetFloatArray、SetTechnique、Begin/Pass/End、OnLost/OnReset。没有注入、IAT patch、vtable patch 或假 COM 槽表。

本分支未运行整个工程构建，主代理统一集成/构建。测试是局部语义与实际 ABI 验证，不是所有场景画面 A/B 等价证明。

## 剩余边界

- 原作者真实源码不可由二进制唯一确定；本次恢复可证实的业务数据结构。
- 真实 GPU 图像及 MME 复杂效果组合未完成全覆盖对照。
- DrawSubset 的极旧硬件 32 位索引软件拆三角 DrawPrimitiveUP 回退不属于当前宿主桥已覆盖路径；本次核实到该分支，未凭猜测加入。
- 原始错误分支与缺 D3DX/标准效果回退另行核验，见后续记录。

## R4 追加核验与明确的错误策略

本次 IDA 读取原 x64 MMD 导入表，确认 `0x7FF7CB548848` 等条目属于 `d3dx9_43`，因此缺失该 DLL 的原程序在进程加载期就不能启动。原 MMHack `0x180002DE0` 核验：Initialize 返回非零时，`0x180002E7D` 弹窗、`0x180002E83` 置 disabled、`0x1800039C4` 返回 0，不置 initFlag，也不调用真实 BeginScene；下一帧继续重试。这不是可作为正常成功状态使用的语义。

已修改：

- `InitD3D` 要求指定架构的运行库及所需符号完整，失败提示一次并返回 false，不再进入没有 D3DX 的半初始化渲染流程。
- bridge 记录 standard effect 缺失后，保留一次原错误提示，并返回 `D3DERR_NOTAVAILABLE` 使帧调用方跳过绘制。**与原版返回伪 S_OK、逐帧重弹模态框不同；为避免卡死有意保留的错误路径差异，不声称严格一致。**
- 不扩展修改“已有 standard effect、MME Initialize 因其它原因失败”的处理。
- 统一 `d3dx_dyn.hpp` 与效果引擎的 COM 声明：`Effect` 现在仅是 `ID3DXEffect` 的别名，不再维护第二套不完整 vtable 镜像。
- 修复运行库缓存误报：原 `Api::Load()` 在模块已加载但缺必要导出时，首次 false、第二次无条件 true；现在缓存实际 `available` 结果。

验证：`tests/incomplete_d3dx_runtime.cpp` 在 **build 临时子目录**编译为不含 D3DX 导出的测试 DLL，测试 exe 复制到该目录后运行 `--incomplete-runtime`，确认连续两次 Load 均 false。输出 `Incomplete D3DX runtime remains unavailable`。该 fixture 不覆盖系统 DLL、不写产品目录、不用于产品分发。随后真实 `--runtime` 全部回归再次通过。
