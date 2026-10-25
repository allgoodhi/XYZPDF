# 第八批：签名验证没验到的那几层（2026-10-25）

第七批（I1 验证签名、I2 签名文档上屏幕）已合进 main，见 `docs/store-submission.md` §126–§128。合并后主会话又修了四处
（英文焊句、摘要写死全角标点、结论盖掉认证那句、结论盖掉"已保存"），屏幕上 `signedsave` / `signedlock` 在装上的那份上全过，
I1 的六种样本经 picker 打开、状态栏结论都对。**还没验到的**就是这一批：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| J1 `gui-probe-signatures` | 侧栏「签名」页上屏幕；签名那一族在英文界面里走一遍 | 只改 `tools/gui-probe.ps1`（样本靠 `XYZPDF_SIGNATURE_SAMPLE_DIR`，不改产品） |
| J2 `signature-timestamp-revocation` | RFC 3161 时间戳"读得出"那一支、联网吊销那一路，都在本机现造、不出这台机器 | `tests/signaturesample.h`、`tests/signaturecheck_smoke.cpp`，必要时 `src/document/signatureverify.*` |

**基线**：门禁 **28 passed / 0 failed**，目录 **750 条**，numerus **65**。

---

## 每个分支都要先做的：环境准备与规矩

与 `docs/handoff-04.md` 的同名一节**完全相同**（两个 junction；从零构建；不删 `CMakeCache.txt`；控制跑按 sha256 还原、
控制必须能让新断言红；门禁全绿；`tr()` / `%n` / `QT_TRANSLATE_NOOP`；目录写成已完成并逐条核；写用户文件只有「保存」一条路；
不改日志与规矩文档；不打包、不安装、不 push、不合并；并行分支 gui-probe 传自己的 `-Exe`；锚点；署名行；最终报告）。
另读 `AGENTS.md` 里"验证数字签名"（§126）、"测试目标的名字别带 install/bench/verify/probe"、"状态栏上接在一句话后面的句子"三条，
以及"bash heredoc 里写 C++ 不要带反斜杠"。

**两个分支都不许做的**：不往任何证书存储、密钥目录写东西（样本密钥用无名 CNG 密钥，`tests/signaturesample.h` 已经是这样）；
不连这台机器以外的任何地址；不下载任何文件。

---

## J1. 侧栏「签名」页上屏幕；英文界面　分支：`gui-probe-signatures`

**背景**：侧栏「签名」页（图标 + 结论 + 签名人 + 时间、展开看细节、点一行跳页）只在 `documenttab_smoke` 里测过；
`-Mode inventory` 不列侧栏里的行，也没有截图。英文界面在装上的那份上一次没跑过签名这一族。

**要做**（只改 `xyzpdf/tools/gui-probe.ps1`）
1. `-Mode signaturepanel -Sample <名字>`：样本用 `XYZPDF_SIGNATURE_SAMPLE_DIR=<新目录> build-local-release/tests/xyzpdf_signaturecheck_smoke.exe`
   现造（目录已存在它会拒写，所以每次给新目录）。打开样本 → 显示侧栏（菜单里的那一项，按名字中英两种拼法找）→ 切到「签名」页 →
   读出每一行的文字（UIA；读不到就像 `outlinetree` 那样找 GridPattern / DataItem，还不行就把侧栏子树的类名与名字倒出来再停）→
   判：valid / expired / appended-one 是「证书不受信任或已过期」（这台机器不信任样本的根，本该如此），tampered 是「内容被改过」，
   x509-rsa-sha1 / broken-blob 是「无法验证」；appended-one 展开后有"签名之后还有 1 次增量更新"那一行。再点一行，看页码变不变（单页样本就看
   "点了不崩、选中项变了"）。侧栏用 `PrintWindow` 按外框出一张图（规矩见 AGENTS 里 `tabstrip` 那段），图存下来，路径进日志。
   `-Control`：在 tampered 上期望「证书不受信任」→ exit 1。没有签名的文档上「签名」页不该出现 —— 用 `tests/fixtures/text.pdf` 跑一轮负例。
2. 英文界面：`-Mode langswitch -Installed -To English` → 跑 `signedsave`、`signedlock -ExpectDocMdp 1/2/0`、`signaturepanel` 各一轮 → 
   `-Mode langswitch -Installed -To 跟随系统`（两种拼法都认的那条）改回去。**改回去必须在同一次报告里验收**（AGENTS："会改用户设置的探针"）。
   英文状态栏上要读到两句之间有空格、没有全角标点（§126 第 1、2 处在屏幕上成立与否）。
3. 新模式进 `$script:saveModes`（如果它会存）或不进（只读就不进）；早退走 `Bail`；`.ps1` 非 ASCII 0；开发版跑时传
   `-Exe H:\test\XYZPDF-wt\gui-probe-signatures\xyzpdf\build-local-release\xyzpdf.exe`；装上的那份用 `-Installed -OpenViaPicker <完整路径>`。
4. 屏幕上看见的产品缺陷单列，**不改产品代码**。

---

## J2. 时间戳与联网吊销，都在本机现造　分支：`signature-timestamp-revocation`

**背景**（I1 的报告原话的意思）：RFC 3161 时间戳"读得出"那一支没测 —— 离线造不出 Windows 认账的 TimeStampToken
（`CryptSignMessage` 填不进内层内容类型 `id-ct-TSTInfo`）；联网吊销那一路只量到"开关换了问法"，样本证书里没有 CRL 分发点也没有 OCSP 地址，
Windows 无处可连，所以"实际连了哪些地址"那一句是空的。

**要做**
1. **时间戳**：用 `CryptMsgOpenToEncode` + `CMSG_SIGNED_ENCODE_INFO`（它的 `pszInnerContentObjID` 可以填 `szOID_TIMESTAMP_TOKEN` /
   id-ct-TSTInfo）自己造一枚 TimeStampToken：TSTInfo 按 RFC 3161 §2.4.2（version、policy、messageImprint = 签名值的摘要、serialNumber、genTime），
   由一张带 `id-kp-timeStamping` 扩展用途（关键）的现造 TSA 证书签。作为签名者的非认证属性 `id-aa-signatureTimeStampToken`
   （1.2.840.113549.1.9.16.2.14）挂上去。验证时 TSA 的根同样经注入的内存存储受信任。要红绿两边：令牌对 → 结论里时间取令牌的、标「（时间戳）」；
   messageImprint 改一个字节 → 不采纳令牌、细节行说"没能通过校验"（这一支 I1 已有，留作对照）。样本形状从 RFC 3161 与 RFC 5652 来，不从被测代码来。
2. **联网吊销，只连本机**：现造的 CA 给叶证书写一个 CRL 分发点 `http://127.0.0.1:<随机端口>/test.crl`（与/或 AIA 的 OCSP 地址），
   测试进程里起一个只听 127.0.0.1 的最小 HTTP 服务（Qt 的 `QTcpServer` 就够），供一份由同一 CA 签的 CRL：一轮叶证书不在 CRL 上、一轮在。
   打开"联网检查"（测试里直接给 Options，不走同意框）→ 记下服务收到的每一个请求路径（这就是"实际连了哪些地址"的答案，进 MEASURE 行）→
   判：不在 CRL 上的仍是原结论且细节写"已联网检查过吊销状态"；在 CRL 上的变成「证书不受信任或已过期」并说明是被吊销。关掉开关那一轮，服务必须
   **一个请求都没收到**（这是"默认不联网"的屏幕外证据）。注意 Windows 的 URL 缓存（`CryptnetUrlCache`）：每轮用新端口或新路径，别让上一轮的缓存答了这一轮。
3. **真 CA 证书签的 PDF**：这台机器上没有。**不要下载**。报告里写一句"需要用户提供"即可。
4. 新增用户可见文字（如"已被吊销"）照规矩：`tr()`、计数走 `%n` 加探针、目录写成已完成。每条新断言配控制跑
   （例如把"在 CRL 上"那轮的结论钉回原样 → 必须红；把吊销开关在关的那轮钉成开 → "一个请求都没收到"那条必须红）。

**测试落点**：`signaturecheck_smoke`（名字里**不要**带 verify）。

---

## 合并方式（主会话做）

J2 先合（可能动产品代码），J1 后合（J1 读的是屏幕上的话；J2 若新增了"已被吊销"之类的句子，主会话把 J1 的判据补上）。
合并后重打包重装，在装上的那份上中英各跑一遍 J1 的模式。
