# XYZPDF 整体方案

一个 Windows 桌面 PDF 阅读器。**第一阶段只做预览（只读），编辑留到后面**，但架构从第一天
起就为编辑留好位置。

做法、工具链、工程约定与 `H:\test\XYZip`（压缩软件）保持一致 —— 同一台机器、同一套
Qt/MSVC/vcpkg、同样的 smoke 测试和 MSIX 上架流程。下面凡是写「照 XYZip」的地方，都是指
去那个仓库里把对应文件读一遍再动手，不要凭想象重写。

---

## 1. 技术选型

| 项 | 选择 | 理由 |
|---|---|---|
| 语言/UI | C++ + Qt 6.11.2 Widgets（`E:\qt\6.11.2\msvc2022_64`） | 与 XYZip 同一套，控件、主题、对话框、翻译流水线全部可搬 |
| 构建 | CMake ≥ 3.19 + Ninja + MSVC 14.51 | 同 XYZip |
| 依赖管理 | vcpkg manifest，triplet `x64-windows` | 同 XYZip；v1 几乎不需要第三方库 |
| 渲染后端 (v1) | **Qt PDF**（`Qt6::Pdf`，内部是 PDFium） | XYZip 的压缩包内预览已经在用它，DLL 分发和 LGPL 规矩现成 |
| 结构编辑 (v2) | **QPDF**（Apache-2.0） | 页面增删/旋转/合并/拆分/加密/水印/书签，纯结构操作，许可干净 |
| 内容编辑 (v2) | **PDFium 全量 API**（BSD-3，预编译二进制） | 注释、表单填写、文本对象，Qt PDF 没暴露 |
| 明确排除 | MuPDF / Ghostscript | AGPL，商业上架不可用 |

### 1.1 Qt PDF 的能力边界（先认清，免得中途返工）

**给得了的**：页面渲染（`QPdfDocument::render`，`QPdfDocumentRenderOptions` 支持旋转、
`scaledClipRect` 局部渲染、`RenderFlag::Annotations` 渲染注释）、异步多线程渲染
（`QPdfPageRenderer`，`RenderMode::MultiThreaded`）、文本提取与选择（`getAllText` /
`getSelection`）、搜索（`QPdfSearchModel`）、书签（`QPdfBookmarkModel`）、链接
（`QPdfLinkModel`）、页导航（`QPdfPageNavigator`）、密码文档。

**给不了的**：表单交互、注释的编辑/新增、任何写回、打印助手（要自己用 `QPrinter` +
按打印机 DPI 渲染页面）、签名验证、附件提取、页面级 XObject 访问。

结论：**v1 的所有功能都在 Qt PDF 能力内**；v2 一开始就要把 PDFium 直接接进来，所以 v1
的文档层要藏在一个薄接口后面（见 §3.2），但不要为了「将来」把接口设计得面面俱到。

### 1.2 视图不要用 `QPdfView`

`QPdfView` 是个演示级控件：以光标为锚的 Ctrl+滚轮缩放它不做（XYZip 里已经自己补过），
选择高亮、搜索高亮、双页/书籍模式、缩略图联动、页间间距、旋转持久化它都不做。v1 就
自己写 `PageView`（继承 `QAbstractScrollArea`，自己排版和绘制），后面编辑阶段的选择框、
拖拽手柄也要长在这个类上。**这是从一开始就要定下的，中途换代价很大。**

---

## 2. 产品形态（"使用方式和压缩软件一致"）

- 双击 `.pdf` 直接打开；`xyzpdf.exe <file>` 命令行打开；支持 `-page N`、`/p`（打印）
- **单实例网关**：第二次启动把文件名通过 `QLocalServer/QLocalSocket` 交给第一个实例，
  由它开新标签页 —— 照 XYZip `src/cli/instancegateway.*`
- **资源管理器集成**：右键菜单（`IExplorerCommand`）、预览窗格处理器、缩略图处理器 ——
  照 XYZip `src/shell/`
- **文件关联 / 默认程序**：MSIX manifest 声明 + 设置里的"设为默认"入口
- **多语言**：全部字符串走 `tr()`，`tools/add_language.py` 流水线原样搬
- **深/浅主题**：照 XYZip `src/theme/`，另加 PDF 专属的"夜间阅读（页面反色）"
- **最近文件**：照 `src/core/recentarchives.*` 改成 `recentdocuments`，带跳转列表
- **打包**：MSIX，本地测试包 + 商店包两份 manifest，WACK 过一遍 —— 照 `packaging/`
- **版本号单点定义**：`project(xyzpdf VERSION ...)` → About 框、VERSIONINFO、包 manifest
  三处生成，`tests/version_smoke.cpp` 校验 manifest 确实被 stamp 过

免费/Pro 双版本（XYZip 有 edition 机制）**v1 不做**，只留 CMake 选项和 `edition.h`，见 §7.2。

---

## 3. 代码结构

```
xyzpdf/
  CMakeLists.txt            # 单一构建脚本，版本、选项、目标、翻译都在这里
  main.cpp                  # DPI、单实例、命令行、主题、语言
  mainwindow.{h,cpp,ui}      # 窗口级：菜单、工具栏、标签容器、最近文件、主题
  documenttab.{h,cpp}        # 文档级：一个标签 = Document + PageView + 侧栏状态
  src/
    core/        version.h.in packageconfig.h.in appsettings.h recentdocuments
                 edition.h tempmanager windowdialogs aboutinfo languages
                 thirdpartynotices
    document/    document.h(后端接口) qtpdfbackend outline pagemetrics
                 textlayer searchindex docmetadata passwordprompt
    render/      pagerenderer(封装 QPdfPageRenderer) rendercache(LRU,按像素字节计)
                 rendertoken(代次令牌)
    view/        pageview pagelayout(连续/单页/双页/书籍) zoomcontroller
                 selectionmodel overlaylayer(搜索高亮/链接热区)
    ui/          toolbar sidebar thumbnailbar outlinepanel searchpanel
                 gotopagebox zoombox statusbar
    print/       printjob(QPrinter + 按 DPI 渲染) exportimages
    settings/    settingsdialog
    theme/       thememanager theme.h            # 直接从 XYZip 搬
    shell/       xyzpdfshell(右键) xyzpdfpreview(预览窗格) xyzpdfthumbnail(缩略图)
    windows/     shellintegration packageidentity windowsfilename dragstaging
    cli/         commandline instancegateway
  packaging/     AppxManifest.xml StoreAppxManifest.xml build-package.ps1
                 build-store-package.ps1 verify-package.ps1 Assets/
  tools/         build_all_tests.py run_all_tests.ps1 add_language.py
  tests/         *_smoke.cpp  fixtures/
  Resources/     resources.qrc translations/  xyzpdf.rc  xyzpdf.manifest
```

### 3.1 直接从 XYZip 搬（复制，不做共享库）

`core/tempmanager`、`core/windowdialogs`、`core/languages`、`core/recentarchives`、
`theme/*`、`cli/instancegateway`、`cli/commandline`、`windows/*`、`ui/languagedialog`、
`ui/thirdpartydialog`、`packaging/*.ps1`、`tools/*.py`。

**为什么复制而不是抽公共库**：两个项目的构建树、edition 选项、打包脚本都不一样，抽库
会让两边的 CMake 同时变复杂，而这些文件本身几乎不变。复制时**把原文件里的注释一起带过来**
—— 那些注释记的是踩过的坑。

### 3.2 文档层接口（为 v2 编辑留的唯一抽象）

```cpp
// src/document/document.h  —— 只定义 v1 真正用到的东西，不要预先塞编辑接口
class Document {
public:
    virtual ~Document() = default;
    virtual Status  load(const QString &path, const QString &password) = 0;
    virtual int     pageCount() const = 0;
    virtual QSizeF  pageSize(int page) const = 0;          // pt
    virtual QImage  render(int page, QSize px, const RenderOptions &) = 0;
    virtual QString text(int page) const = 0;
    virtual std::vector<TextRun> textRuns(int page) const = 0;  // 选择/高亮要盒子
    virtual const Outline *outline() const = 0;
    virtual std::vector<Link> links(int page) const = 0;
    virtual Metadata metadata() const = 0;
};
```

v1 只有 `QtPdfBackend`。v2 加 `PdfiumBackend`，编辑能力作为**单独的**
`DocumentEditor` 接口出现，不要污染上面这个。

### 3.3 三条必须守住的渲染约束

1. **陈旧结果不许上屏。** 每次请求带一个单调递增的 token；换文档、换页、换缩放之后，
   回来的旧图直接丢弃。XYZip 的压缩包内预览这一条改了四轮才咬住，这里一次做对。
2. **`QPdfDocument` 不是线程安全的。** 要么用 `QPdfPageRenderer` 的多线程模式（它自己
   管），要么每个渲染线程一份独立 document。**绝不要**在 UI 线程和工作线程共用同一个。
3. **只渲染看得见的 + 前后各一屏。** 缓存按**像素字节数**设上限（建议 256 MB 可配），
   LRU 淘汰；没渲染好的页画灰底 + 页码占位，不要留白闪烁。高 DPI 屏按
   `devicePixelRatio` 渲染，别拿 1x 图放大。

另外：**只读打开，永不写用户的文件**；所有临时文件走 `TempManager`；所有对话框走
`WindowDialogs`（窗口级模态）。

---

## 4. 里程碑

### M0 — 骨架（能开、能显示第一页）
CMake + 版本单点 + `edition.h` 占位 + About + 翻译框架 + 主题 + 单实例 + 命令行 +
文件关联占位；**MainWindow / DocumentTab 的 1 : N 划分**（见 §7.1，此时 tab bar 可以
不显示）；`QtPdfBackend` 打开文件，`PageView` 画出第 1 页。
**验收**：双击一个 PDF 能开，窗口标题是文件名，关掉不崩；第二次启动的文件进了同一个
窗口的新标签。

### M1 — 阅读核心
连续滚动排版、页间距、缩放（适应宽度/适应整页/百分比/Ctrl+滚轮**以光标为锚**）、
旋转、跳转页码、键盘导航（PgUp/PgDn/Home/End/方向键/空格）、异步渲染 + LRU 缓存 +
占位、缩略图侧栏、书签大纲侧栏、加密 PDF 的密码框。
**验收**：500 页文档滚动不卡；扫描件（纯图，单页 10 MB+）可正常翻页；内存有上限。

### M2 — 文本与检索
文本层、鼠标拖选 + 双击选词 + Ctrl+A、复制、全文搜索（增量、结果列表、当前项与其余项
不同色高亮、上一个/下一个、大小写与全词选项）、内部跳转链接与外部链接（外链点击前
**必须确认**）、文档属性对话框。
**验收**：CJK 文档选择与搜索正确；跨页选择正确；搜索大文档时 UI 不冻。

### M3 — 体验与系统集成
标签页完整交互（中键关闭、拖拽重排、拖出成新窗口）、最近文件 + 跳转列表、夜间阅读（反色）、双页/书籍模式、打印（`QPrinter`，
按打印机 DPI 渲染，支持页范围、缩放到纸张、双面）、导出选中页为 PNG/JPEG、
区域截图到剪贴板、拖放打开、会话恢复（重开回到上次页码与缩放）、
资源管理器右键菜单 + 预览窗格 + 缩略图处理器。
**验收**：WACK 通过；预览窗格在 DllHost 里不锁死、不泄漏。

### M4 — 发布
MSIX 本地包与商店包、文件关联与默认程序、第三方声明页、英文/中文双语完整、
性能基线记录在 `docs/perf.md`、`store-submission.md`。

### M5 — 编辑（后续，方案另议）
先上 **QPDF 结构编辑**：页面增删/重排/旋转持久化、合并、拆分、加密/解密、水印、
书签编辑 —— 这些不碰页面内容流，风险最低，覆盖 80% 的编辑需求。
再上 **PDFium**：注释（高亮/便签/画笔）、表单填写、签名外观。
配套要建的地基：文档修改模型 + 撤销栈 + ~~增量保存~~ + "另存为"默认不覆盖原件。

进度（2026-09-24）：这一串里已落地**删页 / 旋转 / 重排 / 合并**四样，地基（内存操作列表 +
撤销栈 + 脏标记 + 另存为）跟着一起立起来了 —— 逐节的实测与欠账见 `docs/store-submission.md`
§11–§19。合并两侧都有测试（模型 244 处 check，对话框那 156 行是离屏跑的，含倒序的对照），
新对话框在真窗口里中英各点了一遍（§18，撞出三条，两条已改），包也换过来了：本机现在装的这一份
与开发树那一份 exe 逐字节相同，合并到写出 46 页的文件是在**装着的**那一份上走的（§19）。
拆分、加密/解密、水印、书签编辑还没动。

**「增量保存」在 M5 动手时划掉了**，两个理由，都是量出来的不是猜的：
① QPDF 的写入端 `QPDFWriter` 只有"整份重写"这一种输出，没有在原文件末尾追加修订段的接口，
要做增量得自己拼 xref 与对象流 —— 那是造一个 writer，不是用 QPDF；
② 同一条地基里的"另存为默认不覆盖原件"已经把方向定了：写盘从来不碰用户正在读的这份文件，
而增量保存省的恰好是"把新副本写回原来那个大文件"的 IO。为一个省掉的写入去改写用户文件的字节，
与"永不写用户的 PDF 文件"这条底线不成比例。
所以实现是：修改只记在内存的操作列表里，保存时重新打开源文件、按列表重放、整份写到新路径
（这个"重放 + 整份写"的量级是毫秒到几十毫秒，两个 fixture 各自的计时区间见 `docs/store-submission.md` §12）。

---

## 5. 测试

照 XYZip：`tests/*_smoke.cpp`，每个模块一个，`tools/build_all_tests.py` 一把梭
（**配置 + 构建 + 运行**；只跑不构建会拿陈旧 exe 骗自己，XYZip 已经被坑过）。
测试的工作目录必须是源码树，因为 fixture 按相对路径打开。

`tests/fixtures/` 至少放：普通文字 PDF、CJK PDF、扫描件（纯图）、加密 PDF、
大文件（500+ 页）、带书签和内部链接的、**故意损坏的**、页面尺寸不一致的、带旋转标记的。

必须有的 smoke：`document_smoke`（打开/页数/尺寸/损坏文件不崩）、`render_smoke`
（尺寸正确、token 作废逻辑）、`cache_smoke`（上限与淘汰）、`text_smoke`、
`search_smoke`、`outline_smoke`、`layout_smoke`（连续/双页的页位置计算）、
`zoom_smoke`（以光标为锚的公式）、`commandline_smoke`、`instancegateway_smoke`、
`i18n_smoke`、`version_smoke`、`print_smoke`。

---

## 6. 给实施方（其他模型）的硬性要求

1. **先读 XYZip 再动手**：`H:\test\XYZip\xyzip\CMakeLists.txt`、`src/preview/previewpane.*`
   （已有的 Qt PDF 用法和踩过的坑都在注释里）、`src/cli/`、`src/theme/`、`tools/`、
   `packaging/`。搬代码时连注释一起搬。
2. **不要删 CMake 缓存**：本项目按 XYZip 的习惯不带 CMakePresets，Qt 的位置只存在于
   构建树的 `CMakeCache.txt` 里，删了就找不到 Qt。
3. **编不过就说编不过**。工具链不在或没跑通时，代码可以写，但必须明说"未编译验证"，
   不要报告成完成。
4. 源码里有中文字面量，MSVC 必须 `/utf-8`；非 Qt 目标也要加。
5. 每个不显然的决定在代码里留一句注释说明**为什么**，不要写注释复述代码在做什么。

---

## 7. 已定下的三件事

2026-09-16 定，实施方按这个做，不要再自行选择。

1. **多标签页从 M0 就做。** MainWindow 的中心控件是 `QTabWidget`（或自绘 tab bar），
   每个标签一个 `DocumentTab`（持有自己的 `Document` + `PageView` + 侧栏状态）。
   M0 阶段允许只有一个标签、不显示 tab bar，但**类的划分必须已经是"窗口 : 标签 = 1 : N"**
   —— 后补这一刀要动到全部视图和侧栏代码。窗口级的东西（菜单、工具栏、最近文件、主题）
   留在 MainWindow，文档级的东西（页码、缩放、旋转、搜索、选择）全部属于 DocumentTab。
   标签的完整交互（中键关闭、拖拽重排、拖出成新窗口）排在 M3。

2. **v1 不做 Pro/免费双版本，但 CMake 留位。** 照 XYZip 的 `edition` 机制留一个
   `XYZPDF_EDITION` 选项和 `src/core/edition.h`，v1 只有一个取值。**不要**为此建第二棵
   构建树、第二份 manifest、第二套 Assets —— 那些等真要做 Pro 的时候再加。
   代价只是一个头文件和一个选项，收益是将来不用回头改遍打包脚本和测试。

3. **v2 编辑与阅读同一个可执行文件，用模式切换。** 不做独立 exe、不做独立进程。
   阅读是默认模式；进入编辑模式后才创建 `DocumentEditor` 和撤销栈。含义：
   - 文件关联、单实例网关、shell 集成只有一套，v1 怎么写 v2 就怎么用
   - `Document`（只读接口）与 `DocumentEditor`（修改接口）是两个类，编辑模式下后者
     持有前者，**不要**把写方法塞进 `Document`
   - v1 的"只读打开、永不写用户文件"约束仍然成立：写盘只发生在编辑模式的显式保存
