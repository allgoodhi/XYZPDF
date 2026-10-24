# XYZPDF

Windows 桌面 PDF 阅读器（第一阶段只读预览，编辑后续）。
**整体方案见 [docs/PLAN.md](docs/PLAN.md)，动手前先读完。**

## 参照项目

`H:\test\XYZip` 是同一作者的压缩软件，本项目的工具链、目录结构、构建脚本、测试方式、
打包流程全部与它一致。搬代码时**连注释一起搬** —— 那些注释记的是踩过的坑。

## 工具链（这台机器上的实际路径）

- Qt 6.11.2 MSVC 2022 64-bit：`E:\qt\6.11.2\msvc2022_64`（§116 起**不再用 Qt PDF**，渲染引擎是 PDFium）
- Visual Studio Community：`E:\Microsoft Visual Studio\Community`，MSVC 14.51.36231
- 生成器 Ninja；cmake 用 `E:\qt\Tools\CMake_64\bin\cmake.exe`
- Python 3.14 在 PATH 上
- vcpkg 在 `H:\test\vcpkg\vcpkg.exe`；本仓库自己带 `vcpkg.json`（第一个原生依赖是 qpdf，M5 用）。
  **新克隆先在仓库根跑 `vcpkg.exe install --triplet x64-windows`**，否则 CMake 配置就停住（它不
  用 vcpkg 的 toolchain file，路径写死在 `vcpkg_installed/`，那个目录已 gitignore）。目录布局、
  打包与测试方式照 `H:\test\XYZip`。
- **PDFium**（§116，handoff-03 F7）：钉 `bblanchon/pdfium-binaries` 的 `chromium/8111`，放在仓库根的
  `pdfium_installed/`（gitignore，和 `vcpkg_installed/` 同一个待遇）。**新克隆先跑**
  `python xyzpdf/tools/fetch_pdfium.py --from <已解开的目录或 tgz>`（它校验 tgz、`pdfium.dll`、`pdfium.dll.lib`
  三个 sha256，不对就拒绝）；`--download` 才会去 GitHub。这台机器上的那份在 `H:\test\XYZPDF-wt\third-party\pdfium-8111`。
  并行 worktree 和 `vcpkg_installed` 一样用 junction 借主检出的 `pdfium_installed`，删 worktree 之前先单独删链接。

**不要删构建树的 CMakeCache.txt** —— 项目不带 CMakePresets，Qt 的位置只存在于缓存里。

**windeployqt 的那两颗 `dxcompiler.dll` / `dxil.dll` 是按**名字**捞的，连是不是 DLL 都不问**（§96 发现环境
决定清单，§97 把机制量到底，每趟只换一个变量，整棵树计数）：干净 shell → 25 颗、那两颗 ABSENT，而它自己印
`Cannot find any version of the dxcompiler.dll and dxil.dll.`；只设 `WindowsSdkDir` → 27 颗，落的是
`<sdk>\Redist\D3D\x64` 那两份；只把 `<sdk>\bin\<ver>\x64` 放进 PATH → 27 颗，落的是 `bin` 那两份 —— **两个变量各自都够，
而且给的是不同物理文件**（`bin` 的 dxcompiler = `97ABD3B3…`，`Redist` = `A5AA1D9A…`，**同尺寸 14,317,000 不同字节**；
构建树里那颗等于 **Redist**）。最后一脚：把一个临时目录（里面是**改名的 `dxc.exe`**，名叫 `dxcompiler.dll`）放进
PATH → 它印 `Updating dxcompiler.dll.` 并收了那颗 exe；把同目录的假 `dxil.dll` 从 x86 换成 x64，x86 被拒、x64 被收。
**所以 `dxcompiler.dll` 那一支只看名字，`dxil.dll` 那一支看架构。推论：打包 shell 的 PATH 里任何一个目录只要有个叫
`dxcompiler.dll` 的文件，那个文件就进包** —— `build-package.ps1` 的 strip 清单因此是跨环境稳定的那一半，不是装饰；
而"事后这包是从哪条路来的"只有比哈希答得出。**认出处/认相同一律比哈希**：大小相同不是身份（§83 那条规矩的反面，
§96 就在这一处写错过），哈希相同也不说明源码相同（§82）。两次量之前先确认自己是不是从开发者提示符里跑起来的。

## 约定

- 源码含中文字面量，MSVC 全目标加 `/utf-8`
- 往一个已有 `.cpp` 里加 `#include <windows.h>` 之前，先 grep 那文件里有没有 `small` / `interface` /
  `status` 这类宏名：rpcndr.h 会 `#define small char`，于是你改的是 include 那一行、报错却出现在 800 行
  之外一个你从没碰过的局部变量上（§79 实测）。跟着来一句 `#undef small`，别去改别人的名字。
- 版本号只写在 `project(... VERSION ...)`，其余三处（About / VERSIONINFO / 包 manifest）生成
- 所有用户可见字符串走 `tr()`
- 所有对话框走 `WindowDialogs`（窗口级模态）
- **在屏幕上找我们的框：按类名 + 两层找。** 类名不受翻译影响（`gui-probe.ps1` 的 `Dialog`/`DialogAny`），
  但**同一层不保证都有**：§85 量到 `DeletePagesDialog` 在主窗口子树里，§87 量到 `MergePagesDialog` **不在**
  （子树一个候选都不返回，只在进程自己的顶层窗口里）。找不到就把两层的名字/类名倒出来，别只印"没找到"。
  同一族还有另一课：`mergedonor` 三处查找只写英文那一支，中文界面下必然 Bail 在"没有那一行"—— **那句话说的是
  harness，不是产品**。
- **前台任务跑着的时候不许拆它的标签**（§100 实测）：打印与导出为图片是前台跑的，
  每页一句 `QApplication::processEvents()`。**`deleteLater()` 在 `processEvents()` 里被调用，就是
  那一句 `processEvents()` 把它执行掉** —— 所以排队进来的一条「关闭标签」会把正在执行
  `print()` 的那个 `DocumentTab` 当场析构。护栏是 `DocumentTab::ForegroundJob`（RAII）加
  `MainWindow::closeTab()` / `closeEvent()` 两处拒绝；框是窗口模态这件事只挡键盘和鼠标，
  **不是规则**。再写类似的前台循环就拿一个 `ForegroundJob`。
- 临时文件走 `TempManager`；**除了「保存」这一条路，永不写用户的 PDF 文件**（§112 起；原来是"v1 只读"）。
  那一条路的写法定在 `docs/handoff-03.md` F8：同目录临时文件 → 用 qpdf 重开验证 → 等读文档的工作结束、
  释放句柄 → `ReplaceFileW` → 重新打开；失败时原文件逐字节不变。任何别的代码路径一个字节都不许写用户的文件。
- 测试：`python tools/build_all_tests.py`（配置+构建+运行，缺一不可；它还会跑 `tools/check_translations.py`
  对一遍目录——代码里改了用户可见的话，就靠这一步发现目录没跟上）
- **lupdate 只认名字**（§112）：翻译助手叫 `said()`、`sentence()`、`translated()` 时对 lupdate 是隐形的 ——
  句子在代码里、运行时上下文也对，目录里就是没有，英文界面下显示中文（`Translate`、`Ocr`、`WordExport`
  三个上下文都这样空过，`check_translations.py` 看不见：它比的是目录与 lupdate 找到的，而 lupdate 什么都没找到）。
  新助手要么叫 `tr`，要么调用处包 `QT_TRANSLATE_NOOP("上下文", "…")`。查法：
  `grep -rn 'translate("[A-Za-z]*", *[a-z]'` 找出每个助手，再看目录里那个 `<name>` 有没有。
- **`update_translations` 会替你填词，而它填的是别的上下文那一条**（§100 实测）：lupdate 的
  same-text heuristic 按**源文完全相等**套用已有译文，于是属性框里的「主题」（PDF 的
  `/Subject`）直接拿到了外观设置里那个「主题」的 `Theme`。这条在目录里是**已完成、
  (context, source) 全匹配、四条形状规则全绿**的 —— `check_translations.py` 永远看不见。
  所以每次 `update_translations` 之后，**新增那几条要用眼睛过一遍**（包括它自己印的
  `Same-text heuristic provided N translation(s)` 那 N 条）。
- 目录也可以重新生成：`cmake --build build-local-release --target update_translations` 是安全的
  （2026-09-27 实测：42 条 numerus / 84 个 `<numerusform>` 一字不动，唯一变了的那一行只是 `'` 被转义成
  `&apos;`）。但要知道 **`i18n_smoke` 的探针在 lupdate 眼里就是调用点** —— 产品不再用的串只要还挂在探针表上
  就不会被 `-no-obsolete` 删掉，删一行探针倒可能带走一条串（所以"目录 == 代码"里，探针也算代码）
- 小结是 `line += tr("…")` 一句一句加出来的，**段落换行写在可翻译字面量里面**（`tr("\n\n…")`）；英文形
  少了那两个换行，屏幕上就是两句话焊在一起。目录里这类形状由 `check_translations.py` 的形状规则管着
  （集合对不上、numerus 掉形、两形相同、单数写成多数、段落换行丢）—— 这些都看不出在 (context, source) 上
  是"完全匹配"的
- **带计数的句子一律走 `%n` + `tr(单数, 复数, n)`，不要 `%1` + `.arg(n)`**：后者在 `check_translations.py`
  的 numerus 规则眼里是**合法**的（`%n` 压根没出现，掉形/两形相同/单数写成多数一条都看不见），所以
  §74 那条"`1 of them point at no page.`"在目录里安静地活了很久。能发现的只有 `i18n_smoke` 的探针表
  —— 它按 n=1 与 n=40 各问一次再**掩掉数字比较**，所以改一句带计数的话就同时加一行探针（也是
  `-no-obsolete` 的保命符）。两个细节：`.qm` 是构建时嵌进测试的，**控制跑必须重编**（`--run-only` 跑的是
  上一版二进制）；红下来的是"两形必须不同"那条，**不是**"单数得像单数"的正则 ——
  `1 of them point` 里数字后紧跟 `of`，那条正则看不见错（§93 实测）
- **英文形里的助记符写在词内**（`&Print…`、`Select &All`、`Ro&tate Pages…`），**不要把源文的括号抄过去**
  （`Print(&P)…` 是中文界面的习惯，英文菜单不这么写）。`check_translations.py` 的第四条规则
  （`mnemonic_rules_of`，§98）现在会为此 exit 1 —— 这一类缺陷 (context, source) 全匹配、numerus 与段落也都对，
  **只有眼睛看得见**，所以它挂在 §49→§98 六十七片：§88/§90/§92 三支模式都在英文界面里把这排名字读进过日志，
  没有人把括号当一件事。**同一族还有一句方法账**：我以为这 29 条"等他定风格"，其实风格早在同一份目录里
  写过八次（`&File` `&Open…` `Close &Tab` `E&xit` `&View` `&Go` `&Help` `&Settings…`）——
  **把一件事归成"需要他决定"之前，先查这个项目有没有已经这么决定过多次**。改英文形时：同一个菜单内助记字母
  互不重复，`…` 跟着词干；`.ts` 里 `&` 是 `&amp;`（数它的时候按 `(&amp;X)` 数，§96 那个假 0 就是这么来的）。
  **没验的一层**：Alt+字母真能打开对应项，这一族从没在屏幕上按过一次。
- 真窗口那一遍：`powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/tools/gui-probe.ps1
  -Mode inventory|menudump|exportstatus|mergedonor|splitsagain|saveas|saveinplace|closeprompt|langswitch|formpage|uacache|openpicker|outlinetree|tabstrip|bookdialog|deletesummary|pathforms|propsdialog|findwhole|snapshot|strippreview|mnemonics|translatesetup|xfanotice|replynotice|formfill|formtab|formcheck|formime|formcombo|formlist|signedsave|signedlock`（三十三种模式；`saverename` 仍收，是 `saveas` 的别名
  都当场跑通过；脚本注释里就是踩过的坑）。`deletesummary` 带 `-ExpectForm present|absent`：**屏幕上的一条
  判定必须能红**，所以同一模式在无表单文件上跑 `present` 要 exit 1（§85 的控制就是这么做的），而"没读到"
  的失败消息要说清是没框还是没话 —— 合成输入按轮成败，重试是常规操作，不是缺陷。侧栏那棵书签树用 `-Mode outlinetree -Doc <带大纲的文件> -Log <utf8>` 数，
  它的总数来自 GridPattern 的 rows（UIA 只交出现在视口的行，而 Qt 在这棵树上不供 ScrollPattern）。
  `QTableWidget` 同理：`-Mode bookdialog` 开"编辑书签"框，总数读 GridPattern 的 rows，格子是 DataItem
  （只有视口里那几十格存在），**而格子里放控件（QSpinBox）那个控件根本不进 UIA 树** —— 所以层级那一列
  从 §74 起是**正文格**：一行的三个连续 DataItem 就是 标题/层级/页码，**数在 Name 里**（同一格的
  `Value` 是空串 —— 不进编辑态 Qt 不供值，只看 Value 会把这列又读成空的），而文本格可以被
  `ValuePattern.SetValue` 写进去（§74 就是这样在屏幕上验出那句拒话的）。要看某个控件画成了什么样用
  `-Mode tabstrip`：它用 `PrintWindow` 让窗口自己画 ——
  屏幕截图在这台机器上会裁到盖在上面的第三方 topmost 窗口（§66 就是这样裁到了一个 WinZip 试用弹窗），而
  z-order 不是这个 harness 能赢的。**布局这种事只有图能看出来**：§71 的框 620 宽时第三列被切在窗口外，
  所有计数全绿。**反过来图也会说谎**：UIA 的 `BoundingRectangle` 对 Qt 顶层窗口是**客户区**，而
  `PrintWindow` 画的是**整个窗口**（含标题栏与边框）—— 按客户区尺寸开图必然切掉最底一行（§74 差点把
  这个当成"按钮被挤出窗口"的产品缺陷），所以开图按 `GetWindowRect` 的外框尺寸，判"在不在窗口里"用
  两个矩形比。**同一件事管着裁图的原点，§89 就是这么错的**：图的原点是**外框**左上角，所以
  `$left = $r.X - $gr.left`（`GetWindowRect`），写成 `$r.X - $wr.X`（客户区）就把整张图往上挪了
  一个边框高 —— 我们的窗口实测 frame=8,31，于是"页面图"的头 31 行是**标签条**（印着文件名）、
  左 8 列是黑色窗框。**两条 guard 都写成几何，不要写成颜色**（§91）：`formpage` 判"裁条不得越过客户区
  原点"（`ClientToScreen` 减 `GetWindowRect`），`tabstrip` 判"标签自己的矩形必须落在裁条之内"；
  第一版用"左列一串近黑＝窗框"，它当场把**深色侧栏**判成窗框而 exit 1 —— 那说的是用户主题，不是窗口。
  两条控制各自能红（§89 那对 + §91 的 CONTROL C/D）。`tabstrip` 从 §91 起图与裁条都按外框，
  它的 4/3 px 余量会被夹回客户区内（标签自己就贴着客户区左沿）。
  **模式早退必须走 `Bail`**（说一句 + kill 掉 xyzpdf + exit 1）：裸 `exit` 会跳过文件末尾
  那句 kill，残留实例会喂给下一轮，看着像"框没起来"（§74 真丢了一轮才抓到）。§86 把脚本里剩下的 **40 条裸
  `exit 1` 全收了**，并量了一对前后读数：同一条不带 `-LastDir` 的 `mergedonor`，旧脚本 exit 1 后残留 **1 个
  活实例**、新脚本残留 **0** —— 退出码没变，变的是下一轮。**控制跑要先证明它自己跑了**：§86 第一次跑那半条
  "旧脚本"时 `cp` 因为一个被 bash 吃掉的反引号没执行，PowerShell 报 `-File 参数不存在`（exit **127**），
  而那条"残留 0 个进程"量的是一根根本没启动的脚本。
  装的那份要英文界面走 `-Mode langswitch -Installed`（它的偏好没有
  外面的读路，读数在应用自己的设置框里；`-To` 现在两种拼法都认 —— 第一行说的是"跟随系统"这个**行为**、
  会被翻译，§92 就是英文界面里 `-To 跟随系统` 找不到行、Bail 之后把装的那份留在了英文）。
  **会改用户设置的探针，"改回去"那条路必须和"改过去"同一次验收**，不能等下次要用才发现它只认一种拼法。
  **装上的那份现在也能带文档跑任何模式**：
  `-Mode <anything> -Installed -OpenViaPicker <文件名>` —— 包里那份不能由 exe 路径起、也不收文件参数
  （没有 AppExecutionAlias），所以这条走它自己的"文件→打开"：先 `SwitchToThisWindow`（**不加这一句时
  前台是别人的资源管理器窗口，合成点击全打在它身上**：菜单行报 enabled=True、坐标真实、点了没框起来，
  看着像"这条链在装的那份上不通"，§84 就是这样白跑三轮），再点行、再 BM_CLICK 控件 id 1；
  picker 落在哪个目录由壳的 per-exe 记忆决定（与开发版同名，§62 那条），所以样本要先在那儿。
  **-OpenViaPicker 也可以给完整路径**（§90）：找不到那一行时，它先把框里所有控件倒出来（id + 类名 +
  文本），再把路径 `WM_SETTEXT` 进文件名框 —— **这个框里文件名是 id 1148，而 1001 是地址栏**；
  §44 那句"往 1001 写有效"只对**保存**框成立，而 2026-09-28 记在 `PickFile` 上的"输入路径不生效"
  正是写给 1001 的、也就是写错了控件，§90 重测推翻了它。成不成只由**我们自己的窗口标题**判（§88：
  投递一个点击不等于框被接受）。**写进去必须反斜杠形式** —— 同一个框同一个文件，`H:/tmp/...` 不交文档
  而 `H:\tmp\...` 交（§90 三条对照：斜杠红、反斜杠绿、归一后绿），`TypePathIntoPicker` 现在自己归一；
  **拒斜杠是壳的输入框那一族**（§88 那个文件夹框也一样），而我们自己的 Qt 目录框一直收斜杠。
  **这一族拒的是"任何一个 `/`"**（§97 用新 `-Mode pathforms -Target <pdf>` 六行表量出来的，同一份真文件六种拼法
  原样写进 1148、每行只看"我们的标题变没变 + 框关没关"两个收据）：目录段全反斜杠、只在文件名前那一个 `/` → 拒；
  只写斜杠拼法的目录、不写文件名 → 拒且没导航；而 id 1001 那格的类名是 **`ToolbarWindow32`** —— `WM_SETTEXT`
  对它根本不是"往输入框写字"，这就是 2026-09-28 那条笔记一直是错的完整解释（§90 只说到"写错控件"）。
  pathforms 自带两根硬 Bail：R1 反斜杠**必须**打进我们的标题、R5 一个不存在的文件**必须**打不进（R5 那行
  `folderShowsTarget=True`：框正显示目标所在目录却什么都没开 —— **"看得见"不是"接受了"**）；`-Doc` 与 `-Target`
  同名就直接拒跑，因为启动实例的标题里本来就有那个文件名，"标题变了没"在碰框之前就已经是 True。
  还有一条量出来的产品行为：应用那一格"最近目录"是**打开与保存共用的**，一次「另存为」也会改写它，
  所以下一轮 picker 可能落在你上次存出去的那个目录里（§62 那问的是第三个来源）。
  `build-local-release/gui-verify/` 下那些是历次草稿，在 gitignore 的构建树里、丢了找不回来 ——
  量出可用的新路由就收进 `tools/gui-probe.ps1`
- **§109 那五个新模式各带一个 `-Control`**：同一条屏幕判定、故意走错一步（`propsdialog` 期望 41 页、
  `findwhole` 去点「区分大小写」、`snapshot` 跳过 Ctrl+Shift+5、`strippreview` 跳过旋转、`mnemonics` 跳过 Alt+g），
  必须 exit 1 —— 屏幕上的判定必须能红，这次是写进了模式本身而不是靠人记得去改。`mnemonics` 是 §98 那句
  "Alt+字母真能打开对应项，这一族从没在屏幕上按过一次"的第一次实按：英 f/e/v/d/g/w/h、中 f/e/v/d/g/n/h
  各只展开自己那一项。**并行分支的 gui-probe 必须传 `-Exe <它自己的 worktree>\xyzpdf\build-local-release\xyzpdf.exe`**，
  默认值指向主检出。同一轮里它还量到一条所有计数都看不见的产品缺陷（查找栏占半个窗口，§110）——
  又一次"布局这种事只有图能看出来"。
- **§114 那三个模式（handoff-03 F9）也各带 `-Control`**：`translatesetup`（没选服务时 Ctrl+T → 说明框 →
  「去设置」→ 设置框、焦点在翻译服务那一格，设置框按外框画图、28 行控件各自判在客户区内）、`xfanotice -ExpectXfa
  packet|dynamic|absent`、`replynotice`（第 1 页批注、第 2 页回答它的样本）。样本由 `XYZPDF_FORM_SAMPLE_DIR`
  现造，多了 `xfa-dynamic.pdf` 与 `thread-across-pages.pdf`。**启动前指针先挪到屏幕角**（`ParkCursorAway`）：
  指针恰好停在工具栏上时，新窗口一出现就收到悬停，状态栏读到的是那颗按钮的提示 —— 读状态栏的模式都受这一条管。
  同意框（要先有密钥）**不上屏幕**：探针一律不写 Windows 凭据。
- **会按保存的探针模式只开它自己复制出来的那份**（§118，handoff-04 G2）：`saveinplace` / `closeprompt` /
  `saveas` 先把 `-Doc` 复制进 `%TEMP%\xyzpdf-gui-probe\save-<随机>\`，原件跑前、每次 Bail、跑完各比一次 sha256，
  动了就 exit 1。`-Installed` 下护栏自己把副本交给 picker（副本名每趟现取，调用方不可能事先知道）。
  判"真写进去了"不要只看哈希变没变：qpdf 每次重写带新的时间戳与文档 ID，同一份改动存两次字节就不同，
  三个模式都另查副本里有没有 `/Rotate 90`。三答框里的按钮按**名字完全相等**挑 —— `Save` 是 `Don't Save` 的子串，
  挑错就写了文件。
- **表单的四个屏幕模式**（§120，handoff-05 H2）：`formfill` / `formtab` / `formcheck` / `formime` 都在保存护栏里，
  样本由 `xyzpdf_pdfprobe --write-form-sample` 现造（只新建，路径已存在就拒绝），读回字段值用 `xyzpdf_pdfprobe --fields`
  （只读；非 ASCII 印成 `U+XXXX`，因为控制台是 GBK）。**`--fields` 等 PDFium 调用之前必须 `FPDF_InitLibrary()`**：
  不调时 `LoadDocument` / `GetPageCount` 照答，`FPDF_LoadPage` 直接把进程带走、一句话不说。
  `formime` 的 IMM32 那一路在这台机器上拿不到输入上下文（`ImmGetContext` 恒 0），那是探针的边界；同一模式的 TSF
  真按键一路把中文提交进了字段 —— 中文输入法进字段这件事在屏幕上成立。TSF 那路连跑两次会因为输入法切到英文模式
  而读到拼音字母，那是输入法状态，不是产品。
- **字段里的 Tab 要过 `focusNextPrevChild()` 那扇门**（§120）：`QWidget::event()` 在 `keyPressEvent()` 之前就把 Tab /
  Shift+Tab 交给 `focusNextPrevChild()`，基类答"挪焦点"，于是键盘跑到标签条（H2 屏幕上量到的）。`PageView` 在字段有键盘时
  答 false；Qt 把 Shift+Tab 报成 `Key_Backtab`，键表里要有它。**测这一类要经窗口送键，而且旁边要有第二个能拿焦点的控件**：
  孤零零一个 `DocumentTab`（或没激活的窗口）里基类自己就答 false，去掉修复也全绿 —— 这块测试写了三版才红得起来。
- **表单代码分两半**（§120，handoff-05 H1）：`pdfiumbackend.cpp`（渲染 + `FPDF_FFLDraw` + `Document` 接口，应用与两颗壳 DLL
  共用）、`pdfiumformfill.cpp`（输入路由、回调、暂存保存，只编进应用）、`pdfiumformfillstub.cpp`（壳 DLL 用的空答案）。
  `xyzpdf_use_document_backend(... DRAWING)` 选后者。壳 DLL 因此回到 136,192 / 94,208 字节；`dumpbin /imports` 里只有画字段
  那五个 PDFium 函数。**新的填写代码放进 `pdfiumformfill.cpp`**，放错一半就是两颗壳 DLL 又胖回去。
  PDFium 的两个读数定了写法：没有先到的 `FORM_OnMouseMove` 时按下会被拒（点另一个字段键盘不跟过去，§120 修掉）；
  组合框下拉是 PDFium 自己画的，下拉的行 `HasFormFieldAtPoint` 答 -1，所以按下不能先过命中测试。
- **暂存文件按记录收走**（§120，handoff-05 H3）：两处隐藏暂存（就地保存的 `xyzpdf-save-<uuid>.pdf`、表单保存的
  `xyzpdf-form-<uuid>.pdf`）开写前把全路径记进设置、藏好后带上创建时间再记一次，换上去或删掉之后撤记录。
  启动时 `StagingCleanup::sweepStaleRecords()` 只删**记过、仍隐藏、创建时间对得上、记它的进程已不在**的那一颗；
  没记过的同名文件连看都不看；删不动就留着下次再试。**这是本程序唯一会删读者文件夹里文件的地方**。
  另存为与拆分的暂存不隐藏、不记（失败话里把文件名念给了读者），被杀在拆分中间会留一颗看得见的文件。
- **验证数字签名**（§126，handoff-07 I1）：`src/document/signatureverify.*` 只编进应用，Windows 密码学接口（摘要按 `/ByteRange` 自己算，
  签名块用 `CryptVerifyDetachedMessageSignature` —— `CMSG_CTRL_VERIFY_SIGNATURE_EX` 对 `CryptSignMessage` 的产物答 `CRYPT_E_HASH_VALUE`），
  证书链 `CertGetCertificateChain` + BASE 策略，吊销默认 `CACHE_ONLY` 不联网；四种结论 有效 / 内容被改过 / 证书不受信任或已过期 / 无法验证，
  另报"签名之后几次增量更新""覆盖有缺口"。工作线程 `SignatureSession`，被 `drainRenders()` 与析构等到。**测试不碰证书存储，也不碰密钥目录**：
  样本用**无名的 CNG 密钥**（只活在进程里）经 `CERT_KEY_CONTEXT_PROP_ID` + `CERT_NCRYPT_KEY_SPEC` 交给签名器（另外三种挂法量过都不行，
  写在 `tests/signaturesample.h`），"受信任"靠注入 `hExclusiveRoot` 内存存储。
- **测试目标的名字别带 `install` / `bench` / `verify` / `probe`**（§126）：`tools/build_all_tests.py` 的 `NOT_A_TEST` 按**子串**过滤，
  叫 `signatureverify_smoke` 的测试一次都不会被跑（门禁只少数一个目标、照样报全绿），所以它叫 `signaturecheck_smoke`。
- **状态栏上接在一句话后面的句子，英文形自己带开头的空格**（§126）：中文句号后不用空格，英文要；`openedMessage += tr(...)` 直接接，
  英文界面就是 `...signatures.Its certification...`（I2 在屏幕上读到）。标点本身也走翻译（`，`→`, `、`。`→`.`），别写死全角。
  `mainwindow_smoke` 装英文目录读一遍状态栏，`[.:][A-Z]` 与全角标点都算红。
- **已签名的文档**（§122，handoff-06 H4）：签名按 PDF 32000-1 §12.8.1 数**签名字典**（字段的 `/V`），不数签名字段 ——
  `FPDF_GetSignatureCount` 会把空白模板里没有 `/V` 的 `/Sig` 字段也算一颗（它的 `/ByteRange` 0 个数、`/Contents` 0 字节）。
  状态栏明说"本版不验证签名本身"。**有签名且只改了字段 → `FPDF_INCREMENTAL`**：判据是字节，写出文件的前缀与原文件逐字节相同、
  `/ByteRange` 两段 SHA-256 前后相同（加密的也量过）；没签名照旧 `FPDF_NO_INCREMENTAL`（否则文件一次次长胖）。走 qpdf 的八种
  结构修改必然重写整份文件，所以那八个框在按下之前说"保存之后这 %n 个数字签名都会失效"。DocMDP 认证签名 P=1：字段点不进、
  八个框灰；P=2/3：字段可填、结构框灰。`/Lock`（FieldMDP）PDFium 不管，`DocumentTab` 把 qpdf 读出的名字交给表单层封住。
  样本 `tests/formsample.h` 的 `writeSigned()`（从规范造，`/ByteRange` 按装好的字节算）。读签名在共用的 `pdfiumbackend.cpp`，
  所以两颗壳 DLL 各多了约 1 KB 和五颗签名函数的导入。
- **经 picker 开完文档要把指针挪开**（§122）：点完「文件」菜单指针停在工具栏上方，悬停提示一直盖着开文档时那句话；
  `-OpenViaPicker` 那条路现在开完就 `ParkCursorAway`。
- **关窗三答框是「保存 / 不保存 / 取消」**（§118）：`WindowDialogs::askSaveBeforeClosing()`，标准按钮（按角色挑），
  只把 Discard 改写成「不保存(&N)」/ `Do&n't Save` —— Qt 自带目录给的是「丢弃 / Discard」（扔掉），不是 Windows 上
  对"要先保存吗？"的回答。
- **表单填写**（§118，handoff-04 G1）：能力是 `Document` 之外的第二个接口 `src/document/formfill.h`，`DocumentTab`
  用 `dynamic_cast` 取，所以只读的类型碰不到写路径。量到的三件定了写法：点击换算一律 `FPDF_DeviceToPage` /
  `PageToDevice`（页面自己的未旋转 user space，原点左下）；读 `/V` 用 `FPDFAnnot_GetFormFieldValue`（`GetStringValue(
  "/V")` 对同一份文件答空串）；**还带焦点的字不在 `FPDF_SaveAsCopy` 写出的字节里**，先 `FORM_ForceToKillFocus` 再写。
  保存是两段：PDFium 先写同目录隐藏暂存，有结构修改时 `DocumentEditor` 以它为来源再写第二份，验证后走 F8 那条
  `ReplaceFileW`。`FPDF_SaveAsCopy` **保留加密与权限**（量过 R3/RC4）。`FFI_SetTimer` 用 Win32 `SetTimer` 不用 `QTimer`：
  这份源码也编进两颗 `/DELAYLOAD` 的壳 DLL，`QTimer::staticMetaObject` 是数据符号、延迟加载不了（LNK1194）。
  字段撤销是逐键的（打 5 个字要撤 5 次）。没有 V8：字段脚本不跑，开屏说一句。
- **转 Word 是"排出来像 PDF"，不是"把字抽出来"**（§124 重写）：`pdfiumcontent.cpp` 把一页读成 `PageContent`
  （每个字的墨框/占位框/基线/字号/字体/粗细/颜色/绘制顺序，路径，图片原字节，链接）→ `pageanalysis.cpp` 的 `analysePage()`
  排成 `LayoutPage`（`layoutmodel.h`）→ `DocxWriter`。每页一节、页面尺寸照搬；段落**保留 PDF 的换行**（`w:br`）+ 精确行距；
  **每一行用 GDI 量出 Word 会排多宽，差额写成字符间距**（`AdvanceWidths`），否则 Word 的字宽比 PDF 宽一点就多折一行、
  整页往下推 —— 172 页的书曾回来 303 页。缩进按**笔位**（loose 框）不按墨框：中文字的墨比占位窄近 1pt，按墨算正好让最后一个字折行。
  **判据只有图**：导出 → WPS 打成 PDF → 逐页渲染对比；第一道数是**页数**（书 172 → 172）。这台机器上 COM 的 `Word.Application`
  答话的是 WPS（`E:\wps`）。几条量出来的规矩：PDF 里的空格要看"下一个字有没有真的前进过它的半格"（"1 9 8 3" 是排版软件的负字距）；
  粗细按**字体那一面**比（含 `,Italic` 那段，不能按映射后的家族）；页眉页脚要**从页边往里连续**（一行代码碰巧重复两次不是页脚）；
  被后画的不透明形状盖住的字不写；整页是图纸（≥300 条线、字平均不到 8 个一段）或扫描（图占 60% 以上、字 ≤40）就整页成图；
  名字说不出字体（`CIDFont+F2`）时用内嵌字体自己的家族名。
- **bash heredoc 里写 C++ 不要带反斜杠**（§124 踩了四次）：这个环境的 Bash 工具会把 heredoc 里的 `\n` `\\d` `\\0` 先吃掉一层，
  `"\n"` 变成真换行（C2001）、正则 `\\d` 变成 `\d`、`'\\0'` 变成真 NUL 字节（`grep` 报 "Binary file matches"）。带反斜杠的改动用 Edit/Write。
- **识别器要按文字的族挑**（§118，handoff-04 G3）：`pickEngine()` 按用户语言顺序拿引擎，这台机器是 zh-Hans-CN 在前，
  于是拉丁文字交给了中文引擎 —— 它把 "XYZPDF" 的 X 读成 ")("（14 种预处理里 4 种读对），en-US 引擎 14/14 全对；
  反过来 en-US 对中文图回空。现在第一趟的回答自己说明"不是我这一族"时（一个字没读出、或 CJK 引擎读出的东西里没有
  CJK 字）再问另一族的第一个引擎，非空才采纳；只装一族的机器行为不变。离屏平台（offscreen）上 Qt 没有字体，
  画中文图测识别要在真实平台的测试里（`ocr_smoke`）。`tests/ocrprobe.cpp` 是那张 28 格表的判据。
- **菜单助记字母不重复由测试管着**（§118，handoff-04 G4）：`mainwindow_smoke` 中英各遍历一次菜单栏与所有子菜单，
  同一菜单里撞字母就红并点名两项。页面右键菜单是栈上临时构造的，没进这条（眼睛核过：复制 C、翻译 T）。
- **有时限的状态栏消息一律走 `MainWindow::setStatusMessage`**（§114）：直接调 `showMessage` 的那条通知
  会被工具栏悬停的 status tip 永久抹掉（提示替换消息并停掉计时器，指针离开时空提示清栏）。
  `MainWindow::event` 在提示盖上时暂停那条通知的计时、提示走了就带着剩下的时间放回来。
- **`DocumentTab` 的服务住在 `src/services/`**（§114，F6）：搜索、文件改动提示、Word 导出、打印与导出图片
  （`ForegroundJobs`）、截图与识别、翻译，各一个类；`DocumentTab` 留下视图、文档状态、编辑会话与装配。
  搬过去的用户可见句子仍走 `QCoreApplication::translate("DocumentTab", …)` —— 上下文没换，目录一条没动。
  新服务要被 `drainRenders()` 与析构等到，凡是读文档的都算。
- **书签的目的地从文件带下去、原样写回；连接认内容不认位置**（§99，关掉 §75 的路 (a)）。开文件时
  `outlineFromSource()` 按对象链读 `/Dest` 的种类 + 数字（**null 保留**：`/XYZ 0 700 null` 是"缩放别动"），
  `writeOutlineItem()` 原样发出去。哪一行对应哪一条按 **标题 + 页** 取（各一次），**不要按下标连** ——
  下标连接会让"在最前面插一条书签"把后面整棵树的目的地丢掉（实测过：那条请求报 3 条有风险）。
  代价写在代码里：同名同页的两条只有一条能带走。框里那句代价要用**按下之前**的纯查询
  `positionalAtRiskFor(rows)`，不要存"上一次请求"的数 —— 存过，于是那句话在屏幕上永远不会出现。
  **侧栏点击仍只跳页号**（位置进文件了，我们自己不读它）；§70/§80 那条路没动。
- **Qt 的部署工具跑两趟，旗标要说两遍**（§116）：一趟是 `xyzpdf` 的 POST_BUILD windeployqt（让构建树里的
  exe 能双击），一趟是安装时 `qt_generate_deploy_app_script` 生成的脚本（包就是从这一趟暂存的）。F7 只在
  第一趟排除了 `qpdf` 图像插件，第二趟照样把它和它链接的 `Qt6Pdf.dll` 带进包（payload "gained 1, lost 0"）。
  改一个部署旗标，两处一起改，并且**出一次包看 payload 对照**才算数。
- **识别线程池的线程不许过期**（§116，F10）：`winrtPool()` 的线程是进程里唯一的 MTA，`QThreadPool` 默认 30 秒
  空闲就退出 —— 最后一个 MTA 一走，Windows 卸载 `Windows.Media.Ocr.dll`，而 C++/WinRT 的激活工厂缓存还拿着
  旧指针，下一次识别经已释放的 vtable 调用、进程直接没了（用户操作：两次识别间隔超过 30 秒）。
  `setExpiryTimeout(-1)`；再给 WinRT 开一个池也照这样做。
- **测试进程自己的护栏**（§112，F4）：`mainwindow_smoke` 有看门狗，`XYZPDF_SMOKE_TIMEOUT_MS`（默认 120000）
  到点印 FAIL 再 `_Exit(1)` —— §100 那条级联"重新引入时不是变红，是不再结束"，现在不结束也会红。
  `documenttab_smoke` 设了 `XYZPDF_TEST_CRASH_DUMP` 才装进程内的崩溃转储（`XYZPDF_TEST_CRASH_DUMP_DIR`，
  默认 TEMP），并印出异常码、出错地址落在哪颗镜像、指令字节与帧链；**不动系统的 Windows 错误报告设置**。
- **PDF 的键在哪个字典，按规范的表号查，不凭名字**（§112）：F3 把 `/NeedsRendering`（文档目录的键，
  PDF 32000-1 表 28）当成 `/AcroForm` 的，读和测试样本写在同一个错地方，于是代码与测试互相作证、全绿。
  自己造样本测自己的读法时，**样本的形状要从规范来，不要从被测代码来**。
- **控制必须能改变结果**（§99 又踩一次）：`if (const char *kind = false ? nullptr : f(x); …)` 看着像把
  分支关掉了，三目仍然求值了 `f(x)`，于是那趟"控制"26/0 全绿，而它什么都没测。判据是**结果变了**：
  控制跑要么让新钉的那条红，要么先证明它自己跑了（§86/§90/§95/§97 那族）。
- **`.ps1` 必须纯 ASCII，而且改完就查**：`python -c "d=open(p,'rb').read(); print(sum(1 for b in d if b>127))"`
  要 0 —— 中文字（含注释里的）在 GBK 控制台下是双字节前导码，会把解析器炸掉、退出码 1 而不说一句为什么
  （§72/§73/§74 各一次，§84 又两次，其中一次是注释里写了一句屏幕原文；**§90 第 11 次也是注释里的屏幕原文**，
  而那一遍 `PSParser` 报 0 错 —— **能过解析不等于没中**，字节数才是判据）。中文匹配串用 `CjStr 0x…` 现拼。
  同一族还有半个坑：**PowerShell 单引号串里反斜杠不是转义**，写 `UIA\'s` 字符串就在引号处断了，
  解析器吐 14 条错而退出码只给 1（§89 实测）；要引号就写两个 `''`，或者干脆换个说法。
- 带表单字段的样本不用等真文件：`XYZPDF_FORM_SAMPLE_DIR=<目录> build-local-release/tests/xyzpdf_documenteditor_smoke.exe`
  现造 plain / field-flat / field-spread / field-dangling / field-xfa / field-appearance / field-directive，
  外加注释层的三种 markup-highlight / markup-square / markup-note（§81，都是**不带 /AP** 的形状），
  以及 `painted-rect.pdf`（§89：同一片矩形画进页面自己的内容流，屏幕那张表里"必须有差异"的那一行）
  （目录名里带 `fixtures` 会被拒绝并算失败）。
  "这台机器上没有真带字段的 PDF"从 §54 起不再成立
- **像素问题从头测，别从屏幕测**：要看"某一层画不画"就调 `PdfiumBackend::render(0, size, RenderOptions{})`
  （§116 之前是 `QtPdfBackend`），那才是应用走的那条路：绕过它直接调引擎会得到假阴性（§81 那次是 Qt 的默认
  render flags 不带 `Annotations`；PDFium 这边表单层是 `FPDF_FFLDraw` 的第二趟，只调 `FPDF_RenderPageBitmap`
  一样看不见字段）。
  屏幕那一支从 §89 起也走 `PrintWindow`（旧的 `CopyFromScreen` 会被盖在上面的第三方窗口污染 —— §81 实测
  四份不同文件给出**完全相同的 band 计数**和**四个不同的 PNG 哈希**，差的是那个窗口自己的时钟），
  修好裁图原点之后它与旧路由**逐字节相同**（plain.pdf 两张图同为 `473abc153a29…`，即 §54/§57/§58 那三次
  记下的值）。**表单 widget 从 §116 起画得出来**（F7-C：`FPDFDOC_InitFormFillEnvironment` + `FPDF_FFLDraw`，
  只画不填；装上的那份 `formpage` 上量过）。Qt PDF 时代那条"widget 在这一层天生 0 像素"（§82）只对旧引擎成立。
  **未注册**进 `/AcroForm /Fields` 的 widget 仍然 0（§51 那条"注册决定"没变）。
- **构建是可重现的**（§112，F2；§82 量到的那 4 个字节已消掉）：MSVC 全目标 `/Brepro`（编译与链接），
  rcc 经 `cmake/rcc-deterministic.cmd` 把资源时间戳钉在 2020-01-01（它写的是每个资源文件的 mtime，
  而 `.qm` 是构建时生成的）。同一份源码两次全新构建，四颗产物逐字节相同，两种目录根路径也相同；
  判据是 `python xyzpdf/tools/check_reproducible.py`（两次完整构建，慢，不进门禁）。所以现在
  **"改了代码哈希却没变"是真信号**，值得停下来查；反过来哈希相同仍只说明"同一份源码 + 同一套工具链"，
  换了 Qt/MSVC/vcpkg 依赖也会变。"装上的那份是不是这次构建的字节"照旧查打包复制那条链（§16 的四列）。
  新加一个会把时间、路径或随机数写进产物的构建步骤，就会把这条打破 —— 加完跑一次那个脚本。
- **比字节的每一行要带上大小与"文件在不在"**（§83 实测）：`Get-FileHash` 对不存在的路径返回 null，而
  PowerShell 的循环变量留着上一轮的值，于是名字记错的两颗 DLL 照样印出 `same=True size=0`。三处比对
  先各自 `isfile` / `Test-Path`，再把字节数印进同一行；`same=True` 单独一句不算比过。
- 编不过就明说"未编译验证"，不要报告成完成
