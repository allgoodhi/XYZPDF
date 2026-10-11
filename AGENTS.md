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
- 临时文件走 `TempManager`；**永不写用户的 PDF 文件**（v1 只读）
- 测试：`python tools/build_all_tests.py`（配置+构建+运行，缺一不可；它还会跑 `tools/check_translations.py`
  对一遍目录——代码里改了用户可见的话，就靠这一步发现目录没跟上）
- 目录也可以重新生成：`cmake --build build-local-release --target update_translations` 是安全的
  （2026-09-27 实测：42 条 numerus / 84 个 `<numerusform>` 一字不动，唯一变了的那一行只是 `'` 被转义成
  `&apos;`）。但要知道 **`i18n_smoke` 的探针在 lupdate 眼里就是调用点** —— 产品不再用的串只要还挂在探针表上
  就不会被 `-no-obsolete` 删掉，删一行探针倒可能带走一条串（所以"目录 == 代码"里，探针也算代码）
- 小结是 `line += tr("…")` 一句一句加出来的，**段落换行写在可翻译字面量里面**（`tr("\n\n…")`）；英文形
  少了那两个换行，屏幕上就是两句话焊在一起。目录里这类形状由 `check_translations.py` 的形状规则管着
  （集合对不上、numerus 掉形、两形相同、单数写成多数、段落换行丢）—— 这些都看不出在 (context, source) 上
  是"完全匹配"的
- 真窗口那一遍：`powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/tools/gui-probe.ps1
  -Mode inventory|menudump|exportstatus|mergedonor|splitsagain|saverename|langswitch|formpage|uacache|openpicker|outlinetree|tabstrip|bookdialog|deletesummary`（十四种模式
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
  两个矩形比。**模式早退必须走 `Bail`**（说一句 + kill 掉 xyzpdf + exit 1）：裸 `exit` 会跳过文件末尾
  那句 kill，残留实例会喂给下一轮，看着像"框没起来"（§74 真丢了一轮才抓到）。§86 把脚本里剩下的 **40 条裸
  `exit 1` 全收了**，并量了一对前后读数：同一条不带 `-LastDir` 的 `mergedonor`，旧脚本 exit 1 后残留 **1 个
  活实例**、新脚本残留 **0** —— 退出码没变，变的是下一轮。**控制跑要先证明它自己跑了**：§86 第一次跑那半条
  "旧脚本"时 `cp` 因为一个被 bash 吃掉的反引号没执行，PowerShell 报 `-File 参数不存在`（exit **127**），
  而那条"残留 0 个进程"量的是一根根本没启动的脚本。
  装的那份要英文界面走 `-Mode langswitch -Installed`（它的偏好没有
  外面的读路，读数在应用自己的设置框里）。**装上的那份现在也能带文档跑任何模式**：
  `-Mode <anything> -Installed -OpenViaPicker <文件名>` —— 包里那份不能由 exe 路径起、也不收文件参数
  （没有 AppExecutionAlias），所以这条走它自己的"文件→打开"：先 `SwitchToThisWindow`（**不加这一句时
  前台是别人的资源管理器窗口，合成点击全打在它身上**：菜单行报 enabled=True、坐标真实、点了没框起来，
  看着像"这条链在装的那份上不通"，§84 就是这样白跑三轮），再点行、再 BM_CLICK 控件 id 1；
  picker 落在哪个目录由壳的 per-exe 记忆决定（与开发版同名，§62 那条），所以样本要先在那儿。
  `build-local-release/gui-verify/` 下那些是历次草稿，在 gitignore 的构建树里、丢了找不回来 ——
  量出可用的新路由就收进 `tools/gui-probe.ps1`
- **`.ps1` 必须纯 ASCII，而且改完就查**：`python -c "d=open(p,'rb').read(); print(sum(1 for b in d if b>127))"`
  要 0 —— 中文字（含注释里的）在 GBK 控制台下是双字节前导码，会把解析器炸掉、退出码 1 而不说一句为什么
  （§72/§73/§74 各一次，§84 又两次，其中一次是注释里写了一句屏幕原文）。中文匹配串用 `CjStr 0x…` 现拼。
- 带表单字段的样本不用等真文件：`XYZPDF_FORM_SAMPLE_DIR=<目录> build-local-release/tests/xyzpdf_documenteditor_smoke.exe`
  现造 plain / field-flat / field-spread / field-dangling / field-xfa / field-appearance / field-directive，
  外加注释层的三种 markup-highlight / markup-square / markup-note（§81，都是**不带 /AP** 的形状）
  （目录名里带 `fixtures` 会被拒绝并算失败）。
  "这台机器上没有真带字段的 PDF"从 §54 起不再成立
- **像素问题从头测，别从屏幕测**：`-Mode formpage` 用 `CopyFromScreen`，会被盖在上面的第三方窗口
  污染 —— §81 实测四份不同文件给出**完全相同的 band 计数**和**四个不同的 PNG 哈希**（差的是那个窗口
  自己的时钟）。要看"某一层画不画"就调 `QtPdfBackend::render(0, size, RenderOptions{})`，那才是应用走的
  那条路：**直接调 `QPdfDocument::render` 会得到假阴性**，因为 Qt 的默认 render flags 不带 `Annotations`，
  而本项目的 `RenderOptions::annotations` 默认是 true（§81 就是这样先量错了一次）。**表单 widget 这一类
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
