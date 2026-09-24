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
