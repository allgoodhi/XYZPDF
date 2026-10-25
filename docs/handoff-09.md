# 第九批：三种新的转换（2026-10-25）

转 Word 在 §124–§129 重写成"排出来像 PDF"：`src/document/pdfiumcontent.cpp` 把一页读成 `PageContent`（`pagecontent.h`），
`src/export/pageanalysis.cpp` 的 `analysePage()` 排成 `LayoutPage`（`layoutmodel.h`：段落、表格与合并格、图片、锚定图、两栏、页眉页脚、
整页成图的判断），`DocxWriter` 只是其中一个输出端。这一批再加三个，**三件同时发**：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| K1 `export-excel` | PDF → Excel（.xlsx）：表格成工作表 | 新 `src/export/xlsxwriter.*`、`excelexport.*`、`src/services/excelexportsession.*`；菜单一项 |
| K2 `export-powerpoint` | PDF → PowerPoint（.pptx）：每页一张幻灯片，按原位置摆 | 新 `src/export/pptxwriter.*`、`pptexport.*`、`src/services/pptexportsession.*`；菜单一项 |
| K3 `searchable-pdf` | 扫描件 → 可搜索的 PDF：识别出的字作为看不见的文字层，另存为新文件 | `src/ocr/ocrengine.*`（加带位置的识别）、新 `src/edit/textlayerwriter.*`（或同类名）、菜单一项 |

**基线**：发出这一批时的 main（第八批 J1/J2 合并之后），门禁数以那时为准，开工前先量一次写进报告。

---

## 每个分支都要先做的：环境准备与规矩

与 `docs/handoff-04.md` 的同名一节**完全相同**（两个 junction；从零构建；不删 `CMakeCache.txt`；控制跑按 sha256 还原、
控制必须能让新断言红；门禁全绿；`tr()` / `%n` / `QT_TRANSLATE_NOOP`；目录写成已完成并逐条核；写用户文件只有「保存」一条路；
不改日志与规矩文档；不打包、不安装、不 push、不合并；锚点；署名行；最终报告）。另读 `AGENTS.md` 里这几条：
"转 Word 是排出来像 PDF"、"测试目标的名字别带 install/bench/verify/probe"、"bash heredoc 里写 C++ 不要带反斜杠"、
"`DocumentTab` 的服务住在 `src/services/`"（新服务要被 `drainRenders()` 与析构等到）、"lupdate 只认名字"。

**三个分支共同的规矩**
- **永不写用户的 PDF**：K1/K2 写新的 .xlsx / .pptx；K3 写一个**新的** PDF（用户在保存框里选的路径；与原文件同路径时拒绝）。
  写法照 F8：同目录临时文件 → 校验 → 换上去；失败时目标位置什么都不留。
- **不改 `pageanalysis.cpp` 的行为**。需要它多交出什么（例如表格单元格的原始文字行），加只读的接口或在自己的模块里做；
  真要改分析，单独一个提交、说清为什么，并跑一遍转 Word 的回归（172 页的书要仍是 172 页，见下"判据"）。
- 菜单项都放在「文件」菜单「导出为 Word」的下面，助记字母别和同一菜单里已有的撞（`mainwindow_smoke` 会查）。
- 后台线程做转换、有进度、能取消、取消时不留半个文件 —— 照 `WordExportSession` 与 `MainWindow::exportCurrentToWord` 那一套。
- 自己写的 ZIP 用 `src/export/zipwriter.*`；每个 XML 部件要能被独立的解析器读通 —— 照 `tools/check_docx.py` 的做法各写一个
  `tools/check_xlsx.py` / `tools/check_pptx.py`（zipfile 验 CRC、ElementTree 解析每个部件、关系目标都在包里），并给它们各自一条控制
  （截坏一个部件必须 exit 1）。
- **判据是图**，和转 Word 一样：这台机器上 COM 答话的是 WPS（`E:\wps`）。WPS 表格的 ProgID 是 `KET.Application`（或 `et.Application`），
  WPS 演示是 `KWPP.Application`（或 `wpp.Application`）—— 先量哪个能用。用它把导出的文件打印/导出成 PDF，再与原 PDF 逐页渲染并排看
  （PDFium 渲染 PNG 的小脚本可以照 `tools/` 里已有的写法自己写在 scratch 里，不进仓库）。COM 自动化挂住时只杀**自己起的**进程。
- 样本：仓库里的 `tests/fixtures/*.pdf`，加上用 Qt（`QTextDocument` → `QPdfWriter`，要在 `windows` 平台下跑，离屏平台没字体）
  现造的带表格、合并格、格内图片、中英文的样本。**不要下载任何文件。**

---

## K1. PDF → Excel　分支：`export-excel`

**要做**
1. 每页 `analysePage()` 之后取它的表格（`LayoutBlock::Kind::Table`，以及 `Columns` 块里的）：**每张表一个工作表**，名字"第 N 页 表 M"
   （英文 "Page N Table M"，工作表名 31 字符上限、不许 `[]:*?/\`）。没有任何表格的页面：默认跳过；导出选项里可选"表格之外的文字也导出"，
   那时把这页的段落按行写成一列（制表位切开的行写进相邻的列 —— 那就是无框线表格）。
2. 单元格：合并格（`<mergeCells>`）、列宽与行高按 PDF 的尺寸换算、文字换行（格内多行用 `wrapText`）、粗体、字色、底纹、边框有无。
   **数值识别**：`1,234.5`、`-12`、`(12)`、`12%`、`¥1,200`、`$3.50`、`2026-10-25`、`2026/10/25`、`2026年10月25日` 写成数字/日期（带对应的
   数字格式），其余一律文本；看起来像数字但有前导零（编号 `00123`）、超过 15 位（身份证号、卡号）的**保持文本**。格内图片：放成浮动图片锚在那一格。
3. xlsx 的最小正确集合：`[Content_Types].xml`、`_rels/.rels`、`xl/workbook.xml` + rels、`xl/worksheets/sheetN.xml`、`xl/styles.xml`、
   `xl/sharedStrings.xml`；图片要 `xl/drawings/` 与 media。共享字符串、样式按内容去重。
4. 报告框（照转 Word 的完成框）：导出了几页几张表、跳过了几页没有表格的页。
5. 测试目标 `excelexport_smoke`：合成页面上的表格 → 合并格、数值识别（每种写法一条，含前导零与长数字两条反例）、工作表名截断与非法字符；
   写出的包用解析器读回逐项核；fixture `table.pdf` 端到端。每条配控制。

## K2. PDF → PowerPoint　分支：`export-powerpoint`

**要做**
1. 每页一张幻灯片，**幻灯片尺寸 = 页面尺寸**（EMU；页面尺寸不一时取第一页，其余页按比例放进去并居中，说清楚）。
2. 内容按 PDF 的位置**绝对定位**（演示稿本来就是这样，"看着一样"比 Word 容易）：每个段落（`LayoutParagraph`，含 `box`、行距、字号、粗斜体、颜色）
   一个文本框，关闭自动调整、零内边距；字距照转 Word 的做法用 `AdvanceWidths` 量出差额写成字符间距（pptx 是 `spc`，百分之一磅）。
   表格写成 PowerPoint 表格（`a:tbl`，含合并格）或按格的文本框，二选一并说明理由；图片按原字节放在原位置；分隔线与色块写成矩形形状；
   `pictureOnly` 的页（扫描、图纸）整页一张图。两栏块里的段落各自一个文本框即可。
3. pptx 的最小正确集合：`[Content_Types].xml`、`ppt/presentation.xml`（`sldSz`、`notesSz`）、`ppt/slides/slideN.xml` + rels、一个
   `slideMaster` + 一个空白 `slideLayout` + `theme`（PowerPoint 与 WPS 缺了这三样会拒开），media。
4. 测试目标 `pptexport_smoke`：合成页面 → 文本框位置与尺寸（EMU 换算）、表格、整页图；写出的包用解析器读回逐项核；fixture 端到端。每条配控制。

## K3. 扫描件 → 可搜索的 PDF　分支：`searchable-pdf`

**背景**：`src/ocr/ocrengine.h` 的 `recognize()` 只交回文字，没有位置。Windows.Media.Ocr 本身给每一行、每个词的外框（`OcrLine` / `OcrWord::BoundingRect`）。
AGENTS 里两条识别的规矩要遵守："识别器要按文字的族挑"（中英引擎回退）、"识别线程池的线程不许过期"（`setExpiryTimeout(-1)`）。

**要做**
1. `ocrengine` 加一个带位置的识别（例如 `recognizeLayout(const QImage&)` → 行 → 词 → 文字 + 像素外框），原 `recognize()` 不变。中英回退照旧。
2. 对**没有文字层**的页（`PageContent` 里可见字符很少、图片占大半页 —— 与转 Word 的扫描页判断同一个意思）按 300 dpi 渲染、识别，
   把每个词写成**看不见的文字**（`3 Tr`）放在它在页面上的位置：词的外框换算回页面坐标（考虑 `/Rotate` 与裁切框，换算一律走 PDFium 的
   `FPDF_DeviceToPage`），水平缩放（`Tz`）让这个词的宽度等于外框宽度。已经有文字层的页**原样不动**。
3. 字体：要能装中文。做法照 Tesseract 的"无字形字体"：一个 Type0 / CIDFontType2 字体，嵌入一个极小的 TrueType（每个字形都是空的），
   `CIDToGIDMap /Identity`、`/W` 给统一宽度，`/ToUnicode` 把 CID 映射到 Unicode（UTF-16BE，含增补平面的代理对）。字体对象整个文件共用一份。
   **样本形状从 PDF 32000-1 §9.7（复合字体）与 §9.10（ToUnicode）来，不从被测代码来。**
4. 写文件：用 qpdf 往页面内容流后面追加（`q ... Q` 包住，不改原有内容），新文件另存为（用户选的路径；与原文件同路径时拒绝），照 F8 的临时文件 → 校验 → 换上去。
   加密的文档：沿用原加密（qpdf 能做到时）或明确拒绝并说明。
5. 报告框：识别了几页、跳过了几页（已有文字层）、几页什么都没识别出。
6. 测试目标 `searchablepdf_smoke`（识别要在真实平台跑，照 `ocr_smoke` 的写法）：用 Qt 现画一张有中英文的图做成"扫描页"PDF → 生成 →
   (a) PDFium 从新文件里读出的文字包含画上去的那几个词，`FPDFText_GetCharBox` 的位置落在画的位置附近；(b) 新文件每页渲染出的像素与原文件**逐像素相同**
   （看不见的文字真的看不见）；(c) 原文件 sha256 不变；(d) 已有文字层的页内容流不变。每条配控制（例如把 `3 Tr` 改成 `0 Tr` → (b) 必须红）。

---

## 合并方式（主会话做）

三个分支都动 `mainwindow.cpp`（菜单）、`CMakeLists.txt`、英文目录，合并时主会话处理冲突、重生成目录、逐条核新句子。合并后重打包重装，
主会话在装上的那份上各点一遍（gui-probe 照 `wordexport` 模式各加一个），并用 WPS 把三种导出各打开一次并排看。
