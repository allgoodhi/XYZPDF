# 上架材料：隐私政策、商店页文字、提交表单里的回答（2026-10-25 草稿）

`store-submission.md` 是"包与本机验证"的账，这一份是 Partner Center 里要**填进去的话**。凡是写"本程序做什么/不做什么"的句子，
都按 2026-10-25 的代码核过（出处写在括号里）；代码改了这里要跟着改。**隐私政策必须有一个公开网址**，放在哪里由作者定（见文末）。

---

## 1. 隐私政策（中文）

**XYZPDF 隐私政策**　生效日期：（上架当日）

XYZPDF 是一款在你自己的电脑上阅读与编辑 PDF 的程序。我们不运营服务器，不收集、不上传、不出售你的任何数据。

**默认情况下不联网。** 打开、阅读、搜索、打印、导出为图片或 Word、编辑与保存 PDF、截图与文字识别（OCR，使用 Windows 自带的识别引擎，
在本机完成），都不会把任何内容发到网络上。程序没有使用统计、崩溃上报或广告。

**只有你主动开启的两项功能会联网：**

1. **划词翻译。** 只有在你于「设置 → 翻译」中选择翻译服务（DeepL 或 Google Cloud Translation）、填入**你自己的** API 密钥、并在第一次翻译前确认之后，
   程序才会把**你选中要翻译的那段文字**直接发送给你选的那家服务商，取回译文。不发送文档的其它内容、文件名或你的身份信息。
   这些文字如何处理，受该服务商自己的隐私政策约束（DeepL：https://www.deepl.com/privacy ，Google：https://policies.google.com/privacy ）。
   你的 API 密钥保存在本机的 Windows 凭据管理器里，只用于向该服务商证明身份，不发给任何其他人。（`src/translate/translator.cpp`、`keystore_windows.cpp`）
2. **联网检查数字签名证书是否被吊销。** 默认关闭。只有在你于「设置 → 数字签名」中勾选、并在第一次联网前再确认一次之后，程序验证签名时才会连接
   **该 PDF 的证书里写着的**吊销检查地址（CRL 或 OCSP），询问那张证书是否仍然有效。发出去的是证书本身带有的地址与序号，不是文档内容。
   对方因此可以知道有人在查询这张证书。关闭开关即撤回同意。（`src/document/signatureverify.cpp`）

**保存在你电脑上的东西：** 程序设置（语言、最近打开的文件列表、上次使用的文件夹等）保存在本机，卸载程序时随之删除；翻译 API 密钥保存在 Windows 凭据管理器，
可在设置中删除。程序只在你执行「保存」时写入你的 PDF 文件。

**儿童：** 本程序不面向儿童收集任何信息（它不收集任何人的信息）。

**变更：** 本政策如有变化，会在本页更新并修改生效日期。

**联系：** （作者的联系邮箱）

---

## 2. Privacy Policy (English)

**XYZPDF Privacy Policy** — Effective date: (the day of release)

XYZPDF is a program for reading and editing PDF files on your own computer. We run no servers, and we do not collect, upload or sell any of your data.

**It does not use the network by default.** Opening, reading, searching, printing, exporting to images or Word, editing and saving PDFs, and taking
snapshots with text recognition (OCR, done on your computer by the recognizer built into Windows) send nothing over the network. There is no usage
tracking, crash reporting or advertising.

**Two features use the network, and only if you turn them on:**

1. **Translate selection.** Only after you choose a translation service (DeepL or Google Cloud Translation) in Settings → Translation, enter **your own**
   API key, and confirm before the first translation, the program sends **the text you selected to translate** directly to that provider and receives the
   translation. Nothing else from the document, no file names and nothing that identifies you is sent. The provider's own privacy policy governs that text
   (DeepL: https://www.deepl.com/privacy , Google: https://policies.google.com/privacy ). Your API key is kept in Windows Credential Manager on your
   computer and is used only to authenticate to that provider.
2. **Online certificate revocation check for digital signatures.** Off by default. Only if you tick it in Settings → Digital signatures and confirm once
   more before the first connection, checking a signature contacts the revocation address (CRL or OCSP) **written in that PDF's certificates** to ask
   whether the certificate is still valid. What goes out is the address and serial number the certificate itself carries, not the document's content.
   The other side can tell that someone is checking that certificate. Turning the setting off withdraws the consent.

**What stays on your computer:** settings (language, recent files, last folder) are stored locally and removed when the program is uninstalled; the
translation API key is stored in Windows Credential Manager and can be removed in Settings. The program writes to your PDF only when you choose Save.

**Children:** the program collects no information from anyone, children included.

**Changes:** changes to this policy will be posted on this page with a new effective date.

**Contact:** (the author's contact e-mail)

---

## 3. 商店页文字

**名称**：XYZPDF（Partner Center 里先"保留名称"，名字被占就要另想）

**简短描述**（中文）：轻快的 Windows PDF 阅读器：阅读、搜索、表单填写、页面编辑、转 Word、划词翻译与文字识别。

**Short description** (English): A fast PDF reader for Windows: read, search, fill forms, edit pages, convert to Word, translate selections and recognize text.

**描述**（中文）：
XYZPDF 是一款为 Windows 打造的 PDF 阅读与编辑工具，界面与操作方式和常用的桌面软件一致，打开即用。
- 流畅阅读：多标签、缩略图与书签侧栏、全文搜索、全屏与演示模式、深色界面
- 表单：直接在 PDF 里填写文本框、复选框、下拉框与列表，就地保存
- 页面编辑：删除、旋转、重排、合并与拆分页面，编辑书签，加密与去除密码
- 转换为 Word：按原版式还原文字、表格、图片与页眉页脚，导出的 .docx 可继续编辑
- 截图与文字识别：框选区域复制为图片或识别出文字（在本机完成）
- 划词翻译：使用你自己的 DeepL 或 Google Cloud 密钥
- 数字签名：识别并验证 PDF 中的签名，保存时保护已签名的内容
- 资源管理器集成：预览窗格、缩略图与右键"用 XYZPDF 打开"
默认不联网，不收集任何数据。

**Description** (English):
XYZPDF is a PDF reader and editor made for Windows, with the look and behaviour of the desktop programs you already use.
- Smooth reading: tabs, thumbnail and bookmark sidebar, full-text search, full screen and presentation mode, dark theme
- Forms: fill in text fields, check boxes, combo boxes and lists right in the PDF, and save in place
- Page editing: delete, rotate, reorder, merge and split pages, edit bookmarks, encrypt or remove a password
- Convert to Word: text, tables, pictures, headers and footers laid out as in the PDF, in a .docx you can keep editing
- Snapshot and text recognition: copy a region as a picture or as text (recognized on your computer)
- Translate selection: with your own DeepL or Google Cloud key
- Digital signatures: see and check the signatures in a PDF; saving protects what was signed
- File Explorer: preview pane, thumbnails and "Open with XYZPDF"
No network use by default, and no data collection.

**关键词**（最多 7 个）：PDF, PDF 阅读器, PDF 编辑, 转 Word, 表单填写, OCR, 翻译

**类别**：效率（Productivity）；子类别可选"文档"

**截图**：至少 1 张、建议 4 张，1366×768 或更大：阅读页（侧栏+正文）、表单填写、转 Word 前后对照、划词翻译。由作者在上架前截取（探针只截我们自己的窗口，商店图要真实界面）。

---

## 4. 提交表单里的回答

- **受限能力 `runFullTrust` 的用途说明**（英文填写）：
  "XYZPDF is a classic Win32 desktop application (Qt) packaged with MSIX. It needs full trust to render and edit PDF files the user opens from any
  location on disk, to save edits back to those files, to register File Explorer preview, thumbnail and context-menu handlers declared in the
  manifest, and to use the Windows printing and OCR APIs. It does not run in the background and does not modify system settings."
- **隐私政策网址**：第 1/2 节发布后的网址（必填，因为程序有联网功能）。
- **数据收集**：不收集个人信息；翻译功能会把用户选中的文字发送给用户自己选择并提供密钥的第三方服务（如实勾选"与第三方共享：用户主动触发的内容"）。
- **年龄分级问卷**：无用户生成内容的公开分享、无聊天、无购买、无位置信息 —— 按问卷回答，通常为 3+ / 所有人。
- **定价**：免费或付费由作者定；有无试用版由作者定。
- **市场**：默认全部市场，或只选中国/英语市场。
- **系统要求**：Windows 10 1809（10.0.17763）及以上，x64（与两份清单的 `MinVersion="10.0.17763.0"` 一致）；内存按商店表单的档位填。

---

## 5. 隐私政策放在哪里（作者定）

几种常见做法，任选其一，需要的只是一个**公开可访问、长期不变**的网址：
1. **GitHub Pages**：仓库 `allgoodhi/XYZPDF` 若是公开的，可在仓库里加一个 `docs/privacy/index.html`（或单独开一个公开仓库），在仓库设置里打开 Pages。
2. **个人网站 / 博客**的一页。
3. **任何静态页托管**（如 Gitee Pages、Netlify）。
发布到网上是作者的决定；网址定了，填进 Partner Center 的"隐私政策 URL"，并在程序「关于」里加一个链接（可选）。
