# 商店提交前检查表

M4 的最后一格。这份文件不是模板，也不是商店文档的摘要 —— 它是**这个仓库当前的状态**
与"能提交"之间还差的东西，逐条写明差在哪、为什么是问题、以及本机验证到哪一步。

写它的原因是提交商店这件事**不能靠回忆**：包身份一旦上架就改不掉，隐私声明漏一项
是直接拒审，而最容易踩的那个坑（见 §3.2）长得完全不像坑。

---

## 1. 本机已经验证过的

在 `Ryzen 7 9800X3D / Windows 11 IoT LTSC (10.0.26100)` 上，2026-09-22：

```
python tools/makeappicon.py                                          # 图标 + 四张 tile
python tools/build_all_tests.py                                      # 26 项目标，24 通过
powershell -File packaging/build-package.ps1                         # 出包
```

结果：`build-local-release/package/XYZPDF_0.1.0.0_x64.msix`，15.8 MB（解包 36.1 MB / 59 个
文件），`makeappx pack` 报 `Package creation succeeded`。体积与文件数在 §9.2 那一步（磁贴补上
多尺寸变体、包里加 `resources.pri`）之后变过，又在 §9.6 末尾那一步（剥掉 29.3 MB 用不上的
Qt 负载）之后变小了一半，其余不变。清单声明的文件仍然是 8 个 ——
多出来的那些图不在清单里，是 `resources.pri` 负责解析的，见 §9.2。

打包脚本不信任自己盖的版本号，而是把清单从 `.msix`（就是个 zip）里**读回来**核对：
`the package's own manifest says version 0.1.0.0`。清单声明的 8 个文件（exe、四张图、
三个外壳处理器的 DLL）也逐个确认在包里 —— `makeappx` 遇到缺文件只报一句布局不对，
看不出缺哪个。DLL 也要算进来：清单里写着、包里却没有，是一个谁都不会注意到的出包
结果，预览窗格只会说"没有预览"，看起来像处理器自己的 bug。这一条在 2026-09-22 加上
三个处理器之前并不成立 —— 脚本只认 `.exe/.png/.ico/.jpg`，那天它报的是 5 个。

工具的位置值得记一下：`makeappx.exe` 与 `signtool.exe` 在
`E:\Windows Kits\10\App Certification Kit\`，**不在 PATH 上**，脚本按名字找它。
同一目录里还有 `appcert.exe` 与 `appcertui.exe`，即商店认证测试的工具 —— 工具在，
但跑不了那么便宜：它要提权，而跑完之后另有文章，见 §8。

### 1.1 装上也验过了（2026-09-22，开发人员模式打开之后）

装的是**暂存目录的松散注册**，不是那个 .msix：

```
Add-AppxPackage -Register build-local-release\package\app\AppxManifest.xml
```

三条都是撞出来的，值得写下来，因为每一条的报错都在指别处：

| 做法 | 结果 |
| --- | --- |
| `Add-AppxPackage -Path <msix>` | `0x800B0100` 包未签名 —— 开发人员模式也拦 |
| 加 `-AllowUnsigned` | 走到下一步：`0x80073D2C` 发布者证书不受信任（`CN=allgoodhi` 是自签） |
| `-Register` 暂存清单 **带** `-DisableDevelopmentMode` | `0x80073CF9`"清单没有位于程序包目录中"，看着像路径问题，其实不是 |
| `-Register` 暂存清单，不带那个开关 | 成功，`Status: Ok` |

装好之后观察到的（都是读系统状态，不是读我们自己的清单）：

- `Get-StartApps` 报出 `allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF`，用它
  `explorer.exe shell:AppsFolder\<那个 id>` **真的起来了**：PID 6860，窗口标题
  `XYZPDF`，`Path` 是 `package\app\bin\xyzpdf.exe`。`bin\` + `qt.conf Prefix=..` +
  包根 `plugins\` 这套布局是通的 —— 这条之前只是推理。
- `HKCU\Software\Classes\.pdf\OpenWithProgids` 里多了一项 `AppX…`，顺着查下去：
  `AppUserModelID = allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF`、
  `PackageRelativeExecutable = bin\xyzpdf.exe`、图标指向包里的 `Assets\Square44x44Logo.png`。
- 同一时刻 `HKCU\Software\Classes` 下我们自己的 `xyzpdf` 与 `xyzpdf.Document` 键**消失了**，
  正是 §3.2 说的那次清理。

### 1.2 M4 那条"双语完整"是查出来的，不是感觉出来的

```
cmake --build build-local-release --target update_translations
  Found 271 source text(s) (1 new and 270 already existing)
```

缺的那一条是区域截图（`66fccc5`）带进来的按钮 `复制图片`，英文目录里当时**根本没有**这一
条。已填成 "Copy Image"（与同一目录里的 `复制文字` → "Copy Text" 对称），并把两个按钮一起
加进门禁的扫描表（`tests/i18n_smoke.cpp` 的 probes）。

漏掉的形状值得写下来，因为它不出事：`lupdate` 只往 `.ts` 里加一条空的 `type="unfinished"`，
`.qm` 照样编得出来、照样加载得了，英文界面就那一个词是中文 —— 一次一个，出现在读者刚好
看到的地方。扫描表本来就是为这个写的，但表里最新的一条停在 `文字识别`，没跟着 M3 后面每
一个新按钮走。这条通道有没有牙是正控过的：把一条 probe 换成目录里不存在的字符串，门禁立刻
红 —— `FAIL: OcrDialog: "复制图片X" is translated (got "复制图片X")`。

顺带一条别被误导：`lupdate` 报的三行 `Class ... lacks Q_OBJECT macro`（`QtPdfBackend`、
`ThirdPartyNotices`、`ShellIntegration`）不是待办。那三处的 `tr` 是各自写的 lambda /
namespace 函数，context 是写死的字面量，与 `.ts` 里记的类名一致；真去加 `Q_OBJECT` 会把
`qtpdfbackend.cpp` 拉进 moc，而它同时编进预览与缩略图两个代理 DLL —— 那正是
`QPdfLink::staticMetaObject` 踩过的位置。理由已写进 `qtpdfbackend.cpp` 的注释。

### 1.3 签了、真装进系统了、WACK 也真下判词了（同一天晚些）

§1.1 那套是松散注册。这一回拿到自签测试证书，`.msix` 走的是**真安装**：

```
build-package.ps1 -Sign -Cert <指纹>     →  signtool verify /pa  rc=0
Add-AppxPackage -Path <那个 .msix>       →  exit=0
Get-AppxPackage  →  allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4
                    InstallLocation=C:\Program Files\WindowsApps\…  Status=Ok  SignatureKind=Developer
```

| 事 | 结果 |
| --- | --- |
| `signtool sign` 签 `.msix` | 用 `CN=XYZPDF Test Signing` 那张：`0x8007000b`；换成 subject 就等于清单 `Publisher` 的 `CN=allgoodhi`：rc=0，一次过 |
| `signtool verify /pa /v`（证书进了 `LocalMachine\TrustedPeople` 之后） | rc=0，签名与时间戳都验过 |
| `Add-AppxPackage -Path`（已签名） | exit=0，装进 `WindowsApps`，`SignatureKind=Developer` |
| WACK `-packagefullname`（已部署） | 33.3 秒跑完 24 条，exit=0，`OVERALL_RESULT=WARNING`，**22 PASS / 1 FAIL / 1 WARNING**，判词见 §8.6 |
| `程序清单`（ProgramInventory）任务 | 成功 —— §8.2 那条"松散注册的包它看不见"由此反向坐实 |
| 跑完之后包还在不在 | **不在了**：`appcert` 自己那个"部署/清理"任务收尾会把包撤掉，所以验完得重新 `Add-AppxPackage` |

这台机器的当前状态因此和 §5 末尾那句不一样了：装着的是**签名并部署**的那份，不是松散注册。
证书留在 `LocalMachine\TrustedPeople`（`CN=allgoodhi`，指纹 `221C6B38D487167AD5709C8AEBC64554714A0A83`），
那是为了拿本地判词加的，不是提交用的。

## 2. 提交前必须换掉的：发布者身份

清单写的是 `Publisher="CN=allgoodhi"`。这是自签占位，只够在本机装自己玩的包。

商店要求发布者与 Partner Center 账户的证书一致，而 `Identity/@Name` 与 `@Publisher`
**上架之后不可改** —— 改了就不是更新，是一个新应用，评价、下载量、评分全部留在旧的里面。
所以这两个值要在第一次提交之前一次定死，别等到那时才发现账户里的 CN 写法不同。

这两个值不敲命令行，写在 CMake 的 cache 变量里，由打包脚本从生成的头里读回去盖进清单：

```
cmake -B build-store -DXYZPDF_PACKAGE_IDENTITY_NAME=<保留名> `
             -DXYZPDF_PACKAGE_PUBLISHER="CN=<分配的发布者>" `
             -DXYZPDF_PACKAGE_DISPLAY_NAME=<显示名> `
             -DXYZPDF_PACKAGE_PUBLISHER_DISPLAY_NAME=<发布者显示名>
```

为什么绕这一道而不是直接给打包脚本传参：程序自己在运行时要靠这四个值算出**它以为**自己
住在哪个包族里（`src/windows/packageidentity.cpp`），显示名还进了窗口标题与 About。清单
与二进制不一致时，症状分别是"资源管理器菜单里出现两条同名项"（程序以为自己是散装，又往
注册表写了一份）和"开始菜单叫 A、标题栏叫 B"。见 §9.4。

`packaging/build-store-package.ps1` 在发布者还是 `CN=allgoodhi` 的时候**直接拒绝出包**，
不是警告 —— 理由见 §9.3。


## 3. 提交前必须补上的：文件关联

### 3.1 关联已经声明，装包不再空手而来

两个清单现在都带 `windows.fileTypeAssociation`（`.pdf`，带 `DisplayName`、`InfoTip`
与 `<uap:Logo>`），`makeappx` 接受 —— 它会按 `AppXManifestValidationRules` 拒掉不该
注册的扩展名（XYZip 的记录里 `.chm` 就是被直接拒绝的）。装上之后外壳确实登记了它，
观察结果见 §1.1。

### 3.2 为什么它必须写在清单里

`mainwindow.cpp:389` 的逻辑是：检测到本程序自己的包已安装，就调用 `unregisterVerbs()`
清掉注册表里的那份 —— 因为包里那份和注册表那份会并排出现两个同名菜单项，用户会当成
bug 报上来，所以让包赢。

注册表那套一共写三样（`shellintegration.cpp:225-293`），装上包之后是一换一换一加一丢：

| 注册表版本提供的 | 包版本 |
| --- | --- |
| `.pdf\OpenWithProgids` 里的 `xyzpdf.Document` | 换成 `AppX…` 一项，实测在 §1.1 |
| `xyzpdf.Document` 这个 ProgID（图标与命令都在它下面） | 由包的 `FileTypeAssociation` 与 `ApplicationIcon` 承担 |
| 成为"默认应用"候选 | **多了**：注册表那套从来没打算抢默认（见 284-286 行的注释），包可以 |
| `HKCU\Software\Classes\xyzpdf\shell\0001_open`，即右键菜单里那条"用 XYZPDF 打开"（`Position=Top`、`AppliesTo` 限定 `*.pdf`） | 一度**丢了**，现在由 §3.3 那个 `IExplorerCommand` 补回 |

### 3.3 右键那条已经补回来：包里带一个 IExplorerCommand

丢的那一项在 2026-09-22 补上了：`src/windows/shellcommand.cpp`（COM 外壳）加
`src/windows/shellverb.{h,cpp}`（判断与命令行，不带 COM，所以能直接编进测试里调用），
打成 `bin\xyzpdfshell.dll`，清单里三处一起声明 —— `com:SurrogateServer` 给出类，
`desktop4:Extension Category="windows.fileExplorerContextMenus"` 里的
`desktop5:ItemType Type=".pdf"` 给出"在哪些文件上出现"，`desktop5:Verb Id="xyzpdfopen"`
把两者连起来。

两条与 XYZip 不同的决定，都写在了文件头：

- **这个 DLL 不带 Qt**，因此也不需要 `previewdependencies.cpp` 那个 delay-load 钩子。
  它不读文档，只看扩展名然后起一个进程；在人家右键的那一下里加载 Qt 是菜单变慢的
  成本。这也是它单独成一个 DLL、而不是塞进 `xyzpdfpreview.dll` 的原因。
- **只有一个动词，不是子菜单**：`GetFlags` 回 `ECF_DEFAULT`，`EnumSubCommands` 回
  `E_NOTIMPL`。将来要加第二项（导出为图片之类）才需要 `IObjectWithSelection` 把选中项
  往下传给子命令。

非 PDF 的选择是**隐藏**而不是灰掉：`GetState` 对混合选择一律回 `ECS_HIDDEN`，因为灰掉
的行是一个没有答案的问题 —— 外壳这里不给 tooltip 的位置，而 `GetToolTip` 明确回
`E_NOTIMPL`（回空串和回"没有"是两件事）。

实测（2026-09-22，装包并注册之后）：右键一个 `.pdf`，主菜单里出现"用 XYZPDF 打开"，
点了把选中的都开成标签；右键非 PDF 时那一行不出现。语言取
`GetUserDefaultUILanguage()`，中文系统是"用 XYZPDF 打开"，其余是 "Open with XYZPDF"
—— 这个 DLL 没有 Qt，`tr()` 不存在，一种新语言要在这里加一行而不是加进 `.ts`。

清单里那段注释说的是同一件事，两边不要只改一处。

## 4. 提审时要填的说明，不是代码但会拒审

- **`runFullTrust` 是受限能力**。提交时要写为什么一个 PDF 阅读器需要完整桌面权限。
  诚实的答案就够：Qt 的桌面窗口与 PDFium 在同一进程内渲染，且要读写用户选的任意路径。
- **隐私声明必须提**。划词翻译会把选中的文本发到外部服务（`src/translate/translator.cpp`），
  这是"数据离开设备"，商店的数据收集表必须如实勾选并给出隐私政策 URL。
  区域截图、OCR 走系统服务，不出机器；导出、打印、阅读都不联网。
  第三方声明页已经在"关于"里（`src/ui/thirdpartydialog.cpp`）。
- **年龄分级/内容声明**：无用户生成内容、无网络交互入口，填最低档。

## 5. 包里的外壳处理器：路由怎么验的、预览窗格、缩略图

XYZip 有完整参考（`H:\test\XYZip\xyzip\src\shell\xyzippreview.cpp`，1865 行，含
delay-load 钩子与 DllHost 的坑位注释），**它只靠包身份注册** —— `com:SurrogateServer`
写在清单里，没有注册表回退路径。所以先卡着的未知是：**松散注册的包，外壳收不收这个
COM 声明**。

2026-09-22 用一个刻意不含任何依赖的探针（`src/windows/previewprobe.cpp`：一块色块、
几行关于自己所在进程的事实、外加往日志写一行）验过了，结论是**收**：

```
host=DllHost.exe, 64-bit  package=allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4
integrity=Medium (0x00002000)
```

十一行，对应资源管理器里逐个点过去的那十一次预览。三件事由此确定，每件都直接影响真实现
怎么写：

- **路由成立**，不需要签名包、不需要管理员。预览处理器现在就可以做。
- **宿主是 DllHost，不是 Explorer**，完整性 Medium。它不能假设自己在本程序进程里，
  也不能假设能写任意路径。
- **`%LOCALAPPDATA%` 被重定向了**：探针以为写的是 `%LOCALAPPDATA%\xyzpdf-preview-probe.log`，
  实际落在 `...\Local\Packages\allgoodhi.XYZPDF_edpkq8hzfe5n4\LocalCache\Local\` 下面。
  真处理器的临时路径要按这个算；而它旁边的 Qt 与 PDFium 仍要像 XYZip 那样 delay-load，
  因为宿主进程的工作目录是 System32。

探针回答的是路由问题，不是功能；同一天真处理器落地时把它替换掉了
（`src/windows/previewhandler.cpp`，走 `Document` 接缝，和阅读器同一个后端；清单里的
CLSID 与 `Path` 两处一起改）。

**怎么确认窗格里看到的是我们的，而不是 WPS 的**：外壳把处理器装在哪个进程里，那个进程
就映射着哪个 DLL，这是硬证据，不靠肉眼认图。开着预览窗格的时候：

```powershell
Get-Process DllHost | ForEach-Object { $_.Modules } |
    Where-Object ModuleName -match 'xyzpdf|preview' |
    Select-Object -ExpandProperty FileName
```

**别把 `tasklist /M xyzpdfpreview` 当同一条捷径**：`/M` 要的是完整模块名，**带 `.dll`**，
少扩展名它对任何进程都回"没有运行的任务匹配指定标准"。2026-09-23 就因此差点把"窗格正在
显示我们的页面"读成"根本没走我们的处理器"。正控一句就看得到这个坑：`tasklist /M ntdll`
同样一无所有，`tasklist /M ntdll.dll` 列出所有进程；而写对了的那条，实测确实给出
`dllhost.exe  <pid>  xyzpdfpreview.dll`。

2026-09-22 实测：

```
...\build-local-release\package\app\bin\xyzpdfpreview.dll
...\package\app\bin\Qt6Core.dll   Qt6Gui.dll   Qt6Pdf.dll   Qt6Network.dll
```

这一条同时正控了 delay-load 那个坑：Qt 的库全部来自包里的 `bin\`，而不是 System32 —— 钩子
确实把加载器带对了地方（否则处理器根本进不来，窗格只会说"没有预览"）。XYZip 的
`xyzippreview.dll` 在另一个 DllHost 进程里，两个处理器各占一个宿主，互不影响。

肉眼也有一条可辨的：标题行的 `· 共 N 页` 与往下滚的多页排版是我们自己的，页面上没有别家
会加的水印。

**这条取证方法的边界**：代理进程是用完就退的（`DllCanUnloadNow` 说可以退，宿主就真的退），
所以采样要赶在用之前。2026-09-22 再查一次时只剩预览那个宿主还活着，缩略图与右键的已经
退了 —— 那不是"没生效"的证据，只是没在跑。那两样的凭据是另外的：门禁用例把 DLL 当宿主
一样加载并跑一遍外壳的调用序列（`tests/thumbnailprovider_smoke.cpp`、
`tests/shellverb_smoke.cpp`），生效与否看人眼：图标视图里是第一页、右键菜单里有那一行。

### 5.1 缩略图处理器：包内注册是 schema 允许的，且必须是代理

`IThumbnailProvider` 在 Windows SDK 的 `thumbcache.h` 里（不在 `shobjidl_core.h`，
按预览处理器那次包含会找不到），IID `{e357fccd-a995-4576-b01f-234630154e96}`，
一个方法：`GetThumbnail(UINT cx, HBITMAP*, WTS_ALPHATYPE*)`。

包内怎么声明，是查 SDK 自带的 schema 而不是查文档得到的：
`FoundationManifestSchema.xsd` 里 `desktop2:ThumbnailHandler` 的 keyref 指向
`ComServer_SurrogateServer_Class_IdKey` —— 比预览处理器那条（`ComServer_Class_IdKey`，
代理与 exe 服务器都算）**更严**，只认 `com:SurrogateServer` 下的类。而打包用的
`com:ComServer` 里根本没有"进程内类"这个元素（只有 SurrogateServer / ExeServer /
TreatAsClass），所以包里的缩略图处理器必然跑在 DllHost 里，跟预览窗格一样要
delay-load。位置在 `uap:FileTypeAssociation` 里面，紧挨着 `DesktopPreviewHandler`
（`UapManifestSchema.xsd:188-189`）。

实现是 `src/windows/thumbnailprovider.cpp`，走同一条 `Document` 接缝。两处决定：

- **超采样只给 2 倍**，不是预览窗格那个 3 倍：缩略图最大也就 256 像素，而一个文件夹
  打开时是一次要几十张，成本要算。
- **打不开就不给图**（回 `E_FAIL`、`*phbmp` 置空），让外壳退回文件图标。回一张白图
  看着像"这个文档是空的"，那是假答案。

实测：图标视图/大图标下 PDF 显示第一页。注意外壳有它自己的缩略图缓存
（`%LOCALAppData%\Microsoft\Windows\Explorer\thumbcache_*.db`），改过处理器之后旧图
可能还在，先换文件夹或清缓存再看。

**当前机器状态**：`.pdf` 的默认程序已改成 XYZPDF，WPS 的预览被关掉了。撤掉：设置里把
`.pdf` 默认改回 WPS，或 `Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage`。

### 5.2 预览窗格改成工作线程渲染（2026-09-22）

PLAN §3.3 那条验收写的是"预览窗格在 DllHost 里不锁死、不泄漏"。原来那版撑不起"不锁死"的
强意义：页是在 `WM_PAINT` 里向 PDFium 同步要的，一张扫描页几百毫秒就是窗格几百毫秒不重绘，
而宿主那个消息循环也被我们一起占住。现在改成 DLL 自己的一条工作线程：它调同一个同步的
`Document::render()`，画完 `PostMessage` 给窗格自己的窗口；绘制只贴已经回来的图，没回来的
页给白纸＋页码。

三条不是顺手做的设计决定，记下来免得后人以为是简化空间：

- **不复用 `src/render` 的 `PageRenderer`**：Qt 的 `QPdfPageRenderer` 用排队连接把结果送回
  owner 线程，那要有 Qt 事件循环，DllHost 里只有 Win32 的一个。为了这个类去给 COM 代理塞
  `QCoreApplication`，代价大于收益；而且门禁用例存在的理由之一就是"没有 app object 也渲染
  得动"，不该反过来把这条性质废掉。
- **`relayout()` 不再逐页问 `pageSize()`**：Qt PDF 那把全局递归锁（`src/render/pagerenderer.h`
  开头）渲染也要拿。渲染搬到别的线程之后，每次 WM_SIZE 一页一问就会排在工作线程后面 ——
  卡顿换了个名字而已。页尺寸现在在 `open()` 里一次问完存下来。
- **卸载顺序**：`Unload()` 先 join 工作线程，再 `DestroyWindow`，再 `document_.reset()`。
  顺序反了任何一种都是别人进程里的 use-after-free。`DllCanUnloadNow` 现在也数工作线程。

已经测到的（`tests/previewhandler_smoke.cpp`）：可以把工作线程停在"正要渲染"那一步（导出的
`XYZPDF_PREVIEW_TEST_LATCH`，只为这件事存在）；停住的时候连画两次像素指纹不变、标题栏以下
没有墨，而滚动条范围照样发布 —— 所以版式不依赖渲染器；放开之后不必再触发绘制，墨自己回来；
被停住的时候调 `Unload()` 仍然返回，最后 `DllCanUnloadNow` 回到 `S_OK`（工作线程没泄漏，
否则那个计数永远不为 0）。这些断言等的都是条件，不是时间。

**还没看的**：真 Explorer 窗格里的观感 —— 白纸存在多久、往下滚后面的页跟不跟得上。这台机器
现在装着的是这一版（§5.3 末尾那条重装），观感的反馈也就有了，看下一节。

### 5.3 拖窗格宽度时那一下白纸（2026-09-23）

§5.2 那版落地并重装之后，用的人给的反馈是一条："拖窗格宽度的时候会突然变白纸一下，不过很
快就恢复了"。原因在 `relayout()`：它把"这张图是为别的宽度画的"当成"这张图没用了"，一有
WM_SIZE 就把整叠缓存丢掉，再按**那一刻**的宽度排一批新任务。拖 splitter 是连续几十次
WM_SIZE，于是丢几十次、排几十次，而每一次排的尺寸转眼又过期了。

改法都在 `relayout()` 的出口，两条：

- **要不要重画按页算，不按"宽度变了没有"算**。`pageFit()` 把缓存图自己的像素宽跟当前宽度会
  画出的像素宽比一次，答案有三种：没画过（`Missing`）、画过但比现在要的窄（`Soft`）、够用
  （`Current`）。已经更宽的图缩小本来就是清楚的，所以**缩窗格一次渲染都不欠**；只有变宽
  （且没撞 `kMaxRenderWidth` 上限）才欠活。
- **欠的活里只有"糊"可以等，"空"不能等**。`Soft` 页等宽度不动之后再排
  （`SetTimer(kSettleMs = 150)`，`WM_TIMER` 里先 `beginGeneration()` —— 在路上的那些是按拖动
  中途某个宽度要的，收回来，也顺手清掉 `outstanding_`，否则那些页被当成"已经在路上"而永远
  不再要 —— 再排队）。拖动期间窗格显示的是旧图按新宽度拉伸的结果（`blit()` 一直是
  `StretchDIBits`），所以观感从"白一下"变成"糊一下然后清楚"。`Missing` 页当场就排：它没有
  旧图可拉伸，等 150 ms 只是让那张白纸在屏幕上多留 150 ms。计时器挂在窗口上而不是挂在这个
  对象上：`DestroyWindow` 连它一起带走，不会有"窗格都关了还在排队"。

第二条是被第二轮反馈逼出来的，不是一次想到。第一版把 `Missing` 和 `Soft` 当成同一件事（"这
批页欠渲染，所以整批都等"），重装之后他的原话是"只有页码，没有内容，然后闪一下后就出现内
容"——那正是 §5.2 那个白纸占位符。也就是说第一版把"画过的页不该白"修好了，同时把"新拉进
视野的页"从不用等改成了要等；`PageFit` 那个三态与 `enqueueVisible()` 里
`fit == Soft && sharpenPending_` 那一行，就是为了不再犯这个。

门禁给这条改动留了可判的断言，用的还是 §5.2 那个 latch：先把队列抽干（`waitForStable`，
等的是"指纹一秒不变"这个条件），挂住工作线程，把窗格从 420 拉到 700 —— 此时任何新图都
回不来，所以"标题栏以下仍然有墨"测的就是这次改动本身；再等一次挂住，读它挂在哪个尺寸上
（latch 的第 4 个动作），必须比 420 那次大，这才是"停下来之后确实按新尺寸重画了"，而不是
"从此只贴拉伸图"。另一半要一份页数多过缓存（`kCachedPages = 6`）的文档，所以换
`many-pages.pdf`（40 页）：把窗格改窄改高，让一批从没画过的页进入视野，然后**不泵消息**直接
等 latch 挂住 —— 不泵就没有计时器，能挂住的那一个只能是 `WM_SIZE` 里当场交出去的活，这条
断言测的正是"缺页不等宽度"。三条都反证过：把 `relayout()` 改回清缓存，前一组红；把
`kSettleMs` 改成 60000，中间两条红；把 `enqueueVisible()` 那一行退回第一版的写法（缺页也
等），七条红，其中一条就是"拖动当中就该要没画过的页"。改回来之后门禁 24/24。

本机装着的是分开判断之后的这一版（包 15.8 MB，`InstallLocation\bin\xyzpdfpreview.dll` 与门禁
测的那个 DLL 同哈希 `6192AB79…`；看到"白纸"的那一版是 `D5325362…`，已经被换掉）。

**观感的结论（同一天，两轮）**："还是一样，算了这个不处理了。" 这条到此为止，**别再回头改**。
说清楚剩下来他看到的是哪一部分，免得下文被当成没做：拖动时"已经画过的页"现在是拉伸的旧图，
这一半按断言已经不白了；剩的是**从没画过的页进入视野那一小段白纸＋页码**，而那是异步渲染这个
模型本身的价格 —— 那张页上没有可拉伸的图，要在它出现之前拿到像素，就只能回到 §5.2 之前那种
在 `WM_PAINT` 里向 PDFium 同步要图画的做法，代价换成"一张扫描页几百毫秒窗格不重绘、宿主的
消息循环一起被占住"。两版都看过之后，他选的是不要那个交换。门禁里那三条断言留着，它们守的是
"不锁死"那一半（PLAN §3.3 的验收原文）。

## 6. 本机没有验证的

说清楚，免得下文被当成已确认的事实。今天从"未验证"挪走的三条在 §1.1。

- **代码签名证书**：本机自签那张已经走通（§1.3、§8.4），但那只能证明"这条流水线没有别的
  坑"。商店要的是买来的 EV/公司身份证书，那一栏还是空的，而且 §2 说的发布者身份也得跟着换。
- **双击 `.pdf` 会开在谁手里**：本机默认已经改成 XYZPDF（为了验预览那条），但"双击
  真的开起来"没有观察过。要改默认只能由人在设置里点 —— Windows 用只有它自己能算出的
  哈希保护默认值，程序自己去写就是恶意软件的做法（`shellintegration.cpp:284` 那段
  注释说的就是这件事）。
- **WACK 那一遍在 Partner Center 服务端会怎么说**：本机这遍是真判词了（包已签名、已部署，
  见 §1.3 与 §8.6）—— `OVERALL_RESULT=WARNING`，22 PASS / 1 条可选 FAIL / 1 条 WARNING。
  本机替不了服务端的：它跑的是同一套测试的哪一个版本、`#88 已阻止的可执行文件`（可选那条）
  它认不认 full-trust 桌面应用的豁免，都只有提交之后才知道。
- **`makeappx` 没有静态校验可当替身**：本想拿 `makeappx validate` 顶一下 WACK，这个版本
  （10.0.26100.8249）没有 `validate` 这个子命令，只回一屏 usage。所以包结构的校验是本地自己
  写的三条：打包脚本从 `.msix` 里把清单读回来核对（§1）、清单点名的文件逐个确认在（§1）、
  以及 `verify-package.ps1` 解开成品包问的三个问题（§9.5）。
- **商店包本身**：出得来、解得开、离开这台机起得来（§9.6），但它**从没被 Windows 装过**，
  也没跑过 WACK —— 因为发布者身份还是开发值（§2、§9.3 那条拒绝）。带真身份之后这两步要补。
  本机现在装着的是剥完负载之后重打的那一份，`InstallLocation` 下 `resources.pri` 已经在（§9.7）；
  2026-09-23 换成了带工作线程渲染与"拖动不白纸"的那一版，同日又换成把"缺页"与"页偏软"分开
  判断的那一版（§5.3 末尾两条哈希）。
  看过的四眼（2026-09-22，同一台机器、同一个包）：开始菜单与设置里的图标、双击打开、预览窗格、
  缩略图 —— 也就是"没有软件 OpenGL 会不会打不开"这一赌注已经收回。

已经验过、别再当未知的：包内 COM 服务器在松散注册下会被路由（§5）；包能启动
（§1.1）；`.pdf` 关联登记进了外壳（§1.1）；三个处理器的 CLSID 在装好之后各自指向
自己的 DLL（§7 末尾那条查询）。

## 7. 命令

```powershell
# 出包（不改机器）
python tools/makeappicon.py
python tools/build_all_tests.py
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-package.ps1

# 装上暂存布局（改机器：注册包身份，并清掉我们自己写的 HKCU 项）
#
# 注意顺序：松散注册的包会占住它注册的那个目录（DllHost 把里面的处理器映射着），
# 所以重新打包之前必须先撤注册，否则脚本清不掉暂存目录会失败并说明原因。
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-package.ps1
Add-AppxPackage -Register build-local-release\package\app\AppxManifest.xml
# 装完用真实 id 启动，id 从 Get-StartApps 读，别手抄
Get-StartApps | Where-Object Name -match XYZPDF

# 签名 + 真装进系统（改机器：往本机信任根里加一张测试证书）。坑见 §8.4。
# 证书 subject 必须就是清单的 Publisher（CN=allgoodhi），否则 signtool 只回 0x8007000b。
$c = New-SelfSignedCertificate -Type Custom -Subject 'CN=allgoodhi' -KeyUsage DigitalSignature `
      -FriendlyName 'XYZPDF publisher match test' -CertStoreLocation 'Cert:\CurrentUser\My' `
      -NotAfter (Get-Date).AddYears(3) `
      -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
Export-Certificate -Cert $c -FilePath publisher-match.cer
# 下面两行要在提权窗口里跑
Import-Certificate -FilePath publisher-match.cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople'
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage   # 松散注册占着暂存目录
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\build-package.ps1 -Sign -Cert $c.Thumbprint
Add-AppxPackage -Path build-local-release\package\XYZPDF_0.1.0.0_x64.msix
signtool verify /pa build-local-release\package\XYZPDF_0.1.0.0_x64.msix

# 撤掉
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage

# 认证测试。appcert 只能提权跑，结果怎么读见 §8 —— 从当前窗口拉起：
Start-Process 'E:\Windows Kits\10\App Certification Kit\appcertui.exe' -Verb RunAs
# 命令行版（同一个工具，跑完直接出 xml；参数含义与两条死路见 §8.1–§8.3）
& 'E:\Windows Kits\10\App Certification Kit\appcert.exe' test -appxpackagepath build-local-release\package\XYZPDF_0.1.0.0_x64.msix -reportoutputpath build-local-release\wack.xml
# 想要真判词就得给它已部署的包名（§8.6）；跑之前先 reset，否则它可能还在用上一轮的缓存
& 'E:\Windows Kits\10\App Certification Kit\appcert.exe' reset
& 'E:\Windows Kits\10\App Certification Kit\appcert.exe' test -packagefullname (Get-AppxPackage -Name allgoodhi.XYZPDF).PackageFullName -reportoutputpath build-local-release\wack-deployed.xml
# 注意：appcert 跑完会自己把包撤掉，想继续验外壳就重新 Add-AppxPackage -Path
```

### 7.0 商店那一个包（§9）

```powershell
# 身份与名字在配置时给，之后程序、清单、提交说的才是同一套（见 §2）。
# 先让门禁脚本建这棵树（它知道 generator 与 Qt 的位置；手工 cmake -B 一个新目录在这台机器上
# 会拿到默认的 NMake，而构建脚本与 CMakeLists 都按 Ninja 写），然后把四个值盖进它的 cache：
python tools/build_all_tests.py --build-only --build-dir build-store
E:\qt\Tools\CMake_64\bin\cmake.exe -B build-store -DXYZPDF_PACKAGE_IDENTITY_NAME=<保留名> `
    -DXYZPDF_PACKAGE_PUBLISHER="CN=<分配的发布者>" -DXYZPDF_PACKAGE_DISPLAY_NAME=<显示名> `
    -DXYZPDF_PACKAGE_PUBLISHER_DISPLAY_NAME=<发布者显示名>
# 重编一次：这四个值是编译进二进制的，只改 cache 不改二进制 = 清单与程序分家（§9.4）
python tools/build_all_tests.py --build-only --build-dir build-store

# 出包（不改机器；包与暂存布局落在 build-store\package-store）
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-store-package.ps1 -Build build-store

# 验它：解包、清单点名的东西在不在、有没有 glob 进来的垃圾、去掉 PATH 之后起不起得来
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/verify-package.ps1 `
    -Package build-store\package-store\XYZPDF_0.1.0.0_x64.msix -CarriesCrt

# 名字还没 reserve 之前想先把这条链路跑通：同上，只是那四个值换成一看就是假的 stand-in，
# 跑完把 cache 改回开发值、重编、删掉 package-store。今天这一遍就是这么做的，见 §9.6。
```

### 7.1 三个处理器：哪个 CLSID 在哪个 DLL 里

一个 CLSID 写错不会报错，只会让外壳"没有预览""没有缩略图""菜单里没那一行"，而三处
都要一致（源文件里一份、清单里两处）。三张对照：

| 生效于 | CLSID | 声明在 | DLL |
| --- | --- | --- | --- |
| 预览窗格 | `12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7` | `desktop2:DesktopPreviewHandler` | `bin\xyzpdfpreview.dll` |
| 图标视图缩略图 | `4032B809-B50E-4F0A-AFC1-D300803A81CE` | `desktop2:ThumbnailHandler` | `bin\xyzpdfthumbnail.dll` |
| 右键那一行 | `8AFE6540-7E46-4349-BE06-ECD418310E42` | `desktop5:Verb Id="xyzpdfopen"` | `bin\xyzpdfshell.dll` |

查"装好之后外壳实际收到了什么"，读的是注册后的那份清单，不是仓库里的：

```powershell
$loc = (Get-AppxPackage -Name allgoodhi.XYZPDF).InstallLocation
[xml]$m = Get-Content (Join-Path $loc 'AppxManifest.xml')
foreach ($n in $m.GetElementsByTagName('com:Class')) {
    'class  {0}  {1}' -f $n.GetAttribute('Id'), $n.GetAttribute('Path')
}
foreach ($t in 'desktop2:DesktopPreviewHandler', 'desktop2:ThumbnailHandler', 'desktop5:Verb') {
    foreach ($n in $m.GetElementsByTagName($t)) { 'ref    {0}  {1}' -f $t, $n.GetAttribute('Clsid') }
}
```

2026-09-22 实测（六行，三个 class 对上三个引用，没有孤儿）：

```
class  12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7  bin\xyzpdfpreview.dll
class  4032B809-B50E-4F0A-AFC1-D300803A81CE  bin\xyzpdfthumbnail.dll
class  8AFE6540-7E46-4349-BE06-ECD418310E42  bin\xyzpdfshell.dll
ref    desktop2:DesktopPreviewHandler  12DF01C2-01D6-4AF8-AD5F-969C3C3F88D7
ref    desktop2:ThumbnailHandler  4032B809-B50E-4F0A-AFC1-D300803A81CE
ref    desktop5:Verb  8AFE6540-7E46-4349-BE06-ECD418310E42
```

## 8. WACK：两遍 —— 前一遍它没看见我们的包，后一遍是真判词

2026-09-22 提权跑了好几轮（`appcert.exe`，kit 版本 10.0.26100.8249）。这一节分两层，别混着
读：**§8.2、§8.3 是包还没签名、只能松散注册那一阵的记录**，那 21 条 FAIL 是工具自己拿不到
路径，不是包的毛病；**§8.4 起是补上签名、真装进系统之后的**，那一遍才给出结论（§8.6，
22 PASS / 1 可选 FAIL / 1 WARNING）。写清楚这两层的差别，正是为了下一份文档别把前一遍的
FAIL 抄成"清单有 21 处毛病"，也别把后一遍那条 WARNING 当成缺了 DPI 声明。

先说权限那关（§6 记的就是它）：`appcert.exe` 与 `appcertui.exe` 都要求提权，非提权的
PowerShell 里连 `/?` 都启动不了（`Start-Process` 回"此操作需要提升"，Git Bash 一句
`Permission denied` —— 它的内嵌清单写着 `requireAdministrator`）。

### 8.1 参数表（提权之后从工具自己嘴里读的，不是网上抄的）

```
appcert [ test | finalizereport | reset | querytestids ] [options]
test  -packagefullname <包全名>   或   -appxpackagepath <.msix 文件>
      [-testid '[31]'] [-apptype ...] [-setuppath ...] [-waittimeout ...]
      [-reportoutputpath <xml>]
```

两个坑：`-testid` 的方括号是语法的一部分（`-testid 31` 顶回来"实际参数格式不正确"）；
退出码 0 是跑完，1 是要再调 `finalizereport`，-1 是命令行或**生成报告阶段**的错 —— 也就是
下面那条"报告不完整"的信号。

### 8.2 松散注册的包，WACK 的 `程序清单`（ProgramInventory）任务看不见它

`Add-AppxPackage -Register` 注册出来的包，`appcert test -packagefullname <全名>` 会走到：

```
INFO:ProgramInventory Inventorying application allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4
WARNING:Failed to find the package root path for allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4
ERROR[Infrastructure]: Method name: "ExecuteTask"  Error: The specified Windows Store App was not found.
```

它落盘的清单文件是个空壳：`programinventory_<guid>.xml` 里只有 `<ProgramList/>`。往后所有
靠它取路径的任务统一报"找不到此应用包的清单文件"，最后 `尝试创建报告时出错 / An error
occurred while additional data was being added to the report`，exit=-1，报告里没有
`APP_NAME`/`APP_VERSION`。

排掉的三个假说，都留了证据：

- **"包在 H: 上它找不到根目录"** —— 把整个暂存目录复制到 `C:\Users\allgoodhi\xyzpdf-wack-stage\app`
  再注册，`Get-AppxPackage` 回的 `InstallLocation` 就是 C:，工具自己的 `global_data.xml`
  里 `appTypeDetectManifestPath` 也确实写成了那个 C: 路径，`程序清单` 照旧失败。**盘符不是变量。**
- **"`reset` 把状态冲掉了"** —— 同一条命令行，加 `reset` 与不加 `reset` 都失败。
- **"是前置任务 `在 IDE 方案中执行应用包部署/清理` 半路把注册撤了"** —— 提权脚本里挂了个
  每 500 ms 采一次的轮询任务，整场 74 个采样点一次都没掉过注册（`registered
  C:\Users\allgoodhi\xyzpdf-wack-stage\app` 从头到尾），失败照旧。**它没撤，它就是看不见。**

### 8.3 `-appxpackagepath` 能出完整报告，但它是"不部署"的那种跑法

把包从注册里撤掉、改用 `-appxpackagepath <.msix>`，工具第一句话就说明了模式：

```
注意: 在不部署应用程序的运行情况下运行测试。
```

这一回 `程序清单` 成功，exit=0，报告是完整的（`APP_NAME=allgoodhi.XYZPDF`、
`APP_VERSION=0.1.0.0`、`VALIDATION_TYPE=CommandLine`）。结果 **2 PASS / 21 FAIL / 1 WARNING**，
24 条。可是那 21 条里几乎没有一条是对包本身的判词，按工具吐的原文分一下：

| 原文 | 条数 | 是什么 |
| --- | --- | --- |
| `System.ArgumentException: 路径的形式不合法` | 5 | #2 #11 #86 #87 #89：`Path.GetDirectoryName(空)` —— 没部署就没有根目录 |
| `平台元数据目录 不存在或不是目录` | 6 | #53–#58 整族元数据校验，工具自己的目录是空的 |
| `找不到此应用包的清单文件` | 4 | #52 #61 #62 #91：又回去查注册表里的包，见 §8.2 |
| `未将对象引用设置到对象的实例` / `该字符串无法识别为有效的 Uri` / `测试受阻` / `无法…提取已导入的 API 信息` | 6 | #77 #63 #45 #46 #70 #88，同一类：拿不到输入 —— #88 那半句是真的，见下 |

也就是说：**真正跑完并且给出结论的只有两条半** —— `#31 应用清单` PASS、`#90 资源包` PASS，
外加 `#92 DPIAwarenessValidation` 一条 WARNING。而 `#31` 这条是 PLAN 里"清单正确"那件事的
正面证据（它跑的是我们自己那份 `AppxManifest.xml`）。

WARNING 那条值得单独说，因为它看着像真缺陷：`#92` 先说"应用程序清单无效"，接着说"应用
allgoodhi.XYZPDF_… 不是 DPI 感知应用"。**它的前提不成立**，把暂存包里那份 exe 的内嵌清单
抠出来看：

```powershell
& 'E:\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe' '-inputresource:build-local-release\package\app\bin\xyzpdf.exe;#1' '-out:exe-manifest.xml'
```

里面明明白白写着 `dpiAware=true/pm` 和 `dpiAwareness=PerMonitorV2`（还有 `longPathAware`），
是 `xyzpdf.manifest` 经 `/MANIFESTINPUT` 合进链接器默认清单的结果 —— 见 `CMakeLists.txt:281`
那段注释，它记的就是 WACK 这一条。所以这次"不是 DPI 感知"是它自己没读到清单，不是我们漏了。

唯一一条讲的是我们文件的话，来自 `#88 已阻止的可执行文件`：

```
opengl32sw.dll / Qt6Core.dll / plugins\platforms\qwindows.dll  引用 kernel32!CreateProcessW、shell32!ShellExecuteW/ShellExecuteExW
bin\xyzpdfshell.dll  引用 kernel32!CreateProcessW
```

Qt 自己的进程启动路径，加上我们右键那一行为拉起阅读器用的 `CreateProcessW`。桌面型
（full trust）Centennial 应用允许这些 API，商店里每个 Qt 应用都带这一串 —— 记下备查，
今天不动它。

### 8.4 签名这一步：`0x8007000b` 说的是"证书名字不对"，不是"格式不对"

两条路走到同一个前提：**包得真的装进系统**（`-packagefullname` + 已部署），而不是松散注册。
装 `.msix` 要签名。这一段是补上签名时撞出来的，因为它的报错一路都在指错地方。

自签一张能签包的开发证书，参数只能写到这个最小集：

```powershell
# -Provider / -KeyAlgorithm / -HashAlgorithm 任加一个就 0x80090017 (NTE_PROV_TYPE_NOT_DEF)
$c = New-SelfSignedCertificate -Type Custom -Subject 'CN=allgoodhi' -KeyUsage DigitalSignature `
      -FriendlyName 'XYZPDF publisher match test' -CertStoreLocation 'Cert:\CurrentUser\My' `
      -NotAfter (Get-Date).AddYears(3) `
      -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
# 导成 .cer，提权塞进本机计算机的"受信任的人"，装包与 verify /pa 才认
Import-Certificate -FilePath publisher-match.cer -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople'
```

**`-Subject` 必须等于清单里的 `Publisher`。** 一开始那张写的是 `CN=XYZPDF Test Signing`，
`signtool sign` 打在 `.msix` 上就是：

```
Done Adding Additional Store
Error information: "Error: SignerSign() failed." (-2147024885/0x8007000b)
SignTool Error: An unexpected internal error has occurred.
```

`0x8007000b` 是 `ERROR_BAD_FORMAT` —— "企图载入的程序格式不对"。它把注意力整个搬到"appx SIP
装坏了 / 32 位 64 位搞混 / 包损坏"上去，而这三条**都逐一排除了**才找到真因：

| 试过 / 排除 | 证据 |
| --- | --- |
| 不是包坏了 | 同一张证书签 `bin\xyzpdf.exe`：rc=0，`verify /pa` 也过 —— 证书与私钥本身没问题 |
| 不是 `/o`（优化打包，会生成 block map） | 加 `/o` 与不加，出来的包**字节数一模一样**（27917453），签名同样失败 |
| 不是扩展名 | 改名 `.appx` 再签：一样 `0x8007000b` |
| 不是工具目录里那份 `appxsip.dll` 遮蔽了系统那份 | 把 `signtool.exe` 复制到一个干净目录（旁边没有 appxsip）再签：一样失败 |
| 不是 32/64 位 | x64 与 x86 两份 `signtool.exe` 都试过，表现一致；`.zip` 扩展名则老实报"这个文件格式无法签名"，说明 `.msix` 那条路上 SIP **是**加载成功了的 |
| 不是"SIP 没注册"（我一度这样记过） | 注册表键名是 **`CryptSIPDllCreateIndirectData`**，不是 `CryptSIPDllCreateIndirect`；按错名去查当然得 0 条，按真名查 appx/msix 六个回调全在（61 条，`AppxSip.dll`） |
| **就是发布者名字** | 重新签一张 `-Subject 'CN=allgoodhi'`（与清单 `Publisher` 一致），同一个包：`Successfully signed`，rc=0，一次过 |

`/fd SHA1` 那次还回了一个**不同**的错（`0x80091005`），进一步说明签名体内化在跑，只是它拒绝
的不是格式。appx SIP 在写入签名前会拿证书 subject 与清单 `Publisher` 比对，不匹配就用一个
与原因无关的 `ERROR_BAD_FORMAT` 失败。

所以 `build-package.ps1` 现在自己先比一遍，把这句话摆在人面前（比较按前缀，不按全等：
商店发的证书 subject 比清单里的 `CN=` 长，这是打包 API 自己的规则）：

```
the certificate subject is 'CN=XYZPDF Test Signing' but the package Publisher is 'CN=allgoodhi'.
signtool would fail with a meaningless 0x8007000b. Either make the certificate with -Subject 'CN=allgoodhi'
or pass -Publisher 'CN=XYZPDF Test Signing'.
```

同一天从 `-Sign` 这条路里还掉出来一个真 bug：脚本里 `$StageRoot` 是相对路径，而第 3、4 步用
的是 .NET 调用（`manifest.Save`、`ZipFile::OpenRead`）—— .NET 解析相对路径看的是**进程**
工作目录，不是 PowerShell 的位置。提权 `Start-Process` 起来的进程工作目录是
`C:\Windows\System32`，于是清单被存去 `C:\Windows\System32\build-local-release\package\app`，
报"未能找到路径的一部分"，而前三步全成功了，看着像清单坏了。现在 `StageRoot` 在拼任何子
路径之前先绝对化。修法是从 `C:\Users\Public` 用相对 `-Build` 跑通整条流水线验出来的。

自签那张只为在本地拿一份完整的 WACK 判词；商店提交要的还是真证书（§6）。它动的是系统的
信任根，得由人点头 —— 这一次是点过了的，机器现状见 §1.3 末段。

### 8.5 报告怎么读

工具自己的目录：`%LOCALAPPDATA%\Microsoft\AppCertKit\`（`AppCert_Trace_YYMMDDHHMM.txt` 是
trace，`global_data.xml` 是它这一轮实际拿到的路径与包名，`TaskSchedule.xml` 是任务顺序，
`Log_<任务名>_POST-PROCESS.xml` 是每个任务自己的落盘结果）。两个格式坑：trace 里中文和
英文被 NUL 串起来，直接读只看见中文，要按"连续 ≥4 个 ASCII 字节"抠才看得见 `WARNING:`
那些行；`appcert` 的 stdout 是 UTF-8 带 BOM，`Out-File` 之前别按 UTF-16 解。

报告 XML 里 `RESULT` 的结论是**元素文本**，不是属性（`<RESULT>FAIL</RESULT>`），拿属性去读
会全空。核对计数：

```powershell
python -c "import xml.etree.ElementTree as ET; r=ET.parse('wack.xml').getroot(); print([ (t.attrib.get('INDEX'), t.find('RESULT').text) for t in r.iter('TEST') ])"
```

### 8.6 包真装进系统之后：22 PASS / 1 可选 FAIL / 1 WARNING

```powershell
# 提权。签名并装好之后（§1.3），把已部署的那个包名交给它：
& 'E:\Windows Kits\10\App Certification Kit\appcert.exe' test `
    -packagefullname allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4 `
    -reportoutputpath build-local-release\wack-deployed3.xml
```

33.3 秒，exit=0。`OVERALL_RESULT=WARNING`、`APP_TYPE=Centennial`、`VALIDATION_TYPE=CommandLine`、
`PARTIAL_RUN=FALSE`、kit 10.0.26100.8249、`ReportGenerationTime 2026/9/22 17:02:37`。

`appcert` 打出来的 24 个任务**全部"执行结果成功"** —— 与 §8.2 那一轮（`程序清单` 直接失败）
是质的区别。逐条：

| 结果 | 条数 | 编号 |
| --- | --- | --- |
| PASS | 22 | #2 #11 #31 #45 #46 #52 #53 #54 #55 #56 #57 #58 #61 #62 #63 #70 #77 #86 #87 #89 #90 #91 |
| FAIL（`OPTIONAL=TRUE`，不拦提交） | 1 | #88 已阻止的可执行文件 |
| WARNING | 1 | #92 DPIAwarenessValidation |

**#88** 报的是"引用了进程启动 API"与"扫到了可疑字符串"两类：

```
bin\opengl32sw.dll / bin\Qt6Core.dll / plugins\platforms\qwindows.dll  → kernel32!CreateProcessW、shell32!ShellExecuteW / ShellExecuteExW
bin\xyzpdfshell.dll                                                    → kernel32!CreateProcessW
D3Dcompiler_47.dll / opengl32sw.dll / Qt6Core.dll / Qt6Gui.dll / Qt6Pdf.dll / qt_sv.qm  → 对 "cmd"/"Reg"/"CSI"/"CdB"/"reG" 的"不可执行引用"
```

第一类里属于我们自己的只有一处：右键那一条命令要用 `CreateProcessW` 把阅读器拉起来
（§3.3 那个 `IExplorerCommand`）。其余是 Qt 自带的运行库，搬不掉。第二类连 `qt_sv.qm`（一份
翻译文件）都被算进去了，说明它是按字节串扫的启发式，不是调用图。full-trust 桌面 Centennial
应用允许这些 API，商店里每个 Qt 应用都带这一串 —— 而这一条是 `OPTIONAL=TRUE`，所以它没有把
`OVERALL_RESULT` 拉到 FAIL。真要清楚知道答案，只有提交之后看 Partner Center 那一遍。

这一轮之后包变了：上面那三行里的 `opengl32sw.dll`、`D3Dcompiler_47.dll` 与全部 Qt 自带的
`.qm` 已经不在包里（见 §9.6 末尾）。重跑一次 #88 的名单会短一截，`qt_sv.qm` 那条字符串误报
也跟着消失，剩下的是 `Qt6Core.dll` 的进程启动 API 与我们自己那一条 `CreateProcessW`。没有
重跑 —— 结果已经拍板接受（"暂时就这样"），换不来的那一半只有商店签发的包能给。

**#92** 是唯一 `OPTIONAL=FALSE` 却只给 WARNING 的，而它两句话自己就打了架：

```
未能处理二进制文件 C:\Program Files\WindowsApps\…\bin\xyzpdf.exe。
应用 allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4 不是 DPI 感知应用。
```

第一句是它的解析器读不了那个 exe，第二句是读不到之后的默认结论。而这一次不是拿暂存那份
凑数 —— 直接对**已装进系统的那份** exe 抠内嵌清单：

```powershell
& 'E:\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe' `
  '-inputresource:C:\Program Files\WindowsApps\allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4\bin\xyzpdf.exe;#1' `
  '-out:installed-manifest.xml'          # rc=0，普通权限也读得动 WindowsApps 里这个文件
```

出来的是 `dpiAware=true/pm`、`dpiAwareness=PerMonitorV2`、`longPathAware=true`。所以这条按
"工具没读到"记，不按"我们没声明"记；它也没拦整体结果（`OVERALL_RESULT=WARNING` 而不是 FAIL）。

还有一件只有跑了才知道的事：**`appcert` 收尾会把包撤掉**。它那个"在 IDE 方案中执行应用包
部署/清理"任务在测试之后把注册清干净了，所以跑完 `Get-AppxPackage` 是空的 —— 想接着验预览
窗格或右键，得再 `Add-AppxPackage -Path` 装一次。第二轮就是栽在这上面：脚本先 `reset` 再查
包，直接 "not installed, stopping"。

一句边界：这一遍证明的是"包在本机能签名安装、并且过了本机这套 kit"。它**不是**"WACK 通过
= 商店会认证通过"，服务端那一遍我们没跑过（§6）。


---

## 9. 商店包那一条链路

§1 到 §8 讲的都已经验过了。这一节是"提交给商店的那一个包"：它的图从哪来、它的身份由谁
盖、以及本机拿什么验它。今天之前这条链路是不存在的 —— 有两个清单、一个 `build-package.ps1`，
商店那个清单**没有任何脚本会读它**。

### 9.1 仓库里那个商店身份是编出来的

`packaging/StoreAppxManifest.xml` 之前写着
`Publisher="CN=3C3318D2-C146-4BCB-AE5B-29787FFF4D89"`。这个 GUID 在仓库里**别处找不到**，
也没有任何后台有它 —— 是写那个文件那天顺手填的。现在改回 `CN=allgoodhi`，跟散装清单一致。
改它的理由不是整齐：

- 身份本来就要被打包脚本盖掉（§9.3），所以这个字段的唯一作用是"让读的人以为它说明了
  什么"，而它说明的是"名字已经在 Partner Center 保留过了"。这件事没有发生。
- 假 GUID 比明显的占位更难查：`CN=allgoodhi` 谁一眼都知道是自签，`CN=<36 位>` 看着像从
  后台抄来的。真去 reserve 的时候，人更容易相信一个像真货的值。

顺带把两个清单的关系写进文件头了：内容上它们是**同一份**，只有语言表由
`tools/add_language.py` 同时写两边，其余（尤其是三个 CLSID）没有东西在替我们盯着。

### 9.2 四张图不够，而且 Windows 不是按路径找磁贴的

`tools/makeappicon.py` 现在出 25 张：四张基础图各带 `scale-125/150/200/400`，
`Square44x44Logo` 再补 `targetsize-16/24/32/48/256`（按像素要的场合：树、任务栏、标题栏）。
两件事值得记：

- **不生成 `_altform-unplated`**，虽然 XYZip 有。unplated 是"把底板去掉只留图案"的那种磁贴，
  而这张画的红色圆角方块本身就是图案 —— 给它一个 unplated 版本等于自己盖自己一层底板。
- 尺寸算的是 `int(w*scale/100+0.5)`，不是 `round(w*scale/100)`。Python 的 `round` 是"四舍
  六入五成双"，`round(62.5)` 给 62 而不是 63，于是 `StoreLogo.scale-125.png`（50 的 125%）
  会小一个像素。文件名里的 scale 是给人看的，Windows 按那个数去要像素，少一个就是拿一张由
  它自己再缩放过的图。四张基础图的尺寸与 XYZip 那套一致（`StoreLogo` 两边都是 50×50）。

清单里点名的还是那 4 个文件（外加 `Properties/Logo`，也是 `StoreLogo.png`）。多出来的 21 张
靠 `resources.pri`：Windows 拿着 `Assets\Square44x44Logo.png` 这个名字**不是去开这个路径**，
而是问资源系统，资源系统再按当前 DPI/尺寸从那 25 张里挑 —— 名字里带 `.scale-150` 那类后缀
的才是候选。所以 `build-package.ps1` 现在多两步 `makepri`：

```
makepri createconfig /cf priconfig.xml /dq lang-zh-CN_lang-en /o
makepri new /pr <暂存目录> /cf priconfig.xml /mn <暂存清单> /of <暂存>/resources.pri /o
```

`/dq` 那个语言列表是从清单的 `<Resource Language>` 里读出来的，不是写死 —— 但读的方式踩过
一次：`GetElementsByTagName('Language')` 什么都没有，因为 `Language` 是 `<Resource>` 的
**属性**而不是元素。当时的表现是脚本抛"清单里没有 <Resource Language>"，一份有两个的清单。
两个坑记完：`priconfig.xml` 要放在暂存的 `app` 目录**外面**，否则它自己会被打进包。

缺 `resources.pri` 长什么样，值得再说一遍，因为它跟成功一模一样：包装得上、`Status Ok`、
25 张图一张不缺，开始菜单那条有名字、**没有图案**。没有报错可追，所以 §9.5 那个脚本把它
列成硬性检查项。

### 9.3 身份与名字都只有一个来源：build 生成的那两头文件

`packaging/build-store-package.ps1` 只做商店独有的三件事，其余全部交给
`build-package.ps1`（它因此多了 `-Manifest / -IdentityName / -DisplayName /
-PublisherDisplayName / -CrtDirectory` 五个参数 —— 抄两份暂存与索引代码等于两次机会只修
其中一份）。三件事是：

1. **身份从 build 里读，不从命令行传**：`build/generated/xyzpdf/packageconfig.h` 里的
   `XYZPDF_PACKAGE_IDENTITY_NAME` / `_PUBLISHER` / `_DISPLAY_NAME` /
   `_PUBLISHER_DISPLAY_NAME`，加上 `version.h` 的 `XYZPDF_VERSION_QUAD`。这样二进制与清单
   说的必然是同一套（§2 说的两个症状就是这么来的）。版本还要与 `CMakeLists.txt` 的
   `project(... VERSION ...)` 交叉核对一次 —— 不一致就是这棵树没重新配置，报"树过期"，
   而不是悄悄出一个自相矛盾的包。
2. **算出包族名，并在身份还是开发值的时候拒绝出包**。算法：发布者按 UTF-16LE 取 SHA-256，
   前 8 字节按高位在前摊成一个整数，末尾补一位，写成 Crockford base32
   （字母表 `0123456789abcdefghjkmnpqrstvwxyz`，没有 i/l/o/u）。这套算法与它自带的自校验一起
   从 XYZip 搬来（那边是拿两对已知值反推出来的，见它 `build-store-package.ps1` 的同名函数）；
   本机跑的是那条自校验：微软那个发布者必须算出 `8wekyb3d8bbwe`，算不出来就直接停 —— 因为
   下面所有的判断都要用它。拒绝的条件只看发布者：`Identity/@Publisher` 是包族名的一半，而推导
   是单向哈希，用 `CN=allgoodhi` 签出来的包**永远不可能**等于 reserve 的那个族名，提交被打回来
   时只会说一句很短的话。（XYZip 第一版这检查要求两个值都是开发值才拒绝，于是"先填保留名、
   publisher 忘了改"这个最自然的顺序反而静默通过。）今天对着开发身份实跑过一遍，报的就是那段
   话，而且它在暂存之前就停住，没留下 `package-store`。
3. **把 C 运行库带进包**。机器装商店包不要求装过 redistributable，缺 `vcruntime140.dll` 是
   启动时一个弹窗，程序自己报不出来。找它的目录优先用 `$env:VCToolsRedistDir`，没有就问
   `vswhere`（它待在一个固定路径上就是为了在没有初始化的 shell 里也能找到）；挑工具集版本
   按 `[version]` 排，不按文本排 —— 按文本 `14.9` 会赢过 `14.51`。

`windowskit.ps1` 是这一节共用的找工具代码：先 PATH，再读注册表 `KitsRoot10\bin`（64 位机器
上两份都要看：`WOW6432Node` 那份和直读那份），再猜 Program Files，并且过滤掉路径里带
`\x86\ \arm\ \arm64\` 的命中。这台机器上 `makeappx` / `signtool` / `appcert` 在
`E:\Windows Kits\10\App Certification Kit\`（不在 PATH 上，§1 早写过），但 **`makepri` 不在
那里**，它在 `bin\10.0.<x>\x64\` —— 这就是不能沿用旧查找表的原因。

### 9.4 名字也只有一个来源，正反两次都验过

`Edition::productName()` 之前写死 `QStringLiteral("XYZPDF")`。现在它返回
`XYZPDF_PACKAGE_DISPLAY_NAME` —— 也就是同一个配置值同时进窗口标题、About、清单
`Properties/DisplayName`（"设置 > 应用"里那个）与 `uap:VisualElements/@DisplayName`
（开始菜单里那个）。这不是整洁癖：`build-package.ps1` 会盖清单里的两个显示名，如果标题栏
还是字面量，两者就会分家，而分家的表现是"开始菜单叫 A、标题栏叫 B"。

这条通道有牙，两个方向都跑了：

| 跑法 | 结果 |
| --- | --- |
| 把 stand-in 显示名 `XYZPDF Standin` 配置进去、重编、出包、`verify-package.ps1` | 通过（标题与清单一致） |
| 只把 `productName()` 改回字面量 `"XYZPDF"`、重编、出同一个包 | 报 1 条：`xyzpdf.exe put up a window titled 'XYZPDF' rather than 'XYZPDF Standin'` |

第二次就是这条检查存在的理由 —— 也是为什么它比"看到有个窗口"强：见 §9.5。

### 9.5 `verify-package.ps1`：验的是"离开这台机它起不起得来"

三个问题，逐条来自踩过的坑（脚本文件头有全版）：

- **该在的在不在**：`resources.pri`、`bin\` 下的 exe 与三个处理器 DLL、七个 Qt DLL、四个
  `plugins\` 子目录、清单点名的图与两张 `scale-150`；带 `-CarriesCrt` 时再要那三个运行库。
  本地包不带这个开关（本机有 redistributable，要它反而是在报一个不是问题的"问题"）。
- **不该在的在不在**：`vc_redist.x64.exe`（windeployqt 对"编译器运行库"的理解：一个 18 MB
  的安装器，塞进一个不能跑安装器的包里）、`*.lib/*.pdb/*.ilk/*.obj`、任何 `*_smoke.exe` /
  `*_bench.exe`，以及 `bin\opengl32sw.dll`、`bin\D3Dcompiler_47.dll` 和 `translations\` 下
  任何 `.qm` —— 这三样 `build-package.ps1` 第 2b 步会扔出去（见 §9.6 末尾），它们回来就说明
  那条路被谁改过。glob 出来的负载第一次露馅通常是提交体积翻倍。
- **能不能独立起来**：把包解到临时目录，`PATH` 只剩 `system32;Windows;Wbem`，起它，然后把
  它加载的每个模块往回追是谁给的 —— 只要有一个来自解包目录之外就算失败。**判据是窗口的
  标题**，因为缺 DLL 与缺 Qt 平台插件都会弹窗，而弹窗也是个窗口，"起了并出现一个窗口"在每
  一个失败场景里都成立。标题取的是解出来的清单里的 `Properties/DisplayName`，不是写死在这
  个脚本里（XYZip 那条踩的正是写死：两个版本共用一份检查脚本，付费版的显示名不是 `XYZip`）。

两条与本项目特别相关的：跑之前先看有没有 XYZPDF 在跑 —— 单实例网关会让第二个进程把参数
交出去就退，脚本会把这读成"包里的程序一启动就退"，所以它拒绝猜。以及它现在对那三样用不上的
东西是**报错**而不是 note —— 早先是只报个数不拦，因为"能不能不装它"这句话脚本答不了；打包脚本
第 2b 步既然已经把它们扔了，它们回到包里就只可能是出错了（§9.6 末尾）。仍然留作 note 的是
`dxcompiler.dll` / `dxil.dll`：今天的包里根本没有这两个，它们回来与否则取决于 Qt 的版本。

### 9.6 今天本机跑出来的数

```
配置一个 stand-in 身份（XYZPDFStoreTest / CN=B2E1D3F4-… / 显示名 XYZPDF Standin）
python tools/build_all_tests.py --build-only          # 全部目标重编，身份编译进去
powershell -File packaging/build-store-package.ps1    # 出包
powershell -File packaging/verify-package.ps1 -Package 'build-local-release/package-store/…' -CarriesCrt
```

- 包族名：`allgoodhi.XYZPDFStoreTest_wnj647vny8s1j`（算法与微软 `8wekyb3d8bbwe` 自校验都过）
- 商店包：27.4 MB / 101 个文件 / 解包 67.3 MB，`resources.pri` 在，25 张磁贴全在，
  运行库落在 `bin\` 下（跟 exe 同级才是 app-local）：`vcruntime140.dll`、`vcruntime140_1.dll`、
  `vcruntime140_threads.dll` 与 5 个 `msvcp140*`
- 本地包（同一棵树、`CN=allgoodhi`、不带运行库）：26.7 MB / 91 个文件 / 解包 65.4 MB
- 两个包 `verify-package.ps1` 都过：从解包目录起来、`PATH` 上没有 Qt，9（+3）个模块全部
  来自解包目录，窗口标题与清单一致
- 实验做完：cache 里的四个身份值改回开发值、重编、门禁 26 项目标全过、`package-store` 与
  临时解包目录都删了。`grep -c` 过，stand-in 那三个值在 `CMakeCache.txt` 里已经是 0 处

上面那两个包的体积是**剥掉之前**的数。`verify-package.ps1` 每次都 note 出来的 **34 个文件、
29.3 MB「带着但可能用不上」**，同日量过并决定扔：`bin\opengl32sw.dll` 19.7 MB、
`bin\D3Dcompiler_47.dll` 4.0 MB、`translations\` 下 32 份 Qt 自带的 `.qm` 共 5.6 MB（程序的
界面翻译是自己那两份，`qt_add_translations` 编译进二进制、`src/core/languages.cpp` 只加载
`:/translations/xyzpdf_*.qm`，所以 Qt 那 32 份程序一次都不打开）。这三样是
`qt_generate_deploy_app_script` 装的，不是 `CMakeLists.txt` 里那行 windeployqt 的
`--no-opengl-sw --no-system-d3d-compiler --no-translations` 管的那条路 —— 那三条开关在，东西
照样进了包。

扔在打包脚本的第 2b 步（`build-package.ps1`，`cmake --install` 之后、盖清单之前），没有回到
CMake 里去，理由是一个真的限制而不是口味：`qt_generate_deploy_app_script` 只暴露
`NO_TRANSLATIONS` 一个关键字，那两条 DLL 没有对应的开关，所以一半写在 CMake、一半写在打包
脚本会是一个有洞的清单；一处列全，才有一处可查。商店包走的是同一份 payload 与同一个打包脚本，
少的是同样这 29.3 MB，没有为它单独重打一次去量。

剥完的本地包：`15.8 MB`，解包 `36.1 MB / 59 个文件`（原来是 26.7 MB / 65.4 MB / 91 个）。
`verify-package.ps1` 判定通过 —— 这一条顺带回答了"装完之后打不开"那个赌注的一半：解到临时
目录、`PATH` 只剩 `system32;Windows;Wbem` 起得来，标题对，九个模块全部来自解包目录。另一半
（真用户点开的那台机器要不要软件 OpenGL）由重装之后的那一眼回答，过了，见 §9.7。

踩到的一个 PowerShell 坑，记在这里因为它会让一条检查永远说假话：`FileInfo` 对象在其文件被
`Remove-Item` 之后仍留在数组里、`Count` 仍然对，但 `Length` 读回来是 `0` —— 这条第一次跑出来
印的是 `threw away 34 file(s), 0 MB`。所以先量 `$thrownBytes` 再删。

### 9.7 这条链路还没验的部分

- 名字没 reserve，所以商店包**从没被 Windows 装过**：上面那包只是解到临时目录直接跑的，
  没有 `Add-AppxPackage`，也没有对它跑 WACK。带真身份的包出来之后，这两步跟 §1.3、§8 一样
  要重跑一遍（签名证书得换成与那个发布者同名的）。
- 本机现在装的是剥完之后重打的那一份（同日 `Remove-AppxPackage` → `build-package.ps1 -Sign
  -Cert <指纹>` → `Add-AppxPackage -Path`）。`resources.pri` 这一项是查出来的：
  `Test-Path (Join-Path (Get-AppxPackage -Name allgoodhi.XYZPDF).InstallLocation resources.pri)`
  现在回 `True`（装进 WindowsApps 的是 58 个文件，比解包数少一个是 `AppxBlockMap.xml` 之类
  不进安装目录的）。脚本那边 §9.2 与 §9.5 判定通过，眼睛那边同日也过了：开始菜单与设置里的
  图标在、双击打得开、预览窗格与缩略图照旧 —— 所以"扔掉软件 OpenGL"这件事在这台机器上
  是成立的，商店那边换的机器若没有正常显卡驱动，才是要重新看的一天。
- 商店那一边还要填的元数据（隐私声明、截图、年龄分级、`Product identity` 里的实际值）在 §4。

## 10. 第一个原生依赖：QPDF（2026-09-23）

M5（编辑）的第一步不是功能，是把依赖接进来 —— 记在这里而不是记在别处，因为一个原生依赖要
回答的两个问题都是发布的问题：**包多大**，和**第三方声明页要不要多一条**。

- **装了什么**：vcpkg port `qpdf` 12.4.0，许可证 `Apache-2.0 AND MIT`（`ports/qpdf/vcpkg.json`
  里写着，商店可再分发），它自己拉 `zlib` 与 `libjpeg-turbo`。命令是在仓库根跑
  `H:\test\vcpkg\vcpkg.exe install --triplet x64-windows`（manifest 在仓库根，不在 `xyzpdf/`），
  两分钟装完，落在 `vcpkg_installed/x64-windows` —— 那目录 `.gitignore` 第 55 行已经挡住了。
- **真实负载**：`vcpkg_installed/x64-windows/bin` 里四个 release DLL 共 4,235,264 字节，可
  `dumpbin /nologo /dependents qpdf30.dll` 只点名 `z.dll` 与 `jpeg62.dll` —— `turbojpeg.dll`
  （1.0 MB）没有谁导入它。所以要进包的是 3,212,288 字节 ≈ 3.1 MB 三个文件。这条区分是 §9.4
  那一轮剥负载换来的教训："依赖列表"和"运行时真打开的文件"不是一回事，报数要说清报的哪个。
- **包体积今天没变**：`xyzpdf.exe` 里没有一处调用它，链接 QPDF 的只有
  `xyzpdf_qpdf_smoke.exe`，那不进包 —— 接完依赖又把打包跑了一遍（不带 `-Sign`，只为量文件），还是
  15.8 MB / 57 个条目，`qpdf30.dll` 与 `jpeg62.dll` 只出现在 `build-local-release/tests/` 里，
  staging 不收那里。量完又带 `-Sign` 打了一遍并重装（本机现在装的就是它：`xyzpdf.exe` 与三个
  处理器 DLL 四个文件逐一对过哈希，都等于构建树里的那一份）。现在把三个 DLL 拷进去就是 §9.4 刚扔掉的那类东西：没人打开的字节。
  **一个名字上的坑**：包里本来就有一个 `plugins/imageformats/qpdf.dll`，那是 Qt 6.11.2 自己的
  imageformats 插件（`ProductName` 就是 Qt6），与 QPDF 这个库无关，在成品包里 grep "qpdf" 会
  同时命中它 —— 别把它当成新依赖已经进来了。
- **CMake 怎么接的**：`find_package(qpdf CONFIG QUIET)`，找不到就 `FATAL_ERROR` 把上面那条命令
  原文打出来 —— 这里不用 vcpkg 的 toolchain file（Qt Creator 拿不到，§ 前文那条注释说的就是
  这件事），所以路径是写死在仓库根的，新机子忘了这一步就会在这里停住。另外
  `xyzpdf_add_test()` 现在会把 vcpkg 的 release DLL 拷进 `<build>/tests`：
  `tools/build_all_tests.py` 只往 `PATH` 前面加 Qt 的 bin，测试 exe 没有别的路找到 `qpdf30.dll`。
  这是 XYZip 的做法（它的 `CMakeLists.txt` 里那段 glob 循环），连注释一起搬的。
- **测到了什么**（`tests/qpdf_smoke.cpp`）：把 fixture 复制进临时目录、用 QPDF 拿掉一页、写出
  新文件，然后**两个库各自重新读一遍** —— QPDF 数对，Qt PDF 也打得开、页数一致、第一页有墨
  （"不是空画布"这个判法与 `document_smoke` 同一个理由：渲染出来的页是墨浮在透明上）。
  收尾问两句哈希：fixture 和被读的那份副本都一个字节没动。两处反证：把 `removePage` 注释掉
  → 三条红，其中一条正是 Qt PDF 那个页数，所以第二个读数不是白读的（它比的是 `改之前 - 1`，
  不是比 QPDF 自己刚报的那个数 —— 拿第一个答案去核对第一个答案不叫第二意见）；往副本里追加
  一个字节 → 只有"副本"那条红，"fixture"那条照旧绿。门禁 25/25。
- **下一片要补的两件**：第一处调用落地时，`build-package.ps1` 要把那三个 DLL 拷进包（并且
  `verify-package.ps1` 该反过来盯着它们在不在）；`src/core/thirdpartynotices.*` 要加一条
  `Apache-2.0 AND MIT` 的条目 —— PLAN §4 M4 那栏"第三方声明页"，商店会看。
  两件都在 §11 里做掉了，但**包还没有带着那三个 DLL 打过一次**，见 §11 末尾。

## 11. 删页：第一处真的调用 QPDF 的功能（2026-09-23）

PLAN §10 那条"下一片"就是这一片。落地的东西：`src/edit/documenteditor.{h,cpp}`（§7.3 里那个
"修改接口"，故意不是 `Document` 的方法）、`src/ui/deletepagesdialog.*`、菜单栏多一个**文档**、
`DocumentTab::deletePages()` 把三者串起来 —— 结果开在**新标签**里，原来那份继续在屏幕上，可以
当场对一下。PLAN §4 M5 那句"另存为默认不覆盖原件"到这一片才算有实现；**修改模型和撤销栈还
没有**，所以现在是"改一次、存一次、不能反悔"。

- **它只写新文件**：`saveAs()` 第一件事是拒绝"目的地就是刚打开的那份"（`sameFile()` 先比规范化
  字符串，再比 `canonicalFilePath()` 一遍，后者只会得出"相同"、永远说不出"不同"，因为不存在的
  路径返回空串，而那正是另存为的常态）。口令由 `DocumentTab` 记下来再递第二次：阅读器已经用
  它解开过一次，口令本来就在内存里，不是从用户那里再要一遍。
- **暂存写、一次落位，以及为什么不是 `QTemporaryFile`**：第一版用 `QTemporaryFile` 生成暂存名，
  `QPDFWriter` 对着一个本函数三毫秒前刚建出来的文件报 `open …xyzpdf-save-….pdf: Permission
  denied` —— Qt 6.11 的 `QTemporaryFile` 到对象析构之前一直握着创建时那个句柄，而 `closeHandle()`
  在 Qt 6 已经不是公开成员（这一条是编不过才知道的，不是想出来的）。现在改成**只挑名字、不建
  文件**（照 XYZip `ArchiveModify::temporaryOutputPath` 那个写法，名字里带毫秒戳，撞上了就顺延，
  已存在的文件从来不会被截断），写完再用一次 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`
  落位。`QFile::rename` 不够：它拒绝踩已存在的目标，而保存对话框刚刚问过"这个位置已经有文件
  了，要覆盖吗" —— 用户说是，然后程序说他不行，那比不问还糟。失败时那份已经写好的文件**留着**
  并且在消息里点名，因为"目标被别的程序占着"这件事比任何一个对话框都活得久。
- **测到了什么**（`tests/documenteditor_smoke.cpp`，门禁 26/26）：都是"删三页、写到临时目录"，
  量的是 open + delete + write 三步。

  | 文档 | 页数 | 大小 | 时间 |
  |---|---|---|---|
  | `many-pages.pdf`（合成） | 40 | 12 KB | 2 ms |
  | `CH340.PDF`（含照片的数据手册） | 6 | 168 KB | 6 ms |
  | `huge.pdf`（合成，每页一行字） | 600 | 186 KB | 39–42 ms（冷启动那一次 91 ms） |
  | `VerilogHDL硬件描述语言.pdf` | 172 | 4.85 MB | 63 ms |

  钱花在**字节**上，不在页数上：6 页 168 KB 比 600 页 186 KB 便宜一个数量级，因为整本文档是
  重新一份一份写出去的。数量级在几十毫秒，所以一个等待光标就够，这一片不起线程。真实文档那
  两行是喂命令行量的（`xyzpdf_documenteditor_smoke.exe <路径>`，与 perf.md 那两张表同一个套路），
  文件不在仓库里，别人复现不了；不喂参数时这一段根本不跑，门禁不依赖 `H:\pdf_test\`。跑完照旧
  断言那份真实文件的哈希一个字节没变 —— 仓库外的文件是唯一一份，这条不许靠信任。
- **加密的两份都是量出来的，不是假设的**：带用户口令的 —— QPDF 直接 `invalid password`（读者
  那边已经输过口令打开了，所以功能没坏，只是必须把口令再递一次，这就是 `documentPassword` 那个
  成员存在的理由）；递了口令之后写得出去，**写出来的那份仍然要口令**、且确实少一页 —— 保护不会
  经过一次编辑就掉。"谁都能读、但写着不许修改"的那一份（空用户口令 + modify forbidden）：读得出、
  写得成，副本不要口令就能打开。这一份的权限标志到底传没传到输出，当时**没测**，是欠账 —— §11.2
  第一条把它测了。
- **反证**：i18n 那一轮把一条新探针的源文改成目录里不存在的字符串 → `DocumentEditor: "…" is
  translated` 立刻一红，所以新加的 8 条探针不是摆设（`type="unfinished"` 的条目 lupdate 照收、
  lrelease 照编、装载照成功，只有查字符串才看得见）。落位那一步没有反证 —— 反证要真的占住目标
  文件，测试里做了（`QFile holder` 握着它），Windows 让不让替换不由这个进程决定，所以两种结果
  都印出来、只断言两边都成立的那条不变量。
- **§10 欠的两件做掉了**：`build-package.ps1` 现在按名字拷 `qpdf30.dll / z.dll / jpeg62.dll`
  （不 glob，`turbojpeg.dll` 那种没人导入的别想搭车，见 §9.4），`verify-package.ps1` 的
  `$required` 与 `$traced` 各自盯住它们；`Resources/licenses/qpdf.txt`（上游 NOTICE + Apache-2.0
  全文，从 `vcpkg_installed/.../share/qpdf/copyright` 原样取的）进 `resources.qrc`，第三方声明
  页多一条 QPDF。
- **这一轮没验的**：包没重打 —— 15.8 MB / 57 个条目那个数自 §9.6 之后没动过，三个 DLL 一进包
  就是新的数，而 `xyzpdf.exe` 现在**确实**导入 `qpdf30.dll`（不再是 §10 那种"链接了但没人调用"），
  所以带着它们跑不起来是一种会发生的失败；GUI 一次没手点（对话框、菜单项、另存为、结果开新
  标签、状态栏那句回执），删页是这个程序第一次往用户的文件夹里写东西，值得手点一遍再谈完成。

### 11.1 上面那两条补上了（同一天，"验证下"）

- **包重打了**：`build-package.ps1`（不带 `-Sign`、不带 `-Install`，机器一点没变）出
  **16.9 MB / 17,701,043 字节**，`verify-package.ps1` 全绿 —— 解包 **39.2 MB / 60 个文件**，对着
  §9.6 那次的 36.1 MB / 57 个，多的正好是那三个文件、3.1 MB。"跑不起来"这一条排掉了：PATH 上
  只有 Windows 的情况下它开出了窗，而 `qpdf30.dll` 是被**追到包内路径**的，不是这台机器的 PATH
  喂的。**装进系统的那一份还是 `863ddca` 的**，这一片没重装 —— 装要签名、签名要改机器。（这一条
  到 §11.2 作废。）
- **GUI 真点了一遍**（`build-local-release/xyzpdf.exe`，40 页的合成文档）：Alt+D → E 开出**删除
  页面**；范围空着时"删除这些页"是灰的，回执行说"这样一页也不剩了"；敲 `2-4` 之后回执变成
  "共 40 页：删除 3 页，剩下 37 页。"、按钮亮了；另存为是**原生**对话框，起点是源文件所在文件夹、
  文件名填着 `sample 删页.pdf`、类型 `PDF 文档 (*.pdf)`。
- **覆盖原件那条链，在真界面上走到底了**：把文件名改成正在打开的 `sample.pdf` → Windows 自己先
  问"确认另存为"→ 点是 → 程序回一句"另存为不能覆盖正在打开的文件。请换一个文件名。"，磁盘上
  那份哈希 `aa2cf697…` 一个字节没动。这一步值得单独记：测试里 `sameFile()` 挡的是函数调用，这里
  挡的是"用户已经对着系统的覆盖确认说了是"之后的那一次写。
- **成功路径**：新标签 `sample 删页.pdf` 自动激活，状态栏"已删掉 3 页，剩下 37 页，写成了一个
  新文件"，右下角页码 `1 / 37`。这个 37 是 **Qt PDF 重新读了一遍 QPDF 写出的文件**报出来的 ——
  §10 那套"两个库对账"第二次发生在真程序里，不在测试里。再把写出文件的内容流逐个 inflate 出来
  数页面上的字：留下的正好是 1 和 5–40，**删掉的正是 2、3、4**（只数 `/Count` 说不出这个）。
  暂存文件没留残渣，`xyzpdf-save-*.pdf` 一个都没有。
- **驱动方式的坑，记下来免得下次重踩**：`SendKeys` 打的是前台窗口，而前台会被无关程序抢走 ——
  这台机器上就有一个 XYZip 的"解压完成"框抢了过去，那几个数字进了它的对话框。改成往**具体
  hwnd** 发消息之后根本不需要前台。两处细节都是量出来的：Qt 对 `WM_KEYDOWN` 自己会做
  `ToUnicodeEx`，再补一条 `WM_CHAR` 就是每个字符进两遍（截图里真的出现 `22--44`）；`BM_CLICK`
  用 `SendMessage` 会卡在按钮点出来的那个模态循环里（"确认另存为"那一次就是这么挂住的），
  要用 `PostMessage`。
- **收尾**：scratch 目录删了；顺手把被这次验证改到的两处程序设置恢复回去（最近文件列表里那两条
  `gui-verify` 路径、`lastOpenDirectory`），因为注册表是装在系统里那份共用的。
- **下一件**：M5 地基的另外两件（文档修改模型 + 撤销栈），然后才是"就地保存"和重排/旋转持久化。

### 11.2 欠的两件：权限标志，和带着删页的签名包（同一天，"继续处理剩下的两件"）

- **权限标志传到输出了，而且是两份都传**：带口令的那一份 `R6 P-1028`（raw 也是 -1028）
  `assembly=0 annotate=1 other=1`，删一页写出来还是这三个数；"谁都能读、写着不许修改"的那一份
  `R6 P-1036` `other=0`，写出来还是 `P-1036 other=0`。**副本没有在一次编辑之后悄悄改口**。
  两个 fixture 正好差一个 bit（值 8，"改正文"），这就是 `P-1028` 与 `P-1036` 的全部区别。
- **这些新断言不是摆设，反证做了**：在 `DocumentEditor::saveAs()` 里加一行
  `writer.setPreserveEncryption(false)` → 门禁红 **9 条**：8 条是新加的（`the copy's /P is the
  original's…`、`same R, same P…`、两组 modify flag、"这份 fixture 自己到底允许什么"），第 9 条是
  本来就有的 `the copy is still encrypted`。红得最老实的是那句跨 fixture 的防自洽检查：
  `the two fixtures disagree about modification (0 vs 0)`。删掉那一行，回到 **26 passed / 0 failed**。
  - **反证顺带量出这一条自己的边界**：`check(!after.modifyOther, "the copy still says … may not be
    modified")` 在控制下**没有红** —— 一份完全不加密的文件，`allowModifyOther()` 报的就是 false，
    "不许修改"于是显得成立。它单独看是一句假话，只有和同一块里 `after.encrypted` 那半句一起才说得
    通。写下来是为了让下一个人不把这条当成守得住的那道门。
- **为什么"传到输出"是一件要盯的事，而不是一条读读文档就完事的性质**：QPDFWriter 的加密默认是
  *保留*，但这个默认会被三样东西悄悄关掉 —— prefiltering、内容规范化、QDF 模式 —— 而且只要调用
  过 `setR6EncryptionParameters` 就不保留了（`QPDFWriter.hh` 自己写着）。也就是说，将来任何一行
  为了"输出更好看"而加的开关，都可能把删页变成一台"进去是受保护文档、出来是 unprotected 副本"的
  机器，而且**没有任何一处会报错**。所以 `saveAs()` 里现在有一条注释指着这件事，测试里那两个数
  指着它。
- **`rawPermissionBits()`：同一个数读第二遍**。上面每对数字都还从 `isEncrypted(R, P)` 之外又量了
  一次 —— 直接把文件里的 `/Encrypt n 0 obj … /P m` 那几个字节抠出来。理由是这两份文件是**同一个
  解析器**读的：假如写的时候字典整个掉了、读的时候又默默报一个默认值，只问 QPDF 会把同一个错答案
  听两遍。`/Encrypt` 字典本身从不加密、也从不进对象流，所以这些字节就是写进去的那些。两个数一个
  来自原始、一个来自库 —— 上面那两条 raw 与解析值相等，就是在说这条通道活着。
- **签名包重打并装上了**：`build-package.ps1 -Sign -Cert 221C…0A83 -Install` 出
  **16.9 MB / 17,713,093 字节**，比 §11.1 那次不签名多 **12,050 字节** —— 那是签名本身。
  Authenticode 查回来 `status=Valid`、签名人 `CN=allgoodhi`、时间戳戳到了 DigiCert。
  - **装的时候先撞上 0x80073CFB**：`Add-AppxPackage -Path` 说"同一标识已经装着、且不允许装旧的
    或同版本"。版本号只有一个来源（`project(... VERSION ...)` 还是 0.1.0.0），所以每次重打都是
    同版本 —— 这条不是 bug，是这台机器上重装必经的一步：`Get-AppxPackage -Name allgoodhi.XYZPDF |
    Remove-AppxPackage` 再 add，装完 `Status = Ok`。
  - **装进去的就是刚编出来的那份**：`bin\` 下七个文件（`xyzpdf.exe` 加三个 shell 处理器 DLL 加
    `qpdf30 / z / jpeg62`）在 `C:\Program Files\WindowsApps\allgoodhi.XYZPDF_0.1.0.0_x64__…\bin`
    与包内暂存树两边 SHA-256 逐个相同。没跟上一次部署的那份比哈希 —— 重新链接一次哈希就变，比了
    也说明不了源码；这一轮 `src/windows/` 一个文件没动（`git status`）。
  - **包内清单**：解包 **39.2 MB / 62 个文件**，比 §11.1 的 60 多两个，多的正是签名造出来的
    `appxsignature.p7x` 与 `appxmetadata\codeintegrity.cat`（把解包树与暂存树做集合对比列出来的：
    暂存 58 + `appxblockmap.xml` + `[content_types].xml` + 那两个 = 62）。负载一个字节没多没少。
  - **装好的那份真的在跑，QPDF 真的从包里加载**：`explorer.exe shell:AppsFolder\
    allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF` 开出 `XYZPDF` 窗口，进程模块表里
    `qpdf30.dll / z.dll / jpeg62.dll` 三条的路径全在 `WindowsApps\…\bin` 底下 —— 不是这台机器的
    PATH 喂的。看完 `CloseMainWindow()` 关掉了，最近文件列表没被这次启动改到。
  - `verify-package.ps1` 也对着**签名后的**这个 .msix 重跑了一遍：全绿。
- **这一轮仍然没验的**：Explorer 的预览窗格与缩略图在这次重装之后没有再手点一遍（§11.1 点的是
  `build-local-release/xyzpdf.exe`，那是开发树不是包）；同理，**装在系统里的这一份没走过删页**，
  走通的是开发树那份。两者的 `xyzpdf.exe` 现在是同一次编译的字节，所以这条欠的是"部署链路"而不是
  "代码路径" —— 但它是这个程序第一次往用户文件夹里写东西，商店包真装真用之前值得点一次。

## 12. 修改模型 + 撤销栈 + 脏标记 + 旋转（2026-09-23）

§11 欠的那两件里的"下一件"，做完的正是它：**M5 地基的修改模型和撤销栈**，加上第一处把
`/Rotate` 写进文件的旋转页面。PLAN §4 M5 那句"增量保存"在这一片划掉了，理由写在 §4 原地。

- **模型是"一串操作"，不是"一份改过的文档"**：`DocumentEditor` 里现在只有 `pending` / `undone`
  两条 `Operation` 列表，加一份从列表**重算**出来的 `live`（哪些原始页还在、按什么顺序写）。
  `record()` 是唯一入口（追加、清掉 redo 分支、`rebuildLive()`），`undo()` / `redo()` 只是在两条
  列表之间搬一次再重算 —— 这是 `live` 唯一不可能和 `pending` 打架的原因，而打架的样子是"撤销之后
  页码数和对不对得上一串操作"。`pageCount()` 读的就是 `live`，所以内存里那个数跟着删掉的页走，磁盘上
  那份永远不动。
- **操作记的是"原始页号"，不是"当时的位置"** —— 这一片唯一真正的坑，也是 `live` 存在的理由：
  读者看着的始终是**打开那一份**（下面一条说为什么），所以"删掉第 4 页"永远指他屏幕上那第 4 页，
  哪怕前面已经删过两页、它在待写列表里已经排到第 2 位。写盘时 `apply()` 一边重放一边用
  `positionsOf()` 把原始号换成当时的位置。**反证**：把两处换算改成"直接当位置用"（`at = original`
  / `at = page`）→ `documenteditor_smoke` 一跑就是 **5 条 FAIL**，红的全是钉这一件事的断言：
  `and the one after it skips the page that was named second, not the one that happened to sit at
  that position by then`、`and the third page of the result is the sixth of the original, which was
  numbered against the screen it was named on, not against the list`、`and the turn landed on the
  page that was named`、`the page turned right says 90`、`the one turned left says 270, which is
  what a left turn is`。改回来 **26 passed / 0 failed**。
- **保存时重新打开源文件、整份重放**（§4 划掉增量保存之后的必然形状）：两个数各自的计时区间不一样，
  分开记 —— 40 页 12 KB 的 **3 ms** 只套着 `saveAs()` 一步（它自己包含"重开源文件 + 按列表重放 +
  整份写出"）；600 页 186 KB 的 **62 ms** 套的是 `open` + 删 3 页 + `saveAs` 三段，和 §11 那次
  39–42 ms 是同一个区间，量级没动。被测的这两次列表里都只有 1 个操作；"删两页 → 转两页 → 再删一页
  → 转一页"那条四步序列另存了一个文件、**没单独计时**，所以"列表长几倍会不会变慢"这一条还是没数。
  钱花在字节上而不是页数上，这点和 §11 一样；所以还是只有一个等待光标，不起线程。
- **屏幕不动**（这一片定下来的一条，不是没做完）：编辑不会改 `QtPdfBackend` 里那份，也不重启
  文档。代价是"撤销/重做在屏幕上看不见"，换来的是页码不会在读者眼皮底下重新编号 —— 而模型记的
  本来就是原始页号，所以两者不会不一致。要落盘还是走 `保存修改为`，结果开在**新标签**里（§11 的
  行为，一个字没改）。
- **旋转落在 `/Rotate` 上**：`QPDFPageObjectHelper::rotatePage(quarterTurns * 90, relative=true)`。
  两处是踩出来的：这个成员函数**不是 const**（`const std::vector<QPDFPageObjectHelper>` 编不过），
  而 `rotatePage` 加的度数只接受 90 的整倍数，所以 `rotatePages()` 先拒掉 1/2/3 之外的输入 ——
  PDF 存不下别的角度，替读者"就近取整"是让他从页面上发现。
- **三个读数，两个引擎**（同一个防自洽的理由）：页面**身份**用 Qt PDF 的 `text()`（fixture 自己
  印着 `XYZPDF page N`）；转了**没有**用 `pageSize()`（595×842 ↔ 842×595，渲染器把 `/Rotate` 折进
  尺寸里，见 `qtpdfbackend.cpp` 那段注释）；转了**哪个方向**只能回头用 QPDF 读原始 `/Rotate` 整数，
  因为右转 90 和左转 90 在尺寸上一模一样。读 `/Rotate` 那一遍先要 `isInteger()`：没写这个键的页
  是个 null 对象，对它调 `getIntValue()` 每页印一条 QPDF WARNING —— 加了守卫之前一次跑出 35 条。
- **界面上多出来的**：`旋转页面…` 对话框（一组方向 + 与删页同款的页码范围框，两处解析都走
  `Print::parsePageRange`，所以"1-3,5,8-10"这种写法在两个对话框里是同一个意思）、**文档**菜单里的
  `撤销 / 重做`（标题带着对象名："撤销 删除 3 页"，做菜的词来自 `DocumentEditor` 的
  `undoDescription()`）、`保存修改为…` 只在脏的时候亮、标签标题带 ` *`。关标签和关窗口各有一道
  **丢弃确认**（`closeTab()` 与 `closeEvent()`，后者先数有几个标签带着没写进文件的修改）。
- **双语**：lupdate 之后 48 条空的 `type="unfinished"` 全部填了，`tests/i18n_smoke.cpp` 的探针表里
  4 条换掉了 —— 它们的源文在这一片被改走（`已删掉 %1 页，剩下 %2 页…`、`删掉这些页，另存为`、
  `%1 删页.pdf`、`共 %1 页：删除 %2 页…`，四句在目录里 `grep -F` 都是 0 命中），留着就是一句永远
  查不到东西的假探针；同时补进旋转对话框、`保存修改为`、丢弃确认与撤销名字的探针。**反证**：把
  `删除 %1 页` 那条在 .ts 里退回 `type="unfinished"`（就是 lupdate 刚跑完的样子）→
  `DocumentEditor: "删除 %1 页" is translated (got "删除 %1 页")` 一红；还原 → 26/26。这一轮还修掉
  一个不在测试里就会静默的洞：撤销名字原来写在匿名命名空间的自由函数里，lupdate 对它只留一句
  `tr() cannot be called without context` 就**不收进目录**（那一遍的 337 条里没有它），英文菜单会把
  中文的"删除 3 页"原样印出来；现在 `describeOperation()` 是 `DocumentEditor` 的成员，上下文才对
  得上 —— 第二遍 lupdate 是 341 条（4 new）。
- **一道确认顺手管到的另一件事**：把标签**拖到别的窗口**（`detachTabToNewWindow`）走的也是
  `closeTab()` —— 那边按路径重开一份、这边关掉，所以带着未保存修改的标签被拖走时也会问一句。句子
  本身没说错（"关掉这个标签就会把这些修改丢掉"），但读者心里那一下是"移动"不是"关闭"，这一问在
  拖拽之后会显得突兀；要不要为拖拽单独改措辞，留给他看一眼再定。
- **这一轮没验的**：整片的 GUI **一次没点** —— 旋转对话框、菜单里的撤销/重做标题会不会太长、
  脏标记那个 ` *`、两道丢弃确认、以及"保存之后新标签"在新菜单下的样子，全是读过代码不是看过窗口。
  包里装的仍是 §11.2 那份，不带这一片。删页那一条上一轮在开发树里点通过，这一轮的改动动的是它
  脚下的模型，所以值得再点一次再谈完成。（→ §13 点完了，那一句"没点"作废。）
- **仍然欠的**：M5 的**重排**（`live` 的注释里给它留着位置：一个会打乱顺序的操作）；就地保存；
  加密文档走编辑时"必须把口令再递一次"这件事在界面上怎么交代。

## 13. 这一片在真窗口里点了一遍，包也换过来了（2026-09-23）

§12 末尾那句"整片的 GUI 一次没点"是这一轮的活。用的是开发树 `xyzpdf.exe` 加一份 40 页 fixture
（`tests/fixtures/many-pages.pdf` 拷成 `build-local-release/gui-verify/scratch/doc.pdf`，12539 字节，
sha1 `d354e705f0c0baf29a5e8724bf01de665f7ff451`），真鼠标真键盘，不是 `PostMessage` 打进去的。

- **点到了的**：
  - `旋转页面…`：一组方向 + 与删页同款的页码范围框，小结那句"向右转 90°：3 页。"跟着范围框走。
  - **文档**菜单的标题：转完是 `撤销 向右转 90°（3 页）`，再删一页变成 `撤销 删除 1 页` 且 `重做` 灰；
    两下 `Ctrl+Z` 退到空之后反过来 —— `撤销` 灰、`重做 向右转 90° (O)` 亮、`保存修改为` 灰，而标签和
    窗口标题上那个 ` *` **一起消失**（`isDirty()` 就是"列表非空"，撤销到原点算回到不脏）。最长的一条
    是 `重做 向右转 90° (O)   Ctrl+Y`，菜单没被撑出屏幕，`(O)` 落在描述后面读着不别扭。
  - 脏标记：`doc.pdf *` 在标签上、在窗口标题上，切到隔壁标签再切回来还在。
  - 保存 → 新标签：另存框预填 `doc 已修改.pdf`、开在源文件那个目录；存完源标签**仍带星**（§12 说的
    "列表还是待写的"），新标签不带星，状态条回执
    `已写成一个新文件（39 页）：向右转 90°（3 页），删除 1 页`。
  - 落盘那份对着文件核过：39 页、12382 字节、`/Rotate 90` 正好三个，落在**第 2、3、4 页**（页面自己
    印着 `XYZPDF page 2/3/4`，身份不是推出来的），第 10 页不在 `/Kids` 里 —— 删的、转的都是当初屏幕上
    说的那几页。源文件 sha1 一个字没变（跑完之后又核了一遍，还是 `d354e705…`）。
  - 两道丢弃确认，原文照抄：关标签 —— 标题 `还有没保存的修改`，正文
    `「doc.pdf *」还带着没有写进任何文件的修改：向右转 90°（3 页），删除 1 页。`
    `关掉这个标签就会把这些修改丢掉。要关掉吗？`，按钮 `是(Y) / 否(N)`，**默认停在 否**；关窗口
    （`Alt+F4` 那条路，走 `closeEvent`）—— 同一句标题，正文
    `这个窗口里有 1 个标签带着没有写进任何文件的修改。关掉窗口就会把这些修改丢掉。要关掉吗？`，
    数出来是 1，隔壁那个干净标签没被算进去。两边都按了 否，标签和窗口都留着。
  - 删页在新模型上重点了一遍：小结是 `现在这份有 40 页：再删 1 页，剩下 39 页。`，
    `这样一页也不剩了` 那条灰 `确定` 的挡法还在，页码框走的是同一个解析。
- **"屏幕不动"看过了它长什么样**：源标签带着"转 3 页 + 删 1 页"的待写列表时，第 10 页**照样画在窗口里**，
  状态条 `10 / 40`。§12 把"不动"定成设计，这里补上读者实际看到的东西：点完 `删除页面` → `确定`，
  眼前一页都不会少，屏幕上留下的只有标签上多的 ` *` 和菜单里多的那行撤销。**两个对话框各自底下都
  写着这一句**（`原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。`），所以
  "改了但还没落盘"是说在前头的，不是让读者从页面上发现 —— 这一片不用改什么，要改的只是"点完确定之后
  要不要再给一句回执"，那是口味问题，留给他定。
- **方向这一条是重做了一遍才成立的**（先把收回的写下来）：这一轮前半段专门拍了"源文件那页 + 顺时针"、
  "副本那页 + 逆时针"三张对照图，回来发现 `b1`/`b2`/`c1` 的裁剪区**逐字节相同**（同一个 sha1）——
  那两次点击根本没动到画面；而 `a8`/`a9`/`a10` 三张写着"副本第 2 页"的图其实停在**第 1 页**（图上印的
  就是 `XYZPDF page 1`），所以当时说的"横版正立文字"看的是一张**没转过**的页。看图看出结论再回头补图，
  这已经是这一轮第三次了（前两次是把菜单读成 `撤销 删除 3 页（第 2-4 页）`、把页数读成 37，都在当场收回）。
  重做的对照：副本里带 `/Rotate 90` 的页（视图旋转为 0，`适应整页`）→ **横版，`XYZPDF page 4` 从上往下
  排、字头朝右**；源文件第 2 页（没写 `/Rotate`）按一下 `Ctrl+R`（工具栏那条 `顺时针旋转`）→
  **同一个样子**。
  两者一致，`向右转 90°` 的说法和屏幕上看到的转向对得上。至于是"`/Rotate` 与视图旋转相加"这一条，
  这一轮**没有单独验**（那是渲染器复合的事，代码只递了一个视图角）。
- **两个新问题**：
  1. **页码框空着 = 整份**。`Print::parsePageRange("")` 的语义是"全部"（打印对话框里那一条），两个
     编辑对话框都复用它，于是 `旋转页面` 一打开：方向默认选中、范围框空着、`确定` 是**亮的** ——
     一下回车就把 40 页全转了（这一轮真的发生过一次，随后撤销）。`删除页面` 同一套算法，只是它有一条
     `leavesNothing` 把"删 40 页"挡成了"这样一页也不剩了"，所以只有旋转这条路能一脚踩到底。两个对话框
     里本来就写着"还没有说出要转/删哪几页"的分支，那是空框该被当成"没说"时才走的 —— 现在永远走不到。
     改法（**没做**，等他定）：范围框空着时把"说了几页"算成 0，让那条分支活过来、`确定` 跟着灰。
  2. **确认句里带着星号**：`「doc.pdf *」还带着没有写进任何文件的修改` —— 那个 ` *` 是标签的脏标记，
     被 `tab->title()` 原样搬进了句子，于是一句话里"没有写进任何文件"说了两遍。该传的是文件名，不是标题。
- **顺手看到的、不属于这一片**：改过缩放模式之后状态条的页号会和屏幕上的页不一致 —— `Ctrl+2` 重排完，
  整页可见的是第 5、6 页，状态条还写着 `2 / 40`；再按一次翻页键就又对上了。
- **包换过来了**：`build-package.ps1 -Sign -Cert 221C…0A83 -Install` → `XYZPDF_0.1.0.0_x64.msix`
  **16.9 MB**（17727477 字节）：扔掉 34 个文件 29.3 MB 程序从不打开的 Qt 负载、带上 3 个 QPDF 运行时
  DLL、`resources.pri` 索引 25 张图、清单点的 8 个文件全在。`signtool verify /pa` 过（sha256 +
  RFC3161 时间戳）。`verify-package.ps1`：解包 39.2 MB / 62 个文件，**在 PATH 上没有 Qt 的情况下从
  解出来的包里起来了**。装之前先 `Remove-AppxPackage` 撤掉 §11.2 那一份 —— 版本号还是 0.1.0.0，不撤
  就装不上第二份。装好之后从 `shell:AppsFolder\allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF` 起过一次、开着
  那份 fixture，**文档菜单里 `删除页面(E)… / 旋转页面(R)… / 撤销 / 重做 / 保存修改为` 五项都在，
  干净状态下后三项是灰的** —— 这一片真的在部署的那一份里，不只是在开发树里。
- **点这一轮用的脚本**在 `build-local-release/gui-verify/`（`kit.ps1` 真键盘+截图、`post.ps1` 枚举窗口、
  `step.ps1` / `click.ps1` / `crop.ps1` / `fields.ps1`）。那个目录是构建产物、gitignore 掉的，上一轮
  同一套脚本就是这么丢的（这一轮是从对话记录里重打了一遍才有的）；要不要挪进 `tools/` 归他定。
- **这一轮的坑，记下来免得再踩**：
  - `关闭标签` 的快捷键在 Windows 上是 **Ctrl+F4**（`QKeySequence::Close`），**不是 Ctrl+W**。上一轮
    "按 Ctrl+W 结果关掉了一个 Edge 标签、于是把整遍判为污染"—— 错不在按键泄漏，在于 **Ctrl+W 根本不
    是这个程序的键**，发出去的每一句都落到了别的窗口上。要看键是什么，得把菜单打开照着读。
  - 屏幕 2560×1440，Read 工具给的图是 1920 宽 —— 从图上量的坐标一律要乘 4/3，或者干脆只用
    `GetWindowRect` 拿。
  - `PostMessage` 的 `WM_KEYDOWN` 打不进 Qt 对话框里的 `QLineEdit`（试过三次，框一直是空的）；先真
    鼠标点进那个框、再 SendKeys 才有字进去。原生文件对话框（`#32770`）是另一回事，它的 `Edit` 是真
    窗口句柄，`WM_SETTEXT` 直接写。
  - 标签条在屏幕 y≈130 那一条上，往下 30 像素就已经是页面区了 —— 点错一下就什么也不发生，而画面
    不会告诉你没点到。
- **仍然欠的**：M5 的**重排**；就地保存；加密文档在界面上怎么交代口令；上面两个新问题（空框 = 整份、
  确认句里的星号）要不要动（→ §14：两条都改了）；标签**拖去别的窗口**时那一句"关掉这个标签…"的措辞
  （§12 末条），这一轮没有真的拖过；还有删页小结里那个 `现在这份有 40 页` 的 40 到底跟着**待写列表**走
  还是跟着磁盘那份走 —— 这一轮只在"还没删过"的时候看过它，连着删两次才看得出来，而
  `DocumentEditor::pageCount()` 读的是 `live`，所以代码说"前者"，屏幕上没验。

## 14. §13 撞出来的两条，改完又在真窗口里看了一遍（2026-09-23）

改的都是三行的事，值得单独一节是因为两条的**验证**都不在代码里：一条要看对话框刚打开的那一帧，
一条要读屏幕上那句原话。

1. **空页码框不再等于整份。** `RotatePagesDialog::pagesToRotate()` 和
   `DeletePagesDialog::pagesToDelete()` 在框里没字（`trimmed()` 之后空）时直接交回空列表，
   不再把空串喂给 `Print::parsePageRange`。**解析器一个字没改** —— 打印对话框里"没选"和"全部"
   本来就是同一个默认，那两条断言（`tests/print_smoke.cpp:56-57`：`empty means all`、
   `whitespace means all too`）照旧通过；改的是这两个不能"没选"的调用方。
   于是两个对话框里本来就写好的那条分支活了过来（§13：`现在永远走不到`）。
2. **丢弃确认里不再有星号。** `MainWindow::confirmDiscardingEdits()` 传的是
   `QFileInfo(tab->documentPath()).fileName()`，不再是 `tab->title()`。星号回到了它该在的地方 ——
   标签条上。

**点到的（开发树 `build-local-release/xyzpdf.exe`，22:01 新链的那一份；同一份 40 页 fixture，
开场量过 `sha1 d354e705…` / 12539 字节，跑完再量还是这两个数 —— 这一遍没有走保存）**：

- 旋转对话框**刚打开**的那一帧：`还没有说出要转哪几页。`，`旋转这些页` 是**灰的**（改之前这里是
  "方向已选中 + 框空着 + 按钮亮着"，一下回车转 40 页）。在框里打 `2-4` → `向右转 90°：3 页。`、
  按钮亮 —— 这一条是必须的，否则就是拿一个走不到的分支换一条走不到的分支。
- 删除对话框**刚打开**：`还没有说出要删哪几页。`，`删除这些页` 灰的。
- 转完 3 页之后窗口标题 `doc.pdf * — XYZPDF`；`Ctrl+F4` → 标题 `还有没保存的修改`，正文
  `「doc.pdf」还带着没有写进任何文件的修改：向右转 90°（3 页）。` —— 句子干净了。按 否（默认）
  之后裁开标签条看：`doc.pdf *` 还在，星号只是从这句话里出去，没从界面上出去。
- **没有重打包。** 本机装着的那一份还是 §13 的 16.9 MB，**不含**这两处改动。
- **顺带一句关于证据的**：`gui-verify/launch.ps1` 开头会清空 `scratch/`，所以 §13 里核过的那份
  `doc 已修改.pdf`（39 页 / 12382 字节 / 三个 `/Rotate 90`）已经不在了 —— 那些数当时写在上面那一节
  里，别再去目录里找它。

**收回 §13 的一条坑**：那里写"先真鼠标点进那个框、再 SendKeys 才有字进去"—— 说重了。这一遍
`-Keys '%dr2-4{ENTER}'` 一句 SendKeys 从打开菜单一路打到按下 `旋转这些页`，**一次鼠标没点**，
字进了框：两个对话框在构造末尾就 `customEdit->setFocus()`。打不进 Qt 的 `QLineEdit` 的是
`PostMessage` 的 `WM_KEYDOWN`（那一条还在），不是 SendKeys；而焦点本来就在那儿的时候，连"先点一下"
都不必。

**没有测试覆盖这两处** —— 它们在 `QDialog` 和 `MainWindow` 里，这个仓库不带 GUI 测试；能自动化的
只有解析器那一边，而它没改。gate：`python tools/build_all_tests.py` → `28 test targets found`、
`26 passed, 0 failed`、`everything built and everything passed`（应用目标也重新编译链接过：
`mainwindow.cpp.obj` 和两个对话框的 `.obj` 时间戳晚于本次改动，`xyzpdf.exe` 跟着在 22:01:28 重链）。

**仍然欠的**：M5 的**重排**（→ §15 已落地）；就地保存；加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；
删页小结里那个页数跟着谁走（§13 末条，这一遍没删过页）；还有上面看到的"改缩放模式后状态条页号
与屏幕不一致"（§13 顺手记的，不属于这一片，没动）。

## 15. 重排落地，顺带从目录里抓出六条把 `&` 转义了两次的英文（2026-09-24）

删页和旋转改的是"页的内容 / 页还在不在"，重排是这一片第一个**改顺序**的。它没有新依赖，但有
三处一旦想错就会**静默写出跟屏幕不一样的文件**，所以值得单独一节。

1. **一句"移到第几页之后"只能有一个算法。** `DocumentEditor::planMove()` 是全仓库唯一算目标
   顺序的函数：对话框的小结、`rebuildLive()` 里撤销栈的重算、`saveAs()` 里的重放三处都调它。
   这不是复用方便的问题 —— 顺序有两个算法，界面答应的和文件写出的就可以不同，而测试可以全绿。
2. **操作里存的是原始页号。** `Operation::pages` 和 `anchor` 都是**原来那份文件**的 0-based 页号，
   不是当前页号。撤销一条删页会让原始页号重新出现，当前页号不会 —— 存后者的那条链子在
   "删一页 → 移几页 → 撤销删除"这里必然错位。
3. **重放必须先摘再插。** qpdf 的 `addPage()` 看到一张已经在树里的页会**浅拷贝**它，于是"直接
   插到目标位置"会让同一页出现两次。`apply()` 里先把要动的页逐个 `removePage()` 摘干净，再按
   `plan.order` 从左到右 `addPage(first)` / `addPageAt(before=false, 前一页)`。

被点名的页**作为一个整块走，块内相对顺序不变**：框住 3-5 说"移到第 1 页之后"，看到的就该是 3、4、5
连着落在第 1 页后面，而不是解析器替它们排出的别的次序。锚点那条页如果被前面的修改删掉了，就落进
那个空档（排在比它大、且还在的第一页前面；后面没有页了就放到最后），并且在小结里明说。三种情况直接
拒：锚点就在要移动的页里（`第 %1 页就在要移动的页里，这样移等于没移。`）、顺序本来就这样（`那些页已经
在最后了。` 那一组三句）、锚点不在这份文件里（`要说移到第几页之后，而那个页码不在这份文档里。`）。

**测试**（`documenteditor_smoke`）：`planMove` 先当纯函数过一遍，再走真文件 —— 删掉第 10 页、
`movePages({2,3}, AfterPage, 0)`、`movePages({8}, Front)`，**每次都用 Qt PDF 把写出来的文件重新
打开、逐页读出 "XYZPDF page N" 来核顺序**（核的是文件，不是那个列表），撤销/重做各重放一遍，
最后 `hashOfFile(source)` 必须还是原来那个数。两条计时 note 现在是
`40 pages, a 12 KB document: open + delete + write took 4 ms` 和 `600 pages, 186 KB: … 64 ms` ——
和 §12 那两次的 3 ms / 62 ms 同一量级，而那两次列表里也只有 1 个操作，所以"这一片不起线程"没变。

**界面**：`文档 → 移动页面(&M)…`。M 是"移动"的字母，只要在这个菜单里唯一就行 —— 菜单栏本来就
在复用别的菜单放自己条目上的字母（`文件` 的 `打开(&O)` 和 `文档` 的 `重做(&O)`）。对话框接在
删页/旋转那一套上：上面还是"当前页 / 指定页码 + 页码框"，下面多一组 `移到`：`最前面 / 最后面 /
指定页之后 + 页码框`，小结实时跟着变。

**点到的**（开发树 `build-local-release/xyzpdf.exe`，09:34 新链的那一份；同一份 40 页 fixture，
开场量过 `sha1 d354e705…` / 12539 字节，跑完再量还是这两个数 —— 用户的文件一个字没动）：

- 菜单：`删除页面(E)…` / `旋转页面(R)…` / **`移动页面(M)…`**，没有修改时 `撤销(U)`、`重做(O)`、
  `保存修改为(S)…` 三项灰着。
- 对话框**刚打开**那一帧：`当前页（第 1 页）` + `最后面` 选中，小结
  `把 1 页移到最后面：移完它们连着排在第 40-40 页。`，按钮亮着。
- 打 `3-5` → 页码框一有字就自己跳到 `指定页码`：`把 3 页移到最后面：移完它们连着排在第 38-40 页。`
- 把框清空 → `还没有说出要移动哪几页。`，`移动这些页` **灰的** —— §14 那条规矩在新对话框里同样
  成立，没去靠解析器的"空即全部"。
- 打 `3-54`（钳到 40）、目标仍是 `最后面` → `这些页已经在那个位置上了，这样移不会把它们挪动。`，灰的。
- 打 `2-4`、选 `指定页之后` 填 `6` → `把 3 页移到第 6 页之后：移完它们连着排在第 4-6 页。`，亮。
  按回车：标题 `doc.pdf * — XYZPDF`，菜单里 `撤销 移到第 6 页之后（3 页）(U)`。
- `Ctrl+S` 写盘：状态条 `已写成一个新文件（40 页）：移到第 6 页之后（3 页）`；标签条变成
  `doc.pdf *` + `moved.pdf`（新开的这一份**不带星号**）；`moved.pdf` 的第 3 页渲染出来是
  **`XYZPDF page 6`** —— 屏幕上下过的那句承诺和文件里的顺序是同一件事。12539 → 12655 字节。
- 关窗口 → `还有没保存的修改`：`这个窗口里有 1 个标签带着没有写进任何文件的修改。关掉窗口就会
  把这些修改丢掉。要关掉吗？`

**撞出来的两条**：

1. **`第 [框] 页之后` 这种句子翻不了。** 第一版把锚点输入框嵌在一句话中间（三个控件拼一个句子），
   `lupdate` 抽出来的源串就是碎的，英文里语序也对不上。改成 `指定页之后` 单选钮 + 旁边一个 placeholder
   为 `页码` 的小框。**这种"拆开来填空的句子"以后在对话框里别再写。**
2. **目录里有六条手写英文把 `&` 转义了两次**，英文界面读作 `Rotate Pages(&amp;R)`。
   `git show HEAD:` 数出来正好六条，全是 M5 这一片手写的：`Rotate Pages(&amp;amp;R)…`、
   `Save Changes As(&amp;amp;S)…`、`Undo(&amp;amp;U)`、`Undo %1(&amp;amp;U)`、`Redo(&amp;amp;O)`、
   `Redo %1(&amp;amp;O)`。现有三条检查（不是中文 / 不等于源串 / 占位符还在）**一条都抓不到它**，
   所以 `i18n_smoke` 的探针循环里加了一条 `leaks a markup entity`：任何一条译文里出现 `&amp;` 就红。
   加完先看着它红（正向对照，确认这条通道真的能抓到东西），再改那六条，然后用 `lconvert` 把**编译好的
   `.qm`** 倒回 ts 复核一遍 —— 现在 `grep -c 'amp;amp;'` = 0。**本机装着的那一份（§13 的 16.9 MB）
   还带着这六个字面量。**

**没有验到的**：

- 移动之后**关标签**（`Ctrl+F4`）的标签级确认句 —— 这一遍只看到窗口级那一句。而且这一轮 `^(F4)`
  发出去之后两个标签都还在、也没弹任何东西，原因没查（§14 里同样的写法是有效的）。
- 锚点掉空档那两句追加话（`第 %1 页在还没保存的修改里已经删掉了，所以是排在第 %2 页前面。` 和
  `…而它后面没有页了，所以放到最后。`）**没在屏幕上见过**，只有测试里的断言。
- 鼠标点单选钮：`click.ps1` 两次坐标都没点中那个 16px 的钮（(1207,760)、(1075,760)），这一片的界面
  操作全走键盘（`{TAB}{DOWN}{DOWN}{TAB}` 稳定落到锚点框）。坐标映射在这个套子里还是不可靠。
- **没有重打包**；`gui-verify/*.ps1` 也还没搬进 `tools/`。

gate：`python tools/build_all_tests.py` → `28 test targets found`、`26 passed, 0 failed`、
`everything built and everything passed`。界面上点的那一份 `xyzpdf.exe` 时间戳 09:34:04，晚于这一片
最后一次源码改动；它之后只动过注释。

**仍然欠的**：就地保存；加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；删页小结里那个
页数跟着谁走（§13 末条）；"改缩放模式后状态条页号与屏幕不一致"（§13 顺手记的）；上面那四条没验到的；
外加新的一条 —— **M5 这一片的英文菜单在本机安装包上是坏的**，要等下一次重打包才一起出去。
（那一条由 §16 结掉。）

## 16. §15 欠的那一次重打包，和"装上的就是测过的那一份"怎么证（2026-09-24）

链条还是那三步：`Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage` →
`build-package.ps1 -Sign -Cert 221C6B38…0A83 -Install` → `verify-package.ps1 -Package …`。
包 **16.9 MB**（和 §13 一样大），`signtool` 一次过，`verify-package.ps1` 的判词是
`XYZPDF_0.1.0.0_x64.msix unpacks to 39.3 MB in 62 files`、`starts from the unpacked package with
no Qt on PATH`、`the package is complete and runs standalone`。

**这一节真正想记的是中间那一步怎么证。** "重打包并装上了"这句话本身不含任何信息 —— 它可能是
一份旧二进制打了包。能证的是**三个地方的 SHA-256 相同**：

```
build-local-release/xyzpdf.exe            d5554654e13fdb67…4d4c46d4cfdfbf55d071d7ea62f593d
包里的 bin/xyzpdf.exe（解到 %TEMP% 那一份）  同上
C:\Program Files\WindowsApps\...\bin\xyzpdf.exe  同上
```

第三条是装在系统里的那一份，`certutil` 直接读得动（`WindowsApps` 的 ACL 挡的是写，不是读）。
哈希相等之后，§15 里"26 个测试跑的是这份字节、真窗口点的是这份字节"才顺带变成"装着的也是这份字节"，
否则装机这一句只能靠"我重新编译过了"这种话撑着。

**装机之后在屏幕上看的两帧**（部署的那一份，用 `explorer.exe shell:AppsFolder\allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF`
起的 —— 清单里没有 `AppExecutionAlias`，所以带不了文件参数，这一遍没开文档，三行编辑菜单是灰的）：

- 中文：`删除页面(E)…` / `旋转页面(R)…` / **`移动页面(M)…`**，`撤销(U)`、`重做(O)`、`保存修改为(S)…` 灰着。
- 英文：`Delete Pages(E)…` / `Rotate Pages(R)…` / `Move Pages(M)…` / `Undo(U) Ctrl+Z` /
  `Redo(O) Ctrl+Y` / `Save Changes As(S)… Ctrl+S` —— **一个 `&amp;` 都没有**。§15 那六个字面量
  到此才算真的从这台机器上出去。

**切英文走的不是菜单。** `Language` 那一项没有助记符，`{RIGHT}` 三次和 `{ALT}` + `{RIGHT}` 六次
都没停在它上面（第一次还把菜单整个关掉了），两轮没点进去就不点了 —— 改成直接写程序开机读的那个值：
`HKCU\Software\xyzpdf\xyzpdf\appearance\language = en`，重启就是英文。**量的还是同一个界面，
只是绕开了那个不好点的入口。** 看完把 `appearance` 这个子键删掉（这台机器本来没有它，
`recent\documents` 还是 8 条、没有 scratch 路径），`verify-package.ps1` 留在 `%TEMP%` 的 39.3 MB
解包目录也删了。

**顺带一条工具的坑**：`gui-verify/*.ps1` 里写了非 ASCII 字符（一个省略号）就让 Windows PowerShell 5.1
报 `字符串缺少终止符` —— 它按 ANSI 读没有 BOM 的 `.ps1`，那个字符的字节把引号吃了。这些套子里的脚本
**只写 ASCII**。

**这一遍没有的**：没在装着的这一份上打开过 PDF，所以重排、删页、旋转在**安装包**上仍然只是"菜单里
有这三行"，不是"点通过"—— 点通过的是开发树那一份，两份的 exe 逐字节相同，这是这一节能这么说的前提。
预览窗格和缩略图这次没再看。

**仍然欠的**：就地保存；加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；删页小结里那个
页数跟着谁走（§13 末条）；状态条页号与屏幕不一致（§13）；§15 那四条没验到的（其中"移动之后关标签的
标签级确认句"和"锚点掉空档那两句"这一遍仍然没看到）；`gui-verify/*.ps1` 还没搬进 `tools/`。

## 17. 合并：一页的身份从"哪个文件的哪一页"改成"哪一次合并的哪一页"（2026-09-24）

三个决定是他定的：**并进当前文档、进撤销栈**（不是"一次性的合并工具"）；**每份输入各有自己的页范围，
列表还能调顺序**；**加密的输入直接拒，并且要说清是哪一份**（这一片不做口令框）。第一个决定动了
`DocumentEditor` 里"一页是什么"的定义，这一节记的就是那一处，和它为什么不是多余的一层。

1. **同一个文件的同一页可以在这份文档里出现两次，而它们必须是两个身份。** `PageRef{source, page}`：
   `source == 0` 是正在读的这份，正数是**一次合并自己的号**（`nextMergeId` 从 1 起，`open()` 里归零 ——
   别的文档留下的号会变成"两页都声称是自己"的一个身份）。存 `imports` 的下标不行：同页并两次，两条
   key 相同，`indexOf()` 只会答第一个，于是后面那次重排落在错的那一块上，而且没人说不。
2. **这一条是量出来的，不是推出来的。** 把 `mergeId` 临时换成 `index + 1` 做对照，`lean` 那一条正好
   变红：`got 1,6,2,3,…,40,6` 而 wanted `1,6,3,…,40,6,2` —— 第 2 页被塞回了两块 6 中间，读者要的是它
   去最后。改回来只有那一条红。没有这次对照，"并进来两次的两页是两个身份"就只是注释里的一句话。
3. **读者敲的页号永远指 `source == 0`。** 合并进来的页在屏幕上没有号码，所以 删除页面 和 旋转页面
   说不到它们，唯一的路是撤销那次合并 —— 这句现在写在合并对话框的小结里（合并**之前**就说，而不是
   等人在 删除页面 里试出来）。副作用：`现在这份有 %1 页` 那个数跟着 `pageCount()` 走、含那些没号码
   的页，所以 删除页面 现在多一句 `这 %1 页里有 %2 页是合并进来的，屏幕上没有它们的页码，这里删不到
   它们。` —— §16 末条"删页小结里那个页数跟着谁走"到这儿有了答案。
4. **列表顺序 = 文件里的顺序，这件事只由 `mergesToRecord()` 一个函数负责。** 一次合并是"排在目标
   旁边"，不是"排在前一块后面"，所以 最前面 / 指定页之后 两种目标要把行**倒过来记**（三份文件否则会
   写成 3、2、1），最后面 不倒。小结和真操作都从这一个函数读 —— 跟 §15 第 1 条同一个理由：一个 promise
   有两个算法，界面答应的和文件写出的就可以不同，而测试可以全绿。

**读到的数**（同一台机、同一份 fixture，`documenteditor_smoke` 打的 note）：

```
40 页 / 12 KB：                删 3 页  3 ms    并进 3 页  5 ms
600 页 / 186 KB + 1 KB 供体：  删 3 页 64 ms    并进 3 页 81 ms
```

PLAN.md 里"重放 + 整份写是毫秒到几十毫秒"那一句先前只有删页那一列。合并的保存要多开一份 QPDF、把
对象搬进另一份的树里（还要 `fixCopiedAnnotations`），所以补了右边一列：**同一量级，没有翻倍**，
这两次保存也都还在"不起工作线程"的那一档里。

**加密那一支**：`inspectImport()` 在挑文件的那一刻就问，两种话分开 —— 需要口令的（`qpdf_e_password`，
注意不是 `getErrCode()`，也不是 `QPDFExc::pdf_password_error`）说"…想合并它，先把它本身打开一次，或者
去掉它的口令"并且点名是哪一份，坏了的说"解析器说：%2"。**量过的**：带用户口令的那份被拒、话里点着
文件名；只有 owner 口令 / 标了 not-to-modify 的那份能并进来，两页真的写出去了。

**三条只在保存时才说得出的拒**（都要读了文件才知道）：供体在记下修改之后变了（`当时是 %2 页，现在只有
%3 页`）、另存为指向并进来的那一份、重放出来的页序与内存里的不是一份（`请把这种情况报上来`）。三条都有
测试，并且都断言了**没留下半个文件**（`!QFile::exists(out)`）。

**测试（模型）**：`documenteditor_smoke` 1562 行、244 处 `check(`，这一次 +581 / −42。合并那一段是拿 Qt PDF
把写出来的文件重新打开、逐页读 `"XYZPDF page N"` 核顺序（核的是文件不是列表）：落到末尾 / 落到最前 /
并进来之后再删"第 1 页"删的是屏幕上那页 / 同一页并两次各自独立 / 把正在读的这份并到自己身上 /
撤销重做各重放一遍 / 供体与源文件的哈希一个都没动。

**测试（对话框）**：`documenttab_smoke` 里加了 156 行 —— 这是这个套子里第一个被真的构造出来的对话框。
理由就是上面第 4 条：那个倒序住在 `MergePagesDialog` 里，而 `planMerge` 一次只看一行，所以模型侧的
测试**结构上**够不到它。为了让它能被跑，`添加文件…` 拆成了两半：原生选择框那一半留在 `addFiles()`，
"逐个验File + 摆行 + 把拒绝的话交回调用者"那一半是公开的 `addPaths()`。于是离屏（`QT_QPA_PLATFORM=offscreen`）
可以填三行、点那三个目标、改一行的范围列，再读 `mergesToRecord()` 和小结标签。

**倒序那条又做了一次对照**：把 `mergesToRecord()` 里的 `std::reverse` 拿掉重跑，红的正好是顺序那两条 ——
`pointed at 最前面 the rows are recorded so the file gets them in list order: got many-pages.pdf,dense.pdf,mixed.pdf`
和后面那段逐页走（44 页 44 条断言，第 3 页起全红）。改回来只有该红的红。**没有这次对照，
"列表顺序 = 文件顺序"就还只是注释里的一句话**（§15 的同一个手法）。

顺带撞出来的一条是关于 fixture 的：第一次挑的三份供体里 `lines.pdf`、`runs.pdf` 都**只有 1 页**，
所以行里写 "2" 解析出来是空 —— 红的是"这一行的范围列按那个文件自己的页数读"。换成
`many-pages.pdf`(40) / `dense.pdf`(2) / `mixed.pdf`(2) 之后全绿。**这不是代码错，是测试假设错**，
记下来是因为下次还会想当然地以为"fixture 总不止一页"。

**没在真窗口里点过。** 上面那些是离屏跑的：有一个窗口对象，但没有桌面、没有真字体、没有原生选择框。
所以仍然没看过的有 —— 表格三列的宽度够不够、`双击"要合入的页码"` 那句提示在屏幕上读不读得通、
`添加文件…` 真的开出来的那个框、`指定页之后` 旁边那个 56px 的框、以及英文界面下这五句小结会不会
把窗口撑变形。**特别地：`mergesToRecord()` 的倒序有测试了，但那条测试是从 `addPaths()` 喂进去的，
真点一次选择框的路径没走过。** 还有一条模型侧也没验的：测试用的供体是 `writeDonor()` 造出来的三页文件，
**没有注释也没有表单**，所以 `fixCopiedAnnotations` 那条路真正走过没有，不知道。
（**→ §18 点过了**：这一段留着，因为它记的是"当时没看过"这件事本身。）

**目录**：`update_translations` 一次，源文 376 → 426 条（50 条 new）。41 条手写英文，9 条是 lupdate 自己的
same-text heuristic 从别处抄来的旧英文（`取消`、`最前面`、那句共同的尾巴…）—— 先核过抄来的措辞与本目录
其余部分一致，才把 `type="unfinished"` 摘掉。现在 **0 条 unfinished**、`&amp;amp;` **0 处**（§15 那一处的
复发检查），i18n 扫描的探针 48 → 54 条。`文档` 菜单七行：四行记修改、两行在列表里走、一行写文件；
新那行是 `合并文件进来(F)…`（E/R/M/U/O/S 都被占了，"合并"没有拉丁字母可取，字母给了"文件"）。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。**包里还是 §16 那份 exe（三个哈希
相同的那一份），没有合并。**

**仍然欠的**：合并的界面在真窗口里点一遍（含 `添加文件…` 那个原生框、表格三列的宽度、英文界面会不会把
窗口撑变形）；带注释/表单的供体合并（`fixCopiedAnnotations` 那条路还没真走过）；重打包并重装；就地保存；
加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；状态条页号与屏幕不一致（§13）；§15 那四条
没验到的（其中"移动之后关标签的标签级确认句"和"锚点掉空档那两句"这一遍仍然没看到）；
`gui-verify/*.ps1` 还没搬进 `tools/`。

## 18. 合并界面在真窗口里点了一遍：撞出三条，第三条猜错了两次（2026-09-24）

dev 树那份 exe，英文一遍、中文一遍，`添加文件…` 走的是真原生选择框（`#32770`），供体是
`gui-verify/scratch/` 下 `d1/d2/d3.pdf`（各 2 页），底稿是 40 页那份。这一节记看到的原话、
撞出来的三条，和第三条为什么值得单独写。

**看到的**（都是屏幕上的字，不是测试里的字符串）：

- 列表摆出来：`文件 / 一共 / 要合入的页码` 三列，`一共` 那一列是**那个文件自己的**页数
  （三行都是 `2 页` / `2 pages`），范围列预填整份 `1-2`。
- 清空其中一行的范围 → 按钮灰掉，小结换成 `d3.pdf 还没有说出要合入哪几页。` —— 点名的那一份，
  不是"那就全要"。§17 第 3 条那条"空 = 还没说"的规矩在这儿活着。
- 范围写成 `1-9`（对一份 2 页的文件）→ 收窄成它自己的两页，不报错也不并出空页。
- 单选钮没点、直接在 `指定页之后` 旁边那个框里敲 `5` → 钮自己跳过去（`textChanged` 那一段），
  小结变成 `3 个文件一共取 6 页，合到第 5 页之后，按列表的顺序连着排在第 6-11 页。`
  **6-11 就是"列表顺序 = 文件顺序"在屏幕上的一次读数**：三份各两页、连着排、起点是第 5 页之后。
- 框空着 + 选了 `指定页之后` → `要说合并到第几页之后。` + 按钮灰。
- 已经并过一次（3×2 → 46 页）之后再开这个框，小结多出一段 `这份文档里已经有 6 页是合并进来的了。`
  / `6 pages of this document have already been merged in.` —— 合并**之前**就说，不用等人在
  删除页面 里试出来。
- 英文整屏：`Merge Files In` / `Files to merge in` / `Add Files…` `Remove` `Move Up` `Move Down` /
  `Merge them` / `to the front` `to the end` `behind page` / `Merge these files`，新加的英文里
  `&amp;amp;` **0 处**（§15 那一条的复发检查）。

**撞出来的三条**，前两条各改三行：

1. `指定页之后` 旁边那个 56px 的框把英文占位文 `Page number` 剪成 `Pag…`（中文 `页码` 正好放得下）。
   这条是 §15 给 移动页面 记的那一条**原样复发** —— 同一个写死的 56 抄在了第二个文件里。两处现在都
   是 `qMax(56, fontMetrics().horizontalAdvance(占位文) + 24)`。
2. 小结第一句 `3 个文件一共 6 页` 读起来像"这份文档一共 6 页"。改成 `一共取 6 页`（目录、
   `fill_en.py` 的映射键、i18n 探针三处跟着改；英文那句 `Merges %2 pages from %1 files %3: …` 不动）。
3. 三份文件在列表里时，**表格被挤到看不见行**（英文那一版一行都不剩）。给表格加了"至少四行"的下限
   （`setMinimumHeight(4 * 行高 + 表头 + 8)`）之后，第三行改成了**压在** `添加文件…/移除/上移/下移`
   那一排上面 —— 窗口没跟着长。

**第三条为什么单独写**：前两次猜都猜错了，而错的不是猜，是没测。

- 第一次猜"窗口没长是因为没人叫它长"，加了一句"矮了就 `resize` 到布局的最小高度"。屏幕上**一点变化
  没有**。这时该停手去量，我先把话写进了注释里。
- 于是往 exe 里塞了一段临时探针，把 `winH / minH / tableH / labelH / label 的 sizeHint 与
  minimumSizeHint` 逐次写进一个文件（stderr 在这个 exe 里是死的，文件读得动）。**离屏那 24 条**先出来：
  `tableMin=144`，`minH` 老老实实跟着 `labelH` 走（434 / 462 / 490）—— 机制在测试里是通的，
  只是没人看图，那 24 条里 `labelH=120` 和 `labelMinHintH=75` 已经并排印了 45 的差。
- **真窗口那三条**：`winH=544 needed=544`，窗口一直是 583 物理。Qt 的模型和窗口其实是对得上的
  （标题栏 + 边框 39），所以那句 `resize` 没生效是假象 —— 它要长的数**本来就是 544**。
- 差的 45 在 QLayout 那一侧：算总最小高度时，对一个会自动换行的 `QLabel` 它用的是
  `minimumSizeHint`（"放得下的那三行"），不是这句话在这个宽度上真正要的五行。所以"长到布局最小高度"
  这句话没错，是**那个数少算了 45**。补上差额：
  `totalMinimumSize + summaryLabel->sizeHint() − summaryLabel->minimumSizeHint()`。
- **对照**（一次可逆的、不改代码的实验）：用 Win32 `MoveWindow` 把同一个窗口直接撑到 700 高，重叠
  立刻消失、四行全见 —— 表格和按钮自己的排布是对的，唯一缺的就是高度。这一条把"是布局写坏了"
  和"是窗口不够高"分开，前面三次猜都在前者上打转。
- 补完之后的实测（物理窗口高）：英文 空 583 → 1 份 643 → 3 份 643 → 四段小结（已经并过一次）673；
  中文 空 568 → 1 份 583 → 3 份 583（中文短，一次就够）。**窗口跟着话长，不是跟着猜长。**

**探针已经拆了**（含那两个临时 include），最终那份重新跑过 gate，屏幕上那两张图（英文 3 份、中文
3 份 + 锚点 5）是拆完之后拍的。

**这一遍没覆盖的**：确定 → 保存 → 把写出来的文件逐页读回那一段，看到的是**改之前**那一份（这一遍
只重新走到"记下修改"：标题出现 `*`、再开这个框就说"已经有 6 页是合并进来的了"）；带注释/表单的供体
（`fixCopiedAnnotations` 那条路仍然只在测试里存在）；"锚点掉进还没保存的删除留下的空档"那一句在合并
界面里没在屏幕上见过（模型侧有测试）。**包里还是 §16 那份 exe，没有合并。**（**→ §19 换过来了**）

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**（这一节里跑了四次：加表格下限、
补 45 的差额、拆探针，各一次）。

**仍然欠的**：带注释/表单的供体合并；重打包并重装（§16 那三个哈希相同的证明要重做一遍）；就地保存；
加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；状态条页号与屏幕不一致（§13）；§15 那四条
没验到的；`gui-verify/*.ps1` 还没搬进 `tools/`（这一节又添了 `pick.ps1` / `dbl.ps1` / `dlg.ps1` /
`grow.ps1` / `en-merge.ps1` 五个）。

## 19. 带着合并重打了一次，这一回在**装着的**那一份上把合并走完了（2026-09-24）

链条还是 §16 那三步，一步没改：

```
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage
packaging/build-package.ps1 -Sign -Cert 221C6B38…0A83 -Install
packaging/verify-package.ps1 -Package build-local-release/package/XYZPDF_0.1.0.0_x64.msix
```

包 **16.9 MB**（和 §13、§16 一样大 —— 合并没有往里加东西，qpdf 那三个 DLL §10 就进来了），
`signtool` 一次过，`verify-package.ps1` 的判词还是
`unpacks to 39.3 MB in 62 files` / `starts from the unpacked package with no Qt on PATH` /
`the package is complete and runs standalone`。**文件数一个没变**，这一句是"合并没往包里添东西"的读数。

**三个地方的 SHA-256 相同**（§16 那条规矩，这一遍重做）：

```
build-local-release/xyzpdf.exe                      6acbe2edce7d07c9c56ba123…38ab9e3658
包里的 bin/xyzpdf.exe（解到 %TEMP% 那一份）           同上
C:\Program Files\WindowsApps\...\bin\xyzpdf.exe      同上
```

也就是：26 个测试跑的、§18 真窗口点的、现在装在系统里的，是同一份字节。

**这一遍比 §16 多走了一段，所以 §16 末条那句"没在装着的这一份上打开过 PDF"到此作废。**
部署的那一份没有 `AppExecutionAlias`，带不了文件参数，所以这一回是 `Ctrl+O` 起原生框、在文件名框里
敲绝对路径开的 40 页那份。看到的：

- 空框 576x568 → 1 份 576x583 → 3 份 576x583 —— 与 §18 开发树那一份**同一套数**，四行表格全见、
  按钮那排不压在上面。§18 那条布局修复到此才算在包上验过。
- `文档` 菜单四行：`删除页面(E)…` / `旋转页面(R)…` / `移动页面(M)…` / **`合并文件进来(F)…`**，
  下面 `撤销 合并 2 页 (d3.pdf) (U)` 亮着、`重做 (O)` 灰着、`保存修改为(S)…` 亮着 —— 一个 `&` 都没有。
  撤销那行点名的是 `d3.pdf`：三份按列表顺序记，最后记的那一块就是最后一行。
- 确定 → `保存修改为` 写出 `merged-packaged.pdf`（15,193 字节），标签标题换成那个文件、星号没了，
  状态条 `1 / 46`（40 + 3×2）。**这一段（确定→保存→写出→读回页数）在 §18 里是"看到的是改前那一份"，
  现在看到的是装着的这一份。**

**没在装着的这一份上做的**：英文那一遍（英文只在开发树上看过了，两份 exe 逐字节相同是这儿能这么说的前提）；
写出来的文件**逐页读回核顺序**（那是 `documenteditor_smoke` 拿 Qt PDF 干的活，屏幕上只数到 46 页）；
预览窗格与缩略图这次没看。

**收尾**：`verify-package.ps1` 留在 `%TEMP%` 的 39.3 MB 解包目录删了；`appearance` 键没有；
`recent\documents` 里 scratch 路径 **0 条**；`files\lastOpenDirectory` 没有。

**仍然欠的**：带注释/表单的供体合并（`fixCopiedAnnotations` 那条路还没真走过）；就地保存；
加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；状态条页号与屏幕不一致（§13）；§15 那四条
没验到的；`gui-verify/*.ps1` 还没搬进 `tools/`。

## 20. 拆分：第一个不改任何一页的操作，和它照出来的那一类标点（2026-09-24）

三个决定还是他定的：**进撤销栈、按保存才写**（不是"点一下立刻给你 N 个文件"的那种工具）；
**每 N 页一份**（拆分只有这一个轴）；**拆的是"保存会写出的那一份"，合并挂着时拒绝**。
这一节记的是：为什么这一片的撤销不需要新机制、接线时撞出来的那一处上报、分隔符那一条为什么是
**一类**而不是一个笔误，和屏幕上看到的原话。

1. **拆折不改任何一页，所以 `rebuildLive()` 里没有它的分支。** 那四处（删页、旋转、重排、合并）
   都要回答"现在这份有哪些页"，拆分一个字都不动它 —— 它改的是**保存写几个文件、每个叫什么名字**。
   于是撤销天然站得住：磁盘上从来没有过东西，撤销只要把那一条操作从列表里拿掉，没有"反写"这一步。
   星号那条链跟着一起对上：`title()` 的 ` *` ← `hasUnsavedEdits()` ← `isDirty()` ← `!pending.isEmpty()`，
   所以**撤销掉唯一挂着的那条拆分，星号必须自己消失**（这一条在屏幕上看见了，见下）。
2. **一份答案只由两个函数给。** `pieceRanges(total, size)` 决定哪几页进哪一份，`piecePath(dest,
   index, total, range)` 决定那一份叫什么：对话框的小结、保存的循环、测试读回来的名字，三处读的都是
   这两个 static。跟 §15 的 `planMove()`、§17 的 `mergesToRecord()` 同一个理由 —— 一个 promise 有
   两个算法，界面答应的和文件写出的就可以不同，而测试可以全绿。补零的宽度跟着**最后一份**走
   （`qMax(2, ...)`）是因为文件夹按名字排序：`1、10、11、2` 是一副没人能核对的顺序。
   单页那一段写 `21` 不写 `21-21`，`my.report.v2.pdf` 取 `completeBaseName()`，无扩展名的目标
   给出的份也没有扩展名（那是读者自己选的名字，不是这里该改的地方）。
3. **`每 N 页一份` 的框上限是总页数减一。** `N == 总页数` 只会写出一份，`splitInto()` 拒它，
   所以那个框压根给不出"不拆"这个选项（40 页那份最大 39）。默认值是**一半**：那是最小的、
   也肯定算"拆"的那个数，而 600 页默认给 1 是一份没人要的礼物。已经挂着一条拆分时，框开在那个已有的数上，
   所以再点一次 确定 读起来是"改那个数"而不是第二件事（模型侧也是这样：最后一次拆分作数，
   撤销一次回到前一个数）。
4. **和合并互斥，两边都问。** 拆分要按屏幕上看得见的页号数份，而合并进来的页没有屏幕页号
   （§17 第 3 条），所以两样凑在一起没有"读者能核对的一组文件"。`splitInto()` 和 `mergeFile()`
   各自拒一次，`saveAs()` 在真的动手之前再兜一道（到了那儿就说明前面有一边停止检查了）。

**接线时撞出来的那一处不是界面，是上报。** `documentSavedAs(path, changes, pages)` 原来把**保存框里
那个名字**直接回给窗口去 `openDocument()`。拆分挂着时**那个名字不是任何存在的文件**（落盘的是三个
份名），于是这一片第一次逼出了"写了哪几个文件"这个信息：`saveAs(..., QStringList *written)` 把实际
落地的名字带回来，信号多一个 `fileCount`，状态条因此有**两句**措辞 ——
`已写成一个新文件（%1 页）：%2` 和 `已写成 %1 个新文件（整份 %2 页）：%3`。
两句分开不是为了漂亮：拆分之"整份 40 页"是整份的页数，而新开的标签是那份 15 页的文件，
把"40 页"印在它旁边是一个**没有指称的数**。这是"一个信号里塞一个名字"这种省事写法在第一个
多输出操作上崩掉 —— §11 到 §19 那四样都是单文件，没人碰过这一处。

**重放的代价是量出来的**（一次拆分保存 = 每份把整份重放一遍）：

```
40 页 / 12 KB，拆 3 份（15/15/10）：  3 次重放 + 3 次写出 =  10 ms
40 页 / 12 KB，每 1 页一份 = 40 份：  40 次重放 + 40 次写出 = 123 ms
600 页 / 186 KB，拆 4 份：            4 次重放 + 4 次写出 = 189 ms
```

三条读数除掉单位对得上（120、1 600、2 400 页·份 → 10 / 123 / 189 ms，也就是**每页·份约 0.08 ms**，
三条各自算出来是 0.083 / 0.077 / 0.079），所以对话框那句"会等一会儿"的阈值划在
**份数 × 页数 ≥ 12 000**：上面三条（120、1 600、2 400）都不说话，600 页每 1 页一份（360 000）才说
—— 按那个单位代价是大约一秒的位置。
（`splitpagesdialog.cpp` 里那句注释写的是 129 ms / 183 ms，是**写注释那一次**的读数；本轮同一台机
跑出来 123 / 189。两个数都留着，因为阈值是从这两次之间划的，不是推的。）

**测试**：`documenteditor_smoke` 1562 → 1994 行、`check(` 244 → **332** 处（+88）。核的是
`pieceRanges()` 的边界（不拆 = 一份整份、最后一段是剩下的、整除不多一段、10 页每 1 页 = 10 份、
`N ≥ 总页数` 与 0 页都给一份/空）+ 40 页在 `3 / 7 / 39 / 40` 这四个尺寸上的"不重不漏、每段起点 =
上段终点 + 1"（余数能取的形状都过一遍）、
`piecePath()` 的命名（含三点文件名、无扩展名、单页段、12 份按名字排序就是按页排序）、
`splitInto()` 的三条拒、第二次拆是改主意、拆完撤销到干净、页号按列表数不按屏幕数（删 2 页再每 19
一份 → 2 份且最后一段到 37）、双向互斥 + 各自撤销之后另一边就通过、**写出三份再逐页读回内容**
（`"XYZPDF page N"`，核的是文件不是列表）、源文件哈希未动、40 份一次写出且文件夹里不剩任何
`xyzpdf-save-*`、被占用的份名**拒整份**（不是"把写得出的那几份先写了"）、撤销拆分之后普通保存又只
写一个文件且旁边没有份名文件、移动 + 拆分一起重放（第一份是 `9,1,2,…`，所以重排真的到了文件里）。
**对话框侧这一次没有加测试**（不同于 §17 为合并加的那 156 行）：拆分的唯一算法是 `DocumentEditor`
上的 static，模型侧直接够得到；合并那次必须构造对话框是因为倒序住在对话框里。这一条留着是因为
它划出了"什么时候**不**需要构造对话框"。

**真窗口看到的**（开发树那一份 exe，底稿 40 页 / 12 539 字节）：

- 打开 拆分文件：`每 [20] 页一份`（默认一半），小结
  `会写成 2 份，每份最多 20 页：1-20、21-40。` + `文件名是从保存时填的那个名字切出来的，例如第一份会叫「book 01 (1-20).pdf」。`
  + `原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，这 2 个文件才会一起出现。`；按钮 `拆成 2 份`。
- 改成 1 → `会写成 40 份，每份最多 1 页：1、2、3、4、5、6、…、40。` —— 列表只列六段加省略号加**最后一段**，
  屏幕上就是这个排法（要核对算术的人看的是末段），例子名 `book 01 (1).pdf`（单页那一段写一个数）。
  **并且这一屏没有"会等一会儿"那一句** —— 40 × 40 = 1 600 < 12 000，阈值在 40 页这份底稿上是不说话的。
- 接受 15 → 标题 `book.pdf * — XYZPDF`，菜单 `撤销 拆成每 15 页一份(U)` 亮着。
- **接受之后立刻列目录：里面只有 `book.pdf` 一个文件。** 上面那句"这一步也不写任何文件"是能在磁盘上读的。
- `保存修改为` → 三个文件按承诺的名字落下：`book 已修改 01 (1-15).pdf` 5 004 / `… 02 (16-30).pdf` 5 013 /
  `… 03 (31-40).pdf` 3 483；没有 `xyzpdf-save-*` 残留；`book.pdf` 的字节和**时间戳都没动**（还是 9 月 21 日那一份）；
  第二个标签 `book 已修改 01 (1-15).pdf` 右下角 `1 / 15`；状态条
  `已写成 3 个新文件（整份 40 页）：拆成每 15 页一份`。
- **撤销那一张单独重跑了一遍**（第一次去点标签条切回来，点歪了）：单标签窗口里 拆 15 → `Ctrl+Z` →
  标题 `book.pdf — XYZPDF`（星号没了）、`撤销(U) Ctrl+Z` **灰**、`重做 拆成每 15 页一份(Q) Ctrl+Y` **亮**、
  `保存修改为(S)…` **灰**。第 1 条那句"撤销只要拿掉一条操作"到这儿是屏幕上的一排灰和亮。
- 英文整屏：`Split Files` / `One file every [20] pages` / `Split into 2 files`；`文档` 菜单
  `Delete Pages(E)… / Rotate Pages(R)… / Move Pages(M)… / Merge Files In(F)… / Split Files(C)…`，
  下面 `Undo Split into pieces of 15 pages(U)` —— 一个 `&` 都没有。新那行的字母是 C：
  E/R/M/F 在上面四行，U/O/S 在下面三行。
- 合并挂着时开拆分（40 页底稿 + 一份 2 页供体）：那两句都在屏幕上第一次出现 ——
  `Those page numbers count the document a save would write (42 pages), not the one on the screen (40);
  the earlier changes left the view alone.` 和 `But 2 of those 42 pages came in from another file and have
  no page number on the screen, so which pages belong in which file cannot be said. Undo those merges
  before splitting.` —— 并且 **`Split into 2 files` 是灰的**（点之前就说，不是点之后）。
- 反过来（拆分挂着时点合并的 `Merge these files`）：警告
  `This document has already been told to split into pieces of 15 pages. Undo that step before merging a file in.`，
  行没记下（标签还是只有那一条拆分）。

**上面那一批图看的是分隔符改之前那一份 exe**（17:09 写出三个文件、17:15 撤销、17:18–17:25 英文与两句拒绝），
改完之后重编（17:31）在屏幕上重看的**只有范围列表那一行**（`Writes 3 files of at most 15 pages each:
1-15, 16-30, 31-40.`）。同一份改后的 exe 上还试过"两句操作连成的英文小结"，那几张（`sep-02`–`sep-04`、
`sep-06`）拍下来的是输入法吃键之后 删除页面 的页码框，不是那句小结。也就是说这里没有 §16 那条
"两份字节相同"的支撑 —— 两份 exe 不是同一份，撤销与两句拒绝在改后那一份上没有再看一遍。
改的是标点不是逻辑（两行 join + 目录），但这一条是欠的，记着。

**撞出来的三条**，第一条改了：

1. **英文界面里出现中文标点 —— 两处。** 拆分框的范围列表 `Writes 2 files of at most 20 pages each:
   1-20、21-40.`（屏幕上看英文那一遍时撞见的），以及多条挂起的修改连成一句时的 `，`
   （`Split into pieces of 15 pages，Delete 3 pages`）。**为什么 i18n 扫描看不见这一类**：那三条检查量的是
   "英文 ≠ 源文""不含 0x3400–0x9FFF 的字""不含 `&`"，而 `、`(U+3001) 和 `，`(U+FF0C) 既不在那个区间里，
   也不是任何一句话的源文 —— 分隔符是代码用 `QStringLiteral` join 出来的，从来没进过目录。
   所以这**不是一句漏翻**，是"由代码而不是由句子产生的、读者看得见的标点"这一整类，第一次露头。
   改 = 两行的 join 走 `tr("、")` / `tr("，")`，目录两条 → `, `，探针 54 → 63 里含这两条，
   外加两条**钉字节**的检查（`== ", "`，带末尾那个空格）：空格是 .ts→.qm 往返最容易掉的东西，
   掉了就读成 `1-21,22-42`，而那仍然是"不含中文"的。改完屏幕上：`Writes 3 files of at most 15 pages each:
   1-15, 16-30, 31-40.`
2. 拆分挂着时，**合并**那一侧的对话框照样亮着 `Merge these files`，小结照样写
   `Merges 2 pages from 1 files to the end…` —— 拒是点下去之后才说的话。**当时没改**，记下来的是这个不对称：
   拆分框在点之前就灰掉按钮并说清理由，合并框在点之后才说。话确实到了读者面前，所以这是措辞时机的问题。
   （→ **§22 第 1、2 条改了**：合并框现在读模型同一个 `splitPieceSize()`、灰住按钮，并把模型那句
   拒绝原文换成算术那一句。屏幕没看过，欠在 §22 第 7 条。）
3. 关于 / 第三方那个框的**第二列不在 `tr()` 后面**（`（编入 Qt6Pdf.dll）`、`各自的原许可证，见许可证全文`），
   英文界面里就是整段中文。这一条是追第 1 条时顺手看见的，属于同一个目录之外的类。**当时没改。**
   （→ **§22 第 3、4 条改了**：逐列数下来不止两格，是四格，含第四行自己的名字；跟着加了一条
   "问表本身"的循环，因为具名探针按构造就看不见没进目录的串。）

**点这一片学下来的手法**（六条都会复发）：

- 拼音输入法把 SendKeys 的**字母和数字**一起吃掉，连 `%d` 这种 Alt 和弦里的那个 `d` 也算成文本 ——
  有一遍什么都没记到，因为字全落进 删除页面 的页码框里（`sep-03-two-ops.png` 看得见那个框里的 `d`），
  星号根本没出现。方向键与和弦是安全词汇，但和弦里带的字母不安全。
- `{ENTER}` 点的是**当前聚焦**的按钮，不是对话框默认按钮：在合并框里它把原生选择框又开了一次
  （焦点在 `添加文件…`）；旋转框那个页码行编辑框抓着焦点，于是它既不吃 `{ENTER}` 也不吃量出来的点击。
- 按钮位置**要从窗口矩形算**（`right-134, bottom-37`），不能从缩放过的截图上目测：目测那次差了约 250 px；
  量了还错的那一次，是因为对话框在两行小结时 583 高、三段小结时 643 高 —— §18 那条"窗口跟着话长"
  在这里变成一个新的坑。
- 每按一次都**问进程有哪些窗口**（`TopWindows` / `DialogTitles`）再信按键：不看就拍的图，
  "框开了"和"框没开"长得一样。上一轮遗留的模态框还会让 `Activate` 拒收
  （`REFUSING: foreground window is not the target`），单个 `{ESC}` 关不掉它 —— 后面的脚本自己起一份实例。
- 原生选择框：`pick.ps1` 用 `WM_SETTEXT` 填文件名，收下用它自己的 `-ClickOpen`（对那个 `打开(&O)` 按钮
  发 `BM_CLICK`）；往名字框里 post 一个 `VK_RETURN` 在后来那一遍里把框留在了屏幕上。
- 跑 gate 之前先 `Get-Process xyzpdf | Stop-Process -Force`：真窗口那一遍的实例锁着 exe，
  编译器报的是 `LNK1104: 无法打开文件"xyzpdf.exe"`，长得像代码错了。

**没在屏幕上做的**：`，`→`, ` 那一句只在**两条以上**挂起修改的英文小结里看得见，这一遍英文屏上每屏都只挂着
一条操作，所以那一句靠的是钉字节的检查而不是图（追它去点 旋转页面 的 确定，好凑出第二条操作，那一个框
不吃 `{ENTER}` 也不吃量出来的点击，五轮之后停手 —— 见上面"手法"第二条）；
≥100 份时补零宽度换档那条路（测试核到 60 页 12 份按名字排序 = 按页排序，屏幕上最多到 40 份）；**暂存名
计数器停在 99 那一处是数出来的，不是对照跑出来的** —— 要让改前那一份变红需要 100 页以上的 fixture 和
100 次以上的重放，而 40 份那一条在改前的代码上也会绿（每份各让一个名字，走不到那个上限）；英文的
"会等一会儿"那一句（阈值要在 600 页那份上开，屏幕上走的是 40 页这份）；
`这份要写出的文档只有 %1 页，拆不出第二个文件来。`（40 页底稿走不到那个分支）；
**装着的这一份上没有拆分** —— 包里还是 §19 那三个哈希相同的 exe。

**收尾**：`appearance` 键删了（看英文那几遍写的）、`files\lastOpenDirectory` 没有、
`recent\documents` 里 scratch 路径 **0 条**（用户自己那 5 条留着）；`scratch/out/` 是空的（staged 名字
没漏在真文件夹里）；实例全停。

**目录**：`update_translations` 两次 —— 426 → 454（**28 条新**，拆分本身）→ **456**（又 **2 条**：那两个
分隔符）。28 条里 26 条手写，`取消` / `确定` 那两条是 lupdate 的 same-text heuristic 从别处抄来的旧英文，
先核过措辞与本目录其余部分一致才摘掉 `type="unfinished"`（§17 那个手法）。现在 **0 条 unfinished**、
`&amp;amp;` **0 处**（§15 那一条的复发检查）；i18n 探针 54 → **63 条**。
`文档` 菜单现在八行：五行记修改、两行在列表里走、一行写文件。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。

**仍然欠的**：~~合并界面在点之前就说"拆分挂着"（第 2 条）；关于 / 第三方那两列走 `tr()`（第 3 条）~~
（这两条 → **§22 已改**，屏幕那两遍仍然欠，记在 §22 第 7 条）；
两句操作连成的英文小结、~~≥100 份的命名换档与撞暂存名~~（这两条 → **§24 补上了**，一条加检查、
一条改的是上限本身）、英文的等待句、单页文档那一句，都还只在测试或
注释里；带注释/表单的供体合并（`fixCopiedAnnotations`）；**重打包并重装**（这一片不在 §19 那份 exe 里）
（→ **§23 换过来了**，装着的这一份现在带六样）；
就地保存；加密文档在界面上怎么交代口令；标签拖去别的窗口时那句措辞；状态条页号与屏幕不一致（§13）；
§15 那四条没验到的；英文的单复数（`Moves 1 pages`、`pages 40-40`、`Merges 2 pages from 1 files`）；
`gui-verify/*.ps1` 还没搬进 `tools/`（这一片又添了 19 个）。

## 21. 加密 / 去掉口令：第二个不改任何一页的操作，和它照出来的那个装饰性的第二个口令框（2026-09-25）

三个决定还是他定的：**加密和解密两边都做**（不是只做"给文件加锁"那一半），而且**解密那一半先量**
（`tests/encryptprobe.cpp`）；**六个权限勾，并且界面必须说这些位只是写给阅读器看的请求**；
**只写 R6/AES-256，不给版本下拉**，框里要有一句"Acrobat X 之前的阅读器打不开这一份"。
这一节记的是：为什么这一片的撤销跟拆分一样不需要新机制、"一个口令而不是两个"是从哪一次测量里掉出来的、
六个框到 `/P` 那个整数的映射（本轮量出来、测试每次跑都自己打一遍）、测试撞出来的那一条**是产品 bug
而不是测试 bug**，和这一遍**为什么屏幕上一个字都没看到**。

1. **加密也不改任何一页，所以 `rebuildLive()` 里仍然没有它的分支。** 和 §20 同一条理由：它改的是保存
   写出去那一份**带哪个字典**，不是里面有哪些页。于是撤销又是"把那一条从列表里拿掉"，没有反写那一步，
   星号那条链跟着自己对上。这一片的惰性比拆分还多一层 —— 拆分至少还要说"哪几页进哪一份"，加密连页都不看。
   所以一条删页和一条加密能同一次保存出去，而且这两条之间问不出顺序：`/P` 只有一份，列表里最后那条
   Protect 作数（`protectionToWrite()` 就是那一个函数给的答案）。
2. **一个口令，不是 PDF 的 user/owner 那一对。** 这是先量那一步换来的（`tests/encryptprobe.cpp`，
   手跑，40 行输出，头三条是仪器自己的对照）。它打的四份底稿里，**同一份文件用 user 口令开和用 owner
   口令开，读出来的 `R` / `P` / 各个标志一字不差**（`locked-allowmodify`、`locked-forbidmodify`、
   `restricted-empty-user` 三份各两行，逐字相同）—— 于是"用 owner 口令解除限制"这个读者的常识，
   在这个实现里连可问的问题都不存在。既然两把钥匙问不出区别，界面上再要第二个口令就是**假装**有那个区别 ——
   所以框里只有一个口令（外加一个"再输一遍"），写出去的那一份 U 与 O 是同串。
   更要紧的是那三件事**一个只有 user 口令的会话全做得了**：
   ① `setPreserveEncryption(false)` 写出一份不要口令的副本（`/Encrypt in bytes=no`，空口令打得开，40 页还在）；
   ② 同一次保存里顺手删掉一页（`reader sees 39 pages`）—— 所以 解密 和 编辑 共用一次保存是量出来的，
   不是设计上的希望；③ 在已有的 `/Encrypt` 上换一套权限位再写一次。
   而 `/P` 明写着禁止修改的那一份（`P-1036`），这三件事**一件都没被挡住** —— 这就是"这些位只是请求"的证据，
   它挡的是随手打开的人，不挡拿到文件的人。所以这句话必须印在框里：组标题就叫
   「允许的用途（这些限制写在文件里，不是强制的）」，因为不印的时候读者的预期是"我勾了禁止打印，别人就打印不了"。
   ③ 还带出一句要交代读者的话：**换一次加密会把旧口令换掉**，重新加密之后那份副本上"老口令"是
   `invalid password`，只有新口令打得开。
   一个量到但这里没用的怪处：`same-user-and-owner` 那一份（两把钥匙同串，也就是本程序写出去的形状）
   用 owner 口令开是**拒的**、用 user 口令开是通的。这一片不受影响，因为这里从来不问"是哪把钥匙"；
   记着是因为它说明"两把钥匙写成一个"在库里的行为不是对称的，将来谁要加 owner 口令那一步，
   得先把这三行重跑一遍。
   另外 `restricted-empty-user` 那一份是**空口令 + 带限制**：它不需要口令就能开，`/P` 上却写着禁止。
   所以"没有东西可去掉"那一句必须把两样都点名（没有口令，**也没有**权限限制），只问"要不要口令"会
   把这一类文件说成没锁。
3. **`/P` 那一个整数：六个框各管哪些位，是量出来的**（`documenteditor_smoke` 一次跑打出 33 条 note，
   这一段就是其中 8 条）：

   ```
   六个全勾                    R6  /P = -4
   关掉 打印                -> /P -2056   清掉 0x804  （bit 3 打印 + bit 12 高分辨率打印）
   关掉 复制文字和图片        -> /P -20    清掉 0x10   （bit 5）
   关掉 插入、删除或旋转页面  -> /P -1028  清掉 0x400  （bit 11）
   关掉 批注                  -> /P -36    清掉 0x20   （bit 6）
   关掉 填写表单              -> /P -260   清掉 0x100  （bit 9）
   关掉 其它修改              -> /P -12    清掉 0x8    （bit 4）
   ```

   真正钉住的是三条不变量，不是这张表：**每个框都动得了 `/P`**（有一个勾空转，框上那六个标签就是假的）、
   **关框只清位不授权**（`~on & offBits` 必须为 0 —— 否则"取消一个勾"反而多给读者权限）、
   **六个位互不重叠**（两个框共用一位，后勾的那个悄悄盖掉前一个，读者照小结做出来的文件就不是他答应的那一份）。
   `打印` 那一条值得单看：qpdf 的打印参数是三值枚举（`qpdf_r3p_full` / `qpdf_r3p_low` / `qpdf_r3p_none`），
   一个框只给得出 full 或 none，所以"只准低分辨率打印"这一档在这个界面里**到不了**；而 full 一次占两位
   （bit 3 和 bit 12），所以那个勾关掉的比它的标签说的多一位。
   `Carry`（什么都没要）走的是 writer 的默认 `preserveEncryption = true`，源文件的 `/P` 原样带过去：
   量到 `-1028 → -1028`；带一条请求时 `-1036 → -4`；去掉口令之后 `R0`、`/P` 不存在。这个默认值在预过滤、
   内容规范化、QDF 三种模式下都是关的，`setR6EncryptionParameters` 一调用也关 —— 所以将来任何一行加上
   那三样之一，就会把 删页 变成"发未锁副本"的工具；这就是比特要在**两侧**都比的原因
   （`src/edit/documenteditor.cpp` 里那段注释记的是这个，不是给今天的实现记功）。
   代价（`encryptprobe` 那一段：同一份文件写三种，量的是"加密"这一步值多少）：

   ```
   40 页 / 13 KB      原样带锁 = 14 ms   去掉口令 = 10 ms   去掉口令并删一页 = 10 ms
   600 页 / 196 KB    原样带锁 = 73 ms   去掉口令 = 45 ms   去掉口令并删一页 = 45 ms
   ```

   **加密那一段才是开销**，不是"多写一份"：去掉口令比把锁原样带出去还快 4 ms（40 页）到 28 ms（600 页），
   而删一页在两种情况下都不另外要时间。新加一道锁的那 14 ms 与 §20 那三条（10 / 123 / 189 ms）同量级，
   所以框里没有为加密单独设一句"会等一会儿" —— 它落在没人说话的那一段。
   **跨厂商那一句仍然测不了**：另一家的阅读器围着同一个整数写不同的话，这台机器上没有第二个阅读器可问。
4. **测试撞出来的那一条是产品 bug。** 第一次跑这一片的门禁，报的是
   `FAIL: one box filled and the other empty is two different口令` —— `再输一遍` 那个框**收进来了但从来没比过**，
   所以它是装饰性的：读者打错一个字母，程序会拿那个打错的口令把文件锁上，然后没人打得开。
   修 = 在 加密 分支里比 `passwordEdit->text() != confirmEdit->text()`，灰住按钮并在小结里点名
   （`两次输入的口令不一样。`）。**放在 加密 分支而不是最外层**，因为 去掉口令 时那两个框是灰的 ——
   灰着的框不能挡路，这一条和"拒的是哪三样"是同一类判断。下一遍 26/26。
   为什么测试看得见而人看不见：这一片照 §17 为合并开的那个先例，**在离屏里构造真框**，
   拿 `findChild<QDialogButtonBox*>()` 的 `isEnabled()` 当读者的眼睛（`documenttab_smoke` +136 行、四个框作用域）。
5. **三条拒绝，框和编辑器各说一次，而且都在点之前。** ① 列表里已经挂着一次 去掉口令；② 源文件没锁过、
   也没有挂着一次 加密（那时候"去掉"没有指称）；③ 这一次请求和 `protectionToWrite()` 一模一样
   （"按下不会多改什么"）。三样都不是点下去之后的警告框，而是**确定 灰住 + 小结给理由**，
   测试两侧都问：框灰的那一个请求，`protectWith()` / `removeProtection()` 也必须拒。
   §20 第 2 条那个不对称（拆分框点之前就拒、合并框点之后才说）这一片没有犯。
   框是每次点菜单新建一个，所以它不可能对着一个更老的列表表态 —— 这一条也是测试里问的（第二次开框，
   口令框是空的、六个勾是记下的那六个，重打同一个口令就撞上第 ③ 条）。
6. **口令不进任何一句话。** `describeOperation()` 里没有它，所以撤销条目、`pendingSummary()`、状态条、
   丢弃确认里都没有；它只在内存里（`DocumentTab::documentPassword`、`DocumentEditor::sourcePassword`），
   不写设置、不写日志、不写小结。撤销条目说的是 `加上打开口令（AES-256）` 这**件事**，不是那把钥匙。
   这一条没有测试能替它守住（测试自己就是读不到口令才知道对），所以它是写在 `documenteditor.h` 注释里的
   一条约定，撞它的代码得自己看见。
7. **新标签不再问一次刚才打的口令。** `documentSavedAs(path, changes, pageCount, fileCount, password)`
   多带一个口令，`openDocument(path, page, password)` 接住。这里**不是**直接读 `protectionToWrite().password`：
   `Carry` 的情况下副本是拿**源文件**那把钥匙锁的，读那个函数会给出空串，于是刚保存完的新标签会向读者要一个
   他从没打过的口令。所以有 `passwordOfWrittenFile()` 这一个函数给这一份答案 —— 和 §15 的 `planMove()`、
   §17 的 `mergesToRecord()`、§20 的 `pieceRanges()` 同一个理由。
8. **目录**：lupdate +45 条（总 **501** 条、25 个 context、**0 条 unfinished**、`&amp;amp;` **0 处**），
   i18n 探针 63 → **68 条**。另加两条**相等**检查，钉住"同一个中文串在两个 context 里只能有一个英文"
   （`去掉口令` 既是框里的按钮也是撤销条目；`加密 / 去掉口令` 既是框标题也是警告框标题）—— 这不是审美：
   lupdate 的目录按**源文**索引，一个中文串就是一个 key，写反了会变成 .ts 里两条不同英文而没有任何一条检查
   看得见，因为每一条单独都是"翻译过的"。英文菜单行的助记符是 P（中文那一份是 J），因为英文那五行已经占了
   E/R/M/F/C。
9. **真窗口这一遍没看成，而且原因不在这份代码里。** 指针钉死了：`SetCursorPos`、`Cursor.Position` 赋值、
   `SendInput` 带 `MOUSEEVENTF_ABSOLUTE|VIRTUALDESK` 三种问法，读回来都是同一个 `(638,1181)`；
   前台换不来：`SetForegroundWindow`、`AttachThreadInput`、`SwitchToThisWindow`、真·鼠标点击四条路之后
   `GetForegroundWindow` 仍然是 `Program Manager`，`kit.ps1` 的 `Activate` 因此拒发键（那个守卫是对的，
   它挡住了键落进开发用的 IDE）。排除掉的：不是远程会话（`SM_REMOTESESSION = 0`）、不是提权差异
   （`elevated = False`）、驱动与目标同在 session 1、单块 2560x1440 且 `VirtualScreen` 与 `GetWindowRect`
   同一坐标系、截图是活的（任务栏时钟在走）。**结论：注入的鼠标和键盘被吃掉了**，最像某个常驻程序挂了
   `WH_MOUSE_LL` 并丢弃 injected 那一位。所以这一片屏幕上一个字都没看过：三条拒绝、两次口令不一致、
   去掉口令那两句、英文整屏、写完那份带锁的文件重开要不要口令 —— 全只有第 4 到第 7 条里那些断言撑着。
   驱动脚本已经写好等着：`gui-verify/enc-a.ps1`（起实例 → 文档菜单 → 开框 → 报框的矩形），
   后面按 §20 那套再补三刀。

**收尾**：这一遍起的 scratch 实例已停；`files\lastOpenDirectory` 删了；`recent\documents` 里 scratch 路径
剪掉一条（6 → 5，用户自己那 5 条留着）；`appearance` 键没有（这一遍没切英文）。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。
这一片的量：`documenteditor.cpp` +208 / `.h` +137、新框 `protectpagesdialog.cpp` 228 行 + `.h` 96 行、
`documenttab` +35/+33、`mainwindow` +51/+9、测试 `documenteditor_smoke` +582（check 332 → **428**）、
`documenttab_smoke` +136、`i18n_smoke` +40、`encryptprobe.cpp` 404 行（它不进菜单，只回答那三问）。

**仍然欠的**：**真窗口这一遍**（第 9 条，整片一个屏幕都没看过 —— 这是这一节最大的一条，也是这一串里
第一次连"点之前灰住"都没在屏幕上确认过；§23 那条 UIA 通道现在能读**装着的**这一份的菜单，
第 4 条读到的就是这两行新菜单项，但开出框还是不行）；~~**重打包并重装**~~（→ **§23 换过来了**：
包里那份 exe 现在带删页/旋转/重排/合并/拆分/加密六样，三处哈希相同）；
§20 那四条里的两条（≥100 份命名换档、
英文单复数 —— 另两条"合并框的措辞时机"与"关于/第三方那两列走 `tr()`"已经改在 §22）；
带注释/表单的供体合并（`fixCopiedAnnotations`）；就地保存；
标签拖去别的窗口时那句措辞；状态条页号与屏幕不一致（§13）；`gui-verify/*.ps1` 还没搬进 `tools/`
（这一片又添了 4 个：`enc-a.ps1` 是那一遍没跑成的驱动，另外三个是量"输入到底通不通"的探针）。

## 22. §20 记下没改的那两条：一句拒话挪到点之前，和四个进不了目录的格子（2026-09-25）

两条都是上一节收尾时列在"仍然欠的"里的，合起来**没有动任何一个算法**：一条把本来就存在的拒绝从
点下去之后搬到点下去之前，一条把四个读者看得见的格子从 `QStringLiteral` 搬进 `tr()`。
第二条跑出来的那个对照结果是这一节里唯一一句新的话，而且它说的是**测试本身**，不是这个功能。

1. **合并框现在在点之前就拒。** §20 第 2 条记的是措辞时机而不是规则：规则一直住在模型里
   （`DocumentEditor::mergeFile()` 见到挂着一次拆分就拒），拆分框则是灰住按钮并把理由写进
   小结，于是同一类"这两样凑不成一件能核对的事"在两处有两个时机 —— 读者在合并那一侧要按一次下去
   才知道不行。改完之后两边同形。
   框读的是 `splitPieceSize()` —— **和模型同一个函数**，不是把那句拒绝抄一份进对话框。这一条和
   §15 的 `planMove()`、§17 的 `mergesToRecord()`、§20 的 `pieceRanges()`、§21 的
   `passwordOfWrittenFile()` 是同一个理由：一个 promise 有两个算法，界面答应的和文件写出的就能不同，
   而测试可以全绿。
   理由那一句是 `mergeFile()` 的**原文逐字**（`这份文档已经说了要拆成每 %1 页一份。先撤销那一步，
   再把文件合并进来。`），所以 lupdate 的 same-text heuristic 当场把它填进了新 context —— 五条新词里
   一条是这么来的，其余四条手写。跟着加了一条**相等**检查钉住两个 context 只有一个英文（§21 第 8 条
   同一条理由：目录按源文索引，写反了会变成两条各自"翻译过"的英文，没有任何一条检查看得见）。
2. **小结里是替换而不是追加。** 拆掉的是那句算术（`1 个文件一共取 2 页，合到最后面，按列表的顺序
   连着排在第 41-42 页。`）。它说的 41-42 是**一批不会被记下的页**在**一份不会写出的文件**里的位置，
   和 §20 讲 `documentSavedAs` 多带一个 `fileCount` 那一段里，印在 15 页新标签旁边的"整份 40 页"
   是同一类没有指称的数。拆分那一侧当年是
   **追加**（那里的算术本身没错，错的是"哪几页进哪一份"说不出来），这一侧被拒的是整件事而不是某个数，
   所以留一个没有指称的数比留两个错处更糟。
   新块问的是三件事一起：框灰住、理由用模型的措辞、**且**不再许诺页号；外加 §21 第 5 条那条规则的
   反向问法（框灰的那一个请求，`mergeFile()` 也必须拒），和撤销那次拆分之后同一个请求又亮
   —— 这后两条合起来才说明框读的是列表**现在**的样子，而不是它第一次被打开时的样子（框每次点菜单
   新建一个，同 §21 第 5 条）。
3. **第三方那张表有四个格子从来没进过目录。** `src/core/thirdpartynotices.cpp` 的 `components()`
   每行下面那段说明一直在 `tr()` 后面，上面几列没有：`（编入 Qt6Pdf.dll）`、
   `（编入 Qt6Gui.dll、Qt6Core.dll）`、`各自的原许可证，见许可证全文`，和第四行自己的名字
   `Qt 内置的第三方库`。§20 那一条点了两个格子名，这一遍逐列数下来是四个（第二格那个中文顿号正是
   §20 第 1 条那一类"由代码而不是句子产生的、读者看得见的标点"，英文那一份跟着改成 `, `，
   与 §20 钉字节的那两条同一个排法）。
   为什么目录里没有：lupdate 只看得见 `tr()` 后面的串，所以这四格**不是一条 unfinished 条目**，
   是**压根没有条目**。这是双语这一串里的第三种失效形状：§15 那六条是"目录里有但内容错"
   （`&amp;amp;`），§20 那两条是"句子对、标点从代码里漏出去"，这一种是"读者看得见的一句话不在目录里"，
   而目录完整性检查（0 条 unfinished、0 处 `&amp;amp;`）对它完全无声。
4. **一个用串的名字去问的探针，看不见一个没进目录的串** —— 这一条是对照跑出来的，不是推的。
   加了两样：那四格各一条具名探针（68 → 73），和一条**问表本身**的循环（`ThirdPartyNotices::components()`
   逐行把 name / libraryFile / licence / note 四列问一遍 `hasChinese()`；URL 与资源路径不是散文，跳过）。
   然后只把 `（编入 Qt6Pdf.dll）` 那一格改回 `QStringLiteral`，`.ts` 一个字不动 —— 也就是"目录里有、
   代码不来问"这个真实形状 —— 门禁报 **1 条失败，而那一条是循环报的，具名探针是绿的**。
   它绿得完全应该：`translate("ThirdPartyNotices", "（编入 Qt6Pdf.dll）")` 仍然从目录里拿回英文。
   所以 §15 起那整套探针（现在 73 条）挡的是**漏翻**，挡不住**没问**；后者只有"问代码它到底持有什么"
   那种循环挡得住。两条不互相顶替：具名探针留着，因为它挡的是另一件事（有人把某一格改写去别的
   context，循环按不到名字）。
   循环带的是同一副眼镜：`hasChinese()` 的范围是 0x3400–0x9FFF，所以**顿号和全角括号它也不认**（§20
   第 1 条那句局限对这条循环同样成立）。这一格是整格换掉的，不受影响；"只漏一个标点"那一类仍然
   要靠钉字节的那两条。
   循环链的是**真表**，所以 `i18n_smoke` 从此多链一个源文件（`CMakeLists.txt` 里那一行拆成三行）。
   抄一份字符串进测试来问，就又变成"两处算法给两处说法"。
5. **两个对照各打住几处**（都是一行、可逆，跑完 grep 过 marker 确认清干净）：控制 A 把合并框那两条
   一起拆掉 → 新块 **3 处红**，其余 25 个测试全绿；控制 B 就是第 4 条那一次。
   顺带量到一件关于门禁输出的事，机制在 `tools/build_all_tests.py:270-272`：**一个测试失败它只印
   第一条含 `FAIL` 的行，还截到 140 字符** —— 控制 A 那三处在 `python tools/build_all_tests.py` 的
   输出里只看得见一条。所以"1 failed"读作 1 个测试，不读作 1 条断言，而且失败的那一条后面可能还有
   两条没人转述；要数断言得自己去跑那个 exe（`PATH` 带上 Qt 的 bin）。这和"绿的时候不报测试名"
   是同一族：门禁给的是一副摘要，不是一份清单。
6. **目录**：501 → **506** 条（context 仍 25 个、**0 条 unfinished**、`&amp;amp;` **0 处**），
   探针 68 → **73** 条，另加 1 条相等检查 + 1 个 4×4 的循环（4 行 × 4 列 = 16 次问话）。
7. **这一片没有在真窗口里看过**，理由与 §21 第 9 条是同一条（这台机器的合成鼠标和键盘被吃掉，
   指针钉在 `(638,1181)`）。具体欠的是两个画面：英文界面下第三方那张表四列是不是全英了 —— 那是
   §20 **用眼睛**发现的东西，本该用眼睛收掉；和合并框在拆分挂着时那颗灰掉的按钮。两屏都并进 #83
   那一串里跑，那张表的英文甚至不需要打开 PDF 就能看到（帮助 → 关于 → 第三方开源许可）。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。
这一片的量：`mergepagesdialog.cpp` +15/-2（一个分支、一个 `splitting` 读数、`setEnabled` 多一个条件）、
`thirdpartynotices.cpp` +10/-4（四个格子进 `tr()`，外加那段"每一格都是给人读的"注释）、
`CMakeLists.txt` +2、`documenttab_smoke` +81 行（11 处 check，201 → **212**）、`i18n_smoke` +49 行
（5 条探针 + 1 条相等检查 + 1 个循环）、`xyzpdf_en.ts` +5 条。

**仍然欠的**：这一片自己那两屏（第 7 条，在 #83 那一串里）；§20 剩下那两条（≥100 份的命名换档、
英文单复数）；~~重打包并重装~~（→ §23 换过来了）；
带注释/表单的供体合并（`fixCopiedAnnotations`）；就地保存；标签拖去别的窗口时那句措辞；
状态条页号与屏幕不一致（§13）；`gui-verify/*.ps1` 搬进 `tools/`。

## 23. 带着拆分、加密和 §22 那两条重打了一次，顺手撞到一条**不吃合成输入**的读数通道（2026-09-25）

链条还是 §16/§19 那三步，一步没改。这一节记的是：换过来的那三个哈希、那三个哈希顺带盖住的英文目录，
和撞到的那条新通道 —— 它能读**装着的**那一份的菜单，也因此读到了 §20/§21 那两行一直只在开发树
存在过的菜单项；它读不了模态框，所以 #83 那一串还是欠着，这一点新通道没有改掉。

1. **换过来的东西**：装着的 `xyzpdf.exe` 从 §19 的 `6acbe2ed…38ab9e3658` 换成 `f434518e…2f923f`
   （1,244,160 字节）。包 `17,764,818` → **`17,789,705`** 字节（+24,887），签名 `Valid`、
   签署人指纹 `221C6B38…0A83`，`verify-package.ps1` 判词逐字与 §19 相同：解包 **39.3 MB / 62 个文件**、
   "starts from the unpacked package with no Qt on PATH"、"the package is complete and runs standalone"。
   **文件数一个没变** —— 拆分、加密、§22 那五个词条没往负载里添任何一个文件，这是同一句读数、
   和 §19 那条"合并没有往里加东西"同一个理由。
   重装之前先跑了 `python tools/build_all_tests.py --build-only`：报 built、什么都没重链，
   构建树那份 exe 哈希一个字没动 —— 也就是 §22 收尾那次门禁已经把改动链进去了，
   这一遍装的是**跑过 26 个测试的同一份字节**，不是"又编了一次"的另一份。
2. **三个哈希**（§16 那条规矩，第四次做）：`build-local-release/xyzpdf.exe`、解到 `%TEMP%` 的那一份、
   `C:\Program Files\WindowsApps\allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4\bin\xyzpdf.exe`
   三处同为 `f434518ef7ea52f5404596f556a353a1e9d5b4004a21a8a7998e4d28012f923f`。
   装着的这一份也真的起来了：`explorer.exe shell:AppsFolder\…!XYZPDF` → pid 27316、
   标题 `[XYZPDF]`、`MainModule` 读回的就是 WindowsApps 里那条路径。
3. **这一份哈希盖住的不止代码**。查这一遍才知道：装着的包里 `bin\` 下面**一个 `.qm` 都没有**（数过：0），
   因为程序自己的目录是 `qt_add_translations` 编译后**挂在 target 上**的（`resources.qrc` 顶上那段注释
   写的就是这件事："This program's own catalogues are NOT here"，松放的只有 Qt 自己那份中文目录）。
   于是 §22 那 506 条词条、73 条探针加 4×4 循环问过的**同一份编译后目录**，就住在这三处相同的字节里。
   这一条把 §22 第 7 条那两屏里的英文那一屏**去掉了一半**：不是"用眼睛看见四列全英"，
   而是"程序用来查英文的那份目录，就是被问过 16 次全绿的那一份" —— 这一半现在成立了，
   另一半（那四格在窗口里排成什么样）仍然只是没看过。
4. **撞到的通道：UI Automation 读 Qt 菜单，不吃合成输入。** 这台机器的鼠标键盘被钩子吃掉之后，
   装着的这一份就一直只能"起得来"。这一遍试的是可访问性树，不是 `SendKeys`：
   `ExpandCollapsePattern.Expand()` 打在 `文档(D)` 上，读回来 17 条 MenuItem ——
   `删除页面(E)…` / `旋转页面(R)…` / `移动页面(M)…` / `合并文件进来(F)…` / `拆分文件(C)…` /
   `加密 / 去掉口令(J)…`，然后 `撤销(U)`、`重做(O)`，最后 `保存修改为(S)…`，八条顶层菜单也在
   （`文件(F)`…`帮助(H)` 加 `Language`）。三条各自有用的读数：**两行新菜单项第一次出现在装着的这一份上**
   （§20/§21 记的都是开发树），**六行在没有文档时全是 `enabled=False`**（该灰的灰着，不是"看着在但点不动"），
   和**没有一个 `&`**（§15 那一族在装着的字节上仍然干净）。
   顶层那八条早就读得到，`Expand` 才是要紧的那一步：不展开时树里只有八条，子菜单不在。
5. **这条通道读不了模态框，所以 #83 还是欠着。** `InvokePattern.Invoke()` 打在 `文件`→`打开(O)` 上
   **确实开出了框**（`#32770`、名字"打开 PDF"，在主窗口的子树里而不是另一个顶层窗口），但两件事拦住往下：
   同步的 `Invoke()` 要等模态循环结束才返回，于是调用超时抛异常；而原生选择框的孩子是通过
   MSAA→UIA 桥接出来的，控制类型一律报 `Pane`、`GetSupportedPatterns()` 一律空，
   文件名框上 `ValuePattern` 直接"不支持的模式" —— 填不进路径；把点"打开"挪进另一个 PowerShell
   job 里做，job 自己也不回来了。**三轮，收手**（同 §21 第 9 条那条规矩）。
   两件事的确定性不一样：`ValuePattern` 不支持是**当场量到**的，而"点下去没生效"和"调用挂住"在
   那一次里分不开 —— 文件名是空的，那颗按钮本来就没有东西可提交。**收手**说的是不再往这条通道
   花轮次，不是说它判死了。
   所以这一遍的新通道只把"菜单上有没有、灰不灰"变成可核对的，**没有**把"框里那一句怎么说"变过来：
   合并框那颗灰掉的按钮、加密框那两栏口令、英文那张四列表，仍然只在测试和目录里绿着。
6. **一条关于 `.ps1` 编码的新读数**（撞在第 4 条第一次尝试上）：脚本里写 `-like '*文档*'` 匹配不到任何东西，
   打印出来是 `NO 鏂囨。 ITEM` —— PowerShell 按 ANSI 读没有 BOM 的 UTF-8 脚本，脚本里那句中文
   在比较之前就已经是坏的了，而**从 UIA 读回来的中文是好的**。同一个坑的新一层：
   `gui-verify/*.ps1` 必须 ASCII 这条规则，绕法是按码点拼（`[char]0x6587 + [char]0x6863`），
   不要指望文件编码。
7. **收尾**：这一遍起的实例 `taskkill` 掉了（选择框还开着的时候）；`verify-package.ps1` 留在
   `%TEMP%` 的两个 39.3 MB 解包目录删了（现在 0 个）；注册表这一遍**一个字没添** ——
   `files` 子键没有值（没走到 `lastOpenDirectory`），`recent\documents` 还是那 5 条老路径，
   一条 `many-pages.pdf` 都不在里面，因为文档从没打开过。

gate：这一遍没改任何源码，只跑了 `--build-only`（built、无事可做），所以 §22 那句
**26 passed, 0 failed** 仍然是这一片最后一次门禁的数。

**仍然欠的**：#83 那一串（现在**能读菜单**了，欠的是"开出框并看见那一句"，第 5 条说清了拦在哪）；
~~§20 剩下那两条~~（≥100 份的命名换档、暂存名停在 99 —— 两条 → **§24**）；带注释/表单的供体合并；就地保存；标签拖去别的窗口时那句措辞；
状态条页号与屏幕不一致（§13）；`gui-verify/*.ps1` 搬进 `tools/`（这一遍的四份 UIA 探针脚本在
`H:\tmp\uia*.ps1`，要搬就把第 4 条那个 `Expand` 读菜单的和第 5 条那个"能开框但不能填框"一起搬，
后者是一份负面结论，值得留）。

## 24. §20 欠的那两条：一条是加检查，另一条是把一句承诺的第二个出口拿掉（2026-09-25）

这一片不改任何一页、不改一句用户看得见的字，`.ts` 一个字没动。还的是 §20 自己在收尾时列下的两条：
**≥100 份时补零宽度换档**和**暂存名计数器停在 99**。第一条是纯测试；第二条写下来之后发现它不只在测试里 ——
那个上限给了循环一个"没测过就交出去"的出口。两条各自跑了一行对照。

1. **换档要两边都钉，因为一边测不到。** 宽度是 `qMax(2, QString::number(total).size())`：
   份数在 99 以内它等于 2，"永远等于 2"也等于 2 —— §20 那条 60 页 12 份的检查（还有新加的 99 份那一条）
   在**硬编码 2** 的代码上照样绿，所以它们不是对照，它们是"边界这一侧本来就该长这样"。
   真正的换档只有 100 份那一边看得见：`book 001 (1).pdf` / `book 099 (99).pdf` / `book 100 (100).pdf`
   —— 第一百份把**整套**名字都拖宽，第一个一起变（不是只有它自己三位）。
   再补一条按名字排序 = 按页排序，覆盖到 100 份：那才是这件事在 Explorer 里的全部理由，
   `1、10、11、2` 是一副没人能核对的顺序（§20 第 2 条原话）。
2. **停在 99 那个上限，坏的不是它撞不撞得到，是它从哪儿出去。** 改之前是
   `for (int attempt = 2; QFileInfo::exists(staged) && attempt < 100; ++attempt)`：两个出口，
   一个是"找到一个盘上没有的名字"，另一个是"数到 99，把手上那个**没测过**的名字 return 出去"。
   而第二个出口交出去的那个名字**是已经存在的文件**（循环条件刚为它成立过一次），紧接着
   `QPDFWriter writer(*document, staged…)` 就朝那个名字写 —— 也就是把别人已经写好的那一份截断。
   §20 记的是"这一处是数出来的，不是跑出来的"，并给了跑出来的代价：100 页以上的 fixture 加 100 次重放。
   这一遍换了个问法：**那句承诺归哪个函数管，就直接拿那个函数问** —— 一个固定 `stamp`、
   在磁盘上预先摆 99 个名字，三行就能把天花板顶到。
3. **为什么是"拿掉上限"而不是"撞满了拒".** 同一个函数里有**四句**带"请换一个文件名"的拒绝
   （`拆出来的文件里有一个（%1）就是正在打开的这份`、`另存为不能覆盖正在打开的文件`、
   `另存为不能覆盖合并进来的文件（%1）`、`%1 那里已经有一个文件了。拆分不会盖掉已经存在的文件`），
   它们够用是因为**那个名字是读者打的字**，他可以换一个。暂存名不是他打的、也从不拿给他看
   （唯一一次露面是那句"写好的文件留在 %3"），所以对他说"请换一个文件名"是一句没有宾语的命令，
   而且那条分支在没有 100 次重放之前谁都到不了。留上限的代价是读者的文档，去掉的代价是一个没人读的名字。
   于是这一片**没有新词条**。
4. **抽出来成 `static QString stagedPath(const QDir &, qint64 stamp, int piece)`**（`h:559` 声明、
   `cpp:395` 实现、`cpp:1407` 调用），为的还是 §15 `planMove()`、§17 `mergesToRecord()`、
   §20 `pieceRanges()` 那一条：一个承诺只有一个算法。原来它长在 `saveAs` 里的一个 lambda 上，
   "返回的路径背后没有文件"这句话只能靠跑一整次保存来问，而那个保存要凑齐 100 次同毫秒的重放。
   行为一个字没变：同一份 `stamp` 仍然由调用方带来、一次保存里各份靠 `piece` 分开，
   计数器只在**跨保存**或**有人留了同名文件**时才会走。测试里那三条"邻居那份的占用不推动这一份的计数器"、
   "晚一毫秒的下一次保存不继承这一步"、"空文件夹先给出那个 plain 名字"钉的就是这句话。
   播种放在 `scratch/staging/` 子文件夹里，因为 §11 那条检查是**数** scratch 里剩几个
   `xyzpdf-save-*.pdf`（一次被拒的写该只留一个，而且它的名字要能在报错里读到），
   跟这一百来份种子混在一起就量错东西了。
   顺带一处同名：`writeOne` 那颗 lambda 的形参原来也叫 `stagedPath`，抽出来之后就正好盖住新函数
   （编译没问题，读的人有问题），形参改叫 `staged` —— **程序里**从此只有一个地方说得出暂存名的形状。
   测试里还有第二处（第 6 条的 `shape()`），那是故意的：期望值得自己写出来，不然任何一条都比不出什么；
   而它写错了会被哪一条抓到，第 6 条说的就是这件事。
5. **两条对照，各红两条，且只红那两条**（改一行、重链、直接跑那一个测试二进制才看得见全部 FAIL 行 ——
   门禁只转述第一条）：
   - 宽度写死成 `2` → 红的正是 `the hundredth piece widens every name in the set, the first one included:
     book 01 (1).pdf / book 99 (99).pdf / book 100 (100).pdf` 和 `a hundred one-page pieces still sort by name
     into page order`；99 份那一条**绿**（第 1 条说的就是这个不对称）。
   - 放回 `&& attempt < 100` → 红的是 `99 taken names is not the end of the counter: it answers
     xyzpdf-save-1758765432100-7-99.pdf` 和 `no file is behind the name it hands back`；
     前面那三条"怎么走"的**全绿** —— 所以红的确实是天花板，不是循环第一次动。
   两处都改回去了，`grep` 过没有残留标记。
6. **写第一版时自己撞的一处**（不是跑出来的结论，记下来是因为它说明了哪一条在测什么）：
   `shape()` 一开始返回**文件名**，而 `stagedPath()` 返回**带目录的完整路径**，第一条比较必然对不上。
   读代码时看见的，没跑到。改法是让 `shape()` 也走 `staging.filePath(...)`，代价是那一条"空文件夹给出
   plain 名字"的检查从"顺带"变成**唯一**钉住名字形状的一条 —— 其余那些条测的都是计数器怎么走。
7. **这一片没碰的**：没有屏幕（#83 那一串一个字没变，这里全是不开窗口的算术）、没有重打包。
   构建树的 `xyzpdf.exe` 因为这些改动从 §23 的 `f434518e…2f923f` 变成
   **`be0b2c1228e83dc34849c8d54a9456c359c278a70c0527f7eb2f09a12bfafd17`**（中途那一次对照回滚之后
   还是 `8483588e…b753`，第 4 条末了那个形参改名又换了一次字节），
   大小**仍是 1,244,160 字节**（同一份负载、不同的字节）。装着的还是 `f434518e…`，也就是
   **那条 99 的上限在本机装着的这一份里还在**，屏幕上不会有任何差别。§16 那"三处哈希相同"到此又不
   同步了，下一次重打包要连这条一起重做。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。`documenteditor_smoke` 新增
**13 处 `check()`**（换档那 3 处 + 暂存名那 10 处），跑起来的断言数还要多：其中一处住在 `seed()` 里，
每往盘上摆一个名字就走一次，这一遍摆了 101 个。单独跑那一份：`all checks passed`、33 条 note。

**仍然欠的**：#83 那一串（这一遍一个窗口都没开）；§20 剩下的那几条（两句操作连成的英文小结、
英文的等待句、单页文档那一句）；英文单复数那一族（`Moves 1 pages`、`pages 40-40` —— 量过了，
Qt 的复数机制一条消息只能带一个有数的名词，所以它欠的是一个**措辞决定**不是几行代码）；
带注释/表单的供体合并（`fixCopiedAnnotations`）；就地保存；标签拖去别的窗口时那句措辞；
状态条页号与屏幕不一致（§13）；`gui-verify/*.ps1` 搬进 `tools/`。

## 25. §13 欠的那一条：屏幕上那一页从此只由一个函数说，并且拿**装着的**那一份做了对照（2026-09-25）

`.ts` 一个字没动。改的是 `src/view/pageview.*` 和它的测试，外加一次真窗口读数 —— 那一次读数顺手
成了 §23 那条通道的第一件**用它测过东西**的事（上一遍它只测了"能不能读菜单"）。

1. **症状不是缩放的事，是"一个几何函数只在一个通道上被重算"的事。** §13 写下的读数是
   "`Ctrl+2` 重排完，整页可见的是第 5、6 页，状态条还写着 `2 / 40`；再按一次翻页键就又对上了"。
   机制：哪一页在窗口正中是 `PageLayout::pageAtViewportCenter()`（`pagelayout.cpp:231`）的算术，
   而它以前只在 `scrollContentsBy()` 里被问一次 —— 也就是**只有滚动条动了**才算。缩放因子、适配模式、
   排版模式、旋转、视口高度这五样都会让"中线落在哪一页"换答案而**不动滚动条一格**：没有滚动事件，
   没有人重新问，屏幕上翻到了第 30 页而号还停在第 13 页。同一个数喂着状态条的 `N / M`、页码框、
   以及六个文档对话框的"当前页（第 %1 页）"，所以错的不只是窗口角落那一小块。
   （§13 那句"再按一次翻页键就又对上了"因此是机制本身，不是巧合：那一按产生了一个滚动事件。）
2. **一句承诺一个算法**：抽成 `void PageView::updateCurrentPageFromViewport()`（`pageview.cpp:1157`），
   在**每一个**能让答案改变的出口上问一遍 —— 七个：`setPageSizes()` 199、`setLayoutMode()` 219、
   `setZoom()` 251、`setZoomMode()` 281、`setRotation()` 342、`resizeEvent()` 1488、
   `scrollContentsBy()` 1500（最后那个原来写着同样的三行，现在只是搬进函数）。算术仍然只在
   `pageAtViewportCenter()` 一处，这里不重复它。
   `goToPage()` 故意**不**问：那条路是"宣布要去哪一页"，而比窗口矮的一页会被摆到让中线落进
   **下一页**去，重算等于把刚宣布的那一页再拿回去 —— 那正是 `adjustingScroll` 存在的理由，
   函数第一行就是它。
3. **适配模式是两趟**，这一条是量出来的不是想出来的：换适配模式改的不只是页高，还有页宽，
   页宽一窄，横向滚动条就消失 —— 视口因此**在下一个事件里又高了一截**（量到的：336 → 350 px，
   zoom 0.370546 → 0.387173），第二趟 `applyFitIfNeeded()` 跟着把答案又挪一次。
   所以 `resizeEvent()` 那一处不是给"用户拖窗口"备的装饰：它接的是**这一片自己第二次改的几何**。
   对照第 5 条的第二行会看见这件事在测试里长什么样。
4. **测试的期望值两边独立。** `documenttab_smoke` 里那一屏（---- the page the status bar names …）
   用一个 `pageOnScreen()` lambda 从 `pageRectInViewport()` 自己数出"屏幕上第几页"，
   和程序的答案（`currentPage()` + 它喊出来的那一句 `currentPageChanged`）**各自算一遍再比** ——
   不然一个错的函数可以自证。三处非空检查防止"站着不动也算过"：`shown != 11`（中线确实跨了页界）、
   `announcements > 0`（不是只有内部字段变了，状态条听得到）、`zooming to 400%` 那屏额外钉一句
   "滚动条一格没动"（这就是没有滚动事件的正证）。手算的页号（第 3 页、第 21 页、第 10 页）钉的是规则，
   lambda 钉的是"这条路真的重算了"。
5. **七个出口，七行对照**（改一行、重链、直接跑那一个测试二进制才看得见全部 FAIL 行 —— 门禁只转述第一条）：
   - 199 `setPageSizes` → `opening a two-page document names the page that is in front (bar says page 1, the screen shows page 2)`
   - 219 `setLayoutMode` → `turning on two-page mode … (bar says page 11, the screen shows page 21)`
   - 251 `setZoom` → `zooming to 400% … (bar says page 11, the screen shows page 3)`
   - 281 `setZoomMode` → **第一次跑是全绿的**。理由就是第 3 条：那一屏用的是窄窗口，适配把横向滚动条
     拿掉，第二趟 resize 顺手把数补上了，砍掉第一处没人发现。于是补了一屏
     `适应整页 in a wide window`（前置钉住 `horizontalScrollBar()->maximum() == 0` 和视口高度前后相同，
     也就是"不会有第二趟"），砍 281 才红得下来：
     `适应整页 in a wide window … (bar says page 11, the screen shows page 30)`
   - 342 `setRotation` → `rotating … (bar says page 11, the screen shows page 15)`
   - 1488 `resizeEvent` → 红四条：`适应整页 … (bar says page 27, the screen shows page 26)` 和
     `making the window shorter … (bar says page 11, the screen shows page 10)` 各带一条"没喊出来"。
     27 和 26 差一页就是第 3 条那一趟。
   - 1500 `scrollContentsBy` → **在补"scrolling to page 2"那一屏之前砍它是全绿的**：老的那条测试只数
     `currentPageChanged` 响了几次（`== 1`），从不比"说的是不是屏幕上那一页"，所以一个把号喊错的滚动
     路径能一路绿。补了那一屏之后它红两条：`scrolling to page 2 … (bar says page 11, the screen shows page 2)`。
     这一条不在 §13 的账上，是它自己的账：**"数次数"不是"说对号"的替身**。
6. **量到但没改的第二件事：新文档不回到第一页**（四条读数，临时插在测试里跑的，跑完删了）：
   ```
   reopen text.pdf       pages 2   scroll 25632 -> 1142   bar 2  screen 2   max 1142
   reopen mixed.pdf      pages 2   scroll   866 ->  866   bar 1  screen 2   max 1484
   reopen runs.pdf       pages 1   scroll   866 ->  630   bar 1  screen 1   max  630
   reopen many-pages.pdf pages 40  scroll    12 ->   12   bar 1  screen 1
   ```
   `setPageSizes()` 里有一句 `layout.setCurrentPage(0)`，那是**意图**（"新文档从第一页开始"）不是观察，
   而滚动条位置换新文档时不重置。第一行是"越界被夹回来"那一类：夹本身就是一次滚动，老路径顺手把号
   改对了（所以砍 199 只有第二行那一类会红）。第二行是没夹住的那一类：留下的位置还在新文档范围内，
   没有滚动、没有人重算，屏幕上第 2 页、号写第 1 页。
   这一片只把"号对不对"改了（多一个出口，第 5 条第一行），**"新文档该从哪一页开始"没改** —— 那要动的是
   打开时把滚动位置归零，会连带决定 `保存修改为` 之后重开这一份落在哪儿，是个产品决定，留给读者拍。
   测试里那一屏因此只钉不变式（号 == 屏幕），不钉它等于几 —— 将来真去"打开就回第一页"时它不该挡路。
7. **真窗口那一遍：两个二进制、同一台机器、同一个 fixture，一次 A/B。** 不吃合成输入的那条通道
   （§23）这次真的拿来量了东西：`RangeValuePattern.SetValue()` 把纵向滚动条摆到当前位置的 30%，
   `ExpandCollapsePattern.Expand()` 开第三个顶层菜单，按名字（那四个字从码点拼出来，脚本保持 ASCII）
   找到"适应整页"，`InvokePattern.Invoke()`，然后读状态条那个匹配 `^[0-9]+ / [0-9]+$` 的 Text：
   ```
   装着的（§23 那一份 f434518e…2f923f，没有这一片）
     A 滚到 content 20360   bar 13 / 40   max 67868
     B 点了"适应整页"        bar 13 / 40   max 27612      <- 号没动
     C 再滚一格              bar 30 / 40                  <- 一格就跳 17 页对上
   构树的这一份（4b161464…c3f58e，带这一片）
     A 滚到 content 20360   bar 13 / 40   max 67868
     B 点了"适应整页"        bar 30 / 40   max 27612      <- 号当场就是对的
     C 再滚一格              bar 30 / 40
   ```
   B 那一行就是 §13 那句话的现在时：几何确实换了（`max` 从 67868 掉到 27612），号还停在 A。
   期望值不靠自己声明 —— **没修的那一份在 C 给出的那个 30**，和修过的这份在 B 给的，是同一个数。
   两处测量上的教训记下来：(a) 第一版把位置定成绝对像素 30000，结果它落在新范围之外，被夹到末尾，
   夹本身就是一次滚动，两行的 B 都变成 40/40 —— 什么也区分不出来；取"当前范围的三成"才留在范围内。
   (b) 窗口里纵向滚动条**不止一个**（缩略图侧栏那一条也是竖的），要按范围最大的那条挑。
   另外：`SetValue` 偶发 `Could not open the process token`，重试一次就过；跑完进程自己退了，
   两边 settings 都走 `XYZPDF_SETTINGS_PATH` 落在 `H:\tmp\gui-*.ini`，`HKCU\Software\xyzpdf\xyzpdf`
   跑完仍是空的（`Get-ItemProperty` 读不出东西）。
   **这一遍仍然不是 #83**：那三项要开对话框、要往原生路径框里填字，那条通道 §23 量过是不通的。
8. **这一片没碰的**：没有新增词条，没有动 `#83` 那三条，**没有重打包** —— 装着的还是
   `f434518e…`（第 7 条正是拿它当"没修的那一份"用的），所以这一片在装着的界面里还没有。
   构建树的 `xyzpdf.exe` 从 §24 记的 `be0b2c12…` 变成
   **`4b1614645e0360c1aee9f62eaa98a336cf46181bdfebbbb57e9a0cb378c3f58e`**，大小**仍是 1,244,160 字节**。
   §16 那"三处哈希相同"到这里又欠一次重取，下一次重打包要连 §24、§25 一起。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。`documenttab_smoke` 新增
**20 处 `check()`**，跑起来 55 次断言：其中 5 处住在两个共用的 lambda 里（起始那一屏的 2 处、
不变式那 3 处），八屏各走一遍。

**仍然欠的**：#83 那一串（第 7 条测的是不用开窗口的东西）；**新文档打开时不回第一页**（第 6 条，
量好了没改）；§20 剩下的那几条（两句操作连成的英文小结、英文的等待句、单页文档那一句）；
英文单复数那一族（`Moves 1 pages`、`pages 40-40`）；带注释/表单的供体合并
（`fixCopiedAnnotations`）；就地保存；标签拖去别的窗口时那句措辞；老那条"数次数"的测试要不要改成
比号（第 5 条末）；`gui-verify/*.ps1` 搬进 `tools/`。

## 26. 水印：第七样，第一样往一页上画东西的，和它先在真窗口里露出来的那个 1%（2026-09-25）

`.ts` 这一回动了：506 → **537** 条（context 仍 25 个、**0 条 unfinished**、0 条 vanished）。
前面几节的模板是"先落地再量"，这一片反过来 —— 先量了一次才敢动笔，理由在第 1 条。

1. **`docs/PLAN.md` 那句"这些不碰页面内容流"是错的，而错的正是这一样的全部难度。** 页码、朝向、
   存在与否是字典上的事；水印是**画上去的东西**，要往那一页的内容流里加算符。所以先写了
   `tests/watermarkprobe.cpp`（名字带 probe，门禁编它但不跑它）去量两条候选路各自在渲染器里
   长成什么样。量到的（qpdf 12.4 + Qt 6.11.2，40 页 fixture 和一份 6 页中文数据手册）：
   - 文字路把字交给 PDF 算符之后，**那行字进了页面的可提取文本**（13 个字符变 30 个）——
     于是它会跟着复制、划线翻译、导出一起被拿走。位图路那边 13 还是 13。
   - 不嵌字体的 Type0/STSong-Light 确实能画出中文（渲染器自己换了一套系统字体），代价是**同一份
     文件在下一台机器上画成下一台机器的字**，没有可换的时候就是空框（离屏平台上量到文字路的
     中文变成一串形状不明的框）。
   - `/SMask` 是被认的：没有它，位图那块透明画出来的底**盖住了页面上原有的字**（每个取样区都读
     成 1.0000 墨）；有它，页面原有文字区一字不动（0.3564 → 0.3564）而整页多出那行水印
     （0.0028 → 0.0536）。"水印出现了"分辨不出这两者 —— 所以一个忽略 alpha 的实现会**看起来
     很好地把用户的文档糊住**。
   - 两条路都不知道 `/Rotate`：字放在页面自己那套未旋转的坐标里，跟着纸转，于是在显示为转过的
     那一页上是横着的。
   - 一次保存可以同时水印 + R6 加密（40 页 56 ms，无警告，不给口令拒开、给了口令画着字）。
   - 每页的代价**位图反而便宜**：600 页上 0.24 KB/页 对文字路 0.42 KB/页 —— 图是整份共享的一个
     对象，每页只多一个 `cm` 和一个 `Do`；文字路每页要带字体和一个图形状态。
   结论照抄进 `documenteditor.h` 开头：**量出来的是"走哪条"，不是"能不能"**，因为两条都画得下来。
   选的是位图路（`/DeviceRGB` 的图 + `/DeviceGray` 的 `/SMask`），文字覆盖免费、且**不污染文本层**。
2. **一句承诺一个函数**：`watermarkToWrite()`（要写出去的那一条 = 列表里最后那个水印，因为第二次
   要求顶掉前一次）、`watermarkPlacement()`（static：给定页的显示盒、画布尺寸和角度，算出放在哪儿、
   多大）、`pagesStillThere()`（说好的那些页里现在还剩几页）。框的小结、保存的重放、测试读回来的
   三个地方读的是同一份答案 —— 跟 §15 的 `planMove()`、§17 的 `mergesToRecord()`、§20 的
   `pieceRanges()`、§21 的 `protectionToWrite()` 同一个理由。`/Rotate` 是这条规则的受益者，也是
   这一片**比那次量多做的一件事**：probe 第 4 问的答案是"两条路都不知道纸转过"（字跟着纸转，在
   横着显示的那一页上是躺倒的），实现里把那一个四分之一圈折进了 `watermarkPlacement()`
   （`documenteditor.cpp:759`），所以横页上那行字是正的 —— 而这条不是在屏幕上看的，是测试把写出来的
   矩阵读回来钉的（`|a| < 1e-4 && d > 1`，字沿 y 走）。
3. **它是第三个惰性操作，但不是"不改任何一页"的那一类。** `rebuildLive()` 里没有它的分支（哪几页
   存在、什么顺序，它一个字都不动），撤销还是从列表里拿掉一条，磁盘上要等按下 `保存修改为`。
   不一样的地方是它的效果落在**那一页画着什么**里，而这一层没有"列表"可以对照，所以那两条核
   换成了文件本身：`orderInFile(result) == orderInFile(source)`（盖过字的那份文件页序还是原来的）
   和 `hashOfFile(source) == originalHash`（被盖的那份原件逐字节没动）。
4. **写进内容流的东西，测试按结构核**：整份文件共享**一张**位图（`ink.maskedImages == 1`）、每一页
   点名它并且真的画它（`namedButNotDrawn` 为空）、`/ca` 就是框上那个数（20% 落到 0.2）、
   转过 90° 那一页的矩阵 `|a| < 1e-4 && d > 1`（字沿 y 走，因为纸是横的）。还有一条拒话是这一片
   自己的边界：那一页的 `/Contents` 既不是流也不是数组（也就是坏的那种）时，抛出去的那句话被
   保存那一层接住（`documenteditor.cpp:1847`）—— **暂存的那份删掉、整个保存返回 false**，
   宁可什么都不写，也不写一份只画了一半承诺的文件。共享的 `/Contents` 数组在往里加之前**先抄一份**
   （两个页指向同一个间接数组时，只有一页被点名）。这条拒话**没有测试**（见本节末"仍然欠的"），
   它此刻只在目录里被探针问过。
5. **测试**：`documenteditor_smoke` 这一段 **91 处 `check()`**（73 + "水印 beside the other six" 的 18，
   后者钉的是水印和拆分/加密/删页/重排挤在同一张列表里时的样子），`documenttab_smoke` 新加
   **32 处** —— 这个框**第一次有自己的测试**，前六样的对话框只有真窗口那几遍在管（§20 的
   `many-pages` 那屏、§21 的拒话、§22 的挪位置）。为什么这里敢写"结构核"而不敢写"字形核"：
   离屏平台上 Qt 解析不出任何字体（`tests/documenteditor_smoke.cpp:612` 那句），每个字都画成同宽的空框，
   所以断言"画得像字"等于断言那台机器装了字。真字形在 `watermarkprobe` 的控制 C6 那边量过一次
   （C6 就是为此存在的：它第一版只问"有没有像素被画出来"，而空框也会画出来）。
   跟着跑出来的 35 条 note 里有两条新计时（第 8 条）。
6. **真窗口那一遍撞出两条，两条都是产品 defect，而且都是只有眼睛能撞的那种。**
   - **不透明度从 `1` 开起。** `QSpinBox` 没被 `setValue` 过就停在自己的最小值上，而这里最小值
     恰好是 1%。于是界面上演的是：一个人什么都没动，框对他说「DRAFT」，**1% 不透明度**，
     盖在 40 页上 —— 那是一个没人看得见的水印。修法不是写 20，是把数字**从结构体读**
     （`DocumentEditor::Watermark{}.opacityPercent`）：框和模型各自记一个默认值，就会在某一天的
     一次改动之后分叉。
   - **一模一样的第二次请求还亮着确定。** 模型那边 `addWatermark()` 已经会拒（"…再要一次不会
     多改什么"），框却照样把按钮点亮、把话留到点下去之后 —— 正是 §22 给加密框定下的那个形状
     没做到。现在框先问 `watermarkToWrite()`：同一个词同一个数同一批页 → 灰住 +
     "…按下不会多改什么 —— 要改就换一个词，或者动一下不透明度。"；不一样 → 亮 +
     "这会把前面记下的「机密」换掉；撤销一次回到那一个。"两句都从模型的算术读，不抄。
   - 读数（构建树 `45207494…`，`tests/fixtures/many-pages.pdf`，`XYZPDF_SETTINGS_PATH` 落在
     `H:\tmp\gui-*.ini`）：文档下 `加水印(W)…` 亮着 → 开出 `加水印`（536x399）→ 确定 `en=False` +
     `还没有说要盖什么字。` → 往第一个 Edit `SetValue('DRAFT')` → `「DRAFT」，20% 不透明度，盖在 40
     页上。` + 那张图那一句 + 按钮变 `盖这 40 页` 且亮 → 点"指定页码"、往范围框 `SetValue('2-3,7')`
     → `盖在 3 页上` / `盖这 3 页` → **真的点它** → 框没了，标题从 `many-pages.pdf — XYZPDF`
     变成 `many-pages.pdf * — XYZPDF`。**新的是"这些话是被读出来的"**：前几遍的真窗口那些轮次靠
     截图看字、靠 `{ENTER}` 点按钮（§20 末那组"点这一片学下来的手法"三条就是这么撞出来的），这一遍小结那三句、
     按钮那两个字的变化、星号那一个字符，都是从元素属性上直接读的 —— 那两条 defect（1% 和没灰住）
     正是这样才露出来的，截图上没人会去比一个 spin box 里的数字。
   - 通道的三条新事实（这三条各花了至少一轮才量出来，写清楚免得下次再花）：
     (a) 对一个会跑 `exec()` 的菜单行调 `InvokePattern.Invoke()` **永不返回**，并且把那个 client
     连接一起带崩（之后 `"Could not open the process token"`、所有 `FindAll` 超时），换 Start-Job
     换线程一样崩；能用的是**读那一行的矩形，然后真的指针点下去**（`SetCursorPos`+`mouse_event`）。
     (b) Qt 交给 UIA 的对话框是**主窗口子树里的一个 Window**，不是桌面根的子节点 —— 前五轮那句
     "框根本没开"就是拿 `RootElement.FindAll(Children)` 找出来的。
     (c) §25 里那句"点了适应整页"点的**不是菜单行**：按名字精确匹配撞到的那个元素是工具条上同名
     的 `QToolButton`。所以走 UIA 这条通道，"菜单行 → 对话框 → 框里的字 → 按下确定"这一段到
     这一节才是第一次读通（§18–§20 那几遍走的是另一条通道：真键盘真鼠标）。
   - **没测到的**：点过"确定"之后再 `Expand()` 顶层第二个菜单去读那条撤销行，两次都没匹配上
     （同一毫秒前刚数出八行顶层、第二行确实是 `编辑(E)`），第三轮收手 —— 那一行的**文字**在
     模型测试里钉着（`undoDescription()` 含"水印"和"机密"、且不含 `%`），差的是它在真窗口里渲染出来。
     同样还差着 #83 那三条。**还有一条是这一片自己的**：没走 `保存修改为` 把水印写进文件 ——
     §19 为合并走过这一段（那一轮真键盘能敲进原生框），今天这几轮不行（§21 记过合成输入被吃掉），
     所以"文件里真的带着那行字"目前只有第 4 条那批结构读数撑着。
7. **一条关于 lupdate 的洞，和 §22 那四个格子同族。** 那句"这一页的内容流不是能往上加东西的样子…"
   最初写在**自由函数**里：lupdate 看不见类，就没有 context，于是这条串**不是 unfinished 而是
   压根没有条目**；更糟的是 `QCoreApplication::translate()` 找不到条目会**回落到源文**，所以英文
   界面会安静地打出一句中文，而"数 unfinished"这种检查什么都不会说。修法是那句话由成员
   `DocumentEditor::apply()` 传进 `appendWatermarkContent()`（`tr()` 因此有了 `DocumentEditor` 这个
   context），并且**给它一条具名探针** —— 探针把"目录里没有条目"变成一条红测试。这十五处
   WatermarkDialog/DocumentEditor/MainWindow/DocumentTab 具名探针（`i18n_smoke.cpp:302`）问的就是
   这一类：包括带前导空行的那两句小结（它们是拼到小结后面的，key 必须是那个精确文本，所以写死
   在这里而不是靠遍历无人命名的标签够到）。
8. **代价（这一片自己那两条 note）**：
   ```
   40 pages, 3 of them watermarked: 15 KB out against 12 KB unwatermarked,
       i.e. 994 bytes per inked page; 8 ms
   open + 水印 on all 40 pages + write took 12 ms (23 KB out)
   ```
   一页约 1 KB、40 页全盖 12 ms，和 §11/§12 那批结构编辑同一个量级；图共享所以页数越多摊得越薄
   （probe 第 6 问在 600 页上量的是 0.24 KB/页）。
9. **这一片没碰的**：**没有重打包** —— 装着的还是 §23 那一份 `f434518e…`，水印不在包里；构建树的
   `xyzpdf.exe` 从 §25 记的 `4b161464…`（1,244,160 字节）变成 **1,279,488 字节**。真窗口那几轮跑的是
   **`45207494d4928e0af9febf82e545ef9fb2641d3c2203ac969eb894a97b78898f`** 那一份；上面第 1 条那句更正改完
   头文件注释之后再链接，成了 **`6a6b800211350716028640a9bfee0905eae33a837f02e0378c9452f82065ab44`**，
   大小一字未变 —— 也就是说**眼睛看过的那份字节和提交里躺着的那一份不是同一份**，差的是注释。下一次
   真窗口那一遍（#83 也好、走 `保存修改为` 也好）应当跑在后一份上，那一对哈希才需要重取。
   §16 那"三处哈希相同"现在欠 §24 + §25 + §26 三次，下一次重打包要一起补上。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。`documenteditor_smoke` 全文件
527 处 `check()`（其中这一片 91 处）并打印 35 条测量；`documenttab_smoke` 263 处（其中这一片 32 处）。

**仍然欠的**：#83 那一串（加密/去掉口令 + §22 那两屏），加上这一片自己的四条 ——
**点过确定之后读那条撤销行**（第 6 条末，三轮收手）、**走一遍 `保存修改为` 在屏幕上看见水印**
（第 6 条末：测试那一边已经把它写进文件又读回来了，缺的是眼睛）、**第 4 条那句"内容流不是能往上加
东西的样子"没有测试**（它只在目录里被探针问过：要钉它得先造一份 `/Contents` 是名字而不是流/数组的
fixture，那是一个能写的测试，今天没写）、以及**下一次重打包**（第 9 条）。老账不变：新文档打开时不回第一页、
两句操作连成的英文小结、英文的等待句、单页文档那一句、英文单复数那一族（`Moves 1 pages`、
`pages 40-40`）、带注释/表单的供体合并（`fixCopiedAnnotations`）、就地保存、标签拖去别的窗口时
那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。水印这一片新添的一条同类待办：位图按 200 px 画、
再按页面盒子缩放，**放大看会不会糊**没量过（要一个高分屏 + 一份只盖一页的文件）。


## 27. 加密那一遍真窗口：四句拒话都读到了，而撤销行交出一个 0 页（2026-09-27）

还的是 §21 第 9 条和 §22 第 7 条那一串（#83）。`.ts` 这一节一个字没动。**读到八屏里该读的七屏，
撞出两条 defect 并且都改了，剩一屏两次量死在同一个地方后收手**（第 7 条）。

1. **通道**：UI Automation + 真指针点矩形（`SetCursorPos`+`mouse_event`），跑在构建树的
   `xyzpdf.exe` 上，不是装着的这一份。§21 第 9 条那句"合成输入被吃掉"仍然成立，但它挡的是键盘和
   滚轮 —— 这一遍是**开框、填框、点按钮、读属性**全程没碰那两条：`ValuePattern.SetValue()` 能写
   Qt 的口令框，按钮靠矩形点，小结算 `ControlType.Text` 里最长那一个元素的 `Name`。
   这一片最后落在 **`8ad61d462f7ff9a6f8197308ceff6350e4648206b3676ec55eb05f6df1c7a4f5`**
   （1,278,976 字节）。口令只出现在脚本里两个常量上，日志、标题、菜单行三处都掩过之后才写，
   所以第 4 条那句"口令不进这些字符串"是被读出来的，不是被相信的。
2. **加密框开出来的样子**（`tests/fixtures/many-pages.pdf`，40 页）：小结那几段一次不落全读出来
   （`写出的这一份要输入口令才能打开（AES-256，也就是 PDF 里的 R6）。`、那六项只是一份请求那一段、
   R6 打不开更早阅读器那一段、原来那份文件不会改动那一段），`只改权限（不设打开口令）` 那颗是亮的，
   六个权限勾数下来 4 开 2 关（默认关的那两条排在最后），第一个框名字是 `口令`、`pw=True`，
   第二个 `再输一遍`。
3. **四条拒话，四条都是"按下之前就灰住"** —— §22 给这个框定下的形状，第一次在屏幕上逐条看见：
   - 两次输入不一样 → `两次输入的口令不一样。` + `加密` **en=False**；
   - 从没设过口令的文件上点"去掉口令" → `正在打开的这份文件没有口令，也没有写下任何权限限制，
     所以这里没有东西可去掉。` + `去掉口令` **en=False**；
   - 已经记下一次去掉口令、再要一次 → `前面已经记下了一次去掉口令，写出的那一份本来就不带口令，
     再要一次不会多去掉什么。要退回去用撤销。` + 灰 + 两个框 `en=False`；
   - 一模一样的第二次加密（同口令同勾） → `这跟前面已经记下的一条一模一样，按下不会多改什么 ——
     要改就改一个勾，或者换一个口令。` + `加密` **en=False**。
   另外读到一句本来只在测试里出现的话：`这会把前面记下的那一条顶掉；撤销一次回到那一个。`
4. **接受之后**：框没了，标题 `many-pages.pdf — XYZPDF` → `many-pages.pdf * — XYZPDF`，文档菜单里
   `撤销 加上打开口令（AES-256）(U)` 亮着、`重做(O)` 灰着，点那一行星号自己消失。**口令一个字都不在
   标题、菜单行或日志里** —— §21 第 6 条那条承诺这一遍是在屏幕上核的。顺带把窗口模态也定了：
   框开着时**真的指针点**菜单栏，弹出 0 行（`ExpandCollapse.Expand()` 走的是 Qt 的 accessible
   action，它绕不过模态判断，所以那一条测不出这件事）。
5. **defect 一：那两个口令框在名字上分不开。** 不挂 buddy 时两个框都回答组标题 `打开口令`，
   于是**只靠名字工作的那位**（读屏）拿到两个一模一样的名字 —— 而"哪个框是第一遍、哪个是再输一遍"
   恰好是这两个框唯一的区别。框上本来就有两个 `QLabel`，只是没挂上去：`setBuddy()` 两行，
   测试用 `QAccessible::queryAccessibleInterface(edit)->text(QAccessible::Name)` 钉（offscreen 平台
   跑得动，所以这条进门禁而不是进真窗口）。一行对照：把两行 `setBuddy` 注释掉 →
   `FAIL: the two fields are named apart by whatever only reads names: [打开口令] [打开口令]`，25/1。
6. **defect 二：撤销那条水印说它在 0 页上盖字，而同一屏上框说的是 40 页。**
   机理就在 `describeOperation()` 开头那一句 `const int count = op.pages.size();` —— 六种结构操作都
   往 `op.pages` 里记页，第七样不记，它的页在 `op.watermark.pages`。修的是**读哪一个列表**，不是
   再算一遍：`addWatermark()` 已经把筛剩下的那一批记进操作（`asked.pages = sortedFrom(named)`，用的
   正是 `pagesStillThere()` 那个谓词），所以行里的数就是按下那一刻框上说的数。这一句要紧，因为
   另一种写法（撤销行现场调 `pagesStillThere()`）会让一行菜单文字跟着后面的删页改动变 —— 那条
   列表是"当时要了什么"的记录，不是一个读数点。**跟 §15/§17/§20/§21/§24/§26 同一个理由：一句承诺
   只由一个地方算**，这次是"这些页要盖字"这句话，框的小结、保存的重放、菜单的行三处读同一份答案。
   一对读数（同一脚本、同一 fixture、同一通道，差的是字节）：未修那一份给
   `撤销 加水印「DRAFT」（0 页）(U)`，重新构建之后给 `撤销 加水印「DRAFT」（40 页）(U)`，而两次的框上
   都写着 `「DRAFT」，20% 不透明度，盖在 40 页上。`。前一份的哈希当时没记（它就是门禁里那条红的
   那一次构建，红字里带着 `（0 页）`），后一份是第 1 条那个 `8ad61d46…`。门禁那条对照是
   `FAIL: and it counts the pages it actually names: 加水印「机密」（0 页）`，同样只红一条，25/1。
7. **§22 那两屏：一屏读到，一屏收手。**
   - 英文的第三方那张表：`appearance/language=en` 写在启动之前的那份 ini 里（§16 走过注册表那条，
     这次不用动用户设置）。八行顶层菜单 `cjk=0`，`Help → Third-party licences` 点开 820x560 的框，
     四行 `Qt / PDFium / QPDF / Third-party libraries inside Qt`，小结和详情两段全读出来，
     **逐行点过四行、每一次整个框里 CJK 字符数都是 0**。这条的问法换了：不比句子，数汉字。
     因为比句子得先猜对句子，而这一串里有两轮各折在一次猜错的 needle 上（第 8 条）。
   - 合并框在拆分挂着时那一屏**还是没读到**，而这次知道为什么读不到：原生选择框是主窗口子树里的一个
     Window（`[要合并进来的 PDF] 1200x720`），只带 `Window` 和 `Transform` 两个 pattern；子树里数出
     65 个 Edit，全是列表列头（名称/修改日期/类型/大小 ×16）和搜索框，**那个"文件名"框根本不作为
     Edit 出现**。改走键盘：`SendKeys` 打路径会弹出自动完成（一个 756x61 的匿名顶层窗口），
     先 `{ESC}` 再 `{ENTER}` 连点三次，框都不关。§23 那次量的"原生框经 MSAA 桥不带 pattern"是同一
     件事的另一个面。这一屏仍然只有 #84 那条离屏测试撑着（`addPaths()` 直接把供体喂进列表 +
     `splitInto(15)` + 按钮灰 + 那句模型自己的话），差别只在没人眼看过。**第三轮之后收手**，
     不再为它花第四轮。
8. **两条读数纪律，各花一轮才学到，写下来免得下次再花。**
   - `DialogOf` 里我写的 `Height -gt 300` 把 560x**300** 的拆分框筛没了，于是连读两轮都没看见它
     （一轮报 `no split dialog`，一轮把还开着的加密框当成了拆分框）；`Wide` 里那句"宽度 > 900 就是主
     窗口"在 1200 宽的原生选择框上会认错主窗口。现在脚本先打印它看到的每一个窗口和它的矩形
     （`Wins`、`Picker`），再决定用什么筛 —— **筛子是自己的，不是屏幕的**。
   - §26 末那句"点过确定之后读那条撤销行，两次都没匹配上，第三轮收手"**是错的**，而错得有价值：
     撤销行住在**文档**菜单，不在我数着的第二个菜单（编辑）；而我那两条 needle 的码点打错过
     （0x8F83 是"较"不是"辑"，0x5E95 是"底"不是"销"）。通道一直是通的，第 4、6 条那两行菜单文字
     这一遍都是从真窗口读的。为什么这条要写进文档：§26 据此写下了"那一行的文字只有测试钉着，差的是
     它在真窗口里渲染出来"，而那个空档里正躺着第 6 条那个 0 页 —— **一句读不到的结论，代价是把一条
     defect 留在了没人看过的字段上**。
9. **这一片没碰的**：**没有重打包**，装着的还是 §23 那一份 `f434518e…`（六样），§16 那"三处哈希
   相同"现在欠 §24 + §25 + §26 + §27 **四次**；没有走 `保存修改为`；目录、词条、探针都没动。

gate：`python tools/build_all_tests.py` → **26 passed, 0 failed**。这一片新增 **2 处 `check()`**
（`documenttab_smoke` 的名字分开、`documenteditor_smoke` 的行内页数），各自跑过一行对照、各自只红
那一条；改的产品代码两处共 **3 行**（两行 `setBuddy` + 撤销行读哪个列表）。

**仍然欠的**：#83 剩下的那一屏（合并框挂着拆分，第 7 条：要一条能驱动原生选择框的通道，两条已量死）；
水印那一半的四条老账不变 —— 走一遍 `保存修改为` 在屏幕上看见水印、`/Contents` 既不是流也不是数组
那句拒话没有测试、200 px 位图在高分屏放大下糊不糊、以及下一次重打包。再添两条这一遍看见的：英文
菜单栏里 `Edit(E)`、`Document(D)`、`Window(N)` 三行带括号而 `File`、`View`、`Go`、`Help` 四行不带
（中文那一套七行都带 `(&X)`，所以差的是英文目录里助记符写法不统一 —— 没测 Alt+ 字母通不通），
和 `去掉口令` 那一屏上两个口令框都是 `en=False`（读到的是：切到"去掉口令"这一侧，整组框跟着那个
单选钮一起disable；这一条只是屏幕上的样子，没有测试钉它）。
老账不变：新文档打开时不回第一页、两句操作连成的英文小结、英文的等待句、单页文档那一句、英文单复数
（`Moves 1 pages`、`pages 40-40`）、带注释/表单的供体合并（`fixCopiedAnnotations`）、就地保存、
标签拖去别的窗口时那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。

## 28. 走了那一趟 `保存修改为`：水印在屏幕上有了墨，而它顺手给出一个"默认视图就在放大那张位图"的数（2026-09-27）

还的是 §26 末列下的那两屏（走一遍另存为、和 200 px 画布在放大下糊不糊），也就是 #98。
**这一片一行产品代码都没改，一次门禁都没跑**（最后一次仍然是 §27 的 26 passed / 0 failed），`.ts`、
探针、界面都没动；落在仓库里的改动只有"删掉两份探针自己留下的垃圾文件"。

1. **另存为那一条路走通了。** 还是 §27 那条通道（UIA 读属性 + 真指针点矩形），跑构建树那一份
   `8ad61d46…`。`加水印`（`DRAFT`，其余按默认）→ 文档 → `保存修改为…` → 原生另存框里给一个新路径 →
   写出的那一份在**第二个标签**里打开：灰的 DRAFT 压在页面自己的黑字上，状态条那句是
   `已写成一个新文件（40 页）：加水印「DRAFT」（40 页）` —— §27 第 6 条修的那个页数，这一遍是它在屏幕上
   第二次被读到（第一次是菜单里那行撤销）。
2. **原生另存框这一次填进去了，而 §27 第 7 条那句"那个'文件名'框根本不作为 Edit 出现"是错的。**
   它作为 Edit 出现了，我发错了对象：框里数出 65 个 Edit，`id=1001` 那个是**搜索框**，真正的文件名框是
   **跟 `ComboBoxEx32` 容器共享控件 id（41477）的那一个**。前两轮把 `WM_SETTEXT` 发到搜索框上，于是框用它
   自己种下的默认名去写，撞上"文件已存在 → 确认另存为"，两轮都报 `file written: False`。换成按 id 认框 +
   `ValuePattern.SetValue()` 就一次写成。所以 §23/§27 那句"原生框不带 pattern"要收窄：**它的顶层 Window 不带
   （只有 `Window` 和 `Transform`），它下面的 Edit 带 `ValuePattern`**；而 `WM_SETTEXT` 在那个框上既不回读也不
   生效（Qt 自己的线编辑吃 `SetValue`，原生那份不吃 `WM_SETTEXT`）—— 三轮才走通，代价见第 3 条。
3. **那两轮撞的默认名，落点在仓库里**：`xyzpdf/tests/fixtures/many-pages 已修改.pdf`。这一串从此都在
   `H:\tmp\wm-save-scratch\src\` 的一份副本上跑，`many-pages.pdf`（12,539 字节）整串跑完 SHA-256
   `6eaed302…` 前后相同 —— 程序仍然一个字都没往它身上写。仓库里那一份是探针留下的（35,181 字节，
   跟程序写出的那份同尺寸不同字节，就是 `/ID` 和时间戳之差），删了；同时删的还有
   `xyzpdf/H<私用区字符>tmpwmgeom-out.txt` —— python 里 `"H:\tmp\..."` 的 `\t` 被吃成一个制表符，
   于是往仓库里写出一个怪名字的文件。**这条是门禁旁边的坑**：脚本里的 Windows 路径一律 raw string 或用 `/`。
4. **"改前后同一区域"从此是一个像素数，不是一句描述。** 同一个窗口、同一个矩形、同一个缩放，未盖那份和
   写出那份各截一次：全窗口 **23,895 个像素不同**，其中 22,476 个在页面上（余下一千多个在窗口上方的标签条
   一带，两份文件的名字本来就不同）。挑最密的一块 110×80：**未盖那份 0 个非白像素，盖了那份 6,607 个**，
   灰全部落在 200–249 这一档、没有一个比 150 更黑（所以它是水印的墨，不是页面自己的字）。顺带对上一件事：
   平台灰度是 **204 = 255×(1−0.2)**，而文件里那个 `/ca 0.2` —— 框上那栏不透明度第一次在屏幕上有一个数可以对。
   而**中间那条 700×420 的裁块两边逐字节相同**（哈希都是 `443c546a…`），因为墨在窗口右下之外：
   **裁块相同不等于屏幕相同**，这条正是第 5 条那个"抬窗口"非做不可的理由。
5. **UIA 报告的是矩形，截屏要的是最上面那一个。** 前几轮的基线图 `paper x=-1..-1` 连试十次不出纸，把图
   本身读出来才发现截的是**另一个应用的窗口** —— 矩形是真的，窗口在后面。`SetForegroundWindow` 从这里抬不动
   xyzpdf，**点一下标题栏抬得动**（脚本里那个 `Raise`）。这条不改任何产品代码，但它决定第 4 条那些数可不可信：
   修好之前那一份"盖了水印前后屏幕没变"的差是 99% 像素相同，**那是假的**。
6. **糊不糊的那个数**（§26 末问的就是这个）：
   - 位图自己：`/Width 729 /Height 578`，画在 535.5×424.58 pt 的框上，两轴都是 **1.3613 像素/pt**
     （所以它是一张**预先转好 30° 的位图等比贴上去**的，不是贴完再转）。
   - 屏幕：这台机器上窗口 1240×860，页宽 595 pt。`实际大小` 那一档页占 606 px → **1.018 像素/pt**；
     `适应宽度` → 1224 px → **2.057**；`适应整页`（83%）→ 0.845。**打开时的默认就是适应宽度**
     （`src/view/zoomcontroller.h:81` 和 `documenttab.cpp:148` 两处都写着 `FitWidth`），不用谁去放大。
   - 倍率 = 屏幕 ÷ 位图：**默认那一屏 1.51 倍**，`实际大小` 0.75 倍，`适应整页` 0.62 倍，打平点在
     **≈134%**。它只由 `视口宽 ÷ 页宽` 决定，跟"高分屏"没关系 —— 窗口拉到 1600 px 宽，默认那一屏就是
     1.96 倍。**所以这不是一个高级用户才会踩到的档，是每个人打开一份 A4 就站在的那一档。**
   - 边上摊开多少：默认那一屏上量水印笔画"灰到白之间"的游程，**734 段，中位 3、均值 3.01、最长 8 像素**
     （1.51 倍把源图自带的那 2 px 反锯齿摊开，正是这个量级）；4 倍放大看的那一块里对角边是清楚的阶梯。
     数字和图都交出去判，我不替谁下"糊"这个字。
   - 一句限定：`实际大小` 和 `适应整页` 那两档**没有**同样的干净数 —— 在那里按灰档找水印会把页面自己的
     反锯齿文字一起找进来（100% 那档量出"游程最长 585 像素"就是这件事，那不是水印的边）。要在那两档上也量，
     得先有一个不靠灰度的水印定位法。
7. **成本这一遍换了一个字，所以不能跟 §26 那句 23 KB 直接比。** 那一句写的是 `机密`（两个汉字，画布窄），
   这一遍是 `DRAFT`（五个拉丁字母，画布 729 px）。12,539 → 35,181 字节，拆开是：**整份共享的那一张图
   和它的 SMask 一共 12,567 字节**（`/Length 11319` + `/Length 1248`，两条流），剩下约 10 KB 是 40 页各自
   多出来的 `/Resources` 条目和 58→74 字节的内容流。所以"一页约 1 KB"那句要这样读：盖三页时共享的那一张
   摊在三页上（§26 那个 994 字节/页），盖 40 页时它摊在 40 页上（这一遍 566 字节/页）。**图是按字数长的**，
   这一条以前只在代码里，现在有了两个尺寸的读数。
8. **这一片没碰的**：没重打包（装着的还是 §23 那一份 `f434518e…`，六样），§16 那"三处哈希相同"现在欠
   §24 + §25 + §26 + §27 + §28 **五次**；没提交；`/Contents` 那句拒话仍然没有测试；合并框挂着拆分那一屏
   仍然只有离屏测试撑着。三份探针脚本还在 `H:\tmp\`（`uia-wm14.ps1`、`uia-zoom3.ps1`、`uia-flat.ps1`），
   "搬进 `tools/`"那条老账不变，而这一遍多一样值得搬的：第 4 条那个"同一矩形、两份文件、相减"的问法 ——
   它是这一串里第一次把"屏幕上有了墨"变成一个数。

gate：**这一片没跑**（没有可改的产品代码；跑它只会重读 §27 那一份字节）。工作树里仍然是 §27 那六个文件，
外加两份探针垃圾被删。

**仍然欠的**：整张水印在 1.51 倍下的样子仍然只在人眼里 —— 默认视图把它切掉一半，而 UIA 那根滚动条
`Maximum` 读回来是空的、`SetValue()` 不动它（这一串里两轮栽在这根条上），所以"滚到水印再截"这条通道还没通；
第 6 条末那两档的干净量法；`/Contents` 那句拒话；合并框那一屏；下一次重打包；§27 末那两条（英文菜单助记符
写法不统一、`去掉口令` 那一屏两个框一起灰）；老账不变：新文档打开时不回第一页、两句操作连成的英文小结、
英文的等待句、单页文档那一句、英文单复数（`Moves 1 pages`、`pages 40-40`）、带注释/表单的供体合并
（`fixCopiedAnnotations`）、就地保存、标签拖去别的窗口时那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。

## 29. 那一屏量到了，而它顺手把 §28 关于原生框文件名的那句改回来（2026-09-27）

还的是 §22/#84 立起来、§27 第三轮收手没读到的那一屏（"合并框在拆分挂着时"），也就是 §28 末"仍然欠的"
里点名的那一条。**这一片一行产品代码都没改，一次门禁都没跑**（最后一次仍然是 §27 的 26 passed / 0 failed），
`.ts`、探针、界面都没动；它全部的价值是两遍独立读数 + 两张截图 + 一条通道结论的更正。

1. **那一屏长这样。** 列表里躺着 `donor.pdf | 2 页 | 1-2`，小结是
   `这份文档已经说了要拆成每 20 页一份。先撤销那一步，再把文件合并进来。` 下面跟着那句一直保留的
   `原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。`，而
   `合并这些文件` 是灰的（`IsEnabled=False`，截图上也是灰字）。**§22 第 2 条那句"替换而不是追加"
   从此有屏幕上的对照**：`1 个文件一共取 2 页，合到最后面，按列表的顺序连着排在第 41-42 页。`
   一个字都不剩，不是被压在下面。那个 20 是 `splitPieceSize()` 的读数 —— 拆分框自己那一屏写的是
   "每份最多 20 页"，同一个数从同一个函数来，不是对话框里抄的第二份（§22 第 1 条的理由，这一遍是它
   在屏幕上第二次被读到）。两遍（tag m5、m6）逐字相同，中间还夹了一次整轮重来。
2. **正对照：同一趟、同一个框、同一个尺寸，只把拆分那一步拿掉。** 小结变成那句算术（外加
   `合并进来的页没有屏幕上的页码：…`），按钮亮。两份截图都是 560x544，表里同一行 `donor.pdf`，
   差的只有"挂没挂拆分" —— 所以这一对读数不是两个时机下的两件事。
3. **一件顺序上的事实**（新读到的，不是一条 bug 断言）：列表还空着的时候小结说的是
   `还没有加上要合并进来的文件。`，不是那句拆分拒绝 —— `describeOperation()` 里 `listRows == 0` 排在
   `splitting > 0` 前面。按钮两种情况下都灰（`listRows > 0` 那一项），所以屏幕上没有"先答应了什么
   再收回去"。记下来是因为它不在任何测试里，而它决定这句措辞要不要动：现在的形状是"先说你还没放文件
   进来"，放进来之后才说"放了也不能合"。
4. **原生框这一遍是靠"点列表里那一行"过去的，文件名框一个字都没填。** 走法：真实点 `添加文件…` →
   在框的文件夹列表里点中 `donor.pdf` 那一行（`ControlType.ListItem`，矩形 598x24）→ 给控件 id=1
   那个按钮发 `BM_CLICK`（打开框上它的文字是 `打开(&O)`，另存框上是 `保存(&S)`，所以按 id 认不按文字认）
   → 接受信号不看那个框、看**自己那个表**：`ControlType.DataItem|donor.pdf` 出现即成。前提是先把
   `XYZPDF_SETTINGS_PATH` 指到 scratch ini 并预写 `[files] lastOpenDirectory=…`（`src/core/appsettings.h`
   读它），让框开在有 donor 的那个文件夹里 —— 顺带这一遍一个字都没碰真注册表。
5. **§28 第 2 条关于文件名框的话是反的，这一遍改回来。** 三条读数：
   **(a)** "换成 `ValuePattern.SetValue()` 就一次写成"重不出来 —— 两份框（打开框、另存框）里那两个
   `Edit` 按 hwnd 绑到的元素都是 `ControlType.Pane`，打开框那两个 `GetSupportedPatterns()` 是空的，
   `ValuePattern` 在两份框的两个 Edit 上都不存在。**没有那个 pattern，所以从来没有"一次写成"。**
   **(b)** 另存框里 `WM_GETTEXT` 从 **`id=1001`** 读回种子名 `many-pages 已修改.pdf`，而 §28 认定的那一个
   （`id=41477`，跟 `ComboBoxEx32` 共享 id 的）读回**空** —— 也就是说装着文件名的那个框恰恰是 §28 说
   "那是搜索框"的一个。**(c)** §28 引的那三张日志（wm1/wm2/wm3）里目标路径一律 `file written: False`，
   而它自己第 3 条记下的那个 litter 名字正是框种下的默认名 —— 两边合起来读得出：**那次写出用的是默认名，
   改名字这一半从来没做到过**。所以 §28 第 1 条"另存为这条路走通了"要收窄成"按下保存、由框自己种下的
   名字写通了"。（§27 第 7 条那句"那个框根本不作为 Edit 出现"仍然是错的 —— 它在两份框里都作为 Edit 出现，
   §28 这一点没弄反。）
6. **一条通道上的坑，第一轮整轮丢在它上面**：拿 `FindType(picker, ControlType.Edit)` 去问原生框，回来的是
   17 个列表单元格加一个搜索框（`System.ItemNameDisplay` / `System.DateModified` / `SearchEditBox` 这些
   AutomationId），往单元格上 `SetValue` 先抛"值为只读"、再抛 `Could not open the process token`，然后
   整趟卡死四分钟，一次都没碰到文件名框。**原生那份的子元素要按 hwnd 绑（`FromHandle`），不要按树走。**
   还有两条这次踩实的小事实：`RectS` 那种"整数开头再拼字符串"在 PowerShell 里是算术（`(int)+'x'` 直接抛
   转换错），以及脚本里出现一个 `§` 就破坏了"`.ps1` 必须纯 ASCII"那条（UTF-8 的 `§` 首字节 0xC2 在 GBK 里
   是前导字节，会吃掉下一个字节）。
7. **没动的**：产品代码零改动、门禁没跑（仍 26/0，最后一次是 §27）、**没重打包**（§16 那"三处相同"现在欠
   §24/§25/§26/§27/§28 五次，这一片不改任何负载所以仍然是五次）、没提交。脚本还在 `H:\tmp\uia-merge1.ps1`
   （`gui-verify/*.ps1` 搬进 `tools/` 那条老账不变），跑在 `H:\tmp\wm-save-scratch\src\` 的副本上，
   `many-pages.pdf` 整串跑完 SHA-256 `6eaed302…` 前后相同，`donor.pdf`（946 字节）`bdb1945d…` 相同。

**仍然欠的**：**另存框改名字这一半**（需要一个真能写进 `id=1001` 的通道 —— 真实键盘，或者让默认名落在
想要的位置上；这一条把 §28 第 1 条从"走通了"降级成"走通了一半"）；整张水印在 1.51 倍下的样子（还在那根
读不到 `Maximum` 的滚动条上）；§28 第 6 条末那两档的干净量法；`/Contents` 那句拒话；§27 末那两条（英文菜单
助记符写法不统一、`去掉口令` 那一屏两个框一起灰）；下一次重打包。老账不变：新文档打开时不回第一页、
两句操作连成的英文小结、英文的等待句、单页文档那一句、英文单复数（`Moves 1 pages`、`pages 40-40`）、
带注释/表单的供体合并（`fixCopiedAnnotations`）、就地保存、标签拖去别的窗口时那句措辞、
`gui-verify/*.ps1` 搬进 `tools/`。**已经还上的**：合并框那一屏（这一节），划掉。

## 30. `/Contents` 那句拒话终于有人问了，而它顺带问了另一头：一页没有内容流时章该落在哪（2026-09-27）

1. **§26 记下的是"这句拒话只有 i18n 探针"，而探针能证的东西跟这条测试差得很远。** 探针证的是
   `这一页的内容流不是能往上加东西的样子，所以水印没有画上去。` 进了目录并且翻成了英文
   （`tests/i18n_smoke.cpp:326` 那条具名探针）；它不能证的代码事实是 `appendWatermarkContent()`
   最后那个 `throw` 有没有一次被执行过。一个走不到的分支加一句
   翻好了的话，可以在门禁里一起绿着 —— 这就是这条欠账留着的原因，不是"缺一条测试"这种形式上的说法。

2. **形状在测试里造，不新增 fixture**：`writeContentsShape(source, dest, page, shape)` 用 QPDF 把某一页的
   `/Contents` 改写成指定的形状再写盘。跟 §17 的供体、§26 的共享页复制同一个理由（那些形状要能被这一条
   读出来，而且 fixture 目录里多一个文件会被别的测试 glob 到）。挑这两种是因为它们正是"别人生产的文件"
   的两端：**整个缺席**是合法的且很常见（空白页、墨全在自己注释里的页），而**直接写成一串字面量**不合法
   （标准只允许流，或流的数组）但真在野出现 —— 有生成器把算子直接写进页字典。前者章必须落得上去，
   后者要拒。

3. **拒话那一半钉的不是"抛了"，是抛了之后的六件事**：`saveAs` 返回 false、`error` 里带着那句
   （前缀是 `写出文件时出错，原来的文件没有改动。`，所以断言查的是"内容流"这个子串而不是整句）、
   目的地**没有文件**、scratch 目录里 `xyzpdf-save-*.pdf` 一个不剩、被打开的那份文件逐字节没动、
   以及**那条修改还挂在列表上**（`isDirty()` 且撤销行仍写着那个水印）。最后一条是"拒绝而不是跳过"这个
   决定的直接后果：什么都没写到磁盘，撤销就必须还是一个真选项 —— 否则菜单那一行描述的是一个已经存在的
   文件，而 §20 为同一件事已经改过一次状态条的措辞。

4. **正对照点在同一个文件的另一页**：不挂坏形状那一页、只点名健康的第一页，保存要写得出去，写出的文件里
   有墨的页正好是 `{0}`。没有这一条，上面那六件事读起来是"这份 scratch 文件加水印就是失败"，而一个坏在
   别处的 scratch 文件产出完全一样的结果。

5. **另一头那条只钉一句：`drawn == {0, 1}`**，即一页没有 `/Contents` 时章要落在它上面，跟旁边那页一起。
   为什么单独钉：一个"这种页就跳过吧"的捷径在这一页上**没有任何可见后果** —— 文件照样打开、页数照样、
   那一页本来就没字。而 §26 第一遍探针量的正是同一类"说不出来的少"（对话框说盖在 2 页上，文件里只有一页
   有墨），所以这里宁可多一条只查一个向量的检查。附带钉住 `namedButNotDrawn` 为空（不许只在资源里挂个名字
   而不去 draw）、两个页共享同一张位图、页数没变。

6. **两处对照各钉红几条，是单独数出来的**（门禁只转第一条 FAIL，§12 就记过这件事："1 failed" 数的是
   测试不是断言）。把最后那个 `throw` 改成"直接 return（跳过这一页）"，红的是三条，正是那六件事里跟
   "拒"本身有关的那三条：`the save refuses rather than appending to content it cannot parse`、
   `and the sentence says which judgement it made`（改完 `error` 是空的）、`and no file is at the destination`
   —— 后面三条（暂存名没留下、修改还挂着、原件没动）在"其实写成功了"的路径上不会红，因为那三条说的
   是"什么都没发生"，而跳过之后**确实**什么都没发生。所以那三条钉的不是这条分支，是别的东西；真正咬住
   分支的是前三条，这一点原来没算过。把 `isNull` 那条改成同样的跳过，红的是
   `the page with no content of its own is in the list of pages that draw: 1`（回来的 `drawn` 只剩健康的
   那一页）。两处都还原了：`git diff` 里产品代码一个文件都没有，只有测试文件 +127 行。

7. **这一片没有的**：产品代码零改动、没有新词条（那句拒话 §26 就在目录里，这一片只是走到它）、
   没重打包。门禁 **26 passed / 0 failed**，新增 22 个 `check()` 站点全在 `documenteditor_smoke`。
   顺带一条跟 §16 有关的读数，而且是这一节自己撞出来的：**同一份源码在这一串里连建两次，两个哈希** ——
   第一次绿跑完是 `22fa6555…`，中间为对照改一行、还原、再绿一次是 `d489f23f…`，两次都是
   1,278,976 字节而产品代码逐字节等于 HEAD（`git diff` 里只有两份 docs 和那一个测试文件）。
   跟 §27 那两遍跑的 `8ad61d46…` 也是同一回事。**是哪一段字节变了没去核**（核它要留两份字节对照），
   能说的是：哈希会因为跟行为无关的原因漂，所以 §16 那条规则要证的从来不是"哈希没变过"，
   而是"测过的那份字节 == 装着的那份字节"，而这句话只有在重打包那一次才问得动。
   装着的仍是 §23 的 `f434518e…`，欠的五次（§24–§28）不变。

**仍然欠的**：§29 顺带量到但没钉的那条顺序事实（合并框的空列表判定排在"拆分挂着"之前，所以两样同时成立时
那句拆分拒话根本不出现 —— 按钮两种情况都灰，于是没有承诺被收回，但这条路径今天没人问）；整张水印在 1.51 倍
下的样子；§28 第 6 条末那两档的干净量法；另存框改名字那一半；§27 末那两条（英文菜单助记符写法不统一、
`去掉口令` 那一屏两个框一起灰）；下一次重打包。老账不变：新文档打开时不回第一页、两句操作连成的英文小结、
英文的等待句、单页文档那一句、英文单复数（`Moves 1 pages`、`pages 40-40`）、带注释/表单的供体合并
（`fixCopiedAnnotations`）、就地保存、标签拖去别的窗口时那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。
**已经还上的**：`/Contents` 那句拒话（这一节第 3 条），划掉；顺带把同一函数另一头的空页也钉了（第 5 条）。

## 31. 一页不是一个范围：这条规则有四个出口，两个已经写对了两次，两个从没写过（2026-09-27）

1. **这条老账的形状不是"一个 typo"。** `拆分` 的文件名早就对了（HEAD 里就有 `a one-page piece says
   21, not 21-21` 那条检查），拆分框的小结也是对的 —— 但它俩是**各自实现了一遍同一条规则**
   （`piecePath()` 里一份、`SplitPagesDialog` 里一个 lambda），两份都对、两份都没被单独问过；
   而另外两个出口**根本没实现它**：`移动页面` 对单页打"移完它们连着排在第 5-5 页"，
   `合并文件进来` 打"连着排在第 40-40 页"。所以"只钉住一个出口"是不够的，规则收在
   `DocumentEditor::pageSpan(first, last)`（0 基进、1 基出，`first == last` 就只有一个数），
   四个出口都读它 —— 跟 §15 的 `planMove()`、§17 的 `mergesToRecord()`、§20 的 `pieceRanges()`、
   §24 的 `stagedPath()`、§25 的 `updateCurrentPageFromViewport()` 同一个理由，只是这次的证据更强：
   **自带两份实现，恰好证明这确实是一句需要一处说出来的承诺，而不是顺手写的两段。**
   后来在屏幕上又撞出**第五个出口**（第 4 条），所以这一片实际收成的是五处。

2. **数改的不只是那个数，还有它前后的词。** 单页那一支里"它们连着"和英文的 "they end up together at
   pages" 跟着一起错。所以那两句各拆成两条措辞，分支条件就是页数 —— 跟 `拆分` 的状态行按 `fileCount`
   分两句（`mainwindow.cpp:1420`）同一个理由：**一个会改措辞的数，要在句子外分叉，不能在句子里当参数**。
   单数那条把"1"直接写进中文（`把 1 页移到%1：移完它就排在第 %2 页。`），不是偷懒：那一支的条件就是
   `pagesMoved == 1`，把 1 藏回变量等于允许后来人往它传 2。

3. **合并那句的单数支里 `%1 个文件` 敢写 "file"（一个数词不带 s），是算出来的**：列表里每一行至少贡献
   一页（范围空着的那一行在上面 `还没有说出要合入哪几页` 那一支就拦住了），所以一共取 1 页 ⇒ 恰好一个文件。
   这条推理写进了代码旁边，因为它是那句英文的唯一依据；测试用一页的供体（`rotated.pdf`，`/Count 1`）
   跑的就是这一条。

4. **屏幕上撞出第五个出口，而它是这一片唯一"看代码看不出来"的一条。** 离屏那三问跑完之后，同一套
   UIA 通道（§29 那条：点列表里那一行 + 给控件 `id=1` 发 `BM_CLICK`，判据看自己那张表）拿**一页的供体**
   在真窗口里又走了一遍，合并框的小结读回
   `1 个文件一共取 1 页，合到 最后面，按列表的顺序排在第 41 页。`、按钮亮 —— 单数那一支从此有屏幕上的读数。
   同一帧里表里那一栏写的是 **`1-1`**：`addPaths()` 给新行种的范围格是 `"1-%1"`，一页的文件就种出 `1-1`。
   那是同一条规则的第五处，也是唯一一处**输入**：它解析出来跟 `1` 一样，所以下游什么都发现不了，
   只有读者看得见。现在它也读 `pageSpan()`，屏幕上那一格跟着变成 `1`（同一遍第二次跑，截图 `mg-m11-2-filled.png`）。
   值得记的是这一条怎么来的：不是查代码查到的（`grep` 一轮只给出那四处），是**看屏幕**看到的。

5. **落点的算术没动。** 两个框的位置仍然出自 `planMove()` / `planMerge()`（§15、§17），这一片改的只是
   把它们的输出**翻译成文字**的那一处：合并是 `pageSpan(landsAt, landsAt + added - 1)`，移动是
   `pageSpan(plan.landsAt, plan.landsAt + plan.pagesMoved - 1)`。量出来的落点全部写进断言：40 页的文档里
   合并一页到末尾 = 排在第 41 页，合并两页 = 连着排在第 41-42 页，移动第 7 页到末尾 = 它就排在第 40 页，
   移动 7,9 两页 = 连着排在第 39-40 页。

6. **顺手补上的是两个框自己：`MovePagesDialog` 和 `SplitPagesDialog` 的小结第一次被离屏读过。**
   此前它们只在 i18n 目录里（`连着排在第` 那两条探针），没有任何测试构造过这两个框 —— 一句界面话
   有词条不等于有人在读它，这是 §22 那条"探针问不到的东西"的另一种形态：**问的是目录，不是屏幕**。
   拆分框那一问挑的是"每 1 页一份"，因为它的列表只列到六份再省略号，那种形状下**每一个数都是单页**，
   横杠出现在任何一处都是这条 bug。

7. **两刀对照各红各的。** `pageSpan` 改成永远带横杠：两个测试红，一处是 §20 那条老检查，一处是新那句，
   而它的失败消息把缺陷直接打在屏幕上 —— `排在第 41-41 页`。只拿掉合并框的单数支（`added == 1` 改成
   `false`）：一个测试红（`one page is not 连着`），而"排在第 41 页"那一条**照旧绿**。数和对措辞是两条
   独立的钉，一条松了另一条还站着；合成一条检查就看不出这一点。两处都还原，门禁 **26 passed / 0 failed**。

8. **目录 537 → 539 条，具名探针 102 → 104**（两句各变两句），0 unfinished。这一片的 i18n 改动全部是
   跟着源文走的，一条都不是"发现缺翻译" —— 这里记一条 §22 那件事的**另一个方向**：
   **源文改了名字，旧探针会红，而红的是探针不是产品**。改源文的那一片必须同一片改探针，否则门禁给出的
   是一条不存在的缺陷。另外这一片自己撞出一条测试侧的坑（不是产品的）：`QLatin1Char('、')` 编得过、
   跑得错 —— U+3001 不在 Latin-1 里，UTF-8 源码下它是三个字节而 `QLatin1Char` 只取到一个，于是那条
   "列表切成 8 段"的检查第一次跑就红，红的还是那句里最不该错的分隔符。**按码点问分隔符
   （`QChar(0x3001)`），别按 Latin-1。**

9. **这一片改了进包的字节，而它自己的两次读数就不一致。** 中途量过一次 `aa7888b7…`、**1,278,976 字节 ——
   跟 §27/§30 那一串相同**，那时 `pageSpan()` 和两条新句子已经在里面了；跑完 `addPaths()` 那一处（第五个
   出口）再量，是 `ad1c6f63…`、**1,280,512 字节**。所以"大小没变"这句话在这一片里既对过也错过，
   **它不是负载有没有变的信号** —— 能当信号的只有下一次重打包那一次的三处对照。装着的仍是 §23 的
   `f434518e…`（1,244,160），于是 §16 那"三处相同"现在欠 §24–§28 五次，**而这一片自己也在等那一次作证** ——
   它跟 §24/§25 同一类：改了负载的那几片，屏幕上的话要靠重打包才真的装上。屏幕上这一遍跑的是
   **开发树那一份**（§29 那条通道，两页与一页两种供体各一遍，截图 `mg-m10-2-filled.png` /
   `mg-m11-2-filled.png`），不是装着的这一份 —— 装着的还是 §23 的字节，里面这两句旧话都还没有。
   复数那一支新旧渲染出来的字一模一样（`…连着排在第 41-42 页。` —— 旧式把两个数分给两个参数，
   新式让 `pageSpan()` 先算成 "41-42"），所以看得见的差别只在**单页那一支、那两条新句子，和表里那一格**。

**仍然欠的**：**英文单复数剩下的那批** —— 这一片只修了两句落点句，目录里还有一串"数词 + pages"的名词
单复数不安全（`Move after page 5 (1 pages)`、`Delete 1 pages`、`Rotate 180° (1 pages)`、
`Watermark "x" (1 pages)`、`Merge in 1 pages`、`Split into pieces of 1 pages`、`This document has 1 pages`）。
正路是 Qt 的 numerus：源文用 `%n`，`.ts` 里 `<message numerus="yes">` 一条给 one/other 两形，中文两支同文
所以界面不动。**没做不是做不动**：`i18n_smoke` 现在那 104 条探针问的是 `translate(context, source)` 这个
两参重载，它问不出复数条目 —— 要改就连探针一起改，一次动 104 行，值得单独一片而不是塞在这里。
其余照旧：§29 那条顺序事实（空列表判定排在"拆分挂着"之前，没人钉）、整张水印在 1.51 倍下的样子、
§28 第 6 条末那两档的干净量法、另存框改名字那一半、§27 末那两条（英文菜单助记符写法不统一、
`去掉口令` 那一屏两个框一起灰）、下一次重打包。老账不变：新文档打开时不回第一页、两句操作连成的英文小结、
英文的等待句、单页文档那一句、带注释/表单的供体合并（`fixCopiedAnnotations`）、就地保存、
标签拖去别的窗口时那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。**已经还上的**：`pages 40-40`
（第 1、2、3 条 —— 五个出口收成一处）。

## 32. 英文那个跟着数词变的名词：机制先在一条上跑通，而它顺手抓出我自己写的一条空检查（2026-09-27）

1. **为什么要动机制而不是动翻译。** `(1 pages)` 这种话是读屏软件会照着念出来的，跟 §27 那两个同名口令框
   是同一类问题：不是不好看，是**只有靠文字才看得见的人拿不到正确的那一份**。所以在英文里避开名词
   （"pages: 1" 那种写法）不算修好。Qt 的正路是 numerus：源文用 `%n` 放那个数，目录里一条 `<message numerus="yes">`
   给两种形态，运行时按 n 挑。中文两支同文（中文不变化），所以中文界面一个字都不动。

2. **这个仓库此前没有任何 numerus 条目**（`grep numerus\|%n` 在 `src/`、`tests/`、`Resources/translations/` 里
   零命中），所以这一片的全部价值是**先把机制跑通**，只挑了一条最简单的串落地：`删除 %1 页` → `删除 %n 页`。

3. **`DocumentEditor` 不是 QObject，它的目录出口是文件里那个匿名命名空间的自由函数**
   （`documenteditor.cpp:48`，注释写着理由：走 `QObject::tr()` 会把整批句子堆进 "QObject" 上下文）。
   那个 helper 只接一个参数，所以复数在这里另起一个名字：`trN(const char *text, int n)`，里面是
   `QCoreApplication::translate("DocumentEditor", text, nullptr, n)`。**这一条我先写错过一次，改正是这样的**：
   我起初以为 Qt 把"复数源文"放在第三个槽（平时放 disambiguation 的那个），于是写成
   `translate(ctx, one, many, n)`，还顺手把探针改成同一个形状 —— 两边一致，所以门禁全绿，而那个绿是
   查了一条不存在的条目换来的。按 Qt 的形状，numerus 条目**只有一条带 `%n` 的源文**，第三个槽仍然是
   disambiguation（这里给 `nullptr`），第四个才是挑形的数。证据不是文档而是编译：`tr(one, many, n)`
   在这个文件里报 `C2660: 函数不接受 3 个参数`（那个自由 `tr` 只接一个参数），而换成 `trN` 之后
   两形都照 n 出。**留下的一般规则**：探针要用与程序完全相同的调用形状，形状错了它问的就是另一条
   消息，而"两边都错得一致"是查不出来的。同样的自由 `tr` 在另外 7 个文件里也有
   （`src/document/qtpdfbackend.cpp:152`、`src/export/wordexport.cpp:201`、`src/ocr/ocrengine.cpp:36`、
   `src/translate/translator.cpp:26`、`src/windows/shellintegration.cpp:29`、`src/core/thirdpartynotices.cpp:10`、
   `src/cli/commandline.cpp:108` 等），它们今天都不接 n —— 那批文件里要动复数，得一个一个加第二个出口。

4. **量到的第一件事：`%n` 在没挂目录时也会被替换。** 这是中文用户看的那一路，而它此前**没有任何断言在管**：
   `documenteditor_smoke` 关于撤销行只写了 `!undoDescription().isEmpty()`。所以先补一条钉死全文的
   （`删除 2 页`），然后拿一处真错验证它会红 —— 把代码改成单参 `tr("删除 %n 页")`（也就是漏掉复数那一路），
   红的正是这条，消息里打出来的就是屏幕上会出现的东西：`got "删除 %n 页"`。这一条控制是这一片最要紧的一次，
   因为它证的是"机制在两种目录下都成立"，不是"我改的这段能编译"。

5. **量到的第二件事，是"我写的那条检查根本没在检查东西"，而且重复了三次。** 探针里那条"另一种数
   要给出不同的话"，第一版写成 `translate(..., n=1) != translate(..., n=2)` —— **这条永远成立**，因为
   数字本身就代进去了：`Delete 1 pages` 和 `Delete 40 pages` 当然不一样。把目录里两个 `<numerusform>`
   故意写成一模一样去跑，门禁照样全绿，这才看出它什么也没问。第二版先把数遮成 `#` 再比，但遮的是
   `text.replace(QString::number(n), "#")` —— 它把 `%1` 里那个 `1` 也一起遮了，于是带占位符的行遮完仍然
   不同，控制**又是全绿**。第三版才遮对（只遮不属于占位符的那串数字：`(?<![%\d])\d+`）。中间还有一步
   是它自己抓出来的：给检查加了一条**"遮不动就是没在检查"**的守卫（`mask(english) != english`），这条
   守卫一响就指出第二版按空格找 `" 1 "` 的写法漏掉了所有把数贴着括号的形，比如 `Turn right 90° (1 page)`。
   最后验控制时特意换了一行从没手工调过的（`拆成每 %n 页一份`），红的正是它。**留下的一般规则**：
   比较"两个形"必须先把正在变的那个量遮掉；而遮的手法本身要有一条守卫盯着，否则**遮错方向的表现和
   检查通过一模一样**。跟 §25 那次"只数发射次数、不比页号"是同一族错误，只是这次我连踩三层。

6. **这一族到底有多大，是数出来的不是估的。** `xyzpdf_en.ts` 里"数词旁边站着英文复数名词"的翻译有 **42 条**；
   逐条回到调用点看那道守卫之后：**34 条的那个数真能取到 1**（例：`splitInto()` 的下界是 `pagesPerPiece < 1`
   而不是 `< 2`，所以"每 1 页一份"是合法的 —— `documenteditor.cpp:1142`；`documenttab.cpp:1260` 那句
   "只有 %1 页，拆不出第二个文件"更是**只在 1 页时才出现**），**8 条永远到不了 1**（`拆成 %1 份` 的 N≥2、
   拖放那句有 `size() == 1` 的另一支、`已写成 %1 个新文件` 在 `fileCount > 1` 那一支，等等 —— 改它们是纯搅动），
   **还有一类要留个记号而不是一笔算掉**：一句里两个数各自带一个英文名词，而一条 numerus 只有一个 n。
   这一类里**已经确认两条**：`%1 在这些修改记下来之后变了：当时是 %2 页，现在只有 %3 页。` 和
   `已导出 %1 页、%2 个段落到：` —— 两个数都能取到 1，所以一条复数条目只能让一个名词跟着变，另一个照旧错。
   其余看着像这一类的，逐条看守卫之后大多**不是**，而且看细了一层：`现在这份有 %1 页：再删 %2 页，剩下
   %3 页。` 唯一带英文名词的是 %1，而 %1 恒 ≥2（1 页的文档里任何点名都走到 `leavesNothing` 那一条措辞），
   %2 后面跟着的是 "more" 不是名词 —— **所以这条根本不用改**；`会写成 %1 份，每份最多 %2 页` 里 %1 恒 ≥2，
   n 取 %2 就够；合并框那句复数支里 %2 恒 ≥2，n 取文件数就够。**这一类剩下的数目我不敢给**：它要求每条
   都回到守卫看一眼，还得看一眼那个名词到底挂在哪个数上 —— 上面这三条就是这么改判的，而第一条从
   "要改"变成"不用改"。真要修剩下那两条，要么在句子外面分叉（§31 的办法），要么改英文措辞 ——
   后者是编辑决定，留给他拍。

7. **这一片落地的不止那一条：模型里 `describeOperation()` 的整批计数行**（三档旋转、三种移动、合入、
   拆分、加水印、删除）一共 **10 条**，都换成 numerus 了 —— 菜单那一行是这一族里读者最常看的，而它的
   中文渲染一个字没变（两条控制都证明了这点）。目录里因此有 10 条 `<message numerus="yes">`；探针跟着
   源文一起改，并补上一条**此前根本没有探针**的行：`移到第 %1 页之后（%n 页）` —— 跟 §31 那两个框的
   小结同一类，**目录里有词条不等于有人在读它**。

8. **顺手量到目录这条链子是有牙的：只改 `.ts` 也会重跑 lrelease，而它的错误会停下门禁。** 一次控制实验
   往 `<numerusform>` 后面塞了个 `<!-- CONTROL -->` 注释，门禁给出的不是"某条检查红"而是
   `lrelease error: Unexpected tag <numerusform> at .../xyzpdf_en.ts:195`，构建就停在那里 —— 目录的语法
   不会悄悄烂掉，写坏一条 XML 跟写坏一个词是两种响法。**但这是"编辑 .ts 才有的保护"**：它不检查某条
   源文改名之后探针还问不问得到（那是 §22/§31 记的那件事，只有探针跟着改才看得见）。

9. **数字，都是量过的。** 门禁 **26 passed / 0 failed**。目录 **539 条**（§31 那一次是 537 → 539；
   这一片把其中 10 条改成 numerus 形状，条目数因此**不再动** —— `<message numerus="yes">` 现在 10 条、
   `numerus` 标记 20 个），0 unfinished。探针 `{ "` 计数 **101 → 106**：+2 是 §31 那两句各拆单复数，
   +3 是这一片补的（`向左转 90°`、`旋转 180°`、`移到第 %1 页之后（%n 页）` —— 三行都在目录里，
   此前没有任何一条探针点名过它们）。dev 树 `xyzpdf.exe` 是 `bb591b8e…`、**1,280,000 字节**（§31 那一次
   1,280,512 —— 两次都在 1,278,976 之上，所以大小跟着内容走，但它不是"负载有没有变"的信号，
   那一问只有重打包答得动）。产品代码动的是 `documenteditor.cpp`（一个新 helper + 10 处调用）和
   §31 那三个对话框。**没重打包**，所以 §31 与这一片都在等同一次 `build-package.ps1` 作证。

**仍然欠的**：**numerus 那一族剩下的那些**（第 6 条给了 42 / 34 / 8 这三个数和一条翻案，剩下"到底几条
真要改"要每条回守卫看一眼才算 —— 那是下一片的第一步，不是这一片的结论；这一片动的是模型那 10 行，
对话框与状态行那批还没动），其中第 6 条末那两句式的要求的是措辞决定而不是 numerus，
§29 那条顺序事实（合并框空列表判定排在"拆分挂着"之前，没人钉）；整张水印在 1.51 倍下的样子；
§28 第 6 条末那两档的干净量法；另存框改名字那一半；§27 末那两条；下一次重打包。老账不变：新文档打开时
不回第一页、两句操作连成的英文小结、英文的等待句、带注释/表单的供体合并（`fixCopiedAnnotations`）、
就地保存、标签拖去别的窗口时那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。
**已经还上的**：numerus 这条路能不能走（第 3、4 条），以及中文界面那一路不会漏出 `%n`（第 4 条）。

## 33. 这一族铺到模型外面：八行的守卫，和目录自己把第二只形削掉（2026-09-27）

1. **这一片动的是 §32 末记下的那条债：模型那 10 行之外的整批计数句。** 做法是一条一条回守卫，不是照
   着扫描的名单改：目录里"数词挨着一个以 s 收尾的英文名词"的条目这一遍扫出 **40 条**，逐条打开调用点
   之后落 **8 行**，其余四种下场分别记在第 3、4、5 条。落地的 8 行是 `MainWindow` 那四句（`已导出 %n 张
   图片到 %1`、`其中 %n 个带网格的表格…`、`其中 %n 处图形…`、`这个窗口里有 %n 个标签带着没有写进任何
   文件的修改`）、`DocumentTab` 的 `列表里前面 %n 个文件已经记下了`、`RotatePagesDialog` 的
   `说好的 %2 页里有 %n 页还在，只转那 %n 页`、`DeletePagesDialog` 的 `这 %n 页里有 %1 页是合并进来的`、
   `WatermarkDialog` 的 `前面合并进来的那 %n 页…所以不在这 %1 页里`。目录里的 numerus 条目 **34 → 42
   条**（§32 那一片是 10 条，§33 前半铺到 34，这一片再到 42；§30 那一份是 0 条）。

2. **中文一个字都没动，而这不是一句感觉。** 每行的 `%n` 挑的都是原来那个 `%1`/`%2` 站的位置，剩下的占位
   符顺次补上，所以 40 页里有 6 页是合并进来的那一份文档，改前改后渲染出的中文一模一样。

3. **两条"看着像、其实不由数决定"的翻案。** `正在导出为 Word…（%1 / %2 页）` 的英文写的是
   `page %1 of %2`，名词不在数后面；`第 %1 页就在要移动的页里` 的 `one of the pages` 是固定短语。这两条
   改它们是纯搅动 —— 而它们都在那份 40 条的名单里，也就是说**扫描名单本身就是不可信的**，只有回到调用
   点看一眼才算。

4. **"永远到不了 1"这一类现在每一条都有出处。** 拖放那两句走 `pendingDrop.size() == 1` 的另一支
   （`mainwindow.cpp:241`）、`已写成 %1 个新文件` 在 `fileCount > 1` 那一支（`mainwindow.cpp:1422`）、旋转
   框那句的 %2 恒 ≥2 因为 `present < pages.size()` 这一支排在 `present == 0` 之后（两条合起来才是"命名过
   的页至少两个"）。**最有意思的一条不在同一个文件里**：拆分框的 `可是这 %1 页里有 %2 页是合并进来的`
   和 `这些页号是照着要写出的那一份数的（%1 页）` 都不用改，理由在 `documenttab.cpp:1260` 那道门（不到两
   页根本开不了这个框）；而删页框没有那道门，所以同一个人写的同一句话在 `deletepagesdialog.cpp:108` 就
   真能撞上"只剩一页、而那一页正是合并进来的"。**同一句话在两个框里一个要改一个不要改，只看那一个文件
   是判断不了的。**

5. **剩下要的是措辞决定，不是 numerus，两小类。** 一句里两个数各自带一个英文名词的四条
   （`已导出 %1 页、%2 个段落到：`、`已把 %1 个窗口的 %2 个标签并入当前窗口`、`当时是 %2 页，现在只有
   %3 页`、`这份要写出的文档一共 %1 页，每份 %2 页只会写出一份`）—— 一条 numerus 只有一个 n；另一类是
   §31 那种代词（`所以是排在它后面那页前面` 的英文 `they land` 会盖到只合入一页的情况），它要的是句子外
   面分叉。两种修法 §31 都做过一次，等拍板。

6. **目录这条链子自己断了一次，而它响的方式值得记。** 这一片跑项目自己的 `update_translations` 目标之
   后，门禁从绿变成 **25 passed / 1 failed**，红的消息是"这条还是中文"。查下来：被削掉第二只形的条目
   **全是 `DocumentEditor` 上下文的**，别的 context 一条没掉。根因不在这个 CMake 目标 —— 拿
   `lupdate -target-language en` 手工跑同一次合并，掉的是同样那几条，而它自己就说了 `Removed plural
   forms as the target language has less forms`。**lupdate 按位置读 `tr()` 调用**（源文、disambiguation、
   数），所以两参的 `tr(text, n)` 把数交到了 disambiguation 槽里：词条照抽、源文照对，只有"这是复数"这一
   件丢了，而丢了之后每一条 (context, source) 仍然完全匹配 —— 这是 §22 那条"探针问不到的东西"的一个新形
   状，**它不在集合里，它在形状里**。修法是让 helper 的形状等于 `QObject::tr` 的形状：
   `tr(const char *text, const char *disambiguation, int n)`，13 处调用各补一个 `nullptr`（刻意不给默认参
   数，给了 `tr(text)` 就歧义）。这条写在 `src/edit/documenteditor.cpp:53` 那段注释里，和 §32 那条"lupdate
   认名字"排在一起：同一个 helper 的两个坑，第一个让词条进不了目录，第二个让词条进了目录却丢了形。

7. **改坏目录之后唯一那份完整副本，是门禁几分钟前编出来的 `.qm`。**
   `lconvert build-local-release/xyzpdf_en.qm -o recovered.ts` 读回 539 条、34 条 numerus、0 unfinished；
   拿它当底子再跑一次显式语言的 lupdate，合并出 8 条新源文（42 条 numerus、0 unfinished），然后一条一条
   **除那 8 对之外 0 条形变、0 处文字变**；再按 HEAD 的条目顺序把 26 个 context 里的消息排回去（`.qm` 是按
   hash 存的，读回来就是那个顺序 —— 不排回去 `git diff` 会报 1799/1672 行，排回去之后是 540/413 行，报的才是
   真差别）。留下的规则：**动手改目录之前先问"改坏了从哪里回去"** —— 这一次答案是"刚编出来的 .qm"，而它只在
   构建之后才存在。

8. **新的三条规则，五条控制，都实测。** `tools/check_translations.py`（§32 末写的那个脚本，这一片第一次
   接进门禁）多了一组问**形状**而不是问**集合**的检查：源文里有 `%n` ⇒ 条目必须是 numerus、必须两个形、
   两个形必须不同、单数形不许按多数写。四条控制各自打红一次（单数形改成多数 / 两个形写成一样 / 剥掉
   numerus 标志 / 删掉一个 numefirst），四次都还原到字节一致。**它的限度也量了**：把 `1 ruled tables` 喂
   进去**不红** —— 数和名词之间隔一个词就漏，所以窗口只留一寸宽，注释写了为什么不加宽（加宽会把正确的
   `1 of those files` 报成错）。C++ 那边两条新断言：`n=1` 那一支不许按多数读；占位符按**整行两个形**问而
   不按单个形问 —— 后者是被删页那句逼出来的（`pageCount()==1` 时合并页数必然也是 1，那一句里没有第二个
   数可说），控制是把多数形的 `%1` 拿掉，红的正是 `loses %1 from both plural forms`。

9. **门禁现在多一步 `=== catalogue ===`**，位置在构建之后、跑测试之前：目录落后于代码的时候，i18n_smoke
   的报法是"这一条还是中文"，而 drift 那一行说的是同一件事的真名字。`--run-only` 也跑它，且没有关掉它的
   开关 —— 这一步存在的理由就是"没人会记得跑"。

10. **数字。** 门禁 **26 passed / 0 failed**（26 是可执行文件数不是断言数，§30 那条规则照旧）；目录 539
    条、0 unfinished、numerus 42 条；具名探针 `{ "` 行 **106 → 116**；`check(` 计数 vs §30 的 HEAD：
    i18n 34→39、documenteditor 553→560、documenttab 265→287 —— **§31/§32/§33 三片都还没提交，这几个差是
    三片合起来的，不是这一片单独加的**（这一片自己动的是 8 处源文、11 行探针、2 条断言、脚本里 4 条规
    则）。dev 树 `xyzpdf.exe` 是 `5fe2b2aac08a…`、**1,277,952 字节**（§32 那一次 1,280,000）：字节跟着内容
    走，但长度不是"负载变没变"的信号 —— 这一片还多给一条同一族的读数：**把目录按 HEAD 的顺序排回去之后长度
    一字没动而哈希换了**（`9202e4bb…` → `5fe2b2aa…`），因为 `.qm` 存的就是那个顺序。所以哈希证的从来不是
    "内容没变"，而是"测过的字节 == 那一次构建的字节"（§30 那句的重述），而"测过的 == 装着的"只有重打包答得
    动。**没重打包**，所以 §16 那"三处相同"欠的是 §24–§28 五次再加 §31/§32/§33 这三片。这一片没跑真窗口
    —— 英文那几屏要的是切语言 + 装着的这一份，两样都压在同一次重打包上。

**仍然欠的**：第 5 条那两类（四个双数句 + 三处代词）要的是措辞决定，§31 的办法已经在手；英文那一族的**屏
幕上**那一遍（切英文 → 撤销行 / 关闭确认 / 导出小结各读一次）；删页与拆分那两句"有几页是合并进来的"可以各
带一个例子文件名（`屏幕上没有它们的页码` 对读者不如"看得见的那两页叫什么"有用 —— 动手前写在计划里，这一片
没做，所以它记在这儿而不是完成清单上）；§29 那条顺序事实；整张水印在 1.51 倍下的样子；§28 第 6 条末那两档
的干净量法；另存框改名字那一半；§27 末那两条；下一次重打包。老账不变：新文档打开时不回第一页、两句操作连
成的英文小结、英文的等待句、带注释/表单的供体合并（`fixCopiedAnnotations`）、就地保存、标签拖去别的窗口时
那句措辞、`gui-verify/*.ps1` 搬进 `tools/`。
**已经还上的**：§32 末那条"到底几条真要改"（40 条名单 → 8 行落地 + 3 类不落地，各自有出处）；
`DocumentEditor` 那个 helper 的第二个坑（第 6 条）；目录形状第一次有了会被门禁问到的检查（第 8、9 条）。

## 34. 英文那一族第一次在屏幕上读到了：六个撤销行、一句关闭确认，和两只读不回来的控件（2026-09-27）

1. **这一片不改一行产品代码，它补的是 §32/§33 一直欠着的那半句话。** 那两片的证据全在离屏探针里：
   "1 page / 2 pages" 这个分别是**问目录问出来的，不是看出来的**。而这一族只有英文会变，中文两支同文，
   所以门禁（它只看中文）永远看不见它成没成 —— 屏幕上那一遍是唯一还在欠着的证法。

2. **切语言这条路不用碰注册表。** `AppSettings::settings()` 认 `XYZPDF_SETTINGS_PATH`（`appsettings.h:30`，
   门禁给每个测试就是这么隔离的），所以一份 scratch ini 就够了：`[appearance]` + `language=en`。
   同一个脚本跑两遍做通道正对照：`zh.ini` 读出来是 `|文件(F)| |编辑(E)| |文档(D)|`，`en.ini` 读出来是
   `|File| |Edit(E)| |Document(D)|` —— 差别只由那份 ini 造成，所以后面读到的英文不是我的针脚写错的产物。
   **顺带一条要写清的**：`languageCode()` 的默认值是 `system`，而 `Languages::resolve()` 在 stored==system 时
   去看系统区域 —— 所以**一份空 ini 也是中文界面**（这台机器系统区域是 zh_CN），"看着像中文"不能当作"设置生效
   了"的证据。这一遍是靠菜单栏的名字本身（`|文件(F)| |文档(D)|` 对 `|File| |Document(D)|`）来认语言的，不是靠
   猜。

3. **屏幕上读到的英文，一支一支写在这里。** 撤销行（菜单里那一行，`Expand()` 打开文档菜单后读 MenuItem 名）：

   | 操作 | 一个单位 | 多个单位 |
   |---|---|---|
   | 删页 | `Undo Delete 1 page(U)` | `Undo Delete 2 pages(U)` |
   | 旋转 | `Undo Turn right 90° (1 page)(U)` | `Undo Turn right 90° (2 pages)(U)` |
   | 移动 | `Undo Move to the end (1 page)(U)` | `Undo Move to the end (2 pages)(U)` |
   | 拆分 | `Undo Split into a piece of 1 page(U)` | `Undo Split into pieces of 5 pages(U)` |
   | 水印 | `Undo Watermark "DRAFT" (1 page)(U)` | `Undo Watermark "DRAFT" (2 pages)(U)` |

   外加两处不来自撤销行：关窗确认在**只有一个**脏标签时读回
   `1 tab in this window carries changes that are written in no file.`（标题 `Changes have not been written`，
   按钮 Yes/No —— 这是 §33 落地的 8 行之一，而 §13/§14 早年在屏幕上读过它的中文孪生句
   `这个窗口里有 1 个标签带着没有写进任何文件的修改。`，所以这一遍读的正是那句话的英文那一半）；
   以及**水印框的确定按钮自己就会变**：`Stamp this page` / `Stamp these 2 pages`，同一框里的小结是
   `"DRAFT" at 20% opacity, on 1 page.` / `on 2 pages.`。拆分框的小结读回
   `Writes 40 files of at most 1 page each: 1, 2, 3, 4, 5, 6, …, 40.` —— 一行里两个数，跟着数变的那个
   名词挂在 `%n`（每份页数）上，另一个（40 份）留在英文里永远是复数，因为它的守卫是 pieces ≥ 2。

4. **两只控件读不回来，而这不是"没生效"。** UIA 给 Qt 的 QLineEdit `SetValue('3')` 之后立刻读 `Value`
   是**空串**（日志原话：`filled an Edit with [3] now reads ||`）；拆分框那个 QSpinBox 走
   `RangeValuePattern.SetValue(1)` 之后读回 `Value`/`Minimum`/`Maximum` **全是空**，而它的确定按钮同一刻写
   着 `Split into 40 files`、撤销行写着 `a piece of 1 page`。**留下的规则：这两样"填没填进去"只能由后果判**
   —— 后果就是小结里那个数、和确定按钮上那个数；读回来自证，读到的是空,于是会以为自己没填上，接着再填一遍。

5. **实例交接这一坑是这一遍最贵的一条。** `$p.Kill()` 之后立刻再启动，新起的进程常常**只有一个空窗口**
   （标题就是 `XYZPDF`，正文是"把 PDF 拖到这里"）：`main.cpp:112` 那一句 `forward(request, name, 1000)`
   把手上的文档交给了一个刚被杀掉、名字还没散架的实例。同一次排查里另一条：UIA 的窗口名和 Win32 的
   `MainWindowTitle` **可以不一致**（`Get-Process` 那边写着 `doc40.pdf — XYZPDF`，UIA 这边同名窗口拿不到），
   所以靠"标题里有没有 .pdf"来找窗口不稳。可用的写法是：先把所有 `xyzpdf` 进程杀干净、睡三秒、再启动，
   然后**轮询**到出现带文档名的窗口为止（这一遍是 1 秒就到位，而失败的那几遍是杀完立刻启动的）。

6. **中文那一遍没读成，记在这儿而不是藏起来。** 四次尝试都没能让 `文档(D)` 在下一次读里交出子菜单行：
   `Expand()` 在英文那几遍好使，在中文这一遍菜单栏本身是中文的（第 2 条那个正对照证明了语言确实切过去
   了）、`文档(D)` 也找到了，可展开之后 `FindAll` 仍然只看到那 8 个顶层项。中间还叠了我自己两个坑：一次是
   那个实例根本没把文档开起来（第 5 条那条，所以栏是中文而窗口是空的，找不到任何编辑行），一次是"有没有
   子菜单行"的判据把带 `(&X)` 的中文行全排除掉了 —— 两次都不是语言没切过去。
   **所以"numerus 改动没碰中文"这一条现在的证法还是离屏的**：§32 钉死的 `删除 2 页` 全文、
   `documenttab_smoke` 读的那批中文小结、加上 §13/§27/§30 早年读过的中文屏幕 —— 而不是这一遍的新读数。
   差的那一屏没读到的话也就是这一条，不是产品里的缺陷。

7. **数字。** 5 个脚本、约 7 轮、**产品代码一行没动、没重新构建**，所以屏幕上这一遍跑的就是 §33 末那个
   `5fe2b2aac08a…`（1,277,952 字节）—— 这也是为什么这一遍不必重跑门禁：字节是同一份。读到 6 条撤销行的
   单/复两支、1 句关窗确认、2 条框内小结、2 个按钮标签。**仍然欠的**：第 6 条那一屏中文；
   `Merge in 1 page (donor1.pdf)` 的英文屏幕读数（要走 §29 那条点行 + `BM_CLICK` 的通道）；
   `Exported 1 image to …` 那一行状态条（§33 那 8 行里唯一还没在屏幕上见过的导出话）；
   第 5 条那两类措辞决定；下一次重打包。老账不变。
**已经还上的**：numerus 那一族的**屏幕证**（英文侧，第 3 条），以及两条通道事实（第 4、5 条）。
