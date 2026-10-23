# PDFium：方案与探针（F5）

分支 `pdfium-plan`，2026-10-22。handoff-02 的 F5：**只做方案与量，不改应用代码。**

这一支动过的东西只有五样，全在应用之外：

| 文件 | 行数 | 是什么 |
|---|---|---|
| `xyzpdf/tests/pdfiumprobe.cpp` | 1574（新） | 探针本体，八条控制 |
| `xyzpdf/CMakeLists.txt` | +79/−0 | 一个默认关的探针目标 + 那条"没给目录就不建"的 guard |
| `xyzpdf/tools/pdfium_probe_build.py` | 62（新） | 用门禁自己的机制建那一根目标 |
| `xyzpdf/tools/pdfium_probe_run.py` | 61（新） | 按门禁的方式跑它（先写样本，再跑） |
| `xyzpdf/tools/pdfium_probe_control.py` | 193（新） | 九次"故意改坏"的控制跑，跑完按 sha256 还原 |

（这一支不写本文档自己的行数——写下去的那一刻它就旧了。**提交信息里出现过的两个文档行数都算错**
（485、507，现读 `wc -l docs/pdfium-plan.md` 为准），因为补一句话进去就会让那句话自己变旧：
这是同一族毛病，本项目在 §78 记过"上一片记的 0 WARNING 是读数不是愿望"。
表里其余行数核过且不会再动：pdfiumprobe.cpp 1574、CMakeLists +79/−0、
pdfium_probe_build.py 62、pdfium_probe_run.py 61、pdfium_probe_control.py 193。）

`git diff HEAD -- xyzpdf/src/ xyzpdf/documenttab.* xyzpdf/mainwindow.* xyzpdf/Resources/` **为空**：产品代码、目录、打包脚本一行没动。
默认构建不依赖 PDFium（量法见 §2 末尾）。

---

## 1. 先说结论

1. **表单 widget 不显示这件事，直接链接 PDFium 能修，而且是那一步没调用而不是没能力。**
   同一份文件、同一个矩形，Qt PDF 六档渲染尺寸全是 **0 像素**（§94 的表在本支的仪器下原样复现），
   而 PDFium 加了 `FPDFDOC_InitFormFillEnvironment` + `FPDF_FFLDraw` 之后**画满整框**：
   页面视图 417×63 = **26271** 像素、300 dpi 打印 834×125 = **104250** 像素，一格不落。
2. **建议走"取代"，不走长期并存**，理由是包体积（+7.59 MB vs +2.97 MB 未压缩）和"两份 PDFium 各自
   认自己的锁、缓存与字体"，不是"少写点代码"——并存期间要写的代码其实更多（§6、§8）。
3. **第一步只做"画"，不做"填"。** 量到 `FPDFAnnot_SetStringValue` 被接受、`/V` 读得回来，
   但**下一帧一个像素都不变**（框内 0）；要看到新值必须 `FPDFPage_Flatten`（量到返回 1=成功，
   整页 26271 像素变化）。"填进去就能看见"是另一件有尺寸的事，见 §7。
   （"读得回来"那句只说写进去之后再读那一颗注释这一种读法；文件里原有的 `/V` 用表 3 那种读法读不出来，
   两条读法的差别写在 §4.2，没查。）
4. **替换的保真代价：合成 fixture 上是 2–3/255 的抗锯齿抖动（0.05–0.2% 像素），真书上是
   4.6% 像素、最大 69/255。** 这句话必须在拍板之前说，因为它是"用户会不会看出来"的第一层证据，
   而 69/255 是可能看得见的量级（§10 的 R1）。
5. **这一支没给"换完之后会变快"的承诺。** 打开与页树明显快（真书 35.3 ms → 0.3 ms，600 页尺寸
   遍历 25.0 ms → 3.2 ms），文字提取快约 25%，但**单页光栅化在合成文字页上慢一倍**
   （0.43 → 0.92 ms/页），扫描件快（28.75 → 21.19 ms/页）。差异里有一大块是本探针自己的做法造成的，
   没资格当结论（§5 那节写清楚是哪一块）。
6. **线程这件事可以照 Qt 的做法抄，不要抄它的理由。** PDFium 头文件自己说这些 API 不是线程安全的；
   我们量到"四个线程各持一份文档、不加锁"4/4 成功且 25.6 ms 打完 86.2 ms 的活。
   但这不等于 F7 该给每个线程一份文档——**内存那一半没量到**（工作集 0 KB 是"还没画"不是"便宜"）。

---

## 2. 拿到的 PDFium，和它是怎么被量的

**来源**：GitHub `bblanchon/pdfium-binaries`（非 Google/Foxit 官方；PDFium 最常用的预编译发布，
仓库脚本 MIT，每周自动构建）。**没有下载任何未经批准的东西**：这一份是先向作者报告来源/版本/大小/许可证、
得到"下载最新固定版本"一句之后才取的。

| 项 | 值 |
|---|---|
| release tag | `chromium/8111`，版本 **158.0.8111.0**，发布 2026-10-19 |
| 取的资产 | `pdfium-win-x64.tgz`（x64 release，**不含 V8**），3,905,343 字节 |
| tgz sha256 | `3452d11031050ec6…` |
| 解出的 `bin/pdfium.dll` | **7,586,816** 字节，sha256 `a318c39c1374…` |
| `lib/pdfium.dll.lib` | 115,006 字节，sha256 `8842b071d21e…` |
| 展开后整个目录 | 8,333,977 字节（DLL + 22 个头文件外加 `include/cpp/` 两个 + 18 份许可证 + args.gn + VERSION + PDFiumConfig.cmake） |
| 构建参数（`args.gn` 原样） | `pdf_enable_v8=false`、`pdf_enable_xfa=false`、`pdf_is_standalone=true`、`is_component_build=false`、`target_cpu=x64` |
| 放在哪 | `H:\test\XYZPDF-wt\third-party\pdfium-8111`，**在仓库之外**，版本库里没有它的任何字节 |

对照的另一半是这台机器上正在用的：`E:\qt\6.11.2\msvc2022_64\bin\Qt6Pdf.dll` **4,620,600** 字节
`c68882fca099…`（PDFium 静态编在里面，§82 量过它只剩 2 个 `FPDF_*` 字符串）。

**为什么按哈希而不是按名字认**（§97 那条规矩在这里的直接用处）：探针第一件事就是把自己进程里
真正加载的那两颗 DLL 的路径、大小、sha256 印出来——`pdfium.dll` 落在构建树里（POST_BUILD 复制的那份），
`Qt6Pdf.dll` 落在 Qt 的 bin。这两行在日志开头，读数才有主人。

### 怎么重跑

```
python tools/pdfium_probe_build.py                 # 单独一个 build-pdfium-probe 目录
python tools/pdfium_probe_run.py H:\tmp\outline\real-book.pdf H:\tmp\pdfium-probe\run.txt
python tools/pdfium_probe_control.py               # 四次故意改坏 + 按 sha256 还原 + 重编
```

`pdfium_probe_run.py` 先跑 `xyzpdf_documenteditor_smoke.exe`（带 `XYZPDF_FORM_SAMPLE_DIR`）把 §54 那台
生成器的十一样本写出来，再跑探针——**样本是现造的，不是手抄的**，所以每次重跑都是同一批字节。
`real-book.pdf`（172 页中文书，`H:\tmp\outline\`，不在版本库里）只有这台机器上有，
凡是引用它的数都写着"真书"。

### 默认构建确实不依赖它（量过，不是设计意图）

门禁脚本会把 `CMakeLists.txt` 里**每一个** `XYZPDF_BUILD_*` 选项都打开再配置一次，所以"选项默认 OFF"
本身保护不了门禁——真正的护栏是"只有给了 `XYZPDF_PDFIUM_DIR` 才创建目标"。量出来的三行：

| 问的是 | 答 |
|---|---|
| `python tools/build_all_tests.py` 全绿吗 | **26 passed / 0 failed**，目录 631 / numerus 49（与基线一字不差），目标数 31 → **32** |
| 默认构建树里有没有 pdfium 的规则 | `build-local-release/build.ninja` 里 `pdfiumprobe\|pdfium.dll\|xyzpdf_pdfium` **0 处**；`build-pdfium-probe/build.ninja` 57 处 |
| 缓存里那两个值 | `XYZPDF_BUILD_PDFIUM_PROBE:BOOL=ON`（被脚本打开的）配 `XYZPDF_PDFIUM_DIR:PATH=`（空）→ 目标不存在，这是那条 guard 在起作用 |

**一次假的红灯，记在这里因为它不是这一支的**：收尾那一遍 `previewhandler_smoke` 报
`FAIL: the file can be deleted while the pane is still showing it`（25/1）。
用私有 `TMP/TEMP` 单独重跑那一根两次：**exit 0、0 FAIL**。同一族的事已经记过一次
（并行分支的测试套件共用 TEMP 时 previewhandler / mainwindow_smoke 会抖）。
这一支没有碰预览处理器，也没有碰任何产品源码（`git status` 只有 CMakeLists 一处改动加四个新文件），
收尾那一遍在私有 TEMP 下是 **26/0**。

---

## 3. 探针量的是哪五件事

一次跑完输出七节（`== 1. .. == 7.`），八条控制打在上面。控制的意思是：**仪器必须能红**——
所以这一支的八条控制**每一条都被真弄红过一次**。九次"故意改坏"的日志各存一份、不互相覆盖
（`H:\tmp\pdfium-probe\control-<变异名>.txt`，汇总 `controls-all.txt`）：

| 控制 | 钉的是 | 把它弄红的变异（每次只改那一行） | 结果 |
|---|---|---|---|
| C1 | 210 个格子（7 样本 × 5 路 × 6 尺寸）每格真出了图——"没渲出来"和"没画"长得不一样 | `qt-route-produces-nothing` | **C1 红**（42/210 没图），C2/C4/C8 跟着红。**级联写在这儿不是意外**：它们读同一批格子，一条路不出图时这张表本就不该继续绿 |
| C2 | 注释层看得见：`markup-square.pdf` 在 Qt 和"要了注释"的 PDFium 路上必须差出像素 | `no-annot-flag` | **只有 C2 红**，六档全 0 |
| C3 | 那个差值真是注释开关给的：flags=0 时同一个方块**不许**差 | `square-draws-without-flag` | **只有 C3 红** |
| C4 | 数的窗口落在真像素上：画进内容流的矩形必须每条路每档都差出来 | `box-on-blank-paper`（把它的框挪到空白处） | **只有 C4 红** |
| C5 | **这一支的主张**：带 `/AP` 的 widget 在 `page+annot` 上是 0、加了 FFL 之后必须有东西 | `no-ffl`（那句 `FPDF_FFLDraw` 短路掉） | **只有 C5 红**，六档全 0 |
| C6 | 两个读页尺寸的库说同一件事（框按点算，尺寸一错两列就不是同一个矩形） | `pdfium-page-size-lies`（PDFium 报的尺寸 ×1.1） | **C6 红**（七份样本 + 九份保真文件各一条），另红 C2/C4/C5 共 13 条——框整体错位时这张表也不该绿 |
| C7 | FFL 那几行量的是"真有个表单模块在管这份文档"，且"widget 是 0"带着"文件确实带着画"这句伴行（§82 的规矩） | `carried-opens-without-form`（表 3 不带表单环境打开） | **只有 C7 红** |
| C8 | 纸是纸、墨是墨：`plain.pdf` 整页墨量要 >0 **且** < 1/5 页 | `no-fill`（不填白） | **只有 C8 红**：PDFium 侧 2,173,720/2,173,720 全算墨 |
| | | `no-composite`（不把 Qt 的图合成到白底） | **只有 C8 红**：Qt 侧整页算墨 |

九次退出码都是 1（每次都确实有控制红），跑完 `pdfiumprobe.cpp` 与动手前 **sha256 逐字节相同**
（`28287f1cfcbe…`）并且重编了一遍。**还原后重跑过**（§78 那条：`--run-only` 跑的是上一版二进制）：
`run8-restored.txt` 与还原前那份干净读数 `run7.txt` 比，**231 行像素读数一行不差**
（时间那一栏不比——它本来就不该逐次相同）。
下面 §4/§5 引用的就是这批数。

**C8 抓到的两个错都是我自己的仪器错**，值得写下来，因为它们是同一族的老毛病：

1. `FPDFBitmap_Create(...,0)` 给的缓冲区**不是白的，是没填过的**：不调 `FPDFBitmap_FillRect` 就等于
   把未初始化内存当纸。头两遍跑出来的"每个框都满墨"（`ink` 列全等于框面积）就是这么来的，
   那时 C8 还不存在，我差点把"PDFium 画满了"写进结论。
2. 反过来，**先归因归错了一次**：我以为是 `BGRx` 的第四个字节被当 ARGB32 读、把画的东西合成掉了，
   于是加了"改成 RGB32"这一步。控制跑 `argb32`（把图按 ARGB32 读回去）**一条控制都没弄红**——
   说明填过白的缓冲区里第四字节确实是 0xFF，那个改动没有承重力。真正承重的是第 1 条。
   这一条按 §99 的判据处理：**控制没让结果变的，就不是证据**，所以那句注释现在写的是
   "defensive, not load-bearing"，而不是我原来的说法。

---

## 4. 表单：量到的东西

### 4.1 六档尺寸、五条路、七份文件（差值像素，全部在字段矩形**之内**）

`plain.pdf` 是基线；`qt-pdf` 就是应用现在走的路。
尺寸表抄自 §94（每档是哪一处调用点的，那边写着文件与行号）。

"qt / +page / +annot" 那一行的三格是**三条路**的同一个样本（三格全 0 = 三条路都没画）；
每档只展开写 `+annot+ffl` 那一行，因为它是唯一把 widget 画出来的路。
（`pdfium-page` 与 `pdfium-page+annot+ffl` 两行在 150/260/400/2480 四档上的完整读数在
`H:/tmp/pdfium-probe/run7.txt`，本表是抄录。）
un7.txt`，本表是抄录。）

| 尺寸（档） | 路 | flat | appearance | directive | xfa | square | painted |
|---|---|---|---|---|---|---|---|
| 1240×1753 页面视图 | qt-pdf | 0 | 0 | 0 | 0 | 2580 | 22034 |
| | pdfium-page | 0 | 0 | 0 | 0 | **0** | 22034 |
| | pdfium-page+annot | 0 | 0 | 0 | 0 | 2580 | 22034 |
| | **pdfium-page+annot+ffl** | **5058** | **26271** | **5068** | **5058** | 2580 | 22034 |
| | pdfium-page+ffl（不给注释开关） | 5058 | 26271 | 5068 | 5058 | **0** | 22034 |
| 150×212 侧栏缩略图 | qt / +page / +annot | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 183 / 0 / 183 | 342 |
| | **+annot+ffl** | **201** | **408** | **199** | **201** | 183 | 342 |
| 260×368 壳缩略图 | qt / +page / +annot | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 348 / 0 / 348 | 909 |
| | **+annot+ffl** | **448** | **1232** | **454** | **448** | 348 | 909 |
| 400×566 预览窗格 | qt / +page / +annot | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 498 / 0 / 498 | 2170 |
| | **+annot+ffl** | **874** | **2835** | **891** | **874** | 498 | 2170 |
| 1240×1754 导出 150dpi | **+annot+ffl** | **5082** | **26271** | **5095** | **5082** | 2580 | 22034 |
| 2480×3508 打印 300dpi | qt / +page / +annot | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 9297 / 0 / 9297 | 88136 |
| | **+annot+ffl** | **18075** | **104250** | **18083** | **18075** | 9297 | 88136 |

读法（每条都有伴行证据，不是"看着像"）：

* **`qt-pdf` 那一行五份字段文件全是 0。** §94 的结论在这台机器、这个新仪器下原样复现。
  它现在有意义，是因为同一行的 `square` 与 `painted` 两列不是 0 —— 这一层在这个尺寸看得见那个矩形。
* **`pdfium-page+annot` 与 `qt-pdf` 在 `square`/`painted` 两列逐档相等**（2580/2580、183/183、348/348、
  498/498、2580/2580、9297/9297）。这条比"widget 画出来了"更有信息量：
  **两个 PDFium 在注释层的非-widget 部分行为一致，widget 那一支才是差的**，
  所以"Qt 这条路没调用 FFL"是唯一还站得住的解释——也是 §1 结论 1 的根据。
* **FFL 不看注释开关**（最后一行 `pdfium-page+ffl`：widget 照样 5058/26271，方块 0）。
  头文件那句话（"Regardless of FPDF_ANNOT flag, this function will always render widget annotations"）
  量出来是对的。实践意义：走 FFL 之后**必须自己**决定"什么时候该画 widget"，别指望开关。
* **没有 `/AP` 也画**（`field-flat.pdf` = 5058，`worst=255` 黑；`field-appearance.pdf` = 26271、
  `worst=76` 正是外观流里那句 `0.70 0.70 0.90 rg`）。PDFium 会**自己造**一层面皮，
  这一条把"要不要自己解 `/AP` 画到页面位图上"（§82/§94 留的那个选项）从待决变成了不需要——
  **前提是把表单模块初始化起来**。
* **`/NeedAppearances`（directive 5068）与 XFA 包（xfa 5058）都不改变"画不画"这件事**：
  两者与 flat（5058）同一档，directive 多出的 10 个像素**没有查为什么**——那份文件比 flat 多的
  是文档级 `/DA`、`/DR`（§58 的生成器注释写着），看着像字号/字体变了几个像素，但这是猜测，
  不是读数。要紧的是三者都 >0：**这条能力不被 `/NeedAppearances` 或 XFA 包挡住**。
  第二份表单本身（§55 的 XML）**仍然没有被画**：见 §4.3。

### 4.2 文件带着什么，用要画它的那个库读

| 样本 | annots | subtype | 外观解出几个对象 | `FPDF_GetFormType` | `/V`（表 3 的读法） |
|---|---|---|---|---|---|
| plain.pdf | 0 | — | 0 | none | — |
| field-flat.pdf | 1 | Widget | **0** | acroform | （空） |
| field-appearance.pdf | 1 | Widget | **2** | acroform | （空） |
| field-directive.pdf | 1 | Widget | 2 | acroform | （空） |
| field-xfa.pdf | 1 | Widget | 0 | **xfa-foreground** | （空） |
| markup-square.pdf | 1 | Square | 0 | none | — |
| painted-rect.pdf | 0 | — | 0 | none | — |

**`/V` 那一列是空的，而且这是一个还没解释的不对称**：表 3 用 `FPDFAnnot_GetStringValue(annot, "/V", …)`
读，每份字段文件都返回不到一个字符（生成器明明白白写了 `/V (typed on page 1)`，qpdf 那边 §54 的读数一直看得见它）；
而 §7 那一段在**先 `FPDFAnnot_SetStringValue` 再读**的同一颗注释上读回了 `[typed-by-probe]`。
两种读法差的是"有没有先写过一次"，机制没查。
不影响这一支的结论——表 2 里那 5058 个像素证明画面用的是值（没写过 `/AP` 的文件也画出了字），
但**别把这一列当"文件里没有值"读**，那是错的。F7 之前值得单独量一次。

这张表是 §3 里 C7 的证据来源：`field-flat` 的 0 个对象 + 5058 个像素 = "PDFium 自己造的"；
`field-appearance` 的 2 个对象 + 26271 = "画的是文件里那层"。

### 4.3 XFA：这一份 build 检得出、装不上

```
field-flat.pdf         form before=acroform        LoadXFA=false form after=acroform
field-appearance.pdf   form before=acroform        LoadXFA=false form after=acroform
field-directive.pdf    form before=acroform        LoadXFA=false form after=acroform
field-xfa.pdf          form before=xfa-foreground  LoadXFA=false form after=xfa-foreground
```

`FPDF_LoadXFA` 在所有样本上都返回 false（`args.gn` 里 `pdf_enable_xfa=false`），
而 `FPDF_GetFormType` **仍然报 xfa-foreground** —— 也就是说：换成直接链接的 PDFium，
我们对 XFA 的能力**没有增加**（画不出来），但**检测变容易了**（一个调用，不用自己扒 `/AcroForm /XFA`）。
F3 那两句状态栏的话因此不受这一支影响，反而是这一支给它递了一个现成的判据。
要真显示动态 XFA 得换 `pdf_enable_xfa=true` 的构建——那是自己编 PDFium，不在 F5 的范围内（§8 没排它）。

---

## 5. 保真与成本

### 5.1 同一个普通页面，两个库画出来差多少

`page+annot`（不带表单环境）vs `qt-pdf`，整页 793×1123（96 dpi 过 PDF 的 72 dpi，`docs/perf.md` 的几何）：

| 文件 | 墨量 qt / pdfium | 差出来的像素 | 占整页 | 最大单通道差 |
|---|---|---|---|---|
| text.pdf | 1785 / 1786 | 456 | 0.05% | **2** |
| table.pdf | 6789 / 6791 | 722 | 0.08% | 3 |
| colored.pdf | 7129 / 7131 | 1737 | 0.20% | 2 |
| graphic.pdf | 341039 / 341040 | 566 | 0.06% | 2 |
| mixed.pdf | 2904 / 2905 | 726 | 0.08% | 2 |
| picture.pdf | 321438 / 321438 | **0** | 0% | 0 |
| many-pages.pdf | 2257 / 2258 | 578 | 0.06% | 2 |
| scan.pdf | 241743 / 241743 | **0** | 0% | 0 |
| **真书（172 页中文书）** | 84341 / 84419 | **30932** | **4.63%** | **69** |

两行 0 很重要：**位图页（picture、scan）逐像素相同**，所以差异不来自缩放/取整那条路，
而来自**文字与矢量的抗锯齿**（±2/255）。
真书那一行是这一支唯一"可能看得见"的差：4.63% 的像素动了，最大 69/255——
这是中文正文的小字轮廓，量级上足够让一次后端替换在屏幕上"看着不一样"。
**这条必须让眼睛过一遍再定**（§91 的教训：布局与墨量这种事只有图能看出来），
F7 里它对应"拿 `gui-probe` 的 `formpage`/`tabstrip` 那些 PrintWindow 路由在替换前后各取一张图对一遍"，
不是对应一条计数断言。

### 5.2 成本

| 量 | huge.pdf（600 页文字） | scan.pdf（3 页纯图） | 真书（172 页） |
|---|---|---|---|
| 打开 | qt 1.6 / **pdfium 0.3 ms** | 0.3 / 0.1 | **35.3 / 0.3 ms** |
| 遍历全部页尺寸 | 25.0 / **3.2 ms** | 0.1 / 0.0 | 10.4 / **1.8 ms** |
| 渲 40 页（96 dpi） | **0.43** / 0.92 ms/页 | 28.75 / **21.19** ms/页 | 4.48 / **5.14** ms/页 |
| 读 40 页文字 | 0.51 ms（792 字符） | 0.21（0 字符） | **57.81** ms（38646） |
| | **0.31** ms（792） | 0.14（0） | **44.04** ms（38646） |
| 表单层自己的成本（300 dpi 一张） | — | — | page+annot **9.1 ms** → 带 FFL **9.5 ms**，第二次 **8.1 ms** |

**读数旁边要写的三件事**（不然这栏会被当成承诺读）：

1. **单页渲染那两行不是一对一的比较。** 探针每渲一张都 `FPDF_LoadPage`/`FPDF_ClosePage` 一次，
   而 `QPdfDocument` 把页留着；`FPDF_RenderPageBitmap` 里还有一次建/毁位图 + 一次填白。
   真接入时这两笔都会消掉（backend 持页），所以**"文字页慢一倍"现在不能当结论**——
   它现在的身份是"这一支没量到快，且量到的差在这个量级上"。
2. **打开/尺寸遍历那一列是真的**，两侧都是同一套调用（各自打开整个文档），
   而 `documenttab.cpp:1408` 那个"重新加载之前先开一份探针"的路径正好吃这笔收益。
3. **表单层几乎免费**（9.1 → 9.5 ms，300 dpi 一整页；第二次 8.1 ms 说明这不是一次性成本）。
   这一条是 F7 第一步值不值得做的直接依据。

页数两边一致（600 / 600、3 / 3、172 / 172），七份样本第一页的尺寸两边一致（C6 钉的就是这件事），
`FPDF_GetPageSizeByIndexF` 在 huge.pdf 上 600 页全部读成功。**逐页尺寸是否两两相等没量**——
C6 只看第一页，而排版用的是每一页；那一处差异如果存在，表现是"某一页宽了几个像素"，
F7 里应当变成一条遍历全页的比较，不是现在这句话。

### 5.3 线程

`fpdfview.h:11-12` 的原话：**"None of the PDFium APIs are thread-safe. They expect to be called from a
single thread. Barring that, embedders are required to ensure (via a mutex)…"**
Qt 的做法写在 `src/render/pagerenderer.h` 顶部：一把**全局递归锁**，每个 `FPDF_*` 调用都走它，
一个文档对象，不是一个线程一份文档；`MultiThreaded` 只是把渲染挪出画窗口的那条线程，
**没有并行加速**（那段注释还钉着第二条：UI 线程问文档一个问题会被正在跑的那 300 ms 卡住）。

量到的一行：**四个线程、四份各自打开的文档、不加锁 —— ok=4 failed=0，串拍 86.2 ms，墙钟 25.6 ms。**
也就是"各持一份文档"这条路在这台机器上确实并起来了。

**没量到的两半，而且它们正是决策要的那两半：**

* **内存。** "四份文档同时打开"的工作集读数是 `27780 KB -> 27780 KB (0 KB)` —— 这句几乎肯定是假的便宜：
  `FPDF_LoadDocument` 是懒的，四份文档那时**还没被读过**。
  一个"给每个渲染线程一份文档"的方案真正的代价就在这行没量到的数上，**F7 必须先量了再选**。
* **共享一份文档跨线程。** 故意没试（头文件说那是 race；崩掉的探针会把读数一起带走）。
  这一支写这句话而不是去撞它，是因为这条路的结论不是"要不要加锁"，而是"锁是必需的"，
  而后者已经被 C++ API 文档写死了。

**结论**：F7 抄 Qt 的模型——一把全局递归锁 + 一份文档 + 工作线程。
"每线程一份文档"作为一次带内存读数的后续实验，不作为默认。

---

## 6. Document 接口：要动的每一处

review-03 §1.1 说这个抽象目前是名义上的。这一支把它读成数，结果比那句话细：

**接口本身够用**：`src/document/document.h` 15 个 `virtual`，`print/`、`export/` 三条路
已经收的是 `Document &`（`printjob.h:83`、`imageexport.h:49`、`wordexport.h:257`）。
**泄漏只有一处**，但那一处不在接口上：

| 位置 | 现在 | 换成 PDFium 要做的 | 量到的规模 |
|---|---|---|---|
| `src/document/qtpdfbackend.{h,cpp}` | 全部 Qt PDF 调用都在这 | 新 `pdfiumbackend.{h,cpp}`；**头文件注释里那句"换后端应该只是 DocumentTab 一行"是错的，见下** | 49 处 `QPdf*` |
| `src/render/pagerenderer.{h,cpp}` | 封 `QPdfPageRenderer`（`MultiThreaded`、`pageRendered` 信号、`Rotation` 枚举、`RenderFlag`） | 自己写工作线程 + 全局锁 + `FPDF_RenderPageBitmap*`；`PageRenderer` 对外的 `setDocument(QPdfDocument*)` 签名必须消失 | 17 + 12 处 |
| `documenttab.h:43,526` | `std::unique_ptr<QtPdfBackend>` | 改成 `std::unique_ptr<Document>` + 一个工厂（见下） | — |
| `documenttab.cpp:103` | `backend(std::make_unique<QtPdfBackend>())` | 工厂调用 | — |
| `documenttab.cpp:384` | `QtPdfBackend *backendPointer = backend.get();` | 类型名换掉 | — |
| `documenttab.cpp:745` | `renderer->setDocument(backend->pdfDocument())` | **这条就是 §1.1 说的"名义上的"那一点**：`pdfDocument()` 是唯一的 Qt 句柄出口 | 全仓 1 个调用点 |
| `documenttab.cpp:1408` | `QtPdfBackend probe;`（重新加载之前先开一份试） | 同一个工厂，否则"换后端"要动两处具体类型 | — |
| `src/windows/previewhandler.cpp:751` | `std::make_unique<QtPdfBackend>()` | 换 + 见 §6.1 的 DLL 清单 | — |
| `src/windows/thumbnailprovider.cpp:245` | 同上 | 同上 | — |
| `src/document/outline.{h,cpp}`、`textlayer`、`textsearch`、`bookmarkposition` | **不碰 Qt PDF**（自己从 backend 的返回建树/折选择/匹配） | 不用动 | 0 处 |
| `tools/pdfprobe.cpp:57` | 测量工具也用具体类 | 一起换 | 1 处 |

工厂这件事值得单独一句：`qtpdfbackend.h` 顶上写的"swapping it in should be a change to one line in
DocumentTab"现在**已经被证伪**（两个具体类型点 + 一个句柄出口 + 两个壳 DLL + 一根测量工具）。
`Document` 要成为真的抽象，第一步是加 `createDocument()`（谁持有它另说），把上面这五行收成一处。

### 6.1 两颗壳 DLL 与包（这一层有它自己的一套坑，都是量过的）

* **包里的文件数**：`pdfium.dll` 在自己那个目录 + `System32` 之外什么都不允许搜的条件下**能加载**，
  并且带进来的模块只有它自己（**1 个**，量法：`SetDefaultDllDirectories(0x8)` + `LoadLibraryExW`，
  前后各数一遍进程模块）。对照同一把尺子下 `Qt6Pdf.dll` **加载失败**（gle 87）——
  它本来就住在 Qt6Core/Qt6Gui 旁边、从来不是单独用的，所以这条不是说 Qt 坏了，
  而是说**替换在负载上是一换一**，不像 qpdf 那样要带 `qpdf30.dll + z.dll + jpeg62.dll` 三颗（§83 的清单）。
  Qt6Pdf 为什么在这把尺子下失败**没查**，也不需要——我们的部署里它从来不是一个人。
* `CMakeLists.txt` 里 `Qt6::Pdf` 出现在：应用、`document_smoke`、`qpdf_smoke`、`documenteditor_smoke`、
  `render_smoke`、`wordexport_smoke`、`documenttab_smoke`、`mainwindow_smoke`、`xyzpdfpreview`、
  `xyzpdfthumbnail`、`xyzpdf_pdfprobe`。这一串都要跟着改，而且**不止是换个名字**：
  `document_smoke`、`qpdf_smoke`、`documenteditor_smoke`、`encryptprobe`、`watermarkprobe`、`outlineprobe`、
  `perf_bench`、`wordexport_smoke`、`render_smoke` 和两个大 smoke 都是**把 `qtpdfbackend.cpp` 连同被测源码
  一起编**的（`xyzpdf_add_test` 的 SOURCES 列表），换后端就是每张这样的表都要决定
  "这一支测 Qt 路、pdfium 路，还是两条都测"。
* 两颗壳 DLL 是 **`/DELAYLOAD:Qt6Pdf.dll`** 的，且 `previewdependencies.cpp` 按**全路径**加载 Qt
  （`QT_NO_VERSION_TAGGING` 那段论证的由来）。换成 `pdfium.dll` 之后：
  delay-load 的名字要换、那份"我们自己的库"清单要换、`DllHost` 里按包目录找 DLL 这条路由要重验
  （§5.2 那次预览窗格改工作线程渲染、§5.3 拖窗格白纸那两处修复都住在这条路上）。
* `windeployqt` 不再带 `Qt6Pdf.dll`，`packaging/build-package.ps1` 那份**按名字点**的清单要加
  `pdfium.dll`；§96/§97 的教训在这里正面生效：**打包那个 shell 的 PATH 里任何一个同名文件都会进包**
  （那一族量出来过一次：`dxcompiler.dll` 只看名字，连是不是 DLL 都不问）。
  所以这一条不是"加个文件名"，是"那条 strip/校验清单要重跑一遍 payload 对照"。

### 6.2 API 对照表（接口那 15 个方法在 PDFium 里各是谁）

| `Document` 的 | Qt PDF 现在用 | PDFium | 这一支验过吗 |
|---|---|---|---|
| `load` / `lastError` / `pageCount` / `pageSize` | `QPdfDocument::load/status/Error/pageCount/pagePointSize` | `FPDF_LoadDocument(path,password)` / `FPDF_GetLastError` / `FPDF_GetPageCount` / `FPDF_GetPageSizeByIndexF` | **量了**：页数与尺寸逐档对等（C6） |
| `render` | `QPdfDocument::render` + `QPdfDocumentRenderOptions`（`Rotation`/`RenderFlag`/`scaledClipRect`） | `FPDF_RenderPageBitmap`（rotate 0–3 + flags）+ `start_x/y`、`size_x/y` 裁局部 | **量了**：整页与注释层；**没量**：`clip`/`scaledSize` 那条分块渲染的路 |
| `text` / `textRuns` / `characterIndexAt` / `pageSelection` / `hasTextAt` | `getAllText` / `getSelectionAtIndex` / `QPdfSelection` | `FPDFText_LoadPage` + `FPDFText_CountChars` / `GetText` / `CountRects` / `GetRect` / `GetCharBox` | **量了**（F7-E）：字符数 38646 与 Qt 相同；行框在 PDF user space（原点左下，842 高的页上一行字答 top 779.18 / bottom 761.74），所以每个框都要翻 y；`FPDFText_GetCharIndexAtPos` 在 `text.pdf` 那一行的 9 个字缝里 **5 个答 -1**（零容差时 9 个全 -1），行上/行下/行尾之后也一样 —— 这就是点击仍要走二分的原因。断行是两个零宽字符（U+000D、U+000A），落在它所结束那行的末尾 |
| `outline` | `QPdfBookmarkModel`（roles） | `FPDFBookmark_GetFirstChild` / `GetNextSibling` / `GetTitle`（UTF-16LE）/ `GetDest` + `FPDFDest_GetDestPageIndex` | **量了**（F7-E）：`documenteditor_smoke` 的 `nested-outline.pdf` 五条全在树里、层深按 1 起（§63 那两条钉法照旧成立——平读只拿顶层，层深从 0 起会把整棵树压平）。头文件把循环引用交给调用者，所以走的次数与深度都有上限 |
| `links` | `QPdfLinkModel`（roles） | `FPDFLink_Enumerate` / `GetAnnotRect` / `GetDest` / **`FPDFAction_GetURIPath`**（8111 这份头文件里没有 `FPDFLink_GetURIPath` 这个调用），UTF-8 字节串 | **量了**（F7-E）：`tests/fixtures/links.pdf` 一条 URI、一条 `/XYZ 0 700 null`，页面与位置都读得回来；`/XYZ` 的 null zoom 读成 `hasZoomVal=false`，不是 0 |
| `metadata` | `QPdfDocument::metaData`，八个字段（title/author/subject/keywords/producer/creator/creation/mod，`qtpdfbackend.cpp:608-616`） | `FPDF_GetMetaText(doc, "Title" …)` 同名八个键 | **F7-E 量了**，并且改了本表原来那一行写的 `"/Title"`：标签是**字典自己的键名，不带斜杠**。在带 `/Info` 的 `tests/fixtures/info.pdf` 上，`Title` 读回 42 字节（"XYZPDF Fixture Title"），`/Title` 对同一个文件答 2 字节（空串）。日期是 PDF 自己格式的字符串，PDFium 不解析，要自己解（§14.3.3 表 231） |
| 加密 | `setPassword` + `Status` | `FPDF_LoadDocument` 的 password 参数；`FPDF_GetDocUserPermissions` | **没量**（`encryptprobe` 是另一套问题，问的是 qpdf） |
| 表单 | **没有** | `FPDFDOC_InitFormFillEnvironment` / `FPDF_FFLDraw` / `FORM_On*` / `FPDFAnnot_*` / `FPDFPage_Flatten` | **量了**（§4） |

**"这一支没量到"的那几行就是 F7 的探针清单**——照 review-03 的规矩：没量过的不写成结论。

---

## 7. 表单的两步：第一步是什么

**第一步（F7 里应该单独成一片）：只画，不接收任何输入。**

* 打开文档 → `FPDFDOC_InitFormFillEnvironment`；每份文档一个 `FPDF_FORMFILLINFO`，
  **它必须活到句柄关掉为止**（头文件明说）；
* 每次渲染：`FPDF_RenderPageBitmap(page, …)` 之后 `FPDF_FFLDraw(form, bitmap, page, …)`；
  回调一个都不必填（本探针一个都没填，widget 照样画出来）；
* 页加载/卸载配 `FORM_OnAfterLoadPage` / `FORM_OnBeforeClosePage`（本探针这么做了）；
* 撤掉两句用户可见的话：§100 打开时那句"本版不显示也不编辑其中的内容"、§109 C 打印框那句。
  **这两句撤掉要重打包才进得了装上的那一份**（§16 那笔账现在欠到 §111）。
* 成本：§5.2 那行 —— 一页 9.1 → 9.5 ms（300 dpi）。

第一步之后仍然要**明说**的：字段里的值是 PDFium 造的面皮，字体、字号、对齐按它自己的规则来；
`/NeedAppearances` 我们不应答；XFA 那份第二副本仍然不显示（§4.3）。

**第二步（填）：这一支只量到"它比第一步大得多"，没有做拆分以外的任何承诺。**

量到的三点决定形状：

1. `FPDFAnnot_SetStringValue` 被**接受**、`/V` 读得回 `[typed-by-probe]`，
   但下一帧**框内 0 个像素变化** —— 值进去了，画面不更新。
2. `FPDFPage_Flatten(FLAT_PRINT)` 返回 **1=成功**，之后整页差出 **26271** 像素 ——
   把当前 widget 烘进页面内容流是可行的，这也是"打印一份填好的表单纸上不该空白"那条
   （§94 抬上去的那个问题）现成的解。
3. 键盘/鼠标那一族 API 全在（`FORM_OnFocus/OnChar/OnKeyDown/OnLButtonDown/OnMouseMove/OnMouseWheel`、
   `FORM_GetFocusedAnnot`、`FORM_ForceToKillFocus`、`FORM_CanUndo/Undo/Redo`），
   意味着"填"要接的是**焦点、滚动区域、IME、tab 顺序、撤销栈**这一整套，
   外加 `FPDF_FORMFILLINFO` 里那几个真正需要应答的回调。
   再加上保存回写就撞上"永不写用户的 PDF 文件"那条 v1 规矩（AGENTS.md）与 §79 的锁 ——
   **那是 F8 的问题，不是 F7 的**。

所以：**第一步 = 画；第二步 = 填；中间还有一档 = 打印/导出前 flatten**（很便宜，
但它是行为改变：烘进去之后别人再也改不了那些字段）。这一档要不要，等一句话。

### 7.1 那一档的回答（F7-F，2026-10-23）

**不做 flatten。** handoff-03 F7 第 5 条已经把这句话拍了：打印与导出走的是同一条
`Document::render()`，表单层画在页面位图上，所以字段的答案在纸上、在导出的图里都有，
不需要把它们烘进内容流；保存出来的文件里字段仍然是字段（填是第四阶段的事，写回是 F8 的事）。

量出来支持这个决定的两件事：

* `FPDFPage_Flatten` 在这台机器上返回 1，之后整页差出 26271 像素（§4.1 那格）——
  也就是说 flatten 能做成，但它改的是**文件**，不是画面；一旦做了，别人拿这份文件
  再也改不了那些字段。这一条不是性能问题，是"我们有没有权利替读者决定"的问题。
* 打印路径另加了一个旗标 `FPDF_PRINTING`（`RenderOptions::printing`，F7-F 起
  `printjob.cpp` 设它）。**量下来它在今天什么都不改变**：A4/300 dpi（2480x3508）下
  `text.pdf`、`table.pdf`、`graphic.pdf`、`scan.pdf` 四份，加与不加**逐像素相同**
  （差 0 个像素）。所以它不是 flatten 的替代品，只是一个名字对了的旗标；
  "它是惰性的"这件事被钉在 `tests/documenteditor_smoke.cpp` 的 §94 那一节里，
  两条控制都验过能红（一条把旗标变成真会改画面、一条让旗标把表单层换掉）。

没量的：真打印机驱动拿到这张图之后做什么；`FPDF_PRINTING` 在别的 PDFium 版本上是否还惰性。

---

## 8. 取代还是并存（体积与代价）

`§83/§84` 记的现在这版包：**msix 17.5 MB，解包 40.5 MB / 67 个文件**。两颗引擎的真实字节：

| | `pdfium.dll` | `Qt6Pdf.dll` |
|---|---|---|
| 磁盘 | 7,586,816（≈7.2 MiB） | 4,620,600（≈4.4 MiB） |
| deflate-9 | 3,729,012 | 2,459,460 |

| 方案 | 包里净变化（未压缩） | 估计 msix 变化 | 代码面 |
|---|---|---|---|
| **并存**（`Qt6::Pdf` 继续链接） | **+7.59 MB** | ≈ +3.6 MB | 两套引擎、两套锁、两份缓存；同一个文件在两条路上渲染差多少要**长期**回答 |
| **取代** | **+2.97 MB**（+7.59 − 4.62） | ≈ +1.27 MB | §6 那张表一次付清 |

**并存唯一的好处是"能回退"**。这个好处可以用另一个更便宜的方式拿到：一个**只在构建期存在**的开关
（`XYZPDF_DOCUMENT_BACKEND=qt|pdfium`），两条实现都编、但**负载里只放当前那一个引擎**。
这样"回退"是改一次配置，不是带上 7.59 MB 上线。

**所以建议：取代，且并存只作为开发期状态存在（不进包）。**
包体积按 §83 那条规矩重打包重测一次才算数——现在这两个 ms 级的估计是 deflate 算的，**不是 msix 量的**。

---

## 9. 许可证与第三方声明

* **PDFium 本体：BSD-3-Clause**（`Resources/licenses/pdfium.txt` 已经在仓库里，是从 Qt 那条抄的）。
  换成我们自己分发 `pdfium.dll` 之后，`src/core/thirdpartynotices.cpp` 里那一行
  （现在写着 `（编入 Qt6Pdf.dll）`）必须改成点名 `pdfium.dll`，并加一行 **Qt 那一条的 DLL 清单里
  不再有 `Qt6Pdf.dll`**。那两句是用户可见文字 → 走 `tr()` → 目录要跟着填（AGENTS.md 那条规矩）。
* **这份发布自带的 18 份第三方许可证**（`licenses/` 目录，随 DLL 一起给）：
  `pdfium`(BSD-3)、`abseil`、`agg23`、`dragonbox`(Apache-2.0-LLVM / Boost)、`fast_float`、
  `freetype`(FTL)、`harfbuzz`、`highway`、`icu`、`lcms`、`libjpeg_turbo`(IJG + BSD-3 两份)、
  `libopenjpeg`、`libpng`、`llvm-libc`、`simdutf`、`zlib`。
  这一串是 F5 让回答的那个"PDFium 带进来的组件各自要加什么声明"的**清单来源**——
  注意 **FreeType 是 FTL/GPL-2 双许可**，随包分发时选 FTL 那句话要写进声明里。
* **仓库侧的落法**：新增 `Resources/licenses/pdfium/*.txt`（18 份）+ `resources.qrc` 条目 +
  `thirdpartynotices.cpp` 的行。现有 `Component` 结构一行只挂**一份**许可证文本
  （`licenceResource` 是单个路径），所以要么把 PDFium 那一行的文本合成一份
  （"BSD-3-Clause + 下列第三方"），要么给结构加"多份文本"。这是个界面决定（属性框/关于框那条路），
  F7 里要拍。**建议：合成一份，结构不动**——改动面小，且现有 `thirdpartydialog` 不用重做。
* **`bblanchon/pdfium-binaries` 自己的脚本是 MIT**，我们不分发它的脚本，只分发它构建出来的
  PDFium 二进制；声明里要不要提这个构建来源，属于"上架元数据"那一族，等他一句话。

---

## 10. 风险，与迁移顺序

| # | 风险 | 证据 | 缓解 |
|---|---|---|---|
| R1 | **屏幕上的字看起来不一样**：真书 4.6% 像素差、最大 69/255 | §5.1（量过） | F7 里"替换前后各取一张 PrintWindow 图对一遍"是硬步骤；不许只靠计数 |
| R2 | **单页渲染可能变慢**（文字页 0.43 → 0.92 ms/页）——但这一行有已知 confound | §5.2 | 先做 §6 的页缓存/持页改动，再重测 `perf_bench`；`docs/perf.md` 的"换了 Qt/PDFium 版本就更新这里"这条现在就生效 |
| R3 | **锁的响应面**：一条 300 ms 的扫描页挡住 UI 线程问文档的任何问题（§5.3 已写进 `document.h` 顶部注释） | `pagerenderer.h` 那段 + perf.md 的 30 ms/页 | 抄 Qt 的模型并把这条注释一起搬（AGENTS.md：连注释一起搬） |
| R4 | **"每线程一份文档"的内存没量** | §5.3 那行 0 KB 是假便宜 | 不许在量之前把它当方案；`formpage` 那种从设备侧读内存的路由可以复用 |
| R5 | **壳的两颗 DLL**：delay-load 名单、按全路径加载、`DllHost` 里路由、`QT_NO_VERSION_TAGGING` 那段论证 | §6.1 | F7 单独一片；`previewhandler_smoke`/`thumbnailprovider_smoke` 是现成的能红断言 |
| R6 | **包内容随打包 shell 变化**，加一颗 DLL 会放大这件事 | §96/§97 量过的机制 | `build-package.ps1` 的名字清单 + `verify-package.ps1` 的 payload 对照重跑 |
| R7 | **文件句柄**：PDFium 按路径自己开文件且不带 `FILE_SHARE_DELETE`（PLAN.md:906 量过，预览窗格因此改成"开副本"） | 那条记录 + `documenttab.cpp:1408` 那份"先开一份试"的探针 | 阅读路径现在也走同一个库 → "别人删不掉/覆盖不了开着的文件"那一族（§3.4/§79）会从推论变成实测；**这条要在 F7 之前决定策略，不是之后** |
| R8 | **XFA 仍然不能显示**（`LoadXFA=false`），F3 那两句照旧要写 | §4.3（量过） | 不列为 F7 的风险，列为 F7 的**边界** |
| R9 | **版本漂移**：预编译发布每周动一次；我们钉的是 `chromium/8111` + 一个 sha256 | §2 | 钉版本 + 记录哈希（本支已做）；升级要重跑探针与 §5.1 的图 |

### F7 可以照着做的拆分

顺序的原则只有一条：**每一步结束后门禁都能全绿，且屏幕上那句用户可见的话不会在没验证前被撤掉。**

1. **F7-A 把 `Document` 变成真的抽象**（不引入 PDFium）。
   `createDocument()` 工厂收掉 §6 表里那五行具体类型；`PageRenderer` 不再接 `QPdfDocument *`，
   改接 `Document &` + 一个"渲一页"的请求对象（把 `RenderOptions` 交给它）。
   验收：门禁 26/0 一字不改；`grep -c QPdf documenttab.*` 从 1 变 0；`pdfDocument()` 可以消失了。
2. **F7-B `PdfiumBackend` 落地，只做 `render/text/pageCount/pageSize`**，构建期开关选后端，
   负载里只放一个引擎。默认仍然是 Qt。
   验收：同一份文件两条路逐档对数（C6 那种），§5.1 的表在 `perf_bench` 里长成一列。
3. **F7-C 表单层**：`FPDFDOC_InitFormFillEnvironment` + `FPDF_FFLDraw`（§7 的第一步）。
   验收：`gui-probe -Mode formpage` 的表**必须从"widget 0 像素"翻成"widget 满框"**——
   那是这一支全部的动机，也是它的红绿判据；控制是关掉 FFL 那一行再跑一次。
   然后才撤 §100/§109 那两句，随一次**重打包 + 重装**（欠着的 §16 账一起对上）。
4. **F7-D 两颗壳 DLL 换过去**（§6.1），`previewhandler_smoke` / `thumbnailprovider_smoke` 带着能红的断言。
5. **F7-E 其余 Qt PDF 调用**（outline/links/metadata/selection）迁完，`Qt6::Pdf` 从链接列表消失，
   `windeployqt`/打包清单跟着改。
6. **F7-F 打印/导出的 flatten 决定**（§7 中间那一档）+ 就地保存的边界（那是 F8）。

**每一片之后**：`python tools/build_all_tests.py` 全绿 + 目录检查 + 一次真窗口读数
（脚本对并行分支要求传 `-Exe <这个 worktree>\...\build-local-release\xyzpdf.exe`）。

---

## 11. 这一支没做 / 没测（说明白，别让它读起来像已经做完）

* 没改产品代码、没改目录、没改打包脚本；**没重打包、没安装、没 push、没合 main**。
* 探针**没在真窗口里跑过**（它不需要窗口）；`gui-probe` 的 20 种模式这一支一次都没跑（那是 F4 的活）。
* **没量**：局部渲染（`clip`/`scaledSize`）在 PDFium 上的等价做法；文字选择与命中；书签树；链接；
  元数据八个字段；口令文档；"每线程一份文档"的内存；跨多次 PDFium 版本的渲染稳定性；
  真扫描件（`scan.pdf` 仍是合成的，perf.md 第 4 条原样有效）；500+ 页真出版物（仍不存在）。
* **没做**：把 PDFium 放进版本库/CI；写 `XYZPDF_DOCUMENT_BACKEND` 那根开关（属于 F7-B）。
* §2 那两个 msix 估计是 deflate 算的，**不是打包量出来的**，写在这儿是为了给个体量级，不充当结论。
* `tools/pdfium_probe_*.py` 三个脚本里的路径是**这台机器的绝对路径**（`H:\test\XYZPDF-wt\third-party\…`），
  跟 `gui-probe.ps1` 现在的习惯一致，但换机器要改；没有把它们参数化是因为参数化会让"哪份字节被量了"
  这句话又要靠记忆——探针自己会印 sha256。
