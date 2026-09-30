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
- vcpkg 在 `H:\test\vcpkg\vcpkg.exe`；本仓库自己带 `vcpkg.json`（第一个原生依赖是 qpdf，M5 用）。
  **新克隆先在仓库根跑 `vcpkg.exe install --triplet x64-windows`**，否则 CMake 配置就停住（它不
  用 vcpkg 的 toolchain file，路径写死在 `vcpkg_installed/`，那个目录已 gitignore）。目录布局、
  打包与测试方式照 `H:\test\XYZip`。

**不要删构建树的 CMakeCache.txt** —— 项目不带 CMakePresets，Qt 的位置只存在于缓存里。

## 约定

- 源码含中文字面量，MSVC 全目标加 `/utf-8`
- 版本号只写在 `project(... VERSION ...)`，其余三处（About / VERSIONINFO / 包 manifest）生成
- 所有用户可见字符串走 `tr()`
- 所有对话框走 `WindowDialogs`（窗口级模态）
- 临时文件走 `TempManager`；**永不写用户的 PDF 文件**（v1 只读）
- 测试：`python tools/build_all_tests.py`（配置+构建+运行，缺一不可；它还会跑 `tools/check_translations.py`
  对一遍目录——代码里改了用户可见的话，就靠这一步发现目录没跟上）
- 目录也可以重新生成：`cmake --build build-local-release --target update_translations` 是安全的
  （2026-09-27 实测：42 条 numerus / 84 个 `<numerusform>` 一字不动，唯一变了的那一行只是 `'` 被转义成
  `&apos;`）。但要知道 **`i18n_smoke` 的探针在 lupdate 眼里就是调用点** —— 产品不再用的串只要还挂在探针表上
  就不会被 `-no-obsolete` 删掉，删一行探针倒可能带走一条串（所以"目录 == 代码"里，探针也算代码）
- 小结是 `line += tr("…")` 一句一句加出来的，**段落换行写在可翻译字面量里面**（`tr("\n\n…")`）；英文形
  少了那两个换行，屏幕上就是两句话焊在一起。目录里这类形状由 `check_translations.py` 的形状规则管着
  （集合对不上、numerus 掉形、两形相同、单数写成多数、段落换行丢）—— 这些都看不出在 (context, source) 上
  是"完全匹配"的
- 真窗口那一遍：`powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/tools/gui-probe.ps1
  -Mode inventory|menudump|exportstatus|mergedonor|splitsagain|saverename|langswitch|formpage|uacache`（九种模式
  都当场跑通过；脚本注释里就是踩过的坑）。装的那份要英文界面走 `-Mode langswitch -Installed`（它的偏好没有
  外面的读路，读数在应用自己的设置框里）。`build-local-release/gui-verify/` 下那些是历次草稿，在
  gitignore 的构建树里、丢了找不回来 —— 量出可用的新路由就收进 `tools/gui-probe.ps1`
- 带表单字段的样本不用等真文件：`XYZPDF_FORM_SAMPLE_DIR=<目录> build-local-release/tests/xyzpdf_documenteditor_smoke.exe`
  现造 plain / field-flat / field-spread / field-dangling / field-xfa / field-appearance / field-directive
  （目录名里带 `fixtures` 会被拒绝并算失败）。
  "这台机器上没有真带字段的 PDF"从 §54 起不再成立
- 编不过就明说"未编译验证"，不要报告成完成
