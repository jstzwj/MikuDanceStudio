# fix12：恢复必需 D3DX 运行库的进程加载边界

## 先验证

直接解析原版 PE 的普通导入目录，未依赖注释推断：

| 原版文件 | 普通导入 DLL | 导入数量 |
|---|---|---:|
| OpenMMD/MikuMikuDanceE_v932x64/MikuMikuDance.exe | d3dx9_43.dll | 27 |
| OpenMMD/MMEffect_v037x64_English/MMEffect_v037x64_English/MMEffect.dll | d3dx9_43.dll | 24 |
| OpenMMD/MikuMikuDanceE_v932/MikuMikuDance.exe | d3dx9_32.dll | 27 |
| OpenMMD/MMEffect_v037_English/MMEffect.dll | d3dx9_32.dll | 24 |

修复前，宿主 `d3dx_dyn.hpp`、toon loader、MME 自由函数转发层均使用
`LoadLibrary/GetProcAddress`。音频/姿态代码还单独解析两个四元数函数。
因此缺运行库时，原版在 Windows 加载阶段失败，而移植版已进入应用初始化，
后来才在 `InitD3D` 提示错误。这是已确认的行为差异。

## 根因修复

- `cmake/D3dxRuntime.cmake` 和 `scripts/gen_d3dx_imports.py` 从共享函数声明生成
  正常的 MSVC 导入库；最终 EXE 普通导入对应架构的原运行库。
- 生成器给 `LIB /DEF` 提供一个编译器产生的签名对象，让 x86 的 stdcall 符号
  正确映射至 DLL 的无修饰导出名。签名函数体不链接到产品，也不生成替代 DLL。
  不需要固定 SDK 路径，不修改任何系统 DLL，不使用 hook 或延迟导入。
- 删除 `third_party/mmeffect/src/d3dx9_dyn.cpp` 的动态转发实现。MME 直接调用
  正式导入函数；`D3DXMatrixIdentity` 按原 SDK 形式保留为头文件内联函数。
- 宿主旧 `Api` 调用面变成无状态的类型适配函数，内部直接调用正式导入。
  `Load()` 仅为现有调用点保留恒真兼容接口，没有加载状态、函数地址表或失败缓存。
- toon 和额外四元数函数使用同一接口，移除所有产品 D3DX 动态解析。
- VPD 导入、骨骼关键帧注册和姿态辅助函数不再依赖旧 `Api.module` 状态门控；
  所需函数由 Windows 加载器保证存在，不添加虚假的模块句柄兼容字段。
- 删除 `InitD3D` 的迟到运行库错误提示。缺少 DLL 或必需导出时，Windows 在
  CRT/WinMain 之前拒绝进程加载，应用没有机会弹这个框。
- 将 `D3DXQuaternionToAxisAngle` 的错误返回类型 `float*` 修正为原生 `void`；
  现有调用方本来不使用返回值。

这次仍保留宿主浮点数组与 D3DX 类型之间的适配，以免把加载边界修复扩大成
全部数学存储类型迁移。适配代码没有裸地址硬编码，未改变运算顺序。

## 回归和结果

独立输出目录 `build/fix12-d3dx/x64`、`build/fix12-d3dx/x86`，均通过：

1. `tests/d3dx_import_probe.cpp`：链接生成的导入库，实际调用本机 Microsoft
   D3DX 的平移、缩放、矩阵乘法、矩阵转置和四元数转轴角；运算结果正确。
2. `tests/check_d3dx_imports.py`：仅 Python 标准库解析 PE，验证对应架构 DLL
   是普通导入、没有 D3DX delay-load、没有 MMEffect/MMHack DLL 依赖。
3. `tests/check_d3dx_loader_failure.py`：在临时测试目录放置原有
   `tests/incomplete_d3dx_runtime.cpp` 构建的无 D3DX 导出 fixture，隐藏启动探针。
   x64/x86 均得到 `STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139)`，且 `main`
   入口标记文件没有产生。父进程设置错误模式，避免加载错误弹出模态框。

旧 `mme_regression --incomplete-runtime` 检查二次 `Api::Load()` 的测试已移除，
因为在正确的普通导入机制下，损坏运行库不应允许进入 `main` 再检查返回值。
新的子进程测试直接覆盖正确的失败边界。

可复现命令（完整构建后的路径以主 CMake 的目标输出为准）：

```powershell
python tests/check_d3dx_imports.py build-x64/build/Release/MikuMikuDanceE.exe
python tests/check_d3dx_imports.py build-x86/build/Release/MikuMikuDanceE.exe
```

完整宿主及 CTest 的最终结果由主线统一记录。未删除系统运行库来模拟真正缺 DLL；
缺 DLL 的入口前失败结论来自正常 PE 导入结构，损坏/缺导出路径有上述真实进程验证。
