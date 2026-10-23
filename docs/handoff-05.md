# 第五批：表单补全与上屏幕、暂存文件不留在读者文件夹（2026-10-23）

第四批（G1–G4）已合进 main，见 `docs/store-submission.md` §118–§119。表单填得进、存得回，但只在无头测试里验过；
组合框下拉、列表框多选没接；暂存文件在程序被杀时会留在读者的文件夹里。这一批三件**同时发**：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| H1 `form-complete` | 组合框下拉、列表框多选与滚轮；表单代码不再编进两颗壳 DLL | `pdfiumbackend*`、`pageview`、`CMakeLists.txt` |
| H2 `gui-probe-forms` | 表单填写第一次上屏幕：点字段、打字、Tab、复选框、中文输入法、保存后重开 | 只改 `tools/gui-probe.ps1`（必要时一个只读的测量工具） |
| H3 `staging-cleanup` | 程序被杀后留下的隐藏暂存文件，下次由程序自己按记录的路径收走 | `documenteditor`、`pdfiumbackend` 暂存那一处、`appsettings`、`main.cpp` |

H4（已签名文档：只改字段时走增量保存、保存前说明会让几个签名失效）要动保存那条路的同一批函数，**等 H1、H3 合并后再写说明**。

**基线**：门禁 **27 passed / 0 failed**，目录 **692 条**，numerus **53**。

---

## 每个分支都要先做的：环境准备与规矩

与 `docs/handoff-04.md` 的同名一节**完全相同**（两个 junction：`vcpkg_installed`、`pdfium_installed`；从零构建；不删
`CMakeCache.txt`；控制跑按 sha256 还原；门禁全绿；`tr()` / `%n` / `QT_TRANSLATE_NOOP`；目录写成已完成并逐条核；
写用户文件只有「保存」一条路；不改日志与规矩文档；不打包、不安装、不 push、不合并；并行分支 gui-probe 传自己的 `-Exe`；
锚点；署名行；最终报告的内容）。先读那一节，再读本节以下自己的那一件，基线数字以本文为准。

---

## H1. 表单补全　分支：`form-complete`

**背景**：G1 的报告明说没接：组合框的下拉列表、列表框的多选、滚轮在字段内滚列表；IME 候选框跟字段矩形而不是插入点
（这份头文件不给插入点，接受）。另：`pdfiumbackend.cpp` 同时编进预览与缩略图两颗壳 DLL，G1 的表单代码跟着进去，
两颗各大了约 50 KB（§119），而它们只画不填。

**要做**
1. **组合框**：取焦后点下拉按钮或 Alt+↓ 打开选项列表（PDFium 的 `FORM_*` 与 `FFI_*` 回调里有它要的那几个；
   量到 PDFium 自己画下拉、还是要我们弹一个 Qt 列表，按量到的做），选中一项写进字段、星号出现、保存后 qpdf 读回 `/V`。
   可编辑组合框（`/Ff` 的 Edit 位）还能直接打字。
2. **列表框**：单击选一项、Ctrl/Shift 多选（`/Ff` 的 MultiSelect 位），保存后 `/V` 是数组；滚轮在列表框有焦点时滚它自己，
   没焦点时照旧滚页面。
3. **壳 DLL 瘦身**：把只有填写才用的代码（输入路由、`FORM_On*`、暂存保存那一段）从 `pdfiumbackend.cpp` 拆进一个只编进应用的文件；
   壳 DLL 仍然**画**字段（`FFLDraw` 那一段留在共用的那半）。量：两颗壳 DLL 的字节数回到 §117 附近；`previewhandler_smoke` /
   `thumbnailprovider_smoke` 里字段照样画得出来（那两条现成的能红断言）。
4. 生成器（`XYZPDF_FORM_SAMPLE_DIR`）不够就加：组合框（含可编辑的）、多选列表框。

**测试**：`pdfiumbackend_smoke` 测纯后端；`documenttab_smoke` 在 `    // ---- night reading ----` 之前测整条路
（取焦 → 选 → 星号 → 保存 → qpdf 读回 → 重开画出新值）；`mainwindow_smoke` 不需要就不改。每条配控制跑。

---

## H2. 表单上屏幕　分支：`gui-probe-forms`

**背景**：G1 的填写、Tab、撤销、中文输入法**一次都没在屏幕上出现过**（§119）。G2 已经立了保存护栏（会按保存的模式只开自己复制的那份）。

**要做**（只改 `xyzpdf/tools/gui-probe.ps1`；需要一个读字段值的办法时，可以给 `tools/pdfprobe.cpp` 加一个**只读**的
`--fields` 子命令，不许碰产品代码）
1. `-Mode formfill`：护栏复制样本（`field-flat.pdf` 一类，`XYZPDF_FORM_SAMPLE_DIR` 现造）→ 在字段矩形里真点一下（坐标从页面
   →屏幕换算，或从 `PrintWindow` 图里找字段的外观框）→ `SendInput` 打 ASCII 字 → 标题出现星号 → 字段那一片的墨量变了
   （`PrintWindow`，几何判据）→ Ctrl+S → 保存句、星号消失 → 读回副本的字段值等于打进去的字（`--fields` 或等价的只读办法）。
   `-Control`：不打字直接 Ctrl+S → 没有保存句 → exit 1。中英各一遍。
2. `-Mode formtab`：两个字段的样本，点第一个 → Tab → 打字 → 字进的是第二个（读回两个字段的值）。Shift+Tab 回来。
   `-Control`：不按 Tab → 字进第一个 → exit 1。
3. `-Mode formcheck`：复选框点一下 → 墨量变、星号出现 → 保存 → 读回 `/V` 变了；再点一下回来。
4. `-Mode formime`：**真走输入法**：`ImmGetContext` + `ImmSetCompositionStringW` + `ImmNotifyIME(NI_COMPOSITIONSTR, CPS_COMPLETE)`
   把"中文"作为提交串交给窗口（这台机器的用户语言是中文，见 §118）；读回字段值是"中文"。做不到（没有输入法上下文、
   提交串没进来）就把每一步的返回值印出来并 exit 1，**不要退回成 `KEYEVENTF_UNICODE` 冒充**（那走的是逐字 `WM_CHAR`，不是 G1 那条
   `FORM_ReplaceSelection` 路）—— 两条路都量一次、各印结果更好。
5. 四个新模式都会改字段或按保存：一律加进护栏的 `$script:saveModes`（G2 立的那张表），只开护栏复制的副本。
6. 早退全走 `Bail`；`.ps1` 非 ASCII 0；每次传 `-Exe H:\test\XYZPDF-wt\gui-probe-forms\xyzpdf\build-local-release\xyzpdf.exe`。

**报告**：每个模式正例与控制的退出码、读到的原文与字段值（中英）、墨量前后、每轮前后的 sha256；屏幕上看见的产品缺陷单列。

---

## H3. 暂存文件不留在读者的文件夹里　分支：`staging-cleanup`

**背景**：就地保存与表单保存都在**原文件旁边**写隐藏的暂存文件（`xyzpdf-save-*.pdf`、`xyzpdf-form-*.pdf`，同卷才能原子替换），
写完即换或即删；但程序在这中间被杀（崩溃、任务管理器、断电）就会留下一颗隐藏文件在读者的文件夹里（F8、G1 的报告都写了"没新解"）。

**要做**
1. **只收我们自己记过的路径，按全路径，不按名字模式扫**：每写一份暂存文件之前把它的全路径（加创建时间与这一进程的 PID）记进设置
   （`AppSettings` 里一个列表），换上去或删掉之后从列表里去掉。读者文件夹里任何**没被记过**的文件，名字再像也不碰。
2. 程序启动时（`main.cpp`，单实例那道门之后）检查列表：路径还在、文件仍是隐藏、记它的那个 PID 已经不在（或不再是本程序）→ 删掉并
   从列表去掉；路径不在了 → 只从列表去掉；PID 还活着（另一个实例正在保存）→ 不动。删不掉（被占用、权限）→ 留在列表里下次再试，
   不弹框、不打扰读者。
3. 这条规矩写进代码注释："永不写用户的 PDF 文件"之外，**这是本程序唯一会删读者文件夹里文件的地方，而且只删自己记过的那几个路径**。
4. 测试（临时目录的副本上）：模拟"写了暂存、没换就被杀"（列表里留一条、文件在、PID 是一个已经结束的进程）→ 启动收尾删掉它；
   同名但没记过的文件 → 不动；PID 还活着 → 不动；文件被别的句柄占住 → 留着、下次再试。每条配控制跑。
   测试锚点：`documenttab_smoke` 在 `    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----` 之前；
   需要的话 `mainwindow_smoke` 在 `    // ---- a second document joins the first ----` 之前；纯设置那部分进 `appsettings_smoke`。
5. 暂存写在 `pdfiumbackend.cpp` 的那一处（表单保存的第一段）也要记进同一个列表；只动那一个函数，H1 也在改这个文件，不要顺手改别处。

---

## 合并方式（主会话做）

H2 先合（只动 harness），H3 其次，H1 最后。合并后重打包重装，在装上的那份上跑 H2 的四个模式（`-Installed`，护栏自己把副本交给 picker）。
然后写 H4（已签名文档）的说明。
