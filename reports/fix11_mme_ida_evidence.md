# Fix11 补充 IDA 证据

Microsoft d3dx9_43.dll（本机 System32 原件的 build 临时副本）带符号反编译：

- `GXTri3Mesh<unsigned short,1,65535>::DrawSubset`: 0x18006AB3C。
- `GXTri3Mesh<unsigned int,0,4294967295>::DrawSubset`: 0x18006C854。
- 优化表检测及 index 优先：0x18006ABC0..0x18006AC0E / 0x18006CB6C..0x18006CBB3。
- 16 位索引未优化中间 draw `0x18006AC95` 后不读取 eax；末尾 draw `0x18006ACDC` 后 `mov ebx,eax`。无末尾匹配 run 时返回初始 S_OK。
- 32 位索引中间 draw `0x18006CC70` 后 `test eax/eax` 失败跳出；末尾 draw返回 `v4`。
- 无优化表的 minVertexIndex=0、numVertices=mesh总顶点数、startIndex=3*(currentFace-runLength)，不是每段重新估计顶点包围范围。
- GetAttributeTable: 0x18006CF38，写真实count并复制20*count字节；未优化时count为0。

原 MMHack 初始化失败末尾（0x180002DE0）：

```cpp
  v3 = "Initialize Error"; /*0x180002e76*/
  MessageBoxA(hDlg, v3, "MikuMikuEffect", 0x10u); /*0x180002e7d*/
  byte_18006E794 = 1; /*0x180002e83*/
  return 0; /*0x1800039c4*/
}
```

原 MME 名称树 unique insert: `0x18003BA30`，相等key走 `0x18003BB2A j_free(newNode)`，返回现有node并把inserted字节置0，不覆盖value。

名称树排序字段仍以 audit10 的 0x180059AD6/0x180059B35 与 0x180057BC0 证据为准。
