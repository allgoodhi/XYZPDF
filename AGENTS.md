# XYZPDF

Windows 桌面 PDF 阅读器（第一阶段只读预览，编辑后续）。
**整体方案见 [docs/PLAN.md](docs/PLAN.md)，动手前先读完。**

## 参照项目

`H:\test\XYZip` 是同一作者的压缩软件，本项目的工具链、目录结构、构建脚本、测试方式、
打包流程全部与它一致。搬代码时**连注释一起搬** —— 那些注释记的是踩过的坑。

## 工具链（这台机器上的实际路径）

- Qt 6.11.2 MSVC 2022 64-bit：`E:\qt\6.11.2\msvc2022_64`（需要 Qt PDF 模块）
- Visual Studio Community：`E:\Microsoft Visual Studio\Community`，MSVC 14.51.36231
- 生成器 Ninja；cmake 用 `E:\qt\Tools\CMake_64\bin\cmake.exe`
- Python 3.14 在 PATH 上
- vcpkg 在 `H:\test\vcpkg\vcpkg.exe`；本仓库自己带 `vcpkg.json`（第一个原生依赖是 qpdf，M5 用）。
  **新克隆先在仓库根跑 `vcpkg.exe install --triplet x64-windows`**，否则 CMake 配置就停住（它不
  用 vcpkg 的 toolchain file，路径写死在 `vcpkg_installed/`，那个目录已 gitignore）。目录布局、
  打包与测试方式照 `H:\test\XYZip`。

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
- 临时文件走 `TempManager`；**永不写用户的 PDF 文件**（v1 只读）
- 测试：`python tools/build_all_tests.py`（配置+构建+运行，缺一不可；它还会跑 `tools/check_translations.py`
  对一遍目录——代码里改了用户可见的话，就靠这一步发现目录没跟上）
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
  -Mode inventory|menudump|exportstatus|mergedonor|splitsagain|saverename|langswitch|formpage|uacache|openpicker|outlinetree|tabstrip|bookdialog|deletesummary|pathforms|propsdialog|findwhole|snapshot|strippreview|mnemonics`（二十种模式
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
  还有一条量出来的产品行为：应用那一格"最近目录"是**打开与保存共用的**，一次 `保存修改为` 也会改写它，
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
- **书签的目的地从文件带下去、原样写回；连接认内容不认位置**（§99，关掉 §75 的路 (a)）。开文件时
  `outlineFromSource()` 按对象链读 `/Dest` 的种类 + 数字（**null 保留**：`/XYZ 0 700 null` 是"缩放别动"），
  `writeOutlineItem()` 原样发出去。哪一行对应哪一条按 **标题 + 页** 取（各一次），**不要按下标连** ——
  下标连接会让"在最前面插一条书签"把后面整棵树的目的地丢掉（实测过：那条请求报 3 条有风险）。
  代价写在代码里：同名同页的两条只有一条能带走。框里那句代价要用**按下之前**的纯查询
  `positionalAtRiskFor(rows)`，不要存"上一次请求"的数 —— 存过，于是那句话在屏幕上永远不会出现。
  **侧栏点击仍只跳页号**（位置进文件了，我们自己不读它）；§70/§80 那条路没动。
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
- **像素问题从头测，别从屏幕测**：要看"某一层画不画"就调 `QtPdfBackend::render(0, size, RenderOptions{})`，
  那才是应用走的那条路：**直接调 `QPdfDocument::render` 会得到假阴性**，因为 Qt 的默认 render flags 不带
  `Annotations`，而本项目的 `RenderOptions::annotations` 默认是 true（§81 就是这样先量错了一次）。
  屏幕那一支从 §89 起也走 `PrintWindow`（旧的 `CopyFromScreen` 会被盖在上面的第三方窗口污染 —— §81 实测
  四份不同文件给出**完全相同的 band 计数**和**四个不同的 PNG 哈希**，差的是那个窗口自己的时钟），
  修好裁图原点之后它与旧路由**逐字节相同**（plain.pdf 两张图同为 `473abc153a29…`，即 §54/§57/§58 那三次
  记下的值）。**表单 widget 这一类
  在这一层天生 0 像素**：同一份 `/AP /N` 挂到 `/Square` 上画满 6000 格、挂到 `/Widget` 上一格不画，
  注册与否、`/NeedAppearances` 与否都不改变（§82）—— Qt 这版只供 `RenderFlag::Annotations` 一个相关
  开关，别去找第二个。
- **`build-local-release/xyzpdf.exe` 的哈希不是内容指纹**（§82 逐字节量过）：不是可重现链接，同一份源码
  重链只差 4 个字节 —— COFF 头与 debug directory 里各一份链接时间戳。所以**哈希变了不说明代码变了**
  （只 `touch` 一下也换），哈希相同也不说明源码相同；别拿重建后的哈希互相比，要问"装上的那份是不是
  这次构建的字节"就查打包复制那条链（§16 的"三处相同"因此仍然有效）。
- **比字节的每一行要带上大小与"文件在不在"**（§83 实测）：`Get-FileHash` 对不存在的路径返回 null，而
  PowerShell 的循环变量留着上一轮的值，于是名字记错的两颗 DLL 照样印出 `same=True size=0`。三处比对
  先各自 `isfile` / `Test-Path`，再把字节数印进同一行；`same=True` 单独一句不算比过。
- 编不过就明说"未编译验证"，不要报告成完成
