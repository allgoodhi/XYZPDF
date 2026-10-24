# 第六批：已签名的文档，以及组合框/列表框上屏幕（2026-10-24）

第五批（H1–H3）已合进 main，见 `docs/store-submission.md` §120–§121。现在表单填得进、存得回、上过屏幕；
但一份**带数字签名**的文档，任何一次保存都会重写整份文件 —— qpdf 那条路必然、PDFium 那条现在也是
（`FPDF_NO_INCREMENTAL`）—— 签名覆盖的字节一变，签名就失效，而读者事先一个字都没听到。两件**同时发**：

| 分支 | 内容 | 主要改动面 |
|---|---|---|
| H4 `signed-docs` | 识别签名；只改了字段时走增量保存、签名覆盖的字节一个不动；会让签名失效的保存在按下之前说清楚；DocMDP 不许改的不让改 | `pdfiumformfill.cpp`、`pdfiumbackend*`、`documenteditor`、`documenttab`、几个编辑框、`mainwindow` 状态栏 |
| H5 `gui-probe-choices` | 组合框与多选列表框上屏幕（H1 做的，H2 当时还没有它们） | 只改 `tools/gui-probe.ps1`（必要时 `tools/pdfprobe.cpp` 的样本/读值子命令） |

**基线**：门禁 **27 passed / 0 failed**，目录 **692 条**，numerus **53**。

---

## 每个分支都要先做的：环境准备与规矩

与 `docs/handoff-04.md` 的同名一节**完全相同**（两个 junction；从零构建；不删 `CMakeCache.txt`；控制跑按 sha256 还原、
**控制必须能让新断言红**；门禁全绿；`tr()` / `%n` / `QT_TRANSLATE_NOOP`；目录写成已完成并逐条核；写用户文件只有「保存」一条路；
不改日志与规矩文档；不打包、不安装、不 push、不合并；并行分支 gui-probe 传自己的 `-Exe`；锚点；署名行；最终报告）。
另读 `AGENTS.md` 里 §118–§120 那几条（表单代码两半、暂存收走、保存探针护栏、Tab 那扇门）。基线以本文为准。

---

## H4. 已签名的文档　分支：`signed-docs`

**要做**
1. **识别**：打开时用 PDFium（`fpdf_signature.h`：`FPDF_GetSignatureCount`、`FPDFSignatureObj_GetByteRange`、
   `FPDFSignatureObj_GetDocMDPPermission`）读出有几个签名、各自覆盖的字节范围、有没有 DocMDP 认证签名及其 P 值（1/2/3）。
   状态栏说一句（`setStatusMessage`，有时限）："带着 N 个数字签名，本版不验证签名本身"——**不验证**这件事要说出来，
   因为读者会以为我们看过。属性框（文档属性）加一行签名数。
2. **只改字段 → 增量保存**：没有任何结构修改、只有字段改动时，`stageFieldValues` 改用 `FPDF_INCREMENTAL`。
   **判据是字节**：写出的文件的前 N 字节必须与原文件逐字节相同（N = 原文件长度），签名的 ByteRange 两段按 SHA-256 前后相同。
   没有签名的文档照旧 `FPDF_NO_INCREMENTAL`（文件不会一次次长胖）——或者量了之后你认为一律增量更好，就写出理由与读数。
   加密文档的增量保存先量：写出来能不能用原口令打开、前缀是否仍逐字节相同。
3. **会让签名失效的保存，在按下之前说**：任何走 qpdf 的结构修改（删除、旋转、移动、合并、拆分、加密、水印、书签）都会重写整份文件。
   文档带签名时，这八个框里加一句（照 §112 XFA 那句的做法：按下之前的纯查询，`%n` 计数）："保存之后这 N 个数字签名都会失效"。
   关窗三答框与「保存」本身不再多问一遍（框里已经说过）。
4. **DocMDP 不许改的不让改**：认证签名 P=1（不许任何改动）→ 字段只读（点了不取焦）、八个编辑框的确定按钮灰、并说为什么；
   P=2（只许填表）→ 字段可填、结构修改的确定按钮灰；P=3（还许注释）→ 同 P=2（本程序不写注释）。FieldMDP 锁住的字段（签名字段的
   `/Lock`）只读 —— 先量 PDFium 自己是否已经这样，再决定要不要我们挡。
5. **样本**：生成器（`XYZPDF_FORM_SAMPLE_DIR` 或 `tests/formsample.h`）造带签名字典的样本（`/Sig` 字段、`/V` 指向签名字典，
   `/ByteRange` 与 `/Contents` 占位真实可算；不必是真的密码学签名 —— 判据是"覆盖的字节没变"，不是"验证通过"）；
   另造一份带 `/Perms /DocMDP` 的认证签名样本，P=1、P=2 各一份。样本形状从 PDF 32000-1 §12.8 来，**不要从被测代码来**（AGENTS 那条）。

**测试**：`pdfiumbackend_smoke` 测识别与增量写出的字节前缀；`documenttab_smoke` 在
`    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----` 之前测整条路（填字段 → 保存 → 前缀相同、ByteRange 摘要相同；
结构修改的框里有那句、按下后签名覆盖的字节确实变了；P=1/P=2 的只读与灰按钮）；`documenteditor_smoke` 在
`    // ---- whether two paths name one file ----` 之前；`mainwindow_smoke` 在 `    // ---- a second document joins the first ----` 之前测状态栏那句。
每条配控制跑（例如把 `FPDF_INCREMENTAL` 换回 `FPDF_NO_INCREMENTAL`，前缀那条必须红）。

---

## H5. 组合框与列表框上屏幕　分支：`gui-probe-choices`

**背景**：H1 接上了组合框下拉（PDFium 自己画）、可编辑组合框、多选列表框、滚轮分流，全在无头测试里；H2 的四个屏幕模式
写的时候还没有它们。

**要做**（只改 `xyzpdf/tools/gui-probe.ps1`；样本与读值需要扩展时，只动 `tools/pdfprobe.cpp` 的 `--write-form-sample` /
`--fields`，不碰产品代码）
1. `-Mode formcombo`：护栏复制样本 → 点组合框的下拉按钮 → 下拉列表在屏幕上画出来（`PrintWindow` 量字段下方的墨，几何判据）→
   点第二项 → 星号 → Ctrl+S → `--fields` 读回是那一项。再一遍用 Alt+↓ 打开。`-Control`：不点下拉直接保存 → 值不变 → exit 1。
2. `-Mode formlist`：多选列表框，单击一项、Ctrl+单击另一项 → 保存 → 读回两项都在（数组）。`-Control`：第二下不按 Ctrl → 只剩一项 → exit 1。
3. 可编辑组合框：点进去打字 → 保存 → 读回打的字（放进 `formcombo` 的第三轮或单独一个模式都行）。
4. 中英各一遍；新模式进 `$script:saveModes`；早退走 `Bail`；`.ps1` 非 ASCII 0；每次传
   `-Exe H:\test\XYZPDF-wt\gui-probe-choices\xyzpdf\build-local-release\xyzpdf.exe`。
5. 屏幕上看见的产品缺陷单列（H2 上次就是这样看见 Tab 那条的）。

---

## 合并方式（主会话做）

H5 先合（只动 harness），H4 后合。合并后重打包重装，在装上的那份上跑 H5 的模式与 `formfill`（签名样本那一份由主会话补一轮）。
