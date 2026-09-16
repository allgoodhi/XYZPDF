# XYZPDF

Windows 桌面 PDF 阅读器（第一阶段只读预览，编辑后续）。
**整体方案见 [docs/PLAN.md](docs/PLAN.md)，动手前先读完。**

## 参照项目

`H:\test\XYZip` 是同一作者的压缩软件，本项目的工具链、目录结构、构建脚本、测试方式、
打包流程全部与它一致。搬代码时**连注释一起搬** —— 那些注释记的是踩过的坑。

## 工具链（这台机器上的实际路径）

- Qt 6.11.2 MSVC 2022 64-bit：`E:\qt\6.11.2\msvc2022_64`（需要 Qt PDF 模块）
- Visual Studio Community：`E:\Microsoft Visual Studio\Community`，MSVC 14.51.36231
- 生成器 Ninja；cmake 用 `E:\qt\Tools\CMake_64\bin\cmake.exe`
- Python 3.14 在 PATH 上
- vcpkg：参照 `H:\test\XYZip\vcpkg.json` 与 `vcpkg_installed`（triplet `x64-windows`）

**不要删构建树的 CMakeCache.txt** —— 项目不带 CMakePresets，Qt 的位置只存在于缓存里。

## 约定

- 源码含中文字面量，MSVC 全目标加 `/utf-8`
- 版本号只写在 `project(... VERSION ...)`，其余三处（About / VERSIONINFO / 包 manifest）生成
- 所有用户可见字符串走 `tr()`
- 所有对话框走 `WindowDialogs`（窗口级模态）
- 临时文件走 `TempManager`；**永不写用户的 PDF 文件**（v1 只读）
- 测试：`python tools/build_all_tests.py`（配置+构建+运行，缺一不可）
- 编不过就明说"未编译验证"，不要报告成完成
