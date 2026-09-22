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
  本机现在装着的是剥完负载之后重打的那一份，`InstallLocation` 下 `resources.pri` 已经在（§9.7）。
  还欠三眼：开始菜单/设置里的图标、没有软件 OpenGL 还起不起得来、预览窗格与缩略图照旧。

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
（有真显卡驱动、真用户点开的这台机器上，缩略图与预览窗格要不要软件 OpenGL）只有眼睛能回答，
见 §9.7。

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
  不进安装目录的）。脚本那边 §9.2 与 §9.5 都已判定通过，还欠的是三眼：开始菜单/设置里图标
  在不在、双击打开正不正常（软件 OpenGL 没了）、预览窗格与缩略图在剥完之后还出不出来。
- 商店那一边还要填的元数据（隐私声明、截图、年龄分级、`Product identity` 里的实际值）在 §4。
