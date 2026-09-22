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

## 2. 提交前必须换掉的：发布者身份

清单写的是 `Publisher="CN=allgoodhi"`。这是自签占位，只够在本机装自己玩的包。

商店要求发布者与 Partner Center 账户的证书一致，而 `Identity/@Name` 与 `@Publisher`
**上架之后不可改** —— 改了就不是更新，是一个新应用，评价、下载量、评分全部留在旧的里面。
所以这两个值要在第一次提交之前一次定死，别等到那时才发现账户里的 CN 写法不同。

`-Publisher` 参数就是为这一步留的：打包时盖进去，仓库里的清单永远保持占位。

## 3. 提交前必须补上的：文件关联

### 3.1 现在清单里什么关联都没有

`AppxManifest.xml` 没有 `windows.fileTypeAssociation`，也没有 `IExplorerCommand`。
双击 `.pdf` 目前靠的是 `src/windows/shellintegration.cpp` 写进 HKCU 的那套注册表项。

### 3.2 而装上包会把那套注册表项删掉

`mainwindow.cpp:389` 的逻辑是：检测到本程序自己的包已安装，就调用
`unregisterVerbs()` 清掉注册表里的那份 —— 因为包里那份和注册表那份会并排出现两个同名
菜单项，用户会当成 bug 报上来，所以让包赢。

**问题是那个包今天并不声明任何关联或菜单。**于是"装上包"的实际效果是：用户的右键菜单
没了，什么也没换回来。这段逻辑本身没错，错的是它假设的那一半还不存在。

所以提交前二选一，且要先选：

- 在清单里补 `windows.fileTypeAssociation`（`.pdf`，带 `<uap:Logo>`），让包真的带上关联。
  关联项要同时给 `DisplayName` 与 `InfoTip`，否则"默认应用"列表里显示的是内部名而不是
  人读的名字。
- 或者把 §3.2 那段"包优先"的判断从"包装着"改成"包声明了关联"。

前一条是正路，因为它同时解决"默认应用"注册 —— 商店包只有声明了关联，
设置 → 应用 → 默认应用 里才会出现这个程序。

## 4. 提审时要填的说明，不是代码但会拒审

- **`runFullTrust` 是受限能力**。提交时要写为什么一个 PDF 阅读器需要完整桌面权限。
  诚实的答案就够：Qt 的桌面窗口与 PDFium 在同一进程内渲染，且要读写用户选的任意路径。
- **隐私声明必须提**。划词翻译会把选中的文本发到外部服务（`src/translate/translator.cpp`），
  这是"数据离开设备"，商店的数据收集表必须如实勾选并给出隐私政策 URL。
  区域截图、OCR 走系统服务，不出机器；导出、打印、阅读都不联网。
  第三方声明页已经在"关于"里（`src/ui/thirdpartydialog.cpp`）。
- **年龄分级/内容声明**：无用户生成内容、无网络交互入口，填最低档。

## 5. M3 还欠的，顺序上卡在包后面

预览窗格 `IPreviewHandler` 与缩略图 `IThumbnailProvider` 一行都还没有。
XYZip 有完整参考实现（`H:\test\XYZip\xyzip\src\shell\xyzippreview.cpp`，1865 行，
含 delay-load 钩子与 DllHost 的坑位注释），**但它只靠包身份注册** ——
`com:SurrogateServer` 写在 AppxManifest 里，没有任何注册表回退路径。

所以顺序是死的：先把包做出来并装上（§3、§6），预览处理器才有地方挂。
这也意味着 §3.2 那个决定要一起做，别分两次踩。

## 6. 本机没有验证的

说清楚，免得下文被当成已确认的事实：

- **签名**：没有代码签名证书，`-Sign` 这条路一次都没跑过。
- **安装**：`Add-AppxPackage` 会改机器（注册包身份、改默认应用候选），没有跑过。
  要跑需要明确同意，而且未签名包只在开发模式机器上装得进去。
- **商店认证测试**：`appcertui.exe` 在，但没跑过 —— 它要求包已安装。
- **双击 `.pdf` 是否真走包里的关联**：未验证，依赖 §3.2 补完。
- **包里的程序能否启动**：没有观察过。布局本身查过 —— `bin\qt.conf` 写的是
  `Prefix = ..`，于是插件解析到包根的 `plugins\platforms\qwindows.dll`，那里确实有它。
  但"路径推得通"和"跑起来看得见窗口"不是一回事，装包之前这条只能算半个。

## 7. 命令

```powershell
# 出包（不改机器）
python tools/makeappicon.py
python tools/build_all_tests.py
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-package.ps1

# 签名并安装（改机器，需明确同意；开发模式下可省 -Sign）
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/build-package.ps1 `
    -Publisher "CN=<商店账户的发布者>" -Sign -Cert <证书指纹> -Install

# 认证测试（装完之后）
& 'E:\Windows Kits\10\App Certification Kit\appcertui.exe'
```
