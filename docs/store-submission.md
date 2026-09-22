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

结果：`build-local-release/package/XYZPDF_0.1.0.0_x64.msix`，26.6 MB，
`makeappx pack` 报 `Package creation succeeded`。

打包脚本不信任自己盖的版本号，而是把清单从 `.msix`（就是个 zip）里**读回来**核对：
`the package's own manifest says version 0.1.0.0`。清单声明的 8 个文件（exe、四张图、
三个外壳处理器的 DLL）也逐个确认在包里 —— `makeappx` 遇到缺文件只报一句布局不对，
看不出缺哪个。DLL 也要算进来：清单里写着、包里却没有，是一个谁都不会注意到的出包
结果，预览窗格只会说"没有预览"，看起来像处理器自己的 bug。这一条在 2026-09-22 加上
三个处理器之前并不成立 —— 脚本只认 `.exe/.png/.ico/.jpg`，那天它报的是 5 个。

工具的位置值得记一下：`makeappx.exe` 与 `signtool.exe` 在
`E:\Windows Kits\10\App Certification Kit\`，**不在 PATH 上**，脚本按名字找它。
同一目录里还有 `appcert.exe` 与 `appcertui.exe`，即商店认证测试的工具 —— 工具在，
但跑不了那么便宜：它要提权，细节见 §6。

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

## 2. 提交前必须换掉的：发布者身份

清单写的是 `Publisher="CN=allgoodhi"`。这是自签占位，只够在本机装自己玩的包。

商店要求发布者与 Partner Center 账户的证书一致，而 `Identity/@Name` 与 `@Publisher`
**上架之后不可改** —— 改了就不是更新，是一个新应用，评价、下载量、评分全部留在旧的里面。
所以这两个值要在第一次提交之前一次定死，别等到那时才发现账户里的 CN 写法不同。

`-Publisher` 参数就是为这一步留的：打包时盖进去，仓库里的清单永远保持占位。

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

- **签名**：没有代码签名证书，`-Sign` 这条路一次都没跑过；从 `.msix` 安装**必须**它
  （`0x800B0100` / `0x80073D2C`，见 §1.1 的表）。
- **双击 `.pdf` 会开在谁手里**：本机默认已经改成 XYZPDF（为了验预览那条），但"双击
  真的开起来"没有观察过。要改默认只能由人在设置里点 —— Windows 用只有它自己能算出的
  哈希保护默认值，程序自己去写就是恶意软件的做法（`shellintegration.cpp:284` 那段
  注释说的就是这件事）。
- **商店认证测试（WACK）**：工具在（`appcert.exe` 与 `appcertui.exe`，就是 §1 说的那个
  不在 PATH 上的目录），包也注册了，但一次都没跑完。拦路的是权限不是配置：非提权的
  PowerShell 里连 `appcert.exe /?` 都启动不了，`Start-Process` 回"此操作需要提升"（Git
  Bash 更干脆，一句 `Permission denied` —— 它的清单里写着 `requireAdministrator`）。
  既然参数表只能在管理员 shell 里读到，这里就不抄一份网上来的命令行充作已验证；要跑的
  人请先在提权窗口里让 `appcert.exe` 自己吐一次用法，按这一份的参数来。
- **`makeappx` 没有静态校验可当替身**：本想拿 `makeappx validate` 顶一下 WACK，这个版本
  （10.0.26100.8249）没有 `validate` 这个子命令，只回一屏 usage。所以包结构的校验目前
  就到 `makeappx pack` 自己那一步，加上打包脚本从 `.msix` 里把清单读回来核对（§1）。

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

# 撤掉
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage

# 认证测试（装完之后）。appcert 只能提权跑，见 §6 —— 从当前窗口拉起：
Start-Process 'E:\Windows Kits\10\App Certification Kit\appcertui.exe' -Verb RunAs
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

