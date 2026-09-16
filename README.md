# XYZPDF

Windows 桌面 PDF 阅读器。工具链、目录结构、构建脚本和测试方式与同一作者的
[XYZip](H:/test/XYZip) 一致。

**整体方案在 [docs/PLAN.md](docs/PLAN.md)，动手前请先读完。** 实现约定在
[CLAUDE.md](CLAUDE.md)。

## 现在到哪一步了

按方案 [§4 里程碑](docs/PLAN.md) 的划分，**M0（骨架）已完成**：

- 双击 `.pdf` / `xyzpdf.exe <file>` 能打开，窗口标题是文件名，关掉不崩
- **单实例网关**：第二次启动把文件通过命名管道交给第一个实例，开成新标签页
- `MainWindow : DocumentTab = 1 : N` 的类划分已经成立，标签栏在只有一个标签时自动隐藏
- 视图是自己写的 `PageView`（不用 `QPdfView`）：连续滚动、页面间距、占位页、
  Ctrl+滚轮**以光标为锚**的缩放、适应宽度/适应整页、旋转、键盘翻页
- 渲染走 `PageRenderer`（Qt PDF 的多线程模式）不上 UI 线程，回来的图按**代次令牌**
  判断是否还该上屏；渲染结果按**像素字节数**做 LRU 缓存（默认 256 MB，可配）
- **内存有上限**：视图只留"看得见的页 + 预取额度内的邻居"，且预取按**字节**给额，
  不按屏数；比视口大得多的页（800% 一张 A4 是 288 MB）只渲染屏幕上那一条
  （`setScaledClipRect`），实测 2500×1400 的一条是 12.7 MB
- **拖放打开**：从资源管理器把 PDF 拖进窗口任意位置即可打开（窗口是拖放目标，标签栏、
  页面、空状态上都行）；已经打开的文件会把它的标签提到前面而不是开第二个；
  拖进来的动作一律按「复制」处理——只允许移动的拖放会被直接拒绝，绝不接受移动，
  免得把用户的原文件搬走
- **文字选中**：鼠标拖选、双击选词、Shift 扩展、Ctrl+A 全选、Ctrl+C 复制，
  选中处有蓝色底色；跨页选中会按页拆成多段并在复制时用换行分隔；
  指针移到文字行上会变成 I 形光标（由 `Document::hasTextAt()` 判定，页边距和纯图片页不算）
- **框选识别（OCR）**：在扫描件、纯图片页等没有文字层的地方直接拖动拉出矩形框，
  松开后识别框内像素里的文字（嵌入图片里的字也能识别），结果弹出在独立窗口中可一键复制；
  普通页面上按住 Alt 拖动同样可以框选。识别用 Windows 自带的离线 OCR
  （`src/ocr/ocrengine.cpp`），不联网、不装额外组件，识别语言跟随系统语言包；
  没有语言包的机器会在窗口里如实说明
- 主题（浅色/深色）、语言、最近文件、设置、关于、第三方许可
- 中文/英文两套界面（英文目录已完整翻译）

还没做，且都在方案里排了队的：侧栏（缩略图/书签）、文本选择与搜索、双页/书籍模式、
打印与导出、标签页的完整交互（拖拽重排/拖出成新窗口）、资源管理器右键菜单与预览窗格、
MSIX 打包。`packaging/` 目前只有两个清单占位，打包脚本排在 M4。

## 构建

```powershell
# 一次配置 + 构建 + 跑全部测试
python tools\build_all_tests.py

# 只要构建
python tools\build_all_tests.py --build-only
```

脚本自己会去找 cmake、vcvars64.bat 和这台机器上的 Qt（也可以用 `--qt` 指定）。
生成器固定为 Ninja，构建目录默认 `build-local-release`。

构建后会**自动 `windeployqt`**，把 Qt 的 DLL 和插件部署到 exe 旁边，所以
`build-local-release\xyzpdf.exe` 可以直接双击运行，不需要把 Qt 的 bin 加进 PATH。
测试程序不带这份部署（它们在 `build-local-release\tests\`），由脚本运行时给它们
加上 Qt 的 bin —— 这也是为什么测试要经由脚本跑，而不是直接双击 exe。

**不要删构建树里的 `CMakeCache.txt`**：本项目不带 CMakePresets，Qt 的位置只存在于
缓存里。要换 Qt，重新配置一个新目录，或删掉整个构建目录后让脚本重新找。

## 测试

`tests/*_smoke.cpp`，每个模块一个，用 `tools/build_all_tests.py` 一把梭
（**配置 + 构建 + 运行**；只跑不构建会拿陈旧 exe 骗自己）。

工作目录必须是源码树，因为固件按相对路径打开。测试用到的 PDF 由
`python tools/make_fixtures.py` 生成，已经提交在 `tests/fixtures/`。

现有的 smoke：`version` `commandline` `instancegateway` `i18n` `document`
`outline` `textlayer` `render` `cache` `layout` `zoom` `documenttab`
`shellintegration`。

其中 `documenttab` 是把 `PageView` 与 `DocumentTab` 接起来的那一个：缩放、旋转之后
页面是否**真的上屏**，只能在这个层次上验——各部分的单元测试都对，照样可能因为两个
信号的先后顺序而永远只有灰底。

## 目录

```
xyzpdf/
  CMakeLists.txt      单一构建脚本：版本、选项、目标、翻译、测试开关都在这里
  main.cpp            语言、主题、单实例、命令行
  mainwindow.*        窗口级：菜单、工具栏、标签容器、最近文件、设置
  documenttab.*       文档级：一个标签 = Document + 渲染 + PageView + 缓存
  src/core/           设置、临时文件、窗口级对话框、语言表、最近文件、关于、许可
  src/document/       只读文档接口与 Qt PDF 后端、页面度量、书签树
  src/render/         代次令牌、异步渲染器、按字节计上限的缓存
  src/view/           自绘页面视图、页面排版、缩放控制器
  src/theme/  src/ui/  src/settings/  src/cli/  src/windows/
  packaging/          MSIX 清单（打包脚本在 M4）
  tools/              build_all_tests.py、add_language.py、make_fixtures.py、makeappicon.py
  tools/pdfprobe.cpp  量一个文档：每页多少像素、渲染多久、缓存能装几页
  tests/              *_smoke.cpp 与 fixtures/
  Resources/          图标、许可证、翻译
```

## 量一个文档

"这个文件为什么慢"没法从文档描述里回答，只能量：

```powershell
python tools\build_all_tests.py --build-only   # 会一并构建探针（XYZPDF_BUILD_PDFPROBE）
build-local-release\xyzpdf_pdfprobe.exe "D:\某文档.pdf" 12 2500
```

它打印页数、页尺寸、按"适应宽度"算出来的缩放、抽样页的像素尺寸/字节数/渲染耗时，
以及 400% 下整页与"屏幕上那一条"的字节数对比。任何名字里带 `probe` 的目标都不会被
测试脚本当成测试跑。
