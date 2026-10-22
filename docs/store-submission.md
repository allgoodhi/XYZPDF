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

## 35. 谁先说：一句拒话有几个真前提的时候，屏幕上出现的是哪一句（2026-09-27）

1. **欠账是 §29 记下的那一条**：合并框里 `还没有加上要合并进来的文件。` 和 `这份文档已经说了要拆成每
   %n 页一份…` 两句，在"列表是空的 + 拆分挂着"这一状态下**同时为真**，而屏幕上出现的是前一句 —— 那一次
   是真窗口读到的，没有任何测试说过。照着这一条去找，这类不止一处：每个框回答的是一条 **链** 上的一句
   话，而链上每一环都各有一个测试管着它单独成立的样子，**顺序本身一处都没管**。把两环对调，读者看到的
   话换了，而所有单分支检查照旧绿。

2. **落地的钉（9 个状态、30 处 `check()`，全在 `documenttab_smoke`）**：合并框 —— 空列表 + 拆分挂着 ⇒
   空列表那句、并且不提拆分；静默行 + 拆分挂着 ⇒ 拆分那句赢（被静默那一行**不能**抢在前面）；静默行 +
   锚点空 ⇒ 点名那一行（带文件名），锚点的问题排在后面。删页框 —— 什么都没写 ⇒ `还没有说出要删哪几页`
   且按钮灰；写了但那些页已被前面那步删掉 ⇒ `这些页在前面的修改里已经删掉了` 而不是空框那句；写了全部
   剩下的页 ⇒ `这样一页也不剩了` 抢在算术之前（并且断言没有 剩下 0 页 那行数字）。移动框、水印框各一对
   （第 4 条）。

3. **一处顺序是产品的错，改的是代码。** 水印框那条"前面合并进来的那 %n 页…也不会带上水印"的补充话，原来
   只由 `imported > 0 && hasPages` 门着 —— 于是读者**一个字都还没写**的时候，框先说"还没有说要盖什么字"，
   紧接着解释那几页合并进来的页**不会**被盖。它讲的是一件不会发生的事。补一句 `hasWords &&` 之后 aside
   跟着答案走，理由写在代码旁边：**一句题外话只在一个答案成立时才是真的**，跟 §22 把合并框那句拒绝从
   "点下去之后"搬到"点下去之前"、以及"拒绝是替换那句算术而不是挂在它下面"是同一条规矩。同一类在两处成立
   的题外话没有一起改：删页框和拆分框那句"这 %1 页里有 %2 页是合并进来的、屏幕上没有它们的页码"—— 它讲的
   是**读者够不够得着那些页**，这件事跟框正在拒绝什么无关，所以照旧挂着。

4. **写钉的时候量到两件事，都不是读代码读出来的。** 其一是**我自己那条钉猜错了**：第一版断言"移动框没写
   页码 + 锚点空 ⇒ 先说页码"，门禁把它打回来，屏幕上那句是 `要说移到第几页之后。` —— 因为那个框**开就是
   当前页选中**的，而当前页永远命名了一页，所以 `pages.isEmpty()` 那一只守卫在默认选择下根本不为真；
   页码那一句只在选了 指定页码 而框里空着时才存在。于是这一环钉了两个方向：自定义页码空 ⇒ 页码那句赢，
   当前页 + 锚点空 ⇒ 锚点那句赢。其二：**有几句拒话此前完全没有行为测试**，只有目录里的词条 ——
   `还没有说出要删哪几页`、`这些页在前面的修改里已经删掉了`、`这样一页也不剩了`、`还没有说出要合入哪几页`，
   而合并框那句"拆分挂着"此前只在**有行**的情况下被测过。这是 §22 那条"问的是目录不是屏幕"的第三种形态：
   **词条在目录里、分支在代码里，而行为一次也没被问过。**

5. **三条控制，和一条先是空的假控制。** 真控制：把合并链的第一环改成 `listRows == 0 && splitting == 0`
   （也就是让空列表那句不再抢在拆分之前）⇒ 红两条，正是那一条钉的两半，别的一条不动；把删页框第一环加一个
   `&& false` ⇒ 红两条，同样是那一条钉的两半；把水印 aside 的门改回 `imported > 0 && hasPages` ⇒ 红一条，
   正是那句"never asked to be inked"。**假控制那一跑值得记**：我第一版把两环对调写成
   `if (splitting > 0 && listRows > 0) … else if (listRows == 0)` —— 这一改**在要被钉的那个状态下根本
   不改变输出**，所以 0 条 FAIL，而那副绿看起来跟"这条钉咬不住"一模一样。留下的规则：**控制必须能改变
   读者看得见的东西**；跑绿了先证明改动后的那一支在那个状态下可达，再谈结论。跟 §32 那三版空检查同族。

6. **数字。** 门禁 **26 passed / 0 failed**；`documenttab_smoke` 的 `check(` 287 → **317**（+30）；
   **目录一字未改**（539 条、0 unfinished、42 numerus —— 这一片只复用已有的句子，所以门禁那步
   `=== catalogue ===` 顺带证明没漂）；dev 树 `xyzpdf.exe` `18f36c1ce428…`、1,277,952 字节。产品改动是
   一处条件（加 `hasWords &&`）+ 四行注释。**没重打包**，所以 §16 那"三处相同"欠的还是 §24–§28 五次加
   §31–§35 这五片。这一片没跑 GUI。

**仍然欠的**：§34 那两屏英文读数（`Merge in 1 page (donor1.pdf)`、`Exported 1 image to …`）；§34 第 6 条那一屏
中文对照；§33 第 5 条那两类措辞决定（四个双数句 + 三处代词）；移动框的钉只覆盖了"页码 vs 锚点"这一对，
链上其余几环（`anchorIsMoved`、`changesOrder`）共不共存没人问；其余四个框里同类的题外话有没有第二种"讲的
是不会发生的事"（这一片只在水印框找到了）；下一次重打包。老账不变。
**已经还上的**：§29 那条顺序事实（现在是钉，不是读数）；以及一句更一般的话 —— **一条链的顺序是接口决定，
每个分支单独有测试不等于顺序有测试**。

## 36. 带注释的供体合并：那条修复第一次真的跑到了，而"注解放过来了"不算是证据（2026-09-27）

1. **这是 §17 就写下、每次收尾都重抄一遍的那条老账**："带注释/表单的供体合并（`fixCopiedAnnotations`
   那条路还没真走过）"。这一片去数了一遍，那句其实还说轻了：**11 个 fixture 里 `/Annot` 出现 0 次、
   `/AcroForm` 出现 0 次** —— 于是 `documenteditor.cpp:1573` 那一句 `if (fromHandle.hasKey("/Annots"))`
   在这套测试的每一次运行里都是**假**的，它不是"没被测过"，是**没有任何测试能走到**。

2. **供体在测试里现造**（QPDF 打开 donor 的第一页，挂两个注释，写出到 scratch）：一个 `/Text` 便签，
   一个既是 `/Widget` 又是 `/Tx` 字段的对象，外加字段所需要的那个 `/AcroForm`。不往 `tests/fixtures/`
   加文件，理由跟 §17 的供体、§30 的内容流形状同一句：那个目录是被别的测试 glob 的。两种注释都要，因为
   合并要做的是**两件**不同的事 —— 便签只要落到复制过来的那一页上；表单字段还得被**这份**文档的
   `/AcroForm` 认出来，否则它在任何会显示表单的阅读器里就是消失了。

3. **同一页合两次，不是贪心，是唯一能看见的形状。** 只合一份的时候，"这份复制跟供体共用注释"和"这份
   复制拿到了自己的注释"从文件上读出来**一模一样**。qpdf 自己的头文件写的就是前者不可能：
   `fixCopiedAnnotations` "ensures that each separate copy of a page has private annotations"。所以最小的
   可分辨用例是同一页的两份复制。

4. **有修复的时候（绿的读数）**：两份复制各自 2 个注释；每个注释的 `/P` 就是载着它的那一页（obj id
   相等）；没有任何注释对象被两页同时引用；写出那份的 `/AcroForm` 有 2 个字段，各自指向自己那一页；
   供体文件的哈希前后一样（它只被读过）。

5. **把那一行改成 `if (false && …)` 之后（控制，红 3 条）**：2 个注释指向别的面、2 个对象被两页共用、
   而 `/AcroForm` 的字段数是 **0** —— 供体里填好的那个答案根本不在写出的那份的表单清单里。这是这一片
   最该记住的一条：**`每页两条注释` 那条断言在控制下照旧绿** —— 注释确实是"跟着过来了"，过来的是同一份
   共享对象、`/P` 还指着别人家那一页、字段整体不见。所以"页上有注释"从来不是证据；能看见差别的只有
   **谁的注释、指向哪一页、在不在这份文档自己的 `/AcroForm` 里** 这三问。写测试的人如果只钉数量，
   这条修复删掉也照样全绿。

6. **数字。** 新增 11 处 `check()`（`documenteditor_smoke` 560 → 571），**产品代码一行没动** —— 这一片
   从头到尾是覆盖率，不是行为改变；那条修复本来就是对的，缺的是一条能让它跑起来的输入。门禁
   26 passed / 0 failed；目录一字未改（539 / 0 unfinished / 42 numerus）；dev 树 `xyzpdf.exe`
   `c62b170010d7…`、1,277,952 字节（这一格原先写的是 `2bfdf8b8ccc9…`，那个哈希在磁盘上不存在，
   更正的经过记在 §37 末尾）—— 哈希又换了而产品字节等于 §35 那一份，因为控制那一跑把它重链了一次，
   这正上是 §30 那条"同一份源码连建两次就是两个哈希"的又一次复现：**哈希只说"这是哪一次构建的字节"**。
   **没重打包**（§16 那"三处相同"欠 §24–§28 五次再加 §31–§36），这一片没跑 GUI。

**仍然欠的**：这一片证的是"我们的保存路径 + 手造的供体"。往上还有三层没碰：**已经带字段的文档**再合进
一份带字段的供体（qpdf 那句话后半段 —— 它说要 resolve conflicts from common resource and field names ——
我们一行都没问过）；真文件而不是手造字典（`H:\pdf_test\CH340.PDF` 那类带表单的规格书，字段名是嵌套的、
可能带 `/XFA` 流）；`/Annots` 里出现引用的注释（popup、批答链）。老账里这一条销掉，其余照旧：§34 那两屏
英文读数、§34 第 6 条那屏中文、§33 第 5 条那两类措辞决定、移动框链上其余两环、另存框改名字、水印那两档
干净量法、新文档开在第几页、就地保存、下一次重打包。
**已经还上的**：从 §17 一路抄到 §35 的那条"带注释/表单的供体合并" —— 现在那条分支每次都跑，而它跑不过
的时候有 3 条检查会红。

## 37. 导出那一行终于读到了：它不在 Text 里，而"读到的"和"屏幕上的"不是一回事（2026-09-27）

1. **还的是 §34 第 7 条点名的那一屏。** `已导出 %n 张图片到 %1` 是 §33 那 8 行 numerus 里唯一没在屏幕上
   见过的一句（离屏那一遍早就把两支形状钉住了 —— `i18n_smoke.cpp:374` 那条探针）。这一片不改一行产品代码，
   只问一句：真实窗口里那一行长什么样。

2. **头两遍我报的是"这一行读不到"，而那是仪器的问题，不是产品的。** 那两遍的抓法是把整个窗口里
   `ControlType.Text` 的孩子全打一遍，于是只抓到 `1 / 40` 和 `202%`。**根因**：`showMessage()` 那句话由
   QStatusBar 自己画，它不像两个常驻读数那样是子 QLabel，所以在 UIA 树里它是 **StatusBar 元素自己的
   Name**。把抓法换成"拿 StatusBar 元素、读它的 Name"，同一份文件、同一台机器、同一条点击路径，第一次
   采样就读到了。这条比 §34 第 6 条那三遍收手更值得记：前两遍的日志**看起来完全像"产品没显示"**，
   而差别只在抓哪个属性。

3. **两档都在屏幕上读到了，而且是两条独立通道一起读到的：**
   - 单张（当前页）：`Exported 1 image to H:/tmp/num34/export`
   - 全部（40 页）：`Exported 40 images to H:/tmp/num34/export`
   英文那一支的单复数就是 `1 image` / `40 images`；`%1` 落下来是斜杠原样（`H:/tmp/…`，不是反斜杠）。
   像素那一遍是把状态条的消息区（左边界到常驻标签之前）裁成 PNG 亲眼看，两张图里那句话都在。

4. **一条时间序列，两个通道同一刻一起读**（每 0.7 s 一次，40 页那一档，采到 21 s）：
   `t=0.7…6.3 s` Name 有那句话、消息区墨点 1,941；`t=7.0 s` 起 Name 空、墨点 1,588，此后不再变。
   所以 `showMessage(…, 6000)` 那六秒在屏幕上是真的（最后看见 6.3 s、第一次看不见 7.0 s，正好差一个采样格）。
   **对照**：空白那条线自己就有 1,588（裁进去的下沿是页面内容），所以这一片用的是差值 353 而不是"非零即有字"。
   30 个采样点里 Name 和像素**没有一次不一致**，包括 `t=0` 那一次：那时 Name 写着 `Opened doc40.pdf`，
   裁下来的图上也正是那句（它的 4000 ms 到没到期我没测，所以这一格只算"两个通道同意"）。
   另外：40 个 png 在按下 Export 之后 0.7 s 就已经全落盘了。

5. **要撤回一条上一遍的推断。** r11/r12 那两遍我从 Name 读出"过了 6 秒还在、隔二十几秒再读还在"，
   当时写下的是"这句话在无障碍层里过期不失效"。**像素说没有那回事**：同一条路径 7.0 s 时 Name 已经空了。
   所以那个"持久"是**读的那一侧**的性质（.NET 的 UIA 客户端对元素属性有缓存，它什么时候作废我没测），
   不是产品行为。能留下的只有结论那句：**问"这句话此刻在不在屏幕上"要读像素；Name 说的是"最近一次刷新
   以来它看到过什么"**。为什么 r12 里二十几秒不刷新、r14 里 7 秒就刷新，仍然欠着 —— 那是拿 UIA 事件本身
   做一遍的活，不是产品代码的活。

6. **顺带把 §29 那条原生框路由又走通两次**，并且量到一件之前只是当作事实的事：
   `imageexportdialog.cpp:88` 那个目录框是 `setReadOnly(true)` —— 所以"导出到某个目录"这一屏**必须**经过
   原生选择框，离屏没有捷径。这一片两遍都用 `WM_SETTEXT` 到那个 Edit + `BM_CLICK` 到 id=1 的按钮填目录，
   两遍都成；`All (40 pages)` 那个标签本身也是屏幕上读到的（复数正确）。

7. **数字。** 产品代码一行没动、一个词条没添、没重新构建、没跑门禁（纯 GUI 读数），**没重打包**；
   跑的是 §36 那一次构建留下的 `xyzpdf.exe`（`c62b170010d7…`、1,277,952 字节）。日志
   `H:\tmp\num34\r10…r14.txt`，裁下来的图 `r13-msg.png`、`r14-0.png`、`r14-1.png`。

**顺手更正 §36 的一个数**：那一格原先写的 dev exe 哈希 `2bfdf8b8ccc9…` **在磁盘上不存在** —— 现在这份是
`c62b170010d7…`，而它的 mtime（18:49:35）早于 §36 那次写文档（18:51:50），所以不是"文档写完又重链了一次"，
是那个数抄早了。字节数 1,277,952 对得上。§36 的 `check(` 560 → 571 这一遍从文件重数过（对），
门禁 26 passed / 0 failed 没为重跑（源码与那次跑是同一份）。这一条本身就是 §30 那句话的又一次复现，
只是更硬：**哈希只说"哪一次构建的字节"，而抄下来的哈希要当场对磁盘核 —— 不然它连"哪一次"都不是。**

**仍然欠的**：§34 第 6 条那一屏中文；`Merge in 1 page (donor1.pdf)` 的英文屏幕读数（§34 第 7 条点名的
另一屏，要走 §29 那条点行 + `BM_CLICK` 的通道）；第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上
（已带字段的文档、真文件、`/Annots` 里的引用）；§33 第 5 条那两类措辞决定、移动框链上其余两环、
另存框改名字、水印那两档干净量法、新文档开在第几页、就地保存、下一次重打包（§16 那"三处相同"
欠 §24–§28 五次再加 §31–§36）。
**已经还上的**：§34 第 7 条点名的那一行状态条 —— 两档都在屏幕上，且像素与 Name 同读；
外加一条仪器事实（状态条那句在 UIA 里是元素的 Name，不是子 Text），它让"读不到"这个结论从此不能只凭
一次抓取就写下。

## 38. 驱动收进仓库，顺手量了一句本来要写进 AGENTS.md 的话（它说反了）（2026-09-27）

1. **还的是 #4 那两笔**：把 §26/§29/§34/§37 一路现写、跑完就扔在 `H:\tmp` 里的 UIA 驱动收成一个
   tracked 文件；以及在 AGENTS.md 里写一句"别对这个目录跑 `update_translations`"。**第二笔没有照原样
   写**，因为动手前先去量了一遍，量出来的结论跟记下来的那条相反 —— 那句"别跑"是 §33 被咬之后记下的，
   而 §33 已经把成因修掉了。

2. **跑法**：备份 `.ts` → `cmake --build build-local-release --target update_translations` → 逐行对比
   → 还原（`git diff` 为空）。读数：
   - `<message>` 539 条，前后一样多；`numerus="yes"` 42 条、`<numerusform>` 84 个，**一字未动** ——
     §33 那个"把 helper 改成 `QObject::tr` 的形状（source, disambiguation, n）"确实把坑填上了，
     lupdate 现在能正确读到计数槽。
   - 22 增 7 删里 **21 行是 `<location>` 记账**：15 行新增指向 `tests/i18n_smoke.cpp`，6 删 6 加是
     我们自己源码的行号刷新（§33/§35 挪过行）。
   - 唯一一处译文变化是 `This document's only page` → `This document&apos;s only page`：XML 转义，
     Qt 读出来是同一个字符串。
   - `unfinished` 仍是 0；`.qm` 没被这个 target 碰（mtime 还是 17:12:39），所以还原之后没有任何东西
     需要重建 —— **这一片一行代码都没编**。
   所以 AGENTS.md 该写的不是"别跑"，而是"跑得起，而且 `=== catalogue ===` 那一步会兜住"。

3. **量出来的一件之前没人注意过的事**：那 15 行新 `<location>` 说明 **lupdate 把探针表里的
   `translate(ctx, "…")` 也当成一次调用点**。后果两头都成立 —— 产品里已经不再用的串，只要还挂在探针表上，
   `-no-obsolete` 就不会删它；反过来，删掉一行探针可能把那条串从目录里带走。"目录 == 代码"这句话里，
   **探针也算代码**。

4. **`xyzpdf/tools/gui-probe.ps1`**（333 行，纯 ASCII）三种模式，**每一种都当场跑通**：
   - `inventory`：窗口里每个非空 Name + 控件类型 + enabled（26 条），就是 §26 抓到两个产品缺陷的那张表；
   - `menudump`：`Expand()` 展开一个顶层菜单读它的行（英文 18 条：七个编辑项都带 `…`，无修改时
     Undo / Redo / 保存修改为 三条 `enabled=False`）；
   - `exportstatus`：走原生选择框真导出一张，再按 §37 那条时间序列读回执 —— **第二次独立复现**：
     0.7…5.6 s 有那句话（ink 1902），6.3 s 起两路一起空（1588），跟 §37 的 1941/1588 同一形状。
   注释里写的是踩过的坑本身（`Invoke` 在跑 `exec()` 的菜单行上会打断整条连接；对话框在主窗口的子树里；
   原生框只能 Win32 `WM_SETTEXT` + `BM_CLICK` id=1；状态句是元素的 Name 不是子 Text；DIP→px 用
   `GetDpiForWindow`；空白那条线自己就有 1588 个墨点，所以判据是差值不是"非零即有字"）。

5. **搬的过程里撞到两条，都是这一片自己的**：
   - PowerShell 里函数的"返回值"是它写进流水线的**全部**东西。所以辅助函数里一句诊断 `Write-Output`
     把 `StatusInk` 的计数变成两元素数组，`$ink -lt $ink0` 报"无法比较"；而 `PickFolder` 那句
     "picker appeared after 0.5s"被吞进返回值、屏幕上根本没打印 —— 那副样子看着像"路由没走通"，
     其实话只是被吞了。改成 `[Console]::Out.WriteLine` 之后两处一起好了。
   - 我在这份脚本里写了中文回退匹配串（`'当前'`、`'导出'`、`'文件夹'`），ASCII 检查抓到第一个字节
     0xE5；按老规矩换成码点拼（`CjStr 0x5F53,0x524D`）。同一份文件里还留着一个 `§`（0xC2 是 GBK
     前导字节，会吞掉下一个字符）—— 那条坑明明记在记忆里，这一片又亲手踩了一次。

6. **数字。** 新增 1 个 tracked 文件（`xyzpdf/tools/gui-probe.ps1`）、AGENTS.md 三条、两份文档各一段；
   **产品代码一行没动、没构建、没跑门禁**（没有 C++ 改动，门禁跑不出新东西；`.ts` 已还原成 HEAD 字节），
   **没重打包**（§16 那"三处相同"仍欠 §24–§28 五次再加 §31–§37）。

**仍然欠的**：`Merge in 1 page (donor1.pdf)` 的英文屏幕读数（现在给 `gui-probe.ps1` 加一个模式就能做）；
§34 第 6 条那屏中文（`menudump -Language zh_CN` 已知不通，是 `Expand()` 的语言问题，不是产品）；
§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；另存框改名字、水印那两档干净量法、
新文档开在第几页、就地保存、下一次重打包。
**已经还上的**：#4 那两笔，外加一条关于工具链的事实（探针在 lupdate 眼里就是调用点），
和一句本来会写进 AGENTS.md 的错话。

## 39. 合并那行撤销字读到了，代价是它顺手钉出一条真缺陷：四条形把段落换行丢了（2026-09-27）

1. **还的是 §34 第 7 条点名的另一屏**：六个撤销行里只有合并那一行从来没在屏幕上见过，因为它是唯一
   一行**点名一个文件**的撤销字，而要到那一行必须过原生打开框。§38 把驱动收进 `tools/gui-probe.ps1`
   之后，这一屏就只是"再加一个模式"。两支一次跑完：
   `Undo Merge in 1 page (donor1.pdf)(U)` / `Undo Merge in 2 pages (donor2.pdf)(U)`。

2. **顺带第一次在屏幕上读到 §31 那两句落点话的英文单/复两支**（那一片的证据当时全是离屏的）：
   `Merges 1 page from 1 file to the end: in the order of the list it ends up at page 41.` 对
   `Merges 2 pages … they end up together at pages 42-43.` —— 换的不只是数，是 `it ends up at page`
   对 `they end up together at pages`。合并表自己那三格也一并读到：`donor1.pdf | 1 page | 1` 对
   `donor2.pdf | 2 pages | 1-2` —— §31 那条"一页不写成 1-1"的第五个出口，这次是在屏幕上看见的。

3. **缺陷**：第二句小结当时长这样 ——
   `…they end up together at pages 42-43.This document already has 1 page merged in from another file.`
   两句话**焊在一起**了，而中文那一版分段正确。根因不在拼接代码：这些小结是 `line += tr("…")` 一句一句
   加出来的，**段落换行就写在可翻译字面量里面**（`tr("\n\n这份文档里已经有 %n 页是合并进来的了。")`），
   而那两条的英文形没带上那两个换行。数了一遍整个目录：**源文以换行开头的有 32 条（35 个形），
   丢了段落换行的正好 4 个形 = 2 条 numerus × 2 支** —— `mergepagesdialog.cpp:453` 和
   `splitpagesdialog.cpp:123`。其余 31 个形都带着，所以判据可以写成"相等"而不是"至少一个"。
   修好之后屏幕上就是 `…at pages 42-43. /  / This document already has 1 page merged in…`。

4. **为什么门禁以前看不见**：这一类漂移不在集合里也不在 numerus 形状里 —— (context, source) 照对、
   `%n` 照有、两个形照不同、单数形照不写成多数。§33 那四条形状规则一条都不沾。所以补了第五条
   （`paragraph_break_rules_of`）：**源文开头有 k 个换行，每个形开头也必须有 k 个**。控制：把其中一形
   的换行拿掉 → 检查器退 1 并且报出`源文开头有 2 个换行，译文只有 0 个，两句话会粘在一起：This document
   already has %n page merged in…`；还原 → 退 0。

5. **一条通道事实（已经写进脚本注释）**：原生框要按 **类** 认，不按标题 —— 第一版 `mergedonor` 找
   标题里带 `Open` 的窗口，找不到，而它打印出来的候选行显示那个框其实开着，标题是我们自己的可翻译串
   `PDFs to merge in`（中文那版会是另一句）。而 `#32770` 这个类名恰好把"壳的框"和"我们的框"分得干干净净：
   我们的对话框报的是自己的控件类名（`MergePagesDialog`、560x544）。**"打印候选再写过滤"这一条又一次
   在一轮之内救回一个假阴性。**

6. **数字。** 目录 4 个形补上段落换行（539 条 / 42 numerus / 84 形，条数与形状一项没变），
   `tools/check_translations.py` 加一条规则，`tools/gui-probe.ps1` 加 `mergedonor` 模式 + `PickFile`
   （按 `#32770` 认框、点 ListItem 行、`BM_CLICK` id=1），产品代码一行没动 —— 错在目录不在程序。
   门禁 **26 passed / 0 failed**，`=== catalogue ===` 干净；dev 树 `xyzpdf.exe`
   `8102a1dd53e4…`、1,277,952 字节 —— **这一片是真改了负载**（目录是编进 exe 的），字节数照旧不动而
   哈希换了，又一次是 §30 那句话。**没重打包**（§16 那"三处相同"欠 §24–§28 五次再加 §31–§39）。

**仍然欠的**：`splitpagesdialog.cpp:123` 那一屏（要连着记两次拆分才看得见，它的修复这一片只由新规则和
离屏测试撑着，没有新读数）；§34 第 6 条那屏中文；§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；
另存框改名字、水印那两档干净量法、新文档开在第几页、就地保存、下一次重打包。
**已经还上的**：§34 第 7 条点名的两屏（导出状态行在 §37、合并撤销行在这一片），外加一条目录形状规则，
和四条形上少了的两个换行。

## 40. 补上 §39 欠的那一屏：换掉拆分的那句话，两支都在屏幕上（2026-09-27）

1. **§39 修了两条形，只读到其中一条的屏幕。** 另一条在 `splitpagesdialog.cpp:123`，而它有个门：
   **已经记下一次拆分，而且这次填的数跟那一次的数不一样**，那句话才出现。所以这一片必须走两步：
   先记一次，再开一次框，把数改掉。`tools/gui-probe.ps1` 加 `splitsagain` 模式做这件事，
   两圈跑完两支都在屏幕上：

   - 先记「每 1 页一份」，重开框（自旋默认回到 1），改成 7 →
     `… /  / This replaces the earlier "one piece per 1 page"; undoing once goes back to that one. /  / …`
   - 再记「每 7 页一份」，重开改成 3 →
     `… /  / This replaces the earlier "one piece per 7 pages"; …`

   **段落换行在**（` /  /` 那两处），也就是 §39 补进目录的那两个换行，在真窗口里按 6000 条路径之外的
   这一条走通了；目录里 `&quot;` 落到屏幕上就是引号，不露 `&quot;`。

2. **顺带读到的一句"三段拼起来的话"，判过了没有缺陷。** 框里那一行是 `QLabel(tr("每"))` + QSpinBox
   + `setSuffix(tr(" 页一份"))` 三块拼的（UIA 只报出中间那块的名字，所以日志里先看见一个孤零零的
   `One file every`，看着像坏）。查了两端：英文是 `每 → "One file every"`、` 页一份 → "pages"`，
   连起来就是 **`One file every 7 pages`** —— 跟中文 `每 7 页一份` 各自都成句，词序在这两种语言里
   刚好不冲突。这一条记下来是因为它属于 §20/§33 那一族（跨控件拼的话不进目录、目录管不到词序），
   而这一次是**查过说没事**，不是没查。

3. **同一屏另外三句白捡的读数**（都是以前只有离屏证据的）：`Writes 6 files of at most 7 pages each:
   1-7, 8-14, 15-21, 22-28, 29-35, 36-40.`（§32 那族 numerus 的英文两支之一，落在屏幕上）、
   `the first file would be called "doc40 01 (1-7).pdf"`（§24 的 `piecePath()` 那句命名，连范围一起），
   以及换掉之后撤销行是 `Undo Split into pieces of 3 pages(U)`（复数那一支；14 份那份的清单在屏幕上被
   省略号截成 `16-18, …, 40`）。

4. **数字。** 产品代码一行没动、目录一字未改（这一片是**读数**，跑的就是 §39 那份
   `8102a1dd53e4…` 的 dev 树），所以没重构建也没跑门禁；改的只有 `tools/gui-probe.ps1`（多一个模式）
   和这两份文档。**没重打包**（§16 那"三处相同"欠 §24–§28 五次再加 §31–§40）。

**仍然欠的**：§34 第 6 条那屏中文（`Expand()` 在中文菜单上四遍交不出子行，是通道不是产品）；
§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上（已带字段的文档、真文件、被引用的注释）；
另存框改名字那一半；水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§39 那两条形的另一半屏幕证据，外加一句"三段拼的话"被查过而不是放过。

## 41. 另存框改名字：没还上，但这五遍把这条路上四个"看着能行"的仪器各自证伪了（2026-09-27）

1. **欠的是 §28/§29 那一笔**："另存为里改文件名"这一半从来没被这台针脚做到过。五遍之后**它还是没做到**，
   但这一片不是白跑：它把这条路上每一个看起来能用的仪器都单独判了一遍，其中一条**把上一片记下的话翻了**。
   `tools/gui-probe.ps1` 的 `saverename` 模式留着，注释里就是下面这四条"别信"。

2. **`WM_GETTEXT` 读不回那个框 —— §29 那句要撤回。** 名字框确实是 `Edit` 控件 id=1001，而且
   `GetGUIThreadInfo` 报它**就是焦点所在**；可 `WM_GETTEXT` 答**空串**，而同一时刻截屏里那个框明明白白写着
   `doc40 modified.pdf`（`H:\tmp\num41\dialog.png`）。所以 §29 记的"`WM_GETTEXT` 从 id=1001 读回了种下的名字"
   在这台机器、这份构建上不成立。**凡是靠这个读法判断"填没填上"的，都不算证据** —— 这个框只能拍照看。

3. **`BM_CLICK` 到 id=1 不接受这个框。**  typed 的名字没写出来，种下的名字也没写出来，框还开着
   （`c-after.png` 里 保存(S) 带着焦点矩形）。而同一个调用在 §29 的**打开框**、§37/§38 的**文件夹框**上都好使
   —— 所以"按下默认按钮"是**逐框**的事，不是一条通用路由，别把一次的成过当成一类。

4. **键盘确实写得进去，而它下一遍又自己坏了。** `^a` + 全路径，截图里框里就是
   `H:\tmp\num41\typed-name.pdf`（反斜杠齐全、整条替换掉种下的名字）。**下一遍同样的代码**，框里成了
   `H:\tmpnum41typed-name.pdf` —— 反斜杠被吃了（这台机器的 IME 吃合成输入是记过的老坑，这次吃的是 `\`）。
   `{ENTER}` 不接受这个框（按钮带着焦点也没用）。

5. **于是"真点击按钮矩形中心"那一遍不能算证据，这一条是这一片最该记住的。** 那一遍点的是
   (1691, 948) —— 位置由那个按钮自己的 `GetWindowRect` 算出来，是对的；可**它要接受的文件名上一行已经被
   IME 吃成 `H:\tmpnum41typed-name.pdf`**，一个不存在的目录，框当然不肯接受。所以这次失败**既不能证伪也不能
   证实**点击路由，它说的是"输入坏了"。**控制被污染时，失败的读数翻不成结论** —— 跟 §35 那条"控制必须能改变
   读者看得见的东西"是一枚的两面：跑红/跑绿之前，先证明这一次送进去的东西就是你以为的那串字。

6. **还差什么（写清楚，不是"再试一遍"）**：先拍照**逐字**确认框里的路径，再点按钮矩形中心，然后读三样
   回执（文件在不在、新标签、状态行 —— §37 已经教会怎么读状态行）。`saverename` 模式已经把前半段
   （脏化 → 打开保存框 → 枚举 → 写 → 报结果）收好了，缺的就是这三步的顺序。

7. **白捡的两条读数**（都在那张截图上）：一是 §14 那个种下名字的**英文那一版第一次在屏幕上看见** ——
   框里 `doc40 modified.pdf`，标题 `Save these changes into a new file`；二是这个框**两种语言**：
   我们自己给的那句标题跟着设置走英文，而壳自己的 `文件名(N):`、`保存(S)`、`隐藏文件夹` 还是中文，
   因为它跟的是系统区域。**"切了英文"从来不等于整屏都英文**，这句以后再说语言问题都用得上。

8. **数字。** 产品代码一行没动、目录一字未改、没构建也没跑门禁（这一片全是针脚）；改的只有
   `tools/gui-probe.ps1`（多一个模式 + 四条"别信"的注释）和这两份文档。**没重打包**（§16 那"三处相同"
   欠 §24–§28 五次再加 §31–§41）。每一遍都验了源文件哈希：`source hash unchanged: True` 四遍，
   scratch 里没留下任何 PDF。

**仍然欠的**：这一笔本身（另存框改名字）；§34 第 6 条那屏中文；§37 第 5 条那个"UIA 属性缓存何时失效"；
§36 那三层往上；水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：这一片没还上它要还的那笔。它换回来的是四个被各自证伪的仪器，和一条关于"污染的控制在
失败时不能翻成结论"的规矩。

## 42. 那屏中文读到了，而 §34 记的"Expand 不跨语言"是我的针脚写错，不是通道不通（2026-09-27）

1. **还的是 §34 第 6 条**：四次尝试都没能让中文的 `文档(D)` 交出子菜单行，当时写下的是
   "`ExpandCollapse.Expand()` 在英文好使、在中文交不出行"。这一片**那句不成立了**：同一台机器、同一份
   dev 树，`-Language zh_CN` 跑 `menudump -Menu 6587,6863`，**Expand 就开了**，8 个顶层项变 18 个
   （10 条子行）。中文的 `文档(D)` 那 10 行全读回来：七个编辑项都带 `…`、没有多余的 `&`、
   `撤销(U)` / `重做(O)` / `保存修改为(S)…` 在无修改时 enabled=False —— 跟 §38 英文那一遍逐项对得上。

2. **真正的原因有两个，都是我的针脚，不是语言。**
   - 每个要匹配的标签我都只写了**英文**（`'Split Files'`、`'Current page'`、`'Undo*'`、`'Split into'`）。
     中文界面里这些串一个都不存在，所以"找不到行"从头到尾说的是"我的针是英文的"。
     修法是让这份 ASCII 脚本能问中文：`Needle '6587,6863'` 把十六进制码点拼成串（文件里仍然一个
     非 ASCII 字节都没有），`MatchOne` / `RowAny` 一次问两种写法。
   - 另一半更值钱：重开框之后我绑到了一个**正在关闭的窗口**。它的特征很干净 —— `Name` 是空串、
     `FindAll(Descendants)` 返回 **0 个**，可它仍然在 `Window` 列表里、仍然按名字匹配得上。
     于是那一遍报的是"框里 0 个 Text 元素"，看着像中文界面少了什么。修法是 `WaitDialog`：轮询 +
     **要求候选真的有孩子**，并且把 Text 的**条数与最长长度**打出来，这样"什么都没打印"和
     "那里什么都没有"是两句话。

3. **中文那一屏现在读到了什么。** 拆分框重开改数之后的小结全文（180 字那条）里包括这句题外话的中文
   原版 —— `这会把前面记下的「每 1 页一份」换掉；撤销一次回到那一个。` —— 和 §40 那两支持者在屏幕上：
   `会写成 6 份，每份最多 7 页：1-7、8-14、…、36-40。`、`例如第一份会叫「doc40 01 (1-7).pdf」`，
   撤销行 `撤销 拆成每 3 页一份(U)`。**这一条把 §32–§35 那族最后半句"中文两支同文所以不用看屏幕"补成了
   看见的**：同一条流程两语言各跑一遍，英文两支（§40）+ 中文一支（这一片）都在屏幕上。

4. **控制**：改完针脚后英文那一遍照旧（4 条 text、两支 `This replaces the earlier "one piece per
   1 page" / "…per 7 pages"`、撤销行 `Undo Split into pieces of 3 pages(U)`），所以"中文能读了"不是
   把英文那条路改坏之后剩下的假象。

5. **数字。** 产品代码一行没动、目录一字未改、没构建也没跑门禁；改的只有 `tools/gui-probe.ps1`
   （`Needle` / `MatchOne` / `RowAny` / `WaitDialog` / `TopMenu` 报是哪条通道开的 + 各模式的中文针脚）
   和这两份文档。**没重打包**（§16 那"三处相同"欠 §24–§28 五次再加 §31–§42）。

**仍然欠的**：§41 那笔（另存框改名字）；§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；
水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§34 第 6 条那一屏中文 —— 顺带撤回一条写在文档里的通道结论，它其实是我的针脚语言写死了。

## 43. 重打包、装上、三处哈希对上，装上的那一份第一次把文档打开了（2026-09-27）

1. **顺序**（他这一句就是 §16 那笔欠账的授权）：`build-package.ps1 -Sign -Cert 221C6B…0A83` →
   `Remove-AppxPackage` → `Add-AppxPackage` → `signtool verify /pa` → `verify-package.ps1` → 三处哈希。

2. **包**：17 MB、62 个文件、解开 39.4 MB；剥掉 34 个文件 29.3 MB 用不上的 Qt 负载；QPDF 那三个 DLL
   按名字搬进来；manifest 点的 8 个文件全在；`resources.pri` 索引了 25 张图；签名带 RFC3161 时间戳，
   `signtool verify /pa` 报 Successfully verified。
   **一条自己的坑**：在 Git Bash 里直接给 signtool 传 `/pa`，MSYS 会把它当路径改写
   （`SignTool Error: File not found: E:/Git/pa`），换 `powershell -Command` 传才对 —— 同一类坑这一片
   还撞了一次半：`-Package build-local-release\package\…` 的反斜杠也被吃掉。

3. **§16 那笔账平了。** 三处 `xyzpdf.exe` 全是
   `8102a1dd53e4b4883f7b5f3731e7d5719b7e25af59a5ef063dac7f4d6ca04adb`、1,277,952 字节 ——
   构建树、`verify-package.ps1` 解出来的那一份、`C:\Program Files\WindowsApps\…\bin\` 里装着的那一份。
   这是自 §24 以来第一次对上（欠了 §24–§28 五次再加 §31–§42 十二次）。**所以"装上的就是测过的那一份"
   这句话现在覆盖 §24 到 §42 的全部内容**，包括 §39 那次目录修复 —— 也就是装上的英文界面里，合并与拆分
   那两屏的小结分段是对的。

4. **装上的那一份第一次把文档打开了。** §11.2 与 §23 记的是"包里的应用从来没点开过文档，原生打开框后面
   什么都没见过"。这一片用 §29/§39 那条路由做完了：`explorer.exe shell:AppsFolder\…!XYZPDF` 起来
   （没有文件参数，清单也没声明 `AppExecutionAlias`）→ `文件(F)` → `打开(O)…` → 原生框按类 `#32770`
   认出来 → 点 `text.pdf` 那一行 → `PostMessage(BM_CLICK)` 到控件 id 1 的按钮。读回来的四样：
   标签 `text.pdf`、窗口标题 `text.pdf — XYZPDF`、状态行 `已打开 text.pdf`（中文那一份，走的正是 §37
   那条"状态句是 StatusBar 元素的 Name"）、页号 `1 / 2`。

5. **两个临时脚本的坑**（写下来免得下次再交学费）：针脚 `打*` 同时命中 `打开(O)…` 和 `打印(P)…`，
   第一版连着点了两下；"取最后一个 `*.pdf` 行"会撞上 UIA 报出来的**重复行**（`many-pages 已修改.pdf`
   在列表里出现两次）。修法都是把匹配收紧：整名前缀 + 指定要哪一个文件。

6. **一条没量平的矛盾，记下来而不是藏起来。** 装上的那份打开的是
   `H:\test\XYZPDF\xyzpdf\tests\fixtures\text.pdf`，而打开框落的目录正是 HKCU 那把
   `Software\xyzpdf\xyzpdf\files\lastOpenDirectory` 的值 —— 看着像包里的应用和开发版共用同一把键。
   可打开之后再看同一把键下的 `recent\documents`（6 条，含 `CH340.PDF` 那类），**里面没有 text.pdf**。
   所以要么 Qt 的最近文件走的是包自己的虚拟化存储而 `lastOpenDirectory` 不是，要么中间还有一层我没看见。
   这一条留给下次：**验装上的那份时，别默认设置在哪把键上。**

7. **数字。** 产品代码一行没动、目录一字未改、没重跑门禁 —— 包是从 20:26 那次构建（§39 跑过门禁的同一份
   字节）打的。工作树里 §40–§42 的文档与探针脚本仍未提交（要记要推，各需一句）。

**仍然欠的**：§41 那笔（另存框改名字）；§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；
装上的那一份**还没点过任何一次编辑**（打开已经做到，保存还卡在 §41 那个接受不上的框上）；
水印那两档干净量法；新文档开在第几页；就地保存；第 6 条那个设置存储的矛盾。
**已经还上的**：§16 的三处哈希（欠了十七片的那笔），和 §11.2/§23 的"装上的那份没打开过文档"。

## 44. 另存框改名字做完了 —— 而 §29 那句"这个框根本填不进去"是错的（2026-09-28）

1. **还的是 §41 那句"没做成"。** 它当时没做成的真正原因不是"填不进去"，是**没人接受这个框**：
   `BM_CLICK` 到控件 id 1 在这个框上没人理。这一片把两件事拆开测，先只测接受 ——
   **不碰名字框，用真鼠标点在那个画出来的按钮自己的 `GetWindowRect` 中心**：种下的那份
   `doc40 modified.pdf` 落盘了、新标签开了、状态行回执出来了。接受这一半当场解决。
   然后才把写名字接回去：**`WM_SETTEXT` 到 `Edit` id=1001 + 真点击那个按钮** —— 于是
   `renamed-44.pdf` 落盘、种下的那一份**没有**出现、标签写着 `renamed-44.pdf`。**改名字这一笔做完了。**

2. **要撤回 §29 记的那句**："原生文件对话框的名字框从这个针脚根本填不进去（它的字段没有任何 UIA 模式）"。
   填得进去 —— `WM_SETTEXT` 就是能写它，证据是**按名字出来的那个文件**。§29 之所以那样写，是因为它当时
   只有"读回来是空"这一路证据，而**没有能接受的按钮可点**，于是把"看不见效果"当成了"写不进去"。
   现在三条各归各位：**写=可以（WM_SETTEXT）；读=不行（WM_GETTEXT 答空，屏幕上却有字）；
   接受=要真点击，`BM_CLICK` 在这个框上无效**（而同一个调用在打开框和文件夹框上都好使 —— 逐框，不通用）。

3. **键盘这条路为什么不能再试**：上一轮那张图留着当证据 —— `SendKeys` 打 `renamed-44.pdf`，
   框里出来的是 `renamed-。p'd'f`，底下还摊着一排拼音候选（`1 破地方 2 配电房 3 泡打粉 4 拼读法
   5 PDF 6 佩蒂福 7 帕蒂弗`）。这台机器输入法是中文的时候，往原生框里敲 ASCII 就是给它喂拼音。
   （§41 里"有一遍路径整条打对了"是运气，不是反例 —— 同一段代码下一遍就坏了。）

4. **顺带在屏幕上读到的**（都是以前只有离屏证据的）：保存回执的英文那句
   `Wrote a new file (40 pages): Turn right 90° (1 page)` —— §33 那族状态行的中文孪生句早年读过，
   英文这一支是第一次；以及 40 页那份写出来 **12,666 字节**、`source hash unchanged: True`
   （永不写用户那份，这一条在这条路上是量过的，不是推的）。

5. **收进 `tools/gui-probe.ps1` 的东西**：`saverename` 模式从"报失败的诊断"变成一条**跑得通的路线**
   （`-Mode saverename -OutDir …`，当场验过：目标存在 True、12,666 字节、两个标签、状态行）；
   为此加了 `W32::Rect`（按钮自己的矩形），并把菜单行那一步改成**重试三次 + 每次轮询框出现**，
   因为这一片撞见 `Expand()` 有一遍报"开了"而随后那一下点击没落地（同一条流程另一遍又好）。
   两条自己的坑照旧记着：Python 生成脚本时 `'H:\tmp…'` 里的 `\t` 被吃成制表符，写出的路径成了
   `H:    mp...`，`.Save()` 抛异常**把后面的点击一起带走了**（那一遍"什么都没写"其实是脚本死了）；
   以及一行 `Say` 跨两行而第二行以 `+` 开头 —— PowerShell 解析器要的是行尾的运算符，报 8 个错。

6. **数字。** 产品代码一行没动、目录一字未改、没构建、没跑门禁；改的只有 `tools/gui-probe.ps1`
   和这两份文档。**没重打包**（装着的还是 §43 那份 `8102a1dd53e4…`，三处哈希仍然一致）。
   测试写出来的两份 PDF 都删了，`doc40.pdf` / `donor1.pdf` / `donor2.pdf` 逐个数过还在原处。

**仍然欠的**：**装上的那一份还没做过任何一次编辑**（打开已做，保存这条路由现在通了，就差在包里的
副本上走一遍）；§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；§43 第 6 条那个设置存储的
矛盾；水印那两档干净量法；新文档开在第几页；就地保存。
**已经还上的**：§28/§29/§41 一路传下来的"另存框改名字"，外加撤回一条把"读不回来"误写成"填不进去"的
结论 —— 这两件事差得比听上去远得多。

## 45. 装上的那一份做完了一次编辑，一路到写出文件 —— 顺带量出"名字框"不是一个控件 id（2026-09-28）

1. **还的是 §43 记下那条**："装上的那份还没点过任何一次编辑"。这一片整条链跑在**装着的字节**上：
   `shell:AppsFolder` 起来（没有文件参数）→ `文件(F)` → `打开(O)…` → 点 `doc40.pdf` 那一行 →
   文档开起来（`doc40.pdf — XYZPDF`）→ `文档(D)` → `旋转页面` → 选 `当前页（第 1 页）` →
   `旋转这些页` → `保存修改为(S)…` → 原生框里 `WM_SETTEXT` 写名字 → **真点击按钮自己的矩形**。
   落盘 `renamed-094426.pdf`、12,666 字节；两个标签（`doc40.pdf *` 和新的那份）；状态行
   `已写成一个新文件（40 页）：向右转 90°（1 页）`；`source hash unchanged: True`。

2. **包里的应用不看 `XYZPDF_SETTINGS_PATH`。** 环境传不到由 shell 起的进程，所以 `-Language en`
   对它无效 —— 这一遍的界面是系统区域那份中文。脚本现在把这件事**打印出来**
   （`-Language en is NOT applied to a packaged run`），免得下次有人把中文读数当成设置没生效。

3. **"名字框"不是一个控件 id —— 这一条顺手解释 §28/§29 当年为什么吵。** 把打开框的子窗口全枚举一遍：
   保存框里名字框是 `Edit` id **1001**；**打开框里 id=1001 是地址栏 `ToolbarWindow32`**，真正的
   `Edit` 挂在 `ComboBoxEx32` 下面、id 是 **1148**。往那个字段里写全路径 + `BM_CLICK` id=1
   **打不开**（框还开着，读数里看得见），所以打开这条路仍然是 §29/§43 那条**点行**。
   （§28 说"名字框是跟 ComboBoxEx32 同一个 id 的那个 Edit"其实是对的，§29 反过来指认 1001 ——
   两个框各有一个答案，所以两边都"对"过一次。）

4. **包自己记目录。** 上一遍存到 `H:\tmp\num43`，这一遍打开框就落在那儿 —— 而那片 scratch 里的 PDF
   已经被上一次收尾删掉了，于是行列表是空的。所以想要确定性，先把样本放到那个目录，别指望它记得
   你上次是从哪儿开的。

5. **我们的框按类认，壳的框也按类认 —— 对称的一条。** 这一遍找旋转框用的是 `RotatePagesDialog`
   这个类名（语言无关），因为它的标题 `旋转页面` 是我们自己的可翻译串；§39 那条是另一半：
   壳的框按 `#32770` 认，别按标题（标题也是我们的串）。两条合起来才是完整的规矩。

6. **两条脚本自己的坑，都是同一类，而且这一片各撞了两次。** `'shell:AppsFolder\' + $AppId` 里那个
   反斜杠在用 python 生成文件时被吃掉（写出来是 `'shell:AppsFolder' + …`，启动就失败），改用
   `[char]92` 才不靠引号运气；`Say` 跨两行而第二行以 `+` 开头，PowerShell 解析器要的是**行尾**的
   运算符 —— 这个坑 §44 刚记过一次，这一片又踩了一遍，报 4 个错。

7. **数字。** 产品代码一行没动、目录一字未改、没构建、没跑门禁、**没重打包** —— 装着的还是 §43 那份
   `8102a1dd53e4…`，三处哈希仍然一致，这一片验的正是那一份。`tools/gui-probe.ps1` 多了 `-Installed`
   （走 `shell:AppsFolder`）、`NameField`（1001 / 1148 / 任意 Edit 的次序）、按类找我们的框、
   以及菜单行三次重试。跑出来的 PDF 都删了，样本 `doc40.pdf` 留在原处。

**仍然欠的**：§37 第 5 条那个"UIA 属性缓存何时失效"；§36 那三层往上；第 2、4 条合起来那个
"包到底把设置写在哪"（这一片又添一笔：它记目录，但不看环境变量）；水印那两档干净量法；
新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§43 那条"装上的那份没做过编辑" —— 现在从开到存整条都在**装着的字节**上走通过。

## 46. 两份文档带着同一个字段名撞在一起：qpdf 换的是**进来那一个**的名字，而我们的测试第一次问过它（2026-09-28）

1. **§36 记下往上三层里的第一层**："已经带字段的文档再合进一份带字段的供体 —— qpdf 那句话后半段
   （resolve conflicts from common resource and field names）我们一行都没问过"。这一片问了。
   造法跟 §36 同一套（不往 `tests/fixtures/` 放文件）：目标文档自己第一页挂一个 `/Widget`+`/Tx`
   字段，名叫 `donorAnswer`、值是 `typed in the target`；供体那份（§36 的 `writeAnnotatedPageCopy`）
   的字段**同名**、值是 `filled in the donor`。合并、保存，然后**从文件里读**（新加的两列读数：
   `AnnotationReport` 现在带 `fieldNames` / `fieldValues`）。

2. **量到的答案**：保存成功；`/AcroForm` 里 **2 个字段**；名字是 `donorAnswer` 与 **`donorAnswer+1`**
   —— 也就是说 qpdf 换的是**进来那一个**的名字，原来那份文档的字段**名字和值都没动**；
   两个值各自还在（`typed in the target` / `filled in the donor`）；`fieldsNamingNoPage=0`、
   `objectsSharedByTwoPages=0`、`notesNamingAnotherPage=0`；进来的那页照旧带 2 个注释。
   **所以这一层没有缺陷** —— 但"没有"现在是量出来的，不是猜的。

3. **钉的是哪两条**（这是这片要留的判据选择）：**不钉那个 `+1`**。qpdf 怎么起名是它内部的事
   （这一版是"给进来那个加后缀"），钉死字符串等于把 vcpkg 里那个版本号写进测试；钉的是读者能据以行事的
   两条性质 —— **原来那份的字段没被改**（名字还是它、值还是它），**进来的那个换了名但答案还在**。
   外加"两个名字不相等"这一条正面说碰撞。

4. **控制**（把 §36 那行修复改成永不成立）：红 **6 条**，其中 3 条是这一片新加的 ——
   `both fields survive the merge: 1 in the form list`、
   `and the two names are not the same, which is the collision this case is about: [donorAnswer] vs []`、
   `the answer that came in with the page is the one stored now ("")`。
   **读数里最该记住的一条**：没有修复的时候，撞名的那个字段**根本不在表单清单里**（fields=1，第二个名字是
   空串）—— 不是"两个同名共存"，是"进来的那份答案直接消失"。§36 那句"页上有注释不算证据"在这里的孪生版是
   **"字段还在不算证据，得问它是哪一个的名字、装的是哪一份的值"**。

5. **顺带量了真文件这一层能不能问。** `H:\pdf_test` 三份都数过：`CH340.PDF` `/AcroForm` 0 次、
   `/Annot` 2 次、`/Widget` 0 次；`VerilogHDL硬件描述语言.pdf` `/AcroForm` 3 次而 `/Widget` 0 次
   （有表单字典、没有字段）；`FPGA设计全流程.pdf` 三样全 0。**这台机器上没有一份真带字段的表单 PDF**，
   所以"真文件那一层"这一片还是没做，得先有一份能下的表单（嵌套字段名、可能带 `/XFA` 的那种）。

6. **数字。** 新增 12 处 `check()`（`documenteditor_smoke` 571 → 583），**产品代码一行没动**
   （`git diff` 对 `src/` 为空）；门禁 **26 passed / 0 failed**；目录一字未改（539 / 0 / 42 numerus）。
   dev 树 `xyzpdf.exe` 现在是 `d886958438be…`、1,277,952 字节 —— **和装着的那份（§43 的
   `8102a1dd53e4…`）哈希又不一样了，而产品源码等于 HEAD**：控制那一跑把它重链了两次，字节内容没变。
   这是 §30 那句话的又一次复现，也说明 §43 那次对上的"三处相同"只对到 §45 为止 —— 下一次重打包之前，
   装着的与构建树之间只能靠"源码相同"来论证，不能靠哈希。

**仍然欠的**：真带字段的表单 PDF（第 5 条）；`/Annots` 里被引用的注释（popup、批答链）；
§37 第 5 条那个"UIA 属性缓存何时失效"；§43 第 6 条那个设置存储的矛盾；水印那两档干净量法；
新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§36 往上三层里的第一层 —— 同名碰撞现在的行为（换进来那个的名、原字段不动、
答案都留着）第一次被读过，而且删掉修复会有 3 条检查替读者喊。

## 47. 注释互相指着的那一层：qpdf 让每份复制有了自己的注释，**却没把注释之间的引用改过来**（缺陷，钉住了没修）（2026-09-27→28）

1. **§36 往上三层的最后一层**：`/Annots` 里出现被引用的注释（popup、批答链）。qpdf 的头文件对
   `fixCopiedAnnotations` 只说两件事 —— "each separate copy of a page has private annotations" 和
   "resolve conflicts from common resource and field names"（§46 量过这条）—— **注释彼此之间的引用**
   它一个字没提。造的形状：一页上有一个便签，便签挂一个 `/Popup`（popup 不在 `/Annots` 里，只有它
   主人的 `/Popup` 指着它，而它自己的 `/Parent` 指回去），另有一条批答用 `/IRT` 指着那个便签。
   同一页合两次（§36 那条理由一样：一份复制看不出"共用"和"私有"的差别）。

2. **先验仪器再下结论。** 读数第一次给的是 `shared=1 wrongOwner=2 foreignTarget=2`，这足以喊缺陷，
   但"两个 obj id 相等"这种判据最容易是我自己比较错，所以把原始对象号打出来看：
   ```
   page40 annot@86 popup@92 popupParent@93      <- 第一份复制的便签
   page41 annot@90 popup@92 popupParent@93      <- 第二份复制的便签
   ```
   三件事当场定死：**便签本身是私有的**（86 与 90 是两个对象，qpdf 那句承诺是真的）；
   **两份复制的 `/Popup` 指着同一个对象 92**；**92 的 `/Parent` 是 93，既不是 86 也不是 90**。
   两条批答的 `/IRT` 同理指向不在本页的注解。（顺带：往批答上读 `/Popup` 会让 qpdf 往 stderr 打一条
   WARNING —— 我的调试打印没 guard，正式读数用的 `readLinkedAnnotations` 是 guard 过的。）

3. **这对读者意味着什么**：合并后的文档里，两页共用一个 popup 窗口对象，而批答链跨页相连 ——
   点开一条回复可能开到别页的线程，或者根本打不开。**页面渲染完全正常**，页数正常，注释数量正常，
   所以除了直接问这三条引用，没有任何别的测试会看见它。这正是 §36 那条"注释放过来了不算证据"的下一层：
   **每页有自己的注释对象，也不算证据；得问注释互相指着的还是不是对方。** qpdf 自己头文件里那句
   "While the page may look fine, it will probably not function properly with regard to interactive
   features" 说的就是这一类，而它接着写"未来版本会提供一个更不易出错的复制页面接口"。

4. **这一片没修，钉住了。** 修的两半不对称：popup 那一半是**局部可判定**的（每页给自己的注释复制一份
   popup、把 `/Parent` 指回这个注释，不需要任何映射）；批答那一半需要"供体的哪个注解变成了这份复制里的
   哪一个"的映射，而 `fixCopiedAnnotations` 不把这张表交回来。只做一半会留下一个既不是原样也不是修好的
   状态，所以先记成缺陷 + 两条钉：
   - 一条检查承诺还成立的部分（两份复制各自的便签/popup/批答对象都在，`/P` 都指对自己那页）；
   - 一条**故意把缺口钉住**：`shared=1 / wrongOwner=2 / foreignTarget=2`，消息里写着"KNOWN GAP …
     pinned so a fix reddens this on purpose"。将来无论是我们补的还是 qpdf 升级解决的，这条会红，
     红了就有人来把消息改掉 —— 缺口不会被静默地变成"以前就这样"。

5. **给读者的那句话还没定**（要他选）：合并带批答链的页时，要不要在合并框的小结里说一句"批答线程可能
   接不上"？还是先只做 popup 那一半（能修干净的那半），把批答当作已知限制？还是等 qpdf 那个新接口？
   三个都能辩护，但只有他能决定"读者该被告知多少"。

   （**§78 走的是第二条**：popup 那一半修了，而指着"这份文件里任何一页都不显示的注释"的 `/IRT` 摘掉。
   第 5 条问的那句"读者该被告知多少"**照旧等他定** —— 见本篇 §78 仍欠的第一条。）

6. **数字。** 新增 8 处 `check()`（`documenteditor_smoke` 583 → 591），**产品代码一行没动**（这一片是
   量与钉，不是修）；门禁 **26 passed / 0 failed**；目录一字未改；dev exe 仍是 `d886958438be…`
   （1,277,952 字节，11:54 那次链接 —— 只改测试文件不会重链应用本体，所以 §46 之后哈希没再动）。
   **没重打包**。

**仍然欠的**：这一片的修复本身（连同第 5 条那个选择）；真带字段的表单 PDF；§37 第 5 条那个"UIA 属性
缓存何时失效"；§43 第 6 条那个设置存储的矛盾；水印那两档干净量法；新文档开在第几页；就地保存；
下一次重打包。
**已经还上的**：§36 往上三层里的第三层被问过了 —— 答案不是"没事"，是一条有对象号证据的缺陷，
而且它现在会让下一次修复变红。

## 48. 装上的那份把设置存在哪儿：一条记了两片的矛盾，用一次前后对比关掉了（2026-09-28）

1. **欠的是 §43 第 6 条 + §45 那条**：一边是"打开框落的目录正是开发版那把键里的
   `lastOpenDirectory`"，一边是"打开之后开发版那把键的 `recent\documents` 里没有刚开的那份"。
   两句放一起就是"包里的应用到底用哪份设置"没定。

2. **做法**：不猜，跑一次前后对比。先把两处都数一遍（`HKCU:\Software\xyzpdf` 与包的那个
   AppContainer 存储根，128 行读数存文件），然后让**装着的**那份跑一整轮（开档 → 转一页 →
   另存成新名字），再数一遍。

3. **读数**：**注册表 128 行一字未变** —— 开发版那把键的 `files\lastOpenDirectory` 跑完还是
   `H:/test/XYZPDF/xyzpdf/tests/fixtures`，而那一轮明明存到 `H:\tmp\num43` 去了；
   `recent\documents` 也还是那 6 条，没有 `doc40.pdf`。同时，包的数据目录里
   `%LOCALAPPDATA%\Packages\allgoodhi.XYZPDF_edpkq8hzfe5n4\SystemAppData\Helium\User.dat`
   的写入时间正好落在这一轮的分钟上（13:11，`UserClasses.dat` 同一分钟，`Settings\settings.dat`
   还是 04-01 没动）。**所以包里的应用写的是它自己那份容器注册表（Helium 那个 hive），
   跟开发版根本不共用** —— 两句读数不再矛盾，两句都对：它确实持久化了，只是不在 HKCU 那把键上。

4. **一条留着的没定**：那次"打开框落在上一轮存文件的目录"，可能是应用自己的
   `lastOpenDirectory`（写在 Helium 里），也可能是壳给每个应用的对话框记忆
   （`ComDlg32\LastVisitedPidlMRU`）—— 两条路都能造出同样的观察，而**两者都不在开发版的设置里**，
   所以这一片要关的那个问题（"会不会脏了用户那份设置"）已经关了；剩下这一半不影响任何判断，
   记下来而不是硬猜。

5. **这条为什么值得记（三条实际后果）**：
   - 以后验装上的那份，**别拿 HKCU 那把键当它的状态**；要读要改得动 Helium hive（`reg load` 要提权），
     或者干脆走应用自己的界面。
   - 反过来这是好消息：**GUI 验证不会污染用户真实的设置**（那条老规矩"跑完记得把
     `lastOpenDirectory` 放回去"对包里的这份不适用，对开发版仍然适用）。
   - "装上的那份能不能看英文界面"这条路因此**不是**改设置能开到的 —— 但应用的 `Language` 菜单
     两种语言下都显示 `Language`（`languagedialog.cpp:15` 用的是 `QStringLiteral`），
     而 §42 之后菜单行是能点的。**下一片要验装上的那份的英文界面，走那条菜单，别走设置。**

6. **数字。** 产品代码一行没动、目录一字未改、没构建、没跑门禁（这一片是量，不改东西）；
   跑完写出来的那份 `renamed-131055.pdf` 删了，样本 `doc40.pdf` 留在原处；
   注册表两处快照留在 `H:\tmp\num46\reg-before.txt` / `reg-after.txt`（128 行，diff 为空）。
   **没重打包**（装的仍是 §43 那份）。

**仍然欠的**：装上的那份的**英文**界面（第 5 条给的路：`Language` 菜单）；§47 那条缺陷的修法（等他选）；
真带字段的表单 PDF；§37 第 5 条那个"UIA 属性缓存何时失效"；水印那两档干净量法；新文档开在第几页；
就地保存；下一次重打包。
**已经还上的**：§43/§45 那条关于"包把设置写在哪"的矛盾 —— 现在是量过的：**不共用，写在容器 hive 里**。

## 49. 装上的那份，英文界面在屏幕上读到了 —— 而这条路能走，是产品故意留的（2026-09-28）

1. **欠的是 §48 第 5 条点名的那一条**。§48 已经量过：改设置到不了装的那份（它写的是容器自己的
   hive）。到得了的地方是应用**自己的** `Language` 菜单，而这条路找得到的理由是产品故意的 ——
   `mainwindow.cpp:1030` 给那一条菜单标题用的是 `QStringLiteral("Language")`，不是 `tr()`，注释
   就写着："需要这一条的人正看着一份他读不懂的界面"。所以两种语言下它长得一模一样，是这份界面
   上唯一可以从屏幕外面点的东西。驱动收进 `tools/gui-probe.ps1` 的 `-Mode langswitch`
   （`-To` 说要哪一行，`-Keep` 不复原）。

2. **读数没有注册表这条路，只能让应用自己报**。这台机器上：包的那个 AppContainer 存储根整个数下来
   是两个子键、一条值（`ResourcesConfig\ManifestLanguagesList`），偏好本体在 Helium 的 `User.dat`
   里，容器外的 shell 打不开；`HKCU:\Software\xyzpdf` 是开发版的那把键，装的那份从不碰它（§48
   量过）。所以仪器是设置框里那颗语言按钮 —— 它印的正是
   `Languages::nativeName(AppSettings::languageCode())`（`src/settings/settingsdialog.cpp:254`）：
   **界面报自己的设置**。

3. **五屏读数**（同一份装着的 exe、同一条点击路径，日志 `H:\tmp\num49\lang3.txt`）：
   - 开头：按钮 `跟随系统`；菜单栏 `|文件(F)| |编辑(E)| |查看(V)| |文档(D)| |转到(G)| |窗口(N)|
     |Language| |帮助(H)|；`Language` 弹出三行，勾选 `跟随系统=On / 简体中文=Off / English=Off`。
   - 点 `English` → 弹出 `QMessageBox`，标题 `Language`、正文 `语言更改将在下次启动时生效。`
     —— **同一个窗口里两种字符串并存**：标题是不译的字面量，正文是我们的词条。这正是这一片的
     通道正对照：如果标题和正文一起变成英文，说明"英文"是从别处来的而不是目录。
   - **不重启**就读：那颗按钮已经是 `English`。写进存储了，界面按设计不当场改 —— 这句话
     `changeLanguage()` 的注释里写着（"当场重译会漏掉存在模型数据里的那些，重启不会半生效"），
     这一片是第一次在屏幕上同时读到"存储已改"和"界面未改"。
   - 杀掉重启：菜单栏 `|File| |Edit(E)| |View| |Document(D)| |Go| |Window(N)| |Language| |Help|`、
     状态行 `[Ready]`、设置框 `English / Clear the list / OK / Cancel`、勾选挪到 `English`
     （`Off/Off/On`）。**包里那份 exe 自带英文目录且真装载了**：同一句提示第二次是
     `The language change takes effect the next time the program starts.` —— §30/§34 说的"目录编在
     负载里"此前只在开发版上验过，这一次是在**装着的**那份上读到的。
   - 复原：点 `Follow the system`（英文界面里那一行是译过的，只有它 —— 语言行永远写自己的语言），
     重启后中文菜单栏回来，勾选回到 `跟随系统=On`。装的那份留在它原来的状态。

4. **对照**：`-To Deutsch`（日志 `control-deutsch.txt`）。还是那三行，报
   `nothing among the 3 rows is named [Deutsch]` 后退出，什么都没点。两条用处：第 3 条那句
   "点到 English"不是针脚写宽（行名是精确 `-eq`），以及**装着的那份提供的语言就是
   `Languages::available()` 该给的那些** —— 源语言 + 唯一编进来的那份 `.qm`
   （`CMakeLists.txt:1033` 只列了 `xyzpdf_en.ts`）。表里另外 13 种在装的那份里根本不出现，
   这一条此前只在源码里成立过。

5. **仪器自己的三条（第一遍就撞的，日志 `lang.txt` 就是那一遍）**：
   - **菜单栏的每一项对 UIA 都是 `ControlType.MenuItem`**。所以"打开菜单后把所有 MenuItem 打一遍"
     给的是 8（栏）+ 3（弹层）。第一遍把 8 行栏项当语言，然后又对同一个菜单调了第二次 `TopMenu`
     —— `TopMenu` 判断"行出来了"用的是**计数变大**，已开的菜单不可能再变大，于是它退回真点击，
     而那一下正好把菜单**关掉**了。报出来的是一句假话："没有名为 English 的行"，其实行在，
     是菜单被自己关了。现在 `LanguageRows` 减掉栏名、把行数组交给点的人，且不再对开着的菜单
     二次 `TopMenu`。
   - 可勾选的 `QAction` 到 UIA 是 **`TogglePattern`**（`toggle=On/Off`），"哪一行是活的"是**读**
     出来的。这条对读屏的人要紧：勾选记号是屏幕上唯一说"现在用的是这个"的记号，而它在第一遍
     就已经能分辨（一个 On 两个 Off），不是常量列。
   - `LanguageRows` 会把菜单**留着开**，所以顺序必须是先读设置框（那要走 `帮助` 菜单）再开
     `Language`；反过来，一个开着的弹层会让下一次点 `帮助` 看起来像菜单坏了。

6. **屏幕上看到的一条真缺陷（量出来，没动）**：英文菜单栏里 `Edit(E)`、`Document(D)`、`Window(N)`
   带着中文习惯的括号访问键，而 `File`、`View`、`Go`、`Help` 走的是英文习惯（`&File`）。数了整个
   目录：**539 条里 28 个英文形还带 `(&X)`**（`MainWindow` 26 + `PageView` 2；**全是普通形** ——
   numerus 那 84 个形里一个都没有；清单在 `H:\tmp\num49\mnemonic.txt`）。Qt 画的时候 `&` 只留下
   划线，括号照原样印出来，所以英文界面是真的写着 `Edit(E)` —— 那个括号是给中文界面用的脚手架，
   跟着译过来了。
   - **为什么现有那五条形状规则看不见**：集合对得上、`%n` 照有、两形照不同、单数没写成多数、
     段落换行照在。漂移在**措辞约定**里，不在形状里。
   - **修法不是机械替换，两条现成的证据**：访问键在同一个菜单里不能撞（`E&xit` 用的就是 X 不是 E），
     而且目录里已经有一行是**按语言换字母**的 —— `加密 / 去掉口令(&J)…` 的英文形写的是
     `Encrypt / Remove Password(&P)…`。所以两种界面并不在每个字母上都一致，改哪一行的字母是一次
     措辞决定，也会改到用户可能已经记住的按键。这条因此进"等他定"那一栏。
   - **这一片也不给它加检查规则**：一条有 28 个已知违例的规则会让门禁天天红，而把它写成"允许这
     28 个"就是给缺陷建一张表养着。规则要跟修复一起进来 —— 次序与 §39 那条换行规则相反，理由也在
     那一片里：那一条的修是当场改目录就完，这一条的修要人挑字母。

7. **数字与状态。** 产品代码一行没动、目录一字未改、没构建、没跑门禁（这一片是量）。
   `gui-probe.ps1` 加了 `langswitch` 与六个辅助（`MenuBarNames` / `LanguageLabel` / `LanguageRows`
   / `ClickLanguageRow` / `DismissNotice` / `Restart-ProbeApp`），复查是 0 个解析错、
   0 个非 ASCII 字节 —— **但这一片我又亲手踩了一次那条坑**：三回把中文写进这个 `.ps1`（文档串里
   两个词、注释里一个例子），ASCII 复查抓到的是前那两个（27 个字节），第三处是我自己读到的。
   也就是说这道闸不是每次都兜得住，写的时候就得当它不存在。
   三份日志一份清单留在 `H:\tmp\num49\`（`lang.txt` 是第 5 条那一遍失败，`lang3.txt` 是这一片引的
   那一遍，`control-deutsch.txt` 是对照，`mnemonic.txt` 是第 6 条那 28 行）。**没重打包**（装的仍是
   §43 那份），装的那份语言留在 `跟随系统`。
   - 还有一处差点没记下来：往 `PLAN.md` 里插那一段的脚本写成 `lines[:536] + ins + ['']`，
     于是 536 行**之后整段被截掉**（64 行，含"增量保存为什么划掉了"那一整节）。发现它不是靠读文件
     顺不顺，而是 `git diff --numstat` 报了"65 删除"—— 一篇只该加字的文档不该有删除。补法是拿
     `git show HEAD:docs/PLAN.md` 里那一段原样拼回去，再要求"对 HEAD 的删除行数 = 0"。
     **规则：脚本改文档之后，第一句问的是 git 删了几行，不是加了几行。**

**仍然欠的**：第 6 条那 28 个英文形的访问键（连同"改了会不会撞到别的行"的检查）；§47 那条缺陷的
修法（等他选）；真带字段的表单 PDF；§37 第 5 条那个"UIA 属性缓存何时失效"；水印那两档干净量法；
新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：装上的那份的**英文界面** —— §34 那一遍是开发版加一份 scratch ini 读的，这一片是
装着的那份自己在屏幕上读出英文菜单栏、英文设置框、英文提示正文，并且勾选记号跟着走。

## 50. 合并撞的是层级里的名字：qpdf 换的是**父那一段**，而两种走法说同一个地址（2026-09-28）

1. **欠的是 §46 之上那一层**，而这条债当时写窄了一句。§46 结尾写"机器上没有真带字段的表单 PDF，
   所以这一层问不了" —— 真文件带来的是各家生成器和 XFA，可"**字段的名字是几段拼出来的**"这件事
   不需要等真文件：用 QPDF 就能造一棵两层的树。所以这一片的第一条不是读数，是一条判断：**一条挂着
   "等外部条件"的债，拆开来看常常有一半现在就能问**。
   一个字段在 PDF 里的地址不是它的 `/T`，是从根到叶那串段名用点拼起来（PDF 2.0 §12.7.5.2），而且
   **只有叶子是页上的注释** —— 父字段不在 `/Annots` 里，除了表单列表没有别的东西指着它。这正是它和
   §46 那个平铺情形的分别：两份文档可以各有一个读者会叫 `form.answer` 的字段，而字符串 `answer`
   在每份里只出现一次。

2. **形状**（全部造在 scratch，不进 `tests/fixtures/`，理由同 §17/§30/§46）：目标自己第 1 页一个
   `form.answer` = "typed in the target"，供体第 1 页**同一个地址** = "filled in the donor"；
   `/Fields` 只挂父，父 `/Kids` 挂叶子，叶子的 `/Parent` 指回父。合并一次，写出，再从文件里读回来。

3. **读数**：`terminals=2 parents=2 addresses=[form.answer | form+1.answer] values=[typed in the
   target | filled in the donor] unregisteredWidgets=0 duplicateAddresses=0 brokenBackLinks=0
   parentChildMismatch=0`；而 `/Fields` 的条目变成 2 个：`[form | form+1]`。
   **qpdf 换的是父那一段** —— 整棵子树跟着走进来，所以两条限定名两边都不撞，进来那个的答案挂在自己的
   新地址下面。这一层**没有缺陷，现在是量过的而不是假设的**。
   为什么特意问"往下拼 vs 往上拼"两条：§47 的教训就是引用可以留在原来那份文件里。假如叶子的
   `/Parent` 还指着供体的 `form`，那么从 `/Fields` 往下拼会给出 `form+1.answer`，而从叶子往上拼会
   给出 `form.answer` —— 同一份表单里两个同址字段，而**只读一侧的自检永远看不见**，所以这一个块里
   两条各钉一个数（`brokenBackLinks` 与 `parentChildMismatch`），不合成一条。

4. **控制**（把 §36 那句 `if (fromHandle.hasKey("/Annots"))` 改成永不成立，跑完还原）：11 条红，
   其中这一片新加的 4 条。最要紧的一条读数是 `unregisteredWidgets=1` —— 没有那次修复，**页上的框还在，
   而这份文档的表单列表里没有它**，也就是"看得见、谁也读不回来"那个形状；这正是 §46 当年量到的
   "字段整个缺席"在层级里的样子。
   同时说清哪条**没**红：`parentChildMismatch/brokenBackLinks` 那条照旧绿 —— 供体那半棵树整个不见的时候，
   "两个方向一致"是**空真**。**一致性检查能被缺席满足**，所以这一块里"有没有少东西"归 `terminals==2` 和
   `unregisteredWidgets==0` 管，"对不对得上"归 `mismatch` 管，两条替不了彼此 —— §30 那句"那三条钉的是保存
   的卫生而不是这条分支"的同型。

5. **仪器上的一条小规则**：读 `/Fields` 的条目时不能直接 `getStringValue()`。层级里挂在 `/Fields` 上的
   通常是**父字段**，它没有 `/V`，而 qpdf 对类型不对的读法是**往 stderr 打一行 WARNING 再回空串**，
   于是测试输出里冒出三行"看着像文件坏了"的警告 —— 那是读的人粗鲁，不是文件坏。§46 那一版没撞到，
   因为它只喂过平铺字段（每条都有 `/V`）。现在两处都先 `isString()`。

6. **数字。** 新增 13 处 `check()`（`documenteditor_smoke` 591 → 604）；**产品代码 = HEAD 一字未动**
   （`git diff HEAD -- xyzpdf/src/` 为空：控制改过一行、当场还原）；门禁 **26 passed / 0 failed**；
   目录一字未改；dev exe `aa3e897bc0a9…`、1,277,952 字节、14:38 —— §47 记的 `d886958438be…` 被这一片
   两次控制的链接换掉了，字节数照旧不动而哈希动了，同一句 §30 的话。**没重打包**（§16 那"三处相同"
   仍只对到 §45）。

**仍然欠的**：带各家生成器痕迹的表单文件（XFA、`/TM` 别名、跨页分组 —— 这一片量的是树，不是各种写法）；
§47 那条缺陷的修法（等他选）；§49 那 28 个英文形的访问键（等他定）；§37 第 5 条那个"UIA 属性缓存何时失效"；
水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§46 结尾那句"这一层问不了" —— 限定名这一层现在问过了，而且是量过的。

## 51. 反面问了一次：页**出去**之后，表单列表里还留着什么（一条会跟着文件走出去的缺陷）（2026-09-28）

1. **欠的是从来没人问过的那一半**。§36 / §46 / §47 / §50 全在问"页**进来**时字段跟着怎么样"；把页
   **拿掉**这条路，因为 fixture 一个 `/AcroForm` 都没有，从来没被走到过。而读者的两条命令天天在走它：
   删除页面 拿掉字段所在那一页，拆分 写出若干份、其中只有一份含着那一页。字段是挂在根节点的
   `/AcroForm /Fields` 里的，它只是**指着**自己那页（`/P`），所以页出去完全可以留下一条指着"这份文件
   没有的页"的表项 —— §47 那族"引用被落在原来那份文件里"，这一次是我们自己的编辑造成的，不是 qpdf
   的复制造成的。

2. **五张读数**（`H:\tmp\num49\out-run3.txt` 里那几行 MEASURE out）：

   | 走的那条路 | 页数 | 页上还挂着几个 widget | `/Fields` 里还登记的字段 | 那个字段带的值 | `/P` 指着这份文件没有的页 |
   |---|---|---|---|---|---|
   | 删掉字段所在那页 | 39 | 0 | `onOne` 还在 | `typed on page 1` | **1** |
   | 拆分第 1 份 | 20 | 1 | `onOne` 在 | 同上 | 0 |
   | **拆分第 2 份** | 20 | **0** | **`onOne` 还在** | **同上** | **1** |
   | 控制：删的是别的面 | 39 | 1 | `onOne` 在 | 同上 | 0 |
   | 层级字段（`form.answer`）删所在页 | 39 | 0 | 父和叶都还在 | 同上 | **1** |

3. **所以这是一条真缺陷，而且它跟着文件走出去**。最要紧的是第三行：**拆分出来的每一份都带着那一页上
   填着的答案**，而拿不到那一页的那几份在页面上一个字都显示不出来。读者把一份填好的表单拆开发给不同的
   人，每个收件人的文件里都在 `/AcroForm` 里揣着别人那几页的内容 —— 页面上看不见，`/Fields` 里在。
   第一行是同一条的轻版本：删掉一页，撤销行说的是"删除 1 页"，而写出的那份还自称"我有一个叫 `onOne`
   的字段"。第五行说明层级不救你：父字段跨过叶子的页消失之后，照样登记在册（地址仍拼得出
   `form.answer`）。

4. **为什么这条读数不是我的读法造出来的**：控制那一行是同一份文件、同一个 reader、同一条读法，只把
   "出去的那一页"换成"留下的那一页"，`/P naming no page` 就从 1 变回 0。**能把 1 变成 0 的只有页出去
   这件事**，所以"字段还挂着、页没了"不是仪器瞎。这一条按 §47 的样子**钉住而不是当场修**（消息里写着
   `KNOWN GAP … pinned so a fix reddens this on purpose`），因为修法里含一个判断：
   - 能判定的是"这个终端字段在这份文件的任何一页上都没有 widget 了"（qpdf 自己就有这套件：
     `QPDFAcroFormDocumentHelper::getFormFieldsForPage()` / `getWidgetAnnotationsForPage()` 给真相，
     `removeFormFields()` 能把它从 `/AcroForm` 里摘掉，父字段空了再摘父）；
   - 判断不了的是"**该不该摘**"：PDF 允许一个没有 widget 的字段合法存在（值不在页上画、被脚本或
     默认值用着），而我们手上这条从文件上看不出它是哪一种。摘掉 = 删掉用户填过的值；不摘 = 让答案
     跟着拆分走到别人手里。所以三个出口都能辩护：**默默摘掉**、**摘掉并在小结里说一句**
     （"这份文件里没有那几个页上的字段了"）、**留着不动只在文档里写明**。他定。
   - 顺带一条不该顺手做的："把值清掉留字段"比两种都糟 —— 那是在用户没要求的地方销毁内容。

5. **钉了什么**（这一片 18 处新 `check()`，604 → 622）：页确实出去了（39 页、0 个 widget 在任何一页
   上）；控制那条（别的面被删 ⇒ `/P` 不悬空）；**两条故意钉住缺口的**（删页后仍登记 + 拆分那份仍带着
   答案）；层级那条（父跨过叶子的页仍在册）。合起来这块的分工与 §50 那句同型：**"有没有少东西"和
   "对不对得上"是两类检查**，而一致性检查能被缺席满足，所以这里没有拿"两条一致"去代替"值跑到哪儿去了"。

6. **数字。** 新增 18 处 `check()`（604 → 622）；**产品代码一行没动**（`git diff HEAD -- xyzpdf/src/` 为空 ——
   这一片是问与钉，不是修）；门禁 **26 passed / 0 failed**；目录一字未改；dev exe 仍是 §50 那次链接的
   `aa3e897bc0a9…`、1,277,952 字节（只改测试文件不重链应用本体，与 §47 那句同一件事）；
   日志在 `H:\tmp\num49\out-run3.txt`。**没重打包**。

**仍然欠的**：第 4 条那个选择（三个出口任选，选完才好修）；一个字段横跨两页、而这两页被拆分分到
不同文件里（这一片的字段都只有一页一个 widget，跨页那形还没造过）；带各家生成器痕迹的表单文件
（XFA、`/TM` 别名）；§47 那条缺陷的修法；§49 那 28 个英文形的访问键；§37 第 5 条那个 UIA 缓存问题；
水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§46/§50 那条梯子的反面 —— "页出去"这条路第一次被走到，而且是量过的。

## 52. 一个字段的框横跨两页，而拆分把它们分开了：这一形把 §51 的修法限死了一半（2026-09-28）

1. **欠的是 §51 结尾自己点名的那一形** —— 前面所有测试里的字段都是"一页一个 widget"。这一次造的是
   Adobe 自己那种：**登记在册的字段没有 `/Rect`、不在任何页上**，它的 `/Kids` 是两个纯 widget 注释
   （各有 `/P` 与 `/Parent`，各自**没有 `/T`**）—— 名字说一次，框画两处（PDF 2.0 §12.7.5.2）。
   值得专门造的原因是：**这一形让"把没页的字段摘掉"这条修法变成错的**。

2. **仪器先被这一形改了三次**，每一次都是"旧读法会把合法形状读成缺陷"或者"读不出要看的东西"：
   - **地址**：纯 widget 没有自己的段名，往下拼时不能添那个点（否则每个跨页字段都读出 `spread.`）；
   - **"重名"不等于"撞名"**：§50 那条 `duplicateAddresses` 数的是"同一个地址出现两次"，在这一形上会把
     **合法**的跨页字段报成冲突。现在按**段主**比：地址相同而拥有这个名字的字段对象不同才算撞
     （`terminalOwners`：带 `/T` 的算它自己，纯 widget 算它的父）。撞名与跨页是两件事，之前那条规则
     把它们混在一起了 —— 是新形状把老规则的边界逼出来的；
   - **值在字段上、不在框上**，所以往下走必须**带着** `/V`。不然"这份文件带着别人那页的答案"这句话
     根本读不出来（§51 那句是从平铺字段读到的，因为平铺字段的框和值在同一个对象上；跨页这形会整个漏掉）。

3. **读数**（`H:\tmp\num49\spread-run2.txt` 里那几行 MEASURE spread）：

   | 走的那条路 | 页数 | 自己页上真有框 | `/Kids` 里的终端 | 带着的值 | 指着这份文件没有的页 |
   |---|---|---|---|---|---|
   | 拆分第 1 份（页 1–20） | 20 | 1 | 2 | `typed across two pages` ×2 | **1** |
   | 拆分第 2 份（页 21–40） | 20 | 1 | 2 | 同上 ×2 | **1** |
   | 删掉其中一页 | 39 | 1 | 2 | 同上 ×2 | **1** |

   两句一起说：**两份都拿到了整个字段**（各自页面上确实有一个画得出来的框），也都拿到了**对方那一页的
   widget 对象** —— 连带它的 `/Rect` 和 `/P`，被 `/Kids` 引用着，所以是真的写进了这两份文件。删页是同一形。

4. **对 §51 那个选择的影响**（这一片最实在的产出）：能钉死的不是"摘不摘"，而是"**摘什么**" ——
   - 字段**必须留在两份里**（每份都有活的框），所以 §51 三个出口里"默默把没页的字段摘掉"这一条
     **在跨页字段上是错的**，而这一片的钉会在有人那样改的时候红；
   - 可摘的是那个**属于对方页的 widget**（它在这份文件里既画不出、也没有页可指）；值不能摘（那是用户
     输入的，而且这一形里字段是活的）。
   于是修法的形状收窄成一句话：**把 `/Kids` 里那些"在本文件无页可指"的 widget 摘掉，字段本身与本文件
   有框者一律保留**。三条读数的钉已经写在这里，真去改的时候红哪一条一目了然。
   仍然等他选的没有变：**要不要动手**，以及动了要不要在小结里说一句。
   （另记一条不该顺手做的，与 §51 那条同型：顺手把"重复的值"去掉也是销毁内容 —— 这一形里值本来就只有一份，
   是两处显示。）

5. **数字。** 新增 9 处 `check()`（622 → 631）；**产品代码一行没动**（`git diff HEAD -- xyzpdf/src/` 仍为空）；
   门禁 **26 passed / 0 failed**；目录一字未改；dev exe 仍是 §50 那次的 `aa3e897bc0a9…`、1,277,952 字节、
   mtime 14:38 —— 连着三片只改测试，应用本体没重链，这与 §47/§51 那句是同一件事。**没重打包**。

**仍然欠的**：§51 那个选择（这一片把它的形状收窄了，"要不要动"还是他的）；`/TM` 别名与 XFA 那份真表单；
§47 那条缺陷的修法；§49 那 28 个英文形的访问键；§37 第 5 条那个 UIA 缓存问题；水印那两档干净量法；
新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§51 点名的跨页那一形 —— 造出来了、量了、钉了，而且它反过来把修法的可能性关掉了半扇门。

## 53. 名字的最后两个边缘：别名 `/TM` 与"根本没有名字"（2026-09-28）

1. **欠的是命名规则自己剩下的两个形状**。§50 问的是"两个**名字**撞了改谁"，§52 问的是"一个名字画两
   处时地址是什么"。剩下的两条是：一个字段的名字**不是** `/T` 里那个串 —— `/TM` 别名（PDF 2.0 表单
   规范加的，限定名照旧由 `/T` 拼），以及**压根没有名字**（合法，`/TM` 存在之后空段名是被允许的）。
   这两个都要问，因为它们各自都能让我自己的撞名计数器读错，而"同一个名字两次"其实有第三种意思。

2. **三张读数**（`H:\tmp\num49\alias-run.txt`）：
   - 目标 `/T alpha /TM beta`，供体 `/T beta`（无别名）→ 合并后 `addresses=[alpha|beta]`、
     `aliases=[beta|]`、`collisions=0`、两个值都在。**没改名**：一个字段的名与另一个字段的别名同串，
     不算撞 —— 修复只按 `/T` 判。目标那侧的别名跟着文件活了一整轮（`alpha` 仍带着 `beta` 这个别名），
     而 Qt PDF 打得开这份（页数 41 读回来自 Qt 侧，不是 QPDF 自证）。
   - 目标 `/T alpha /TM beta`，供体 `/T gamma /TM beta` → `addresses=[alpha|gamma]`、
     `aliases=[beta|beta]`、`collisions=0`：**两份别名为同一串、什么都不改**。这条记成"量过的已知"，
     不钉成缺陷：会用 `/TM` 寻址的是 XFA 一路的查看器，而**这份产品两种名都不画**（v1 没有表单 UI），
     所以它没有读者可见的后果；写在这里是因为将来接 XFA/导出时要有人记得这个洞。
   - 两个都没有 `/T` 的顶层字段 → `terminals=2 nameless=2 addresses=[|]`、`collisions=0`、
     `unregisteredWidgets=0`：**空地址不是地址**，所以两个无名字段不撞。

3. **仪器这一片被改了两次，而且两次是**一对**，这才是要记的**：
   - §52 留下的段主键（`terminalOwners`：带 `/T` 的算自己，纯 widget 算它的父）在**无名顶层字段**上
     会落到哨兵 `-1`，于是两个不同的无名段字段看起来是"同一个主"—— 读数是**对的**，但是**靠事故**；
   - 把哨兵换掉（无父时用字段自己的对象号）之后，这两个字段的段主变成两个不同 id，而它们的"地址"
     都是空串 —— **这时老的碰撞规则会把合法文件报成撞名**。所以第二步是：地址为空的终端**不进**碰撞
     计数。
   - **两次必须一起做**：只做前一次会把一个合法形状读成缺陷；只做后一次则把"靠事故读对"留在原地。
     这条正好是 §52 那句"老规则遇到没造过的合法形状会把合法读成缺陷"的具体一课，而且它多教了一句：
     **修事故时会暴露出规则本身缺的那半边**。

4. **数字。** 新增 13 处 `check()`（631 → 644）；`FieldTreeReport` 多了 `aliases` 与 `namelessTerminals`；
   **产品代码一行没动**（`git diff HEAD -- xyzpdf/src/` 仍为空）；门禁 **26 passed / 0 failed**；目录一字未改；
   dev exe 仍是 §50 那次的 `aa3e897bc0a9…`/1,277,952（**连着四片只改测试**）；日志在 `H:\tmp\num49\alias-run.txt`。
   **没重打包**。
   过程里编译错四处（一个变量重名 `owner`、三处 `const QPDFObjectHandle` 上调 `replaceKey` —— 那个 API
   是非 const 的，§47 那句"replaceKey 非 const"记过一次，这一片又在**新建的句柄变量**上踩了一遍），
   都在第一轮构建就报出来，没进入读数。

**仍然欠的**：`/XFA` 那一串（这一片只碰了 `/TM`，XFA 流本身还没造过也没测过）；§51 那个选择（§52 已把
形状收窄）；§47 那条缺陷的修法；§49 那 28 个英文形的访问键；§37 第 5 条那个 UIA 缓存问题；水印那两档
干净量法；新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：命名这一族剩下的两个边缘形状 —— 别名按 `/T` 判、空名不算地址，两条现在都有钉。

## 54. 第一次用眼睛看一个带表单字段的页面：我们这份阅读器**什么都不画**（2026-09-28）

1. **欠的是"有没有人看过"这一条**。§46 到 §53 全在读文件里的字典，一句关于屏幕的话都没说过；而
   §46 那句"这台机器上没有真带字段的 PDF"从那时起一直没被拆穿 —— 它说的是 fixture，不是定律。
   所以这一片先造工具再造读数：`documenteditor_smoke` 里加了一个**样本生成器**（环境变量
   `XYZPDF_FORM_SAMPLE_DIR=<目录>` 才动；目录名里带 `fixtures` 直接拒绝并算失败，理由 §17/§30 那句：
   那个目录是被别的测试 glob 的），写出四份：`plain.pdf`、`field-flat.pdf`（第一页一个字段带值）、
   `field-spread.pdf`（§52 那一形）、`field-dangling.pdf`（§51 那一形：页没了、字段还在册）。
   顺手把第一页的 MediaBox 打成读数，因为"屏幕上没画"这句话必须说得出**在哪个矩形上没画**。

2. **新的屏幕模式**：`tools/gui-probe.ps1 -Mode formpage` —— 开窗、用标题栏真点击把窗口抬起来
   （`SetForegroundWindow` 在这个 harness 里不动它，§27 那条）、挑页面元素、按 DPI 换算、裁下来、
   每 12 条横带各数一次非白点。**先打候选再挑**是硬要求，理由见第 4 条。

3. **几何先摆好**：`SAMPLE page1 mediabox: 0 0 595 842`，页面按 fit-width 画在 1240 px 宽的窗口里 ⇒
   2.084 px/pt（§28 量的 2.057 同一档）；字段框在 PDF 里是 `60,600 – 260,630` pt，落到屏幕上就是
   **x 125–542、y 442–504**（页顶对齐视口顶），完全在看得见的那块里 —— 这一条先立住，否则"没画"
   有可能只是"没滚到"。

4. **读数，以及第一条读数是怎么假的**：
   - 第一遍 A/B 报 **681 个像素差，bbox x15–119 y3–27**。那个位置不是页面 —— 是我挑的那块 Pane
     是**外层**那块（1240x756，从工具栏下方起算，顶端 36 行是标签条），而那 36 行里印的是**文件名**。
     两份文档标签不同，于是"字段有没有被画"读成了"有 681 个像素被画"。**是差值的位置出卖了它，
     不是差值的大小**：所以模式现在既打印每一个候选的矩形，也改成"在面积 ≥ 最大 60% 的候选里挑
     顶边最低的那块"（页面视图是嵌套 Pane 里最靠内的那块）。
   - 修完之后同一条 A/B：**整块页面区域 0 个像素差**，两张 PNG **SHA-256 相同**
     （`473abc153a29…`），字段落点那块（x 100–560、y 430–520）也是 **0**。
     两份日志各自的窗口标题分别写着 `plain.pdf — XYZPDF` 与 `field-flat.pdf — XYZPDF`，
     所以这不是"两次截了同一张图"。
   - **所以：一个 `/Widget` 在我们这份阅读器里没有任何像素** —— 没有边框、没有底色、也没有把
     `/V` 里那句话显示出来。第 3 条的几何保证了这个"没有"说的是那个矩形，而不是屏幕外。

5. **这条读数回手把 §51/§52 的意义钉准了**（这是它最实在的地方）：那条会跟着文件走出去的答案，
   **在源文件上读者也看不见** —— 有页的时候不画，没页的时候同样不画。于是 §51 那个缺陷的
   伤害面**只在"我们写出去的那份文件"上**，不在"我们显示的那一页"上：拿我们的阅读器看一份填好的
   表单，看不出它被填过；把它拆开发给不同的人，每个文件都带着别人那页的答案。v1 只读、不做表单 UI
   是他的决定，所以今天这条是"按设计"，但它现在是**量过的按设计**，不是猜的。
   另有一句不该顺手做的也记下来：给字段画个框、或者把 `/V` 显示出来，都是把"阅读器"变成"编辑器"
   的第一步，等他要不要。

6. **踩到的两处仪器坑**（都是老规矩在救场）：
   - 三处编译错（`getMediaBox` 在 `QPDFPageObjectHelper` 上不在 `QPDFObjectHandle` 上；那条 helper
     自己也要**非 const** 才能调 —— §47 记过的"这些 API 是非 const 的"，这一片又踩了两回），
     以及**中间一次构建失败之后我照样跑了测试 exe**：那一次的 SAMPLE 输出是**旧二进制**印的
     （少了 mediabox 那行才发现）。门禁没被消音，所以没有读数建立在那次之上 —— §26 那条老规矩，
     第 N 次生效。
   - `formpage` 一开始把截图固定存成 `formpage.png`，第二次跑就把第一次的证据吃掉了。改成按文档名
     存（`formpage-<doc>.png`），一张 A/B 才留得下两张图。

7. **数字。** **没有新增 `check()`**（644 一字未动 —— 这一片是生成器加一次看，不是断言）；
   `documenteditor_smoke` 多了一段受环境变量控制、默认不执行的样本写出；`gui-probe.ps1` 到**八种模式**
   （`formpage` 当场跑通三轮），复查 0 个非 ASCII 字节、0 个解析错 —— 以及这一片**我又写进去两个
   `§`**（0xC2 是 GBK 前导字节，这次连累注释后面的解析报出 6 个解析错），ASCII 复查抓到、改成
   `sec.` 才干净；那条坑是第三次亲手踩。**产品代码一行没动**（`git diff HEAD -- xyzpdf/src/` 为空），
   门禁 **26 passed / 0 failed**，目录一字未改，dev exe 仍是 §50 的 `aa3e897bc0a9…`/1,277,952
   （连着五片只改测试与工具）；四份样本、两张 PNG 与日志在 `H:\tmp\num49\samples\` /
   `H:\tmp\num49\*2.png` / `form-*.txt`。**没重打包**。

**仍然欠的**：屏幕上要不要给表单一点痕迹（第 5 条，等他）；§51/§52 那个修不修（形状已被 §52 收窄）；
XFA 流本身；§47 的修法；§49 那 28 个访问键；§37 第 5 条那个 UIA 缓存问题；水印那两档干净量法；
新文档开在第几页；就地保存；下一次重打包。
**已经还上的**：§46 从那时起挂着的"没有真带字段的 PDF" —— 现在有一台确定性的生成器，而且第一次
有人在屏幕上看过一个带字段的页面。

## 55. XFA：表单的第二份副本，我们的写路径一眼都不看（2026-09-28）

1. **欠的是 §53/§54 都点了名没碰的那一层**。一份文档可以有**两份**表单：字典里的 `/AcroForm /Fields`，
   和 `/AcroForm /XFA` 里那包 XML（(子类型, 流) 的成对数组）—— 认它的那些查看器把 XML 当作表单本体。
   qpdf 没有 XFA 的任何设施（它所有头文件里连 `XFA` 这个字都没出现），所以这个问题缩小成一句更要紧的：
   **我们这条写路径到底碰不碰它**。样本是手造的最小包（一个说"有一页 P1"的 template，一个带着
   `<xfaField>typed on page 1</xfaField>` 的 datasets）—— 它不是任何生成器的输出，这一点写在注释里，
   这里要问的也不是包的内容质量，而是**字节跟着走不走**。

2. **四个方向的读数**（`H:\tmp\num49\xfa-run3.txt`，285 字节是模板 + datasets 两段合起来的长度）：

   | 走的那条路 | 包里还有没有东西 | 还写着 P1 吗 | 还带着那句答案吗 | 字典那边 |
   |---|---|---|---|---|
   | 删掉字段所在那页（39 页） | **在，2 对 / 285 字节** | **是** | **是** | 字段悬空（§51） |
   | 拆分第 1 份（20 页） | 在，2 对 / 285 字节 | 是 | 是 | 有它自己的框 |
   | 拆分第 2 份（20 页） | 在，2 对 / 285 字节，**与源那份逐字节相同** | 是 | 是 | 只有它自己那半页 |
   | 把一个普通字段合进来 | 在，2 对 / 285 字节 | 是 | 是 | **2 个字段**，XML 里不提新来的那个 |
   | 把带包的那页合到一份干净文档里 | **不在**（present=0） | — | — | 1 个字段跟页来了，包没来 |

   合起来是三件事，都钉住了：**(a) 删页/拆分把整份 XML 原样带走** —— §51 那条"答案走到别人手里"在
   XFA 这一层不只是答案，是**整份表单的定义加上每一个字段值**，而且两份 piece 拿到的是同一份完整副本；
   **(b) 字段可以进来而包里永远不提它** —— 于是"这份文档的两份表单"从此是两个不同的表单，而 §54 量过
   我们这边一页纸上都不画，谁也不会察觉；**(c) 包不属于页** —— 把带包的页合出去，页和它的字段来了，
   `/XFA` 没来（它挂在文档根上），所以"合入一页表单页"得到的是一个字典有字段、XML 不知道的形状。
   Qt PDF 照样打得开那份两份表单互相不一致的文件（页数从 Qt 侧读回）。

3. **这一片自己栽的两个仪器坑，都记进代码注释而不是抹掉**：
   - `QPDFObjectHandle::newStream(&qpdf, data)` 返回的是**流对象，不是字典**：在它上面 `replaceKey`
     被 qpdf 回一句 `operation for dictionary attempted on object of type stream: ignoring key
     replacement request` 然后**照样写出**，`/Subtype` 就悄悄没了 —— 键要走 `getDict()`。
   - 第二版想省事：`writeFieldCopy` 先写一份，再开第二遍加包、**写回同一个文件名**。qpdf 读对象是懒的、
     文件句柄一直开着，所以原地覆盖正是它文档里警告的那件事：结果是**一句错都不报、字段在而包不在**
     的文件，我照着它写了半篇"我们的写路径丢掉 XFA"的结论。改成"写副名 + 换名"只让这次的失败**变响**
     （`could not move the packet-carrying file into place` —— Windows 上那份文件还被读着，删不掉），
     正解是**一趟过**：从源打开、把字段和包一起加上、写目标名。
   - 这两条合起来又一次是同一句话：**报"没有"之前先证明仪器看得见"有"**（§51 那条控制、§54 那个标签条、
     这一片的空包，三个星期里的同一课）。

4. **为什么不当场修**：XFA 的一致性不是一句"顺手摘掉"能解决的 —— 要么**改写 XML**（我们既没有解析器
   也不该有一个），要么**在写出去的文件里去掉 `/XFA`**（那是销毁表单定义， Acrobat 类查看器会从
   "显示旧表单"变成"显示字典里那份表单"，是个行为变更而不是修 bug），要么**在检测到 `/XFA` 时拒绝编辑
   或提前说明**。三条都是产品决定，进"等他定"那一栏；而 §51 那个选择现在多了一条相关事实：**如果
   将来真要摘，XFA 那份必须一起考虑，否则摘掉字典的悬空字段而 XML 原样留着，不一致会更难看见。**

5. **数字。** 新增 12 处 `check()`（644 → 656），样本生成器多出一份 `field-xfa.pdf`；**产品代码一行没动**
   （`git diff HEAD -- xyzpdf/src/` 为空）；门禁 **26 passed / 0 failed**，手工跑那份测试 **0 个 FAIL、0 个
   WARNING**；目录一字未改；**没重打包**（这一片不进负载）。

## 56. §37 那句"UIA 客户端缓存"是没测过的归因 —— 四路同刻一起读，它不成立（2026-09-28）

1. **欠的是 §37 第 5 条**。那一层当时写下的是："状态条那句话的 `Name` 会比屏幕上画的晚（.NET UIA 客户端
   的属性缓存）"，并且留了一句"为什么一轮 7 秒刷新、另一轮 20 秒前的旧话握着 24 秒"。这次不再猜，
   把**同一瞬间**分四路读：
   - `held` —— 变化之前抓住的那个元素，每次采样重读 `.Current.Name`（缓存要藏就藏在这儿）；
   - `fresh` —— 每次从窗口元素重新找一个 StatusBar（本套件所有模式一直用的就是这个）；
   - `root` —— 每次从桌面根节点全新搜一遍（元素的缓存没地方躲）；
   - `ink` —— 真相：把消息那一块从屏幕上裁下来数非白点。
   收进 `tools/gui-probe.ps1 -Mode uacache`。

2. **头两轮什么都没量到，原因是路走错了**：第一版照着 `exportstatus` 走"导出 → 选文件夹"。结果是
   `files written: 0`、三条 `Name` 全空、`ink` 在 760/805 之间跳 —— 因为**模态框还盖着状态条**：
   裁到的那块是**对话框的下缘**，不是消息区；而应用在模态框开着的时候本来就没有 showMessage。
   改了两次仍然不对，于是把这条整个换掉：**用应用自己发的那句话**（开档时的 `Opened <名字>`），
   不需要框、不需要写文件、不需要原生选择器 —— 一句话怎么开始、怎么自己消失，正是这个问题要的。

3. **读数（两轮 × 44 个采样，间隔 0.5 秒，`H:\tmp\num56\launch1.txt` / `launch2.txt`）**：两轮逐行相同 ——

   ```
   t=2.2s ink=1733 held=[Opened doc40.pdf] fresh=[Opened doc40.pdf] root=[Opened doc40.pdf]
   t=2.8s ink=1588 held=[]                  fresh=[]                  root=[]
   ```

   **四条通道在每一个采样上都一致，包括那句话消失的那一次。** 也就是说：**被抓住的元素重读并不滞后**，
   与重新找到的、从根搜到的、以及像素全对得上。**所以 §37 那句"是 .NET UIA 客户端的属性缓存"是
   没根据的归因，这一片把它撤掉。**（那一轮 24 秒的读数还剩一个候选，而且在我们这边：它的 `ink`
   是从"消息区"裁的，而那个矩形是从状态条元素算出来的 —— 窗口动过或被子元素挡过，裁到的就不是那句话。
   不再追，等它再出现。）

4. **顺带把"时长为什么两轮不一样"结掉了，而且答案在产品里**：`已打开 %1` 用的是 **4000 ms**
   （`mainwindow.cpp:1501`），`已导出 %n 张图片到 %1` 用的是 **6000 ms**（`mainwindow.cpp:1398-1399`）。
   §37 量到"6.3 秒还在、7.0 秒两路一起空掉"的是**导出**那句；这次"2.2 还在、2.8 没了"的是**开档**
   那句，而采样时钟是在"窗口出现 + 抬窗口 + 睡 0.6 秒"之后才起的，把那段补回去就是 4 秒 —— **两句
   都对，是两个不同的超时值**。规则跟着来一句：**跨轮比时间之前，先确认这一轮触发的到底是哪句
   话**；这个应用的状态条超时不是一个常数，是两个。
   另一条对得上号的数：`ink` 从 1733 掉到 **1588** —— 1588 正是 §37 量过的"空消息区"底噪（页面内容
   渗进裁条里），所以这次"消失"也是两路一起证的，不是单看 `Name`。

5. **数字。** 产品代码一行没动（只读了两行时间常数）；`gui-probe.ps1` 多一种模式（九种），新模式的
   注释里写着"为什么不能用导出那条路测这个"；纯 ASCII 复查 0 个非 ASCII 字节、解析 0 错。**这一片
   自己也踩了一次大的**：用脚本改这个 `.ps1` 时先以 `'w'` 打开（立刻截断）再抛异常，**文件被清成 0 字节**
   —— 是 `git diff --numstat` 报的 `0 加 / 1001 删` 暴露的（§49 立的那条规矩第一次真救回来），
   `git checkout --` 从 HEAD 还原，重贴新模式，逐次用字节计数核对落地。**教训不只是"改完问删了几行"，
   还有"脚本改文件不如 Edit 改文件"** —— 后者显示 diff 且不会把没写成的中间态留在盘上。
   门禁没跑（没有代码改动，`git diff HEAD -- xyzpdf/src/` 为空），**没重打包**，日志在
   `H:\tmp\num56\`（`launch1.txt` / `launch2.txt` 是被引的那两轮，`run1.txt` / `run2.txt` 是走错路的
   那两轮，留着是因为它们记的是"怎么不成立"）。

**仍然欠的**：§47 那条缺陷的修法（等他选）；§51+§52 的那个选择（形状已被 §52 收窄）；§55 的 XFA 三选一；
§49 那 28 个英文形的访问键；带各家生成器痕迹的表单文件（XFA 流手造过了，真的没有）；§48 留的那半句
（打开框记的目录是应用自己的还是壳的对话框记忆）；水印那两档干净量法；新文档开在第几页；就地保存；
下一次重打包；§56 的提交。
**已经还上的**：§37 第 5 条那个"UIA 属性缓存何时失效" —— **缓存这个说法本身不成立**，剩下的只是
"那一轮的裁矩形可能不是消息区"这一条候选，而它不需要现在追。

## 57. 字段带着自己的画：§54 那句"零像素"过了正对照，而 §51 漏的东西里多了一样（2026-09-28→29）

1. **这一片是对自己上一片做的对照**。§54 量的是"一个 `/Widget` 在我们这份阅读器里 0 像素"，但它的
   widget 只有 `/Rect`、`/T`、`/V` —— **没有 `/AP` 外观流**，也就没有"可画的东西"。真实表单不是这个形状：
   谁在 Acrobat 里填过一份表，那个框就是靠 `/AP /N` 那个小 Form XObject 画出来的（底色、边框、连字）。
   所以 §54 那句话可能属于一个没人发布过的形状。补上：造一份带 `/AP /N` 的（内容流自己画
   `0.70 0.70 0.90 rg 0 0 200 30 re f` 加一句 `APPEARS-HERE`），再问同样的问题。

2. **文件那一层，三个方向都量了、都钉了**（`H:\tmp\num57\run.txt`）：

   | 走的那条路 | 注册的字段 | 带着那份画（解得开） | 备注 |
   |---|---|---|---|
   | 建出来（第一页） | 1 | 1 | setup |
   | 把那一页**合进来** | 1 | 1 | 外观整份跟着页复制过来，不是只剩值 |
   | **删掉那一页** | 1 | **1** | §51 那条悬空现在多了一项：跟着走的是**一个画得出来的盒子**，而它要画的那页不在这份文件里 |
   | 拆分（每份 20 页） | 两份都 1 | **两份都 1** | 第 2 份没有任何一页能让它画 |

   钉法照 §51/§55：读 `/AP /N` 的**解码字节**里那句 `APPEARS-HERE` 在不在，不只看键在不在 ——
   "字节带过来了"和"还画得出来"是两件事（§26 那条 `/Contents` 的旧账同一个分法）。

3. **屏幕那一层：§54 的结论活着，而且变强了。** 同一个 `-Mode formpage`，同样的两块 1240x720 裁条
   （`plain.pdf` 与 `field-appearance.pdf`，第一页，fit-width）：
   **整页区域 0 个像素差**，字段落点那块（x 125–542、y 442–504 pt→px）**也是 0**，两张 PNG 的
   SHA-256 相同（`473abc153a29…`，与 §54 那一对**同一个值** —— 说明这条通道两次跑都落在同一处几何上）。
   **所以"不画"不是因为没东西可画**：这个字段把自己要画什么都带进来了，我们这份阅读器仍然一个字节的
   差别都不产生。换句话说，它没去问注释层要外观，而不只是"这次没外观可问"。
   这条通道能看见差值是被证过的：§54 那 681 个像素的假阳性（标签条）、§28 那 22,476 个像素的水印差，
   都是它报出来的 —— 这次它报 0，报的是"0"。

4. **对那几条已经钉住的缺陷，多出一条要说清的事实**：§51 的"答案走到别人手里"、§55 的"整包 XFA 跟着走"，
   现在再加上"**那个框的样子也跟着走**"。三者对我们的屏幕都没有症状（§54/§57 都是 0 像素），但对外部
   接收方是同一句话的两半：**文件里带着它已经不含的那一页的表单，连视觉都在**。这把 §51 那个选择里
   "默默摘掉"那一档的理由写得更实在了 —— 摘的不只是一个空字典项，还有一份没人会去画的 XObject。
   等他定的那三件事（§47、§51+§52、§55）一件都没变，只是各多了一条量到的事实。

5. **数字。** 新增 16 处 `check()`（656 → 672），样本生成器多出 `field-appearance.pdf`（AGENTS.md 那行
   跟着改）；**产品代码一行没动**（`git diff HEAD -- xyzpdf/src/` 为空）；门禁 **26 passed / 0 failed**，
   手工跑 **0 FAIL / 0 WARNING**；目录一字未改；**没重打包**；dev exe 仍是 §50 的
   `aa3e897bc0a9…`/1,277,952（连着七片只改测试与工具）。日志与两张 PNG 在 `H:\tmp\num57\`。

**仍然欠的**：§47 的修法、§51+§52 的那个选择、§55 的 XFA 三选一、§49 那 28 个访问键、§54 的"要不要给表单
一点痕迹"（§57 让它更值得想了：不是"画不画框"，是"要不要用 Qt 的注释外观层"）；真带各家生成器痕迹的表单
文件；§48 留的那半句；水印那两档干净量法；新文档开在第几页；就地保存；下一次重打包；§56 与 §57 的提交。
**已经还上的**：§54 那句结论的正对照 —— 它现在不再依赖"我造的形状刚好没外观"。

## 58. 那张画站在什么上面：指令层（/DA /DR /Q /NeedAppearances）跟着页走过来了，而我带进去的假设第一轮就被否掉（2026-09-30）

1. **这一片问的是 §57 那份画的下一层**。一个框要画出字来，得有人说出用什么字体、多大、什么颜色、
   怎么对齐 —— 真实表单把这些写在**文档级** `/AcroForm` 上：`/DA` 是一串内容流算子（字段自己不写就
   继承它，PDF 2.0 §12.7.3.5）、`/DR` 是那串算子可以点名的资源、`/Q` 是对齐；而 `/NeedAppearances`
   干脆是一句对阅读器说的话："这里的画别信，重做一遍"。qpdf 的头文件里还有一条实验来的事实：**Acrobat
   和 Reader 无视字段上的 `/DR`** —— 那一样只能住在文档上。**我们的写路径复制的是页**，于是问题很清楚：
   属于文档的东西到不到得了对面。

2. **我是带着答案进去的，而第一轮就把它否掉了**。先按"文档级三样留在原地"写了两条 `KNOWN GAP`
   钉住，构建一跑 —— **两条一起红**：合进来的那条字段，它从供体文档**继承**的 `/DA` 和 `/Q` 被写到了
   字段自己身上（合并后 `own=/Helv 0 Tf 0 g|Q1`，正是供体文档那句），`/DR` 带着供体那个字体对象进了
   新文档，`/NeedAppearances` 也照过来；没照原样过来的只有文档级 `/DA` —— 因为它搬家了，不是因为它丢了。
   **所以 §55 那句"包属于文档不属于页"到这一层不成立**，成立的是我们自己写在 `documenteditor.cpp:1530`
   很久的那句注释（"after pushing any attributes the other file inherited down into it"）—— 这一片是
   第一次把那句话量成真的。**规矩照旧：从库的头文件承诺写出来的钉是假设，第一次运行由文件说。**

3. **撞车那一读法**（两份文档都把某个字体叫 `/Helv`，一个指 Helvetica、一个指 Times）：合并后
   `/DR` 是 `[/Helv=/Helvetica, /Helv_1=/Times-Roman]`，**而且进来那张画的内容流被一起改成了
   `/Helv_1`** —— 钉的是那条不变量："合并文件里每一张画都还认得它自己点名的那个字体"（`unresolved`
   为 0，两个字段两张画各自算得出）。留下一处不对称，也是量到的：进来那条字段的 `/Q` 写到自己身上了，
   `/DA` 没有 —— 于是它**继承**来的那句 `/DA` 里的 `/Helv`，在合并后的 `/DR` 里指 **Helvetica**，而它
   带来的画指 Times。**一份文件对"这个框该用什么字"给两个答案**（§55 那一族），但这一处不是我们的漏：
   那是 qpdf 头文件写明要做的事（"resolve conflicts that may occur from common resource and field names
   across documents"），换名是唯一说得通的解，而对我们这份阅读器零症状（§54/§57 那两条 0 像素）。
   **为什么 `/Q` 推了而 `/DA` 没推，这一片只量到形状、没量到原因**：进来那条字段自己的说法是
   `own=<none>|Q1`（对面没有一句 `/DA` 时是 `own=/Helv 0 Tf 0 g|Q1`）。两种解释都还立得住 —— "文档上那句
   会丢才推"与"只在新建 `/AcroForm` 那条路上推"，我们这条读路分不开它们。**记形状，不替 qpdf 编机制**，
   规矩同 §31/§50/§52：**同一个承诺在不同出口给出不同答案，正是这一族要找的东西。**
   **记成 measured-and-known，不钉成缺陷**，钉的是那两行读数本身（换库或换版本时它要红）。

4. **出去那两条，指令层是全须全尾地走的**：`/DA`、`/DR`、`/Q`、`/NeedAppearances` 都住在文档上，
   而拆出来的每一片都是一个文档 —— 两份各 20 页的拆分**都**写着"重做外观"、**都**带着那个字体对象、
   **都**还册着那条字段和那张画，其中一份没有任何一页能让它画。删页同一形状。§51 那条泄漏到这一片是
   四件东西：**答案、画、字体，以及"重画一遍"的命令**。

5. **屏幕那一遍，问的是这一族最硬的一句**：`field-directive.pdf`（文档级三样都带，`/NeedAppearances`
   为真）对 `plain.pdf`，同一个 `formpage`、同样两块 1240x720 裁条 —— 12 条带逐条相同，两张 PNG 同一个
   SHA-256（`473abc153a29…`，**与 §54、§57 那两对同一个值**：三次跑、通道落在同一处几何上）。分辨力是
   当场可见的：字段落点那一带（band 7、8）都是空白底噪 480，而同一条裁条里别的带能从 480 跳到 4740 ——
   那 480 就是"那儿没有东西"。**于是 `/NeedAppearances` 这一整层指令值 0 个像素**：这份阅读器既不去注释层
   要外观，也不读那本说明书；表单这一层在它面前是**整层**不可见，而不是某一个键没接。

6. **仪器学的四条，都写进代码注释了**：① **从 stream 句柄上读键也要走 `getDict()`** ——
   `normal.getKey("/Resources")` 回的是 WARNING + null，于是第一版读数说"每一张画都认不出自己的字体"，
   §55 那条写侧的坑有个读侧孪生，而且它**先伪装成一条缺陷**；② 对一个 null 对象再问一次键也报 WARNING
   （`/AP` 根本不在那个字段上，那是第 4 段那一读的必经之路），两处共 5 行 WARNING 把这两条抓出来 ——
   手工跑数 WARNING 又一次不是仪式；③ 字段句柄的方法是非 const 的（这族第五次踩）；④ "合并后的
   `/Fields` 里哪一条是进来那条"要由**那条自身的性质**钉（这里用继承来的 `/Q`：1 对 0），不能按位置猜
   —— 位置猜错，下面两条断言就读在错的行上，而它们看着像绿的。

7. **数字。** 新增 34 处 `check()`（672 → **706**），生成器多出第七份样本 `field-directive.pdf`
   （AGENTS.md 那行跟着改），离屏打印 34 → 35 条；**产品代码一行没动**（`git diff HEAD -- xyzpdf/src/`
   为空）；门禁 **26 passed / 0 failed**，手工跑 **0 FAIL / 0 WARNING**；目录一字未改；**没重打包**；
   dev exe 仍是 §50 的 `aa3e897bc0a9…`/1,277,952（连着八片只改测试与工具）。日志与两张 PNG 在
   `H:\tmp\num58\`。

**仍然欠的**：§47 的修法、§51+§52 的那个选择（这一片把它再宽一点：走掉的除了答案和画，还有字体和
"重画一遍"的命令）、§55 的 XFA 三选一、§49 那 28 个访问键、§54/§57 之后剩下的那半句"要不要给表单一点
痕迹"（现在整层指令都在门外，不只是框）、各家生成器**写法不同**的真表单文件（指令层这一族齐了，缺的是
它们的方言）、§48 留的那半句、水印那两档干净量法、新文档开在第几页、就地保存、下一次重打包、
§56–§58 的提交。
**已经还上的**：§55 那句"属于文档的东西会不会跟着页走"在指令层的反面 —— 会，而且 qpdf 已经把它推到
字段上了；外加一条我自己写错的假设，被第一次运行改正，而不是留在文档里当结论。

## 59. 重打包、装上、三处哈希又对上（欠了十五片）—— 顺带量到包里多了五个没人打开的图片插件（2026-09-30）

1. **顺序**（他那一句"提交、推送并重打包吧"就是 §16 那笔欠账的授权）：`acbad0e`（§56–§58 三片一起记）→
   走代理推上去（`git -c http.proxy=socks5h://127.0.0.1:10808 push`，没动 git 配置）→
   `build-package.ps1 -Sign -Cert 221C6B38…0A83` → `signtool verify /pa` → 装 → `verify-package.ps1` → 三处哈希。

2. **§16 平了。** 三处 `xyzpdf.exe` 全是 `aa3e897bc0a9b3cfc4eae52da808cd1a631bf384d9e5a4a93a9a610f0c989e96`、
   1,277,952 字节 —— 构建树、`verify-package.ps1` 解出来的那一份、`WindowsApps` 里装着的那一份，
   脚本自己回了一句 `all three equal: True`。上一次对上是在 §43（那时是 `8102a1dd…`），**欠的正是 §44–§58
   十五片**。签名字段：`sha256` + RFC3161 时间戳，`Successfully verified`；包 17.5 MB；
   `Get-AppxPackage` 报 `status=Ok`；`verify-package.ps1` 判词
   "the package is complete and runs standalone"（裸 PATH 起得来，模块全部溯源）。

3. **而这次对上的哈希背后，载荷动了 1.1 MB，且动的不是产品。** 装出来的树 66 个文件 / 40.5 MB（§43 那一次是
   61 / 39.4；`.msix` 里再多数出来的一个 `[Content_Types].xml` 才等于当年记的 62 —— 那句当时写成"62 个文件"，
   两面是同一件事）。**差别全在 `plugins\imageformats`：5 个变 10 个**，多出来的是
   `qicns / qtga / qtiff / qwbmp / qwebp`（约 1.09 MB）。产品代码、`CMakeLists.txt`、`build-package.ps1`
   的剥离清单这一段一字未改（`git diff` 只有测试、工具与文档），Qt 那侧的目录里 release 插件正好 10 个、
   字节都是 08-13 的。所以话只能说到这儿：**安装期那次 windeployqt 这一回搬了全套，§43 那一次只搬了五个，
   机制没查** —— 我不替它编原因。
   两条跟着来的事实值得记：① `verify-package.ps1` 的第二问是"该没有的有没有混进来"，而它**没吭声** ——
   它的名字表里只有 `opengl32sw` / `D3Dcompiler_47` / `*.qm` 和 test/pdb 两类，图片格式插件不在其中，
   所以"包干净"这句话的检查面比听起来窄；② 这五种格式这个程序一种都不打开（读 PDF、写 PNG/JPEG/BMP、
   图标是 SVG），判据与当年那 29.3 MB 是同一条，而**要不要把它们收进剥离清单是他的一句话** —— 那条判据是他
   立的，我不擅自替他扩。§30 那句话在这里的反向用法值得再说一遍：哈希不动从来不代表载荷不动（这次载荷动了
   1.1 MB 而 exe 一个字节没变），哈希动了也不代表载荷动了（重链接会换哈希）。

4. **装上的那份在新包上起来了一遍**（不是旧结论的复读）：`Add-AppxPackage` 之后窗口起来，26 个不同名字的
   元素读回来 —— 拖放区那句、`打开 PDF…`、三个工具条、八个菜单（含故意不翻的 `Language`）、
   状态行 `就绪`，且 `-Language en` 对包里的运行照旧不生效（§48 那条：包不认 `XYZPDF_SETTINGS_PATH`，
   它的偏好没有外面的读路）。**一句话别说过头**：装上的这份在新包上还没点过"开→编辑→另存改名"那条链
   —— §45 跑的是 §43 的字节，包换了以后没重跑；英文界面那一路（§49）同理，字节是新的、结论是旧的。

5. **数字。** 提交 `acbad0e`（5 个文件，+1008/−5）已推到 `origin/main`（`73b1874..acbad0e`）；包
   17.5 MB / 解出 40.5 MB / 装出来 66 个文件（+ zip 里那个 = `.msix` 内 67）；三处哈希如上；
   日志、比对脚本与解包目录在 `H:\tmp\num58\`。

**仍然欠的**：这五个 imageformats 要不要进剥离清单，以及"为什么这一次搬了全套"的机制；装上的那份在**新包**
上重跑"开→编辑→另存改名"与英文界面那两屏；§47 的修法；§51+§52 的那个选择；§55 的 XFA 三选一；§49 那 28 个
访问键；表单要不要在屏幕上留一点痕迹（§54/§57/§58 三次都是 0 像素，指令层整层在门外）；各家方言的真表单文件；
§48 留的那半句；水印那两档干净量法；新文档开在第几页；就地保存。
**已经还上的**：§16 那笔自 §44 起欠十五片的账 —— 三处哈希又对上，而且这次装上的那份带着 §58 之前所有已知的
读数与目录，跑在机器上。

## 60. 那五个插件的机制：不是随机的，是当场定的 —— 而 §43 那次已经追不回来了，所以把清单变成产物（2026-09-30）

1. **谁在决定搬几个插件**：`cmake --install` 把命令行原样打出来了 ——
   `'E:\qt\6.11.2\msvc2022_64\bin\windeployqt.exe' <那份 exe> --dir . --libdir bin --plugindir plugins
   --qml-deploy-dir qml --translationdir translations --force --qtpaths ...`，后面跟着
   `To be deployed: Qt6Core Qt6Gui Qt6Network Qt6Pdf Qt6PrintSupport Qt6Svg Qt6Widgets`。
   CMake 侧那句 `include("${...}/xyzpdf-plugins.cmake" OPTIONAL)` 点名的文件**不存在** —— 也就是说
   **这个仓库里没有一张插件清单**，那 10 个是 windeployqt 每次跑当场算出来的。

2. **今天是稳的**：连跑两次 `cmake --install` 到两个空前缀，两次都是 10 个，逐字相同 —— 所以这不是
   "有时候少搬五个"的抖动。而输入里每一件能点名的东西都没变：`windeployqt.exe` 与 Qt 的 DLL 全是
   2026-08-13 的字节，`.qt/deploy_xyzpdf_*.cmake` 与 `.qt/QtDeploySupport.cmake` 都是 09-16 生成的
   （早于 §43 那一次打包），`CMakeLists.txt` 与 `build-package.ps1` 在 §44–§58 那一窗一字未改。
   **结论只能是：§43 那 5 个是当时那套输入下的一个结果，而那套输入里跟现在不同的那一样，追不回来了** ——
   §43 的 `.msix` 和它的 exe 都不在盘上（全盘 `find -name xyzpdf.exe` 只剩今天这几份），而唯一还带着旧载荷的
   地方是装出来的那份树，**它正好被这次安装覆盖掉**：能对比的东西被对比的动作本身毁了。这不是"没查"，
   是这一条已经没有证据可查 —— 写在明处，而不是留着让人以为下次还能查出来。

3. **所以这一片交付的不是原因，是"下一次不可能看不见"**：`verify-package.ps1` 现在把解包后的文件清单
   写成产物 —— `build-local-release/payload-history/payload-<exe 前 12 位>-<UTC 时间>.txt`，并跟上一次留下的
   那份比名字集合；有增减就报 `PAYLOAD CHANGED against <file>: gained N file(s), lost M [+路径] [-路径]`。
   **它是 note 不是 problem** —— 载荷是允许动的（Qt 升级就会动），不允许的是**悄悄**动。历史放在构建树根
   而不是包目录，因为 `build-package.ps1` 每次开头会把那个树删掉（这条写在脚本注释里）。

4. **控制**：拿今天这份真清单删掉那 5 行做成"§43 的样子"（62 行）→ 跑一次，它逐字点出那五个名字
   `+qicns / +qtga / +qtiff / +qwbmp / +qwebp`；还原 → `payload is the same 67 file(s)`。两次都退 0。
   顺手定一条口径：**`.msix` 里 67 个文件、`InstallLocation` 里 66 个**，差的那个是 `[Content_Types].xml`
   （它只在 zip 里，装出来的目录没有）—— 所以 §43 记的"62 个文件"和我今天数的"旧的那份 61 个"是同一件事
   的两面，以后一律说清是哪一个数。

5. **等他那一句话的形状现在写得出具体样子**：要剥掉这五个，就是在 `build-package.ps1` 第 2b 步那个
   `$named` 列表里加上这五个相对路径 —— 与当年那 34 个文件同一张表、同一条判据（这程序一种都不打开：
   读 PDF、写 PNG/JPEG/BMP、图标是 SVG）。判据是他 2026-09-22 立的，我不擅自替他扩；扩与不扩都由他一句话，
   而这句话现在有据可依。

6. **数字。** 产品代码一行没动；**门禁没跑** —— 改的只有 `xyzpdf/packaging/verify-package.ps1`，它不在
   门禁里（构建树那份 exe 仍是 `aa3e897bc0a9…`/1,277,952，包也没重打，用的还是 §59 那一份）；
   `verify-package.ps1` +58 行 / 删 0，纯 ASCII 复查非 ASCII 字节 0；控制那一跑用的假清单与两次实验的暂存
   目录都清掉了，历史里只留今天这一份真清单。

**仍然欠的**：这五个插件进不进剥离清单（第 5 条给了具体加法）；§43→今天那 5 个的机制（**已判定不可追**，
别再回来猜）；装上的那份在**新包**上重跑"开→编辑→另存改名"与英文界面那两屏；§47 的修法；§51+§52 的那个
选择；§55 的 XFA 三选一；§49 那 28 个访问键；表单要不要留痕迹；各家方言的真表单文件；§48 那半句；水印那
两档干净量法；新文档开在第几页；就地保存；§59 与 §60 的提交。
**已经还上的**：§59 那条"机制没查"里还能查的那一半 —— 定了它不是抖动、是 windeployqt 当场算的、且旧证据
已不存在；外加一条从今以后不必靠人记住的规矩：**包的文件清单每次落盘，下一次自动比。**

## 61. 装上的那份在 §59 的新包上把两屏重跑了一遍 —— "字节是新的、结论是旧的"这半句销掉（2026-09-30）

1. **还的是 §59 自己记下的那半句**：那次换了包，而"开→编辑→另存改名"（§45）与英文界面（§49）两屏的
   读数都属于 §43 那份字节。两条路由早就收在 `tools/gui-probe.ps1` 里，所以这一片**不加一行代码**，
   只把读数落到新的包上。

2. **第一条链一次跑通**，读数与 §45 逐条对上：AppsFolder 起来 → `文件(F)` → `打开(O)…` → 原生框
   （0.5 秒认出来，标题 `打开 PDF`）→ `文档(D)` → `旋转页面`（按类 `RotatePagesDialog` 认出，不按标题）→
   框开在"指定页码"那一档所以 `旋转这些页` 是灰的（§14 的那条门），点 `当前页（第 1 页）` 就亮了 →
   `保存修改为(S)…`（第一次点着，脚本里那条重试没用到）→ 框标题 `把这些修改存成一个新文件` →
   `WM_SETTEXT` 写名字 + 在按钮**自己那块矩形**上真点 → **出来 12,666 字节**（与 §45 同一个数）→
   状态行 `已写成一个新文件（40 页）：向右转 90°（1 页）` → 两个标签 `many-pages.pdf *` 与
   `renamed-155035.pdf` → **源哈希不动：True**。

3. **这轮看清的一件：装上的那份记得的"上次打开的目录"就是仓库里的 `tests/fixtures`。** 第一跑我按
   §45 的老习惯把源放在 `H:\tmp\num43\doc40.pdf`，原生框列出来的 16 行却是 `blank.pdf / colored.pdf /
   damaged.pdf …` —— 那批 fixture 的名字。§48 说"包写的不是 HKCU 那把键"，这一轮给了它一个具体形状：
   它记的那个目录在仓库里。**后果不是抽象的：任何走这条 picker 的运行都在看被别的测试 glob 的那批文件。**
   于是这一片把两件事分开：打开的源就选 fixture 之一（`many-pages.pdf`，只读地打开），存出去的目标仍在
   `H:\tmp\num61\out`。这样反而把"永不写用户的 PDF 文件"这句**放到被 git 跟踪的文件上**验了一遍：跑完
   `git status --porcelain xyzpdf/tests/fixtures` 是空的，写出的那份 12,666 字节当场删掉。
   顺手一条：保存框里这轮报出**两个** Edit（id 1001 与 id 41477），`WM_GETTEXT` 两个都回空 ——
   老事实"能写不能读"的两个出口，不是新缺陷。

4. **第二条链两向都读了**，也是复验：`跟随系统`（`toggle=On`）→ 点 `English` → 提示窗 `QMessageBox`
   标题 `Language`、正文这时还是中文"语言更改将在下次启动时生效。" → **不重启**设置框里那颗按钮就变成
   `English`（存了值，界面故意不在自己底下重翻）→ 重启 → 菜单栏
   `File | Edit(E) | View | Document(D) | Go | Window(N) | Language | Help`、状态行 `Ready`、
   设置按钮 `English / Clear the list / OK / Cancel`、提示正文这次是英文 → 点 `Follow the system` 放回 →
   再读一次回到中文那八条与 `跟随系统 toggle=On`。八条里 `Language` 在两种语言下都显示 `Language`
   （`mainwindow.cpp:1030` 那句 `QStringLiteral`），这就是这条路能走的原因。三条通道老账同轮复验：
   `-Language en` 对包里的运行不生效（脚本自己打这句）；菜单条本身那 8 条也是 MenuItem
   （日志里那句 "3 rows under [Language] (8 items before, bar is 8)"）；可点的 QAction 用 TogglePattern 读。

5. **顺带把 §49 那条缺陷放到 shipped 字节上再看一眼**：英文菜单栏里屏幕上真的显示
   `Edit(E)`、`Document(D)`、`Window(N)` —— 那 28 个带着中文式 `(&X)` 括号的英文形不是目录里的抽象事，
   是装上的那份的界面。改不改仍是他的一句话。

6. **数字。** 产品代码一行没动；**没构建、没跑门禁**（这一片只跑现成路由）；**没重打包**，§16 那三处哈希
   仍是 §59 的 `aa3e897bc0a9…`/1,277,952；两条链的退出码都是 0；临时写出的那份删了，语言放回 `跟随系统`，
   日志在 `H:\tmp\num61\`（`saverename.txt` 是第一跑"picker 落在 fixtures"的那份，留着记这条读数）。

**仍然欠的**：那五个 imageformats 进不进剥离清单（§60 给了具体加法）；§47 的修法；§51+§52 的那个选择；
§55 的 XFA 三选一；§49 那 28 个访问键（这轮在装上的那份屏幕上又复读了一遍）；表单要不要在屏幕上留痕迹；
各家方言的真表单文件；§48 那半句（**这轮把它缩小了**：包记的目录已看到是 `tests/fixtures`，剩下的只是
"这是我们自己写的 `lastOpenDirectory` 还是壳给每个应用的对话框记忆"）；水印那两档干净量法；
新文档开在第几页；就地保存；§59–§61 三片的提交。
**已经还上的**：§59 那半句"结论是旧的"—— 两屏现在都是在装着的这份包上读的，而那条"永不写用户文件"的
承诺这次落在被 git 跟踪的 fixture 上。

## 62. §48 剩的那半句判掉了：打开框落的目录是**我们显式交给它的那一个**，不是壳按 exe 名记的 MRU —— 而中途我把"读注册表就能判"那句撤回（2026-10-01）

1. **问的是哪一个**。§48 关掉的是"包会不会脏用户那份设置"，留下一句没判："打开框落在上一轮那个目录，
   是应用自己写的 `lastOpenDirectory`，还是壳给每个应用的对话框记忆（`ComDlg32\LastVisitedPidlMRU`）"。
   这条不是洁癖 —— §45 那句"包自己记目录：上一遍存到哪儿，这一遍打开框就落在那儿"是**当证据**写的，
   它到底证明了我们那把键在起作用还是壳的记忆在起作用，取决于哪一个通道赢。

2. **代码那一半先读干净**（不用跑）：`src/core/appsettings.h:124-137` 是那把键 `files/lastOpenDirectory`；
   **写它的有四处** —— `mainwindow.cpp:1480`（打开一份文档，记那份所在）、`documenttab.cpp:1407`（另存成功
   之后，记目标所在）、`documenttab.cpp:1135`（导出图片）、`mergepagesdialog.cpp:194`（合并挑完供体）；
   **读它的有三处**，且都是"把它当作起始目录交给原生框"：`mainwindow.cpp:1554`（空就退回
   `QDir::homePath()`）、`mergepagesdialog.cpp:184-186`、`imageexportdialog.cpp:110`。
   所以这两个通道在我们这边**不是并列的**：我们的框每次都被显式给了一个目录。

3. **我先把一条自己说过头的话改回来。** 读到两处不一致时我以为判据到手了：`HKCU:\Software\xyzpdf\
   files\lastOpenDirectory` = `tests/fixtures`，而 `ComDlg32\LastVisitedPidlMRU` 里 `xyzpdf.exe` 那条
   （值 [9]）解出来是 `H:\tmp\num43`。**但这两处都不是 probe 运行在用的那份设置** —— `gui-probe.ps1` 起任何
   实例都设 `XYZPDF_SETTINGS_PATH` 指向 scratch ini（§38 起就这么设计），HKCU 那把键只是手工起的那次的残渣。
   撤回之后判据反而更简单：**让框在两个候选目录指向不同地方那一刻起来，看它列哪批文件**。

4. **判据与读数（两遍，各自独立）**。新通道 `-Mode openpicker`：起实例 → **先在自己标题栏上真点一下抬窗口**
   → `文件 → 打开…`（三次重试，按类 `#32770` 判框起来没有）→ 把框里的每一行打印出来 → 按控件 id 2 的
   Cancel 用 `BM_CLICK` 关掉，什么都不选。
   - **开发版**（不给 `-LastDir`，`-Doc H:/tmp/num58/samples/plain.pdf`）：框里 7 行正是
     `plain / field-flat / field-spread / field-dangling / field-xfa / field-appearance / field-directive`
     —— 就是**这一次启动时打开的那份文件所在目录**（`mainwindow.cpp:1480` 写的）。而同一时刻壳那条 MRU 指着
     `H:\tmp\num43`、HKCU 那把键指着 `tests\fixtures` —— **两个候选都不符，框跟的是我们交给它的那个目录**。
   - **装的那份**（不带文档起来）：第一遍 0 行，第二遍我在预测的那个空目录里放了一份 `marker-62.pdf`，
     框里就**只有那 1 行**。那个目录是 `H:\tmp\num61\out` —— §61 那次**装着的另存**写进去的位置
     （`documenttab.cpp:1407`），而 `C:\Users\allgoodhi` 有 52 项、当场被排除。**包里那份设置跟着它自己的
     保存走，这条通道对包里那份也成立。**
   **所以 §48 那半句判掉了：是我们显式交给原生框的那个目录说话**，壳那份按 exe 名记的 MRU 不起作用（它甚至
   可能压根没被问过 —— 我们把目录给死了）。

5. **今天关于"点不动"的读数，包括一条我给自己涨错了的结论**。前四轮（三次 `-Mode saverename -Installed`
   加一次开发版 `mergedonor`）读到的是 `|打开(O)…| enabled=True at 674,303` 而框死活不起，开发版那次连
   "子树里的窗口"都打印成空；`openpicker` 起来头两轮（开发版一遍、包里两遍）都是一试就起，与那四轮的差别
   看上去就是新模式多做的一步 —— **动手点菜单条之前先在自己标题栏上真点一下抬窗口**（§28/§54 为像素记过
   这条）。**这个因果随后被自己否掉了**：把模式原样再跑一遍（抬窗口那步还在里面），开发版三次全没起。
   所以**抬窗口值得留着，但它不是原因**；§41 那条规矩原样成立 —— **合成输入落不落地是逐轮定的**，
   一次绿不能当"修好了"。真正让这几次失败可用的是模式改成了**打印它瞄准的那一行**（名字、矩形、enabled）
   并按类 `#32770` 判框起来没有：死轮子因此留下一条读数，而不是一句"路由没走通"。
   另外我之前给自己补丁记的那条可疑处（"顶栏重复点击可能自相抵消"）**同样不是今天的原因**，降级为未验证。

6. **还有一处对不上的账，我不抹平**：§48 说那次让**装着的**那份跑一整轮并另存到了 `H:\tmp\num43`，
   照第 4 条的机制，包里那把键此后应写着 `num43`；可 §61 第一次点 `文件→打开…` 列出的是 `tests\fixtures`
   那 16 行，而 §59 中间做过 `Remove-AppxPackage` + `Add-AppxPackage`。今天这两遍把**机制**定住了，
   没有把这段**历史**定住：要么那次另存其实是开发版那一跑（§44 与 §45 的临时文件如今同在 `num43`，
   从文件名分不出当时 `-OutDir` 给的是谁），要么重装把容器那份清掉而 `fixtures` 是 §43 那次"从 fixtures
   开档"写的、清掉之后不该再出现。**这条要判需要读容器里那份设置，而 §48 写明那要提权** —— 所以它记在这里，
   不当结论用。

7. **数字。** 产品代码一行没动；没构建、没跑门禁、没重打包（改的是 `tools/gui-probe.ps1`：新模式 + 重试
   循环 + 头注释，累计 +101/−2；纯 ASCII 复查 0 个非 ASCII 字节），AGENTS.md 那一行到十种模式；
   临时放的 `marker-61.pdf` / `marker-62.pdf` 与 §61 写出的那份都删了，起的实例都关了，读数与两次注册表
   转写留在 `H:\tmp\num61\`、`H:\tmp\num62\`（`openpicker.txt`、`openpicker-installed*.txt` 是第 4 条那两遍）。

**仍然欠的**：合成输入为什么逐轮不落地（§41 的老账今天又多一例，而且我给的第一个解释被自己否掉了）；第 6 条那段历史
（要读容器里的设置，得提权，或再攒一次能区分的运行）；那五个 imageformats 进不进剥离清单；§47 的修法；
§51+§52 的那个选择；§55 的 XFA 三选一；§49 那 28 个访问键；表单要不要留痕迹；各家方言的真表单文件；
水印那两档干净量法；新文档开在第几页；就地保存；§59–§62 的提交。
**已经还上的**：§48 留的那半句 —— 现在是**两遍独立读数判过的机制**（我们显式交给框的目录说话，壳的 MRU
不算），附带把"probe 起的实例不读 HKCU 那把键"这条自误撤回并因此把判据变干净；**外加一条我自己当场否掉的
因果**（"先抬窗口才点得动菜单"—— 抬窗口留着，但它不是原因，输入落地仍按 §41 逐轮看）。

## 63. 书签编辑动笔之前先交量：qpdf 的大纲 helper 是只读的，而我们的读路径把 Qt 的层级模型当成平的读 —— 真书 184 条书签，侧栏今天只有 14 条（2026-10-02）

1. **为什么这一片交量不交功能**。"还没做"那块里唯一整块没动、又不需要先定修法的书签（大纲）编辑，按这一族
   的先例应该量在前（水印是 `watermarkprobe.cpp` 在前，表单是 §54 的生成器在前）。这里"量"的答案会直接决定
   功能长什么样，所以先把它量干净。新工具 `xyzpdf/tests/outlineprobe.cpp`（536 行）：CMake 里
   `option(XYZPDF_BUILD_OUTLINEPROBE ... OFF)` 门着，而 `build_all_tests.py` 的 `testOptions()` 是从
   CMakeLists 里读所有 `XYZPDF_BUILD_*` 的，所以**门禁每次都编它**（它不会烂掉），而 `NOT_A_TEST` 里那句
   `'probe'` 让它**不进那 26 项运行**。四段按顺序问：一棵手造大纲三个读者各读一遍 / 四处故意写坏 /
   页在书签底下动 / 一份真书。

2. **写的一侧：这条 API 只读，所以"编辑书签"就是手写 `/Outlines`**。`QPDFOutlineObjectHelper` 的构造函数
   全是 private（头文件写明只能用 `QPDFOutlineDocumentHelper` 拿到），没有一个 setter —— 于是可写的只剩
   `/Title /Parent /First /Last /Prev /Next /Count` 这七个键加 `/Dest` 的形状（PDF 2.0 §12.3.3）。
   手造那份把七形铺成 9 条项（`/Fit` 三处、`/XYZ` 两处），三个读者的读数：
   - `/Dest [page /Fit]`、`/Dest [page /XYZ 0 142 0]` → 两家都给页；第二形 Qt 另给 `loc=0,142`。
   - `/A << /S /GoTo /D [page /FitH 650] >>`（不写 `/Dest` 而写动作）→ 两家都认（qpdf 把它解成 `array(3)`）。
   - `/Dest (chap1)` 加根上 `/Names /Dests` 一叶 → 两家都解到 `page=8`：**命名目标在两条读路上都走得通**。
   - `/Dest (nowhere)` 与什么都没有（一节标题）→ 两家的"指不到"是同一个表示（Qt `page=-1`）。
   - `/Dest <<…>>`（目标字典，§12.3.3 并没给大纲项定义这一形）→ qpdf `dest=dict page=-`、Qt `page=-1`。
   **顺手撤回一条我自己写进工具注释里的假设**：第一版我看到 Qt 的路数里子项不见了，就在
   `buildPlainNestedSample` 上面写了"PDFium 因为一个畸形 `/Dest` 而拒掉整棵子树"。那份专门用来分离两种解释的
   样本把自己那份解释否掉了 —— 纯嵌套没有畸形形的文件同样只有根的行数（2 / 4），混着畸形的也是（6 / 9）。
   **拒掉子树的是我自己的读法，不是阅读器**（§56、§62 同族：先把假设写在明处，再被自己的下一遍量翻）。

3. **这一片的主要读数：模型不是平的，而后端按平的读**。`qtpdfbackend.cpp:500-504` 那段注释写着"Qt 的
   bookmark model 是一个带 Level 列的**平表**，所以读 Level 而不是读 `parent()/index()`，因为按树读会
   只剩顶层"——**这句恰好把因果说反了**：`rowCount(QModelIndex())` 才是只剩顶层的那一个。
   - 真书（`H:\tmp\outline\real-book.pdf`，4,971,343 字节，从 `H:\pdf_test\VerilogHDL硬件描述语言.pdf` 抄来）：
     qpdf 顶层 14 条、整棵树 184 条；Qt 根下行数 14、逐层走 184；**我们后端 14 节点、14 全顶层**。
     Level 值 14 条为 0、89 条为 1、81 条为 2 → **Qt 是从 0 数起的**。
   - 手造：qpdf 9 项 / Qt 根下 6 行 / 层级 9 行 / 后端 6 节点；纯嵌套那份 4 / 2 / 4 / 2 节点。
   - 第二处错在 `outline.h:24`（"Qt numbers levels from 1"）加 `outline.cpp:53` 的
     `qBound(0, row.level - 1, path.size())`：Qt 的 0 减一变成 -1，夹回 0，而节（1）减一也变成 0。
   - **两处错的症状一模一样**（全都成顶层），这就是这一条最要小心的地方：只把读法改成层级而不动基准，行数是
     184 而树仍旧是平的；只改基准而不动读法，嵌套项根本没进 rows。**所以验收得写成两条断言**：
     节点总数 == 层级行数，且顶层数 == `rowCount(QModelIndex())`。一条断言抓不住另一半。

4. **四处故意写坏 —— 哪些一致性是承重的**（每处单独一份文件）：
   - 把父项 `/Count` 翻成负数：三个读者的读数与好文件**逐行相同**。不承重。而且**真书上 184 条里 34 条写着非零
     `/Count`、其中 5 条是负数**（-3 / -7 / -13 / -23 / -28）—— 那是规范里"这一节默认折叠"的合法写法，不是畸形。
     给写侧的约束就从这里来：**我们要是把负 `/Count` 当错误清掉，就会改坏一本正常书的大纲**。
   - 顶层第一条的 `/Next` 删掉：qpdf 1 项、Qt 1 行、后端 1 节点 —— **一条链断，后面整片没了**。
   - 子项 `/Parent` 指到根而不是它的父：与好文件**逐行相同**，看不见。→ 这一族的规矩照旧：不可见的畸形既不能
     拿来写断言，也不能拿来当"我们处理了"的证据。
   - 根保留 `/Count` 但不写 `/First` / `/Last`：`qpdf: no /Outlines at all`、Qt 0 行、后端 0 节点 ——
     **整个大纲消失**。
   - 写侧结论：那七个键要么一次配平，要么别写；而"写完立刻用三个读者读回来比行数"是这套写**唯一**能验收的形式
     —— 与 §36 那句同形（只钉数量的断言看不见这一类，坏在这里是"少一片"而不是"少一条"）。

5. **页在书签底下动的时候**（都走我们自己的 `DocumentEditor`）：
   - 删掉第 6 页（`deletePages({5})`）：指着它的 `Child one` —— qpdf 那侧仍报 `page=ok`（页对象还在文件里，
     只是不在 `/Kids` 了；**§51/§52 那条"对象比页活得久"第三次出现，这次在大纲层**），Qt 报 `page=-1`
     且带 `loc=0,-701`（负的 y 是它自己换算的残渣，不去钉它），旁边的 `Child two` 从 8 变 7（Qt 跟着剩下的页
     重号）。而我们后端因为只读顶层，**这三行里根本没有它们**。
   - 把首页挪到最后（`movePages({0}, Back, -1)`）：`Top fit` 0→39、其余整体前移一位 —— **书签跟着页走，
     移页这一条不需要我们重写大纲**。这条是好消息。
   - 于是删页要做的决定被量出了形状，三个候选（摘掉那条 / 留着让它指到现在的另一页 / 让它悬空、点了不动），
     这一片不替他选；而且今天连副作用都读不到 —— 因为侧栏看不见嵌套项（第 3 条）。

6. **门禁为什么从没看见这件事**：`tests/document_smoke.cpp:204` 与 `:428` 是里面仅有的两处 outline 断言，
   两处都只说 `isEmpty()`（一份无书签的文档、一份加密的）。**没有一条断言问过"有书签的文件该看见几条"** ——
   所以 184→14 坏成这样而 26 项全绿。跟 §36 那次同一个形状但更彻底：那次是"没有输入能走到"，这次是
   **没有断言想走到**。修读路径时该顺手带上这条，而样本不用等真文件：probe 已经证明手写 `/Outlines` 写得出
   （`outline-plain-nested.pdf` 那份就是它现造的）。
   另外一条**读侧的丢数据**，量在 A 的每一行上：Qt 有 `Location` 与 `Zoom` 两个 role，后端一个都不读
   （`qtpdfbackend.cpp:513-518` 只取 Title/Level/Page）——手造那份 `Top xyz` 在 Qt 是 `loc=0,142`，到我们后端
   成 `location=0,0`；真书 184 条全是 `/Fit` 所以恰好都是 0,0，看不出代价。"点书签跳到页顶"在带坐标的文件里
   就是丢数据。这条不是纯读侧的事：`OutlineNode` 已经有 `location` 字段，但面板发的信号只有 `jumpToPage(int)`
   —— 要把坐标用起来得动那条信号，**属于行为改变，等他定**。

7. **数字**。产品代码一行没动。新增 `tests/outlineprobe.cpp` 536 行 + `CMakeLists.txt` +17/−0（那一段挨着
   `watermarkprobe`）；probe 里两处注释按第 2 条的撤回改了（只动注释，没重编）。踩到的仪器坑继续是那一族：
   `const QPDFOutlineObjectHelper` 的方法编不过（C2662/C2440，**这族第七次**）；`DestinationDict` 那一形我
   第一版对字典调 `appendItem`，qpdf WARNING "ignoring attempt to append item" 而 probe 当场停 —— 得把数组装在
   字典的 `/D` 里；根 `/Count` 我手打成 8 而树里 9 个后代，改成从树算。门禁 **26 passed / 0 failed**
   （完整 `tools/build_all_tests.py`）。读数留在 `H:\tmp\outline\`（`probe.txt` 是只看了根行数的那第一遍，
   `probe2.txt` 补了控制段，`probe3.txt` 是层级走通之后的那一遍、含真书）。

**仍然欠的**：修 `QtPdfBackend::outline()` 那两处（第 3 条，成对改、成对钉）—— **§64 当场就修了**；书签编辑本身（第 4 条那七个键的
写法、以及它落在哪一层 —— 一个新操作还是只允许重排已有的）；删页时书签的三选一（第 5 条）；`Location/Zoom`
要不要用起来（第 6 条，得动 `jumpToPage(int)`）；给门禁一条非空大纲的断言；命名目标与 GoTo 形支不支持写侧；
那五个 imageformats 进不进剥离清单；§47 的修法；§51+§52 的那个选择；§55 的 XFA 三选一；§49 那 28 个访问键；
表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；新文档开在第几页；就地保存；§59–§62 的提交。
**已经还上的**：这一族第一次把设计问题**量在写之前** —— "这条 API 只读""那个模型不是平的""哪三个键承重"
三件事钉住了，省下来的是"写完再发现侧栏是空的"；附带撤掉我自己写进工具注释里的一条假解释（第 2 条），
以及 §36 那句"不是没被测过，是没有输入能走到"在这里有了第二个更狠的版本（没有断言想走到）。

## 64. 那条读路径当场修掉：书签树走层级、Level 自己数，8 条断言成对写；控制把第二半单独还原一次，红了 5 条而"总数"那条照旧绿（2026-10-02）

1. **为什么这一条不留给他定**。§47 / §51 / §55 那几条欠着是因为**修法本身是选择**（改哪一层、要不要显示、
   XFA 留不留）。这条不是：侧栏少画 170 条书签没有一种读法能称之为对的，它的正确行为没有争议，而 §63 已经
   把两处错都量准了。所以量完就地修，不留"钉住待他选修法"的账。

2. **改了什么**（`src/document/qtpdfbackend.cpp:496-536`、`src/document/outline.h:1-31`）：
   - `outline()` 从"取根下的行"改成**沿层级递归**（一个 `std::function` 的 `appendRows(parent, depth)`），
     发出去的 `level` 是**我们自己的 depth + 1**。
   - **`outlineFromRows` 一行没动**：它是 `outline_smoke` 钉着的那个装配点，契约（1 起数）保持不变，改的只是
     喂它的那一头。这是这一片唯一想要的形状 —— 装配规则已经测过，别再动它。
   - `outline.h` 里两处**错的前提**更正了：那段头注释原先说"Qt 的 model 是带 Level 列的平表，所以别按树读"，
     现在写的是它是层级、根下行只是顶层；`OutlineRow::level` 那句"Qt numbers levels from 1"改成"我们从 1
     数起，Qt 的 Level role 从 0 数起，所以读它的那一头加一"。
   - `Location` / `Zoom` **仍然不读**。不是漏了：要用上它们得动 `jumpToPage(int)` 那条信号，那是行为改变，
     §63 第 6 条已经把它交回给他。

3. **钉在哪里**（`tests/documenteditor_smoke.cpp`，+126/−0）：新 helper `writeNestedOutlineSample()` 是这仓库里
   **第一次真写出 `/Outlines`** 的地方（Chapter 1 带两个 section、第一个 section 带一个 sub —— 五个项、三层深，
   七个键按 §63 第 4 条算出来而不是手打）。读数成对写：
   `tree.count() == 5`（整棵树都在）**和** `tree.nodes.size() == 2`（顶层只有两个），外加四条结构断言
   （第一章挂着那两条 section、第一条 section 挂着 sub、第三层那条仍指着 page 4、第二章没被第一章收养）。
   真书那一跑加了一条 **note 不是 check**（`real-book.pdf holds 184 bookmarks, 14 of them at the top level`）——
   那个数属于一份不在仓库里的文件，别的机器会不同意。

4. **控制：把第二半单独还原一次**。层级照走，只把 `entry.level` 换成 Qt 的 Level role 原值（0 起）——
   门禁 **25 passed / 1 failed**，手工跑那一份红 **5 条**，而 **`tree.count() == 5` 那条照旧绿**：
   章与节全落到顶层，总数一个不少。这就是第 3 条为什么必须是两条断言而不是一条 —— 只钉总数的话，
   §63 那个 0-based 分支永远不会被发现。还原 → 门禁 **26 passed / 0 failed**。

5. **屏幕上还没看**。侧栏现在应该多出 170 条嵌套书签，而 `gui-probe.ps1` 里没有读那一棵树的模式，
   这一片也没现造一个 —— 所以**这条修复只有文件层与后端层的证据，没在真窗口看过**。按 §41/§62 那一族的
   老账（合成输入逐轮定），要做就先加模式再跑，不在这段里凑。

6. **数字**。`check(` 计数 documenteditor_smoke **706 → 714**；`outline_smoke` 一条没动（装配契约没变）；
   门禁 26 passed / 0 failed；手工跑 0 FAIL、37 条 note；dev 树 `xyzpdf.exe`
   **`3b106dbfd0741898…`、1,278,464 字节** —— 自 §53 起连着十一片只改测试与工具之后**第一次动产品代码**，
   字节数与哈希一起换。**没重打包**：§16 那"三处相同"自此又欠着 §63–§64 两片，而装上的那一份正是读不到
   这条修复的形状（它里面还是 14 条）。

**仍然欠的**：屏幕上看那 184 条（第 5 条）；书签编辑本身（§63 第 4 条那七个键的写法、它落在哪一层）；
删页时书签的三选一；`Location`/`Zoom` 要不要用起来（得动 `jumpToPage(int)`）；命名目标与 GoTo 形在写侧支不支持；
那五个 imageformats 进不进剥离清单；§47 的修法；§51+§52 的那个选择；§55 的 XFA 三选一；§49 那 28 个访问键；
表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；新文档开在第几页；就地保存；§59–§64 的提交。
**已经还上的**：§63 第 3 条那两处读路径错（成对钉、并各自量过它能单独活下来）；那条"184 条书签只见 14 条"
从读数变成修复。

## 65. 屏幕上数那棵树：修好的那份 103 行、数字开头的 21 行，控制那份 14 行、数字开头 0 行 —— 而 UIA 交的层级是假的（2026-10-02）

1. **还的是 §64 自己记下的第一笔**（"屏幕上还没看"）。新通道 `-Mode outlinetree`：起开发版 → 真点击抬窗口 →
   （侧栏默认是关的，`documenttab.cpp:113`）走 `查看(&V) → 导航侧栏` 那一行开它 → 找到 `Tree` 元素 → 数行。
   问的是同一份文件（`H:\tmp\outline\real-book.pdf`，4,971,343 字节，与 §63 第 3 条那三个读者读的同一份）。

2. **两份构建、同一个模式、同一份文件**：
   - **修好的那份**：`GridPattern rows=103`、`TablePattern rows=103`；UIA 交回的 `TreeItem` 只有 **26** 行，
     其中 **21 行名字以数字开头**（`1.1 什么是Verilog HDL ？`、`2.3 数据流描述方式` …）。
   - **控制那份**（把 §64 的递归那一行注释掉，等于旧的平读）：`rows=14`、`TreeItem` 14 行、
     **数字开头 0 行** —— 14 行全是 `第N 章…`。同一遍门禁里 `tree.count() == 5` 那条红（25 passed / 1 failed）。
   - **103 对得上文件**：14 章 + 89 节 = 103，而 184 − 103 = 81 是第三层，被 `outlinepanel.cpp:45` 的
     `expandToDepth(0)` 折着。所以屏幕上不是"少了一整层"，是**折叠**；而 §64 修的是"模型里根本没有"。
   - **承重的是那条形状而不是那个数**：`21 行以数字开头` 是平读造不出来的（旧那份一个都没有），
     而"行数变多"单独看可能被视口大小冒充 —— 所以模式把两个都打出来（§57 那条正对照的规矩）。

3. **仪器四条，都写进脚本注释了**：
   - **UIA 在这棵树上没有层级。** 26 行全是 `Tree` 元素的直接孩子，每一行自己 `children=0`（包括 `第1 章简介`
     那种明明挂着三节的）。层级只活在缩进里，而缩进不是 UIA 的名字或孩子数。**所以"数嵌套"不能靠 FindAll(Children)，
     要么问 GridPattern 的 rows，要么靠名字的形状。**
   - **UIA 只交已实现的行**：一棵 103 行的树交回 26 个 `TreeItem`（视口那么多）。展开 5 条折着的根行之后
     仍是 26 行 —— 展开把行换进换出，不往 UIA 里加行。
   - **这棵树没有 ScrollPattern，整个窗口 0 个元素供它**（模式打出来了：`Invoke, Selection, Value, Grid, Table`）。
     退成键盘翻页（先真点击进树里拿焦点，再 `{PGDN}`×24 与 `Ctrl+End`）→ distinct 名字仍是 26，一步没动。
     **这一遍没判是"合成输入这轮没落"（§41）还是"这条通道根本不动 QTreeView"**，所以不替它编机制；
     要用这条模式数到 184，得先有第三条能滚动的通道。
   - **`TogglePattern.Toggle()` 对一个开着的 Qt 菜单行什么都不做**（`toggle=Off` 前后一样），而且它把菜单
     留着开；下一次 `TopMenu` 撞上已展开的菜单会把它**关掉**（§56 那条），于是报"没有 导航侧栏 这一行"，
     看着像应用没这个功能。修法是每轮先收起展开项，并把**真点击排在前面**（§34 起唯一没失手过的通道）。
     另外两处的第一版假阴性：菜单标题是 **查看(&V) 不是 视图**；而我在脚本注释里写中文又撞上"`.ps1` 必须纯 ASCII"
     这条老规矩（两处非 ASCII 字节被检查抓到，改成码点写法）。

4. **数字**。产品代码一行没动；`tools/gui-probe.ps1` 这一片 +269/−0（累计 +393/−3，共 1429 行、**十一种模式**），
   纯 ASCII 复查 0 个非 ASCII 字节、`Parser` 语法干净；AGENTS.md 那一行跟着改。**门禁跑了两遍**（控制 25 passed /
   1 failed，还原 **26 passed / 0 failed**）。dev 树 `xyzpdf.exe` 从 §64 记的 `3b106dbfd074…` 变成
   **`d280073c607a…`、字节数一样 1,278,464** —— 源码一致而哈希换，正是 §43 那句"哈希只说哪一次构建的字节"
   （重链就 churn），别把它读成"负载变了"。读数留在 `H:\tmp\outline\`（`outlinetree-fixed.txt`、
   `outlinetree-controlA.txt`、`gate65-controlA.txt`、`gate65-restored.txt`）。

**仍然欠的**：能滚动这棵树的第二通道（第 3 条那句"26 行是视口"因此还没被屏幕证实到 103/184，只是 GridPattern 说的）；
删页时书签的三选一；书签编辑本身（§63 第 4 条那七个键的写法、落在哪一层）；`Location`/`Zoom`（得动 `jumpToPage(int)`）；
那五个 imageformats 进不进剥离清单；§47 的修法；§51+§52 的那个选择；§55 的 XFA 三选一；§49 那 28 个访问键；
表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；新文档开在第几页；就地保存；§59–§65 的提交。
**已经还上的**：§64 第 5 条那句"屏幕上还没看" —— 现在侧栏那棵树在屏幕上有两份构建的对照读数（103 行 vs 14 行、
数字开头 21 vs 0），并且看清 UIA 对这棵树**不供层级也不供滚动**，所以往后的书签活都得先选对读数通道。

## 66. 他报的两条：标签的关闭按钮画成了下拉箭头，预览窗格把文件锁住删不掉 —— 关闭按钮是样式表抄错的一行，锁文件是 PDFium 的句柄（2026-10-03）

1. **两条都是当场修的那类**（不像 §47/§51/§55 欠着是因为改哪一层是选择）：一张 ✕ 该长什么样没有争议，
   "只是预览一下就不能删文件"也没有一种读法能称之为对的。

2. **关闭按钮：一行 token 抄错。** `src/theme/thememanager.cpp:220` 写的是
   `QTabBar::close-button { image: url($CHEVRON_DOWN); }` —— 和上面第 175 行 `QComboBox::down-arrow`
   同一句，也就是**下拉箭头**被当成了关闭按钮的图。修法：新资产 `tab_close_light.svg` / `tab_close_dark.svg`
   （12x12、与 chevron 同一套墨色 #5A6472 / #C8CDD6、同 stroke 1.7 圆头）+ `$TAB_CLOSE` token + qrc 两行。
   **并且补一条形状规则**（`documenttab_smoke`，两种主题各跑一次）：那条规则里必须出现 `tab_close_`
   且不得出现 `chevron`。理由是这一族的 old 那句：样式表是一个字符串，**任何计数都看不见它指错了图** ——
   只有像素能看见，而像素需要有人去看（这次是他看的）。

3. **锁文件的机制**：PDFium 拿到路径就自己去开文件，而它开的时候**不带 `FILE_SHARE_DELETE`** →
   预览窗格还开着，Explorer 就说"正在使用"。
   - **我先试的那条被量掉了**：改成我们自己开文件、把 `QIODevice` 交给 `QPdfDocument::load(device)`
     （甚至走到 `CreateFileW` 带 `FILE_SHARE_DELETE` + `_open_osfhandle` 那一步）。功能测试全绿，
     连"开着也能删"都绿；顺带量到**一个普通 QFile 的句柄同样挡住删除**（所以"换成 Qt 的文件句柄"
     并不是安全的替代）。但 **`documenttab_smoke` 挂死 600 秒** —— 渲染线程与设备读取共用一个有状态的
     device，页渲染的 latch 永远等不到。整条撤掉：`qtpdfbackend.h` 回到原样，cpp 里只留一句注释说明
     为什么不走那条路。
   - **落地的修法在真正疼的那一层**：预览窗格读**副本**（`previewhandler.cpp` 的 `stageSource()`）。
     复制到 `%TEMP%\xyzpdf-preview-<uuid>.pdf`，PDFium 开的是副本；用户那份文件只在一次顺序复制期间被碰。
     `Unload()` 里删副本；换文档时**先重建 backend 再删上一份副本**（PDFium 还握着句柄时删不掉，
     那就是另一种泄漏）。两个诚实的边界：>256 MB 不复制（就地打开，接受锁 —— 为了"能删"而"不给看"更糟）；
     复制或副本打开失败就退回原路径（错误句子里说的是用户那份文件的名字，不是 temp 名）。

4. **钉在哪里**：`previewhandler_smoke` 加 7 处（textual 52→60）—— 复制一份到 temp → 交给 pane →
   画出来 → **删掉它，成功** → 屏上仍有页面（只测"能删"会放过一个变 blank 的 pane）→
   `xyzpdf-preview-*.pdf` 计数 ≥1（**证明它读的是副本而不是被点名的那份**）→ 最后 Unload 之后计数 ==0
   （不许留东西）。测试开头先清一遍这个 pattern，否则"没留东西"测的是上一次崩溃的残渣。

5. **仪器两条**（都进注释了）：
   - **屏幕像素需要 z-order，UIA 不需要。** `-Mode tabstrip` 第一轮裁出来的是盖在我们窗口上面的 IDE；
     `SwitchToThisWindow` + 标题栏点击之后 foreground 标题已经是我们窗口了，裁出来还是那个
     **置顶的 WinZip 试用弹窗** —— 第三方 topmost 窗口这件事赢不了。改用 `PrintWindow(PW_RENDERFULLCONTENT)`
     让窗口自己画，一次就对：图上 `real-book.pdf` 右边是一个 ✕。
   - **UIA 报的 `TabItem` 矩形和标签实际画的位置差一行**（按它裁到的是工具栏那一行），所以这个模式
     同时留整窗图，我看的是整窗图 —— 裁错矩形和"标签上没有按钮"在孤立的小图里长得一模一样。

6. **数字**。产品代码：`thememanager.cpp` +7/−1、`previewhandler.cpp` +99/−4、`qtpdfbackend.cpp` 相对 §64
   只多一段注释、`qtpdfbackend.h` 回到 HEAD；资源 +2 个 svg + qrc 2 行。测试：`documenttab_smoke` 317→318
   textual（那条规则跑两个主题＝2 条断言）、`previewhandler_smoke` 52→60。`gui-probe.ps1` 本片约 +95
   （累计 +488/−3，**十二种模式**，非 ASCII 字节 0、语法解析干净），AGENTS.md 那行跟着改。
   门禁 **26 passed / 0 failed**；中间两次红都是有用的：一次是 device 路把 `documenttab_smoke` 挂死 600 秒
   （于是撤掉整条），一次是我的测试在 `Unload()` 之后继续复用同一个 handler（COM 不允许，红在下一节）。
   dev 树 `xyzpdf.exe` **`2f41dc238dd5…`、1,280,512 字节**（比 §65 的 1,278,464 大 2,048 —— 两张 svg 编进
   resources 的代价）；`%TEMP%` 里 `xyzpdf-preview-*` 现在 0 个。

**仍然欠的**：**这两条修复只在这份 dev 构建里** —— 装上的那份（§59 的包）两个毛病都还在，要到他手上需要
一句"重打包并重装"；应用自己打开一份 PDF 时仍锁住该文件（这次只修了预览/壳那一层，要不要一起改是他选）；
缩略图 provider 也按路径打开（一次性的，风险小，这次没动）；把 `stageSource` 去掉看那 7 条红的控制；
能滚动书签树的第二通道（§65）；删页时书签的三选一；书签编辑本身；`Location`/`Zoom`；那五个 imageformats；
§47；§51+§52；§55 的 XFA；§49 那 28 个访问键；表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；
新文档开在第几页；就地保存；§59–§66 的提交。
**已经还上的**：他报的两条都在 dev 构建里修好并钉住了 —— 关闭按钮现在是 ✕（整窗图上看见的），
预览窗格开着也能删文件（`previewhandler_smoke` 里那条删除断言），并且副本会随 pane 一起清掉。

## 67. §66 的两条控制：它们红了该红的，并且抓出我自己一条空断言 —— 测试自己往被数的集合里放文件（2026-10-03）

1. **为什么单独一片做控制**。§66 落下去的时候我在自己的记录里写了"欠着控制"。这一片补上，做法是**两处同时还原**
   （`stageSource()` 开头加一个恒真的 `stagingRefusedForControl` 直接 `return false`；样式表那行改回
   `$CHEVRON_DOWN`），跑一遍门禁，再各别手工跑 exe 数红条 —— 门禁只转述第一条 FAIL，"几条断言被抓住"只能自己跑
   （那条老规矩，§37）。

2. **控制的结果**：门禁 **24 passed / 2 failed**，红在这两支：
   - `documenttab_smoke` 手工跑 **2 条**（两个主题各一条）—— 形状规则确实两边都管得住。
   - `previewhandler_smoke` 手工跑 **2 条**：`the file can be deleted while the pane is still showing it`
     与（改名之后）`and the pane is reading a copy, not the file it was named`。

3. **这一片真正的收获：控制抓到一条我自己的断言是空的。** 第一版控制跑出来，`countStagedCopies() >= 1`
   在**副本机制被关掉**的情况下照样绿 —— 因为测试自己那份文件当时就叫 `xyzpdf-preview-deletable.pdf`，
   **被数到的正是它**，不是 pane 复制出来的那份。改名成 `xyzpdf-pane-lock-target.pdf`（不匹配那个 pattern）
   之后，同一个控制把它红了。**一条"证明机制在场"的计数断言，如果测试自己往那个集合里放东西，它就不是证据。**

4. **同一条因果链上的第二条**：`removeStagedCopies()`（测试开头那句"先把这个 pattern 清一遍"）写的是
   `DeleteFileW(found.cFileName)` —— `FindFirstFileW` 交回的是**裸文件名**，相对当前目录解析，所以那句话
   **什么都没删掉**，%TEMP% 里留着上一次控制跑的残渣，正好又喂了第 3 条那个假绿。修成
   `DeleteFileW(parent + found.cFileName)`。所以 §66 记录里"测试开头先清 pattern"那句是**执行了但无效**——
   那时没有控制去问它。

5. **还原之后**：门禁 **26 passed / 0 failed**；两个源文件里 `CONTROL` 字样 0 处；%TEMP% 里
   `xyzpdf-preview-*` 现在 0 个（连同之前那两份残渣一起被修好的清理删掉了）。

6. **数字**。产品代码这一片净变化 0（两处控制都还原了）。测试 `previewhandler_smoke` 相对 HEAD +100/−0，
   其中这一片是改名 + 两条注释 + 收尾 best-effort 删除 + 那个路径修正。门禁跑了 4 遍（3 控制 + 1 还原）。
   dev 树 `xyzpdf.exe` **`5343f0320358…`、1,280,512 字节** —— 与 §66 的 `2f41dc238dd5…` **字节数相同而哈希又换了一次**，
   源码没动，一次重链而已（§43 那句话今天第三次撞上，这次连"字节数一样"都挡不住它换哈希）。

**仍然欠的**：重打包并重装（§66 那两条要到他手上只有这一条路，需要他一句）；应用自己打开 PDF 时仍锁文件
（只改了壳那一层）；缩略图 provider 同机制没动（一次性的，风险小）；书签编辑本身（§63 那七个键的写法与落在哪一层）；
删页时书签的三选一；`Location`/`Zoom`（得动 `jumpToPage(int)`）；能滚动书签树的第二通道；那五个 imageformats；
§47；§51+§52；§55 的 XFA；§49 那 28 个访问键；表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；
新文档开在第几页；就地保存；§59–§67 的提交。
**已经还上的**：§66 记下的那条控制欠账 —— 并且它不是走过场：它抓出一条空断言和一处"执行了但没生效"的清理，
两者是同一条因果链。

## 68. 书签写出去：`DocumentEditor::writeOutline` 整树重建，两个读者走回来一样，两条控制钉在门禁里 —— 顺带又踩了本文件自己写着的那个坑（2026-10-03）

1. **为什么是"整树重建"而不是"改一条"**。§63 量到的就是这一条：顶层第一条的 `/Next` 断掉，后面整片没了；
   根上缺 `/First` `/Last`，整份大纲没了 —— 而 `/Count` 说错了话**三个读者都看不见**。也就是说**逐条打补丁
   会安静地坏**，坏法还不报错。所以 `writeOutline(qpdf, rows, error)` 收的是**一张平表**（标题 + 1 起的深度 +
   页号），一次把 `/Title /Parent /First /Last /Prev /Next /Count` 全算出来重写一遍：那些键不是"记得写"，
   是**结构上不可能漏**。平表的形状就是 `outlineFromRows` 已经在用的那个（§64 钉着的装配点，一行没动），
   所以**侧栏看到的树和文件里写出去的树共用一套词汇**，不会各数各的。

2. **两个读者走回来**（`documenteditor_smoke`，`check(` 714→732）：写 5 条三层深 →
   qpdf 的 `getTopLevelOutlines()` + `getKids()` 走一遍：**5 条、根下 2 条、标题按深度优先的顺序**；
   我们自己的后端（§64 那条读路，也是侧栏真正读的那一条）同一份文件：**`count()==5`、`nodes.size()==2`、
   第一章挂两条、第一条 section 挂一条 sub**。两条都要 —— 只有一个读者的话，另一个读者怎么想没人管。
   另外三条行为也钉了：**再写一次是替换**（旧的子树不在，不是追加）、**空表 = 删掉 `/Outlines`**
   （不是留一个空根让读者猜）、**页号越界就拒**，句子说的是人话并且用 1 起的页号（"第 1000 页"）。

3. **两条控制钉在门禁里，跑的是我们自己写出的文件**（§63 那些控制是手造文件，一次性的）。
   - 把第一条顶层项的 `/Next` 拿掉 → **4 条挂在 1 个根下**（第一章和它的三条后代还在，第二章那一片没了）。
     这个数字**与 §63 印出来的"1"不一样**，因为 §63 数的是顶层条数，这里数的是整棵树 —— 记下来免得下次
     以为谁错了。
   - 把根的 `/First` `/Last` 拿掉 → **0 条**，整份大纲没了。

4. **又踩了本文件自己写着的那个坑**。第一版那条断言写成
   `check(walkOutlineFile(f, walk, &error) && walk.total == 1, "...%1".arg(walk.total))`，跑出来报
   **"it left 0"** —— 而真实数字是 4。原因是 `documenteditor_smoke.cpp` 里 2080 行附近就写着的那条：
   **实参在函数调用之前就已经构造好了**，所以 `.arg(walk.total)` 打印的是走树之前的 0。修法就是文件里那条
   老规矩：先把调用提出来，再拼消息。这一条值得记的不是我踩了，而是**注释在同一个文件里、我写新断言的时候
   还是照踩** —— 说明这类"规矩"如果只活在注释里，它的作用被高估了；能挡住它的只有把断言写成提出来那种形状
   （这一片的三条控制断言都是那个形状）。

5. **目录跟上**：新增两句用户可见的话（越界那句 + 标题为空时的"一条书签"），门禁的 `=== catalogue ===`
   立刻报了 "541 vs 539"。按 AGENTS 那条跑 `cmake --build build-local-release --target update_translations`
   （安全，实测这次是 +101/−76，其中绝大部分只是 `<location>` 的行号搬家），再把两条英文手工填上：
   `%1 points at page %2, and this document has only %3 pages.` / `A bookmark`。顺带记一条噪音：lupdate
   会报 `resources.qrc:7/:11 Unterminated C++ character`（它把那段 XML 注释里的英文引号当 C++ 字符扫），
   **这是本来就有的**，与这一片无关，别去追。

6. **数字**。`documenteditor.cpp` +121/−0（writer + 两个文件内的递归 helper）、`documenteditor.h` +22/−0、
   `documenteditor_smoke.cpp` 这一片约 +223（`check(` 714→732）、`.ts` +101/−76。门禁 **26 passed / 0 failed**
   （中间两遍都有用：一遍目录红、一遍控制断言按 §63 的数字写所以红）；手工跑 0 FAIL、35 条 note。
   dev 树 `xyzpdf.exe` **`e12c1ff0ac5a…`、1,280,512 字节** —— 字节数与 §66/§67 那次一模一样而哈希又换了，
   又一次是 §43 那句话。

**仍然欠的**：这一片只到 writer —— **还没有操作、没有撤销、没有界面**（§69 把它接进 `Operation` 与重放，
§70 才是对话框 + 英文 + 屏幕上那一遍 —— **实际到 §71 才是**：§70 先去量了编号那条契约，见本篇 §70）；
删页时书签怎么办仍未定（writer 只管把给的页号写下去）；
`Location`/`Zoom` 还是丢着（`/Fit` 一条路）；重打包并重装（§66 那两条到他手上）；应用自己打开 PDF 仍锁文件；
缩略图 provider 同机制没动；能滚动书签树的第二通道；那五个 imageformats；§47；§51+§52；§55；§49；
表单要不要留痕迹；各家方言的真表单文件；水印那两档干净量法；新文档开在第几页；就地保存；§59–§68 的提交。
**已经还上的**：M5 那张清单里"书签编辑"这块的**第一层** —— 写出去这条路从"qpdf 不给你 API"变成了
"我们有一整树重建的 writer，且它写出的东西两个读者都认、承重的那两个键坏掉会红"。

## 69. 书签接进 `Operation`：存的是"哪一页"不是"第几页" —— 而控制告诉我们这条不是讲究，是拆分能不能写的问题（2026-10-03）

1. **加的是第四个"不动页"的种类**：`Operation::Kind::SetOutline`。`apply()` 里它和 `SplitBySize`、`Protect`
   走同一个提前返回（一行 if，三段注释），理由是**它必须在切片之后才Resolve**：重放阶段解一次、切完片再解一次
   是两个答案，所以宁可让重放完全不看它。真正落盘的地方是 `saveAs()` 里那个逐片的 `writeOne`，位置就在片切完、
   writer 出现之前：`if (outlineAsked()) writeOutline(*document, outlineRowsFor(outlineToWrite(), present.mid(keep.first, keep.count())))`。
   撤销因此一步到位 —— `outlineToWrite()` 读的是 pending 列表里**最后一条** SetOutline（水印那条规矩），
   把那条从列表里拿掉就回到上一棵树或"没人要过"，没有任何"把画擦掉"的代码。

2. **`outlineAsked()` 与 `outlineToWrite()` 是两个问题，必须分开**：空表是一个真实的请求（"删掉它们"），
   而空答案同时能表示"没人要过"和"要的是没有"。屏幕上量到的分别是：撤销之后 `!outlineAsked()`，写出的文件
   **连 `/Outlines` 这个键都没有**（qpdf 走回 0 条，不是留一个空根）；redo 之后同样这份保存又写出 5 条。

3. **`OutlineEdit` 存 `PageRef{source, page}` 而不是页号**，这是这一片唯一的新设计，也是控制唯一要问的那一条。
   读者的页号在 `setOutline()` 门口就换成身份（~~`live.at(row.page)`~~ —— **§70 量掉：这一行是错的**，它换的是
   *位置*而不是读者打的号；现在是 `PageRef{0, row.page}`，见 §70），之后每一次输出用
   `outlineRowsFor(edits, written)` 对着**那个文件里实际有的那些页**解一次。
   - 存的位置长什么样 —— 控制：把那一行换成 `row.page = edit.page.page`（就是"记数字"那版设计）。红了 3 条，
     而**红法比预想的硬**：
     (a) 删一页那一份，读回来是 `Root A@0, Child A1@2, Grandchild A1a@4, Child A2@6, Root B@10` —— 一份 39 页的
     文件上四条书签**各自安静地往前挪了一页**，没有一个报错；
     (b) 拆分那一份**整份保存拒写**：`Back 指向第 21 页，而这份文档只有 20 页。`（`writeOutline` 的越界守卫
     撞上来了）。
     所以"存身份"不是优雅度问题：**存数字的话，书签和拆分这两件事凑在一起我们就写不出文件**，而书签和删页凑
     在一起会写出指错页的文件。这条比 §68 那条"逐条打补丁会安静地坏"更近一层：坏的是页号本身。
   - 身份而不是位置，还白送了一件：书签能跟着**合并进来的那一页**走（`source` 参与比较），而这一片的检查里
     没测它 —— 那条留给 §70 的对话框那一遍，别把它当成已量到。

4. **"指不到页"是树里的一种合法形状，三处都验了**：`OutlineRow::page == -1` 一路写下去就是不落 `/Dest`，
   回来两个读者都说"没有页"而不是"第 1 页"（`Part One@0, Notes@-1`）；`setOutline()` 收 -1 时存的是
   空 `PageRef()`，`outlineRowsFor()` 让它一直是 -1；页被删掉的那条**留在树里当标题**，不丢
   （`Root A@0, Child A1@-1, Grandchild A1a@3, Child A2@5, Root B@9` —— 后三条正是各自那一页挪了位置）。
   为什么不丢：书签是读者要过的东西，而 `outline.h` 对"没有页的书签"本来就写的是画出来、别假装它不在。

5. **两条形状钉住了，但它们记的是"我们还没管的那一层"**。
   - **没要过 = 不改写**：开一份带手抄大纲的文件，删一页，什么书签都没要求 → 文件里**照旧 5 条**走回来，
     而我们自己的读者给第一条 `-1`。也就是说被删那页的目的地对象还在文件里、还被树指着，屏幕上是一条点不到的
     书签。这是 §51 那一族的**书签版**，而且是**第四种泄漏形状**（前面是答案、画、字体、重画的命令）：
     不是我们写坏了，是我们没看它。修法与 §51 同批等他定（要么删掉指不到页的项，要么留 —— 留的症状现在已经
     钉住了，不会悄悄变）。
   - **拆分时每一片都带整棵树**：两片各读到 5 条，第一片 `Front@0, Front child@2, Back@-1, Back child@-1, Last@-1`，
     第二片 `Front@-1, Front child@-1, Back@0, Back child@5, Last@19`（**每片的页号从它自己第一页数**，这条是对的）。
     另一片那些条以"标题"的样子跟着走 —— 一个不丢项、也不筛项的实现。要筛就得区分"这页被删了"和"这页在另一片"
     （`written` 里查不到，但整份 `present` 里查得到，分得开），那是策略不是机制，先钉现状。

6. **两句拒话**。同一个树要两次 → 拒（`这份要写出的文件已经带着一模一样的书签了…`），于是 pending 照旧 1 条、
   标签不会因一次空改动变脏、撤销菜单里不会多一行"把文件放回它现在的样子"；层级写 0 → 拒（不夹平，
   `“%1” 的层级是 %2…`）；页号出界 → 拒并且句子用 1 起的页号。三条都在 `record()` 之前，所以
   `pendingCount()` 那条断言是它们真没进去的证据。

7. **数字**。`check(` 732→**785**（这一片 +53，一次编译一次跑就全绿；绿不是因为没测 —— 上面那条控制同一轮之内
   就红了 3 条）。`=== catalogue ===` 先红（546 vs 541，5 条新话）→ `update_translations` → 手工填 5 条英文，
   其中 `设置书签（%n 条）` 是 numerus，两支写成 `Set %n bookmark` / `Set %n bookmarks`（目录 541→546 条、
   numerus 42→**43**、`<numerusform>` 84→**86**，unfinished 0）。撤销那一行按字面钉 `设置书签（5 条）`，
   理由与 §32 那条一样：只问"这行存不存在"的话，`%n` 没被填上也照旧绿。
   `documenteditor.cpp` **+258/−1**、`.h` **+93/−1**、测试 **+685/0**、`.ts` +133/−77 —— 这四个数是对 HEAD
   (`acbad0e`) 的，§68 没提交，所以里面是两片合起来的量。门禁 **26 passed / 0 failed**（三轮：一轮目录红、
   一轮控制红 3、一轮全绿），手工跑 **0 FAIL / 0 WARNING / 35 条 note**。dev 树 `xyzpdf.exe`
   **`c3698d6ad419…`、1,291,264 字节** —— 字节数从 §65 起第一次动（+10,752），因为这一族两片都真改了产品代码。
   `tests/fixtures` 一片没进去（样本全在 `H:\tmp` 与 scratch）。

**仍然欠的**：界面上还够不着 —— 没有对话框、没有菜单行，读者今天没有任何办法触发 `setOutline`（~~§70 补对话框~~
**§70 实际先去量了上面第 3 条那句没量过的话，对话框改到 §71**；"书签跟着合并进来的页走"这一条到 §70 也**仍未量过**，
且 §70 把它收窄成"界面上没有任何号能指到合并进来的页"）；上面第 5 条那两个形状（没要过时指不到页的旧书签、
拆分把整棵树发给每一片）是**待定的策略**不是已修的缺陷；`Location`/`Zoom` 仍只有 `/Fit`；删页时书签到底怎么办、
重打包并重装（§66 那两条）、应用自己开 PDF 仍锁文件、缩略图 provider、书签树的第二通道、那五个 imageformats、
§47、§51+§52、§55、§49、表单痕迹、各家方言真表单、水印两档干净量法、新文档开在第几页、就地保存、
§59–§69 的提交。**已经还上的**：§68 欠的那半句"还没有操作、没有撤销" —— 现在有了第四个种类、它有撤销、
它在重放里被跳过、它在每一片里对着那一片的页数解一次，而且这三条各自都有红过一次的控制在后面撑着。

## 70. 书签那句"每个别的对话框都用这一套编号"是没量过的 —— 量出来是错的，而错法有三种（2026-10-04）

1. **为什么这一片不动对话框先动这句话**。§69 的头文件注释写着 `setOutline` 收的页号是"读者正在看的
   那个编号，也是这里每一个别的对话框用的那一个"。这句话的一半是真的（它确实是读者屏幕上的编号），
   另一半是**断言不是读数**：代码写的是 `live.at(row.page)`，而 `live` 是**已被待保存的修改改过形状的那条
   列表**。删页/旋转/移动读读者打出来的号走的是另一条地址：`live.contains({0, page})`、
   `anchor >= originalPages` —— 都是**打开那份文件的页号**。这一族的老顺序（先量后写，§63 那样）说：
   在对话框把这些号交给读者之前，得先知道它到底指哪一页。

2. **测量：同一份文件、同一句"第 6 页"，先删一页再要书签**。红 6 条，读数各自抄下来：
   - `Sixth@5`（期望 4）—— 读者打「第 6 页」，书签落在**它后面那一页**上。差的正好是前面删掉的
     那一页；而**没有一个报错**，文件写出来了，只是指错。
   - `Third@2`（期望 `Third@-1`）—— 指名"已经被删掉的那一页"的书签**借了邻居的页**：不是没有页，
     是有了一张**没打算给的页**。这条比上一条疼，因为读者看不出它多了什么。
   - `“Last page” 指向第 40 页，而现在这份文档只有 39 页。` —— 40 页的文件，删掉一页之后，**屏幕上
     那最后一页就不能要了**。
   - 同一句子里两个基准：报出来的 39 是 pending 列表的长度，而读者的号是从文件的 40 页数出来的。

3. **为什么 §69 那 53 条断言全绿而看不见这个**：§69 每一次 `setOutline` 都在**刚打开的编辑器**上问，
   那一刻位置 == 原始页号，两条读数**长得一模一样**。能把它们分开的只有一个顺序：**先改页、后要书签**。
   这就是 §35 记过的那一族的又一件："链上每一环都各自有测试管着它单独成立的样子，顺序一处都没管" ——
   这一条不只是顺序，还是**同一句话的两个基准**，单看哪一半都对。

4. **修法是两个半边，不是一个**：地址改成 `{0, row.page}`（和 `positionsOf()`/`deletePages()` 同一个地址），
   边界改成 `originalPages`（和 `movePages()` 的 anchor 同一个边界）。两句都得改，因为**一个对、另一个错
   的话症状不同**（见第 5 条）。句子跟着换：`“%1” 指向第 %2 页，而这份文件打开时是 %3 页。` —— 目录
   546 条不变，但里面**一条新、一条过期**（`update_translations` 报 "1 new / Removed 1 obsolete"），
   英文填 `"…" points at page %2, and this file was opened with %3 pages.`。

5. **两条半边各配自己的断言，然后只还原边界那一半跑控制** → 红 3 条，而 `Sixth@4` 与 `Third@-1` **照旧绿**：
   也就是说"指错一页""借邻居"这两条**只由地址那半边管着**，边界那半边看不见它们；反过来那 3 条红的
   （最后一页不能要、它后面那条链、以及越界句子）只由边界管。控制那一句自己还多送一个证据：
   `“Last page” 指向第 40 页，而这份文件打开时是 40 页。` —— **拒一个号又说文件有那么多页**，句子自相
   矛盾，说明边界和句子里那个数必须来自同一个源（这正是 §63/§64 那条"两处错症状相同，验收必须写成两条"
   的形状，这次是两处错症状**不同**，所以断言必须成对而不是合起来一条）。

6. **一个新决定，钉住而不是修**：指名"已被前面那份未保存的删页拿掉的页"**不拒**，走成 heading ——
   与 §69 已经钉住的"ask 之后那页才丢"是同一个答案，于是**两个顺序共用一句话**（模型里只有一个
   `outlineRowsFor()` 在解）。代价说清楚：读者打了一个屏幕上还看得见的号，得到一条点不到的书签，
   而这句话**必须在按下之前讲给他听**，不是事后 —— 那是对话框欠的（§71 第一条）。

7. **顺带把 §69 记过的另一句没测的话收窄**。合并进来的页在读者的编号里**根本没有号**（`importedPageCount()`
   那句水印的排除是同一条理由），所以这条 API 指不到它们：界面上的书签只能落在打开那份文件自带的页上。
   `OutlineEdit` 仍然存整条引用（`{source,page}`），所以一条**已经**指着合并页的书签照样被带走、被解对 ——
   只是还没有任何东西能要求它。**"书签跟着合并进来的那一页走"仍然没量过，别当已知**（§71 才有路径去问）。

8. **数字**。`check(` 785→**797**（+12：四段读数 + 三条 setup + 那条 pending 计数）。对 HEAD 的累计：
   `documenteditor.cpp` +272/−1、`.h` +103/−1、测试 +770/0、`.ts` +133/−77（这一片自己：cpp +14、
   .h +10、测试 +85，目录一换一）。门禁三轮都有用（**测量红 6** → 修 → **控制红 3、25 passed/1 failed** →
   还原 **26 passed / 0 failed**），手工跑 0 FAIL / 0 WARNING / 35 条 note；dev exe `019da45bfccb…`、
   **1,291,264** 字节 —— 字节数与 §69 一模一样而哈希又换（产品代码是真改了：改了边界就换代码页，
   但大小不动，§43 那句话第四次撞上）。`tests/fixtures` 一片没进。

**仍然欠的**：**书签对话框还没写**——这一片的产出是"读者打的号到底指哪一页"这条契约定了，§71 =
`BookmarkDialog` + 文档菜单那一行 + 第 6 条那句"你指的这页已经被删了"要在按下之前说 + 屏幕上那一遍；
侧栏那棵树变成对话框能种下的平表还缺一个 tree→rows（`outlineFromRows` 的反向，今天没有）；
§69 那两种形状（没要过时指不到页的旧书签、拆分把整棵树发给每一片）仍等他定；`Location`/`Zoom` 只有 `/Fit`；
重打包并重装（§66 那两条到他手上）；应用自己打开 PDF 仍锁文件；缩略图 provider；书签树的第二通道；
那五个 imageformats；§47；§51+§52；§55；§49；表单痕迹；各家方言真表单；水印两档干净量法；
新文档开在第几页；就地保存；§59–§70 的提交。**已经还上的**：§69 注释里那句没量过的断言——现在它有读数了，
而且读数说它错，错了三处，三处都改成了红过的断言。

## 71. 编辑书签的框：树在这里是一张三列表，而"没改过就不许按"这条只有这个框能看见（2026-10-04）

1. **为什么是一张表而不是一棵缩进的树**。写出去收的是平表（§68 的 `writeOutline`）、模型存的是平表
   （`OutlineEdit`）、`outlineFromRows` 装配的也是平表 —— 屏幕上再画一种嵌套，就是"这条挂在谁下面"
   的第二个答案，而 §64 已经教过一次：同一个结构被两处各数一遍，两处都能各自"看起来对"。所以框里
   三列 **标题 / 层级 / 页码**，层级是个数字（QSpinBox，1..30，所以 0 和非数字根本打不出来），
   页码那一格**留空就是不落页**（§69 的 heading；数字是 1 起的，即读者在屏幕上数的那个号 —— §70 定的）。

2. **两片新的纯函数，放在 `outline.{h,cpp}` 里**：
   - `outlineRowsOf(tree)` —— `outlineFromRows` 的反向。它必须存在是因为**读侧交回来的是树**
     （`QPdfBookmarkModel`，§63/§64），而写侧收的是列表，中间这一步不能谁要用谁现推。反向走的顺序就是
     侧栏画的顺序（深度优先），层级 = 深度 + 1。一条不对称写在了注释里并且配了断言：
     **树→表→树 是等幂的，表→树→表 只在表本来合法时等幂**（首行层级 3、层级掉两级会被
     `outlineFromRows` 归一化，读回来的就是归一化后的那一份）。
   - `outlineSubtreeEnd(rows, index)` —— "这一行和挂在它下面的那些"到哪里结束。一个数字而不是
     "帮我移动"，因为**框的加/删/上移/下移和测试问的是同一个问题**；`pieceRanges()`、`planMove()`
     那条老规矩（一个答案一个函数）到这里第四次。

3. **模型新增的只有 `outlineToEdit()`**：把 pending 的引用表倒回读者打的那个数。它住在模型里而不是
   让框自己算，因为"书签的页号到底指什么"是 §70 刚量定的事，框再算一次就是两个答案。而且是**全函数**：
   `setOutline()` 是唯一能往列表里放 `OutlineEdit` 的地方，它只存 `PageRef()`（heading，-1）或
   `{0, n}`，所以读回 `page.page` 就是进去那个数。注释里点名了将来会坏在哪一行：如果哪天有第三种
   （指着合并进来的页的那一种），这一行会安静地把它变成 heading。

4. **一条模型没有、也不该由模型有的规则：没改过的框不许按**。`setOutline()` 只拒"和前面记下的那条一模一样"
   （§69），而**文档自己那棵树它根本看不见**（§70 量过：没要过 = 原样带走），所以"开框、什么都不动、
   按确定"在模型层是一次合法请求 —— 后果是标签变脏、撤销菜单多一行把文件放回它现在的样子。这个比较
   只有框能做（它同时看得到文件的树和 pending 的树），就做在这里，并且**两句不同的话分别说**：
   和文件一样 → `这跟这份文档现在的书签一模一样…`；和记下的那条一样 → 同一句里点名是哪一个。
   控制：`unchangedFrom` 恒返回 false → 手工红 4 条（两处"不许按"、两处该出现的句子换了内容），
   门禁 25 passed / 1 failed。**两条控制是同一个构建里一起做的**（一处改比较、一处改插入位置，
   红在不同的小节里，所以归得清：控制一 4 条、控制二 1 条），还原后源码里 `CONTROL` 字样 0 处。
   **比较故意只算三列**：`location` 没有列，而我们的读路根本不填它
   （`qtpdfbackend.cpp` 只取 Title/Page/Level 三个 role），把它算进去的话，哪天读路开始填位置，
   一个没人碰过的框就会报"你改了东西"。

5. **三个按钮各自钉住一件事，控制把该红的红给你看**：
   - **加一条**落在选中行的**子树之后**，不是紧跟它。控制：改成 `at + 1` → 读数
     `第一章/1/0, 新书签/1/-1, 1.1/2/2, 1.1.1/3/4, …` —— 新行插进了章和它三条子项中间，于是
     **那一章的内容整体改挂到一条空白书签下面**，正是 §63/§68 那一族"安静地换了树"。
   - **删一条**连子树一起删（按钮 tooltip 就这么写）。5 行的树删第一条剩 **1 行**，不是 1 行加三条
     无父的孤项：留下它们，`outlineFromRows` 会把它们挂到上面任意一行上。
   - **上移/下移**只跟**同层的兄弟**换整块；第一个孩子没有上面的兄弟，按钮是**灰的**而不是点了没反应
     （越过父亲往上走 = 孩子没了父亲）。选中 1.1 点下移 →
     `第一章, 1.2, 1.1, 1.1.1, 第二章` —— 孩子跟着母亲一起走，这条读数就是那条算术。

6. **§70 欠的那句话，交在按下之前**。前面有一份未保存的删页、框里某行指着那页 → 小结
   `有 %n 条指的页在前面的修改里已经删掉了：它们会留在树里，但不指向这一份的任何一页。` 并且**按钮照亮**
   （§70 钉的决定：不拒，走成 heading，两个顺序共用一个答案）。写的过程中我自己踩了一次这条链：
   第一版断言在**没人碰过的框**上要这句话，结果读到的是"这跟这份文档现在的书签一模一样" ——
   红的是我的测试前提不是产品；改成"先改一行名字，再问它有没有说被删的页"就对了。
   顺序也钉了一条：**先比行号再比种类**（第 2 行缺名字赢过第 3 行页号出界），因为读者的眼睛是从上往下扫的，
   按种类报会去讲第 3 行而读者正在看第 2 行 —— §35 那一族。

7. **这一遍顺手抓到一条仪器洞，是我自己输错参数输出来的**：手工跑 `documenteditor_smoke` 时我照着
   `documenttab_smoke` 的习惯带了 `tests/fixtures`，而它那个位置要的是**一份真文件**；传进去的是个目录，
   于是它打印了 `FAIL: the real document could not be edited: … Permission denied` ——
   **却仍然 exit 0 并且在末尾印"all checks passed"**，因为那一句走的是 `std::cerr` 而不是 `check()`，
   没有 `++failures`。修成计入的（注释写明这就是"印了一条没人必须照办的读数"），并且**当场验了它现在会红**：
   同一句参数再跑一次 → `exit=1`、末尾 `1 failure(s)`（修之前是 `exit=0` + "all checks passed"）。
   教训不是"参数要看清"，是**一条 FAIL 文本如果改不了退出码，它就只是装饰** —— 这一族的规矩
   （§36/§37/§43：数字要能红）这次撞在我自己的 harness 上。

8. **接线**：`DocumentTab::editOutline()`（框种的树从 `backend->outline()` 取，不是从编辑器取 ——
   编辑器故意不看它没被要求替换的 `/Outlines`）→ 文档菜单 `编辑书签(&B)…`（(&B) 的理由照 §26 那条写法：
   「书签」没有可用拉丁首字母，S 是 保存修改为 的，B 与侧栏那一栏和别的阅读器一致），
   tooltip 说的是框自己说不了的一件事：**侧栏那棵树要等新文件打开才变成这个样子**。
   `bookmarkAction` 进 `hasDocument` 那一串，和另外七个一样。

9. **数字**。新文件 `bookmarkdialog.h/.cpp` 共 **490 行**；`outline.h` +35/−8、`outline.cpp` +36/−0、
   `documenteditor.cpp` 这一片 +20（对 HEAD 累计 292/−1）、`.h` +11（累计 114/−1）、
   `documenttab.cpp` +29、`documenttab.h` +6、`mainwindow.cpp` +17/−1、`CMakeLists.txt` +21、
   `.ts` **+415/−194**（其中多数仍是 `<location>` 搬家）。断言：`documenttab_smoke` **317→357**（+40，
   框的链全在这里）、`outline_smoke` **17→29**（+12，反向读 + 子树算术）、`documenteditor_smoke` 797 不变
   （只改了上面那条 FAIL 分支）。目录 **546→573**（27 条新话），numerus 43→**46**、形 86→**92**、
   unfinished **0**；lupdate 的 same-text 启发自己填上了 上移/下移/取消/家族结尾那四句 —— 我逐条核过，
   没有一句被它猜错含义。门禁 **26 passed / 0 failed**，这一片跑了五遍，每一遍都有用（一遍红 3 条是
   我的测试前提不是产品、一遍目录差 27 条、一遍控制 25/1、还原两遍 26/0 —— 最后两遍之间夹的是第 7 条
   那句 harness 修正，只改测试所以 exe 哈希没动）。，手工跑 documenttab / outline / documenteditor / i18n 全 **0 FAIL / 0 WARNING**。
   dev exe `019da45bfccb…` → **`5f006b84a6f9…`、1,314,816 字节**（+23,552 = 一个新对话框 + 27 条话 +
   两个纯函数）。`tests/fixtures` 还是 17 项，一片没进。

**仍然欠的，其中第一条最重**：**真窗口那一遍完全没做** —— 菜单那一行从没被点过，
`DocumentTab::editOutline()` 里 `dialog.exec()` 阻塞，所以任何测试都没走过"菜单 → 框 → `setOutline`"
这一段（这是每个对话框都欠过的一遍，别的六条已经还上了，这条还没有）；`gui-probe.ps1` 没有能读
`QTableWidget` 的模式（哪种控件报什么要现学：§65 教过 QTreeView 不交层级，表格里"层级"是一列数字，
UIA 会不会把它读成一列 Text 未知）；框好不好用是眼睛的事（层级写成数字读者认不认、620×480 装得下
184 行吗、双击编辑这个动作要不要提示）。**一条推出来但没在文件上量过的后果**：这个框写出去只有 `/Fit`，
而我们的读路在**读的时候**就丢掉了目的地在页上的位置（`qtpdfbackend.cpp` 只取三个 role），所以
一份"书签指着页面中段"的文件被这个框改过一次之后，所有位置会拍平成页顶 —— 这是从两条代码事实**推出**的，
不是在哪个 PDF 上读到的；框自己也看不见它，因此没法像第 6 条那样事先讲给读者。要治要么把 `/XYZ` 一路带下去，
要么读的时候就把位置取回来。其余：§69 那两种形状（没要过时指不到页的旧书签、拆分把整棵树发给每一片）仍等他定；
保存之后侧栏不刷新；重打包并重装（§66 那两条到他手上）；应用自己打开 PDF 仍锁文件；缩略图 provider；
书签树的第二通道；那五个 imageformats；§47；§51+§52；§55；§49；表单痕迹；各家方言真表单；
水印两档干净量法；新文档开在第几页；就地保存；§59–§71 的提交。
**已经还上的**：§70 记在明处的两条 —— 对话框有了（第 4 条那条只有框能看见的规则也一起立了），
"这页已经被删了"那句现在说在按下之前（第 6 条）。

## 72. 屏幕上那一遍抓出一条产品缺陷：184 条书签里有 77 条"指向被删的页"，而谁都没删过页（2026-10-04）

1. **为什么这一遍值得单独做**：§71 的框全绿（357 条断言、门禁 26/0），但"菜单 → 框 → `setOutline`"
   这一段没有任何测试走过（`exec()` 阻塞）。新通道 `-Mode bookdialog`（脚本 1524→1683 行，第十三种模式）
   第一次真点它，用真书 `real-book.pdf`（184 条书签）。**它当场抓到一条产品缺陷，而这条缺陷是纯逻辑测试
   结构上看不见的** —— 见第 3 条。

2. **链走通了，读数一条不落**：菜单行 `|Edit Bookmarks(B)…| enabled=True` → 框起来 `800x480` →
   一张表，`GridPattern rows=184 columns=3` → 六个按钮，其中 `|Write 184 bookmarks| enabled=False`、
   `|Move Up| enabled=False`（选中的是第一个孩子，§71 第 5 条那条灰的规则在屏幕上是真的）→
   小结读回 `This is exactly the bookmark list the document already has; pressing would change nothing …`
   → 点 Add one → `rows=185`、`|Write 185 bookmarks| enabled=True`、小结
   `Pressing leaves this document with 185 bookmarks. / 1 of them point at no page.` → 点它 →
   标题 `real-book.pdf — XYZPDF` 变成 `real-book.pdf * — XYZPDF`，文档菜单出现
   `|Undo Set 185 bookmarks(U)| enabled=True` → 点它 → 标题的星号回去，那一行变成
   `|Redo Set 185 bookmarks(O)| enabled=True`。**记下来的树没写进任何文件**（跑完杀进程）。

3. **那条缺陷**：第一版跑出来，小结里多了一句 `77 of them name a page that the changes not yet saved
   have already taken out` —— 而这份文档**没有任何未保存修改**，谁都没删过页。机制是我的算术：
   那句用 `named.size() - editor->pagesStillThere(named)` 算"没了的页"，而 `pagesStillThere()`
   为了它自己的调用者（页码范围）**按页号去重**（`QSet`），返回的是"这些里面还有多少*个不同的页*"。
   书签表是**一行一条书签**，真书里 184 条挤在远少于 184 个页上（前四条都在第 1 页），于是
   行数减不同页数 = 77 = **共用同一页的书签条数**，被报成了"指向已删除的页"。
   - 修法是把那一问按行问：`DocumentEditor::pageStillExists(int)`（同一个 `{0,page}` 地址，
     §70 定的那一个），框自己逐行数；`pagesStillThere()` 不动（它的调用者要的就是"多少个页"），
     只在它上面写清楚"这是数页的，不是数行的"，并把这条读数引过去。
   - **补的断言就是那条没人想过的形状**：三条书签压在两个页上、什么都没删 → 那句不许出现；
     再把其中一个页真的挂上未保存的删页 → 那句要出现并且**数的是 1 条**。控制：把旧的
     "行数减不同页数"装回去 → 手工红 2 条（该不出现的出现了、该是 1 条的成了 2 条）。
     §71 那批测试为什么全绿：它们的 fixture 每页最多一条书签 —— **两个算术只在"多行同页"时才分岔，
     而那个输入一个都没造过**（§70 那条教训的同一形状：两条读数在测过的案例里天然相等）。
   - 修完在同一份真书上重跑：那句 77 没了，小结只剩 `1 of them point at no page`（那 1 条是刚加的
     空白新行，真的没有页）。

4. **两条仪器读数（哪种控件交什么形状）**：
   - `QTableWidget` 和 §65 那棵 `QTreeView` 一样，**总数只能从 `GridPattern` 拿**（rows=184 是整份表，
     而 `DataItem` 只有 30 个 —— 视口里那十行 × 三格）；表头是三个 `ControlType.Header`，名字就是
     `Title` / `Level` / `Page (empty = no page)`。
   - **格子里放控件，那个控件不进 UIA 树**：层级那一列用 QSpinBox 当 cellWidget，读数里
     `Spinner: 0`、`Edit: 0`，而那一格的 `DataItem` 名字是**空的**（标题格和页码格都有名字）。
     也就是说这一列对读屏软件是沉默的 —— 看得见的人有数字，看不见的人什么都没有。**记为待改**（§73）：
     层级改成普通文本格 + 把"不是数字/小于 1"作为按前的拒话，顺带把 §71 那句"spin 所以 0 打不出来"
     的注释改掉（它换来的代价现在量到了）。
   - **我自己的计数通道在说谎**：第一版用 `ControlType.ValueFromName('ControlType.DataItem')` 去数，
     报出 `DataItem: 0、Button: 0、Text: 0`，而同一次运行下面的形状转储里那些元素明明都在。改成从
     后代列表里按 `ProgrammaticName` 归堆才对的。§58 那条又应验一次：**看不见某东西的通道，产出的正是
     "没有那样东西"的结论**。
   - `gui-probe.ps1` 里 `$doc = (CjStr …)` 把参数 `$Doc` 盖掉了（PowerShell 变量名不分大小写），
     于是图片存成了 `bookdialog-文档.png`。**因为日志里印了图片路径才发现** —— 打印你写的文件名不是装饰。

5. **一条只有图能看出来的缺陷**：620x480 的框把自己的第三列切在窗口外面（表头显示成
   `Page (empty = no page` 且格子半截在视野外），而所有计数、所有断言、门禁 26/0 全都在它绿的时候
   成立。改成 800x480，两个数字列定宽（层级 90 / 页码 180），拉伸只给标题；重看一次图，表头完整了。
   这条是 §66 那条规矩的又一次兑现：**要看画成了什么样得让窗口自己画**（`PrintWindow`），
   而 harness 里每个新 UI 都该配一张图。

6. **数字**。产品：`documenteditor.h` +12/−1（`pageStillExists` + `pagesStillThere` 那句更正注释）、
   `documenteditor.cpp` +8/−0、`bookmarkdialog.cpp` 改 3 处（逐行数、宽度、列宽）。测试：
   `documenttab_smoke` **357→362**（+5：同页三条不报丢失、真删一条报 1 条、以及三条 setup）。
   工具：`gui-probe.ps1` 1524→**1683** 行（新模式 + 两处自身修正），非 ASCII 0、语法解析 0 错。
   门禁四遍（第一遍绿 → 控制 25/1 手工红 2 → 还原 26/0 → 宽度改完再 26/0），手工
   `documenttab_smoke` 0 FAIL / 0 WARNING；dev exe `5f006b84a6f9…` → **`d80c00da8760…`、
   1,314,816 字节**（字节数没动，§43 那句第五次撞上）。AGENTS.md 那行到十三种模式，并把
   "格子里放控件不进 UIA"与"布局只有图能看出来"写进约定。
   日志：`H:/tmp/outline/bookdialog.txt`（有缺陷那份）、`bookdialog2.txt`（修完）、`bookdialog3.txt`（宽度修完）；
   图：`%TEMP%\xyzpdf-gui-probe\bookdialog-real-book.png`。

**仍然欠的**：**层级那一列对 UIA 是空的**（§73 第一条，改法已定：文本格 + 按前的拒话，并改掉 §71 那条
"spin 所以打不出 0"的注释）；框的其余眼睛账（800x480 下 184 行滚动顺不顺、双击编辑要不要提示、
spin 的箭头占了大半格）只看了两张图，没有逐项判；**§71 那条推出但未量过的后果仍然没量**（框只写
`/Fit`，所以"书签指页面中段"的文件被它改一次会全部拍平到页顶）；`/XYZ` 一路带下去还是读时取回位置，
等他定；§51 书签版两种形状仍等他定；保存后侧栏不刷新；重打包并重装（§66 那两条 + §68–§72 这一族到他手上）；
应用自己开 PDF 仍锁文件；缩略图 provider；书签树的第二通道；那五个 imageformats；§47；§55；§49；
表单痕迹；各家方言真表单；水印两档干净量法；新文档开在第几页；就地保存；§59–§72 的提交。
**已经还上的**：§71 最重的那条欠账 —— 屏幕上那一遍做完了，而且它不是走过场：它抓出一条产品缺陷、
一条仪器假阴性，和一处只有图能看见的布局错误。

## 73. 重打包并重装：三处哈希又对上（欠的是 §60–§72 十三片）—— 而 `-Install` 这条路第一次把"同版本重装必挡"写进脚本（2026-10-05）

1. **顺序与第一次的失败**：`build-package.ps1 -Sign -Cert 221C6B38…0A83 -Install` —— 包建好也签好了
   （`XYZPDF_0.1.0.0_x64.msix`，17.5 MB，`Successfully signed`），**死在最后一步**：
   `Add-AppxPackage : 0x80073CFB`（同标识同版本已安装，禁止更新安装）。不是签名问题也不是进程占着
   （先确认过没有 `xyzpdf`/`DllHost` 实例在跑）：**版本号一直是 `0.1.0.0`** —— 它写在
   `CMakeLists.txt` 的 `project(... VERSION ...)` 一处，不会为一地重装而涨，而 Windows 不允许同版本
   原地更新。手工 `Remove-AppxPackage` → `Add-AppxPackage` 装上，`status=Ok`。§40/§49/§59/§61/§67
   那几次都是这么手工过的，**脚本里从来没写这一步** —— 所以第 4 条把它补进脚本。

2. **§16 那笔账平了**：三处 `xyzpdf.exe` 全是 **`d80c00da8760…`、1,314,816 字节**（构建树、
   `verify-package.ps1` 解出来的那一份、`WindowsApps\…\bin\` 里装着的那一份）。上一次对上是在 §59
   （`aa3e897bc0a9…`），**欠的正是 §60–§72 十三片** —— 里面包括他报的那两条（§66）与书签这一族
   （§64 的读路修复、§68–§72 的写出去/接进模型/对话框/屏幕那一遍）。

3. **装上的那份单独验了三件事，没有复读旧结论**：
   - **三颗壳扩展 DLL 逐一与构建树相同**：`xyzpdfpreview.dll` `605f8189e7e7…`（**§66 那条"预览窗格
     锁住文件删不掉"的修复就在这颗里**，exe 的哈希盖不住它）、`xyzpdfshell.dll` `5bdb230257a7…`、
     `xyzpdfthumbnail.dll` `088a2726faad…`。这是"装的是修好的那份"最硬的一条读数。
   - **文档菜单里 `编辑书签(B)…` 在装上的那份出现了**：`-Mode menudump -Menu 6587,6863 -Installed`
     读出 19 行 = 菜单栏自己 8 行 + 文档 11 行（§65 那条减法：菜单栏的条目本身就是 `MenuItem`），
     八个编辑行全在，没开文档所以都是 `enabled=False` —— 这正是 `hasDocument` 该有的样子。
   - **重装清了包自己的设置容器**，装上的那份回到"跟随系统"＝中文界面。这条要预先讲：
     §49/§61 在旧包上读到的**英文**那两屏，对现在这份不再成立（要英文得再走一次它自己的 Language
     菜单）。§48/§62 说的那个容器 hive 今天给了一次具体的代价。
   - §60 那道清单守卫第一次在"该没事"的场合报平安：`note: payload is the same 67 file(s) as
     payload-aa3e897bc0a9-20260930-073756.txt` —— 十三片过去，载荷一个文件没动。（67 与 §59 正文里
     那句"66 个文件"的差就是 `[Content_Types].xml`，§59 第 3 条已经记过两面是同一件事，**别读成漂移**。）

4. **一条工具修正，因为这次失败落在人身上而不是脚本里**：`-Install` 现在先 `Get-AppxPackage -Name`
   → 有就 `Remove-AppxPackage` → 再 `Add-AppxPackage`，并且**把代价印出来**（"包自己的语言、最近文件、
   上次打开的目录会一起走"）。修完原样再跑一遍：`removing 1 installed copy ... installed from ...`、
   exit 0，三处哈希仍然相等。顺带**我第三次撞上同一条坑**：把那句中文错误文本抄进了 `.ps1` 的注释
   （33 个非 ASCII 字节），改掉并复查为 0 —— 这条坑 §38 记过一次、§72 又记一次，抄注释时最容易带进来。

5. **两条没在装上的那份上重看，说清楚限度**：标签 ✕ 的像素（他报的第一条）与 Explorer 里"预览时删文件"
   （第二条）。成立的推理是：`xyzpdf.exe` 与 `xyzpdfpreview.dll` 与被 §66/§67 看过、被
   `previewhandler_smoke` 钉过的那份**逐字节相同**，所以"装的是修好的那份"是真的；但**"同一份字节在
   装上的环境里表现相同"这一条今天没有再读一遍** —— 包环境有自己的变量（§48 那个容器就是例子），
   要真验需要一条"先用原生 picker 开文档、再裁图"的路由，而 `tabstrip` 现在不带开文档那一步。
   这条路由欠着，跟他报的另两屏（§45/§49 那两条 §61 已在新包上复验过）不同级。

6. **数字**。产品代码一行没动；`build-package.ps1` +16/−0（含注释），非 ASCII 0、语法解析 0 错；
   包 17.5 MB / 解包 40.5 MB / 67 个文件；`signtool verify /pa` → `Successfully verified`；
   `verify-package.ps1` 判词 "the package is complete and runs standalone"（裸 PATH 起得来）；
   `Get-AppxPackage` `status=Ok`。**门禁没跑** —— 改的只有 packaging 脚本，它不在门禁里（§60 同一条）。

**仍然欠的**：**装上的那份上重看他报的那两屏**（✕ 的像素、Explorer 里预览时删文件）—— 需要给
`tabstrip` 补一条"picker 开文档"的前奏；**§72 记为"§73 第一条"的那件事编号顺延为 §74**：层级那一列
对 UIA 是空的（格子放的 QSpinBox 不进树），改法已定 = 文本格 + 按下之前的拒话，并改掉 §71 那条
"spin 所以 0 打不出 0"的注释；框剩下的眼睛账；§71 那条推出而未量的 `/Fit` 拍平；`/XYZ` 带下去还是
读时取回，等他定；§51 书签版两种形状、保存后侧栏不刷新、应用自己开 PDF 仍锁文件、缩略图 provider、
书签树第二通道、那五个 imageformats、§47、§55、§49、表单痕迹、各家方言真表单、水印两档干净量法、
新文档开在第几页、就地保存；**§59–§73 的提交**（push 另说）。
**已经还上的**：§16 那笔"三处相同"的欠账（欠了十三片），以及 `-Install` 那条只有跑过一次才知道会挡的路。

## 74. 层级那一格改成文本格：屏幕上从"30 格里 10 格是空的"变成"0 格是空的"，而图差一点说了句假话（2026-10-05）

§72 记为"下一条"的那件事。层级那一列原来往格子里放了一个 `QSpinBox`，而**格子放的控件不进 UIA 树** ——
屏幕上有一列数，读屏那里是一格空。这一片把那一格换成正文，并把它替读者挡着的那件事改成说出来的。

1. **换的是形状，不是样式**。`levelOf()` 从"问 cellWidget 里的 spin"改成"读那一格的正文"，`rebuild()`
   不再往格子里放控件；随之丢掉的是 spin 顺手挡掉的东西（0、负数、非数字、空），所以那四样改成
   **按下之前说**：`Problem` 多一种 `Level`，位置在名字之后、页码之前（一行之内仍按列从左往右，§71
   钉过的那条顺序），话是 `第 %1 行的层级要写成 1 或更大的数字：1 是顶层，往下每深一层加 1。`
   一句覆盖空/非数字/0/负数四样。**不钳位**：`rows()` 里留着读者写下的那个数（写 0 就是 0），
   钳位会把书签悄悄挪一层，那正是 §69 拒绝钳位的理由。
   顺手撤掉两条此刻变成假话的注释：§71 那条"所以 0 打不出来"，和 `documenteditor.cpp` 里那句
   "No dialog produces a level of 0"（现在能打了，只是这个框不让按；模型那条拒话留着当后路）。

2. **屏幕上的读数才是这一片的答案**（同一份真书 184 条、同一条 `-Mode bookdialog`、同一套几何）。
   §72：每格三元组的中间是空的 —— `DataItem ||`，30 格里 10 格空 Name。这一片：
   `cells: 30 DataItem, 0 with an empty Name, 20 whose Name is just a number`，逐行读到
   `第1 章简介 |1| |1|`、`1.1 什么是Verilog HDL ？ |2| |1|`、`第2 章HDL 指南 |1| |4|`。
   **而判据不能看 Value**：同一格的 `Value=[] isReadOnly=False` —— 没进编辑态时 Qt 不供值，
   数在 **Name** 里。只看 Value 会把这一列又读成空的，那就是 §72 那条"通道找不到东西，长得跟窗口里
   没有那样东西一模一样"的第四次。

3. **文本格的第二份收益：那一格写得动了**。通过它自己的 Value pattern 写 `0` →
   `accept enabled=False`，小结整句换成 `Row 1's level has to be a number, 1 or more: 1 is the top
   level, and each step down adds 1.`（这条英文形第一次在屏幕上读到）；写回 `1` → `enabled=True`。
   spin 在格子里的时候它压根不在树里，连写都没处写。

4. **一条差点记成产品缺陷的假案**。图里 OK/Cancel 那行被切掉一半，看着像"英文小结更长，把按钮挤到
   窗口外"。两个矩形一比就翻过来：UIA 的 `BoundingRectangle` 对 Qt 顶层窗口是**客户区**（800x480），
   而 `PrintWindow` 画的是**整个窗口**（外框 816x519）—— 按客户区尺寸开图必然切掉最底下一行；
   accept 自己的底边在窗口底边**之内** 11 px。改成按外框开图，图里三列 + 灰着的
   `Write 184 bookmarks` + Cancel 全在。§72 立的规矩是"布局只有图能看出来"，这一片给它补一句反面：
   **图也会说谎，而说谎的是取景尺寸** —— 所以判据要写成两个矩形的比较，不是"看图觉得"。

5. **自己踩的三条仪器坑**。① 那条布尔判据用中文"层级"去匹配英文小结（这个 harness 默认
   `Language=en`），屏幕上明明有那句话而它报 False —— 判据改成两种语言都匹配，并把理由写进注释
   （§42 为另一行记过同一族账）。② **早退不杀进程**：模式里 `Say '...'; exit 1` 会跳过文件末尾那句
   kill，于是残留实例喂给下一轮 —— 这一轮真丢了一次运行（"框没起来"，其实连的是上一轮的窗口），
   重试即好；`bookdialog` 那四处早退改走 `Bail`（说一句 + kill + exit 1），**其余模式的早退这一片没动**，
   仍欠。③ 同一根 ASCII 坑**第四次**（§38、§72、§73、§74）：注释里写 `§42` 的那个 `§`，2 个非 ASCII
   字节，复查归零。

6. **控制**。把 `firstProblem()` 里那条层级判据拿掉 → 门禁 25/1、手跑红 4 条，而第一条读数就是"没拦住
   会写成什么"：`按下去这份文档会有 2 条书签。`（表里第 1 行层级是 0，框准备按下）。还原 → 26/0。
   另一条是测试前提而不是产品：判"灭着"之前先改一次名字。§71 的"没改过的框不许按"会让按钮因为
   **另一个原因**灭着，那样"0 被拦住"这条判据不装判据也能绿 —— 灭着有两种原因，得先把另一种排掉。

7. **一条 measured-and-known，不改，等他定**。屏幕上读到 `1 of them point at no page.` —— 那句计数话
   用的是 `%1` 而不是 `%n`（`其中 %1 条不指向任何一页。`），中文没有单复数所以看不出来，英文就是不合
   语法；同一形状还有一处 `这会把前面记下的那一条（%1 条）换掉`。目录那五条形状规则看不见它，因为它
   压根不是 numerus 形（`%n` 没出现，规则无从下手）。§33 那一族的规矩是"计数句走 numerus"，所以这是
   漏网的一条，性质同 §49 那 28 个带 `(&X)` 括号的英文形：**记下来，改法（转成 `%n` 两支 + 一条能看见
   "填进去的是计数"的规则）等他定**。

8. **一条边界随文本格一起变宽，说清楚**。层级现在是自由文本，所以"比祖先还深"的数（第 2 行写 5）由
   `outlineFromRows` 的钳位接住 —— 挂在当时最深的那一行下面（§64 定的读法）。这不是新的答案：spin
   的上限是 30，同样的数一直写得出来，只是以前不可见、也没人问过。**钳位之后文件里长什么样这一片
   没量**，与 §71 那条 `/Fit` 拍平记在同一笔欠账上。

9. **数字**。`bookmarkdialog.cpp` 417→440（+23：拒话一种、注释两处撤回、spin 那六行拿掉）；
   `documenteditor.cpp` 只有注释（+4/−2）；`documenttab_smoke.cpp` 的 `check(` **362→376**（+14）；
   目录 **573→574**（1 条新话，numerus 46、形 92、unfinished 0 都没动）；`gui-probe.ps1`
   **1683→1783**（+100：逐格读、Value 写、两个矩形判图、`Bail`），非 ASCII 0、解析 0 错；
   门禁三遍（绿 / 控制 25-1 红 4 / 还原 26-0），手跑 0 FAIL；dev exe
   **`ee24ab133c39b50e91aa…` / 1,315,840 字节**（+1,024）。**没重打包** —— §16 那"三处相同"自 §73
   起欠这一片。

**仍然欠的**：**装上的那份上重看他报的那两屏**（✕ 的像素、Explorer 里预览时删文件 —— §73 记的，需要
给 `tabstrip` 补一条"picker 先开文档"的前奏）；**其余模式的早退仍不杀进程**（这一片只补了
`bookdialog` 那四处）；层级列宽 90 是当年给 spin 留的，现在只放一两个数字（外观账，没量过怎样更好）；
第 8 条那句钳位之后的文件形状、§71 那条 `/Fit` 拍平、`/XYZ` 带下去还是读时取回；第 7 条那句计数句的
英文单复数（连同能看见它的规则）；框剩下的眼睛账；§51 书签版两种形状、保存后侧栏不刷新、应用自己开
PDF 仍锁文件、缩略图 provider、书签树第二通道、那五个 imageformats、§47、§55、§49、表单痕迹、各家
方言真表单、水印两档干净量法、新文档开在第几页、就地保存。
**已经还上的**：§72 点名的那一列 —— 它现在在无障碍树里，屏幕上 30 格 0 空，而且写得动；一条只有把图
开对尺寸才看得见的清白（按钮没被挤出窗口，是我的图短了 39 px）；以及 **§59–§74 这十六片同一天提交并
推送了**（`8d1bcc4`，29 个文件、+5768/−238，其中五个新文件；提交前没重跑验证 —— 最近一遍门禁就是
还原后的 26/0，之后只动了文档；push 走 `-c http.proxy=socks5h://127.0.0.1:10808`，不改 git 配置，
`origin/main` 与 `HEAD` 现在相同）。

## 75. 量 §71 那条没量过的后果：一次保存把书签指向页面中段的位置全拍平 —— 而"问 Qt 多要一个 role"救不了真书（2026-10-05）

排序里第 2 条（第一档里唯一一条"推出来但从没在文件上量过"的）。量完它成立，所以这一片不是撤回而是**钉住 +
把三条改法的代价摆出来**，改法本身等他定。

1. **三段读数，都在文件上**（不是代码推理）。
   - 输入形状：四条书签，`[/XYZ 0 700 0], [/FitH 650], [/Fit], none`。
   - 读路：我们后端交出来的四行里 **0 行带位置**；同一份文件 Qt 自己的模型对 `/XYZ` 报
     `loc=0,142` —— 顶左原点（页高 842 减 700 等于 142），所以**任何写回都要换算**，不是把数字抄回去。
   - 写路：`setOutline(读到的那四行) + saveAs` 之后，文件里变成 `[/Fit], [/Fit], [/Fit], none`。
     **§71 的推断成立**：读者只改一条书签的名字，其余指向页面中段的书签全部回到页顶。

2. **这条为什么比"少读一个 role"贵 —— 那句是这一片真正的读数。** 真书 184 条书签**全是 `/FitH <top>`**
   （top 从 225 到 962，一条不落），而 Qt 的 `Location` role 对 `/FitH` 报 `0,0`。所以最便宜的那条改法
   （问模型多要一个 role）在这份真书上**一条都救不回来**，在这份样本上只救回 1/4 —— 它是个假修复，
   写在这里是为了让它不被当成便宜的正确答案选中。

3. **三条路的代价（等他定）**。
   - **(a) 从文件带下去**：开文件时用 qpdf 把每条 `/Dest` 原样读进 `OutlineEdit`，writer 原样写回；
     页没变就整条搬，页变了就按同一种类换算坐标。成本不止那两处代码：`OutlineEdit` 多一个不透明
     目的地，§69 的 `operator==` 与撤销那行要不要算它得定，而 **§71 写在 `unchangedFrom` 上的理由
     （"读路只取 Title/Page/Level 三个 role，所以算进 location 会让没人碰的框报'你改了'"）当天就
     不再成立** —— 那条注释是这段设计的地基之一，动它必须一起动；拆分时每片还要认出自己的目的地。
   - **(b) 只问 Qt 的 role**：两处 `data()` 调用。按第 2 条记为**不可选**（除非只要 /XYZ 那种文件）。
   - **(c) 不动数据，只在按下之前讲**：开框时用 qpdf 数一次"这份文件有多少条书签带着页面内的位置"，
     小结里说明按下去它们都会回到页顶。成本最小、诚实，也是 §70 那条"代价必须在按下之前讲"的同一原则
     —— 但它治的是知情，不是数据。
   - **我的建议**：先做 (c) 那一小片（挡住"用户不知道"），(a) 要不要真做等他定，(b) 不做。
     （**§99 把 (a) 做了**：目的地从文件带下去、原样写回，连接按"标题 + 页"而不是按行号；(c) 那句
     保留下来但换了意思 —— 它现在说的是"这一次会落下几条"。(b) 仍然不做，第 2 条那个读数是它的死刑。）

4. **钉住的是两条，不是一条**：`documenteditor_smoke` 里 cause 一条（我们的读路对这两条都不带位置）、
   effect 一条（写出来全是 `/Fit`），都标 KNOWN GAP。**控制证明它们谁也看不见谁**：把 writer 的 `/Fit`
   换成 `/XYZ 0 700 null` 之后只有 effect 那一条红（1 条），cause 那条照旧绿 —— 与 §63/§64 那"两处错
   症状相同"、§70 那"两个半边各要自己的断言"同一族。控制还原后 26/0。

5. **工具补了什么**。`tests/outlineprobe.cpp`：新增 E 段（before/after 的目的地形状统计）与
   `describeDest()` —— **数组长度说不出 `/XYZ 0 700 0` 和 `/FitH 650` 的差别，而这一片问的就是这个
   差别**（旧行只印 `array(5)`，那正是它量不到的原因）；D 段顺手加了一份真书的形状统计，第 2 条那个
   "全是 /FitH"就是这么来的。`documenteditor_smoke`：`OutlineWalk` 多一个 `destinations` 字段，
   新助手 `writePositionalOutlineSample` —— 这个形状产品自己写不出来（writer 只写 /Fit），所以只能
   手造，理由与 §63 手造 `/Outlines` 同一句。

6. **数字**。产品代码只有注释（`documenteditor.cpp` +8/−2，writer 一行没动 —— 那三行 `/Fit` 的注释
   从"没有地方带着位置"改成第 1、2 条量出来的说法）；`check(` **797→810**；`outlineprobe.cpp`
   +98/−7；门禁 26/0（控制那遍 1 红）、手跑 0 FAIL / 0 WARNING / 35 note；dev exe
   **`2ee5756de989…` / 1,315,840 字节** —— 字节数与 §74 相同而哈希换了，只改注释也是重链（§30、§43
   那句）。**没重打包**，§16 那"三处相同"欠 §74–§75 两片。

**仍然欠的**：第 3 条那三条路的决定（我建议先 (c)）；§51+§52 与 §55 一起修的那一片（排序里第 1、4 条，
下一条）；§47；§49 那 28 个括号；§74 那句 `%1` 计数句的英文单复数；其余模式的早退仍不杀进程；装上的那份
上重看 ✕ 的像素与"预览时删文件"；§74 那条"层级比祖先还深"钳位之后的文件形状；缩略图 provider 同机制、
书签树第二通道、那五个 imageformats、保存后侧栏不刷新、应用自己开 PDF 仍锁文件、**§75 的提交**。
**已经还上的**：§71 记为"推出而未量"的那条后果 —— 现在它在文件上量到了，而且钉成了两条会替它喊红的
断言；同一遍顺手量出"真书的书签全是 /FitH 而 Qt 不报位置"，这条以前没人说过。

## 76. 修掉 §52 收窄出来的那半扇：拆分不再带着对方那页的框 —— 而控制顺手量出"修过头会造出一条新的不一致"（2026-10-05）

排序里第一档第 1 条。§51 留了三个出口等他定，§52 用一份跨页表单把其中一条关掉了（"把没页的字段摘掉"
在跨页字段上是错的），于是能不动那个决定就修的部分只剩一句话：**摘掉"在本文件无页可指"的 widget，字段
本身与它的值一律不动。** 这一片做的就是那一句话，没做的部分写在第 7 条。

1. **落点只有一个，而且不是"删页的地方"和"拆分的地方"两处**。在 `writeOne` 里、页切完之后、任何
   `QPDFWriter` 存在之前，对**每一份输出文件**都跑。为什么是这一处：这时 `document` 里的页列表就是这份
   文件将要有的页列表 —— 无论页是走 `删除页面` 在 `apply()` 里就没进来，还是走 `拆分` 在这里被切掉。
   写在两处就会有一处漏，与 §68/§69 那句"整树重建而不是逐条打补丁"同一条理由。

2. **判据来自页，不来自 `/P`**。先逐页收活框
   （`QPDFAcroFormDocumentHelper::getWidgetAnnotationsForPage`），再递归 `/Kids` 摘掉同时满足"没有 `/T`、
   `/Subtype` 是 `/Widget`、且不在这份文件的任何一页上"的孩子。**不看 `/P` 是故意的**：§51 那条读数本身
   就是从 `/P` 读出来的，而没有 `/P` 的框照样合法 —— 拿一个会撒谎的字段当判据，就会把"页没了"读成
   "页从没存在过"。`/Count` 只在原来就有时才重算：源码没写的位置不无中生有（§63 那七个键的账，多一个键
   也是结构）。

3. **before/after 在同一份跨页样本上**（`field-spread`：登记在册的字段没有 `/Rect`，两个纯 widget 一边
   一个）。修之前：拆分两份各自 `terminals=2`、`naming no page=1`；删掉一页 `terminals=2`、`naming no page=1`。
   修之后：两份各自 `terminals=1`、`naming no page=0`、`boxes drawn=1`，删页那份同样 `1/0/1`；**两份的值都
   还在**（`typed across two pages` 各带一份），父字段都还在（`parents=1`）。后两条不是顺手绿的，是这一片
   故意要它绿的 —— §52 说清楚了能摘的只有框。

4. **新加了一条谁都没造过的形状**：**什么都没删的保存，一份不许掉框**（`terminals=2`、两页各画一个、
   零悬空）。没有它，"摘掉无页可指的框"只在"已经有页不在了"的文件上被测过，而一个把判据写反的版本在上面
   那三条里可以全绿 —— 判据反过来用（把"不在活框集合"当唯一条件）就是那种版本。

5. **两条控制，各红各的**。① 整个裁剪关掉 → 红 2 条，正是那两条修复断言，读数退回 `terminals=2/2`；
   "值还在""父还在"那两条照旧绿（它们本来就不该被这个控制影响，也不该被修复影响）。② 把活页判据拿掉
   （每个框都可摘）→ 红 **3** 条，多红的那条就是第 4 条的安全判据（`terminals=1` 而 `drawn=2`）。这一遍
   还量出一条以前没人说过的：**修过头不是"少几个对象"，是造出一条新的不一致** —— 那次读数的
   `unregisteredWidgets=1`，页上照样画着两个框而 `/Fields` 只登记到一个，正是这一片要修的同一族缺陷被
   我反向做出来一遍。

6. **§52 那两条 KNOWN GAP 换成了"修好了"的断言**，字面上不再留 GAP 字样，因为它们钉的现在是行为；
   注释里写清是谁关的（§51 提出、§52 收窄、§76 动手）。

7. **没动的，说清楚，别让人以为第一档第 1 条已经清了**。
   - **§51 最要紧的那一行照旧**：值仍然跟着拆分走到每个收件人。值在字段上，而字段必须留在每份里（§52），
     所以"不删值就修不掉这一条"。它要的是那个"该不该摘值"的决定，不是代码。
   - **平铺那一形（字段与框是同一个对象）整条没碰**：摘它的框就是删用户打过的字。§51/§57/§58 那三条
     平铺形状的 KNOWN GAP 在这次修复之后**全部照旧绿** —— 那是"没伸手"的证据，不是"修好了"的证据。
   - **界面上一个字都没说**：这一片改的是写出去的文件，不是话。要不要在拆分框的小结里事先讲"这一份会
     带着别页字段的答案"，与 §75 的 (c) 是同一类决定，等他定。
   - §55 的 XFA 第二份副本一行未动（它记在"要不要动"那一栏，不是"修了一半"）。

8. **数字**。产品代码：新函数 `pruneWidgetsNamingNoPage()` 约 90 行（含注释）+ `writeOne` 里一处调用 +
   三个 include（`QPDFAnnotationObjectHelper`、`QPDFObjGen`、`<set>`）；测试 `check(` **810→815**（两条 GAP
   改写、三条新增：父还在、值还在、没删页不掉框）；门禁 26/0（中间两遍控制各红 2 与 3，都有用）、手跑
   0 FAIL / 0 WARNING / 35 note；dev exe **`e240078691c5…` / 1,327,616 字节**（比 §75 大 11,776 —— 这一片
   是真加了代码，不像 §74/§75 那样字节数不动或只动注释）。**没重打包**，§16 那"三处相同"欠 §74–§76 三片。

**仍然欠的**：第 7 条那三样（值该不该摘、平铺那一形、界面上要不要事先说）；§55 XFA 要不要动；§47 的修法；
§75 三条路选哪条；§49 那 28 个括号；§74 那句 `%1` 计数句的英文单复数；其余模式的早退仍不杀进程；装上的
那份上重看 ✕ 的像素与"预览时删文件"；§74 那条钳位之后的文件形状；缩略图 provider 同机制、书签树第二通道、
那五个 imageformats、保存后侧栏不刷新、应用自己开 PDF 仍锁文件、**§76 的提交**。
**已经还上的**：§52 用一份跨页表单关掉的半扇门，现在真的走了进去 —— 拆分出来的每一份不再带着对方那页的
框；顺带把"修过头会造出新的一致性缺陷"量了出来，并留了一条专门拦它的断言。

## 77. §51 那个决定走的是中间那个出口：答案不再写进没有那一页的文件，而两个框在按下之前就说 —— 判据是这一片真正的产出（2026-10-05）

他那句是"动 §51 那条，值和界面的问题一起理"。§51 当时列了三个出口：**默默摘掉**、**摘掉并在小结里说
一句**、**留着不动只在文档里写明**。这一片走中间那个，两半一起做。

1. **"这份里没有活框"是错的判据 —— 这一片真正的产出是把它量出来。** 造了第三形
   `writeBoxedAndValueOnlyFields`：同一份文件里两个登记字段，一个带框（`/Rect` + `/P`，画在第 1 页），
   一个**天生没有框**（只有 `/T` `/FT` `/V`；PDF 2.0 §12.7.5.2 允许 —— 默认值或脚本读的东西）。删掉
   第 1 页之后量出来：**两个都"在这份文件里没有活框"**，所以那条判据会把一个合法默认值一起清掉。
   文件上分得开它们的是 `/Rect`。于是规则收成一句：**本来画在页上、这份里没画 → 清掉 `/V`**；
   `/DV` 一律不碰（那是表单作者写的默认，不是谁打出来的答案）。`readFieldTree` 从此每条终端都报
   `hasRect` —— 不报就量不到这件事。

2. **落点还是 §76 那一个**（`writeOne` 里、页切完之后、writer 存在之前，每份输出都跑），函数改名
   `tidyFormForThisDocument()`，返回 `{boxesDropped, valuesCleared}`。**撤销是安全的**：这个类不持
   QPDF，每次保存都从源文件重放一遍，所以被清掉的值只在那一次写出的文件里没了 —— 撤销那一步再存，
   值就回来。

3. **读数**（都在文件上，`MEASURE` 那些行）：
   - 删掉字段那一页 → 文件仍登记那条字段，值从 `typed on page 1` 变成**空**；**控制**（删的是别的页）
     值照旧在 —— 所以"空"是关于页出去这件事，不是读法造出来的。
   - 拆分：拿到那一页的那份**留着**答案，没拿到的那份**清空**。§51 说这是最要紧的一行，这一行关掉。
   - 层级 `form.answer`：父照旧登记，值清空。
   - **跨页那一形（§52）两份各自仍带着值** —— 这条**故意没变**：每份都还有一个画得出来的框，所以
     "本来画在页上"与"这份里没画"能同时成立的是平铺那一形，不是跨页形。§52 那句"字段必须留在两份里"
     由此从一条钉住的说法变成有值读数撑着的行为。
   - `boxed-and-value-only`：删页后 `("" / "a default with no page")`；**什么都不删的保存**：
     `("typed on page 1" / "a default with no page")` 两条都在。

4. **上一片自己埋的雷这一片炸出来，值得单记一条规矩**：§51 那三条 KNOWN GAP 把值 `.arg()` 进了
   **消息**而没有断言。修完之后消息还在说"仍带着答案"，而文件里已经没有 —— **一条印在消息里的读数
   不是断言**，它会在修好之后继续绿着并且开始说谎。这一片把三条都改成值进断言，并把"仍然欠的那半"
   （字段还登记着、`/P` 还悬空）留在同一条消息里说清楚。

5. **界面那一半**：`删除页面` 与 `拆分文件` 的小结在按下之前说这条规则，**只在真有表单时说**。为此
   模型加 `hasFormFields()`，它读一次在 `open()` 里（与 `sourceEncrypted` 同一条理由：这个类不持
   QPDF，而框每改一个字都要问一遍），并且**把合并进来的文件也算进去** —— `inspectImport()` 顺手回一个
   bool、`Import` 存下来；否则"先合一份带表单的进来再拆"会明明在动别人的答案却什么都不说。句子是
   不带数字的规则句（"这一份里不会出现那一页上填的答案，字段本身还登记着"）。

6. **屏幕上读到的是拆分框**（`-Mode splitsagain -Doc H:/tmp/forms/field-flat.pdf`，两轮各读到一次），
   英文形在，段落换行也在（小结里它前面是 `\n\n`，§39 那条规则的形状）。**删页框那句没在屏幕上重看** ——
   正负两条都在 `documenttab_smoke` 里钉住了，欠一次眼睛。

7. **三条控制，各红各的**（每个半边都有自己的断言 —— §70 那条规矩的又一次）：① 判据里去掉
   `drawnSomewhere` → 红 2，其中一条正是第 3 条最后那句"没删页也不许清值"；② 去掉 `drawnHere` →
   红 **10**（合并改名、`/TM` 别名、跨页两份带值、拆分里带页那份……整族"值应该还在"全炸）；③ 界面
   条件改成"总是说" → 红 1（无表单的那条负向断言）。

8. **数字**。产品代码：tidy 函数扩成两件事 + `hasFormFields()` + `inspectImport()` 多一个出参 +
   `Import` 多一个字段；`deletepagesdialog.cpp` / `splitpagesdialog.cpp` 各一段；`documenteditor.h`
   三处。目录 **574→576**（两句新话，英文形都填了；numerus 46 / 形 92 未动）。`check(`
   **documenteditor_smoke 810→827**、**documenttab_smoke 376→383**。门禁 26/0（三遍控制各有用）、
   手跑两遍 0 FAIL；dev exe **`3f17cb26620a…` / 1,330,176**（比 §76 再大 2,560）。**没重打包**，
   §16 那"三处相同"欠 §74–§77 四片。

**仍然欠的**：删页框那句的屏幕一遍；**每份具体会清空几个字段**（现在是不带数字的规则句 —— 要出数字就
得把"这个字段的框落在原文件第几页"那套映射建起来，与 §70 的编号基准同一族活）；§55 XFA 要不要动；§47
的修法；§75 三条路选哪条；§49 那 28 个括号；§74 那句 `%1` 计数句的英文单复数；其余模式的早退仍不杀进程；
装上的那份上重看 ✕ 的像素与"预览时删文件"；§74 钳位之后的文件形状；缩略图 provider 同机制、书签树第二
通道、那五个 imageformats、保存后侧栏不刷新、应用自己开 PDF 仍锁文件。
**已经还上的**：§76+§77 两片同一次提交记下了（`ee16d12`，9 个文件、+796/−138），当天推到 `origin/main`
（`0180b48..ee16d12`，远端与本地同一个哈希）；§51 三个出口里中间那个走完了 —— 答案不再写进没有那一页的文件，而且这话在按下之前就说；
§52 那句"字段必须留在两份里"现在有值读数撑着；而"没有活框"这条看起来最自然的判据被证明是错的，错在
会把一个合法默认值一起删掉。

## 78. §47 那一族的另一半动手了：合并进来的批答不再指着这份文件里看不见的注释，而 popup 从此各归其主 —— 顺带把 §77 那句"0 WARNING"量成假的（2026-10-05）

他那句是"按第一条路往下做"，也就是 §47 三条路里那条"只修 popup 那半 + 悬空就摘"。这一片动了 90 行
产品代码，而它真正的产出有两件：一条**新读数分法**（合法的跨页链与复制带过来的死链必须分开数），
和**上一片一句读数的证伪**（§77 写的"0 WARNING"当场是 97 行）。

1. **落点还是 §76/§77 那一个**：`writeOne` 里、页切完之后、writer 存在之前，每份输出都跑；紧接
   `tidyFormForThisDocument()` 之后一行，新函数 `tidyAnnotationLinksForThisDocument()`，返回
   `{popupsMadePrivate, threadsDropped}`。两半各自的规则：
   - **popup**：`/Popup` 指的东西其 `/Parent` 不指回来 → 给这一份复制自己造一个私有副本，逐键搬，
     **只有 `/Parent` 留在外面**（那正是错的那个键），搬完把 `/Parent` 写成这个注释自己。这不是判断
     —— PDF 2.0 §12.5.2.17 只给一个 popup 一个主人，两种坏法（共享、指着供体那页的注释）都必然有
     一半是错的。其余键（含外观）照旧共享，与 §76 对 widget 外观流做的是同一笔交换。
   - **批答**：`/IRT` 指的注释**这份文件任何一页都不显示** → 摘掉 `/IRT` 与 `/RT`；话（`/Contents`）
     一个字不动。判据从页问、不从 `/P` 问，跟 §76 同一条理由。
   - **故意没做，写在明处**：把一条死链改指成"本页那唯一一个看着像目标的便签"（用 `/Contents` 相等
     挑候选）。合并两次那一形里它每次都能挑中，看着像可证 —— 但挑的是文字而不是身份，两个便签文字
     相同就接错对象；接错比留一条死链更坏，所以这一支不进产品，也不悄悄做掉再补一句注释。

2. **一条新的读数分法，因为原来那条把两种形状焊成一个数。** §47 的仪器只有 `repliesNamingForeignTarget`
   （"目标不在本页"），而 PDF 2.0 §12.5.5 明确允许批答指着**同一份文件另一页**的注释 —— 于是修好之后
   那个数照样不为零，用它判修不修就是猜。这一片把它拆成 `repliesOnAnotherPage`（合法）与
   `repliesOnNoPage`（复制/删页带过来的死链）。为了让这两种形状在文件上真的长成两样，造了
   `writeCrossPageThreadCopy`（便签在第 1 页、批答在第 2 页）：**以前这里没有跨页那一形**，所以
   "合法"与"坏了"这两种东西从来没有被分开量过 —— §77 那句"看不出两种合法形状之差的判据"同一族。

3. **读数**（都在文件上）：
   - 合并两次的样本，修之前（§47 钉住的那组）：`shared=1 wrongOwner=2 strayOwner=2 dangling=2`；
     修之后：`notes=4 popups=2 replies=0 shared=0 wrongOwner=0 strayOwner=0 dangling=0`，而
     `reply texts=2`。**`notes` 从 2 变 4 而批答的话一条没少**，这一对读数才是"摘的是链不是话"
     这句话的形状 —— 单看 `replies=0` 会被读成"两条批答没了"。
   - 跨页那一形：删掉谁都不在第 3 页 → `kept replies=1 onAnotherPage=1 popups=1`，链原样不动；
     删掉便签那页 → `cut replies=0 onNoPage=0 notes=1 text kept=1`，死链摘掉而话和便签本身都还在。
   - "没坏也不许动"那一遍：`linked-donor.pdf`（链本来就是对的）删第三页再存 —— popup 的 `/Rect`
     与存前逐字相同、条数不变、四个违例计数全 0。

4. **五遍控制，每一遍红的都不是同一批。**（都重建，不是 `--run-only`：先用 `--run-only` 试了一次，
   它报 26/0 —— 跑的还是旧二进制，那句红根本没发生过；§71 那条"改不了退出码的 FAIL 文本只是装饰"
   的孪生：**跳过构建的控制跑的是上一版的代码**。）
   - 整遍摘掉 → 红 **4**（合并那 3 条 + 删便签页那 1 条）。
   - 只关 popup 那一半 → 红 **1**（popup 归属那条），而批答读数照旧对（`dangling=0 replies=0`）。
   - 只关摘链那一半 → 红 **3**，而 popup 归属照旧绿（`shared=0 wrongOwner=0`）。
     后两遍合起来就是 §70 那句话再一次成立：**两半谁也看不见谁，各要自己的断言**。
   - 边界从"这份里任何一页"收窄到"本页" → 红 **1**，正是那条合法跨页的守卫 —— 边界自己有一条只归它。
   - 反过来做过头（不管对错把每个 popup 都摘掉）→ 红 3，其中一条是**新加的"没坏也不许丢东西"守卫**：
     前四遍都看不见它，只有把修复做过头它才红。

5. **仪器自己错了一次，值得单记。** "没坏也不许动"那条件我先写的是比 popup 的**对象号**，门禁当时就红：
   `1 popups with ids 11, was 1 with ids 13`。查下去不是产品抄了框，而是 **qpdf 每次写都把对象重新
   编号** —— 号根本不是身份，跨写比号必然红，无论有没有拷贝。改量 popup 自己的 `/Rect`（东西的属性，
   不是它的门牌）。**规矩：跨一次写入要认"是不是同一个东西"，比这个东西自己的字段，别比它在文件里的
   位置或编号**；顺带这条也说明了为什么那个守卫**看不见**"没坏却白抄一份"—— 抄出来的副本 `/Rect`
   一模一样，而号本来就会变。这一半留在代码里的条件（`/Parent` 已经指对就什么都不做）而不是留在断言里，
   记录里写清楚而不是假装量到了。

6. **§77 那句"手跑两遍 0 FAIL / 0 WARNING"被这一片量成假的。** 同一条命令，`WARNING` **97 行**，
   全是同一个形状：`dictionary key /AcroForm: operation for dictionary attempted on object of type
   null`。出处就是 §77 自己加的两处 —— `open()` 里读 `sourceHasFormFields`、`inspectImport()` 回的那个
   bool：没有表单的文件 `/AcroForm` 是 null，再往 null 上问一个键，qpdf 就印一行（`source.pdf` 一个
   文件就响 59 次）。§58 那条"对一个 null 再问一次键也报 WARNING"**在同一片代码里第三次成立**。
   两处都加了 `isDictionary()` 判断，现在 0 行。**规矩：一句"0 WARNING"是一次读数而不是一个愿望**，
   写进记录之前先数；这一条撤回写在明处，不悄悄把 §77 那句话改掉。

7. **数字。** 产品代码 `documenteditor.cpp` **+105/−2**（新函数约 90 行 + 一处调用 + `#include <map>` +
   §77 那两处的类型判断），`documenteditor_smoke.cpp` **+262/−16**；`check(` documenteditor_smoke
   **827→842**、documenttab_smoke 383 未动；目录 **576 条 / numerus 46 / 0 unfinished 一字未动**
   （这一片没有新的用户可见的话 —— §47 第 5 条那句要不要说，还在他手上）；门禁 **26/0**（五遍控制各有
   用处）、手跑 **0 FAIL / 0 WARNING / 37 note**；dev exe **`26ea1e98b61e…` / 1,336,832 字节**（在
   §77 的 1,330,176 上加 6,656 —— 是真代码，不像 §74/§75 那两次只改注释）。**没重打包**，§16 那
   "三处相同"自此欠 §74–§78 五片。

   那句 **37 note** 比上一片的 35 多两条，就是这一片新加的读数 —— 它们一开始走的是裸 `std::cout`，工具
   照旧报 "35 measurement(s) printed above" 而输出里已经有 37 行 `note:`：**计数器不数没走它的行**，
   "这一遍量了几件事"那个数就和自己印出来的东西对不上。改走本测试的 `note()` 之后两个数才一致。
   **规矩：新加的读数要走那个计数器，否则下一片拿"多少条读数"当账时会少数。**

**仍然欠的**：合并框在按下之前不说"这条批答会断"（§47 三个出口里第一个，与 §77 那句"每份具体会清空
几个字段"同一族 —— 要出这句话得先知道哪一条链会断）；私有副本除 `/Parent` 之外的键是**共享**的，
两份复制可能仍指着同一个外观流对象（与 §76 对 widget 外观流做的是同一笔交换，没症状但没量过）；
**"没坏却白抄一份"这一半只有代码里的条件守着，断言看不见**（第 5 条）；删页框那句表单话仍欠一次
眼睛（§77 欠下的，这一片没建那个模式）；§55 XFA、§75 三条路选哪条、§49 那 28 个括号、§74 那句
`%1` 计数句的英文单复数、其余模式的早退仍不杀进程、装上的那份上重看 ✕ 的像素与"预览时删文件"、
§74 钳位之后的文件形状、缩略图 provider 同机制、书签树第二通道、那五个 imageformats、保存后侧栏
不刷新、应用自己开 PDF 仍锁文件。
**已经还上的**：§47 那一族动手了 —— popup 各归其主、指着这份文件里看不见的注释的 `/IRT` 摘掉，两条
断言从"故意钉住缺口"翻成"钉住修复"；§77 那两处对 null 再问键的 WARNING 流（97 行）关掉，手跑回到
0 行；而 §47 第 5 条那三个出口，第二个已经不再是一个选项。

## 79. 另存为撞上一个正被读着的名字：这件事以前在整份写完之后才说，现在在动第一个字节之前说 —— 顺带把一条旧钉的"因"换掉（2026-10-05）

他那句还是"继续"，挑的是账上那条"保存后侧栏不刷新"。量下来那句话混着两件事，而**混着的那一半不是缺陷**：

1. **原标签不刷新是设计**，不是漏。`documentSavedAs` → 结果开在一个新标签里，星号留着、撤销仍有东西可拿
   （`documenttab.cpp:1437-1453`），`编辑书签` 那行的 tooltip 早就写明"侧栏那棵树要等新文件打开才是这
   个样子"。全仓库把文档交给两个侧栏的地方**只有** `DocumentTab::open()` 一处（`documenttab.cpp:650-655`，
   失败分支在 602-603 清空），也没有 `QFileSystemWatcher` —— 所以"通知了但 model 没重建""图片缓存没失效"
   两条候选一支都不成立：`OutlinePanel::setOutline()` 与 `ThumbnailPanel::start()` 本身就是完整重建，
   缺的只是没人调，而不调是设计。
2. **真的漏在别处，而且以前没人量过**：保存的目标名字已经开在这程序的另一个标签 / 另一个窗口里。
   这一形 `saveAs()` 不拒（它只拒三样：正在读的这份、合并进来的供体、拆分时名字已存在），于是走到最后一
   步 `MoveFileExW(REPLACE_EXISTING)` —— 而那时**整份文档已经重放完、已经写成一个新的临时文件**。

**量**（`tests/documenttab_smoke.cpp` 新加的一形，不是推想）：一个 `DocumentTab` 正读着那个名字，第一
页已经画完、标签空闲得像读者的样子，然后做产品那同一步。读数：**moved=0，win32=5（ACCESS_DENIED）**，
那个名字上的文件还是原来 2 页，被搬的文件原地还在，标签照旧 2 页。四条断言钉住这个前提，因为下面那句
话完全靠它 —— 将来 Qt 哪天放开 delete 共享，这四条会红，而那句拒绝必须在同一次改动里松开，不能继续
拦一个其实写得进去的名字。

**改法**：在 `saveAs()` 已经在逐个看目的地的同一个循环里，多问一句"这个名字现在盖得动吗" ——
`CreateFileW(DELETE, READ|WRITE|DELETE share, OPEN_EXISTING)`，问的就是那一步自己将要问的问题，一个字节
都不碰。先试的方案是"把文件改名再改回来"，那是**真的**去动用户的文档，中途出事就留一个改过名的文件，
所以换掉。**只有 ACCESS_DENIED / SHARING_VIOLATION 算盖不动**：别的失败（ACL、杀软、reparse point）一律
放行，让移动自己去报 —— 把一个未知翻成拒绝，就会停掉今天本来能用的保存。

**两条旧钉的关系是这一片最要紧的一处判断。** `documenteditor_smoke` 里 §24 那块以前用"本进程开着的句柄"
占住目的地，钉的是"写不动时把成品留在原地、消息点名它在哪"。§79 提前看见了那个因，于是那块红 ——
**不是回归，是那块的因被搬到前面去了**。搬的是那块自己的因：目的地改成**只读文件**（preflight 看不见
的一种盖不动：DELETE 打开成功，属性只挡写内容不挡删开，移动自己拒），于是"晚到的那一句"照旧有人钉。
控制证明这个分法不是讲出来的：preflight 恒真 → 红 **2**（新块的话，和"一处都不许留下暂存文件"）；
preflight 恒假（一刀切）→ 红 **10**，其中一条正是旧块那句"**必须是晚到的那句话，而不是早到的拒绝**"。
两个因、两句话、各一条断言 —— §70 那条规矩第四次成立。顺带一条：**"返回 false"这一支两个因都满足，
它不是证据**；分清早到与晚到的是消息和留不留文件。

**界面**：新话一句，走目录（**576→577**，英文形填了，numerus 46 未动），`i18n_smoke` 探针挂上；弹出去走
的是既有那条路（`saveEdits()` 把 error 交给 `WindowDialogs::warning`，标题"保存修改"）。**屏幕上没看**：
这一句要真的在原生另存框里选中一个正开着的名字才出现，而同一窗口里根本不允许两个标签开同一个文件
（`openDocument()` 抬起已有那个，`mainwindow.cpp:1338-1351`），所以要造那一形得跨窗口 —— 记在下面。

**仪器的一条新坑，值得单记**：往一个已有测试文件里加 `#include <windows.h>`，rpcndr.h 的
`#define small char` 会改写这文件里**一个已有的局部变量**（`documenttab_smoke.cpp` 里 800 行外那个叫
`small` 的），编译器在我一行都没碰的地方报"QString 后面接 char 是非法"。处理是跟着来一句
`#undef small`，而不是去改一个这一片没业务动名字的东西。**规矩：给一个已有文件加 windows.h 之前，
先 grep 那文件里有没有 `small`/`interface`/`status` 这类宏名。**

**数字。** 产品 `documenteditor.cpp` **+44**（helper 27 行含注释 + 目的地循环里 17 行）；测试
documenteditor_smoke **+114/−34**、documenttab_smoke **+108/−1**（含一个 `note()` 计数助手，还有一句跟
`#include <windows.h>` 走的 `#undef small`）、i18n_smoke
**+7**；`check(` **842→855** 与 **383→392**；目录 **+90/−85** —— 那几行几乎全是 lupdate 把 `<location>` 的
行号往前挪（`documenteditor.cpp` 长了 44 行），真正新增的只有那一条话，报这个数的时候要说清哪部分是挪动、
哪部分是新增（§38 那条"目录一字未动"的说法从这里开始要带这句区分）；目录 **577/577、numerus 46、0 unfinished**；门禁 **26/0**
（两遍控制各有用）、手跑 **0 FAIL / 0 WARNING**；dev exe **`50688d8e77dc…` / 1,337,344 字节**。
**没重打包**，§16 那"三处相同"欠 §74–§79 六片。

**仍然欠的**：屏幕上那一遍（要跨窗口才造得出那一形）；**锁本身没修** —— 这一片只是不再假装写得进去，
而同一把锁也让用户删不掉自己正开着的文件（壳那一层 §66 解决了预览那条，主窗口这条还在）；账上"保存后
侧栏不刷新"原本那句意思（原标签就显示改动后的文档）= **就地保存**，功能本身还没做，仍在等他定；
§78 那一串（合并框那句"这条批答会断"、私有副本除 `/Parent` 外仍共享、"没坏却白抄一份"只有代码条件守着、
删页框那句的眼睛）照旧；§55 XFA、§75 三条路、§49 那 28 个括号、§74 那句英文单复数、其余模式早退不杀进程。
**已经还上的**：另存为撞上"正被读着的名字"不再走到写完才失败，也不再留一份成品在临时目录；§24 那条
"写不动时把成品留在原地"的钉换成了它自己的因（只读文件）而仍然有效；而账上"保存后侧栏不刷新"这一条
被量成两件事，其中一半是设计，不再当作缺陷挂着。

## 80. §75 的第三条路落地了：书签那一层位置在按下之前就说，而那个数是从文件上数的、不是从框里数的（2026-10-05）

他说的是"先修 §75 (c)"。§75 量到一次保存把 `/Dest` 里带着的页面内位置全拍平（四条手造书签出来全是
`/Fit`），摆了三条路并建议先走 (c) —— **不动数据，只在按下之前讲**。这一片就是 (c)。

1. **为什么这句话必须由文件来数。** 读路 `qtpdfbackend.cpp` 只问 Title 与 Page 两个 role，写路
   `writeOutlineItem` 只写 `/Fit` —— 也就是说**框里那张表正是已经把信息丢掉的那一份**。从它数会永远
   得到 0，于是"有 N 条会丢"这句话会稳定地撒谎。所以计数放在 `DocumentEditor::open()` 里，跟
   `sourceEncrypted` / `sourceHasFormFields` 并排（同一个理由：这个类不持 QPDF，而框每改一个字都要问
   一遍），走 `QPDFOutlineDocumentHelper` 把 `/Outlines` 整棵树（含嵌套）看完。
   判据：**`/Fit` 与"只指着某一页"之外的一切都算带着位置** —— `/XYZ` 的 top 与 zoom、`/FitH` 的高度、
   `/FitV` `/FitR` `/FitB` `/FitBV` 都是。解析不出来的目的地的**不数**：这句话是关于位置的，不是关于
   "书签还能以别的什么方式写得比 /Fit 更准"，混在一起会让一句诚实的话变成一个说不清的清单。

2. **屏幕上读到的那一遍用的就是真书**（`-Mode bookdialog -Doc H:/tmp/outline/real-book.pdf`）：184 条
   书签**全是 `/FitH <top>`**（§75 量的形状），所以那句话在屏幕上写的是 184，不是一份小样本上的 2。
   两轮各读到一次，英文形在（`184 bookmarks in this document point at a place inside a page, not just
   at the page...`），而且**同一屏上"开框不动"的那一遍没有这句话** —— 那句话挂在"这确实是一个请求"
   那一支里，负向的一半在屏幕上自己出来了，不用另造一例。

3. **控制三遍，红的地方各不相同**（每遍都重建 —— §78 那条"跳过构建的控制跑的是上一版代码"）。
   - 计数恒 0（等于这一片没做）：**1 + 3** 红 —— 模型侧那条"四条形里两条带位置"，加上屏幕上那三条
     （数、句子、句子里的"变成什么"）。**同一遍里"保存之后一条都不带"那条照旧绿** —— 这正是为什么
     这一片把它钉成一对：一个恒说 0 的计数器只看后半是抓不住的（§67 那一族的空断言）。
   - 无条件就说（哪怕这份一条都不带）：**1** 红 —— 正是那条负向断言（"没有位置风险的文件不许听见这句"）。
   - 把 `/Fit` 也数进去（判据放宽一格）：**2 + 3** 红 —— 两条数数的断言都从 2 变 3，屏幕上那三条也炸；
     所以 `else if (dest.isNull())` 之外那一格 `/Fit` 例外是**有人守着的**，不是顺手写宽的。

4. **数字。** 产品 `documenteditor.cpp` **+62**（计数函数 55 行含注释、`open()` 里 6 行、一个 include）、
   `documenteditor.h` **+11**（ accessor + 成员）、`bookmarkdialog.cpp` **+16**（一句 `tr()` + 为什么在
   这里说）；测试 documenteditor_smoke **+23**、documenttab_smoke **+113**（样本得手造 —— 我们的 writer
   写不出带位置的书签，§75 为同一件事说过一次）、`gui-probe.ps1` **+11**（在 Add-one 那一轮后面读一句
   小结，中英两种 needle 都认，§42 那条"只认中文会读成没有"）；`check(` **855→859** 与 **392→401**；
   目录 **577→578**（numerus **46→47** —— 这一条本来就是 `%n` 计数句，所以英文两形都填了，
   §74 那一类"计数句没有单复数"的坑从这里开始少一处），i18n 探针挂上；门禁 **26/0**、手跑
   **0 FAIL / 0 WARNING**；dev exe **`6ad959573542…` / 1,341,952 字节**。**没重打包**，§16 那"三处相同"
   欠 §74–§80 七片。

**仍然欠的**：这一片**只说话，不改数据** —— §75 的路 (a)（把每条 `/Dest` 原样读进 `OutlineEdit`、原样
写回，要连 `operator==`、撤销那行、`unchangedFrom` 那句地基一起动）还是等他定；拆分时每一片要认得出
自己的目的地这一条也在 (a) 里面。屏幕上**中文那一遍没读**（这一片的屏幕那一遍是英文界面下的，因为
harness 起的实例自带 `Language=en`）；英文**单数**那一形只在目录里，没在屏幕上见过（要一份只有一条
带位置书签的文件，而那种文件得手造两份）。§79 与 §78 那两串照旧：跨窗口撞名那一形、锁本身、
就地保存、合并框那句"这条批答会断"、私有 popup 副本共享的键、删页框那句的眼睛。
**已经还上的**：§75 三条路里推荐的那一条做完了 —— 数据仍然会丢，但**不再不为人知**；而"这个数从哪儿
数的"这一件事，从这一片起在代码和记录里都只有一处答案。

## 81. 注释层画不画：量出来是画的，而"表单不可见"那句话的范围被钉小了 —— 这一片先被自己的仪器骗了一次（2026-10-05）

他挑的是我列在"还剩哪些缺陷"里那条**从没问过的未知**：文件里已有的高亮/便签/矩形，在这份阅读器里
到底画不画。§54/§57/§58 量的是**表单 widget**，从没问过别的注释类型。

1. **第一遍量错了，而且错得像个发现。** 我直接调 `QPdfDocument::render(0, size)`，三种注释各得
   **0 像素差** —— 看着就是"这一层不画"。但那条调用**不是应用走的路**：本项目的
   `RenderOptions::annotations` 默认是 **true**（`document.h:52`，理由就写在注释里："别的阅读器都显示
   高亮而这里不显示，读起来像坏了"），而 Qt 自己的默认 render flags 不带 `Annotations`。
   于是那三个 0 是**我这条裸调用的性质**，不是产品的。**§58 那条"仪器看不见某样东西时，产出的发现
   长得跟那样东西真的不在一模一样"，这一片落在我自己头上，而且是先写下结论再发现的。**

2. **换到产品的路上重测**（`QtPdfBackend::render(0, 595x842, RenderOptions{})`，同一页有注释/无注释
   逐像素比，差值边界一起报），三个形状**全都画**，而且都**不带 `/AP`**（PDF 2.0 §12.5.3 要求阅读器
   自己生成外观）：

   | 形状（都无 /AP） | 与同页无注释版差多少像素 | 差值边界 | 注释自己的矩形 |
   |---|---|---|---|
   | `/Highlight`（带 `/QuadPoints`） | 4094 | 70,52 230x18 | 70,50 230x22 |
   | `/Square` | 496 | 70,82 190x60（正好） | 70,82 190x60 |
   | `/Text` 便签 | 265 | 321,53 18x18 | 320,48 24x24 |
   | `/Widget /Tx`（无 /AP） | **0** | — | — |

   两个对照把这张表钉住：**把同一个矩形画进页面的内容流**（不是注释）→ 5060 像素差、差值边界正好等于
   那个矩形，说明仪器看得见这个位置的东西；**把 `annotations` 关掉再渲一次** → 三种各自差的像素
   **正好等于**上面那一列（4094 / 496 / 265），说明这一层就是那个标志带来的，不是别处重排了页面。
   表单 widget 那一行仍读 0 —— 也就是说 §54/§57 的结论活着，但它的范围现在钉死了：**不是"注释层
   不可见"，是"表单 widget 这一类不可见"**。我上一轮把"高亮画不画"列为未知，这一片把它**取消**了 ——
   "要不要接注释绘制"这个议题不用立。

3. **屏幕那条通道本身被证明会说话。** `-Mode formpage` 四份不同文件跑出**完全相同的 band 计数**
   （每带都是 18600，饱和）和**四个不同的 PNG 哈希** —— 裁条里坐着别人的 topmost 窗口，变化的是那个
   窗口自己的时钟（图我看了，是一整屏深色聊天界面）。§66 为同一件事换过 PrintWindow。**这一片没改
   产品，改的是账和规矩**：AGENTS.md 添了两条（样本清单加上 markup 三种；**像素问题从头测，别从屏幕
   测**，并点名"直接调 `QPdfDocument::render` 会得到假阴性"）。§54/§57 的屏幕读数没被推翻 ——
   widget 那一支现在有了从头测的独立复现，两边都是 0。

4. **控制不需要额外跑**：这一片是量，不是修；但仪器有两个"必须不为零"的读数（内容流对照、标志开关
   对照）咬住三个"必须不为零"的注释读数 —— 若哪天 `annotations` 默认被改回 false，三个注释读数会红，
   而"widget 读 0"那条不会红。两半各归自己（§70 那条）。

5. **数字。** **产品代码一行没动** —— dev exe 与 §80 同一颗 `6ad959573542…` / 1,341,952 字节；测试
   `documenteditor_smoke` **+401/−0**（三个注释样本生成器、一个内容流对照生成器、一个注释读数器、
   15 条断言，`check(` **859→874**、note **43→45**）；样本目录多三份 markup 文件（AGENTS 那行跟着
   改）；目录 **578 / numerus 47 / 0 unfinished 一字未动**（没有新的用户可见的话）；门禁 **26/0**、
   手跑 **0 FAIL / 0 WARNING**。**没重打包**，§16 那"三处相同"欠 §74–§81 八片。

**仍然欠的**：`-Mode formpage` 本身还没换成 PrintWindow（下次真要读屏幕像素之前先换）；**表单 widget
为什么不画**没查（Qt 的 PDFium 对 widget 外观的处理，可能是版本/编译选项，也可能是需要
`NeedAppearances` 那套生成 —— §58 已经量到那一层指令在这里值 0 个像素），这一片只钉住"不画"这个事实
并把它与"注释层不画"分开；§80 那一串（§75 的路 (a)、中文那一遍、英文单数形没见过）、§79 那一串
（跨窗口撞名、锁本身、就地保存）、§78 那一串、§55 XFA、§49 那 28 个括号、§74 的单复数、其余模式早退
不杀进程，照旧。
**已经还上的**：那条"从没问过的未知"问完了 —— 高亮/矩形/便签在这份阅读器里**看得见**；而 §54/§57
那句"0 像素"从"表单层"收窄成"表单 widget"，并且第一次有了不依赖屏幕的复现。

## 82. 表单 widget 为什么不画：量出来是"认名字"，同一份画挂在 /Square 上就画满 6000 个像素（2026-10-05）

他挑的是 §81 末尾那条"仍欠"：**表单 widget 为什么不画**。这一片不改产品，只把那句话从"这一类不可见"
问到"是哪一个键决定不可见"，而答案顺带把 §57/§58 的两次收窄又推进一步。

1. **同一份画，五种挂法**（都从 `source` 的第一页出发，只改一个键）。画是 §57 早就写好的那段
   `/AP /N`：填 0.70/0.70/0.90 的 200×30 矩形 + 一句 `APPEARS-HERE`，`/BBox` 就是 0 0 200 30，
   `/Rect` 就是 60,600→260,630 —— **不缩放才看得出"没画"和"画在别处"的差别**。渲染一律走
   `QtPdfBackend::render(0, 595x842, RenderOptions{})`（§81 那条规矩），与同页无注释版逐像素比。

   | 形状 | /Subtype | 进 /AcroForm | 带 /FT /T /V | /NeedAppearances | 像素差 | 差值边界 |
   |---|---|---|---|---|---|---|
   | square 带画 | `/Square` | 否 | 否 | 否 | **6000** | 60,212 200x30（正好等于自己的矩形） |
   | square 带画 + 字段键 | `/Square` | 否 | **是** | 否 | **6000** | 同上 |
   | widget 孤立 | `/Widget` | 否 | 否 | 否 | **0** | — |
   | widget 注册 | `/Widget` | **是** | 是 | 否 | **0** | — |
   | widget + 指令 | `/Widget` | 是 | 是 | **是** | **0** | — |
   | §57 那份生成器 | `/Widget` | 是 | 是 | 否 | **0** | — |

   6000 = 200×30，一格不落。每一行先钉**文件上这一页确实带着那份画而且解得开**（新
   `pageAnnotationPaints()`，走页的 `/Annots` 而不是 `/AcroForm /Fields` —— 后者的读数器
   `appearancesWithPaint()` 天生量不到"没注册"的那两行，而那两行正是这一片要问的）。

2. **所以决定它的是那一个名字，而不是"像不像字段"。** 把一个 `/Square` 装上 `/FT /T /V` 它照样画满
   6000，而一个 `/Widget` 什么字段键都不带（不注册、没有 /T）就一格都不画。`/AcroForm` 在不在、
   `/NeedAppearances` 开不开，都不改变那个 0。**§58 那句"表单的指令层在这份阅读器里值 0 个像素"
   自此有了理由，而且不是它当时那个理由**：不是指令被忽略，是**整个 subtype 在问指令之前就被跳过了**。

3. **这条路我们没有开关没按。** `QPdfDocumentRenderOptions::RenderFlag` 只有
   `Annotations / OptimizedForLcd / Grayscale / ForceHalftone / TextAliased / ImageAliased /
   PathAliased`（`qpdfdocumentrenderoptions.h:24-33`），而这一版 QtPdf 的头文件里**没有任何注释或表单
   类**（公开 9 个类、私有 8 个头，逐个数过：没有 `QPdfAnnotation`、没有 `QPdfForm`）。
   `Qt6Pdf.dll` 里 `FPDF_*` 只余下 2 个字符串，PDFium 是静态链进去的，**机制读不到**（没去替它编一个）。
   也就是说"widget 不画"在我们能碰的这一层不是一处没配好的调用，而是**没有可配的东西**；真要看得见，
   只能自己把 `/AP /N` 那层 Form XObject 画到页面位图之上，那是另一件事、另一个量级。

4. **三次控制，各红各的地方**（每次都重链 —— §78 那条规矩）。
   - 只把两行 square 的 `/Subtype` 改成 `/Widget` → 红 **6**，正好是那两行的三条"必须不为零"各自乘二；
     widget 那三行照旧绿。**这一条是"名字就是开关"这个结论自己的断言**，不是复述表格。
   - 探针生成器不写 `/AP` → 红 **11**（5 行 `carried==1` + square 两行 6 条），**而三条
     "widget 读 0"照旧绿**。这一条把"0 像素"这种钉的性质说干净：**一行"读 0"自己不能证明任何东西**，
     它必须有同一行里"文件确实带着这份画"那句陪着，否则"widget 被跳过"和"这页本来没东西"长得一模一样
     （§81 那次假阴性就是这个形状，只是当时没人能区分）。
   - 产品在 `qtpdfbackend.cpp` 不再 OR 上 `RenderFlag::Annotations` → 红 **12**（§81 那三种注释各 2 条
     + §82 两行 square 各 3 条），widget 那三行两列都还是 0、照旧绿。**这条是唯一一条动产品的控制**，
     它证明这些读数确实从产品的渲染路上取，也再答一次"没有别的开关没按"。

5. **一块仪器上的账，是它自己撞上来的**（记在这儿因为它改动了一句被引用过二十次的用法）。控制还原之后
   我照常报 dev exe 哈希，量到 `8e0cd84f81d0…` 而 §81 报的是 `6ad959573542…` —— 而这一片**产品代码一个
   字没动**。于是把同一份源码连续重链两次，字节级比：**只差 4 个字节、两处各 2 字节**，都在同一个值上
   （`0x118` = COFF 头的 `TimeDateStamp`，`0x104244` = debug directory 那条里同一份时间戳），大小不变
   1,341,952。**结论：这颗 exe 不是可重现链接，哈希记的是"哪一次链接事件"，不是"哪一份代码"。**
   所以"只改注释也是重链，§30/§43 那句"仍然对，但它当时的证据力比写出来的弱 —— **一个字不改、只 touch
   一下也会换哈希**；反过来也一样：哈希相同不代表源码相同。**§16 那"三处相同"不受影响**，因为它问的是
   同一份字节有没有原样走到装上的那一份（打包是复制、不是重链），这件事只有"重新构建再比哈希"才是假的。
   **差点把机制编错**：第一次读 PE 时间戳读在 `lfanew+12`（那是 `SizeOfImage`）读出 0，于是几乎写下
   "差别不是时间戳"。**规矩：解释一次字节差别之前，先把差的是哪些字节找出来。**

6. **推论一句，没测**：既然打印（`printjob.cpp:190`）、导出图片（`imageexport.cpp:82`）、导出 Word
   （`wordexport.cpp:790/1071/1145`）、缩略图（`thumbnailpanel.cpp:80`、
   `thumbnailprovider.cpp:111`）、预览窗格（`previewhandler.cpp:846`）用的是同一个 `render()`，
   那"widget 不画"就一样地带到**打印纸和导出的图片**里 —— 用户填好的表单值在别家阅读器看得见、
   在这份的打印/导出里不存在。**这是同一条代码路的推论，不是另一张量出来的表**，要钉就得给导出那几条
   路各跑一张位图。

7. **数字**。**产品代码 +0/−0**（控制 C 改过的那个文件还原后 `cmp` 逐字节相同、`git diff` 无条目）；
   测试 `documenteditor_smoke` **+337 行**（公共外观流抽成 `paintedAppearance()`，§57 的生成器改用它；
   新 `writeAppearanceProbeCopy()` + `AppearanceProbe`；新 `pageAnnotationPaints()`；度量块 145 行），
   `check(` **874→884**（源行 10 条，运行时断言 **25** 条），note 源行 45→48、**手跑打印 45→51 条**；
   目录 **578 / numerus 47 / 0 unfinished 一字未动**（没有新的用户可见的话）；门禁 **26/0**、
   手跑 **0 FAIL / 0 WARNING**；dev exe 这颗 **`0f744cd07d73…` / 1,341,952**（见第 5 条：这个数只说明
   它是哪一次链接的产物）。**没重打包**，§16 那"三处相同"欠 §74–§82 九片。

**仍然欠的**：widget 画不画这件事**没修**，也修不到 —— 要么接受它，要么自己解 `/AP /N` 画到页面位图上
（等他定，而那是一整件新事）；第 6 条那句打印/导出推论**没测**；**要不要给链接加 `/Brepro`**（让哈希
变成内容指纹、让"三处相同"在重建后仍可复现）也等他定 —— 一片链接开关，动的是包里所有 exe/dll；
`-Mode formpage` 还没换 PrintWindow；§80 那一串（§75 的路 (a)、中文那一遍、英文单数形）、§79 那一串
（跨窗口撞名、锁本身、就地保存）、§78 那一串、§55 XFA、§49 那 28 个括号、§74 的单复数、其余模式早退
不杀进程，照旧。
**已经还上的**：§81 那句"表单 widget 为什么不画没查"问完了 —— 查到的答案是**它认的是 subtype 这个名字**，
而这条只有在"同一份画换个 subtype 就画满"这个对照下才说得出；§57 那个 0 在产品的渲染路上原样复现
（结论活着，通道死了），§58 那句"整层指令值 0 像素"从此有一个量过的理由，而它不是指令被忽略。

## 83. 重打包并重装：§16 那笔欠了九片的账对上，而包里多出来的 25.9 MB 没人叫它（2026-10-08）

他说"重打包并重装"。这是记账的一片：没动产品代码，把 §74–§82 送进装上的那一份，并把它顺带量到的
两件事写下来 —— 一条是我自己的仪器说了一句假话，一条是包自己长胖了而没人叫。

1. **顺序**：`python tools/build_all_tests.py`（**26/0**，目录 578 / numerus 47）→ 
   `packaging/build-package.ps1 -Sign -Cert 221C6B38…0A83 -Install` → 
   `packaging/verify-package.ps1 -Package …` → 三处逐字节比 → 装上的那份起一次窗口。

2. **§16 平了**：三处全等（构建树 = 包内 = `WindowsApps\…\bin`），一次看七颗 ——
   `xyzpdf.exe` **`0f744cd07d73…` / 1,341,952**、`xyzpdfpreview.dll` `f8e17a004f76…` / 133,632
   （§66 那条预览修复住在这里）、`xyzpdfshell.dll` `5bdb230257a7…` / 37,888、
   `xyzpdfthumbnail.dll` `727b03acec88…` / 90,112、`qpdf30.dll` `5ce2fc864988…` / 2,397,696、
   `jpeg62.dll` `feb06772a974…` / 722,944、`z.dll` `5aef909e51b8…` / 91,648。
   `turbojpeg.dll` 只在构建树里、不带走（§39 那条照旧）。**这一片是 §82 那条新规矩的第一次实操**：
   哈希对上靠的是"打包复制同一份字节"，**不可能**靠重建再比 —— 重建一次就换（链接时间戳两处）。
   门禁之后 exe 没被重链（`0f744cd07d73…` 与 §82 记的那一颗相同），因为源码没动、ninja 不重链。

3. **我自己那条假绿，值得单独钉**。第一版三处比对我把两颗壳 DLL 的名字记错了（写成
   `xyzpdfcontextmenu.dll` / `xyzpdfthumbprovider.dll`，实际是 `xyzpdfshell.dll` /
   `xyzpdfthumbnail.dll`）。PowerShell 里 `Get-FileHash` 对不存在的路径报错返回 null，而循环变量
   **留着上一轮的值** —— 那一行于是印出 `build=F8E17A004F76 pkg=F8E17A004F76 installed=F8E17A004F76
   same=True size=0`：**三个路径都不存在的比字节行，照样能印"相同"**。改成 Python 并把"文件在不在"
   读进同一行，才看见那两行其实是 `pkg=None installed=None`。规矩：**比字节的每一行要带上大小与
   "存在"**，`same=True` 单独一句不算比过（§67 那条"计数断言不能数自己放的东西"的孪生：这一条是
   "相等断言不能比两个不存在的东西"）。

4. **包长胖了，而且没人叫它**：`XYZPDF_0.1.0.0_x64.msix` **28.1 MB**（§73 是 17.5 MB），解包
   **66.5 MB / 69 个文件**（§73：40.5 MB / 67）。payload 对照写着 `gained 2 file(s), lost 0`
   —— `dxcompiler.dll` + `dxil.dll`，**25.9 MB**，`verify-package.ps1` 自己报
   "carried and may not be needed: … This program paints with the raster engine."
   这是 §60 那条"windeployqt 每次自己决定带什么"的第二次显形，而这次更贵。更难看的一处：
   **同一个文件名在两处不是同一个东西** —— 构建树里 `dxcompiler.dll` 14,317,000 字节
   （`a5aa1d9a95bf…`），包里那颗 25,684,592 字节（`4b8da3c8d211…`）。**谁供的这一片没查到**
   （Qt 的 `bin`、`vcpkg_installed`、`Windows Kits\10\bin` 三处都没有这个名字的文件），
   没替它编。等他一句话：剥掉这两颗（就是 §60 那条留在 `build-package.ps1` 第 2b 步的那个口子），
   包就回到 §73 那一档；不剥，上传时这 10 MB 是白带的。

5. **装上的那份现在是中文**：`-Install` 自己先卸（§73 立的那条），**设置容器跟着走** —— 
   `-Mode inventory -Installed` 起得来，26 个非空 Name，菜单是 `文档(D) / 转换(G) / 帮助(N) /
   Language / 帮助` 而状态栏是"就绪"。所以 §49/§61 那两处英文读数**对装上的这一份不再成立**
   （同一句话 §73 记过一次，这是第二次）。

**仍然欠的**：**装上的那份没读 §80 那句位置书签话** —— harness 递给包里的实例只有一个不带文档的
启动（没有 AppExecutionAlias），要读它得先让 `openpicker` 那一支把书选进去，那是一件新仪器（正负两条
断言已在 `documenttab_smoke` 钉住，纯逻辑那边不缺证据）；`-Mode formpage` 还没换 PrintWindow；
§80 的中文那一遍与英文单数形；`dxcompiler`/`dxil` 剥不剥；widget 这一类要不要自己画 `/AP`；
要不要加 `/Brepro`；§75 的路 (a)；§55 XFA；§47 那句读者可见的话；§49 那 28 个括号；§74 的单复数；
§79 的锁本身与就地保存。
**已经还上的**：§16 那"三处相同"从 §74 起欠的九片（§74–§82）对上，装上的那份带着 §64 的书签读路、
§66 的两条修复、§68–§74 的书签四层与层级格、§75–§80 的表单/保存/书签话、§81–§82 的量（后两片没有
用户可见的东西，字节相同是应该的）。

## 84. 包里那 25.9 MB 的死重，与装上的那份第一次把 §80 那句话读出来（顺带抓到 harness 自己那条永不成立的 needle）（2026-10-08）

他一句"可以"批的是我提的两条便宜事：把 `dxcompiler.dll`/`dxil.dll` 从包里剥掉，以及把 §80 那句位置书签
话在**装着的字节**上读一遍。两条都做完了，而两条各自推翻了一句我自己差点写进账的话。

1. **先量"加载不加载"，别先猜**。从解包目录、`PATH` 上只有 Windows 起那颗 exe，读它的模块表两遍：
   空启动 110 个模块、开 184 条书签的书并渲一页 105 个模块 —— **`dxcompiler.dll` 与 `dxil.dll` 都不在**，
   而它们服务的 `d3d11.dll` 在。所以剥得起，理由与 §39 让 `D3Dcompiler_47.dll` 走人同一条：拿位图画页的
   程序不去要着色器编译器。

2. **控制把将记的功劳收回去了**。在 `build-package.ps1` 第 2b 步的清单里加上那两颗之后重打，脚本照旧印
   `threw away 34 file(s), 29.3 MB` —— **一字不差**，说明它俩没被这条清单找到过。于是把两行拿掉再打一遍
   作控制：**同样 67 个文件、同样 17.5 MB**。所以 28.1 → 17.5 MB 不是我这两行做的，是部署那一步下一趟
   自己没带。**为什么变，没查**（构建树里那两颗 14,317,000 / 1,509,800 字节还原样躺着，而包里一颗都没有；
   上一趟包里那颗还是 25,684,592 字节，与构建树那颗不同名同、字节不同）。留着这两行是**一条常设规矩**
   ——§60 说过那个清单每次自己决定，§83 就是它变胖的那一次 —— 记录里不许写成这次的原因。

3. **包这边结案**：`verify-package.ps1` 先印 `lost 2 [-bin/dxcompiler.dll] [-bin/dxil.dll]`（对着 §83 那份
   清单），再跑一次印 `payload is the same 67 file(s)`，两次都 exit 0 —— 也就是**少了这两颗，程序仍从解包
   目录、无 Qt 的 PATH 起得来并认出自己的窗口标题**。msix **17.5 MB / 解包 40.5 MB / 67 文件**（回到 §73 那
   一档）。三处逐字节仍全等：`xyzpdf.exe` `0f744cd07d73…`/1,341,952，三颗壳 DLL `f8e17a004f76` /
   `5bdb230257a7` / `727b03acec88`，`qpdf30.dll` `5ce2fc864988`；装着的 `bin` 里 15 个文件、没有 dx。
   （这次比字节的每一行都带着大小与"在不在"—— §83 那条规矩第一次实操。）

4. **装上的那份为什么三轮点不出框：前台不是它的**。新加的 `-OpenViaPicker` 第一次跑，日志写着菜单行
   `打开(O)…` `enabled=True at 674,303 180x27`、坐标是真的、点击发出去了、`#32770` 三次都不来 ——
   这形状看着像"这条链在包里那份上不通"。加一句 `SwitchToThisWindow`（§66 的 tabstrip 那支早就记过它是
   Alt+Tab 那一叫，不跟壳抢前台所有权）之后当场变了：`foreground before the raise [build-pro — 文件资源
   管理器 [ main ▸ … ]]` → `foreground after the raise [XYZPDF]`，picker 第一次就起来，而且**落在
   `real-book.pdf` 那一行**（壳的 per-exe 对话框记忆，与开发版同名所以共享，§62 那条）。点行 + BM_CLICK
   控件 id 1 → `document open in the packaged instance [real-book.pdf — XYZPDF]`。

5. **整条 §80 的链在装着的字节 + 中文界面上跑完**：`GridPattern rows=184 columns=3`；没动过的框
   `写这 184 条书签` `enabled=False`；加一条 → `rows=185`、`写这 185 条书签` `enabled=True`；小结里那句
   （**这句话在 §99 之后不再成立**，原样留在这里：那时一条都留不住，现在只有"对不上文件里原来
   条目"的那几条会退回整页适配，屏幕上同一份 184 条的书加一条书签已经不会再听到它。）原句：
   `这份文档里有 184 条书签指着页面内的某个位置（不只是第几页）。按确定再保存，它们都会变成整页
   适配 —— 那一层位置这一版留不住，书签的名字、层级和页码照旧。`；层级格写 `0` → accept 灭、小结换成
   那句层级的话；写回 `1` → accept 亮。

6. **而 harness 自己报了 `sentence=False`，这一条才是这一片最值钱的**。needle 写的是
   `页的某个位置`，产品的原话是 `页面内的某个位置` —— **多一个"页"字就不是子串**，所以这一支
   **永远不会红，也永远不会绿**。§80 记的"中英两种 needle 都认"当时只从英文那一支走过去（这个 harness
   起的实例自带 `Language=en`），中文那一支从没被执行过 —— 这就是 §42 那条规矩存在的原因，而这一片
   是它第一次真兑现：**一条写在两支语言里的断言，只跑过一支，等于没写**。把 needle 收成 `的某个位置`
   重跑：`summary names the positions-at-risk sentence=True`。

7. **`.ps1` 的 ASCII 坑又踩两次**，第二次是注释里写了一句屏幕原文（"指着页面内的某个位置"），解析器炸掉、
   退出码 1、一句why都不说。区别只在于这次**改完立刻查字节数**（`sum(1 for b in bytes if b>127)`），
   当场看见 51 个非 ASCII 字节而不是跑一轮再猜。AGENTS.md 因此添了两条：这条查询与 `-OpenViaPicker`
   那条新路由（含"不加 `SwitchToThisWindow` 会白跑三轮"）。

8. **数字**。**产品代码 +0/−0**（C++ 一个字没动，所以没重编、门禁没重跑 —— 最近一遍 26/0 之后源文件没变，
   而 §83 之后装着的 exe 就是 `0f744cd07d73…` 那颗）。`packaging/build-package.ps1` **+14/−1**（清单里两个
   名字加两段注释；ASCII 0、解析 0 错）；`tools/gui-probe.ps1` **+78/−1**（`-OpenViaPicker` 参数、
   `Open-PackagedDocument` 助手、`openpicker` 补上 raise 与前台打印、needle 修正；ASCII 0、解析 0 错）；
   目录 **578 / numerus 47 / 0 unfinished 一字未动**。

**仍然欠的**：**为什么那两颗在这两趟之间自己没了**（没查，而 §83 那趟确实带着它们 —— 上传包之前该有人
再读一次 payload 对照）；开发版那一遍的**中文**仍未单独跑（这一片只在装的那份上读了中文，§80 欠的那条
于是还欠半条：English 单数形那一形也没见过）；`-Mode formpage` 还没换 PrintWindow；其余模式早退不杀进程；
`/Brepro`；§75 的路 (a)；widget 要不要自己画 `/AP`（§82 说这层修不到）；§55 XFA；§47 那句读者可见的话；
§79 的锁本身与就地保存；§49 那 28 个括号；§74 那句 `%1` 的英文单复数；§82 那句"打印/导出/缩略图/预览窗格
同样丢表单值"仍是**同一条 render() 的推论、没测**。
**已经还上的**：包里那 25.9 MB 的死重有了明文的处置（常设剥离 + 一条"每次重读 payload"的规矩），而
§83 记的"装上的那份没读 §80 那句话"读到了 —— 顺带把 harness 里那条从未成立的中文 needle 修成真的。

## 85. 删页框那句第一次被眼睛读到（正、负、以及一条能红的控制）+ §80 的开发版中文那一遍（2026-10-08）

他一句"继续"。挑的是"只欠一次眼睛"里最便宜的两条：§77 留在**删除页面**小结里的那句（正负两条断言
早在 `documenttab_smoke` 钉住，屏幕上一次都没读过），和 §80 那句位置书签话在**开发版**上的中文一遍
（§84 只在装的那份上读了中文，所以那条债还欠半条）。产品代码一字未动。

1. **新眼睛**：`-Mode deletesummary -Doc <文件> -ExpectForm present|absent`。开框走的是 §45 立的规矩
   —— 按**类**认 `DeletePagesDialog`（标题是我们的可翻译串，类名不是），先 `SwitchToThisWindow` 再点
   （§84 那条前台账）。读的是框里最长那一格 `Text`，needle 两种语言各一条：中文 `表单字段`、英文
   `form fields`。

2. **三半，各自的结果**（全都跑在最后一版脚本上）：
   - `field-flat.pdf`（带字段）+ `present` → exit 0，屏幕上就是那句：`This document has form fields.
     An answer belongs to the page its field is drawn on, so what was typed on a page you delete will
     not be in the file that gets saved (the field itself stays listed).`
   - `doc40.pdf`（不带字段）+ `absent` → exit 0，`sentence=False`，小结只剩"没有点名任何页码"+"原来
     那份文件不会改动…"。
   - **控制**：同一份不带字段的文件 + `present` → **exit 1**，`Bail` 那句
     `a document that carries a field opened the box without the form sentence`。
     这一条是这片真正的地基：**一条改不了退出码的读数只是装饰**（§71 立的），而这里"没有那句话"既可能
     是产品在无表单文件上闭嘴（对），也可能是我这一支 needle 根本不成立（§84 刚在书签框上抓到过一次
     一模一样的事）—— 只有让它在指定"该有"时必须红，那句 `False` 才说的是产品。
   - 顺带读到的两格形状：`Delete these pages` 在未点名时 `enabled=False`（§14 那条"按下之前就拒"），
     `Cancel` 亮着；模式全程不按任何按钮，收尾杀进程。

3. **一处 flake 要写清楚，不然下一片会把它当成缺陷**：负向那一遍第一次**没读到框**，报的是
   `[Document(D)] found, but neither Expand nor a click produced rows` → `no delete-pages row in
   either language`，exit 1；重试两轮都过。这条链的老毛病（合成输入是否落地是**按轮**决定的，§41/§74）。
   能把它和产品分开的唯一东西是**消息不同**：菜单没起来说的是"没有那一行"，产品没那句话说的是
   "框里没那句"。所以模式里的每条失败各写各的，不是笼统一句"没读到"。

4. **一个我差点含糊过去的机制问题，用一行读数买断了**：这一块的初版在 `ClickAt` 之后只等 1.5 秒找一次，
   报"框没开"；我修的时候**同时**改了两件事（改成轮询、把范围从主窗口子树扩到进程自己的顶层窗口），
   于是"到底哪个才是原因"我从说不清 —— 而说不出来的话，下一个模式要么白加一层范围、要么把 1.5 秒
   当成够用。加了一句打印"第几轮 / 在哪一层"再跑：**round 1，2 个候选，在主窗口子树里**。
   所以先前那次纯粹是**等少了**，扩大范围是没必要的保险（留着，因为它同时是那句答案的来源）。

5. **§80 的开发版中文那一遍**（`-Mode bookdialog -Doc 真书 -Language zh_CN`）：`GridPattern rows=184`、
   `cells: 33 DataItem, 0 with an empty Name, 22 whose Name is just a number`（§74 那一格在中文下也
   不再对读屏沉默）、未动的框 `写这 184 条书签` 灰着、加一条 → `rows=185` 且亮、小结里那句中文位置话
   在，`summary names the positions-at-risk sentence=**True**` —— §84 修好的那条 needle 现在在**两个**
   实例上都成立（装的那份 + 开发版）；层级格写 `0` → 灰 + `summary names the level column=True`，写回
   `1` → 亮。**这一片的 needle 一次都没红**，因为产品说的是中文、needle 也是中文，而 §80 那三遍里
   每一遍 needle 从没在中文界面上被执行过 —— 这一条现在被这次执行覆盖了。

6. **数字**。**产品代码 +0/−0**，C++ 一字未动所以**没重编、门禁没重跑**；`tools/gui-probe.ps1`
   **+104/−1**（新模式 + `-ExpectForm` 参数 + 那条"第几轮/在哪一层"），纯 ASCII 0 违规、解析 0 错
   —— 但**这片又踩了一次 `§` 的坑**（我习惯性地写 `§77` 而不是 `sec.77`，6 个非 ASCII 字节），
   靠"改完立刻查字节数"当场清掉；AGENTS.md 的规矩加了一例（§84/§85 共三次）。目录一字未动。
   屏幕上跑的是开发版那颗 `0f744cd07d73…`（§83/§84 之后没重链）。

**仍然欠的**：`-Mode formpage` 还没换 PrintWindow；其余模式早退不杀进程；§74 那句 `%1` 计数句的英文
**单数那一形**还是没在屏幕上见过（要一份"恰好 1 条"的文档，而那一形只在目录里）；英文那一族的
`deletesummary` 正向已经读了，**中文那一遍这个框还没读**（同一句中文 needle 已经写在模式里，缺一次
`-Language zh_CN` 的执行）；§75 的路 (a)；widget 要不要自己画 `/AP`；§55 XFA；§47 那句读者可见的话；
§79 的锁本身与就地保存；§49 那 28 个括号；`/Brepro`；§82 那句打印/导出的推论；§83 记的"为什么那两颗
DLL 自己没了"（上传包之前该有人再读一次 payload 对照）。
**已经还上的**：§77 欠的那次眼睛（正、负、控制三半都在屏幕上跑了，控制证明那两条不是装饰），以及
§80 欠的开发版中文那一遍 —— 顺带把 §84 修的 needle 在第二个实例上复核了一次。

## 86. 把那 40 条"早退不杀进程"的老账落地（带一对控制），顺手补上删页框的中文那一遍（2026-10-08）

他一句"继续"。挑的是账上最旧的两条仪器债：§74 立过规矩"**模式早退必须走 `Bail`**（说一句 + kill + exit 1），
裸 `exit` 会跳过文件末尾那句 kill"，可当时只改了那一处的坑，脚本里还留着 **40 条** 裸 `exit 1`；以及
§85 新欠的那半条 —— `deletesummary` 的正负两条都在英文界面上执行过，**中文 needle 写了但一次没跑**
（§84 刚教过：只跑过一支语言的断言等于没写）。产品代码一字未动，没重编、门禁没重跑。

1. **先量前提**（不量就改，改完也不知道值多少）：拿转换前的脚本副本跑 `-Mode mergedonor` 不带 `-LastDir`
   —— 这一条检查发生在应用**已经起来之后**，所以 `exit 1` 跳过了末尾的 kill。结果：`exit=1`，
   `tasklist` 里 **1 个 xyzpdf.exe 还活着**。

2. **改**：40 条里 41 行动了 —— 31 条是 `Say '…'; exit 1` 收成 `Bail '…'`（消息一字不动，所以诊断没降级），
   7 条本来连消息都没有（`if (-not (PickFolder …)) { exit 1 }` 这种），各补一句说清是谁没成；
   3 条是多行块（打印完子树窗口再退、语言恢复失败、侧栏没起来把整屏名字倒出来再退）。
   文件 2000 → 1999 行（多行块合成一行），残留的裸 `exit 1` 只剩 `Bail` 自己函数体里那一处。
   脚本仍是纯 ASCII、解析 0 错。

3. **再量结论**：同一句命令、同一台机器、同一版脚本之外的东西都不变 —— `-Mode mergedonor`（不带 `-LastDir`）
   在新脚本上 `exit=1`，**残留 0 个进程**，而那句话照旧印出来。这一对是这片的地基：它说明改动买到的
   不是"退出码变了"（没变），而是"下一轮不会再被上一轮的实例喂"。

4. **差点读出一条假绿，值得单独记**：第一次跑"旧脚本"那半时，我上一条命令因为一个反引号被 bash 吃掉而
   整段没执行 —— `cp` 没发生，PowerShell 报的是 `-File 参数…不存在`，退出码 **127**，而我那条
   `leftover xyzpdf processes: 0` 量的是一根**根本没跑起来的脚本**。看见 exit 不等于 1 才回头查。
   规矩：**控制跑要先证明它自己跑了**（§67"计数断言不能数自己放的东西"的又一支：读数为 0 之前先看
   产生它的那条命令是否真的执行过）。

5. **删页框的中文那一遍**（`-Mode deletesummary -Language zh_CN`，正负各一次，都 exit 0）：
   `DeletePagesDialog found on round 1 (2 candidates; it was in the main window subtree)`，
   带字段那份屏幕上是小结里夹着这一句 —— `这份文档有表单字段：字段填的答案属于它画在哪一页上，
   删掉的那一页上填的答案不会出现在保存出来的文件里（字段本身还登记着）。` → `sentence=True`；
   不带字段那份只剩"还没有说出要删哪几页"+"原来那份文件不会改动…" → `sentence=False`。
   **这是那条中文 needle 第一次被执行**，也是同一模式在两种语言下都有正负两半（英文 2 遍 + 控制 1 遍，
   中文 2 遍）。两遍里 `删除这些页` 都 `enabled=False`（未点名就灰着，§14），模式全程不按按钮。

6. **数字**。**产品代码 +0/−0**；`tools/gui-probe.ps1` **+40/−41**（对着转换前的备份逐行比过：改动就是
   那 41 行，没有整文件重写、没有行尾被顺手换掉 —— §56 那条"脚本改文件先数删了几行"的规矩）；
   目录一字未动；屏幕上跑的还是 §83 之后那颗 `0f744cd07d73…`（C++ 未动，所以没重链）。

**仍然欠的**：§74 那句 `%1` 计数句的英文**单数那一形**还是没在屏幕上见过（要一份"恰好 1 条"的文档，
而那一形只在目录里）；`-Mode formpage` 还没换 PrintWindow（像素问题已经改从头测，所以这条只剩
"别再留一根会说假话的通道"）；`splitsagain`/`mergedonor`/`saverename` 那几支的**中文**屏幕一遍
（它们与 `deletesummary` 同一族：needle 双语写在脚本里，执行过的一直只有英文那一支）；§84 那条
"两颗 DLL 为什么自己没了"；§75 路 (a)；widget 要不要自己画 `/AP`；§55 XFA；§47 那句读者可见的话；
§79 的锁本身与就地保存；§49 那 28 个括号；`/Brepro`；§82 那句打印/导出的推论没测。
**已经还上的**：§74 那条规矩从"一处坑改一次"变成脚本里的**通则**（40 条早退全部会杀进程，且有一对
前后读数证明），以及 §85 自己新欠的那半条中文一遍。

## 87. 那三支的中文一遍：跑之前先发现 `mergedonor` 在中文界面上根本跑不起来（而且两个框不在同一层）（2026-10-08）

他点的是我上一片末尾那句：`splitsagain`/`mergedonor`/`saverename` 的中文屏幕一遍 —— 与 §85 刚给
`deletesummary` 补的那半条同一族毛病：**needle 双语写着，执行过的一直只有英文那一支**（§84 立的规矩）。
产品代码一字未动。三遍都读到了，而**这一片的真正产出是"跑起来之前先发现有两处跑不起来"**。

1. **`mergedonor` 在中文界面上根本不可能跑。** 它三处查找只写了英文那一支：菜单行走 `MenuRow 'Merge Files In'`、
   框标题走 `Dialog 'Merge Files In'`（两次）、撤销行匹配 `-like 'Undo*'`。中文界面下它必然 Bail 在
   `no Merge Files In row` —— **那句话说的是 harness，不是产品**。现在：`$mergeNeedles` 两支都给、
   `RowAny` / 新加的 `DialogAny`、撤销行加 `撤销` 前缀（另一支模式 §39 早就是这么写的，抄过来）。
   标签本身核对过：`mainwindow.cpp:877` 是 `合并文件进来(&F)…`，含中文 needle，不是拼写问题。

2. **两个框不在同一层，这是量出来的**：加了中文 needle 之后仍然找不到框，而 `FindType (Window) $CT::Window`
   这一次**一个都没有返回**（它自己把候选倒出来：空）。给 `DialogAny` 加上第二层（进程自己的顶层窗口）并
   把匹配到的那一格印出来，读数立刻变成 `matched [合并文件进来] class=MergePagesDialog`。对照 §85：
   `DeletePagesDialog` 是 `found on round 1 … in the main window subtree` —— **删页框在主窗口子树里，
   合并框不在**。所以"我们的框按类认"那条规矩（§45）少了一半：**还得按两层找**。
   `DialogAny` 现在两层都找，找不到就把两层的候选名字/类名全印出来 —— "没找到"从死路变成读数。

3. **一次按轮 flake**（第三次遇到同一族）：菜单第一次 `Expand` 出行、第二次不出行，加了 §45 那条三次重试后
   第二轮自己回来（日志留着 `try 1: no merge row yet`，看得见它发生过）。

4. **三遍读到的东西**（都是中文界面、开发版那颗 `0f744cd07d73…`）：
   - `splitsagain`：两轮各"记一个再改一个"。第一屏 accept `拆成 40 份`，重开问 7 →
     `会写成 6 份，每份最多 7 页：1-7、…、36-40。…这会把前面记下的「每 1 页一份」换掉；撤销一次回到那一个。`；
     再问 3 → `会写成 14 份…「每 7 页一份」换掉…`。**§40 那条 aside 的两个 numerus 分支在中文里都在**，
     末尾撤销行 `撤销 拆成每 3 页一份(U)`。
   - `mergedonor`：两个供体各一遍。`1 个文件一共取 1 页，合到最后面，按列表的顺序排在第 41 页。`
     与 `…一共取 2 页…连着排在第 42-43 页。 / 这份文档里已经有 1 页是合并进来的了。`；
     撤销行 `撤销 合入 1 页（donor1.pdf）(U)` / `撤销 合入 2 页（donor2.pdf）(U)`；
     收尾状态行读回**空** —— 这一条只记不判（§29：空的读数证明不了任何东西）。
   - `saverename`：整条链跑通，`wrote renamed-112325.pdf`、`target exists: True size 12666`、
     `source hash unchanged: True`、两个标签 `doc40.pdf *` 与 `renamed-112325.pdf`、
     状态行 `已写成一个新文件（40 页）：向右转 90°（1 页）`。**跑出来的那份我删掉了**，
     `export/` 只剩 §37 那张旧 PNG。

5. **`.ps1` 的 ASCII 坑在这一片里又咬了两次**（第一次是我在注释里直接写了菜单标签的中文字，21 字节）——
   两次都是"改完立刻查字节数"抓的，不是跑挂了才发现。这一条在 AGENTS.md 里已经是规矩，这里是它第 7、8 次生效。

6. **数字**。**产品代码 +0/−0**；`tools/gui-probe.ps1` 从 §86 前的备份算起 **2000 → 2043 行（+90/−47）**，
   其中 §86 是 +40/−41、**§87 ≈ +50/−6**（`DialogAny` 新助手、`$mergeNeedles` 与三支查找、撤销前缀、
   三次重试、注释）；纯 ASCII 0 违规、解析 0 错。没重编、门禁没重跑（C++ 一字未动）。

**仍然欠的**：`-Mode formpage` 还没换 PrintWindow（现在像素问题都从头测，这条只剩"别再留一根会说假话的通道"）；
`exportstatus`/`uacache`/`outlinetree`/`tabstrip`/`menudump`/`inventory` 这几支**从没指定过 `-Language zh_CN`
跑过**（`inventory` 在装的那份上读到的是中文，其余六支只在英文下走过 —— 同一族病，逐支补）；§74 那句 `%1`
的英文**单数那一形**（要一份"恰好 1 条"的文档）；`mergedonor` 末尾那个空状态行**为什么是空**没查
（三种解释：合并不写状态、状态条被框盖住没采到、UIA 那一格那一刻不供值 —— 这一片只记"读到空"）；
§84 那条"两颗 DLL 为什么自己没了"；§75 路 (a)；widget 自己画 `/AP`；§55 XFA；§47 那句读者可见的话；
§79 的锁本身与就地保存；§49 那 28 个括号；`/Brepro`；§82 那句打印/导出的推论。
**已经还上的**：他点的三支的中文一遍全读到了，而其中一支是**先修才能读**的 —— §84 那条"双语 needle 只跑过
一支等于没写"从书签框、删页框扩到 `mergedonor`；顺带新学一条：**我们的框两层都有，别按一层找**。

## 88. 六支的中文一遍，被它逮出两根假绿：一根是英文单支的查找，一根是"点了就算成功"的壳框（2026-10-08）

他一句"继续"。接着 §87 那族病把剩下的六支跑完（`bookdialog` 的英文一句 + `exportstatus`/`outlinetree`/
`tabstrip`/`menudump`/`inventory`/`uacache` 的中文一遍），**跑出来两根假绿**，这一片的账都在那两根上。
产品代码一字未动。

1. **§74 钉的那条文案缺陷第一次在屏幕上读到了原句**：`-Mode bookdialog -Language en -Doc 真书`，加一条
   之后的小结是 `Pressing leaves this document with 185 bookmarks. / **1 of them point at no page.** /
   184 bookmarks in this document point at a pl…`。`bookmarkdialog.cpp:392` 用的是 `%1`（目录里同一支英文
   `%1 of them point at no page.`），没有 numerus —— **1 后面跟复数动词**。修法（换成 `%n` 两形）在 §33
   那一族里是熟路，**但仍等他定**，这一片只把读数拿到手。

2. **`exportstatus` 三处查找是英文单支**（菜单行 `MenuRow 'Export as Images'`、框标题 `Dialog 'Export as
   Images'` 三次）—— 与 §87 的 `mergedonor` 一模一样的病，中文界面下它根本进不去。现在：`$exportNeedles`
   两支都给（`导出为图片`：`mainwindow.cpp:562` / `imageexportdialog.cpp:25`）、`DialogAny`、接受按钮走
   `MatchOne @('Export', 导出)`。

3. **第一根假绿：点了就算成了。** 中文那一遍连跑两次都是 `files=0` 采 14 次然后 **DONE、exit 0**。给它加
   "按下接受之后进程有哪些窗口"这一步，答案当场露出来：`[Choose a folder to save into] class=#32770`
   **还在**，而它自己写着 `H:/tmp/num34/export / 文件夹无效` —— **壳的文件夹框拒了我给的路径**（我用的是
   斜杠形式），而 `PickFolder` 只管把 `BM_CLICK` 投递出去就 `return $true`。所以"按下导出"打在一个还被
   模态框挡住的身上，什么都没发生，而模式一路绿。三处都改了：
   - `PickFolder` 现在**轮询那个框是否消失**，没消失就把它自己说的话印出来并返回 `$false`；
   - 模式在按下接受后**把进程所有窗口的类名/标题/最长文本倒出来一次**（"没导出"与"弹了个拒话"从此分得开）；
   - 收尾两条硬断言：`-OutDir` 里一张 PNG 都没出现 → Bail；状态条那张判定用的裁图根本没生成 → Bail。
   **控制**：斜杠路径 + 新脚本 → exit 1 且 `the folder picker is STILL open after the click`；反斜杠路径 →
   `files=1`、状态行读到 `已导出 1 张图片到 H:/tmp/num34/export`（ink 1901，5.6 秒后落回 1588 空条 ——
   §37/§56 那条 6000ms 超时第三次对得上）。**注意这不是"斜杠一律不行"**：同一个斜杠形式喂给 `-LastDir`
   （我们自己那个 Qt 目录框）在 §87 里一直好用 —— 拒它的是壳那个 `#32770`。

4. **第二根假绿是旧的**：这支模式从 §37 到 §61 一直"跑通过"，靠的正是上面那个 `return $true` —— 它报的
   DONE 从来没被"到底有没有文件出来"对过账。**改之前它不会红**，所以 §37 那两条英文读数的可信度要靠
   它们各自的 `name=[…]` 行（那两行确实有内容），而不是靠 exit 码。

5. **其余四支中文一遍都过**（一次通过，无 flake）：
   - `outlinetree`：`GridPattern rows=103 columns=1`、展开 5 个根行后 `descendants=26 digit-starting=21`
     —— **§64/§65 那条读路修复在中文界面下读数一样**；
   - `tabstrip`：`foreground before the click [Claude]` → 抬起之后 `[doc40.pdf — XYZPDF]`，
     `PrintWindow returned True`（§84 那条前台账在这支上也露出了名字：**盖在前面的确实是别人**）；
   - `inventory`：31 行中文名字（`Tool`/`CheckBox`/`Tab` 全在），无一处需要 needle；
   - `menudump -Menu 6587,6863`：中文 `文档(D)` 菜单 `19 项`（之前 8 项）；
   - `uacache`：44 个采样、三路（held/fresh/root）逐行一致、21.9 秒一起变空 —— §56 的结论在中文下重演。

6. **数字**。**产品代码 +0/−0**；`tools/gui-probe.ps1` **+47 行**（2043→2090；needle 双语、`DialogAny` 用上、
   按下后的窗口倒出、两条收尾 Bail、`PickFolder` 的消失轮询），ASCII 0、解析 0 错；没重编、门禁没重跑。
   清场：跑出来的 PNG 删了，顺手把 `export/` 里 §37 留下的那张旧 PNG 也删了（图本来就在 `H:\tmp`、不在
   版本库里，读数都在记录文本里）；收尾无残留进程。**`.ps1` 的 ASCII 又差点中招一次**（注释里写了中文标签），
   改完就查，当场清掉 —— 第 9 次。

**仍然欠的**：**`%1` 那支要不要改成 numerus**（等他一词，改法是 §33 那一族）；**壳的文件夹框为什么拒斜杠**
没查（只知道它拒、而我们自己的框不拒）；`mergedonor` 末尾那个空状态行为什么空；`-Mode formpage` 换
PrintWindow；`deletesummary` 的中文一遍已经跑过（§86），`langswitch`/`formpage`/`openpicker`/
`splitsagain` 等在**装的那份**上的中文/英文组合还有没配齐的（`-OpenViaPicker` 这条路 §84 才立）；
§55 XFA；§47 那句读者可见的话；§75 路 (a)；widget 自己画 `/AP`；§79 的锁与就地保存；§49 那 28 个括号；
`/Brepro`；§84 那两颗 DLL；§82 那句推论。
**已经还上的**：§87 末尾点名的六支中文一遍全跑完，而这一片的收获不是那六份读数，是**两根假绿被关掉** ——
一根在查找里（英文单支），一根在"点了就当成了"（`PickFolder` 的无条件 true）。

## 89. `-Mode formpage` 换成窗口自己画：先把裁条的**原点**量错了一次，纠正之后 §82 那句"widget 0 像素"第一次在屏幕上带着伴行证据（2026-10-11）

接着"按推荐顺序执行"里的第 10 项：§88 末尾还挂着的 `-Mode formpage 还没换 PrintWindow`。这一片
产品代码 +0/−0，动的全是仪器（`tools/gui-probe.ps1` 一段重写 + 样本生成器多一份），而收获是那处
**偏移**：换路由的第一遍读数里，两份非 plain 文件的差异带**都从裁条第 0 行开始**（bbox
`23,0 156x23` 与 `523,0 537x330`）。按 §82 的规矩"解释一次差别之前先把差的字节/差的行找出来"，
先去定位那些行是什么，而不是先讲故事。

1. **三条各自独立的读数把病因钉死**：
   - 每一份裁条的**最左 8 列（x 0..7）是纯黑 (0,0,0)** —— 页面不会从黑窗框开始；
   - 把差值按行分带：`field-appearance` 只有 `rows 0-29, x 23..179` 一条；`markup-square` 除了同样
     那条 `rows 0-29`（x 23..170），另有 `rows 208-330` 一条 —— **那条 0-29 与文件内容无关、与文件名
     有关**，它就是标签条（§54 当年那 681 个假像素的病，换了个来源又回来了）；
   - 一起量两个来源：`GetWindowRect` = **1256x899 在 652,235**，`ClientToScreen` 给出的客户区原点
     = **660,266**，而 UIA 的顶层 `BoundingRectangle` 也说 **660,266（deltas 0,0）**，scale 1。
     ⇒ frame = **8,31**：**PrintWindow 那张图的原点是外框左上角，而旧算法按客户区减**
     （`$r.X - $wr.X`），于是整张裁条往左偏一个边框宽、往上偏一个标题栏高；而图本身开的是
     **客户区尺寸**（1240x860），比 PrintWindow 要画的 1256x899 小 16x39 —— 底部 31 行同时被切掉。
     §74 那条规矩写的是"开图按外框尺寸"，这一片把它补成**"裁图的原点也按外框"**，同一个矩形两处要用。

2. **修完之后那五轮**（同一几何，裁条 `8,151 1240x720`，标签条底边在窗口行 **150**，裁条从 **151** 起）：
   - `plain.pdf` 与 `plain-renamed.pdf`（**同一份字节的两个名字**）**SHA-256 逐字节相同**：
     `473abc153a2960de5c847e6a130bc1f908886a77dd00b28934e544156d8e8034` —— 文件名不进图了；而**这个值
     就是 §54/§57/§58 那三次用 `CopyFromScreen` 记下的 `473abc153a29…`**。所以新的屏幕路由与旧路由
     落在同一处几何上，那三次的读数没被这次改动推翻（旧路由的屏幕污染问题是 §81 记的，与几何无关）。
   - 页面在这张图里的位置**是从两个标记自己的 /Rect 反量出来的**，不再是从 fit-width 推的：
     页面原点 ≈ 裁条 **(11,11)**，**2.026 px/pt**（两条独立 x 读数各给 2.0261 与 2.0263）。
     `field-appearance` 的框（60,600–260,630 pt）因此落在 **x 133..538、rows 441..502**，在 720 行的
     看得见那块里 —— §54 当年那组算出来的 x 125–542、y 442–504 与量的差 2~8 px，那句"不是没滚到"照旧成立。
   - **§82 那条结论第一次在屏幕上读出来，而且是带着伴行证据的**（§82 的规矩：一行"读 0"必须有同一
     张图里"确实画得出来"的行列陪着）：
     | 文件 | 对 plain 的差值 | 位置 |
     |---|---|---|
     | `plain-renamed.pdf` | **0 个像素**（同哈希） | — |
     | `field-appearance.pdf`（`/Widget` 自带 `/AP /N`） | **0 个像素**（同哈希） | 落点在框内 |
     | `painted-rect.pdf`（同一片矩形画进**页面内容流**） | **20970**，rows 113-157 x 153..618 | 自己 /Rect 的 ±2 px |
     | `markup-square.pdf`（注释层 `/Square`，**不带 /AP**） | **3012**，rows 177-299 x 153..537 | 自己 /Rect 的 ±2 px |
     12 条带的账同样分得开：plain/field-appearance 逐条相同（`4740,480,1602,509,480×7,484`），
     markup-square 在带 2/3/4 各 +183/+60/+250，painted-rect 在带 1/2 各 +699/+3901；
     **带 0 五份都是 4740** —— 名字那一行不再进图。
   - 新的那一行 `painted-rect.pdf` 进的是样本生成器（`XYZPDF_FORM_SAMPLE_DIR` 现在多写一份，
     `check(` 884→**885**），因为"必须有差异"这一行不该靠手造文件，否则下一片又得重新发明它。

3. **两条 guard，各自能红**（屏幕上的一条判定必须能红；§85 那条）：裁条左列不得有一串近黑像素；
   裁条顶边必须在所有 `TabItem` 的底边之下。**控制**：把脚本另拷两份、各只退回一半的旧算法 ——
   - 只把 left 退回 `$r.X - $wr.X` → **exit 1**，`the crop has 240 near-black pixels down its left edge:
     that is the window frame, so these bands are not the page`；
   - 只把 top 退回 `$r.Y - $wr.Y` → **exit 1**，`the crop starts at window row 120, above the bottom of
     the tab strip (150) -- a difference between two documents there is their file names, not the page`；
   两份拷贝各自先印 `CONTROL A/B:` 那一行才动手。**为什么要这一句**：我第一次写控制时 python 往
   `/h/tmp/…` 那种 MSYS 路径写文件失败，于是 PowerShell 报 exit **127** —— 与"控制红了"**长得一模一样**，
   §86 那条"控制跑要先证明它自己跑了"这次是它自己的孪生版本（不是反引号，是路径写法）。
   还有一条单位 guard：`ClientToScreen` 与 UIA 顶层矩形差超过 2 px 就 Bail。这台机器每一轮都读
   scale 1，所以"UIA 是物理像素"这一句在这一片是**对上的**、不是**在不同 DPI 下量过的** ——
   将来 125% 那一轮会说话的是这条 guard，不是我这段文字。

4. **顺手把一句话钉准**：门禁重跑之后 `build-local-release/xyzpdf.exe` 仍是 §82/§83 那颗
   `0f744cd07d73…` / **1,341,952** —— 产品代码一字没动，**增量构建根本没重链它**。§82 那条"这颗哈希记的
   是哪一次链接事件"因此补上反向：**哈希相同也不说明重链过**（它只说明这次连链接都没发生）。

5. **数字**：产品代码 +0/−0；`tools/gui-probe.ps1` **+96/−11**（`GetClientRect`/`ClientToScreen`/`ClientOrigin`
   进 W32，formpage 那一段的图尺寸、原点、guard 全换），ASCII **0**、解析 **0** 错；
   `tests/documenteditor_smoke.cpp` **+15/−0**（样本生成器多一份 `painted-rect.pdf`）；
   门禁 **26 passed / 0 failed**，目录 578 条一字未动（新那行是 std::cout，不是 tr()）。
   **`.ps1` 那一族坑第 10 次**，这次不是中文字：我在两条 Bail 里写了 `UIA\'s` —— **PowerShell 单引号串里
   反斜杠不是转义**，字符串在 `\` 前就断了，解析器当场吐 14 条错误而退出码只给 1（改法：`''` 或直接换个说法）。

**仍然欠的**：**`tabstrip` 那支的裁条用的是同一套客户区减法**（`$r.X - $wr.X`），这一片没修它、
也没重跑它 —— 它读的形状结论**我不替它宣布没事**，那条 column-ink 数的行区间不是它以为的那 36 行，
这是这一片唯一一处**知道有同一处偏移却没量后果**的账。其余照旧：`%1` 要不要换 numerus（§88 读到原句）、
装的那份上 `deletesummary`/`splitsagain`/`mergedonor`/`saverename` 那四支（推荐顺序里的第 9 项，还没跑）、
§55 XFA、§47 那句读者可见的话、§75 路 (a)、widget 自己画 `/AP`、§79 的锁与就地保存、§49 那 28 个括号、
`/Brepro`、§84 那两颗 DLL 为什么不再进来、§82 那句"打印/导出/缩略图/预览窗格同样丢表单值"仍是推论。
**没重打包**：§16 那"三处相同"自 §83 起欠 §84–§89 六片（装上的那份仍是 §83 的字节）。
## 90. 装的那份上补齐那四支；半路推翻了一条 2026-09-28 记下的"输入路径不生效"（它写的是地址栏，不是文件名框）（2026-10-11）

他一句"继续处理那四个未提交的分支" —— 推荐顺序里的第 9 项：`deletesummary` / `splitsagain` /
`mergedonor` / `saverename` 在**装着的字节**上从没跑过（§84 只给 `bookdialog` 开过这条路）。四支最后都读到了，
而这一片真正值钱的是半路上那处仪器事实。产品代码 +0/−0。

1. **§84 那条 `-OpenViaPicker` 只认"点行"，这一轮的框根本不在样本那儿**：装的那份的 picker 落在
   `C:\Users\allgoodhi\Desktop\11`，行是 `bootgen`、`H743_WiFi_App`、`SCH_STM32H743VIT6 IOT_mini.pdf`…
   于是 `no row naming [field-flat.pdf]` + `no document reached the packaged instance`（exit 1，说的是 harness）。
   **不去猜它为什么落在那儿**，而是让框自己把控件倒出来（新函数 `PickerChildren`，失败时打印每个子窗口的
   id / 类名 / 文本）。倒出来才看见：
   - `id=1001 class=ToolbarWindow32 text=[地址: C:\Users\allgoodhi\Desktop\11]` —— **1001 在这里是地址栏**；
   - `id=1090 Static text=[文件名(&N):]` 紧跟着 `id=1148` 的 ComboBoxEx32 / ComboBox / Edit 三条 ——
     **文件名框是 1148**。
2. **因此 §44 那句"往 1001 写有效"只管保存框，而 `PickFile` 里挂着一条 2026-09-28 的负结论"输入完整路径
   不生效"——它正是写给 1001 的**，也就是写给了错的控件。往 1148 `WM_SETTEXT` 完整路径 + 老规矩的
   `BM_CLICK` id 1，装的那份的标题当场变成 `field-flat.pdf — XYZPDF`。**这条负结论从没做过正对照**，
   是 §82 那条规矩（一行"读 0"必须有同一行"文件确实带着它"陪着）在本项目的又一次命中，只不过这次
   红的是"能不能写成"而不是"能不能画出来"。新函数 `TypePathIntoPicker` 与 `Open-PackagedDocument` /
   `PickFile` 共用这一条，两边都在**找不到行**时才走；给的若是裸文件名就不走（明说一句，不含糊）。
   成不成**只由我们自己的窗口标题判**（§88：投递一个点击不等于框被接受）。
3. **一次成功的 open 会把应用自己的 `lastOpenDirectory` 换成那个目录**：从第二轮起 picker 就落在
   `H:\tmp\forms`，`saverename` 那一轮完全靠裸文件名的行点击走通（它与 §62 那条"我们显式给的目录赢"不冲突
   —— 显式目录来自设置，而装的那份的设置是被它自己每次 open 改写的）。
4. **装的那份的界面是中文**（`文件名(&N):` / `打开(&O)` / `命名空间树状控制项`）—— §83 的重装带走了设置容器，
   §49 那次英文切换随之没了。所以这四支在"装的那份"上补的是**中文那一半**；英文那一半在下面"仍欠"里。
5. **四份读数（都在 §83 那颗 `0f744cd07d73…` / 1,341,952 上）**：
   - `deletesummary -ExpectForm present`（field-flat.pdf）读到 §77 那句：**这份文档有表单字段：字段填的答案
     属于它画在哪一页上，删掉的那一页上填的答案不会出现在保存出来的文件里（字段本身还登记着）。**
     `-ExpectForm absent`（plain.pdf）同一框只有"还没有说出要删哪几页 / 原来那份文件不会改动…"两段，
     `form sentence on screen=False expected=absent` —— **正负两条各自能红**（§85 那条规矩在装的那份上成立）。
   - `splitsagain` 三轮按钮标签 `拆成 40 份` / `拆成 6 份` / （第三次问 3 页）与两轮小结，§77 那句拆分值话在：
     **拆出来的每一份只带着它自己那些页上字段的答案，别的页上填的内容不会跟着过去（字段本身在每份里都还
     登记着）**；撤销行 `撤销 拆成每 3 页一份(U)`。
   - `mergedonor` 两支都读到：`1 个文件一共取 1 页…排在第 41 页` 与 `1 个文件一共取 2 页…连着排在第 42-43 页`
     + `这份文档里已经有 1 页是合并进来的了`，撤销行 `撤销 合入 1 页（donor1.pdf）(U)` /
     `撤销 合入 2 页（donor2.pdf）(U)`。**`MergePagesDialog` 这一轮也是靠 `DialogAny` 的第二层找到的**
     —— §87 那处"两个框不在同一层"的事实**在装的那份上同样成立**。
   - `saverename` 写出 `H:\tmp\inst-out\renamed-114835.pdf`、`source hash unchanged: True`、
     状态行 `已写成一个新文件（40 页）：向右转 90°（1 页）`、标签换成新名。
6. **一处先怀疑后量掉的**：拆分数话说 40 页而刚开的是 field-flat.pdf，我先想过"会话恢复留着上一个标签"。
   量了字节就放下：`tests/fixtures/many-pages.pdf`、`H:/tmp/num34/doc40.pdf`、`H:/tmp/forms/plain.pdf`
   **是同一份字节**（12,539 / `6eaed30273f5`），field-flat 就是在它上面加一个字段 —— 40 页说的是刚开的那份。
   **规矩还是 §56 那句：跨轮比数字之前先确认这一轮说的是哪份文件**，而这次是同一份文件的另一个名字。
7. **`status: []` 在装的那份上也是空的**（mergedonor 末尾那句）：§88 记的是 dev 上的一处怪，现在两份构建
   一样 —— 这条从"我们的开发树怎么了"改成"这一族本来就这样"，**为什么空仍然没查**。
8. **数字**：产品代码 +0/−0；`tools/gui-probe.ps1` 工作树里两片合计 **+174/−30**（§89 自己那遍是 96/11；
   §90 再加 78/19：`PickerChildren`、
   `TypePathIntoPicker`、`PickFile` 的 typed-path 分支、`Open-PackagedDocument` 的 basename 匹配与失败时
   的控件倒出），ASCII **0**、解析 **0** 错；改的都是脚本，没重编、门禁没重跑。清场：无残留 xyzpdf 进程；
   `H:\tmp\forms` 多了 donor1 / donor2 / doc40 三份拷贝（为了让行点击可用），`H:\tmp\inst2\typed-only.pdf`
   一份（为了让"这一份在 picker 落点里没有"成立），`H:\tmp\inst-out` 三份产出的 PDF
   （`renamed-114835` / `-115245` / `-115347`）—— 都在 `H:\tmp`，不在版本库。

9. **两个新控件把 §88 那条"斜杠"账逼成了三条对照，而且顺手量出一处产品行为**：为了确认 `PickFile` 里那条
   typed-path 分支不是空写的（**没有调用方会传路径给它** —— 这条本身就是一个未走过的分支，不能算证据），
   把 `saverename` 装那份的 open 改成传**完整路径**，并把 `-Doc` 指到一个 picker 落点里没有的名字
   （`H:\tmp\inst2\typed-only.pdf`）。跑出来三条：
   - 斜杠形式 + 未归一 → **exit 1**：`no row, so trying the typed-path route` 走到了、`WM_SETTEXT` 三条都写了、
     而 `document window [XYZPDF]` —— 框没交出文档；
   - 反斜杠形式 → **exit 0**：`document window [typed-only.pdf — XYZPDF]`，一路写出 `renamed-115245.pdf`、
     `source hash unchanged: True`、状态行照旧；
   - 在 `TypePathIntoPicker` 里把 `/` 归一成 `\` 之后，斜杠形式也 **exit 0**（`slashes normalised for the
     shell: [H:\tmp\inst2\typed-only.pdf]`），最后一遍 `deletesummary` 的复核轮同样走这条路通过。
   **所以 §88 那句"壳的文件夹框拒斜杠"不是那一个框的怪，而是壳的输入框一族** —— 而我们自己的 Qt 目录框
   （`-LastDir`）从 §87 起一直收斜杠。**为什么拒仍然没查**，现在多一条同族的读数而不是一个答案。
   顺带量出的产品行为：那一轮 picker 落点是 `H:\tmp\inst-out`，**里面只有上一轮"保存修改为"产出的
   `renamed-114835.pdf`** —— 说明应用那个"最近目录"是**打开与保存共用的一格**，一次另存也会把它带走
   （§62 问的是"显式目录 vs 壳的记忆谁赢"，这一条是第三个来源，之前没人说过）。
10. **`.ps1` 的 ASCII 坑第 11 次，而且这次是注释里写了屏幕原文**（第 9 次是同一处毛病）：新注释里敲了
    `文件夹无效` 和一个 `§`，`nonascii=17` 当场被抓（解析器当时还是 0 错 —— **能过解析不等于没中**，
    GBK 控制台下才会炸）。改写成 ASCII 之后重跑一轮证明 route 照旧通。

**仍然欠的**：**这四支在装的那份上的英文那一遍**（要 `-Mode langswitch -Installed -To English -Keep`，
跑完再翻回来）；`mergedonor` 末尾空状态行为什么空；`tabstrip` 那处知道有偏移却没量的裁条（§89）；
壳的文件夹框为什么拒斜杠（§88）；`%1` 要不要换 numerus（§88 读到原句）；§55 XFA；§47 那句读者可见的话；
§75 路 (a)；widget 自己画 `/AP`；§79 的锁与就地保存；§49 那 28 个括号；`/Brepro`；§84 那两颗 DLL；
§82 那句推论。**没重打包**：§16 那"三处相同"自 §83 起欠 §84–§90 七片，装上的这份仍是 §83 的字节
（这一片读的正是"装着的那份"，所以这个"欠"是**故意留着**的——它现在是读数的一部分，不只是债）。
## 91. `tabstrip` 那处"知道有偏移却没量后果"的账：改之前那张图里根本没有标签条，而我把 guard 从颜色改成几何才让它站得住（2026-10-11）

接着 §89 末尾自己点名的那条债：**`tabstrip` 的裁条用的是同一套客户区减法，那片没修也没重跑**，所以它的
column-ink 数的行区间不是它以为的那 36 行。这一片就是去量那个后果，产品代码 +0/−0。

1. **先跑"改之前"那一遍，把图留住再动手**（否则"修好了"没有对照）：`PrintWindow returned True,
   window 1240x860`（**客户区尺寸**）、`crop 124x41 at window-local 0,81`、九条 column-ink（1..7）。
2. **修**：图按 `GetWindowRect` 开（1256x899，并把 `frame 8,31` 印在行里），裁条原点也按外框；
   断言写成**"标签自己的矩形必须落在裁条之内"**。
3. **改之后**：`tab maps to bitmap 8,115 110x35; crop 8,112 124x41`，exit 0；column-ink 只剩五条
   （`0,4,8,12,16 → 2,2,8,6,5`，峰值在裁条 x≈97..105，就是标签右端那颗按钮）；两张裁条哈希不同
   （`07a3a31e…` → `2a8b874f…`）。
4. **两张图自己把话说完**（这一族的规矩：布局这种事只有图能看出来）：
   - **before 那张是工具栏**——画面里是 `View Only` 那颗按钮，左边一条黑窗框，标签条只在最底下露出一点
     圆角边。**所以 §74/§88 那两遍 `tabstrip` 的 column-ink 数的是工具栏，不是标签条**；那九条"右端有笔画"
     的读数说的不是关闭按钮。
   - after 那张是标签本体：圆角上边、`doc40.pdf`、右端一个 **X**。**§66 那颗关闭按钮的修复因此第一次
     被一张确实含著标签的图读到。**
   - 同一句里要分清两件事：§66 当年"画成下拉箭头"的结论出自 `gui-verify/` 里的一份草稿图，那目录在
     gitignore 的构建树里、早就不在了，**这一片无法回头量它**；能说的只有"现在这一份画的是 X"，
     而读它的裁条有断言保证含著标签。
5. **这一片真正改了一条仪器规矩**：我第一版 guard 写的是"裁条左列一串近黑＝窗框"（§89 就是这么抓到病的），
   它**当场把深色侧栏判成窗框**而 exit 1 —— 那是一句关于**用户主题**的话，不是关于窗口。**颜色断言在主题
   面前会说谎；窗框是几何定义的。** 于是两处 guard 都改成几何：`formpage` 用"裁条不得越过客户区原点"
   （`$left/$top` 与 `ClientToScreen - GetWindowRect` 比），`tabstrip` 用"标签矩形必须在裁条之内"。
   §89 那两条控制在新写法下**各自照旧红**，而且消息各自指名差在哪一轴：
   - CONTROL C（只把 tabstrip 的原点退回客户区算法）→ exit 1，`the tab rect maps to bitmap 8,115 110x35
     but the crop is 8,81 124x41 -- the tab is not inside this picture`；
   - CONTROL D（只把 formpage 的左沿退回旧算法）→ exit 1，`the crop starts at bitmap 0,151 but the client
     area only begins at 8,31`。
   两份拷贝各自先印 `CONTROL C/D` 那一行（§86 那条），跑完删掉。
6. **顺带一处冲突要写下来**：`tabstrip` 的裁条本来就带 4/3 px 的余量（§74 的理由：关闭按钮在标签右端，
   只裁文字会把它切掉），而**标签自己就贴著客户区左沿** —— 余量往左就必然踩到窗框。现在余量被夹回客户区
   之内（`if ($left -lt $cl) { $left = $cl }`），夹完仍要求标签完整在内；也就是说"有 slack"与"不踩窗框"
   不是并存的两条，是一条被另一条约束住的。
7. **数字**：产品代码 +0/−0；`tools/gui-probe.ps1` 这一片 **+41/−8**（工作树里 §89–§91 三片合计 215/38，
   文件 2268 行），ASCII **0**、解析 **0** 错（这一片没再中）；`FrameInkCount` 那根颜色探针**删了**，
   不留没用的函数；没重编、门禁没重跑（改的都是脚本）；无残留 xyzpdf 进程；对照用的两份拷贝已删，
   before/after 两张裁条留在 `H:\tmp\sec91-crop-{before,after}.png`。

**仍然欠的**：**这四支在装的那份上的英文那一遍**（§90 只补了中文）；`mergedonor` 末尾那句 `status: []`
为什么空；§66 那颗下拉箭头无法回头量（草稿图已不在，只有"现在画的是 X"这一条新的）；壳框为什么拒斜杠
（§90 把它收成"壳的输入框一族"）；`%1` 要不要换 numerus；§55 XFA；§47 那句读者可见的话；§75 路 (a)；
widget 自己画 `/AP`；§79 的锁与就地保存；§49 那 28 个括号；`/Brepro`；§84 那两颗 DLL；§82 那句推论。
**没重打包**：§16 那"三处相同"自 §83 起欠 §84–§91 八片。
## 92. 那四支的英文那一遍跑完了；它逮住两处"印不出来却 exit 0"，其中一处把用户的设置留在了改过的状态（2026-10-11）

他一句"继续" —— 补 §90 剩下的那一半：**这四支在装的那份上的英文读数**。做法是先 `-Mode langswitch
-Installed -To English -Keep`（装的那份翻成英文并留着），跑完再翻回"跟随系统"。产品代码 +0/−0。
四支都读到了，而这一片的账在两处 harness 缺陷上 —— 都是 §88 那一族：**没读出来，却照样绿**。

1. **四份英文读数（仍是 §83 那颗 `0f744cd07d73…` / 1,341,952）**：
   - `deletesummary` 正：`This document has form fields. An answer belongs to the page its field is drawn on,
     so what was typed on a page you delete will not be in the file that gets saved (the field itself stays
     listed).`；负（plain.pdf）：同一框只有 `No pages have been named yet.` 与 `The original file is not
     touched…` 两段，`form sentence on screen=False expected=absent`。**§77 那句的正负两支在装的那份上
     中英两边都读到了。**
   - `splitsagain`：按钮 `Split into 40 files` / `Split into 6 files`，撤销行 `Undo Split into pieces of
     3 pages(U)`，小结里 §77 那句拆分值话的英文形在（`Each piece carries only the answers of the fields
     drawn on its own pages; …`）。
   - `mergedonor`：`Merges 1 page from 1 file to the end: … it ends up at page 41.` 与
     `Merges 2 pages from 1 file … they end up together at pages 42-43.` +
     `This document already has 1 page merged in from another file.`，两条撤销行
     `Undo Merge in 1 page (donor1.pdf)(U)` / `Undo Merge in 2 pages (donor2.pdf)(U)`。
     **末尾那句 `status: []` 在英文下也空** —— 同一现象第三次复现（dev 中文、装中文、装英文），
     "为什么空"仍然没查。
   - `saverename`：写出 `renamed-125520.pdf`、`source hash unchanged: True`、状态行
     `Wrote a new file (40 pages): Turn right 90° (1 page)`、标签换成新名。
2. **第一处：`mergedonor` 的撤销行读取既没期望也没重试。** 英文第一遍里第二支 donor 那一行**什么都没印**，
   而模式照样 DONE、exit 0 —— 与 §88 那根"点了就当成了"同一形状。现在：最多三次重试（菜单每轮都可能不起，
   §41/§87），并且**调用方要求一行时没有一行就 Bail**。**控制**：把匹配串换成不可能匹配的东西 →
   exit 1 且 `no Undo row after three tries -- a merge that left nothing to undo, or a menu this harness
   failed to open; either way a blank is not a verdict`（拷贝先自印 `CONTROL E` 那一行）。同一支重跑：
   两条撤销行都读到 —— **上一遍就是菜单没起来的 flake，而没有断言时它长得跟成功一模一样。**
3. **第二处更要紧：`langswitch -To` 只认一种拼法，于是把用户的设置留在了改过的状态。** 翻回中文时给的是
   `-To 8DDF,968F,7CFB,7EDF`（"跟随系统"的代码点形式），而**英文界面里那一行写的是 `Follow the system`**
   —— 日志：`switch: nothing among the 3 rows is named [跟随系统]` → Bail，**装的那份留在英文**。
   这个模式自己的注释早就写着"第一行说的是一个行为而不是语言，所以它会被翻译"，而 `-To` 那一支没用上这条
   知识（只有 restore 那一支有配对）。现在抽出 `LanguageNeedles`：行为行的两种拼法一起给，switch 与 restore
   共用。**这一条不是嘴上说的**：修复前同一命令 exit 1（上面那行日志），修复后**同一台英文界面的包上同一
   命令 exit 0** 并点到 `switch: clicking |Follow the system|`；最后用一轮 `deletesummary` 复核装的那份确实
   回到中文（`menu row |删除页面(E)…|`、中文小结、`form sentence on screen=False`）。
   **教训记进 AGENTS.md**：一个会改用户设置的探针，改回去的那条路必须和改过去的路同一次验收，不能等下一次
   要用时才发现它只认一种拼法。
4. **`.ps1` ASCII 第 12 次**，还是同一处毛病（注释里写了屏幕原文 `跟随系统`，12 字节），改完就查当场清掉；
   解析器全程 0 错 —— 再次说明**能过解析不等于没中**。
5. **数字**：产品代码 +0/−0；`tools/gui-probe.ps1` 本片 **+43/−14**（工作树里 §89–§92 四片合计 258/52，
   文件 2297 行）；ASCII **0**、解析 **0** 错；没重编、门禁没重跑（改的都是脚本）；无残留 xyzpdf 进程；
   控制拷贝已删；`mergedonor` 现在需要 `-Donor` 与第二个供体同目录（给路径时它自己派生，不再靠"名字碰巧
   在那儿"）。

**仍然欠的**：`mergedonor` 末尾那句空状态为什么空（现在三种组合都复现）；壳框为什么拒斜杠（§90 收成"壳的
输入框一族"）；§66 那颗下拉箭头无法回头量（§91）；`%1` 要不要换 numerus（§88 已有屏幕原句）；§55 XFA；
§47 那句读者可见的话；§75 路 (a)；widget 自己画 `/AP`；§79 的锁与就地保存；§49 那 28 个括号；`/Brepro`；
§84 那两颗 DLL；§82 那句推论。**没重打包**：§16 那"三处相同"自 §83 起欠 §84–§92 九片。
**已经还上的**：§90 末尾点名的"这四支在装的那份上的英文那一遍"。
## 93. 书签框那句 `%1` 换成了 `%n`：一条目录形状规则永远看不见的缺陷，现在有了会红的探针（2026-10-11）

他一句"先去修那个书签对话框的 %1" —— §74 钉住、§88 拿到屏幕原句、之后一直等他一词的那条。**这是这一串里第一片动产品代码的**（§89–§92 四片都是 +0/−0）。

1. **改法就是这一族已有的那一个形状**：`src/ui/bookmarkdialog.cpp:392`
   `tr("\n\n其中 %1 条不指向任何一页。").arg(headings)` → `tr("\n\n其中 %n 条不指向任何一页。", nullptr, headings)`，
   与同一个函数里 405/421/438 那三句一致；行号没挪（一行换一行），所以目录里那条 `<location line="392"/>` 照旧对得上。
2. **目录**：`Resources/translations/xyzpdf_en.ts` 那一条从 `<message>` 变成 `<message numerus="yes">`，
   两支 `<numerusform>` —— `%n of them points at no page.` / `%n of them point at no page.`（单数形里那个数字仍然写出来，
   跟 §32 那一族其它条目一样）。门禁跑完：目录 **578 条一字未动、numerus 47→48**，`26 passed / 0 failed`。
3. **屏幕上两种语言都读到**（§84 那条"写在两支语言里的断言只跑过一支等于没写"）：
   英文加一条之后 `Pressing leaves this document with 185 bookmarks. / **1 of them points at no page.** / 184 bookmarks …`
   —— 与 §88 那次唯一的差别就是那个 `s`；中文那一遍 `其中 1 条不指向任何一页。`（措辞没动，只是改由 `%n` 填）。
4. **为什么这条必须有一条探针，而不是改完就算**：`%1` 这种写法在目录形状规则眼里是**完全合法**的 ——
   `%n` 压根没出现，所以 §32/§33 那几条 numerus 规则（掉形、两形相同、单数写成多数）一条都不会看它一眼。
   能发现的只有"按 n=1 与 n=40 各问一次再比"这一种，而那是 `i18n_smoke` 的探针表在做的事 —— 于是加了一行
   `{ "BookmarkDialog", "\n\n其中 %n 条不指向任何一页。", 1 }`（+8 行，含为什么加它的注释）。
   **顺带这条也补上 §32 那句地基的另一半**：探针表同时是 lupdate 的 `-no-obsolete` 的保命符，
   挂在表上的串不会被删掉。
5. **控制（让这条新断言证明自己能红）**：把目录里那支单数形改回多数措辞（`points`→`point`，于是两形一样），
   **重建**再跑（`--run-only` 会跑上一版二进制，而 `.qm` 是在构建时嵌进测试的 —— 不重编就等于没改）：
   门禁 **25 passed / 1 failed**，红的正是这一条 —— `FAILED xyzpdf_i18n_smoke (1) FAIL: BookmarkDialog: "…"`
   配那句"needs a second plural form, not the same sentence with another number in it"（掩码掉数字之后两形相同）。
   恢复目录、再跑一遍：**26/0**。
   **注意红的是哪一条检查**：不是"单数读起来得像单数"那条正则（`1 of them point` 里数字后面紧跟的是 `of`，
   它看不见错），而是"两形必须不同"那条 —— 这条如果只写前者，这个形状照样会溜过去。
6. **数字**：产品代码 `src/ui/bookmarkdialog.cpp` **+1/−1**，`Resources/translations/xyzpdf_en.ts` +8/−3，
   `tests/i18n_smoke.cpp` +8/−0；门禁三遍（改完 26/0、控制 25/1、恢复 26/0）；目录 578 / numerus 48 / 0 unfinished；
   dev exe 从 §82/§83 那颗 `0f744cd07d73…` 变成 **`9395f1d3a81a…` / 1,341,952** —— 字节数一样而哈希换了，
   因为这次**真的重链了**（§82 那条规矩的正向用法：现在这颗哈希记的是这一次链接事件，不是"代码变了"的证据）。

**仍然欠的**：**没重打包** —— §16 那"三处相同"自 §83 起欠 §84–§93 十片，而装上的那份现在比源码落后这一条
修复（他要是点头重打包，这条就一起上去了）。其余照旧：§47 那句读者可见的话、§82 那句"打印/导出/缩略图/
预览窗格同样丢表单值"仍是推论没测、§75 路 (a)、widget 自己画 `/AP`、§79 的锁与就地保存、§49 那 28 个括号、
`/Brepro`、§55 XFA、三条"为什么"（空状态行 / 壳框拒斜杠 / 那两颗 DLL）、上传包之前该有人再读一次 payload 对照。
## 94. §82 那句推论不再是推论：打印、导出、缩略图、预览窗格各自量过一遍，六行都是 widget=0（2026-10-11）

他一句"继续"。§82 末尾挂着的那条 —— **"打印/导出/缩略图/预览窗格同样丢表单值"是同一条 `render()` 的推论、
没测** —— 这一片把它变成读数。产品代码 +0/−0，动的是 `tests/documenteditor_smoke.cpp`。

1. **先看清那四条路各自调的是什么**：`thumbnailpanel.cpp:80`、`thumbnailprovider.cpp:111`、
   `previewhandler.cpp:846` 都是 `render(page, size, RenderOptions{})`（`annotations` 的默认是 true，
   `document.h:52`），`imageexport.cpp:82` 与 `printjob.cpp:190` 则各自**显式**写 `annotations = true` +
   `optimizedForLcd = false`。所以决定这件事的地方只有一个：`QtPdfBackend::render()` —— 而 §82 只在那儿
   量过**一个尺寸、一套选项**。
2. **表就按每条路自己的尺寸与选项建**（六行 × 两种形状，同一份 200×30 外观流，只是挂在 `/Widget` 还是
   `/Square` 上）：
   | 路 | 尺寸 | 落点框（像素） | `/Widget` | `/Square` |
   |---|---|---|---|---|
   | 页面视图 fit-width | 1240x1753 | 125,441 417x63 | **0** | 26271 |
   | 侧栏缩略图条 | 150x212 | 15,53 51x8 | **0** | 408 |
   | 壳的缩略图 | 260x368 | 26,92 88x14 | **0** | 1232 |
   | 预览窗格 | 400x566 | 40,142 135x21 | **0** | 2835 |
   | 导出图片 150dpi | 1240x1754 | 125,441 417x63 | **0** | 26271 |
   | 打印 A4 300dpi | 2480x3508 | 250,883 834x125 | **0** | 104250 |
   数的是**落点框之内**的差异像素而不是整页 —— 150 px 宽时那块标记只有 51×8 = 408 像素，整页差异会把
   重采样的噪声算进来当成答案。**那一行的 408 正好等于整框**，这也是这张表自洽的地方。
3. **控制（这张表必须能红）**：把 `renderAt` 里的 `annotations` 关掉再重建再跑 —— 六行的 `square` 全部
   变 0，红的正是"必须有差异"那一句：`FAIL: page-view-fit-width: an annotation-layer mark reaches 0
   pixels at this size, so the widget row below is not a channel that cannot see the layer at all`
   （`thumbnail-strip`、`shell-thumbnail` 等六条一起红）。**这条控制把 §82 那条规矩演成了实物**：
   关掉唯一的开关之后，`/Square` 也读 0 —— 所以"widget 读 0"这句话单独讲出来什么也不说，
   它必须有同一行"这一层在这个尺寸确实画得出来"陪着。恢复 → 门禁 **26/0**、手跑 **0 FAIL / 0 WARNING**。
4. **顺带钉住一条默认值**：`check(RenderOptions{}.annotations, ...)` —— 两条壳路径什么选项都不传，
   整张表对它们成立的前提就是这个默认；哪天默认翻了，这四行读数说的是另一件事。
5. **这一片没有做的**（说清楚，别让它读起来像端到端）：没开打印机、没跑 `imageexport` 的写文件、没起
   壳的 COM。量的是**这四条路唯一共用的那一个决定点**，用它们各自的尺寸与选项。若将来某条路自己换了
   选项，这张表不会知道 —— 所以每一行都写着它抄的是哪个文件哪一行（注释里）。
6. **数字**：产品代码 +0/−0；`tests/documenteditor_smoke.cpp` **+122/−0**（六行表 + 那条默认值断言），
   `check(` **884→892**；门禁三遍（表 26/0、控制 25/1、恢复 26/0）；手跑 0 FAIL / 0 WARNING / 57 note；
   dev exe 仍是 §93 那颗 `9395f1d3a81a…` / 1,341,952 —— **只改测试源码，应用没被重链**（§91 那半句的又一次
   命中）。

**仍然欠的**：**没重打包** —— §16 那"三处相同"自 §83 起欠 §84–§94 十一片，而 §93 那条修复还没进装的那份；
**§55 XFA 那份第二副本在这四条路上同样没量**（我们根本不读 XFA，所以它连"0 像素"都不是）；
`mergedonor` 末尾空状态行为什么空；壳框为什么拒斜杠；§66 那颗箭头无法回头量；§47 那句读者可见的话；
§75 路 (a)；widget 要不要自己画 `/AP`（**这一片把它从"文案问题"抬成"打印/导出也会漏"的问题**——
用户打印一份填好的表单，纸上会是空白字段）；`/Brepro`；§79 的锁与就地保存；§49 那 28 个括号。
## 95. `mergedonor` 末尾那行空状态为什么空：不是消息没出来，是这一族从来不说 —— 而"为什么空"问错了对象（2026-10-11）

他一句"继续"。三条没查的"为什么"里，这一条属于我们自己的产品，先拿它开刀。§87 记下"收尾状态行为什么空"，
§88 又记一次，§90 说装的那份也空，§92 说英文下还空 —— **四次复现，一次没查**。

1. **先读代码，别先猜**。`statusBar()->showMessage(...)` 这个 API 在两份文件里：**`mainwindow.cpp` 有 19 处**
   （`已导出 %n 张图片到 %1` :1414、`已写成一个新文件（%n 页）：%1` :1440、Word 导出 :1761、
   合并其它窗口 :2007、启动时那句 `就绪` :1308…），而 **`documenttab.cpp` 一处都没有**（`grep` 计数 0）。
   `documenttab.cpp` 正是所有**待保存修改**的落点：`mergeFiles()` :1218、拆分、删页、旋转、水印、加/解密
   全在那儿。**所以合并之后状态条是空的，不是消息没来得及显示，而是这条路从来没有人往状态条上写话** ——
   它的回执是别的两样：标签上的 `*`（脏）和撤销行（`Undo Merge in 2 pages (donor2.pdf)(U)`），两条都在同一支
   模式里读到过。**这条分界是有形状的**：状态条说的是"事情在别处完成了"（写出去的文件、并进来的窗口），
   不是"我记下了一笔"。
2. **所以错的是那行读数，不是产品**。模式原来在收尾处印 `status: []` —— 一句话不说地摆出一个看起来像
   测量的空值，于是每一遍读到它的人都以为少了什么（这就是 §87 记下这条债的原因）。现在那一行**带着期望**：
   `status: []  expected: no announcement -- a pending edit is not a completed write, and documenttab.cpp
   never calls setStatusMessage`，并且**能红**：状态里只要出现 `merge` / `Merge` / `合并` 就 Bail。
3. **控制（两次都跑到那一行）**：把读到的状态换成 `'Merged 2 pages from 2 files'` 的拷贝 → 两遍都
   **exit 1**，消息是 `the status bar now announces a merge ("Merged 2 pages from 2 files"). Either pending
   edits started speaking or a merge began writing something when it was accepted`，并且各自先印
   `CONTROL H` 那一行自证。**第一遍其实没跑到**：它在更早的 donor 选择处就红了（`no native dialog within 8s`
   → `the donor file was not picked`，exit 也是 1）—— 跟 §86/§90 那两次同一个形状：**退出码 1 不等于控制跑了**，
   所以这一片把"跑到没跑到"用 `CONTROL H` 那一行钉在日志里。谓词也单独验了四种输入
   （空 → 不红；英文宣布 → 红；中文 `合并2 ` → 红；只有一个 `*` 的畸形 needle → 不红）。
4. **这一片没有做的**：没有给"待保存修改"补一句状态条消息（那是产品决定，而且 §77/§80 那一族已经把话说在
   了**按下之前**的框里，不是没地方说）；也没去量另外五路编辑的收尾状态行（`splitsagain` / `deletesummary`
   这些模式根本不读状态条）—— 代码侧的计数（0 对 19）覆盖的是"这些编辑都在 `documenttab.cpp`"这一条前提。
5. **数字**：产品代码 +0/−0；`tools/gui-probe.ps1` **+18/−1**；ASCII **0**、解析 **0** 错；无残留进程；
   两份控制拷贝删掉。**三条"为什么"现在还剩两条**：壳的输入框为什么拒斜杠（§88/§90 已收成"一族"），
   windeployqt 为什么不再带 `dxcompiler.dll`/`dxil.dll`（§84）。

**仍然欠的**：**没重打包** —— §16 那"三处相同"自 §83 起欠 §84–§95 十二片，§93 那条修复仍未进装的那份；
剩下两条"为什么"；XFA 那份第二副本在这四条渲染路上同样没量（§94 末尾记着）；§47 那句读者可见的话；
§75 路 (a)；widget 要不要自己画 `/AP`（§94 把它抬成"打印会出白纸"）；`/Brepro`；§79 的锁与就地保存；
§49 那 28 个括号；§66 那颗箭头无法回头量（§91）。
## 96. 那两颗 DLL 不是"为什么不再带"，是"那一次为什么带"—— 决定它的是打包那个 shell 看不看得见 Windows SDK（2026-10-15）

他一句"把剩下没做的做了"。剩下那些**大部分是等产品决定**的，我不动；能自己做的两件是：§95 那条新断言的
另一支语言，和 §84 挂着的那条"为什么变没查"。仓库里的文件这一片**一字未改**（全是量）。

1. **问题本身问反了**。§84 记的是"windeployqt 为什么不再带 `dxcompiler.dll`/`dxil.dll`"。把四份 payload
   历史按时间摊开就清楚了：`20260930` 67 个文件、`20261005` 67、`20261008-0929` 67、`20261008-0936` 67 ——
   **只有 §83 那次（`20261008-0124`）是 69 个、带着那两颗**。所以该问的是"那一次为什么会多"。
2. **出处不是 Qt**。把 Qt 树按名字走一遍：`E:\qt\6.11.2\msvc2022_64\bin` 里**没有** `dxcompiler.dll`/`dxil.dll`
   （只有 `d3dcompiler_47.dll` 4,173,928）。而构建树那两颗与 **Windows SDK** 的 x64 那颗**字节相同**（**§97 更正：
   这句错 —— 当时只比了大小。`bin\10.0.26100.0\x64` 那颗是 `97ABD3B3…`，`Redist\D3D\x64` 那颗是 `A5AA1D9A…`，
   同尺寸、不同字节；构建树那颗哈希等于 Redist，所以它来自 Redist 而不是 bin**）：
   `E:\Windows Kits\10\bin\10.0.26100.0\x64\dxcompiler.dll` = 14,317,000、`dxil.dll` = 1,509,800
   （`Redist\D3D\x64` 同字节 —— **这句也是同一处错的产物，见上**）。**这里还有一次我自己的假阴性**：第一遍我按 `C:\Program Files (x86)\Windows Kits`
   找，什么都没找到，就写下"机器上没有这颗" —— 而这台机器的 SDK 在 **E:**（注册表 `KitsRoot10`，
   `packaging/windowskit.ps1` 的注释早就写着"VS 把它装到别的盘"）。**"没找到"是搜索范围的话，不是事实的话。**
3. **机制做成了一对可复现的读数**：同一份 `xyzpdf.exe`、同一套 flag（`--no-translations
   --no-compiler-runtime --no-opengl-sw`，故意不给 `--no-system-d3d-compiler`），`--dir` 到两个临时目录：
   - 普通 shell → 部署目录 **8 颗 DLL**，没有那两颗；
   - 把 `E:\Windows Kits\10\bin\10.0.26100.0\x64` 放进 PATH 并设 `WindowsSdkDir`/`WindowsSDKVersion` →
     **10 颗**，多的正是 `dxcompiler.dll` 14,317,000 与 `dxil.dll` 1,509,800。
     （**§97 补：这一趟两个变量一起加，所以它说不出是哪一个决定的**。拆成一趟只换一个变量之后：只设
     `WindowsSdkDir` → 落下来的是 `Redist\D3D\x64` 那两份字节；只把 `bin\...\x64` 放进 PATH → 落下来的是 `bin`
     那两份。**各自都够，而且是两份不同的物理文件。** 另：这里的 8/10 只数了部署根目录，按整棵树数是 25/27 ——
     差值同一，分母不同，别拿这两对数字互相对照。）
   **所以 §60 那句"windeployqt 的答案每次自己决定"在这两个名字上有了机制：决定它的是打包那个 shell 看不看得见
   Windows SDK，跟仓库里的任何东西无关。** 这也把 §84 那条 strip 规矩从"留着当常设规则"抬成有机制支撑：
   从开发者提示符里打包，那两颗**会回来**。
4. **量不回来的那一半要说清**：§83 包里那颗是 **25,684,592** 字节，而现在机器上任何一颗 `dxcompiler.dll`
   都不是这个尺寸（SDK x86 10,234,312 / x64 14,317,000 / arm64 33,176,528；Edge 那几颗 20,487,496 与
   20,599,112）。最可能是当时构建树里躺着**另一份、更新前的 SDK 副本**，后来被 windeployqt 的
   `Updating <file>.` 覆盖成现在这颗 —— **这是推测，不是读数**；那颗文件的字节已经找不回来了，
   （**§97 把这条推测压低了**：这台机器五个 SDK 版本目录里只有 `10.0.26100.0` 带这文件，两处都是 14,317,000，
   而对八个根按尺寸重扫，正好 25,684,592 的文件是 0 个 —— 含回收站。活着的那条是 §97 量出来的机制本身：
   PATH 那一支按名字捞、连"是不是 DLL"都不问，所以那一份**可以来自任何一个 PATH 目录**，今天正有一颗
   Chromium 家族的 25,923,736 在那儿。机制够了，名字仍然没有 —— 不许写成"解释了"。）
   所以这一条只结到"机制清楚了"，没结到"那个数字解释了"。
5. **§95 那条断言的另一支语言**：`-Mode mergedonor -Language zh_CN` 开发版 exit 0，
   `status: []  expected: no announcement…` 照旧、两条撤销行都在
   （`撤销 合入 1 页（donor1.pdf）(U)` / `撤销 合入 2 页（donor2.pdf）(U)`）。**边界要说实话**：中文那一支的
   "能红"只在**谓词层面**验过（四种输入：空不红 / 英文宣布红 / 中文 `合并2 ` 红 / 畸形 needle 不红），
   端到端那次红是用**英文**假状态打出来的 —— 中文 needle 的 Bail 路径没有端到端跑过。
6. **顺手把 §49 的数字钉正**：带括号助记符的英文形是 **29 条**，不是记录里的 28（`(&amp;X)` 在 `.ts` 里是
   XML 转义过的，我第一次按 `(&X)` 数得到 0 —— 差点把"这条已经修好了"记进去）。而且它**不是**不需要决定的
   机械活：`Print(&P)` 还是 `&Print` 是英文菜单的风格选择，而且改了会动 UIA 的 Name，harness 那些按名字找行的
   needle 要跟着重读两遍（中英各一遍）。回到等你一词。
7. **数字**：产品代码 +0/−0，仓库文件 0 个改动；清场：两个临时部署目录（各约 40 MB）删了，无残留
   xyzpdf 进程。

**仍然欠的**：把"从开发者环境打包会把那两颗带回来"变成**端到端**读数（要一次重打包授权，顺带才对上 §16 那
十二片的账）；壳的输入框为什么拒斜杠（§88/§90 收成一族，机制仍未查）；§49 那 29 条（风格 + needle 重读）；
§75 路 (a)；widget 自画 `/AP`（§94 已抬成"打印会出白纸"）；`/Brepro`；§55 XFA；§47 那句；§79 的锁与就地保存。


## 97. 两条还欠着的"为什么"，都不用等人定词：windeployqt 那两颗是按**名字**从 PATH 捞的（连文件类型都不问），壳的名字框拒的是"**任何一个斜杠**"（新 `-Mode pathforms` 六行表）（2026-10-15）

他一句"继续"。这一片还是不动产品代码：产品 `.cpp`/`.h`、目录 578 / numerus 48 一字未改。

1. **先撤自己上一片写错的一句**（§96 第 2 条）。我在那条里写下"构建树那两颗与 SDK 的 x64 那颗**字节相同**
   （`Redist\D3D\x64` 同字节）"——实际我只比了**大小**。真哈希摆出来：
   `E:\Windows Kits\10\bin\10.0.26100.0\x64\dxcompiler.dll` = `97ABD3B3083607DC…` / 14,317,000，
   `E:\Windows Kits\10\Redist\D3D\x64\dxcompiler.dll` = `A5AA1D9A95BF9EA6…` / 14,317,000 ——
   **同尺寸、不同字节**。而构建树那颗是 `A5AA1D9A…`：**它来自 Redist，不来自 bin**。`dxil.dll` 同一对形状
   （bin `FDA4DDE9…` / Redist `95AC1BB4…`，树里是 `95AC1BB4…`）。这就是 §83 那条规矩（"比字节的每一行要带上
   大小与在不在"）的反面：**大小相同从来不是身份**，认出处只能比哈希 —— 而"同一尺寸的两个不同文件"这一族，
   一个 PE 的 debug 目录/时间戳就够造出来（§82 那句在另一头等着：哈希相同也不说明源码相同）。
2. **§96 那趟实验其实说不出是谁决定的**：它把 PATH 与 `WindowsSdkDir` **同时**加上了。这次拆成四趟，每趟只换一个变量：
   - 干净 shell（两者都不给）→ 部署树整棵树 **25 颗 DLL**，那两颗 ABSENT，而 windeployqt 自己把话讲出来：
     `Warning: Cannot find any version of the dxcompiler.dll and dxil.dll.`（§96 记的 8/10 只数了根目录，
     这次按整棵树数 —— **差值同一，分母不同**，别再拿这两对数字互相"对照"）
   - 只设 `WindowsSdkDir`（+`WindowsSDKVersion`），PATH 干净 → **27 颗**，落下来的字节 = **Redist** 那两份
   - 只把 `bin\10.0.26100.0\x64` 放进 PATH，不设 `WindowsSdkDir` → **27 颗**，落下来的字节 = **bin** 那两份
   **两个变量各自都够，而且给的是两份不同的物理文件。** 事后要问"这一包是从哪条路来的"，只有比哈希答得出。
3. **再往前一脚才是机制**：临时目录里放一颗**改了名的 `dxc.exe`** 当 `dxcompiler.dll`，只把这个目录加进
   PATH → windeployqt 印 `Updating dxcompiler.dll.` 并把它收进部署树：落地 250,296 字节、哈希与我那颗假文件
   **逐字节相同**。它不看版本、不看架构、**甚至不看它是不是 DLL**。同族第二趟只改一个变量——假 `dxil.dll` 从
   SDK 的 x86 那份换成 x64 那份：x86 被拒（**26 颗**），x64 被收（**27 颗**）。
   **所以那两颗的搜索是不对称的：`dxcompiler.dll` 那一支按名字捞且什么都不问，`dxil.dll` 那一支会看架构。**
   这条把 AGENTS.md 那句从"看得见 SDK 就多两颗"抬成一句更硬的话：**打包那个 shell 的 PATH 里任何目录只要有
   一个叫 `dxcompiler.dll` 的文件，那个文件就进包** —— `build-package.ps1` 的 strip 清单因此不是装饰，
   而"从开发者提示符里打包"这件事本身就是包内容的一个来源。
4. **§83 那个 25,684,592 仍然没有名字**，但§96 那个推测现在站在下风。把 14 个命名根走一遍，找到 15 颗
   `dxcompiler.dll`/`dxil.dll`，**没有一颗是这个尺寸**：arm64 33,176,528 / Chrome 155 的 25,923,736 /
   EdgeCore 与 Edge 155 的 20,487,496 / Edge-WebView 的 20,599,112 / SDK bin 与 Redist 各 14,317,000（不同字节）/
   SDK x86 10,234,312 / 假树里那颗 250,296。再按**尺寸**对八个根重扫一遍：正好 25,684,592 的文件 **0 个**
   （含 `C:\$Recycle.Bin`）。§83 那一趟的 `.msix` 也早被覆盖 —— `package/` 里现在躺着的是 2026-10-08 **17:31**
   那一颗（§84 的控制包），`makeappx.log` 不提这两个名字。**SDK 版本目录这一支被量掉了**：五个版本里只有
   `10.0.26100.0` 带这文件，且两处都是 14,317,000。活着的那条是：**PATH 里任何一个同名文件**——今天正有一
   Chromium 家族的 25,923,736 在那儿（版本目录 2026-10-13 换过，10-08 那天的那颗可能正是同族另一版）。
   **机制够了，那一份还是没名字 —— 这一条不许写成"解释了"。**
5. **`-Mode pathforms`：壳的名字框到底拒什么**。§88/§90 收了"壳的框拒斜杠"这一族，两遍都只记下**拒**，
   没问**句子里哪一段**让它拒 —— 而壳的源码在这里读不到，编一个理由正是这套仪器反复抓自己的那一类。
   于是把边界量出来：同一份真实文件六种拼法，逐行**原样**写进名字框（id 1148，不归一化 —— 拼法就是被试的
   那个变量），每行两个**不是点击**的收据（我们的窗口标题有没有变成目标文件名、框有没有关）：
   - R1 `H:\tmp\sec97box\sec97-target.pdf` → `landed=True pickerClosed=True`（正对照：这条路能打穿）
   - R2 全斜杠 → 拒（§90 那一支在**开发版**上复现）
   - R3 `H:\tmp\sec97box` + `/sec97-target.pdf` —— **整串只有一个 `/`**，其余全反斜杠 → **拒**
   - R4 `H:/tmp/sec97box/`（只有目录段，没有文件名）→ 拒，而且**没导航**（框里那张列表读不到目标文件）
   - R5 反斜杠、指向一个**不存在**的文件 → 拒（负对照：`landed` 不是自证的；注意这行
     `folderShowsTarget=True` —— 框此刻正显示目标所在目录（R1 那趟导航过去的）却什么都没开。**"看得见"
     永远不是"接受了"**，这一行差点成为我自己的假阳性）
   - R6 同一个斜杠串写进 id 1001 → 没反应；而 dump 出来的类名替我把这条问完了一半：**1001 是
     `ToolbarWindow32`**。所以这一行答的不是"地址栏解不解斜杠"，它答的是"2026-09-28 那条笔记为什么会一直
     是错的"——那条笔记往一个工具栏 `WM_SETTEXT`（§90 归到"写错控件"，这次给出类名）
   **收成的句子：名字框里出现**任何一个** `/` 就拒** —— 不是"全斜杠才拒"，也不是"前缀得是反斜杠"；
   目录段的斜杠（R4）与文件名段的斜杠（R3）各自单独都够触发。
6. **两根红，各红各的**：控制 A 把 R1 的拼法换成斜杠而期望照旧 `land` → exit 1，印
   `R1-backslash-full: the backslash form did NOT reach our window, so this harness cannot open a document
   through its own picker…`；控制 B 把 R2 换成**能用的**反斜杠而期望照旧 `refuse` → exit 1，印
   `R2-all-slash: this form OPENED a document [sec97-target.pdf — XYZPDF]. Either the slash-path reading
   (sec.90) or the missing-file control is wrong, and they cannot both stand`。两条都由**自己那一行的 tag**
   作证，所以"跑到那一行"不是猜的。
   **而控制 A 的第一遍"exit 1"是假的**：我把一条 python 检查放在 `&&` 链前面，python 用 MSYS 形路径
   `/h/tmp/...` 打不开文件而非零退出，于是那条 PowerShell **根本没启动**，`$?` 照样印 1。
   **这是本项目第四次把 exit 1 当成"控制跑了"**（§86、§90、§95、§97）—— 现在先跑控制、再单独 echo 退出码。
7. **仪器的形状也改了一处**：`ShowOpenDialog` / `FindPicker` / `SetPickerFieldRaw` / `PressPickerAccept` 从
   `-Mode openpicker` 里抽出来给两支模式共用（抽完当场重跑 openpicker：框起来、读到 4 行、按 Cancel 收掉 ——
   抽走的那 41 行一句不少地活在函数里，`git diff` 的 −48 行全是"搬动 + 那行被断言取代的裸 status"）。
   新 `-Target` 参数带一条硬 Bail：**`-Doc` 与 `-Target` 同名就拒跑** —— 启动实例已经打开 `-Doc`，标题里
   本来就有那个文件名，"标题变没变"在碰框之前就是 True。这是 §77/§95 那一族（断言自己往被数集合里放东西）
   在屏幕这一层的形状，所以它写在代码里而不是写在注释里。
8. **数字**：产品代码 +0/−0；`gui-probe.ps1` 这一片 **+172/−47**（对 HEAD 的总账 +190/−48，里面含 §95 的
   +18/−1），ASCII 违规 **0**、`PSParser` 错 **0**；跑过：windeployqt 五趟（干净 / 只 `WindowsSdkDir` / 只
   PATH / 假 DLL 两变）、`openpicker` 回归 1、`pathforms` 1、控制 2。清场：三棵临时部署树与两颗假 DLL 删了，
   `Get-Process xyzpdf` 计数 **0**。

**仍然欠的**：25,684,592 那一份是谁（机制清楚了，名字没有 —— 不许合上）；**地址栏对斜杠的回答仍然没有**
（1001 不是文本框，要问得换一条路：UIA 的 `ValuePattern` 或干脆不问）；`pathforms` 的中文那一支（这套 needle
是英文菜单行 + `CjStr`，两支都在，但没在中文界面重跑）；剩下那些等他定：§49 那 29 条、§75 路 (a)、widget 自画
`/AP`（§94 已抬成"打印会出白纸"）、`/Brepro`、§55 XFA、§47 那句、§79 的锁与就地保存。重打包/重装、提交、
push 各要一词 —— §16 那"三处相同"欠 §84–§97 **十三片**。


## 98. §49 那 29 条英文助记符改成词内的 `&` 了 —— 而"要不要改"其实早被目录里那 8 条答过（2026-10-15）

他一句"继续做下一个欠账的 §49 那 29 条"。这一片动的是**目录里的英文形**（`Resources/translations/xyzpdf_en.ts` 29 行）
和**一根新的目录规则**（`tools/check_translations.py` +56/−0）；产品 `.cpp`/`.h` 一字未改，中文形一字未改。

1. **先说这条欠账为什么挂了 67 片**。§49 在屏幕上把英文菜单栏读出来那天就看见了：
   `|File| |Edit(E)| |View| |Document(D)| |Go| |Window(N)| |Language| |Help|` —— 同一排里 `File`、`View`、`Go`、`Help`
   干净，`Edit(E)`、`Document(D)`、`Window(N)` 带着括号。此后 §88/§90/§92 三支模式都**在英文界面里读过行**，
   每次都把这排名字原样印进日志，没有人把"括号"当成一件事。理由不是没看见，是**没有任何一道检查能看见它**：
   (context, source) 完全匹配、行不 obsolete、numerus 对、段落换行对 —— 四条形状规则管的是别的形状。
   **屏幕上一眼就有的东西，在仪器里是隐形的。**
2. **"要不要这样改"不是没答过，是答过 8 次**：目录里早就有 `&File` `&Open…` `Close &Tab` `E&xit` `&View` `&Go`
   `&Help` `&Settings…` 八条词内 `&` 的英文形 —— 也就是说英文那一侧的自家风格一直写在同一个文件里，
   剩下 29 条是抄了源文形状的漏网之鱼。**这一条把我自己上周的分类推翻了**：§96 我把这 29 条记成"等他定风格"，
   其实风格早已定，只是没人去数"已改的有几条、没改的有几条"。**"这需要一个决定"要去查有没有已经做过八次的决定。**
3. **改了什么**（29 条英文形，源文一个字不碰）：菜单条 `&Edit` `&Document` `&Window`；文件菜单
   `&Export to Word…` `&Print…` `Export as &Images…`；编辑菜单 `&Copy` `&Translate` `Select &All` `&Find…`；
   查看菜单 `&Night Reading` `&Full Screen` `&Present`；文档菜单 `De&lete Pages…` `Ro&tate Pages…`
   `&Move Pages…` `Merge Files &In…` `&Split Files…` `Encrypt / Remove &Password…` `Add &Watermark…`
   `&Edit Bookmarks…` `&Undo` `&Undo %1` `&Redo` `&Redo %1` `Save Changes &As…`；窗口菜单 `&Merge All Windows`；
   PageView 右键 `&Copy` `&Translate`。**同一个菜单里的助记字母互不重复**（文档菜单：L T M I S P W E U R A，
   与菜单条自己的 `&Document`(D) 不撞），`…` 的位置照旧跟着词干，不是钉在末尾。
   **一处源文字母与英文无关**：`加密 / 去掉口令(&J)` 原本英文抄成 `(&P)` —— 英文形本来就在自选字母，
   这条恰好说明括号那一格从来不是"翻译"，是格式。
4. **屏幕上这一遍是**先跑再改的（§91 那条规矩：仪器在修之前先跑一遍，并把输出留着）：改之前五支菜单
   `File`/`Edit`/`View`/`Document`/`Window` 全读完存进 `H:\tmp\sec98-before-*.txt`；门禁重编之后再读一遍：
   **37 行的 UIA Name 变了**（去重后 25 个不同标签，其余是菜单条在同一次读取里重复出现），
   而**每支菜单的行数一字未动**（15 / 14 / 27 / 19 / 9）—— 改的是名字不是结构：
   `Print(P)…`→`Print…`、`Delete Pages(E)…`→`Delete Pages…`、`Undo(U)`→`Undo`、`Merge All Windows(G)`→`Merge All Windows`。
   两支带 `%1` 的撤销行（`撤销 %1(&U)` / `重做 %1(&O)`）**只在有未完成编辑时才在屏幕上出现**，而 §39 那一次
   只读到不带 `%1` 的那支 —— 这一片顺带补上：`row |Undo Merge in 1 page (donor1.pdf)|`、
   `row |Undo Merge in 2 pages (donor2.pdf)|`、`row |Undo Split into pieces of 3 pages|`。
   25 个屏幕标签 + 2 条 PageView 右键（这一片没读右键）+ 2 条 `%1` 孪生 = 29 ✓ 数目闭合。
   中文那一侧当场也读了两支菜单确认没动（`删除页面(E)…` / `撤销(U)` 照旧 —— 那是正确的 Windows 中文习惯）。
5. **harness 要不要跟着改：读数说不要**。`MenuRow` 一直是 `-like ('*' + $needle + '*')`（子串），
   所以 `Split Files`、`Merge Files In`、`Open`、`Settings` 这些 needle 在名字丢掉 `(C)`/`(F)` 之后照样命中 ——
   但这不能靠推理，所以改完跑了三支英文回归：`mergedonor` / `splitsagain` / `deletesummary` 全部 exit 0，
   日志里 `matched [Merge Files In] class=MergePagesDialog`、`menu row |Delete Pages…|`、
   `DeletePagesDialog found on round 1 (… it was in the main window subtree)`、拆框两句小结、删页框那句
   `No pages have been named yet. …`、还有 §95 那条 `status: []  expected: no announcement …` 都在。
   **§97 那句"改产品表面之前先问仪器会不会跟着坏"这一片有答案了：不会，而且有三支跑过的账。**
6. **新的那道规则**（`check_translations.py` 第四条，`mnemonic_rules_of`）：任何英文 `<translation>` /
   `<numerusform>` 里出现 `(&X)` 就失败，理由写在函数 docstring 里 —— 这不是"审美检查"，是把一类**只有眼睛
   看得见**的缺陷搬进机器。控制：把 `&Print…` 一行改回 `Print(&P)…` → 门禁那条 checker **exit 1**，印
   `MainWindow：打印(&P)… —— 英文形把源文的括号助记符抄过来了，应写成词内的 &：Print(&P)…`，
   然后按 sha256 **逐字节还原**（`cb04762275ab…` 前后相同）再跑一遍 → exit 0。
   **两条自己撞上的错要说**：控制脚本第一遍在"打印捕获到的输出"时崩在 GBK 编码上，而崩溃点**在还原那行之前** ——
   目录就那么留在控制状态里，是下一遍先读了一次"控制状态还在不在"才发现（`control state present: 1`）并还原；
   从此这类控制写成 try/finally + 哈希相等，而不是"跑完再手动恢复"。第二条：新加函数时一次 Edit 把函数体
   **复制成两段**，`python -m py_compile` 照样过（Python 允许 for 之后 dedent 到 def）—— **编译通过不结构就没坏**，
   这一族在 `.ps1` 那边记过（"能过解析不等于没中"），这次发生在 Python 上。
7. **没验的一层要说**：`&Print…` 现在把助记符注册到 Alt+P 是 Qt 的定义，不是这一片的读数 —— **没有任何一轮真按下
   Alt+字母**。屏幕上读到的是名字里没有括号了；键盘那一层留给下一次（或留给 WACK 之外的一次人工点击）。
8. **数字**：`Resources/translations/xyzpdf_en.ts` **+29/−29**（每条一行，行数没动，所以 `.ts` 里那些
   `<location line="…"/>` 全部照旧对得上）；`tools/check_translations.py` **+56/−0**；产品代码 +0/−0；
   目录 **578 条 / numerus 48 / 0 unfinished**（这条改动不新增任何一条源文，`(&X)` 只在源文里）；
   门禁一遍 **26 passed / 0 failed**、控制那遍 checker **exit 1**、还原后 **exit 0**；
   dev exe **`9793eae30a0e…` / 1,341,440** —— 比 §93 那颗少 **512 字节**，这是"英文形变短"应有的方向
   （§82 那句照旧：这颗哈希记的是这次链接，不是这份代码；不过这一片源码真的变了，所以少 512 字节是有意义的读数）。
   屏幕：BEFORE 5 支 + AFTER 5 支 + 英文回归 3 支 + 中文确认 2 支，全部 exit 0。
   `gui-probe.ps1` 这一片 +0/−0（只跑不改）。

**仍然欠的**：Alt+字母那一层没按过；PageView 右键那 2 条没在屏幕上读（改动已在目录与规则里）；`(&X)` 在**其它语言**
目录里会不会出现 —— 本项目的目录只有 en 一支（`CATALOGUE_LANGS = ['en']`），别的语言将来由谁管仍没定；
25,684,592 那一份是谁（§97 欠）；地址栏解不解斜杠（§97 欠）；`pathforms` 的中文那一支；
`deletesummary`/`splitsagain`/`mergedonor`/`saverename` 在**装的那份**上仍没跑（**这一片改的是英文目录，
装的那份里还是旧的括号形** —— §16 那"三处相同"欠 §84–§98 十四片）；等他定：§75 路 (a)、widget 自画 `/AP`、
`/Brepro`、§55 XFA、§47 那句、§79 的锁与就地保存。


## 99. §75 的路 (a) 走完了：书签的目的地从文件带下去、写回原样 —— 而"哪一行对应哪一条"这一步先做错了两次（2026-10-15）

他一句"继续将缺陷解决完"。这一片动产品：`src/edit/documenteditor.cpp` +298/−60、`.h` +33/−0、
`src/ui/bookmarkdialog.cpp` +16/−13、`src/document/outline.h` +46/−0、`outline.cpp` +2/−0；测试
`documenteditor_smoke.cpp` +112/−24（`check(` **892→902**）、`documenttab_smoke.cpp` +41/−4（**401→404**）、
`i18n_smoke.cpp` +9/−0；目录 `xyzpdf_en.ts` 106/106 行（旧串换成新串，条数一字未动：**578 / numerus 48 / 0 unfinished**）。

1. **缺陷是什么**（§75 钉住、§80 只挡住了"不知道"）：一次保存把 184 条 `/FitH <top>` 全拍成 `/Fit`。
   §80 那句屏幕上读到的原话是「这份文档里有 184 条书签指着页面内的某个位置……那一层位置**这一版留不住**」——
   那是**知情**，不是数据。这一片动数据。
2. **屏幕上先看到"不再喊"**：同一份真书（`H:\pdf_test\VerilogHDL硬件描述语言.pdf`，184 条全 `/FitH`，
   §75 数的就是它）复制到 ASCII 路径，`-Mode bookdialog` 中英各一遍，都 exit 0：加一条书签之后，
   小结里那句"位置留不住"**不再出现**（模式自己印 `summary names the positions-at-risk sentence=False`），
   而 §80 在同一个动作上读到的是"184 条"。同一遍顺带把 §98 的形状在这份真书上读到：英文菜单行
   `Edit Bookmarks…`、动态撤销行 `|Undo Set 185 bookmarks|`、`|Redo Set 185 bookmarks|` —— 括号没了。
   中文那侧照旧 `编辑书签(B)…`、`撤销 设定书签（185 条）(U)`（没动它，那是正确的写法）。
3. **机制**：开文件时 `outlineFromSource()` 按**对象链**（`/First`，先沿 `/Prev` 退回最左，再 `/Next`，
   父之后进子）走一遍，把每条 `/Dest` 的**种类 + 数字**读进 `sourceOutline`；写回时 `writeOutlineItem()`
   原样发出去。**null 必须留**：`/XYZ 0 700 null` 说的是"缩放别动"，写成 0 是另一句话 —— 所以
   `BookmarkDest::numbers` 是 `QVector<std::optional<double>>`，样本的 `/XYZ` 也因此从 `0 700 0` 改成
   `0 700 null`（改了 §75 钉的那两串原话，这里说清）。侧栏那棵树仍走 Qt 的读路（标题 + 页），
   两份读法在 `setOutline()` 里连起来。
4. **"哪一行对应哪一条"先做错了两次，两次都是测试逼出来的**：
   - **按行号连**（第一版）。测试里"改一条的名字"报 **2** 条有风险、"在最前面加一条"报 **3** 条 ——
     也就是插一条会把后面整棵树的目的地全丢。这不再是"顺序"能解释的：那是设计选错。
     现在**按内容连**：标题 + 页相等才算，按文件顺序各取一次。改完：改名 → 1，插入 → **0**
     （新增的断言就写着"在最前面加一条不该让别的书签丢位置"）。
   - 顺带量到的：用 `QPDFOutlineDocumentHelper` 的那一版，扁平四条的文件上"改一条"报了两条，
     而把两份列表并排打出来时它们**顺序是一致的** —— 所以诚实的话是"这个 helper 的顺序没有书面保证，
     而有一回它的行为像是不一样"，不是"它返回 Xyz 在最后"。代码注释里量到的和没量到的分开写。
     自己走链子之后这个问题不再存在（而且 helper 的构造函数是私有的，本来就拼不回"按句柄解析目的地"）。
   - **代价写在注释里**：再加一条**同名同页**的，两条里只有一条能带走目的地，另一条回到页顶。
     两种选法都不算说假话，所以要写下来，并且它的测试就是一行插入 + 一个重复标题。
5. **产品可见的那句话跟着改了**（`tr()` + numerus + 探针，§93 那套规矩）：原句说"这一版一条都留不住"，
   现在说「其中 %n 条书签指着页面内的某个位置，而这一次它们对不上文件里原来的条目：那几条写出去会变成
   整页适配。没被改动的书签，位置照旧带走。」计数从"文件里有几条带位置"（`sourcePositionalBookmarks()`
   留着当文件事实）换成"这一次请求会落下几条"（`positionalAtRiskFor(rows)`）。
   **为什么必须是"按下之前"的纯查询**：门禁第一遍红的就是这一处 —— 我先把计数存成"上一次请求"的值，
   而框在用户还没按的时候读到 0：那句话在屏幕上永远不会出现。这条与 §77「印在消息里的读数不是断言」、
   §80「代价要在按下之前讲」同族。
6. **控制：writer 那一行退回"永远 /Fit"** → 门禁 **25 passed / 1 failed**，红的正是新钉那句
   「a save that asked for the tree it was given writes the same … destinations back」；按 sha256 逐字节
   还原 → **26 passed / 0 failed**。
   **而第一个"控制"是空的**：我写成 `false ? nullptr : outlineDestKind(node.dest)`，三目仍然求值了那个
   调用，于是它"26/0 全绿"，看起来像断言根本没劲。**控制要能改变结果** —— 这一条差点把我带进
   "控制没红 ⇒ 我的断言是假的"那个反向错里。
7. **没做与没测的**（不许混进"修好了"）：
   - **侧栏点击仍只看页号**。位置现在写在文件里了，别的阅读器读得到；我们自己的 `PageView` 不读它
     （§70/§80 那条路一字未动）。这不是"目的地没保存"，是"我们不跳过去"。
   - 同一次保存里既旋转页面又带 `/XYZ` 的：数字按 PDF 的默认用户空间原样带走（没换算，也没测）。
   - 屏幕上"改一条要喊 1 条"这一支只有无头两处钉着（`documenttab_smoke` 的框、`documenteditor_smoke`
     的模型）；屏幕那一遍按的是"加一条不该喊"。
   - §47 那类"合并进来的注释链"不在这条路里（§78 已经处理）。
8. **数字**：门禁 26/0、控制 25/1、还原后 26/0；dev exe **`6ce4540cc1cc…` / 1,359,872**（比 §98 那颗大
   18 KB —— 这一片真的加了代码，不是只换链接时间戳；§82 那句照旧：这颗哈希记的是这次链接）；
   清场：临时样本目录删了，`Get-Process xyzpdf` 计数 0。

**仍然欠的**：`/Brepro`、§55 XFA、§47 那句读者可见的话、§79 的锁与就地保存、widget 自画 `/AP`
（§94 已抬成"打印会出白纸"）—— 这五条是要你定的；25,684,592 是谁、地址栏解不解斜杠（§97 欠）；
`pathforms` 的中文那一支；**§16 那"三处相同"欠 §84–§99 十六片**（重打包+重装要一词，push 也要一词）。

## 100. review-03 那五条改完了 —— 而第四条"待验"是真的，第六条是它带出来的：一个会把 CPU 烧满的无限循环（2026-10-22）

他一句"就按建议开始修改"。改的是 `docs/review-03.md` 末尾那张"建议的下一步顺序"的前五条。
**第六条不在单子上**：它是在给第四条写测量时掉出来的，而且比单子上任何一条都重。
六条各有一条能红的控制，**其中三条的第一版控制是假的**（详见各节）。

### 1. 缩略图推迟到侧栏打开（review-03 §2.1）

`DocumentTab::open()` 过去无条件 `thumbnailPanel->start(...)`，而 `sidebarWanted` 初值是 `false`：
打开任何文档都立刻起一个 worker 从第 0 页渲到最后一页，**为一个默认不显示的面板**，
每一次渲染都占着 Qt PDF 那把全局递归锁。

**先量**（`xyzpdf_pdfprobe`，仓库自带 fixture）：

```
                        120 px 缩略图       2500 px 整页
scan.pdf（300dpi 纯图）    17 ms（最差 27）     30 ms
huge.pdf（合成文字）        0 ms（最差 2）       3 ms
```

**输出缩到 1/15，耗时只降到 57%** —— 成本在解码不在采样。推下去：500 页扫描书 ≈ 8.5 秒锁；
文字文档上几乎免费。**它只在最慢的那类文档上发作**，是 review-02 那次"非常卡"的第二层
（那次修内存，这次是锁）。

改法：`start()` 只记下活，`showEvent()` 里才开走（`beginIfVisible()`，两边都调）。
`QTabWidget` 隐藏非当前页，所以"侧栏开着但停在书签那一页"也不渲 —— 这是对的。

**控制**：把 `!isVisible()` 拿掉 → `documenttab_smoke` 红 2 条。两条是成对写的：只断"没渲"的话，
一个**永远不渲**的实现也能过。

### 2. 文档属性对话框（review-03 §3.1，PLAN M2 最后一条）

M0–M3 里唯一一条"除了界面哪儿都写好了"的：`DocMetadata`、`QtPdfBackend::metadata()`、
`document_smoke` 的断言都在，全仓库 `grep 属性` 命中 **0**。

新 `src/ui/propertiesdialog.{h,cpp}`（54 + 110 行），只读表单、值可选中复制。三处决定：
**页面大小那一行点名第几页**（PDF 可以混尺寸，一行"页面大小"是在挑一个还不说自己挑了）；
**「打开时需要口令」不写成「已加密」**（`DocMetadata::encrypted` 填的是"这次真用了口令"，
而"这文件允许你做什么"不问 owner 口令答不出来）；**空字段写「（无）」**。
`propertiesInfo()` 与 `showProperties()` 分开，是为了测试不必去抓一个 exec() 出来的模态框。
菜单：文件 末尾，`属性(&R)…` / `P&roperties…`、Ctrl+D。

**控制两条**：`sizedPage` 写死 0 → 红 2 条；空值回退拿掉 → 红 1 条。
**而第二条的第一版是假的**：断言写成"（无）出现过"，可两个日期行本来就在印（无），
于是回退被拿掉它照样绿。改成**只留一个空字段、数个数**才红。（第一次假控制。）

### 3. Ctrl+滚轮在精密触控板上什么都不做（review-03 §2.3）

`angleDelta().y() / 120` 整除，PTP 一次送 20–40，余量又被 `accept()` 吞掉 —— 不缩放也不滚动。
新 `ZoomController::wheelSteps(int &carried, int angleDelta)`：纯函数，攒余量，换方向清零。
**控制**：退回 `angleDelta / kDetent` → `zoom_smoke` 红 4 条。
**没验的一层**：这台机器没有触控板，20–40 这个形状是读来的，不是按出来的。

### 4. 前台打印/导出期间的重入（review-03 §2.4）—— 写的是"待验"，量出来是真的

`print()` 与 `exportImages()` 在前台一页一页渲，回调里一句 `QApplication::processEvents()`。
排队进来的东西都在那一句里跑 —— 包括"关闭标签"，它把标签摘出去并 `deleteLater()`。

**量**：排一条 `closeTabAction->trigger()` 进泵，拿 `QPointer` 看。没有护栏时
**标签在泵的第二轮就没了** —— 也就是
**`deleteLater()` 在 `processEvents()` 里被调用，就是那一句 `processEvents()` 把它执行掉**，
与我第一版注释里写的"会推迟到外层循环"正好相反。那段注释当场删了。

**第一版测量是假绿**：我先用 `window.close()` 当"关闭"，它只藏窗口、根本不碰标签 ——
控制（`deleteLater()` 换成 `delete tab`）**一点变化都没有**，exit 0。
**本项目第五次把一条不会变的绿当成结论**（§86/§90/§95/§97 之后）。换成真会删标签的那条路
（`closeTabAction`，菜单与 Ctrl+W 走的）才看见真相。（第二次假控制。）

修法：`DocumentTab::ForegroundJob`（RAII 计数），`print()`/`exportImages()` 进门就拿一个；
`MainWindow::closeTab()` 与 `closeEvent()` 各拒一次并说一句话。
**为什么不能靠"框是窗口模态的"**：那只挡键盘鼠标，是界面的副作用，不是规则。
**控制**：关掉 `closeTab()` 那条 `if` → 红 2 条（而且测试两半都断：job 在时被拒、job 一撤同一条关闭就过
—— 只断前半的话，一个永远拒绝关闭的实现也能过）。

### 5. 表单字段那句提示（review-03 四.1 / §94）

打开文档时用 qpdf 问一次 `/AcroForm /Fields`（`DocumentEditor` 开了就扔，不留第二个句柄），
`MainWindow::openDocument()` 把"已打开 X"换成一句停 12 秒的话：
「这份文档含表单字段，本版不显示字段里填的内容（屏幕、导出和打印都一样）」。
**成本先量后用**：`text.pdf` 0 ms、`scan.pdf` 0 ms、**`huge.pdf`（600 页）22 ms** ——
旁边作参照的是这条路本来就要花的"读完 600 页尺寸 27 ms"（perf.md），同一量级。
**控制**：`formFieldsInSource = false` 写死 → 红 1 条。
**没做的那一半**：打印框里还没有这句话；事实已经在手上（`hasFormFields()`），接上去是一行。

### 6. 单子上没有的那一条：适应宽度在混尺寸文档上是一个**无限循环**

给第 4 条写测量的时候，`mainwindow_smoke` 挂了 300 秒**不出一个字**。第一反应是测量写坏了；
实际不是 —— 把护栏关掉（标签被删掉）它就跑完，护栏打开（**文档继续开着**）它就不回来。
也就是说：**一份开着的文档本身在无限地产生事件**。

数出来的（在 `requestVisiblePages` / `resizeEvent` / `setPageImage` 各放一个计数器）：
**20 秒 253,000 次 resizeEvent**，CPU 一整核，屏幕上什么都没动。把 resize 的几何印出来，
两态循环一眼可见：

```
vp=886x599 content=886x1980  zoom=1.449 hbar=0 range=0
vp=886x585 content=1306x2928 zoom=2.155 hbar=1 range=420
vp=886x599 ...（无限）
```

**机制**：`PageView::fittingZoom()` 过去按 `layout.currentPage()` 取 `fitTarget` —— 适应宽度
适应的是**正在读的那一页**。`tests/fixtures/text.pdf` 两页是 595x842 与 400x500
（595/400 = **1.4875**，正是上面 1306/886 与 2.155/1.449 的比）。于是：

> 适应 400 那页 → 缩放 2.16 → 595 那页变成 1306 px 放不下 → 横向滚动条出现 →
> 视口**矮了 14 px** → 当前页变成另一页 → 适应 595 那页 → 1.45 → 都放得下 →
> 滚动条消失 → 视口高回来 → 回到第一步。

反馈是从**横向滚动条**绕回来的，所以治法是让"适应什么"根本不看视口：新
`PageLayout::largestFitTarget()`（全文档最宽/最高的那个 fit target，双页/书籍模式按 spread 走一遍，
不是每页问一次 `fitTarget` —— 那在长书上是平方），`fittingZoom()` 用它。
这样每一页都放得下宽度，滚动条不会出现，循环无从起头。

**代价写在代码注释里**：一份文档里有一页特别宽时，其余页会画得比以前小。
换来的是"适应宽度"成为**文档的属性而不是滚动位置的属性"** —— 顺带也没有了"滚过一页缩放自己跳一下"。

**这条在真机上长什么样**：窗口宽度落在那个区间、文档页宽不一致，就是一核烧满、窗口发黏，
而且**永远不会自己停**。`text.pdf` 是仓库自带的 fixture，不是编出来的边角料。

**控制**：`fittingZoom()` 改回 `fitTarget(1)`（非最宽那页）→ `documenttab_smoke` 红 1 条
（`fit width fits the widest page of the document (595 pt at 2.105 is 1252.47 px in a 866 px viewport)`）。
**而这里也有一条假控制（第三次）**：我最早写的断言是"滚到第 2 页缩放不变"，
退回按当前页适应之后它**照样绿** —— 因为 `applyFitIfNeeded()` 只在 resize 时跑，
而那一刻 layout 存的当前页还没跟上（实测 `tab.currentPage()==1` 而算出来的仍是 595 那页）。
一条不会红的绿断言不如没有，删了，换成现在这条按"最宽页"写的。

**欠的**：**级联本身没有红线护着**。`layout_smoke` 钉的是 `largestFitTarget()` 的算术，
`documenttab_smoke` 钉的是"适应的是最宽页"；而真·循环要窗口 + 标签条 + 那个几何才会起，
重新引入时 `mainwindow_smoke` **不是变红，是不再结束**（实测 253,000 次 resize，300 秒被杀）。
"测试会挂"算一种失败，但不是一条好失败 —— 这是一条欠账，写在这里不许合上。

### 目录

`update_translations` → **578 → 603 条**（新增 25），numerus **48 条不变**，**0 unfinished**，
四条规则全绿。

**而它替我填错了一条，而且没有任何规则看得见**：lupdate 的 same-text heuristic 按源文完全相等
套用已有译文，于是属性框里的「主题」（PDF 的 `/Subject`）拿到了外观设置那个「主题」的 `Theme`。
这一条在目录里是**已完成、(context, source) 全匹配、四条形状规则全绿**的 —— 只有眼睛看得见
（和 §98 那 29 条括号同族）。改成 `Subject`，并把这条写进 AGENTS.md。

### 数字

产品代码：`documenttab.cpp` +52/−0、`documenttab.h` +74/−0、`mainwindow.cpp` +52/−1、
`mainwindow.h` +1/−0、`src/ui/thumbnailpanel.cpp` +29/−0、`.h` +53/−4、
`src/view/pageview.cpp` +19/−8、`.h` +4/−0、`src/view/pagelayout.cpp` +29/−0、`.h` +29/−0、
`src/view/zoomcontroller.cpp` +19/−0、`.h` +19/−0、新文件 `src/ui/propertiesdialog.h` 54 行 /
`.cpp` 110 行、`CMakeLists.txt` +4。
测试：`documenttab_smoke.cpp` +214/−0（`check(` **404→431**）、`mainwindow_smoke.cpp` +65/−0
（`check(` **144→150**）、`zoom_smoke.cpp` +33/−0（`check(` **3→15**）、`layout_smoke.cpp` +22/−0
（`check(` **15→16**，另加 4 处 `checkClose`）。
文档：新 `docs/review-03.md`（通检本身）、`AGENTS.md` +2 条。

门禁：**26 passed / 0 failed**（31 个目标，5 个 probe 不跑），目录 603/603、numerus 48、0 unfinished。
控制七条，**每条单独红过再按 sha256 还原**：缩略图推迟、属性量页、属性空值、滚轮余量、
前台护栏、表单探测、适应宽度取最宽页。其中三条的第一版控制不会红，都已换掉（见上）。
清场：`Get-Process xyzpdf` 计数 0。

**仍然欠的**（除 §99 那张单子照旧）：上面第 6 条那条级联没有红线；review-03 §2.2（缩略图内存
仍无上限、不计入预算）、§3.2（全词匹配与结果列表）、§3.3（截图入口）、§3.4（没有重新加载、
主窗口照旧锁着文件）、第一节三条结构。
**屏幕上读到的**：`-Mode menudump` 中英各一遍，新那一行在真窗口里都在、都 enabled ——
英文 `Properties…`（UIA 把 `&` 吃掉，所以源文那个 `P&roperties…` 读成这样）、中文 `属性(R)…`，
位置都在 `导出为图片` 与 `关闭标签` 之间。
**没验的**：触控板那一下没人按过；**框自己**没在真窗口里打开过（`gui-probe.ps1` 还没有它的模式，
菜单行在不等于框画得对）；打印框里还没有表单那句话。

## 101. 缩略图的第二半：只渲看得见那一段、只留那一段 —— 而"哪几行看得见"自己先算错了两次（2026-10-22）

他一句"提交后继续"。§100 把缩略图那条的前一半做了（不打开侧栏就不渲），这一片是后一半，
也就是 review-03 §2.2：**渲了就永远不放**。

### 缺陷

旧的 `ThumbnailPanel` 一趟走完整本书，每张图以 `QIcon` 留在 model 里直到文档关闭。
一张 120x170 的 ARGB 是 **81,600 字节**，600 页就是 **49 MB** —— 而且在 `RenderCache`
那 256 MB 预算**之外**。这正是 review-02 第 1 条在 `PageView::images` 上发现的同一个错，
换了一层皮：认真算过的预算约束不到真正占内存的那一份。

### 改法

按"看得见的那一段"来：视口上下各一屏的行去渲（`kRenderViewports = 1`），
上下各三屏之外的行把图丢掉（`kKeepViewports = 3`）。重算的三个时机是 `showEvent`、
`resizeEvent` 和竖滚动条的 `valueChanged`。一次只跑一批，批子跑着的时候滚动只记一个
`bandMovedDuringBatch`，等这批回来再重算 —— 否则拖一下滚动条就是一像素一个任务。

**行高必须一开始就定**：`setUniformItemSizes(true)` + 每行一个 `sizeHint`。
不是为了好看 —— "哪几行看得见"是从滚动位置算出来的，而一条图到了才长高的行会在
算这件事的同时改变答案。

### 两次算错，都是量出来的

1. **`QListView` 默认按"项"滚动，不是按像素。** 于是滚动条的 value 是一个**行号**（0–599），
   而我的算式把它当像素用：每一次算出来的都是"最前面那两行"，滚到 600 页的末尾**那里一张图都不渲**。
   加 `setVerticalScrollMode(ScrollPerPixel)`。
2. **行距不能用自己设的 `sizeHint` 推。** 实测：hint 是 214 px，行实际间隔 **203 px**，
   每行差 11 px，到第 600 行就差 **34 行** —— 滚到底的那一段算出来停在 565–572，
   而最后一行是 599。改成问视图自己：`rectForIndex(0)` 与 `rectForIndex(1)` 两个矩形，
   项是 uniform 的所以两个就够。

**两次都是同一个形状的错**：拿自己这边的数去推另一边的布局，而另一边有自己的规矩。

### 读数

`tests/fixtures/huge.pdf`，600 页，侧栏 672 px 高：

```
             旧                     新
持有的图     600（49 MB）           7（约 0.6 MB）
滚到末尾     —                      最后一页当场渲出来，持有数仍是个位数
```

### 测试与控制

`documenttab_smoke` 新 5 条断言：开 600 页的书 → 顶上那几行渲出来 → **给它 1.5 秒**
（文字页 0 ms 一张，不拦着的话六百张远在一秒内）→ 持有数 < 60 → `heldCount()` 与 model 里
真正有图的行数一致（自己报的数要和画面上的一致）→ 滚到底，**最后一行拿到图**、持有数仍 < 60。
最后那两条是"不是靠什么都不渲来达标"的那一半。

**控制**：`bandRows()` 改成返回整篇（0 .. rows-1）→ 红 2 条
（`holding 600 of 600`、`still bounded after the scroll (600)`）。还原按 sha256 核对。

### 数字

`src/ui/thumbnailpanel.cpp` +138/−27、`.h` +64/−29、`tests/documenttab_smoke.cpp` +49/−0
（`check(` **431→439**）。门禁 **26 passed / 0 failed**，目录 603/603 一字未动。
清场：`Get-Process xyzpdf` 计数 0。

**仍然欠的**：§100 那条级联仍然没有红线；review-03 §3.2（全词匹配与搜索结果列表）、
§3.3（截图入口）、§3.4（没有重新加载、主窗口照旧锁着文件）、第一节三条结构。
**没验的**：这一片只在无头的 672 px 侧栏里量过，真窗口里拖滚动条的手感没人看过 ——
`kRenderViewports = 1` 够不够是个要用眼睛判的数。

## 102. 全词匹配：规则写在"字符是什么"上，不写在"选项开没开"上 —— 否则这个勾在中文文档上等于"一个都找不到"（2026-10-22）

review-03 §3.2 的前一半（后一半"搜索结果列表"没做，见末尾）。PLAN M2 原话是
**大小写与全词选项**，而只有大小写那一个存在。

### 这一条为什么值一片

"cat 不在 catalogue 里"没什么可说的。要紧的是它对**没有词边界的语言**做了什么 ——
而那正是这个程序主要被读的语言。`QChar::isLetterOrNumber()` 对「说」「明」都答 true，
所以照搬英文那套规则的话：「说明」前后都是汉字 → 两端都不是边界 → **一个都不匹配**。
勾一下搜索框，结果从 2 条变成 0 条，而没有任何提示说为什么。

所以规则写在字符上：`TextSearch::isWordCharacter()` = 字母/数字/下划线，**减去
Han、平假名、片假名**。Hangul 留着当词字符 —— 韩文词与词之间是有空格的。
再加一句：**要不要查边界由 needle 自己的两端决定**，不是由选项单独决定。于是
`(cat)` 这种两端是标点的 needle、以及任何中文 needle，开不开这个勾都一样 ——
中文那一支因此是"这个选项对它不起作用"，不是"这个选项让它找不到"。

勾的 tooltip 把这句话说出来：**「只找独立的词；中文、日文没有词边界，这个选项对它们不起作用」**。
一个默默不起作用的勾比没有这个勾更难理解。

### 一处没让它变成口味问题的地方

英文形先写成 `Whole &words`，随后撤掉那个 `&`：同一条查找栏里 `区分大小写` 是
`Match case`、`上一个`/`下一个` 是 `Previous`/`Next`，**三条都没有助记符**。
§98 那句方法账（"把一件事归成需要他决定之前，先查这个项目有没有已经这么决定过多次"）
这次是在写下去之前用上的，不是事后。

### 测试与控制

`textsearch_smoke` +19 条（`check(` **23→42**）：cat/catalogue/bobcat 三取一、页首页尾算边界、
下划线与数字属于词、这个勾不偷偷改大小写敏感（两个方向各一条）、两端标点的 needle 照旧匹配；
中文那一组按"开与不开都是 2 条"钉，外加 `isWordCharacter()` 八个字符逐个点名
（字母/数字/下划线/空格/标点/汉字/片假名/谚文）。
`documenttab_smoke` +9 条（`check(` **439→448**）：**勾本身**那条链 —— `secon` 不勾找到 1 条、
勾上 0 条、tab 记住了问的是哪一种，以及勾一开始是清的、`setChecked(true)` 会让 tab 再问一遍、
清掉再回去。

**控制两条**，各红各的：
- 把 Han/kana 当成词字符（也就是照搬英文规则）→ `textsearch_smoke` 红 **3** 条，
  其中一条正是「开与不开都是 2 条」。
- 勾的信号永远送 `false`（勾画得出来但到不了扫描）→ `documenttab_smoke` 红 **1** 条
  （`ticking it is what asks the tab again`）。
两次还原都按 sha256 逐字节核对。

### 数字

`src/document/textsearch.cpp` +42/−1、`.h` +20/−1、`src/ui/findbar.cpp` +20/−6、`.h` +4/−2、
`documenttab.cpp` +4/−3、`.h` +3/−1、`mainwindow.cpp` +1/−1。
目录 **603 → 605**（新增 2），numerus 48 不变，0 unfinished，四条规则全绿。
门禁 **26 passed / 0 failed**。清场：`Get-Process xyzpdf` 计数 0。

**这一片没做的**：review-03 §3.2 的另一半 —— **搜索结果列表**。现在仍然只有"第 N / 共 M 个"
加上一个/下一个，一本书里两百个命中只能一个个 Enter 过去。那一条要定的是形状
（片段怎么截、放侧栏第三个页签还是查找栏下面），所以留着问他。
**没验的**：这个勾没在真窗口里按过（`gui-probe.ps1` 没有查找栏的模式）。

## 103. 截图成了一个工具，而不是识别窗口里的一个按钮（2026-10-22）

review-03 §3.3。"把这一块复制成图片"以前是能做到的 —— 选「识别」、拖一个框、
等 WinRT 把框里的字读完、在那个窗口里按「复制图片」。两处不对：
**名字不对**（想要一张图的人不会去点"识别"），**而且每拖一个框都白跑一次识别引擎**。

### 形状不是口味问题

这件事这个项目已经决定过四次。`mainwindow.cpp` 里那段注释写着：
「三个工具放一个互斥组里，而不是一个聪明的指针……一个工具多点一下，不用猜」。
所以第五个工具：`只查看 / 选择 / 识别 / 翻译 / **截图**`，`Ctrl+Shift+5`，
拖的是识别那只框，出来的是图片本身。§98 那句方法账（归成"要他定"之前先查这个项目
有没有已经这么决定过多次）这次也是在动手之前用的。

### 落点

- `PageView::Tool::Snapshot`：`wantsBox` 与指针形状各加一支（十字，和识别同一个）。
- `DocumentTab::onRegionCaptured()`：在识别那一支**之前**分流 —— 图进剪贴板，
  发 `regionCopied(w, h)`。裁剪、缩放、旋转、同步渲染那一整段原样复用，
  包括 §81 那条"先铺白纸"和"同步渲染避免 reload 撞车"的理由。
- 窗口说一句「已复制 %1 × %2 的图片」。**为什么由窗口说**：§95 量过
  `documenttab.cpp` 里 `showMessage` 是 0 处，这一族从来不自己说话。
  而这句话必须有 —— 剪贴板悄悄变了是没人信的剪贴板。

### 测试与控制

`documenttab_smoke` +8 条（`check(` **448→456**）：拖框 → `regionCopied` 报了尺寸 →
剪贴板里真有图 → 尺寸与报的一致 → 形状是框不是整页 → **没有开识别窗口** → 没选中文字。
最后那两条是这个工具存在的理由，不是附带的。
`mainwindow_smoke`：那张工具名单从**三个**扩到**五个**（`check(` **150→151**，另有两条既有断言改成五个一起数）——
它之前停在三个，所以「翻译」和「截图」先后加进产品而这条测试一个都没看见；
互斥那条现在五个一起断，并加一条"从别处改成截图工具，工具栏跟着亮"。

**控制**：把 Snapshot 那一支关掉（退回识别那条老路）→ `documenttab_smoke` 红 **4** 条，
其中就有「没有开识别窗口」。还原按 sha256 核对。

### 控制跑崩了一次，而我没查出是什么

那趟控制在印完 4 条 FAIL 之后以 **0xC0000005** 结束。两条产品形状的复现各跑三遍，
**都干净**：(a) 识别还在跑的时候直接退出进程 —— 3/3 exit 0；
(b) 识别窗口开着、扫描在跑的时候在同一个标签里开另一份文档 —— 3/3 exit 0。
所以崩的不是这两条路，而控制那条配置（截图落进识别）产品里不存在。
**没查出来就不写成解释**：记在这里，欠着。

### 数字

`src/view/pageview.cpp` +7/−6、`.h` +9/−1、`documenttab.cpp` +11/−0、`.h` +4/−0、
`mainwindow.cpp` +15/−1、`.h` +1/−0；测试 +62/−0 与 +16/−6。
目录 **605 → 608**（新增 3），numerus 48 不变，0 unfinished。
门禁 **26 passed / 0 failed**。清场：`Get-Process xyzpdf` 计数 0。

**没验的**：这个工具没在真窗口里拖过一次（`gui-probe.ps1` 没有这一族的模式）；
Ctrl+Shift+5 这个键没按过。

## 104. 重新加载（F5）—— 而"删掉再读"这条测试当场变成了"文件删不掉"的读数（2026-10-22）

review-03 §3.4 的前一半。对着一份正在被重新生成的 PDF（LaTeX、Word 导出 —— 这就是
大多数人去按 F5 的原因），以前只能关掉标签再开。

### 做了什么

`DocumentTab::reload(QString *error)`，菜单 `重新加载(&L)` / `Re&load`、**F5**。
带过去的是 页码 / 缩放 / 缩放模式 / 版式 / 旋转。
**带页码而不是滚动位置**：重新生成过的文档长度不一样，旧文件里的像素偏移在新文件里什么都不指；
页码还要按新文档的长度夹一下 —— 重新生成后变短的文件没有第 40 页。

三条拒绝：没有文档；**有前台任务**（§100 那个 `ForegroundJob` 的第一个新用户）；
有没保存的修改 —— 这一条不是直接拒，是问一句，因为那份操作列表记的是即将被替换掉的
那份文档的页，活不过这次重新加载，但要不要丢是读者的事。

还有一处顺序是故意的：**"文件还在不在"在 open() 之前问**。`open()` 失败会把标签清空 ——
这对"打开另一个文件"是对的，对"把这一份再读一遍"是错的：文件被删掉或正写到一半，
屏幕上的文档不该因此消失。

### 那条测试自己变成了一次测量

本来写的是"在读者脚下把文件删掉，然后断言 reload 拒绝且不清空标签"。
**删不掉**：PDFium 开文件不带 `FILE_SHARE_DELETE`，所以只要文档在屏幕上，
Windows 就不让任何人删它或换掉它 —— §66 在预览窗格上量到过这件事并在那里修了（读副本），
主窗口这一支照旧握着用户自己的文件。

于是 `reload()` 里"它不在了"那一支**从这里够不着**，而断言换成了钉住**锁**本身：

```cpp
check(!QFile::remove(copy), "the open document cannot be deleted out from under the reader");
```

注释里写明：哪天主窗口也改成读副本，这一行会红 —— **那就是回来把那一支真正测掉的时刻**。
一条指着未来的测试，比一条够不着的断言诚实。

### 控制

`reload()` 照常重开、但什么都不放回去 → 红 **2** 条（页码、旋转）。
**缩放那一条没红**，照实说：`ZoomController` 的 `fixedZoom` 本来就跨文档保留，
所以"缩放还在"这句话是一个事实陈述，不是一条钉子。还原按 sha256 核对。

### 数字

`documenttab.cpp` +59/−0、`.h` +16/−0、`mainwindow.cpp` +25/−1、`.h` +1/−0；
测试 `documenttab_smoke` +62/−0（`check(` **456→471**）。
目录 **608 → 618**（新增 10），numerus 48 不变，0 unfinished。
门禁 **26 passed / 0 failed**。清场：`Get-Process xyzpdf` 计数 0。

**这一片没做的，两条都要你定**：
1. **没有任何东西注意到文件变了** —— 没有 `QFileSystemWatcher`，要按 F5 才知道。
   自动重载（SumatraPDF 那样）还是只在状态栏说一句"这个文件变了"？
   自动的好处是 LaTeX 那条路不用动手，代价是读者正在读的那一页可能在手底下换掉。
2. **锁本身**。要治只剩 §66 那条路：主窗口也读 `%TEMP%` 里的副本。代价是每开一份文档复制一次
   （§66 在预览窗格上给的边界是 >256 MB 就不复制），以及"你读的不是磁盘上那一份"这句话要怎么说。

## 105. 编辑看得见了 —— 在缩略图条上：它显示的是保存之后会写出去的那些页（2026-10-22）

review-03 §1.3。七样结构编辑过去在屏幕上**一个像素都不变**：删掉第 5 页，第 5 页还在；
转了 90°，没转；只有"保存修改为…"写出新文件、在另一个标签里打开，才看得到结果。

他在三条路里选了最便宜的那条：**主视图照旧显示文件本身，缩略图条显示保存之后会写出去的那些页**
—— 那个顺序、那个朝向。

### 改法

- `DocumentEditor::pageOrder()` 本来就有（保存会按这个顺序写），旋转却不在里面：
  旋转记成"操作 + 原页码"。于是加两个只读查询：`pendingQuarterTurns(page)`（0–3，
  对那一页把所有待写的旋转加起来）和 `importPathFor(source)`（合并进来的页来自哪个文件）。
- `ThumbnailPanel` 从"一页一行"换成"**一行一个描述**"：`Row{sourcePage, rotation, label, toolTip}`。
  `start()` 照旧给一页一行；新 `setRows()` 换掉描述、丢掉旧图、重渲那一段。
  渲染时把 rotation 交给 `RenderOptions::rotationDegrees`，尺寸按转过的形状算 ——
  转了四分之一的竖页画成横的，不是挤进原来那个竖框。
- 行就是"文件里的某一页"，不是"第几行"：**点第 2 行跳到它显示的那一页**（删掉第 2 页之后，
  第 2 行是原第 3 页）；反过来主视图翻到某页，高亮的是显示那一页的行，那一页若正被删掉就不高亮。
- 合并进来的页：这份文档画不出别的文件的页，所以那一行不放空图，标签直接写
  「3（合并）」、tooltip「来自 X.pdf 的第 N 页」。
- `DocumentTab::refreshThumbnailRows()` 接在 `editStateChanged` 上 —— **一根线管八个发信号的地方**，
  撤销与重做都在里面。
- **缩略图条变成预览时要说出来**：那个页签的名字变成「缩略图（修改预览）」，tooltip
  「这里显示的是保存之后的页面顺序和朝向；正在读的文件本身没有变」。
  否则十页的文档旁边九张缩略图，看着就是坏的。

### 顺带修掉的一个潜在错位

`setRows()` 会在一批渲染还在路上时改变"第几行是哪一页"，而旧代码里迟到的图只按行号范围核一下
就放进去 —— 也就是说**旧行的图可能落进新行**。现在每张图带着发出它的那一批的取消令牌，
到了 UI 线程再核一次；"这一批完了"的那条消息也核令牌，否则一条过期的"完了"会把正在跑的新一批
标成已结束，于是同时跑出两批。

### 测试

`documenttab_smoke` +17 条（`check(` **471→488**），**走读者用的那两个框**（编辑列表属于标签，
标签只让这样改）：开 40 页的书 → 删第 2 页 → 条上少一行、第 2 行是原第 3 页、页签名字变了、
**而主视图仍是 40 页** → 右转第 1 页 → 第 1 行 rotation=90、**它的图是横的** →
点第 2 行跳到原第 3 页 → 撤销两次 → 条与页签都回到原样。
框是用一个限时的计时器替读者填的：先布好再调菜单命令（命令停在 exec() 里），
框没出现只赔几秒，出现了填不进就拒掉 —— exec() 一定会回来。

**控制两条，各红各的**：
- 不接那根 `editStateChanged` → 红 **7** 条。
- 只让缩略图渲染忽略 rotation → 红 **1** 条，正是"它的图是横的"那一条，而 `rotation == 90`
  那条照旧绿 —— 证明那条看图的断言测的是**画出来的东西**，不是账本。
两次还原按 sha256 核对。

### 数字

`src/edit/documenteditor.cpp` +22/−0、`.h` +11/−0、`src/ui/thumbnailpanel.cpp` +91/−29、
`.h` +35/−1、`documenttab.cpp` +66/−0、`.h` +12/−0；测试 +127/−0。
目录 **618 → 623**（新增 5），numerus 48 不变，0 unfinished。
门禁 **26 passed / 0 failed**。清场：`Get-Process xyzpdf` 计数 0。

**这一片没做/没测的**：
- **合并与重排没单独测**。它们走的是同一条 `pageOrder()` 路，删页那条已经证明"条跟着 pageOrder 走"，
  但合并那种"没有图的行"、以及重排的行序，都没有自己的断言（合并要过一个挑文件的框）。
- **水印不反映**：它改的是页面内容流，要看得见得让 qpdf 先写出来再渲，这条路不碰。拆分/加密/书签
  本来就不改页，条上也就没有它们。
- 主视图仍然是文件本身 —— 这是他选的那条路，不是遗漏。
- 没在真窗口里看过那张横过来的缩略图。

## 106. 重打包并重装：§16 那笔欠了二十二片的账对上（§84–§105）—— 而装上的那份顺手指出一个按钮该灰没灰（2026-10-22）

他说"先还 §16 的账，重打包重装"。记账的一片：**产品代码一字未动**，把 §84–§105 送进装上的那一份。
上一次对上是 §83。

### 顺序

1. 门禁 **26/0**（§105 那一遍，HEAD `4deeec8`，树干净），目录 623 / numerus 48。
2. **打包之前**先记构建树的七颗（`threeway.py snapshot`），每行带"在不在"与字节数（§83 那条）。
3. `packaging/build-package.ps1 -Sign -Cert 221C6B38…0A83 -Install`。
4. `packaging/verify-package.ps1 -Package …`。
5. 四列比对：打包前的构建树 / 打包后的构建树 / 包内（把 .msix 当 zip 解开）/ `WindowsApps\…\bin`。
6. 装上的那份起两次窗口读菜单。

**打包 shell 的环境**（§97 那条：决定 dx 两颗来不来的就是这两个变量）：`WindowsSdkDir` **空**，
PATH 里含 `Windows Kits` 的只有 `E:\Windows Kits\10\Windows Performance Toolkit`，**不含**
SDK 的 `bin\<ver>\x64`。

### 读数

- 包：`XYZPDF_0.1.0.0_x64.msix` **18,387,067 字节（17.5 MB）**，解开 **40.6 MB / 67 个文件**；
  strip 那一步照旧 `threw away 34 file(s), 29.3 MB`；`verify-package` exit 0、
  **`payload is the same 67 file(s) as payload-0f744cd07d73-20261008`** —— 与 §84 那趟瘦包逐文件同一张清单，
  dx 两颗不在。
- 装上的：`allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4`。`-Install` 先卸旧的一份，**设置容器跟着走**
  （语言、最近文件、上次目录都回到默认 —— §73/§83 记过，第三次）。
- **四列全等**：

```
xyzpdf.exe           dc2f8ef75575… / 1,403,904
xyzpdfpreview.dll    862d67caa3c1… /   135,680
xyzpdfshell.dll      5bdb230257a7… /    37,888
xyzpdfthumbnail.dll  f229b6e98592… /    92,160
qpdf30.dll           5ce2fc864988… / 2,397,696
jpeg62.dll           feb06772a974… /   722,944
z.dll                5aef909e51b8… /    91,648
```

  "打包前 = 打包后"那一列说明**打包这一步没有重链 exe**（§82 那条：重链一次哈希就换）；
  `xyzpdfshell.dll`、`qpdf30.dll`、`jpeg62.dll`、`z.dll` 四颗与 §83 记的逐字节相同 —— 那几片没碰它们，
  应该如此。`xyzpdf.exe` 比 §83 那颗大 **61,952 字节**（1,341,952 → 1,403,904），是 §84–§105 真加的代码。

### 仪器自己的控制

比对脚本把"装上的"那一列指到一个不存在的包目录 → 七行都印 `installed=MISSING same=False`、**exit 1**。
§83 那次的假绿（三个都不存在的路径印出 `same=True`）在这根仪器上被量过不会再发生，而不是被假定。

### 屏幕上

`-Mode menudump -Installed`，中文界面（设置已被重装清空，跟随系统）：
- 文件 菜单：`重新加载(L)`（§104）与 `属性(R)…`（§100）都在，**都灰** —— 包里那份起来时不带文档，灰是对的。
- 查看 菜单：`只查看 / 选择 / 识别 / 翻译 / 截图` 五个都在（`截图` 是 §103）。

### 装上的那份顺手指出的一条缺陷（钉住，没修）

查看 菜单里另外四个工具都灰，**`翻译` 是亮的** —— 没有文档时也能点。
`MainWindow::updateForCurrentTab()` 那张"有文档才亮"的单子里有 `viewToolAction`、`selectToolAction`、
`recognizeToolAction`（§100 加了 `snapshotToolAction`），**没有 `translateToolAction`**。
它是 §100 之前就在的，只是没人在一个空窗口里看过这排按钮。

**这一片故意不修**：改一行产品代码就会重链 exe，刚对上的"三处相同"当场又欠一片。下一片修，
修完 §16 欠一片 —— 这样账是清的。

### 数字

产品代码 **+0/−0**；文档只有本节。清场：`Get-Process xyzpdf` 计数 0。
**§16 现在对到 §105**。

**仍然欠的**：`翻译` 工具没文档时不灰（上面那条）；§100 那条级联没有红线；§103 那次控制跑的
0xC0000005；review-03 的 搜索结果列表 / 文件监视 / 文件锁 / 第一节两条结构；
以及更早那张单子（`/Brepro`、§55 XFA、§47 那句话、§79 就地保存、widget 自画 `/AP`、25,684,592 是谁）。

## 107. 「翻译」在空窗口里该灰没灰 —— §106 钉住的那一条，修掉，§16 因此又欠一片（2026-10-22）

他一句"可以"。§106 在装上的那份里读到：没有文档时，查看菜单的五个工具里四个灰、`翻译` 亮着。

**机制**：`MainWindow::updateForCurrentTab()` 那张"有文档才亮"的单子写着
`viewToolAction, selectToolAction, recognizeToolAction`（§100 加了 `snapshotToolAction`），
**没有 `translateToolAction`**。它是第四个工具加进来时（`d4772ba`）漏掉的，没有任何注释说它是故意的
—— 而翻译工具的两种拖法（划字、框图）都作用在页面上，没有文档时没有任何事可做。

**改法**：单子里加上那一个名字。产品代码 **+1/−1**。

**测试**：`mainwindow_smoke` 在窗口刚起来、什么都没开的那一刻，把五个工具按 objectName 逐个找出来并断言
都是灰的（`check(` **151→153**；其中"存在"那五条与"灰"那五条在同一个循环里，计数器按源码行数数）。
**控制**：把 `translateToolAction` 从单子里拿回去 → 红 **1** 条，正是
`and with no document open, translateToolAction is grey`。还原按 sha256 核对。

**为什么上一片不修**：§106 是记账的一片，改一行就要重链 exe，刚对上的三处相同当场就作废。
分成两片，账是清的：§106 对到 §105，**§16 现在欠 §107 这一片**。

门禁 **26 passed / 0 failed**，目录 623/623 一字未动（没有新的用户可见字符串）。清场：`Get-Process xyzpdf` 计数 0。

## 108. 重打包并重装：§107 送进装上的那一份，§16 对到 §107（2026-10-22）

他一句"可以"。和 §106 同一套顺序、同一个打包环境（`WindowsSdkDir` 空，PATH 里含 `Windows Kits` 的只有
Performance Toolkit），**产品代码一字未动**。

- 打包前快照：只有 `xyzpdf.exe` 变了 —— `023a3c0e33ca…` / **1,403,904**，与 §106 那颗**同尺寸不同字节**
  （§107 的一行改动加一次重链；§82 那条照旧：哈希变了不说明改了多少，尺寸相同也不说明没改）。
  其余六颗与 §106 逐字节相同。
- 包：18,387,077 字节（17.5 MB），解开 40.6 MB / 67 个文件，`verify-package` exit 0，
  **`payload is the same 67 file(s) as payload-dc2f8ef75575-20261022`**（§106 那张清单）。
- 装上的：`allgoodhi.XYZPDF_0.1.0.0_x64__edpkq8hzfe5n4`，设置容器照旧跟着重装清空。
- **四列全等**（打包前构建树 / 打包后构建树 / 包内 / `WindowsApps\…\bin`），七颗都是 `same=True`，
  比对脚本 exit 0（它在 §106 被量过：指错目录时印 `MISSING same=False` 并 exit 1）。

**屏幕上，这一趟真正要看的那一行**：`-Mode menudump -Installed`，查看 菜单 ——

```
§106（修之前）  只查看 False · 选择 False · 识别 False · 翻译 True  · 截图 False
§108（修之后）  只查看 False · 选择 False · 识别 False · 翻译 False · 截图 False
```

同一根仪器、同一种启动（包里那份、不带文档），在装上的字节上读到的前后对照。

产品代码 +0/−0。清场：`Get-Process xyzpdf` 计数 0。**§16 现在对到 §107。**
