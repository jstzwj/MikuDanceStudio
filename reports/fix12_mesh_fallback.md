# Fix12：32 位附件网格的软件绘制回退

## 先验证问题

基线 `4833705` 的 `DrawAccessorySubset` 在效果激活时始终转为
`DrawIndexedPrimitive`，没有执行 D3DX 为超出设备能力的 32 位网格准备的
`DrawPrimitiveUP` 路径。该缺口真实存在。

本轮独立使用 IDA 的 idapro/Hex-Rays 打开临时副本，分析对象：

- Microsoft System32 `d3dx9_43.dll`，SHA256
  `84b900dbd7fa978d6e0caee26fc54f2f61d92c9c75d10b35f00e3e82cd1d67b4`。
- 原 MME 0.37 的 `MMHack.dll`，SHA256
  `e450b25e5cd2bcf125ca4b8eb9e57f858ec3ca379633c7d25f9dd40e864b390d`。
- 本机反编译输出留在 `build-x64/fix12-mesh/`，不将反编译器输出当成产品源码。

### 已确认的原版条件与行为

`GXTri3Mesh<uint>::Resize`（`0x18006D8B4`）获取 `D3DCAPS9`，满足任意一项即设置软件回退标志：

1. `MaxVertexIndex < vertexCapacity`。
2. `MaxVertexIndex <= 65535`，即使实际只有一个三角形也进入回退。
3. `MaxPrimitiveCount < faceCapacity`。

这里确实比较容量，不是本次 subset 的顶点数/面数，也不是 `capacity - 1`。
同一函数分配 VB 的字节数为 `stride * requestedVertices`，成功后将
`vertexCapacity` 设为 requestedVertices；IB 字节数为 `12 * requestedFaces`，
成功后将 `faceCapacity` 设为 requestedFaces。没有额外的 D3DX 容量 padding；
因此宿主附件使用的自有缓冲区可通过类型化 `GetDesc().Size` 恢复这两个容量。
宿主没有运行时扩缩附件网格或跨设备共享 VB 的路径。本次不声称支持任意外来
D3DX 派生网格、跨设备克隆时继承的内部标志等没有在宿主出现的情况。

`GXTri3Mesh<uint>::DrawSubset`（`0x18006C854`）软件分支：

- 设置 mesh 的顶点声明。
- 优化属性表只取第一个匹配项；这一点与正常路径的 index 优先不同。
- VB/IB 使用 `D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK` 锁定。
- 按 32 位索引拷贝每个三角形的三个完整顶点，逐三角调用
  `DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, ..., stride)`。
- 首次失败即退出，保留 HRESULT；退出时解锁 VB/IB，不恢复 stream 状态。
- 未优化表按面属性筛选；优化表按所选项的面区间处理。

原 MMHack `0x180002440`（device slot 83）仅转发真实设备的
`DrawPrimitiveUP`，无 MME 回调，也没有前后 object/material 状态变更。
这一点通过独立 IDA 反编译再次确认，并与已有 wrapper 恢复源码一致。

## 根本修复

新增 `DrawMeshSubsetWithFallback`：在必要的 32 位能力条件下，直接调用
类型化 `ID3DXMesh::DrawSubset`。由同一个 Microsoft D3DX 运行库执行其真实软件回退，
自然保留锁、逐三角复制、首匹配规则、HRESULT 和设备状态副作用。

正常路径继续进入现有 MME indexed draw；16 位网格不查询新增能力条件。
`DrawAccessorySubset` 的外层对象、材质以及效果绑定流程保持原调用顺序。
未实现 DLL hook，未读取 D3DX 对象私有字段，未手工调用 vtable，也没有复制
一套易与微软实现偏离的软件光栅提交算法。

## 验证及边界

新增 `tests/mesh_fallback_test.cpp`，覆盖：

- 65535/65536 能力边界，即使仅三个顶点也能正确分流。
- 顶点容量、面容量分别超过能力及恰好相等。
- 原 DrawSubset 接收正确 attribute，原始失败 HRESULT 原样返回且不改走 indexed draw。
- 正常 MME 路由保持；16 位网格跳过能力查询。
- 能力/缓冲区描述查询失败的传播。

独立 MSVC x64 `/std:c++17 /EHsc /W4` 编译并执行通过，输出
`Mesh fallback dispatch tests passed`。主线负责接入 CMake、统一双架构构建和 CTest。

这是选择条件和真实 API 转发的回归测试，不是假装已经在旧显卡实测；本轮没有
旧硬件渲染截图，也没有通过修改真实设备 vtable/私有字段强行制造旧硬件。
实际软件三角提交由已核验的原版 D3DX 完成。
