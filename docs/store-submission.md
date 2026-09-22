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
python tools/build_all_tests.py                                      # 23 项目标，21 通过
powershell -File packaging/build-package.ps1                         # 出包
```

结果：`build-local-release/package/XYZPDF_0.1.0.0_x64.msix`，26.5 MB，
`makeappx pack` 报 `Package creation succeeded`。

打包脚本不信任自己盖的版本号，而是把清单从 `.msix`（就是个 zip）里**读回来**核对：
`the package's own manifest says version 0.1.0.0`。清单声明的 5 个文件（exe 与四张图）
也逐个确认在包里 —— `makeappx` 遇到缺文件只报一句布局不对，看不出缺哪个。

工具的位置值得记一下：`makeappx.exe` 与 `signtool.exe` 在
`E:\Windows Kits\10\App Certification Kit\`，**不在 PATH 上**，脚本按名字找它。
同一目录里还有 `appcertui.exe`，也就是本机可以跑商店认证测试（见 §4）。

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
| `HKCU\Software\Classes\xyzpdf\shell\0001_open`，即右键菜单里那条"用 XYZPDF 打开"（`Position=Top`、`AppliesTo` 限定 `*.pdf`） | **丢了**：包里没有替代 |

丢的那一项需要一个 `IExplorerCommand` 处理器作为 DLL 打进包（XYZip 就是这么做的：
`src/shell/xyzipshell.cpp`，632 行，清单里用 `com:SurrogateServer` 注册，没有注册表回退）。
在那之前，"装了包"对用户的意思就是：**打开方式里还在、图标还在、能设成默认了，
但右键少了一项**。这句要说在前面，别等用户报上来。

清单里那段注释说的是同一件事，两边不要只改一处。

## 4. 提审时要填的说明，不是代码但会拒审

- **`runFullTrust` 是受限能力**。提交时要写为什么一个 PDF 阅读器需要完整桌面权限。
  诚实的答案就够：Qt 的桌面窗口与 PDFium 在同一进程内渲染，且要读写用户选的任意路径。
- **隐私声明必须提**。划词翻译会把选中的文本发到外部服务（`src/translate/translator.cpp`），
  这是"数据离开设备"，商店的数据收集表必须如实勾选并给出隐私政策 URL。
  区域截图、OCR 走系统服务，不出机器；导出、打印、阅读都不联网。
  第三方声明页已经在"关于"里（`src/ui/thirdpartydialog.cpp`）。
- **年龄分级/内容声明**：无用户生成内容、无网络交互入口，填最低档。

## 5. 预览处理器：那道关验过了，答案是可以

预览窗格 `IPreviewHandler` 与缩略图 `IThumbnailProvider` 一行真正的实现都还没有。
XYZip 有完整参考（`H:\test\XYZip\xyzip\src\shell\xyzippreview.cpp`，1865 行，含
delay-load 钩子与 DllHost 的坑位注释），**它只靠包身份注册** —— `com:SurrogateServer`
写在清单里，没有注册表回退路径。所以先前卡着的未知是：**松散注册的包，外壳收不收这个
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

探针回答的是路由问题，不是功能；真处理器落地时替换它，清单里的 CLSID 与 `Path` 两处一起改。

**当前机器状态**：探针仍挂在 `.pdf` 上，而 `.pdf` 的默认程序也已经改成 XYZPDF，所以现在
预览 PDF 看到的是那块探针色块。撤掉：设置里把 `.pdf` 默认改回 WPS，或
`Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage`。

## 6. 本机没有验证的

说清楚，免得下文被当成已确认的事实。今天从"未验证"挪走的三条在 §1.1。

- **签名**：没有代码签名证书，`-Sign` 这条路一次都没跑过；从 `.msix` 安装**必须**它
  （`0x800B0100` / `0x80073D2C`，见 §1.1 的表）。
- **双击 `.pdf` 会开在谁手里**：本机默认已经改成 XYZPDF（为了验预览那条），但"双击
  真的开起来"没有观察过。要改默认只能由人在设置里点 —— Windows 用只有它自己能算出的
  哈希保护默认值，程序自己去写就是恶意软件的做法（`shellintegration.cpp:284` 那段
  注释说的就是这件事）。
- **商店认证测试**：`appcertui.exe` 在，包也注册了，但没跑过。

已经验过、别再当未知的：包内 COM 服务器在松散注册下会被路由（§5）；包能启动
（§1.1）；`.pdf` 关联登记进了外壳（§1.1）。

## 7. 命令

```powershell
# 出包（不改机器）
python tools/makeappicon.py
python tools/build_all_tests.py
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-package.ps1

# 装上暂存布局（改机器：注册包身份，并清掉我们自己写的 HKCU 项）
Add-AppxPackage -Register build-local-release\package\app\AppxManifest.xml
# 装完用真实 id 启动，id 从 Get-StartApps 读，别手抄
Get-StartApps | Where-Object Name -match XYZPDF

# 撤掉
Get-AppxPackage -Name allgoodhi.XYZPDF | Remove-AppxPackage

# 认证测试（装完之后）
& 'E:\Windows Kits\10\App Certification Kit\appcertui.exe'
```
