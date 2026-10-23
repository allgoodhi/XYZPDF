# 第四批：表单填写、保存上屏幕、识别准确度、助记符查重（2026-10-23）

第三批（F6–F10）已全部合进 main，经过见 `docs/store-submission.md` §114–§117。现在 PDFium 是唯一的渲染引擎，
表单字段画得出来但填不了；就地保存能用但没在屏幕上按过；识别器把 text.pdf 那行的 "X" 读成 ")("。
这一批四件，**同时发**，改的文件基本不重叠：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| G1 `form-filling` | 表单填写（`pdfium-plan.md` §7 第二步）+ 填好的值经保存写回 | `pdfiumbackend`、`pageview`、`documenttab`、`documenteditor` |
| G2 `gui-probe-save` | 就地保存与关窗三答框上屏幕；`saverename` 改成另存为 | 只改 `tools/gui-probe.ps1` |
| G3 `ocr-accuracy` | 查清并修好识别读错字形 | `src/ocr/`、`src/services/ocrsession.*` |
| G4 `mnemonic-check` | 每个菜单里助记字母不重复，变成一条能红的测试 | 只改 `tests/mainwindow_smoke.cpp`（必要时目录） |

**基线**：门禁 **27 passed / 0 failed**，目录 **684 条**，numerus **52**。

---

## 每个分支都要先做的：环境准备与规矩

（这一节自包含，替代 `handoff-02.md` 的同名一节。）

**环境准备**（新 worktree 里没有构建树，也没有 `vcpkg_installed`、`pdfium_installed`，三者都在 `.gitignore` 里）：
1. 在 worktree 根目录（含 `xyzpdf/` 和 `docs/` 的那一层）执行两条：
   `cmd /c mklink /J vcpkg_installed H:\test\XYZPDF\vcpkg_installed`
   `cmd /c mklink /J pdfium_installed H:\test\XYZPDF\pdfium_installed`
   —— 主检出里都装好了，只读借用。**删除 worktree 之前先单独删掉这两个 junction**（只删链接）。
   junction 不可用时：qpdf 走 `H:\test\vcpkg\vcpkg.exe install --triplet x64-windows`；PDFium 走
   `python xyzpdf/tools/fetch_pdfium.py --from H:\test\XYZPDF-wt\third-party\pdfium-8111`（**不要 `--download`**）。
2. `cd xyzpdf && python tools/build_all_tests.py`：第一次从零构建要几分钟。必须是上面的基线，不是就先停下报告。
3. 不要删构建树的 `CMakeCache.txt`。

**规矩**（`AGENTS.md` 里都有，这几条最容易出事）：
- 每条新断言配一次**控制跑**：临时把产品代码改坏，证明那条断言会红，再按 sha256 逐字节还原并重编。控制没红就是没在测。
- 收尾 `python tools/build_all_tests.py` 必须全绿。编不过就明说"未编译验证"。
- 用户可见文字走 `tr()`；带计数的走 `%n` + `tr(单数, 复数, n)` 并在 `tests/i18n_smoke.cpp` 加探针；翻译助手要么叫 `tr`
  要么调用处包 `QT_TRANSLATE_NOOP`（lupdate 只认名字）。新增的每句都在英文目录里填好、**写成已完成**（不要留
  `type="unfinished"`）；`update_translations` 之后新增条目逐条用眼睛核（same-text heuristic 会填别的上下文的词）。
  英文助记符写在词内，同一菜单里不重复。
- **写用户文件只有「保存」那一条路**（AGENTS.md）；测试只在临时目录的副本上做，绝不碰 `tests/fixtures` 与用户的文件。
- **不改** `docs/store-submission.md`、`AGENTS.md`、`docs/review-03.md`、`docs/handoff-*.md`；**不打包、不安装、
  不用 gui-probe 的 `-Installed`；不 push，不合并到 main**。并行分支的 gui-probe 一律传
  `-Exe H:\test\XYZPDF-wt\<分支>\xyzpdf\build-local-release\xyzpdf.exe`。
- 测试块插在指定锚点前，不插在别处。提交信息末尾一行：`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`

**最终报告**写在最后一个提交的说明里：做了什么（文件 + 增删行数）、量到的数、每条控制红了哪几条、门禁结果、
没做/没测的（明说），以及新增界面文字的「源文 → 英文」对照表。

---

## G1. 表单填写　分支：`form-filling`

**背景**：F7 接上了 PDFium 的表单环境，但只画（`FPDF_FFLDraw`），不接收任何输入（`docs/pdfium-plan.md` §7、§7.1）。
F5 量过：`FPDFAnnot_SetStringValue` 被接受但下一帧 0 像素变化 —— "填"要走 `FORM_*` 那一整套，不是改注释字典。

**要做**
1. **输入**：选择工具与手形工具下，指针在字段上时光标变成字段要的样子（`FORM_OnMouseMove` + `FFI_SetCursor`）；
   单击字段获得焦点（`FORM_OnLButtonDown/Up`），键盘进字段（`FORM_OnKeyDown/OnChar`），Tab / Shift+Tab 在字段间移动，
   Esc 或点页面空白处让出焦点（`FORM_ForceToKillFocus`）。设备坐标 ↔ 页面坐标一律用 `FPDF_DeviceToPage` / `PageToDevice`，
   带上读者旋转与缩放。字段没有焦点时，键盘快捷键照旧归应用。
2. **中文输入法**：`QInputMethodEvent` 的提交串走 `FORM_ReplaceSelection`（一个字一个字发 `OnChar` 会把组合串打碎）；
   候选框位置跟着字段里的插入点（`inputMethodQuery(Qt::ImCursorRectangle)`）。中英两种输入都要有测试。
3. **重画**：`FFI_Invalidate` 回调把那块区域换算到视图、让那一页的渲染作废并重画（缓存的代际照现有做法），只重画受影响的那一带；
   量一下在 `scan.pdf` 那种重页面上打一个字的延迟（UI 线程要拿同一把全局锁，`pagerenderer.h` 那段注释说的就是这个）。
4. **撤销**：字段有焦点时「撤销/重做」走 `FORM_Undo/Redo`（`FORM_CanUndo` 决定灰不灰），没焦点时照旧走编辑列表。
5. **支持面**：文本框、复选框、单选、组合框、列表框。按钮与签名字段只显示不交互。**这份 PDFium 没有 V8**：字段的计算、
   校验、格式化脚本一律不跑 —— 打开带脚本的表单时状态栏说一句（`setStatusMessage`，有时限），话要短。
   XFA 照旧不显示（§112 那两句不动）。
6. **保存写回**（这一步的设计已经定了，照做，量到不行再停下来报告）：
   - 有字段改过 → 标签算"有未保存的修改"（标题星号、关窗三答框都认它）；
   - 保存时先用 `FPDF_SaveAsCopy(…, FPDF_NO_INCREMENTAL)` 把 PDFium 手里的文档写成**同目录的第一份暂存文件**；
     如果还有待保存的结构修改（删页、旋转……），`DocumentEditor` 以这份暂存文件为来源再写第二份；验证（qpdf 重开、页数）
     之后走 F8 那条 `ReplaceFileW` 路。只有字段改动时第一份验证过就直接替换。另存为同理，目标换成新路径。
   - **加密文档**先量：`FPDF_SaveAsCopy` 写出来的是否保留原来的加密与权限。保留 → 照常；不保留 → 带口令的文档上
     保存字段改动时拒绝并说清楚（给另存为不能绕过这条，因为那会写出一份没加密的副本）。量到的写进报告。
   - 写回之后用 qpdf 读 `/V` 断言是填进去的值；重新打开后屏幕上（`render`）字段里画的是新值。
7. **AGENTS 那条规矩**："除了「保存」这一条路，永不写用户的 PDF 文件" —— 填写本身只改内存里的文档，**不许**自动保存、
   不许在关标签时不问就写。

**测试**：`pdfiumbackend_smoke` 里测纯后端（焦点、输入、`/V`、撤销、`FFI_Invalidate` 给出的矩形）；
`documenttab_smoke` 在 `    // ---- night reading ----` 之前测整条路（点字段 → 打字 → 星号 → 保存 → qpdf 读回 → 重开画出新值；
IME 提交串；Tab 顺序；撤销分流）；`mainwindow_smoke` 在 `    // ---- fullscreen and presentation ----` 之前测
关窗三答框认字段改动。样本用 `XYZPDF_FORM_SAMPLE_DIR` 的生成器（不够就在生成器里加：复选框、单选组、组合框）。
每条配控制跑。

---

## G2. 保存与三答框上屏幕　分支：`gui-probe-save`

**背景**：F8 的就地保存与关窗三答框**一次都没在屏幕上出现过**（§116）。现有 `saverename` 模式按 Ctrl+S 期待一个保存框，
而现在 Ctrl+S 会**直接写它打开的那份文件** —— 所以它现在是危险的，§116 没敢跑它。

**要做**（只改 `xyzpdf/tools/gui-probe.ps1`；不改产品代码）
1. **一道硬护栏，先写**：任何会按 Ctrl+S / Ctrl+Shift+S、或会点三答框里「保存」的模式，只许在它**自己复制出来的文件**上跑：
   探针把 `-Doc` 复制进 `%TEMP%\xyzpdf-gui-probe\save-<随机>\`，打开的是副本；`-Doc` 原件跑前跑后各算一次 sha256，
   不同就 `Bail`。`-Doc` 落在 `tests\fixtures` 下照样允许（读它、复制它），但绝不打开原件。
2. `-Mode saveinplace`：副本 → 旋转第 1 页（旋转框）→ 标题带星号 → Ctrl+S → 状态栏读到"已保存到 …"、标题星号消失、
   副本 sha256 变了、`-Doc` 原件 sha256 没变。`-Control`：跳过 Ctrl+S → 副本 sha 不变 → exit 1。中英各一遍。
3. `-Mode closeprompt`：副本做一处修改 → 点窗口关闭 → 读到三答框（类名 + 两层找；按钮三个）→ 依次三轮：
   取消（窗口还在、副本 sha 不变）、不保存（窗口走了、副本 sha 不变）、保存（窗口走了、副本 sha 变了）。
   `-Control`：不做修改就关 → 期望读到框 → exit 1。
4. `saverename` 改成走「另存为…」（Ctrl+Shift+S），名字可以改成 `saveas`（保留旧名作别名也行）；它写出的新文件也在探针自己的目录里，
   跑完断言原件与副本都没被改。AGENTS 里提到 `saverename` 的那几句由主会话改。
5. 早退全走 `Bail`；`.ps1` 改完非 ASCII 字节 0；每次传 `-Exe H:\test\XYZPDF-wt\gui-probe-save\xyzpdf\build-local-release\xyzpdf.exe`。

**报告**：每个模式正例与控制的退出码、读到的原文（中英）、每轮前后的 sha256；屏幕上看见的任何产品缺陷单列。

---

## G3. 识别准确度　分支：`ocr-accuracy`

**背景**：换成 PDFium 之后，这台机器上 Windows 识别器把 `tests/fixtures/text.pdf` 第一行 "XYZPDF one" 读成 ")(YZPDF one"
（§116）。主会话已经排除了两样：渲染是干净的（150%/400%/灰度/不平滑都清楚），给裁图四周加白边没用。F7 另量过放大到
1600 px 也一样。所以问题在"交给识别器的是什么、交给哪个识别器"。

**要做**
1. **先量，再改**：`src/ocr/ocrengine.cpp` 的 `pickEngine()` 先用用户配置的语言（这台机器很可能是简体中文在前），
   再退到第一个已装的语言包。把同一张裁图分别交给每个已装语言的引擎（`OcrEngine::AvailableRecognizerLanguages()`），
   以及几种预处理（灰度、二值化、不同放大倍数、不同抗锯齿），印一张表：哪一种组合读对了。
2. 按量到的改**产品**。候选方向（以量到的为准，不要先选）：按裁图内容挑引擎（例如先用中文引擎，结果里没有中文字、
   拉丁字符可疑时再用英文引擎比一次）、在设置里加"识别语言"（默认跟随系统）、或某种预处理。任何新的用户可见文字照规矩走 `tr()`。
3. **测试**：`documenttab_smoke` 里 F7 改松的那条（"回来的是那一行里的一段"）与 F10 那块（两次读回相同），恢复成**逐字读出
   "XYZPDF one"**；另加一条中文：用 `QPainter` 拿系统字体画一张中文图直接交给 `Ocr::recognize`，断言读对 —— 确保为拉丁字母
   做的修改没把中文弄坏。新块插在 `documenttab_smoke` 的 `    // ---- the region snapshot ----` 之前。控制：撤掉修改，
   两条都要能红。
4. 识别器是这台机器的 Windows 组件：报告里写明这台机器装了哪些识别语言、用户配置语言的顺序，以及在别的机器上可能不同的地方。

---

## G4. 菜单助记字母查重　分支：`mnemonic-check`

**背景**：§116 合并时眼睛看出英文「文档」菜单里 `&Split Files…` 与新加的 `&Save` 撞了 S。`check_translations.py` 只管助记符的
写法，不管同一菜单里重复；`mnemonics` 屏幕模式只按顶层菜单。这一类每加一个菜单项就可能再犯一次。

**要做**
1. `mainwindow_smoke` 里加一块（插在 `    // ---- a second document joins the first ----` 之前）：中文、英文两种界面各一遍，
   遍历菜单栏与每一个子菜单（递归），对每个菜单里**可见的**动作用 `QKeySequence::mnemonic(text)` 取助记键，断言同一个菜单里
   没有重复；顶层菜单栏自己也算一个菜单。失败消息要点名语言、菜单、撞的两项。
2. 如果这条一跑就红（别的菜单里还有没发现的撞车），在英文/中文目录里修掉，并在报告里列出每一处改了什么。
3. 控制：把英文目录里 `Split &Files…` 临时改回 `&Split Files…`、重编（`.qm` 是嵌进去的，`--run-only` 不算），那条必须红；
   按 sha256 还原。
4. 右键菜单（页面上下文菜单、标签右键菜单）如果是常驻对象也一起查；是临时构造的就在报告里写明没查。

---

## 合并方式（主会话做）

G2、G4 先合（不碰产品 C++ 或只碰测试），G3 其次，G1 最后（最大）。英文目录照老办法：先从各分支抽出新增条目，合并时取 main 的、
重生成、填回。合并后重打包重装，在装上的那份上跑 G2 的两个新模式（`-Installed -OpenViaPicker`，副本在探针目录里）。
