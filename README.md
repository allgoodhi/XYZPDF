# XYZPDF

Windows 桌面 PDF 阅读器，带常用的页面编辑、表单填写、签名验证和格式转换。
渲染引擎是 [PDFium](https://pdfium.googlesource.com/pdfium/)，界面是 Qt 6，中文 / 英文两套界面。

以 [MIT 许可证](LICENSE) 开源。

## 下载与安装

到 [Releases](https://github.com/allgoodhi/XYZPDF/releases) 下载 `XYZPDF-<版本>-setup.exe`，双击安装。

- **不需要管理员权限**：装在 `%LOCALAPPDATA%\Programs\XYZPDF`，只写当前用户的注册表。
- 需要 Windows 10 1809 或更新的 64 位 Windows。
- 安装包**没有代码签名**，第一次运行时 SmartScreen 会提示"未知发布者"：点「更多信息」→「仍要运行」。
  每个 Release 附带安装包的 SHA-256，可以先核对：
  `Get-FileHash XYZPDF-<版本>-setup.exe -Algorithm SHA256`
- 安装时可选：
  - 在「打开方式」和右键菜单里加入 XYZPDF（默认勾选）。要让双击 PDF 用 XYZPDF 打开，请在
    Windows 的「设置 → 应用 → 默认应用」里自己选 —— Windows 不允许程序替你改默认程序。
  - 在资源管理器的预览窗格里预览 PDF（XYZPDF 是 PDF 默认程序时生效）。
- 卸载：「设置 → 应用 → 已安装的应用」里找 XYZPDF。卸载会撤掉它写过的注册表项；个人设置
  （语言、最近文件等）留在 `HKEY_CURRENT_USER\Software\xyzpdf`，要彻底清掉可以手动删。

## 能做什么

**阅读**
- 连续滚动 / 单页 / 双页 / 书籍模式，缩放、旋转、全屏、演示、夜间阅读，浅色 / 深色主题
- 多标签、多窗口（可合并），单实例：从资源管理器再打开的文件进同一个窗口；拖放打开
- 导航侧栏：缩略图、书签、数字签名
- 全文查找、选择并复制文字
- 框选识别文字（Windows 自带的离线 OCR，不联网）
- 选中文字翻译（需要在设置里填自己的翻译服务密钥，密钥存进 Windows 凭据管理器）

**编辑**（只有「保存 / 另存为」会写文件，写之前先写临时文件并验证，失败时原文件不变）
- 删除 / 旋转 / 移动页面，把别的文件合并进来，拆分文件
- 加密或去掉口令，加水印，编辑书签，编辑文档属性
- 填写表单（AcroForm）；已签名的文档只改字段时按增量保存，不破坏原签名
- 撤销 / 重做

**数字签名**
- 验证签名：内容有没有被改过、证书链、时间戳（RFC 3161）、吊销（默认只查本机缓存，可在设置里允许联网）

**转换与输出**
- 导出为 Word（.docx，按原版式排出来，保留换行、字体、图片、表格、两栏）
- 导出为 Excel（.xlsx，识别表格）、PowerPoint（.pptx，每页一张幻灯片）
- 扫描件转成可搜索的 PDF（OCR 后加不可见文字层）
- 打印、导出为图片

**资源管理器**
- 「打开方式」与右键菜单「用 XYZPDF 打开」、预览窗格

## 从源码构建

需要：Visual Studio 2022（MSVC x64）、Qt 6.7 以上（MSVC 2022 64-bit）、CMake + Ninja、Python 3、
[vcpkg](https://github.com/microsoft/vcpkg)。

```powershell
# 1. 原生依赖（qpdf 等），在仓库根目录
vcpkg install --triplet x64-windows

# 2. PDFium（bblanchon/pdfium-binaries 的 chromium/8111，脚本会校验 sha256）
python xyzpdf\tools\fetch_pdfium.py --download

# 3. 配置 + 构建 + 跑全部测试
cd xyzpdf
python tools\build_all_tests.py
```

脚本自己找 cmake、`vcvars64.bat` 和 Qt（也可以用 `--qt` 指定）。生成器固定为 Ninja，构建目录默认
`build-local-release`；构建后自动 `windeployqt`，`build-local-release\xyzpdf.exe` 可以直接双击运行。
**不要删构建树里的 `CMakeCache.txt`**：项目不带 CMakePresets，Qt 的位置只存在缓存里。

### 出安装包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf\packaging\build-installer.ps1
```

要先装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。产物是
`xyzpdf\build-local-release\installer\XYZPDF-<版本>-setup.exe` 和它的 `.sha256`。
载荷与 MSIX 那条路（`packaging/build-package.ps1`）同一份暂存：去掉 Qt 用不上的 29 MB，
带上 VC++ 运行库和 QPDF 运行库。

## 测试

`xyzpdf/tests/*_smoke.cpp`，每个模块一个，全部经 `tools/build_all_tests.py` 跑（**配置 + 构建 + 运行**，
只跑不构建会拿陈旧的 exe 骗自己）。它还会用 `tools/check_translations.py` 核对英文目录与代码。
测试用的 PDF 由 `python tools/make_fixtures.py` 生成，已提交在 `tests/fixtures/`。

真窗口上的验收用 `xyzpdf/tools/gui-probe.ps1`（UI Automation 驱动，三十多种模式），用法见 [AGENTS.md](AGENTS.md)。

## 目录

```
xyzpdf/
  CMakeLists.txt      唯一的构建脚本：版本号只写在这里的 project(... VERSION ...)
  main.cpp            语言、主题、单实例、命令行、安装器的两个开关
  mainwindow.*        窗口级：菜单、工具栏、标签、最近文件、设置
  documenttab.*       文档级：一个标签 = 文档 + 渲染 + 视图 + 编辑会话
  src/document/       PDFium 后端、表单、签名验证、书签
  src/edit/           基于 qpdf 的结构修改与保存
  src/export/         Word / Excel / PowerPoint / 可搜索的 PDF
  src/render/ src/view/  异步渲染、按字节计上限的缓存、自绘页面视图
  src/services/       搜索、导出、打印、识别、翻译等后台服务
  src/windows/        资源管理器集成：预览窗格、缩略图、右键菜单
  packaging/          Inno Setup 安装包、MSIX（未上架）
  tools/              构建与测试脚本、gui-probe、pdfprobe
  tests/              *_smoke.cpp 与 fixtures/
  Resources/          图标、第三方许可证、翻译
docs/                 方案、各批交接文档与验收日志
```

## 开发文档

- [docs/PLAN.md](docs/PLAN.md)：整体方案
- [AGENTS.md](AGENTS.md)：实现约定与踩过的坑（写给参与开发的人和 AI 助手）
- `docs/handoff-*.md`：每一批功能的交接说明；`docs/store-submission.md`：逐节的验收日志

## 许可

本项目代码以 [MIT](LICENSE) 许可证发布。随程序分发的第三方组件（Qt 为 LGPL-3.0，PDFium、qpdf、
zlib、libjpeg 等）各自的许可证在 `xyzpdf/Resources/licenses/`，程序里「帮助 → 第三方开源许可」可以查看。
