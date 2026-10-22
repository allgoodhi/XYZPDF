# 交给并行分支的五件事（2026-10-22）

他定了三件（见 `docs/review-03.md` 顶上的横幅）：**搜索结果列表放侧栏第三个页签**；**文件在磁盘上改了只提示、
不自动重载**；**文件锁保持现状**。连同几样小件，分成下面五个可以同时做的分支。

每一件都写成可以直接交给另一个模型的样子：读完这一份、`AGENTS.md`（`CLAUDE.md` 会带进来）和 `docs/PLAN.md`
就能开工，不需要这次对话的上下文。

## 哪些交出去、哪些不交

| 事项 | 交出去？ | 理由 |
|---|---|---|
| A. 搜索结果列表（侧栏第三页签） | ✅ | 需求清楚，有纯函数可测 |
| B. 文件改动提示条（只提示） | ✅ | 需求清楚，复用 §104 的 `reload()` |
| C. 打印框表单提示 + §105 欠的重排/合并断言 | ✅ 打成一包 | 都小 |
| D. gui-probe 新模式（属性框/截图/全词勾/修改预览/Alt 助记符） | ✅ | 只改一个脚本，不碰 C++ |
| E. 书签点击跳到页内位置（§99 欠） | ✅ 有风险提示 | 数据已有，主要是坐标换算 |
| `DocumentTab` 拆分（review-03 §1.2） | ❌ 以后单独做 | 会和上面每个分支撞同一批文件 |
| §100 级联的红线、§103 控制跑那次 0xC0000005、25,684,592 是谁 | ❌ 留在主会话 | 要调查和判断，不是照单施工 |
| 表单字段自画 `/AP`、XFA、`/Brepro`、就地保存、PDFium 注释 | ❌ | 要他先定，或要先出方案 |
| 重打包重装（§16）、商店元数据 | ❌ | 合并之后主会话做；元数据要他本人的信息 |

## 合并方式（避免五个分支互相撞）

- **各分支不改日志**（`docs/store-submission.md`）、`AGENTS.md`、`docs/review-03.md`：五个分支都往同一个文件末尾追加，
  合并时必然冲突。日志由主会话合并后统一写（从 §109 起），各分支把要记的内容放在最终报告里。
- **英文目录**（`Resources/translations/xyzpdf_en.ts`）：lupdate 会改写全文件的行号，并行分支一定冲突。各分支照常填好
  自己的英文形，并在报告里给出「源文 → 英文」对照表；合并时主会话整体重生成一次目录，再按表填回。
- **`tests/documenttab_smoke.cpp`**：每个分支的测试块插在**不同的锚点**前（下面各自写明）。
- **不打包、不安装、不用 gui-probe 的 `-Installed`、不 push、不合并 main**。合并全部完成后，主会话跑门禁、
  重打包重装、四列比对（§106 那套）。
- 合并顺序建议：C → B → A → E → D（从改动最小到最大，D 不碰 C++ 放最后）。

---

## 每个分支都要先做的：环境准备与规矩

**环境准备**（新 worktree 里没有构建树，也没有 `vcpkg_installed`，两者都在 `.gitignore` 里）：
1. 在 worktree 根目录（含 `xyzpdf/` 和 `docs/` 的那一层）执行
   `cmd /c mklink /J vcpkg_installed H:\test\XYZPDF\vcpkg_installed` —— 主检出里已经装好 qpdf，只读借用；
   junction 不可用时才在 worktree 根跑 `H:\test\vcpkg\vcpkg.exe install --triplet x64-windows`。
2. `cd xyzpdf && python tools/build_all_tests.py`：第一次从零配置并构建，要几分钟。
   基线必须是 **26 passed / 0 failed、目录 623 条**；不是这个基线就先停下报告，不要开始改。
3. 不要删构建树的 `CMakeCache.txt`。

**规矩**（`AGENTS.md` 里都有，这几条最容易出事）：
- 每条新断言配一次**控制跑**：临时把产品代码改坏，证明那条断言会红，再按 sha256 逐字节还原并重编。
  控制没红说明断言没在测东西 —— 改断言，不要留着。
- 收尾 `python tools/build_all_tests.py` 必须全绿（测试 + 目录检查）。编不过就明说"未编译验证"。
- 所有用户可见文字走 `tr()`；带计数的句子用 `%n` + `tr(单数, 复数, n)`，并在 `tests/i18n_smoke.cpp` 探针表加一行。
  新增的每句都要在英文目录里填好（先 `cmake --build build-local-release --target update_translations` 再填；
  lupdate 会按源文相等套用别的上下文的译文，新增的每条都要用眼睛核）。英文助记符写在词内（`&Print`）。
- 在自己的分支上提交，提交信息末尾一行：`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`

**最终报告必须包含**：做了什么（文件 + 增删行数）、量到的数、每条控制红了哪几条、门禁结果、
没做/没测的（明说），以及新增界面文字的「源文 → 英文」对照表。

---

## A. 搜索结果列表（侧栏第三个页签「搜索」）　分支名建议：`search-results-list`

**现状**
- 查找栏 `src/ui/findbar.{h,cpp}` 只有：输入框、「第 N / 共 M 个」、上一个/下一个、「区分大小写」「全词匹配」。
- `DocumentTab::startSearch()` 在 worker 上逐页 `backend->text(page)`，用 `TextSearch::matchesInPage()` 汇总，
  扫完一次性交给 `applySearchResults()`；`TextSearch::Match` 只有 `{page, from, to}`。
- 侧栏是 `DocumentTab` 里的 `QTabWidget *sidebar`，现有「大纲」「缩略图」两页，默认关着（`sidebarWanted = false`）。

**要做**
1. `src/document/textsearch.{h,cpp}` 加一个**纯函数**截上下文片段：命中前后各约 20 个字符，换行折成空格，
   两端被截断处加「…」，并给出命中在片段里的起止（用来加粗）。`tests/textsearch_smoke.cpp` 测中文、英文、
   页首/页尾、跨换行。
2. 扫描时把片段一起带回 —— worker 手里已有整页文字，**不要为了片段再读一遍页**（读页占 Qt PDF 的全局锁）。
3. 新 `src/ui/searchpanel.{h,cpp}`：每行「第 N 页 · 片段（命中加粗）」；点击跳到那一处，并让 `currentMatchIndex`
   跟着变、查找栏计数同步；上一个/下一个时高亮当前那一行。最多列 1000 条，超出时末尾一行「还有 N 处没有列出」
   （`%n` + 探针）。
4. 侧栏加第三个页签「搜索」。**侧栏关着时不自动打开**；查找栏加按钮「列出全部」，点了才打开侧栏并切到
   「搜索」；侧栏本来开着时，开始搜索就自动切过去。清空搜索、换文档时列表清空。
5. 新文件加进 `CMakeLists.txt` 的 `XYZPDF_SOURCES` 和所有编了 `documenttab.cpp` 的测试目标
   （grep `src/ui/printdialog.cpp`，每处旁边加上）。

**测试锚点**：`documenttab_smoke.cpp` 中
`        // ---- the same thing on a document with more than one line per page ----` 之前。

**完成标准**：门禁全绿；至少两条控制各自红过并还原（片段函数改坏；点击不跳/不更新当前匹配）。

---

## B. 文件改动提示条（只提示，不自动重载）　分支名建议：`file-change-notice`

**现状**：§104 已有 `DocumentTab::reload(QString *error)`（F5）：带着页码/缩放/版式/旋转重读；没有文档、
有前台任务（`ForegroundJob`）、有没保存的修改（会问）时拒绝。没有任何东西监视文件。

**要做**
1. 每个 `DocumentTab` 一个 `QFileSystemWatcher`，`open()` 成功时加上，换文档/打开失败/析构时移除。
2. 文件变化时在页面上方（查找栏附近）显示提示条：「这个文件在磁盘上改过了。」+「重新加载」+ 关闭按钮。
   按钮调 `reload()`：成功后提示条消失；失败把那句话显示出来，**屏幕上的文档不清空**。F5 重载成功也要让它消失。
3. 坑（都要处理，并在注释里写**为什么**）：(a) 文件被删除/改名后 watcher 停止监视 —— 收到变化时文件还在就重新
   `addPath`；(b) 一次保存连发多个信号 —— 去抖 300–500 ms 并等文件大小稳定；(c) 本程序永不写用户的 PDF，
   不存在自己触发自己，注释写明；(d) 有前台任务或没保存的修改时照样只显示提示条，按钮按下后交给 `reload()` 处理。
4. **先量一件事**：文档开着时文件删不掉（§104 实测，他决定保持锁），所以测试用"原地覆盖写"触发变化。
   **先实测原地覆盖写在文档开着时能不能成功**并写进报告；如果连覆盖写都不行，停下报告，不要绕。

**测试锚点**：`documenttab_smoke.cpp` 中
`    // ---- the strip shows what a save would write (docs/review-03.md §1.3) ----` 之前。用 `QTemporaryDir` 里的副本。

**完成标准**：门禁全绿；控制：断开 watcher 信号 → 提示条断言红；去抖拿掉 → "连发只出一条"之类的断言红（如有）。

---

## C. 小件包：打印框表单提示 + §105 欠的断言　分支名建议：`print-form-and-strip-tests`

**1. 打印框表单提示**：本程序不画表单字段内容（§82/§94），打印填好的表单得到空格子。§100 在打开时用状态栏说过，
并有 `DocumentTab::hasFormFields()`；**打印框里还没有**。在 `src/ui/printdialog.{h,cpp}` 有表单时显示一行，例如
「这份文档含表单字段，本版打印出来字段里是空的。」，从 `DocumentTab::print()` 传进去。测试直接构造 `PrintDialog`，
有/无各一次，读标签断言（`labelsOf()` 写法）。控制：不传/不显示 → 红。

**2. §105 欠的两条断言**：缩略图条显示"保存之后的页"已测删页、旋转；**重排和合并没有单独断言**。
- 重排：`tab.movePages()`，用 §105 的 `answerNextDialog<Dialog>(pages)` 驱动 `MovePagesDialog`；需要更多输入就扩展
  辅助函数并说明。断言行序与点击跳转。
- 合并：`MergePagesDialog` 要挑文件，**要过原生文件框就不要硬做** —— 改在模型层断言 `pageOrder()` 出现 `source != 0`、
  `importPathFor()` 给出路径，并在报告里明说"合并的屏幕那一支没测"。

**测试锚点**：两件都在 `documenttab_smoke.cpp` 中 `    // ---- the properties dialog (PLAN M2) ----` 之前。

---

## D. gui-probe 新模式　分支名建议：`gui-probe-new-modes`

**现状**：§100–§105 新加的这些**在真窗口里一次都没看过**：属性框（Ctrl+D，类名 `PropertiesDialog`）、查找栏「全词匹配」
（objectName `findWholeBox`）、「截图」工具（Ctrl+Shift+5，拖框 → 剪贴板有图，状态栏「已复制 W × H 的图片」）、
缩略图条的修改预览（页签变「缩略图（修改预览）」、转过的页缩略图是横的）、Alt+字母助记符（从没真按过）。

**要做**：新增模式（如 `propsdialog` / `findwhole` / `snapshot` / `strippreview` / `mnemonics`），每个至少一条**能红**的屏幕
判定并跑控制证明会红；中英两种界面各跑一遍（`-Language en` / `zh_CN`）。做不到的判定说清楚为什么。

**必须遵守**：`.ps1` **纯 ASCII**，改完查字节数为 0（注释里的屏幕原文也算），中文用 `CjStr` 现拼；早退走 `Bail`；
截图用 `PrintWindow`；找框按类名两层找。**每次都传 `-Exe <worktree>\xyzpdf\build-local-release\xyzpdf.exe`**
（默认指向主检出，那不是你的代码）；**不用 `-Installed`**。脚本会 kill 名为 `xyzpdf` 的进程，开跑前确认用户没开着
自己的 XYZPDF 窗口，跑完清场。**产品代码不改**，发现缺陷写进报告。不改 `AGENTS.md`，要加进模式清单的句子写在报告里。

---

## E. 书签点击跳到页内位置（§99 欠）　分支名建议：`bookmark-jump-position`

**现状**（先读日志 §75、§99）：目的地（`/XYZ`、`/FitH`、`/Fit`…）已由 `DocumentEditor::outlineFromSource()` 在打开时读进
`sourceOutline`、保存时原样写回；但侧栏走 `QPdfBookmarkModel`，`OutlinePanel::jumpToPage(int)` 只跳页顶。
**不能靠 Qt 的 `Location` role**：§75 量过它对 `/FitH` 报 `0,0`，真书 184 条全是 `/FitH`。

**要做**
1. 目的地来源：编辑器只在编辑会话里懒开，不要为此常驻。可以"开了就扔"（§100：600 页 qpdf 打开 22 ms）或第一次点书签时再读，
   选一种并量成本。需要的话给 `DocumentEditor` 加只读查询。
2. 侧栏条目 ↔ 文件里的目的地：按 **标题 + 页** 连，各取一次；**不要按下标连**（§99 的规矩）。同名同页的代价写进注释。
3. `PageView` 加"跳到某页的某个高度"：PDF 原点左下 y 向上，布局原点左上 y 向下；页面自身 `/Rotate` 已被 Qt 折进
   `pagePointSize`；读者旋转复用 `PageMetrics::rotated` / `rotatedRect`。纯算术部分放纯函数测。`/Fit` 跳页顶，缩放不动。
4. `OutlinePanel` 点击带上位置（或在 `DocumentTab` 里按条目查）。

**风险提示**：动 `pageview.cpp` 的滚动时**不要碰** §100 修过的 `PageView::fittingZoom()` / `PageLayout::largestFitTarget()`。
§100 那个无限循环在 `mainwindow_smoke` 里表现为"不结束"而不是"变红"—— 改完用**全新的设置文件**跑一遍 `mainwindow_smoke`，
确认一两分钟内结束。

**测试**：用 qpdf 造一份带 `/XYZ` 与 `/FitH` 书签的 PDF（写进 `QTemporaryDir`），点书签后断言视口顶部落在那个高度附近，
转 90° 后再点一次。锚点：`documenttab_smoke.cpp` 中 `main()` 末尾 `    if (failures == 0) {` 之前。
控制：退回只跳页号 → 红；坐标换算故意写错（不翻转 y）→ 红。
