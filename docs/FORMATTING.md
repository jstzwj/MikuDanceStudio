# 源码格式

根目录的 `.clang-format` 统一使用四空格缩进、100 列和同行大括号。保留 include 顺序、注释内容和字符串字面量，避免改变 Windows 头文件依赖及原版文本字节。`.gitattributes` 将这些源码固定为 LF，防止 Windows 的 Git 换行转换导致格式检查反复报错。

VS Code 使用 Microsoft C/C++ 扩展（`ms-vscode.cpptools`）的 clang-format，C/C++ 文件保存时自动格式化。`.inc` 关联为 C++。仓库只共享 `.vscode/settings.json` 和扩展推荐，其他个人 IDE 文件继续忽略。

在项目根目录执行：

```powershell
# Format sources under include/mikudancestudio and src (including .inc)
python scripts/format_sources.py

# Check only; return a nonzero exit code on formatting violations
python scripts/format_sources.py --check
```

脚本优先使用 VS Code C/C++ 扩展自带的 clang-format，再查找 PATH 和 Visual Studio 的 C++ Clang 工具。配置兼容 clang-format 19 及以上版本；需要不同机器输出完全一致时，使用相同版本，也可以通过 `--clang-format PATH` 指定程序。本轮使用扩展自带的 clang-format 23.1.0。

处理范围仅为这两个目录中的 C/C++ 头文件、实现文件和 `.inc`；第三方目录、资源文件及 `.def` 导出表不在处理范围内。
