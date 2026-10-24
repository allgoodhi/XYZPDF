# 第七批：验证数字签名，以及签名文档上屏幕（2026-10-24）

第六批（H4、H5）已合进 main，见 `docs/store-submission.md` §122–§123。现在本程序认得出签名、保存时护得住签名覆盖的字节，
但状态栏明说"本版不验证签名本身"。这一批把验证做出来，并把签名文档的保存与限制搬上屏幕。两件**同时发**：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| I1 `signature-verify` | 用 Windows 自己的密码学接口验证每个签名：内容有没有被改、证书链受不受信任、签名之后有没有新修订；侧栏/属性框列出来 | 新 `src/document/signatureverify.*`、侧栏、属性框、状态栏、设置（吊销检查开关） |
| I2 `gui-probe-signed` | 签名文档上屏幕：只改字段就地保存后前缀不变、删除框那句、P=1 点不进、P=2 结构框灰 | 只改 `tools/gui-probe.ps1` 与 `tools/pdfprobe.cpp` 的样本子命令 |

**基线**：门禁 **27 passed / 0 failed**，目录 **701 条**，numerus **57**。

---

## 每个分支都要先做的：环境准备与规矩

与 `docs/handoff-04.md` 的同名一节**完全相同**（两个 junction；从零构建；不删 `CMakeCache.txt`；控制跑按 sha256 还原、
控制必须能让新断言红；门禁全绿；`tr()` / `%n` / `QT_TRANSLATE_NOOP`；目录写成已完成并逐条核；写用户文件只有「保存」一条路；
不改日志与规矩文档；不打包、不安装、不 push、不合并；并行分支 gui-probe 传自己的 `-Exe`；锚点；署名行；最终报告）。
另读 `AGENTS.md` 里"已签名的文档"那一条（§122）。

---

## I1. 验证数字签名　分支：`signature-verify`

**设计已经定了，照做；量到不行再停下来报告**
1. **用 Windows 的密码学接口，不引第三方库**：PKCS#7 / CMS 分离签名（`adbe.pkcs7.detached`、`ETSI.CAdES.detached`；旧的
   `adbe.pkcs7.sha1` 也认）用 `CryptMsgOpenToDecode` + `CryptMsgUpdate`（把 `/ByteRange` 两段喂进去）+
   `CryptMsgControl(CMSG_CTRL_VERIFY_SIGNATURE_EX)` 验；证书链用 `CertGetCertificateChain` + `CertVerifyCertificateChainPolicy`
   （`CERT_CHAIN_POLICY_BASE`），信任来源是 **Windows 自己的证书存储**。`adbe.x509.rsa_sha1` 等少见格式答"不支持的签名格式"，不要硬猜。
   签名的字节用 PDFium 的 `FPDFSignatureObj_GetContents` / `GetByteRange` / `GetSubFilter` / `GetTime` / `GetReason` 取，
   被覆盖的字节从文件读（用本程序自己的句柄，`FILE_SHARE_*` 照 F7 那样）。
2. **每个签名一个结论**，四选一并能并存说明：**有效**（内容没改 + 证书链受信任）、**内容被改过**（摘要对不上 —— 最重的一档）、
   **证书不受信任或已过期**（内容没改，但链建不起来/根不在信任列表/证书过期）、**无法验证**（格式不支持、数据坏了）。另外两条事实单独报：
   **签名之后又有修订**（`ByteRange` 的末尾不是文件末尾 —— 列出之后有几次增量更新；不去判断那些改动是否被 DocMDP 允许，那是另一件事，
   报告里写明没做）；**签名覆盖不到整个修订**（`ByteRange` 有洞以外的缺口）。签名人（证书主题的 CN）、签名时间（有 RFC 3161 时间戳就用
   时间戳并注明"时间戳"，否则用签名属性里的时间并注明"签名者自己声明的时间"）、原因、地点都列出来。
3. **吊销检查默认不联网**：`CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY`，结论里注明"未联网检查吊销状态"。设置里加一个开关「联网检查证书是否被吊销」
   默认关；打开时照 F1 翻译那套知情同意（第一次联网前问一次、说清会连到证书里写的 CRL/OCSP 地址、可撤销），并给出隐私政策要写的那几句。
4. **界面**：侧栏加「签名」页签（与书签/缩略图/搜索并列；没有签名的文档不显示这个页签），每个签名一行：图标 + 结论 + 签名人 + 时间，展开看细节；
   点一行跳到签名字段所在的页。属性框那一行从"N 个"改成"N 个，M 个有效"。状态栏开文档那句从"本版不验证签名本身"改成结论摘要
   （例如"带着 2 个数字签名：1 个有效，1 个证书不受信任"）；有"内容被改过"的，那一句排在最前面。
5. **代码放哪**：只编进应用（不进两颗壳 DLL —— AGENTS 里"表单代码分两半"那条的道理一样）；验证在工作线程上做，不卡界面；
   `drainRenders()` 与析构要等到它（AGENTS："新服务要被 `drainRenders()` 与析构等到"）。
6. **测试不许碰这台机器的证书存储**：测试样本里的签名用**内存里**的自签名证书现签（CNG 的临时密钥 + `CertCreateSelfSignCertificate`
   + `CryptSignMessage` 分离签名，签 `/ByteRange` 两段，填进 `/Contents`），任何时候都不写 `CurrentUser\My`、`Root` 等任何持久存储。
   "受信任"那一档用**自定义链引擎**（`CERT_CHAIN_ENGINE_CONFIG` 的 `hExclusiveRoot` 指向一个内存存储，里面放那张自签根）在测试里注入；
   产品用默认引擎。四种结论各一份样本：有效；改了覆盖范围里一个字节；不受信任的根；签完再追加一次增量更新。另加一份 `adbe.x509.rsa_sha1`
   形状的（答不支持）。样本形状从 PDF 32000-1 §12.8 来。
7. 新增用户可见文字照规矩：`tr()`、计数走 `%n` 加探针、目录写成已完成。

**测试落点**：新测试目标 `signatureverify_smoke`（纯验证逻辑，四种结论 + 不支持 + 时间戳/声明时间两种标注）；`documenttab_smoke` 在
`    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----` 之前测侧栏页签出现与否、行内容、点击跳页、
工作线程被 `drainRenders()` 等到；`mainwindow_smoke` 在 `    // ---- a second document joins the first ----` 之前测状态栏摘要与"被改过"排最前。
每条配控制跑（例如把摘要比较钉成"相等" → "内容被改过"那条必须红）。

**报告还要有**：这台机器上 Windows 信任列表里有没有根会让测试意外"受信任"（应该没有，因为样本根是现造的）；联网开关打开时实际连了哪些地址（只在
手动验证里开一次，测试一律不联网）。

---

## I2. 签名文档上屏幕　分支：`gui-probe-signed`

**背景**：H4 的增量保存、"签名会失效"那句、DocMDP 的门，全在无头测试里；§123 主会话只在装上的那份上读了状态栏与删除框两句话。

**要做**（只改 `xyzpdf/tools/gui-probe.ps1`；样本要扩展时只动 `tools/pdfprobe.cpp`，加一个**只新建**的 `--write-signed-sample <path> [--docmdp 1|2]`：
带一个文本字段 + 一个已签名的 `/Sig` 字段，`/ByteRange` 按装好的字节算，不必是密码学签名）
1. `-Mode signedsave`：护栏复制样本 → 点文本字段打字 → Ctrl+S → 读回字段值是打的字，**并且副本的前 N 字节与保存前逐字节相同**（N = 保存前长度）、
   `/ByteRange` 两段 SHA-256 前后相同（在 PowerShell 里算）。`-Control`：先做一次结构修改（旋转一页）再存 → 前缀不再相同 → exit 1。
2. `-Mode signedlock`：P=1 样本：点文本字段 → 光标不进（几何判据，同 `formfill` 的 caret 量法）、删除框确定按钮灰；P=2 样本：字段能填、删除框确定按钮灰、
   框里有那句"只允许填写表单"。`-Control`：在无签名样本上期望"灰" → exit 1。
3. 读状态栏前指针要挪开（`-OpenViaPicker` 那条路 §122 已经自带；开发版启动那条也有 `ParkCursorAway`）。
4. 中英各一遍；新模式进 `$script:saveModes`；早退走 `Bail`；`.ps1` 非 ASCII 0；每次传
   `-Exe H:\test\XYZPDF-wt\gui-probe-signed\xyzpdf\build-local-release\xyzpdf.exe`。
5. 屏幕上看见的产品缺陷单列。

---

## 合并方式（主会话做）

I2 先合，I1 后合（I1 改了状态栏那句话，I2 读的是现在的说法 —— 合并时主会话把 I2 的匹配串按 I1 的新说法补上）。合并后重打包重装，
在装上的那份上跑 I2 的两个模式，再用 I1 的四种样本各开一次看侧栏。
