# 第二批：全部按长期方案做（2026-10-22）

他一句"现在还没上架，全部都以长期方式做，然后并行给出 AI 提示词，做完后你再检查"。

所以这一批**不做任何权宜之计**：表单显示走 PDFium、翻译走官方接口加知情同意、就地保存真的做、
XFA 按规范处理。代价是有先后依赖，不能一次全并行 —— 分三个阶段：

| 阶段 | 内容 | 为什么这样排 |
|---|---|---|
| **第一阶段**（现在，5 个并行分支） | F1 翻译、F2 可重现构建、F3 XFA 与 §47 那句话、F4 测试欠账、F5 PDFium 方案与探针 | 彼此改的文件基本不重叠 |
| **第二阶段**（第一阶段合并后，单独 1 个） | F6 `DocumentTab` 拆分 | 它会动到几乎所有文件，和谁并行都会合并地狱；而 PDFium 接入必须在它之后 |
| **第三阶段**（第二阶段合并后） | F7 PDFium 取代 Qt PDF 渲染；F8 就地保存与标准的保存/另存为 | F7 的细节取决于 F5 的方案；F8 要动文档的打开/关闭生命周期，必须在 F7 之后 |

第三阶段的任务说明**等 F5 的方案经主会话审过之后再写** —— 现在写就是替一个还没量过的东西拍板。

**不交出去的**：触控板缩放（只能他在笔记本上试）；25,684,592 那颗 DLL 的来源（主会话在日志里结案）；
装上的那份上的新功能检查与每次合并后的重打包重装（主会话做）；商店元数据（要他本人的信息）。

---

## 每个分支都要先做的：环境准备与规矩

**环境准备**（新 worktree 里没有构建树，也没有 `vcpkg_installed`，两者都在 `.gitignore` 里）：
1. 在 worktree 根目录（含 `xyzpdf/` 和 `docs/` 的那一层）执行
   `cmd /c mklink /J vcpkg_installed H:\test\XYZPDF\vcpkg_installed` —— 主检出里已经装好 qpdf，只读借用；
   junction 不可用时才在 worktree 根跑 `H:\test\vcpkg\vcpkg.exe install --triplet x64-windows`。
   **删除 worktree 之前先单独删掉这个 junction**（只删链接），否则可能顺着它删到主检出的依赖。
2. `cd xyzpdf && python tools/build_all_tests.py`：第一次从零配置并构建，要几分钟。
   基线必须是 **26 passed / 0 failed、目录 631 条、numerus 49**；不是这个基线就先停下报告。
3. 不要删构建树的 `CMakeCache.txt`。

**规矩**（`AGENTS.md` 里都有，这几条最容易出事）：
- 每条新断言配一次**控制跑**：临时把产品代码改坏，证明那条断言会红，再按 sha256 逐字节还原并重编。
  控制没红说明断言没在测东西 —— 改断言，不要留着。
- 收尾 `python tools/build_all_tests.py` 必须全绿（测试 + 目录检查）。编不过就明说"未编译验证"。
- 所有用户可见文字走 `tr()`；带计数的句子用 `%n` + `tr(单数, 复数, n)`，并在 `tests/i18n_smoke.cpp` 探针表加一行。
  新增的每句都在英文目录里填好（先 `cmake --build build-local-release --target update_translations` 再填；
  lupdate 会按源文相等套用别的上下文的译文，新增的每条都要用眼睛核）。英文助记符写在词内（`&Print`）。
- **不改** `docs/store-submission.md`、`AGENTS.md`、`docs/review-03.md`、`docs/handoff-*.md`；
  **不打包、不安装、不用 gui-probe 的 `-Installed`；不 push，不合并到 main**。
- 测试块插在指定的锚点前，不要插在别处（几个分支并行改同一批测试文件，锚点不同才不撞）。
- 在自己的分支上提交，提交信息末尾一行：`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`

**最终报告必须包含**（写在最后一个提交的说明里，主会话从那里读）：做了什么（文件 + 增删行数）、
量到的数、每条控制红了哪几条、门禁结果、没做/没测的（明说），以及新增界面文字的「源文 → 英文」对照表。

---

## F1. 翻译：官方接口、知情同意、密钥进凭据管理器　分支：`translate-official`

**现状**（`src/translate/translator.{h,cpp}`、`src/core/appsettings.h`、`src/settings/settingsdialog.*`）：
- 默认 provider 是 `google`，走的是 **`translate.googleapis.com/translate_a/single?client=gtx`** —— Google 网页
  免费端点，非官方、无合同、随时可能失效，上架应用用它有合规风险。
- DeepL 走官方 API，但 `auth_key` 放在请求体里（DeepL 已改为推荐 `Authorization: DeepL-Auth-Key <key>` 头）。
- API key 以明文存在 `QSettings` 里。
- 第一次翻译之前没有任何说明：选中的文字会离开这台机器，发给第三方。

**要做**
1. **删掉 gtx 端点**，连同它的解析代码与测试。
2. 两个官方 provider：**DeepL**（key 以 `:fx` 结尾走 `api-free.deepl.com`，否则 `api.deepl.com`；key 放请求头）与
   **Google Cloud Translation v2**（`translation.googleapis.com/language/translate/v2`，key 放 `X-goog-api-key` 头，
   **不要放 URL**）。错误要分开说：密钥无效（401/403）、额度用完（DeepL 456、Google 403 quota）、网络不通。
3. **默认不翻译**：没有配置 provider 和密钥时，翻译菜单/工具仍可点，点了弹一个说明框：要翻译需要一个服务的
   API 密钥，按钮「去设置」直接打开设置里的翻译页。
4. **第一次向某个 provider 发请求之前问一次**：说清楚"选中的文字会发给 <服务名>"，同意才发，按 provider 记住
   同意；换 provider 要重新问。设置里能撤销同意。
5. **密钥进 Windows 凭据管理器**（`CredWriteW` / `CredReadW` / `CredDeleteW`，目标名形如 `XYZPDF/translate/deepl`），
   `QSettings` 里只存"用哪个 provider"。已有的明文密钥首次启动时迁移过去并从 `QSettings` 删掉。
   **自动测试不许碰真实的凭据管理器**：把密钥存取抽成一个接口，测试用内存实现；真实实现只手动验证一次
   （写入→读出→删除），在报告里写结果，验证完删掉那一条。
6. 把"上架时隐私政策必须写的那几句"写进报告（什么时候发、发给谁、发什么、不存什么）—— 主会话会放进
   `store-submission.md`。

**测试**：`tests/translate_smoke.cpp`（纯逻辑：请求构造、头里有 key 而 URL 里没有、错误分类、未配置时不发请求、
未同意时不发请求）；需要窗口的部分插在 `tests/documenttab_smoke.cpp` 这一行**之前**：`    // ---- night reading ----`。
**控制**：去掉同意检查 → 红；key 回到 URL → 红。

---

## F2. 可重现构建　分支：`reproducible-build`

**现状**：`AGENTS.md` 记着"`xyzpdf.exe` 的哈希不是内容指纹"（§82）：同一份源码重链只差 4 字节 —— COFF 头与
debug directory 里各一份链接时间戳。所以"重编一遍和装上的比哈希"一直不可行，每次对账只能靠"打包复制同一份字节"。

**要做**
1. MSVC 编译与链接加 `/Brepro`（只对 MSVC，写进 `xyzpdf/CMakeLists.txt`，所有目标 —— 应用、三颗壳 DLL、测试）。
2. 新 `xyzpdf/tools/check_reproducible.py`：在两个**全新**的构建目录里各完整构建一次（不碰 `build-local-release`），
   逐字节比较 `xyzpdf.exe`、`xyzpdfpreview.dll`、`xyzpdfshell.dll`、`xyzpdfthumbnail.dll`；有差异就印出差异的
   偏移与所在的 PE 节，exit 1。
3. **把剩下的不确定来源一个个找出来并消掉**，可能的有：Qt `rcc` 往资源里写的文件修改时间（查 `QT_RCC_SOURCE_DATE_OVERRIDE`
   / `SOURCE_DATE_EPOCH`）、PDB 路径与 `__FILE__` 里的绝对路径（`/pathmap:`）、版本资源里的时间。每一个都要**先量到它
   造成了差异，再改，再量到差异消失** —— 不要凭猜测加选项。
4. 完成标准：同一目录下两次干净构建，四颗**逐字节相同**；两个不同路径的构建也相同算加分（做不到就说清楚差在哪）。

**测试**：这一件不改测试文件。门禁照常全绿。把"同一份源码两次构建的四个哈希"写进报告。

---

## F3. XFA 按规范处理，以及 §47 那句话　分支：`xfa-and-reply-notice`

**背景**：§55 记过 —— XFA 是表单数据的第二份副本（`/AcroForm /XFA`），我们的写路径一眼都不看它。XFA 已被 PDF 2.0
移除。§47/§78：合并/删页让一条回复指向本文件里看不见的注释时，§78 会摘掉 `/IRT` 与 `/RT`，把它变成独立批注 ——
**数据已经对了，但读者在按下之前没有被告知**。

**要做**
1. **打开时识别**：复用 `DocumentTab::open()` 里已有的那一次 qpdf 探针（问表单字段、读书签的那次，不要再开一次文件），
   加上 `hasXfa()`，以及"动态 XFA"（目录里 `/NeedsRendering true`，或只有 XFA 没有 AcroForm 字段）。
   `MainWindow::openDocument()` 的状态栏照 §100 表单那句的写法说：含 XFA → 本版不显示也不编辑其中的内容；
   动态 XFA → 这份文档要用支持 XFA 的阅读器才能看到正文（不支持 XFA 的阅读器一般只显示一页"请稍候"之类的提示）。
2. **保存时**（`src/edit/documenteditor.*` 的写路径）：待写的操作**改变了页面集合**（删页、重排、合并、拆分）时，XFA 已经
   不再描述这份文档 → 写出的文件里去掉 `/XFA`，让别的阅读器回到 AcroForm 字段；没改页面集合（旋转、水印、书签、加密）
   时原样保留。**在按下之前说**：涉及的对话框（删页、移动、合并、拆分）在有 XFA 时加一句，规则照 §77/§80：
   用按下之前的纯查询，不要存"上一次请求"的数。
3. **§47 那句话**：同样在按下之前 —— 这次操作会让 N 条回复失去它所回复的注释、变成独立批注时，说出来。
   计数用纯查询（参照 `positionalAtRiskFor()` 的写法）。
4. 样本不用找真文件：`documenteditor_smoke` 已能现造 `field-xfa`（见 `AGENTS.md`），回复链样本见 §78 的测试。

**测试锚点**：`tests/documenttab_smoke.cpp` 中 `    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----` 之前；
`tests/mainwindow_smoke.cpp` 中 `    // ---- a second document joins the first ----` 之前；`tests/documenteditor_smoke.cpp` 只有你这一支在改，位置自定。
**控制**：去掉写出时的 `/XFA` 处理 → 红；按下之前的那句不出现 → 红。

---

## F4. 测试欠账　分支：`test-debts`

1. **§100 的级联要有一条会红的测试**：那个无限循环（混尺寸文档上适应宽度经横向滚动条来回反馈）回归时，
   `mainwindow_smoke` 不是变红，是**卡住不结束**。给它一个看门狗：另起一条线程，超过时限（例如 120 秒，环境变量可调）
   就在 stderr 印一句 `FAIL: ... did not finish` 并以非零码立即结束进程。再写一条专门的级联测试（用 `text.pdf` 那种两页
   尺寸不同的文档、在会触发的窗口几何下），**控制**：把 `PageView::fittingZoom()` 退回按当前页适应 → 必须在时限内红，
   而不是挂住。看门狗插在 `mainwindow_smoke.cpp` 里紧跟 `QApplication application(argc, argv);` 那一行之后；
   级联测试插在 `    // ---- fullscreen and presentation ----` 之前。
2. **§103 那次 0xC0000005 取证**：在测试进程里装一个未处理异常过滤器（`SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，
   只在设了某个环境变量时启用，转储写进临时目录）。**不要改系统的 WER 设置**（那是改这台机器）。然后复现 §103 的控制配置
   （截图工具那一支落进识别那条路，见 §103）至少五遍；拿到转储就分析调用栈并写进报告，五遍都不崩就写"未复现"。
   documenttab_smoke 里的钩子插在它自己的 `QApplication application(argc, argv);` 那一行之后。
3. **D 的五个真窗口模式在当前 main 上重跑**：`propsdialog / findwhole / snapshot / strippreview / mnemonics`，中英各一遍，
   每次传 `-Exe <你的worktree>\xyzpdf\build-local-release\xyzpdf.exe`，不用 `-Installed`；各自的 `-Control` 也跑一次。
   读数写进报告。脚本会 kill 名为 `xyzpdf` 的进程，开跑前确认用户没开着自己的 XYZPDF 窗口。

---

## F5. PDFium：方案与探针（不接入应用）　分支：`pdfium-plan`

**背景**：表单字段内容不显示（§54/§82/§94）—— 屏幕、导出、打印都是空格子。Qt PDF 内部就是 PDFium，但它没有初始化
表单填写环境，所以 `/Widget` 注释不画；**PDFium 自己能画**（`FPDFDOC_InitFormFillEnvironment` + `FPDF_FFLDraw`）。
长期方案是直接用 PDFium。这一支只做方案与量，**不改应用代码**。

**要做**
1. **拿到 PDFium**：先查 `H:\test\vcpkg\vcpkg.exe search pdfium`；没有合适的端口再考虑 `bblanchon/pdfium-binaries` 的某个固定版本。
   **下载任何二进制之前先停下来问用户**，并说明来源、版本、大小、许可证。记下版本与 sha256。
2. 新的探针目标（CMake 选项，**默认关**，名字带 `probe`，门禁不跑它）：用 PDFium 打开 `documenteditor_smoke` 现造的那几份
   表单样本（`XYZPDF_FORM_SAMPLE_DIR`）与仓库 fixture，分别不带/带 `FPDF_FFLDraw` 渲染，**数出字段区域的非白像素**，
   与 Qt PDF 同尺寸的渲染对比；再量渲染耗时（`huge.pdf`、`scan.pdf`、一份真书若用户提供）与 Qt PDF 比。
3. 写 `docs/pdfium-plan.md`，回答：
   - **取代还是并存**：PDFium 取代 Qt PDF（`Qt6::Pdf` 不再链接）还是两者并存（两份 PDFium 进包）；包体积各多少。
   - **线程模型**：PDFium 不是线程安全的，要一把全局锁（和 Qt PDF 内部那把一样）；`PageRenderer` 的多线程渲染怎么改。
   - **`Document` 接口**：review-03 §1.1 说它是名义上的 —— `DocumentTab` 直接持有 `QtPdfBackend` 并取 `pdfDocument()`；
     列出要变成真接口需要动的每一处（`PageRenderer`、打印、图片导出、Word 导出、搜索、文字层、链接、书签、
     预览窗格与缩略图两颗壳 DLL）。
   - **表单**：只画外观，还是也支持填写（填写要处理焦点、键盘、保存回写）—— 分两步的话第一步是什么。
   - **许可证与第三方声明**：PDFium 及其带进来的组件（FreeType、libjpeg-turbo、OpenJPEG、lcms、zlib 等）各自要加的声明。
   - **风险与迁移顺序**，以及第三阶段 F7 可以照着执行的任务拆分。
4. 探针的读数与方案文档都在报告里给出结论。

---

## F6. `DocumentTab` 拆分（第二阶段：第一阶段全部合并后再发）　分支：`documenttab-split`

**背景**：review-03 §1.2。`DocumentTab` 已经同时是视图宿主、渲染调度、缓存、搜索会话、搜索面板、文件监视、
OCR、翻译、Word 导出、图片导出、打印、七样编辑入口、缩略图与书签面板宿主。第三阶段的 PDFium 接入会大改它，
必须先拆开。

**要做**（**行为不变**的重构）
1. 把"拿着文档去做一件事、会花时间、可取消"的那一族拆成各自的小类（名字自定），例如：搜索会话（扫描 + 结果 + 面板联动）、
   文件改动监视（watcher + 去抖 + 提示条）、前台任务（打印、导出图片，连同 `ForegroundJob`）、Word 导出、OCR 与翻译。
   `DocumentTab` 留下：视图、文档状态、编辑会话、各服务的装配。
2. **不变量必须一个不少地保留**：`drainRenders()` 在重新打开之前等所有还在读文档的工作结束（搜索、导出、缩略图……被搬走的
   服务也要被它等到）；`~DocumentTab` 的取消与等待；`ForegroundJob` 期间不许拆标签（§100）；`opened` 要在 `setPageSizes`
   之前置位（review-01 漏掉过的那条）。**注释跟着代码一起搬**（`AGENTS.md`：注释记的是踩过的坑）。
3. **现有测试的断言一条都不改**，只允许改 include 与取对象的方式；`MainWindow` 与测试用到的 `DocumentTab` 公开接口保持，
   必要时转发。
4. 分几个小提交做，**每个提交门禁都全绿**；报告里给出拆分前后每个文件的行数。

---

## 第三阶段（等 F5 方案审完再写详细说明）

- **F7 PDFium 取代 Qt PDF 渲染**：按 `docs/pdfium-plan.md` 执行；表单外观画出来后，§100 打开时那句"本版不显示字段内容"
  与打印框那句（§109 C）要随之撤掉或改写；预览窗格与缩略图两颗壳 DLL 一并换。
- **F8 就地保存与标准的保存/另存为**：要先改 `AGENTS.md` 那条"永不写用户的 PDF 文件（v1 只读）"（PLAN §7.3 本来就允许
  "编辑模式下的显式保存"）。做法：写临时文件 → 重新打开验证 → 释放 PDFium 对原文件的句柄 → `ReplaceFileW`（带备份）→
  重新打开并回到原来的页码与缩放；"另存为"改成标准行为（标签切到新文件，而不是在旁边再开一个）。
