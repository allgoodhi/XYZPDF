<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en">
<context>
    <name>AboutInfo</name>
    <message>
        <location filename="../../src/core/aboutinfo.h" line="27"/>
        <source>&lt;h3&gt;%1 %2&lt;/h3&gt;&lt;p&gt;Windows PDF 阅读器。&lt;/p&gt;&lt;p&gt;Qt %3&lt;br&gt;渲染引擎：Qt PDF（PDFium）&lt;/p&gt;&lt;p&gt;版权所有 © 2026 allgoodhi&lt;/p&gt;</source>
        <translation>&lt;h3&gt;%1 %2&lt;/h3&gt;&lt;p&gt;A Windows PDF reader.&lt;/p&gt;&lt;p&gt;Qt %3&lt;br&gt;Rendering engine: Qt PDF (PDFium)&lt;/p&gt;&lt;p&gt;Copyright © 2026 allgoodhi&lt;/p&gt;</translation>
    </message>
    <message>
        <location filename="../../src/core/aboutinfo.h" line="40"/>
        <source>关于 %1</source>
        <translation>About %1</translation>
    </message>
</context>
<context>
    <name>CommandLine</name>
    <message>
        <location filename="../../src/cli/commandline.cpp" line="108"/>
        <location filename="../../src/cli/commandline.cpp" line="160"/>
        <source>%1 后面要跟一个页码（从 1 开始）。</source>
        <translation>%1 must be followed by a page number, counting from 1.</translation>
    </message>
    <message>
        <location filename="../../src/cli/commandline.cpp" line="128"/>
        <source>一次只能执行一个操作，但命令行里给了多个：%1</source>
        <translation>Only one action can be given at a time, but the command line has more: %1</translation>
    </message>
    <message>
        <location filename="../../src/cli/commandline.cpp" line="153"/>
        <source>无法识别的命令行选项：%1</source>
        <translation>Unrecognised command line option: %1</translation>
    </message>
    <message>
        <location filename="../../src/cli/commandline.cpp" line="175"/>
        <source>%1 只能用在单个文件上，但给了 %2 个。</source>
        <translation>%1 works on one file only, but %2 were given.</translation>
    </message>
</context>
<context>
    <name>DeletePagesDialog</name>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="24"/>
        <source>删除页面</source>
        <translation>Delete Pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="30"/>
        <source>要删除的页面</source>
        <translation>Pages to delete</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="33"/>
        <source>当前页（第 %1 页）</source>
        <translation>Current page (page %1)</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="37"/>
        <source>指定页码</source>
        <translation>These page numbers</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="40"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="66"/>
        <source>删除这些页</source>
        <translation>Delete these pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="67"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="89"/>
        <source>这些页在前面的修改里已经删掉了，这里没有可删的。</source>
        <translation>Those pages were already taken out by the earlier changes, so there is nothing left to delete here.</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="91"/>
        <source>还没有说出要删哪几页。</source>
        <translation>No pages have been named yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="93"/>
        <source>这样一页也不剩了，而一个不剩的 PDF 是打不开的。留着至少一页。</source>
        <translation>That leaves no pages at all, and a PDF with no pages cannot be opened. Keep at least one.</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="95"/>
        <source>现在这份有 %1 页：再删 %2 页，剩下 %3 页。</source>
        <translation>The document as it stands has %1 pages: taking out %2 more leaves %3.</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="103"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
</context>
<context>
    <name>DocumentEditor</name>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="85"/>
        <source>旋转 180°（%1 页）</source>
        <translation>Rotate 180° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="87"/>
        <source>向左转 90°（%1 页）</source>
        <translation>Turn left 90° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="89"/>
        <source>向右转 90°（%1 页）</source>
        <translation>Turn right 90° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="96"/>
        <source>移到最前（%1 页）</source>
        <translation>Move to the front (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="98"/>
        <source>移到第 %2 页之后（%1 页）</source>
        <translation>Move after page %2 (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="99"/>
        <source>移到最后（%1 页）</source>
        <translation>Move to the end (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="101"/>
        <source>删除 %1 页</source>
        <translation>Delete %1 pages</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="281"/>
        <source>这份文档正在阅读中，但结构编辑用的解析器打不开它。

解析器说：%1</source>
        <translation>This document is open for reading, but the parser used for structural editing cannot open it.

The parser says: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="288"/>
        <source>这份 PDF 一页也没有，没法在上面做任何页面修改。</source>
        <translation>This PDF has no pages at all, so no page change can be made to it.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="301"/>
        <location filename="../../src/edit/documenteditor.cpp" line="336"/>
        <location filename="../../src/edit/documenteditor.cpp" line="368"/>
        <location filename="../../src/edit/documenteditor.cpp" line="571"/>
        <source>还没有打开任何文档。</source>
        <translation>No document has been opened yet.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="310"/>
        <location filename="../../src/edit/documenteditor.cpp" line="382"/>
        <source>那些页在还没保存的修改里已经删掉了。</source>
        <translation>Those pages are already gone in the changes that have not been saved.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="311"/>
        <source>页码范围里没有可删除的页面。</source>
        <translation>The page range names no pages that can be deleted.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="315"/>
        <source>不能把每一页都删掉：一个不剩的 PDF 不是 PDF，任何阅读器都打不开它。想丢掉整份文档，就把文件本身删掉。</source>
        <translation>Every page cannot be deleted: a PDF with no pages is not a PDF, and no reader can open it. To get rid of the whole document, delete the file itself.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="343"/>
        <source>旋转只能是 90° 的整倍数。</source>
        <translation>A rotation can only be a whole multiple of 90°.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="352"/>
        <source>页码范围里没有可旋转的页面。</source>
        <translation>The page range names no pages that can be rotated.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="376"/>
        <source>要说移到第几页之后，而那个页码不在这份文档里。</source>
        <translation>It has to say which page to move behind, and that number is not a page in this document.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="383"/>
        <source>页码范围里没有可移动的页面。</source>
        <translation>The page range names no pages that can be moved.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="387"/>
        <source>第 %1 页就在要移动的页里，这样移等于没移。</source>
        <translation>Page %1 is one of the pages being moved, so moving it behind itself changes nothing.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="394"/>
        <source>那些页已经在最前头了。</source>
        <translation>Those pages are already at the front.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="395"/>
        <source>那些页已经在最后了。</source>
        <translation>Those pages are already at the end.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="396"/>
        <source>那些页已经在第 %1 页后面了。</source>
        <translation>Those pages are already after page %1.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="579"/>
        <source>另存为不能覆盖正在打开的文件。请换一个文件名。</source>
        <translation>Save As cannot overwrite the file that is open. Please choose another name.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="587"/>
        <source>这些修改会把每一页都删掉，而一个不剩的 PDF 任何阅读器都打不开。</source>
        <translation>These changes would delete every page, and no reader can open a PDF with none left.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="595"/>
        <source>文件夹不存在：%1</source>
        <translation>The folder does not exist: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="643"/>
        <source>写出文件时出错，原来的文件没有改动。

解析器说：%1</source>
        <translation>Writing the file failed, and the original file was not changed.

The parser says: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="662"/>
        <source>新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，或者这个名字不允许覆盖（Windows 错误 %2）。写好的文件留在 %3。</source>
        <translation>The new file was written, but it could not be put in place at %1 — another program may be holding it, or this name may not be replaced (Windows error %2). The finished file has been left at %3.</translation>
    </message>
</context>
<context>
    <name>DocumentTab</name>
    <message>
        <location filename="../../documenttab.cpp" line="107"/>
        <source>大纲</source>
        <translation>Outline</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="108"/>
        <source>缩略图</source>
        <translation>Thumbnails</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="671"/>
        <source>（空标签）</source>
        <translation>(no document)</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1036"/>
        <source>打开链接</source>
        <translation>Open Link</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1037"/>
        <source>要在浏览器中打开这个地址：
%1

继续吗？</source>
        <translation>Open this address in the browser?
%1

Continue?</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1062"/>
        <source>正在打印…</source>
        <translation>Printing…</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1062"/>
        <location filename="../../documenttab.cpp" line="1108"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1080"/>
        <source>打印</source>
        <translation>Print</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1094"/>
        <location filename="../../documenttab.cpp" line="1099"/>
        <location filename="../../documenttab.cpp" line="1128"/>
        <source>导出为图片</source>
        <translation>Export as Images</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1094"/>
        <source>请先选择保存文件夹。</source>
        <translation>Choose a folder to save into first.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1099"/>
        <source>页码范围里没有可导出的页面。</source>
        <translation>The page range has no pages to export.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1108"/>
        <source>正在导出为图片…</source>
        <translation>Exporting images…</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1146"/>
        <source>修改这份文档</source>
        <translation>Editing this document</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1187"/>
        <source>旋转页面</source>
        <translation>Rotate Pages</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1206"/>
        <source>移动页面</source>
        <translation>Move Pages</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1264"/>
        <source>%1 已修改.pdf</source>
        <translation>%1 modified.pdf</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1269"/>
        <source>把这些修改存成一个新文件</source>
        <translation>Save these changes into a new file</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1285"/>
        <source>保存修改</source>
        <translation>Save Changes</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1269"/>
        <source>PDF 文档 (*.pdf)</source>
        <translation>PDF documents (*.pdf)</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1169"/>
        <source>删除页面</source>
        <translation>Delete Pages</translation>
    </message>
</context>
<context>
    <name>FindBar</name>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="20"/>
        <source>查找：</source>
        <translation>Find:</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="25"/>
        <source>在本文档中查找</source>
        <translation>Find in this document</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="29"/>
        <source>区分大小写</source>
        <translation>Match case</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="41"/>
        <source>上一个</source>
        <translation>Previous</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="42"/>
        <source>上一个匹配（Shift+Enter）</source>
        <translation>Previous match (Shift+Enter)</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="46"/>
        <source>下一个</source>
        <translation>Next</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="47"/>
        <source>下一个匹配（Enter）</source>
        <translation>Next match (Enter)</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="52"/>
        <source>关闭查找栏（Esc）</source>
        <translation>Close the find bar (Esc)</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="109"/>
        <source>搜索中…</source>
        <translation>Searching…</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="113"/>
        <source>没有找到</source>
        <translation>No matches</translation>
    </message>
    <message>
        <location filename="../../src/ui/findbar.cpp" line="116"/>
        <source>第 %1 / %2 个</source>
        <translation>%1 of %2</translation>
    </message>
</context>
<context>
    <name>ImageExportDialog</name>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="25"/>
        <source>导出为图片</source>
        <translation>Export as Images</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="33"/>
        <source>页面范围</source>
        <translation>Pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="36"/>
        <source>全部（%1 页）</source>
        <translation>All (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="40"/>
        <source>当前页（第 %1 页）</source>
        <translation>Current page (page %1)</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="44"/>
        <source>自定义</source>
        <translation>Custom</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="46"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="66"/>
        <source>图片</source>
        <translation>Images</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="70"/>
        <source>PNG（无损）</source>
        <translation>PNG (lossless)</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="71"/>
        <source>JPEG（有损，较小）</source>
        <translation>JPEG (lossy, smaller)</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="72"/>
        <source>格式</source>
        <translation>Format</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="79"/>
        <source>分辨率</source>
        <translation>Resolution</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="89"/>
        <source>尚未选择</source>
        <translation>Not chosen yet</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="92"/>
        <source>浏览…</source>
        <translation>Browse…</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="95"/>
        <source>保存到文件夹</source>
        <translation>Save into a folder</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="100"/>
        <source>导出</source>
        <translation>Export</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="101"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/imageexportdialog.cpp" line="111"/>
        <source>选择保存文件夹</source>
        <translation>Choose a folder to save into</translation>
    </message>
</context>
<context>
    <name>LanguageDialog</name>
    <message>
        <location filename="../../src/ui/languagedialog.cpp" line="29"/>
        <source>跟随系统</source>
        <translation>Follow the system</translation>
    </message>
    <message>
        <location filename="../../src/ui/languagedialog.cpp" line="51"/>
        <source>语言更改将在下次启动时生效。</source>
        <translation>The language change takes effect the next time the program starts.</translation>
    </message>
    <message>
        <location filename="../../src/ui/languagedialog.cpp" line="58"/>
        <source>确定</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="../../src/ui/languagedialog.cpp" line="59"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../../mainwindow.ui" line="14"/>
        <source>XYZPDF</source>
        <translation>XYZPDF</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="486"/>
        <source>把 PDF 拖到这里，或者</source>
        <translation>Drop a PDF here, or</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="488"/>
        <source>打开 PDF…</source>
        <translation>Open a PDF…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="531"/>
        <source>文件(&amp;F)</source>
        <translation>&amp;File</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="533"/>
        <source>打开(&amp;O)…</source>
        <translation>&amp;Open…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="537"/>
        <source>最近打开</source>
        <translation>Recent</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="547"/>
        <source>导出为 Word(&amp;E)…</source>
        <translation>Export to Word(&amp;E)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="549"/>
        <source>把这份 PDF 的文字导出为 .docx 文档</source>
        <translation>Export the text of this PDF as a .docx document</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="553"/>
        <source>打印(&amp;P)…</source>
        <translation>Print(&amp;P)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="555"/>
        <source>把这份 PDF 打印出来</source>
        <translation>Print this PDF</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="562"/>
        <source>导出为图片(&amp;G)…</source>
        <translation>Export as Images(&amp;G)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="564"/>
        <source>把每一页导出为 PNG 图片到一个文件夹</source>
        <translation>Write every page as a PNG image into a folder</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="572"/>
        <source>关闭标签(&amp;W)</source>
        <translation>Close &amp;Tab</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="577"/>
        <source>退出(&amp;X)</source>
        <translation>E&amp;xit</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="589"/>
        <source>编辑(&amp;E)</source>
        <translation>Edit(&amp;E)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="591"/>
        <source>复制(&amp;C)</source>
        <translation>Copy(&amp;C)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="599"/>
        <source>翻译(&amp;T)</source>
        <translation>Translate(&amp;T)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="607"/>
        <source>全选(&amp;A)</source>
        <translation>Select All(&amp;A)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="622"/>
        <source>查找(&amp;F)…</source>
        <translation>Find(&amp;F)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="630"/>
        <source>查找下一个</source>
        <translation>Find Next</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="637"/>
        <source>查找上一个</source>
        <translation>Find Previous</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="645"/>
        <source>查看(&amp;V)</source>
        <translation>&amp;View</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="671"/>
        <source>只查看</source>
        <translation>View Only</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="672"/>
        <source>拖动页面本身，不选择页面上的内容</source>
        <translation>Drag the page itself, without selecting anything on it</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="673"/>
        <source>选择</source>
        <translation>Select</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="674"/>
        <source>选择文字并复制，指针在文字上变成 I 形</source>
        <translation>Select text and copy it; the pointer becomes an I-beam over text</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="675"/>
        <source>识别</source>
        <translation>Recognize</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="676"/>
        <source>拖出一个方框，识别框内像素里的文字</source>
        <translation>Drag a box and recognize the text in the pixels inside it</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="677"/>
        <source>翻译</source>
        <translation>Translate</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="679"/>
        <source>拖选文字即翻译；拖到图片上则框选识别后再翻译</source>
        <translation>Drag over text to translate it; drag over a picture to recognize it first</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="708"/>
        <source>放大</source>
        <translation>Zoom In</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="712"/>
        <source>缩小</source>
        <translation>Zoom Out</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="716"/>
        <source>实际大小</source>
        <translation>Actual Size</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="720"/>
        <location filename="../../mainwindow.cpp" line="1076"/>
        <location filename="../../tests/i18n_smoke.cpp" line="133"/>
        <location filename="../../tests/i18n_smoke.cpp" line="144"/>
        <source>适应宽度</source>
        <translation>Fit Width</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="724"/>
        <location filename="../../mainwindow.cpp" line="1077"/>
        <source>适应整页</source>
        <translation>Fit Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="729"/>
        <source>顺时针旋转</source>
        <translation>Rotate Clockwise</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="734"/>
        <source>逆时针旋转</source>
        <translation>Rotate Counterclockwise</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="739"/>
        <source>连续滚动</source>
        <translation>Continuous</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="748"/>
        <source>单页</source>
        <translation>Single Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="757"/>
        <source>双页</source>
        <translation>Two Pages</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="766"/>
        <source>书籍模式</source>
        <translation>Book Mode</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="779"/>
        <source>导航侧栏</source>
        <translation>Navigation Sidebar</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="791"/>
        <source>夜间阅读(&amp;G)</source>
        <translation>Night Reading(&amp;G)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="795"/>
        <source>把页面反色，暗处阅读时不刺眼</source>
        <translation>Invert the pages, so that reading in the dark does not glare</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="809"/>
        <source>全屏(&amp;L)</source>
        <translation>Full Screen(&amp;L)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="812"/>
        <source>隐藏工具栏和菜单，鼠标推到顶边唤出</source>
        <translation>Hide the toolbars and the menu; push the pointer to the top edge to bring them back</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="816"/>
        <source>演示(&amp;P)</source>
        <translation>Present(&amp;P)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="819"/>
        <source>从当前页开始放映：点击或方向键翻页，Esc 退出</source>
        <translation>Show from the current page: a click or the arrow keys turn pages, Esc ends it</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="838"/>
        <source>文档(&amp;D)</source>
        <translation>Document(&amp;D)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="840"/>
        <source>删除页面(&amp;E)…</source>
        <translation>Delete Pages(&amp;E)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1338"/>
        <source>已写成一个新文件（%1 页）：%2</source>
        <translation>Wrote a new file (%1 pages): %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="912"/>
        <source>转到(&amp;G)</source>
        <translation>&amp;Go</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="914"/>
        <source>第一页</source>
        <translation>First Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="918"/>
        <source>上一页</source>
        <translation>Previous Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="922"/>
        <source>下一页</source>
        <translation>Next Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="926"/>
        <source>最后一页</source>
        <translation>Last Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="934"/>
        <source>跳转到页码…</source>
        <translation>Go to Page…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="944"/>
        <source>窗口(&amp;N)</source>
        <translation>Window(&amp;N)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="946"/>
        <source>合并所有窗口(&amp;G)</source>
        <translation>Merge All Windows(&amp;G)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="948"/>
        <source>把其它窗口里打开的文档全部并进这个窗口</source>
        <translation>Bring every document open in the other windows into this one</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="964"/>
        <source>跟随系统</source>
        <translation>Follow the system</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="983"/>
        <source>帮助(&amp;H)</source>
        <translation>&amp;Help</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="985"/>
        <source>设置(&amp;P)…</source>
        <translation>&amp;Settings…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="990"/>
        <source>第三方开源许可</source>
        <translation>Third-party licences</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="994"/>
        <source>关于 %1</source>
        <translation>About %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1025"/>
        <source>主工具栏</source>
        <translation>Main toolbar</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1033"/>
        <source>翻页</source>
        <translation>Pages</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1042"/>
        <source>页码</source>
        <translation>Page number</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1062"/>
        <source>缩放与旋转</source>
        <translation>Zoom and Rotate</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1070"/>
        <source>缩放</source>
        <translation>Zoom</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1072"/>
        <location filename="../../mainwindow.cpp" line="2131"/>
        <location filename="../../mainwindow.cpp" line="2158"/>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="273"/>
        <location filename="../../mainwindow.cpp" line="278"/>
        <location filename="../../mainwindow.cpp" line="298"/>
        <location filename="../../mainwindow.cpp" line="313"/>
        <location filename="../../mainwindow.cpp" line="1220"/>
        <source>就绪</source>
        <translation>Ready</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="205"/>
        <source>松开以在此窗口打开 %1</source>
        <translation>Release to open %1 in this window</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="207"/>
        <source>松开以在此窗口合并 %1 个文档</source>
        <translation>Release to merge %1 documents into this window</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="242"/>
        <source>松开以打开 %1</source>
        <translation>Release to open %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="243"/>
        <source>松开以打开 %1 个文件</source>
        <translation>Release to open %1 files</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="842"/>
        <source>记下要删掉的页，按保存修改为才写成一个新文件；正在读的这份不动</source>
        <translation>Notes down which pages to delete. Save Changes As is what writes them into a new file; the document you are reading stays as it is.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="849"/>
        <source>旋转页面(&amp;R)…</source>
        <translation>Rotate Pages(&amp;R)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="855"/>
        <source>记下要转的角度，按保存修改为才写成一个新文件；正在读的这份不动</source>
        <translation>Notes down which way to turn the pages. Save Changes As is what writes them into a new file; the document you are reading stays as it is.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="865"/>
        <source>移动页面(&amp;M)…</source>
        <translation>Move Pages(&amp;M)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="867"/>
        <source>记下要把哪几页挪到哪儿，按保存修改为才写成一个新文件；正在读的这份不动</source>
        <translation>Notes down which pages to move and where they go. Save Changes As is what writes them into a new file; the document you are reading stays as it is.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="881"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="889"/>
        <source>重做</source>
        <translation>Redo</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="899"/>
        <source>保存修改为(&amp;S)…</source>
        <translation>Save Changes As(&amp;S)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="904"/>
        <source>把这些修改写成一个新文件；正在读的这份不动</source>
        <translation>Writes these changes into a new file. The document you are reading stays as it is.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1122"/>
        <source>查找</source>
        <translation>Find</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1126"/>
        <source>在文档中查找</source>
        <translation>Find in the document</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1128"/>
        <source>输入即搜索；Enter 到下一个，Shift+Enter 到上一个，Esc 清除</source>
        <translation>Typing searches; Enter goes to the next, Shift+Enter to the previous, Esc clears</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1156"/>
        <source>当前是第几个匹配，一共几个</source>
        <translation>Which match is in front, and how many there are</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1167"/>
        <source>第 1234 / 1234 个</source>
        <translation>1234 of 1234</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1193"/>
        <source>搜索中…</source>
        <translation>Searching…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1198"/>
        <source>没有找到</source>
        <translation>No matches</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1203"/>
        <source>第 %1 / %2 个</source>
        <translation>%1 of %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1237"/>
        <source>找不到文件：%1</source>
        <translation>File not found: %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1316"/>
        <location filename="../../mainwindow.cpp" line="1582"/>
        <source>正在导出为 Word…（%1 / %2 页）</source>
        <translation>Exporting to Word… (page %1 of %2)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1326"/>
        <source>已导出 %1 张图片到 %2</source>
        <translation>Exported %1 images to %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1414"/>
        <source>已打开 %1</source>
        <translation>Opened %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1428"/>
        <source>无法打开 %1。

%2</source>
        <translation>Could not open %1.

%2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1449"/>
        <source>需要密码</source>
        <translation>Password required</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1450"/>
        <source>%1 已加密，请输入打开密码：</source>
        <translation>%1 is encrypted. Enter the password it opens with:</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1461"/>
        <source>密码不正确，已放弃打开。</source>
        <translation>The password was not correct, so the document was left closed.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1471"/>
        <source>打开 PDF</source>
        <translation>Open PDF</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1471"/>
        <source>PDF 文档 (*.pdf);;所有文件 (*)</source>
        <translation>PDF documents (*.pdf);;All files (*)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1490"/>
        <location filename="../../mainwindow.cpp" line="1615"/>
        <location filename="../../mainwindow.cpp" line="1660"/>
        <source>导出为 Word</source>
        <translation>Export to Word</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1491"/>
        <source>Word 文档 (*.docx)</source>
        <translation>Word document (*.docx)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1497"/>
        <source>正在导出为 Word…</source>
        <translation>Exporting to Word…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1513"/>
        <source>导出为 Word — %1</source>
        <translation>Export to Word — %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1514"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1549"/>
        <source>正在停止…</source>
        <translation>Stopping…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1583"/>
        <source>正在导出为 Word…（共 %1 页）</source>
        <translation>Exporting to Word… (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1609"/>
        <source>导出已取消</source>
        <translation>Export cancelled</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1614"/>
        <source>导出失败</source>
        <translation>Export failed</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1619"/>
        <source>已导出 %1 页、%2 个段落到：
%3</source>
        <translation>Exported %1 pages and %2 paragraphs to:
%3</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1626"/>
        <source>其中 %1 个带网格的表格已转换为可编辑 Word 表格。</source>
        <translation>Of these, %1 ruled tables were converted into editable Word tables.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1634"/>
        <source>其中 %1 处图形（图表、框图、照片等）已按图片插入，其所在页面的文字仍是可以编辑的。</source>
        <translation>%1 figures (charts, diagrams, photographs) were inserted as images; the text on their pages stays editable.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1645"/>
        <source>其中 %1 页没有文字层或包含复杂图表/表格，已按整页图片还原以保持原貌，但页内的文字不能直接编辑。需要可编辑的文字可以先用“识别”工具。</source>
        <translation>%1 pages had no text layer, or held charts and tables too involved to rebuild; they were reproduced as full-page images to keep their appearance, so the text in them cannot be edited directly. To get editable text out of one, run the Recognize tool first.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1655"/>
        <source>另有 %1 页既没有文字，也没有渲染出内容，已跳过。</source>
        <translation>Another %1 pages had neither text nor any rendered content, and were skipped.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1659"/>
        <source>已导出为 Word</source>
        <translation>Exported to Word</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1677"/>
        <location filename="../../mainwindow.cpp" line="1699"/>
        <source>还有没保存的修改</source>
        <translation>Changes have not been written</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1678"/>
        <source>「%1」还带着没有写进任何文件的修改：%2。

关掉这个标签就会把这些修改丢掉。要关掉吗？</source>
        <translation>&quot;%1&quot; still carries changes that are written in no file: %2.

Closing this tab throws those changes away. Close it anyway?</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1700"/>
        <source>这个窗口里有 %1 个标签带着没有写进任何文件的修改。

关掉窗口就会把这些修改丢掉。要关掉吗？</source>
        <translation>%1 tabs in this window carry changes that are written in no file.

Closing the window throws those changes away. Close it anyway?</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1888"/>
        <source>没有其他窗口可以合并</source>
        <translation>There are no other windows to merge</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1904"/>
        <source>已把 %1 个窗口的 %2 个标签并入当前窗口</source>
        <translation>Merged %2 tabs from %1 windows into this window</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2064"/>
        <source>撤销(&amp;U)</source>
        <translation>Undo(&amp;U)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2064"/>
        <source>撤销 %1(&amp;U)</source>
        <translation>Undo %1(&amp;U)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2070"/>
        <source>重做(&amp;O)</source>
        <translation>Redo(&amp;O)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2070"/>
        <source>重做 %1(&amp;O)</source>
        <translation>Redo %1(&amp;O)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2157"/>
        <location filename="../../tests/i18n_smoke.cpp" line="138"/>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2176"/>
        <source>%1 — %2</source>
        <translation>%1 — %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2195"/>
        <source>（没有）</source>
        <translation>(none)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2201"/>
        <source>清除列表</source>
        <translation>Clear the list</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2217"/>
        <source>语言更改将在下次启动时生效。</source>
        <translation>The language change takes effect the next time the program starts.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2317"/>
        <location filename="../../mainwindow.cpp" line="2325"/>
        <source>跳转到页码</source>
        <translation>Go to Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2318"/>
        <source>页码（1 - %1）：</source>
        <translation>Page number (1 - %1):</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2326"/>
        <source>页码要在 1 到 %1 之间。</source>
        <translation>The page number has to be between 1 and %1.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2356"/>
        <source>打印将在后续版本中提供。</source>
        <translation>Printing arrives in a later version.</translation>
    </message>
</context>
<context>
    <name>MovePagesDialog</name>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="24"/>
        <source>移动页面</source>
        <translation>Move Pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="30"/>
        <source>要移动的页面</source>
        <translation>Pages to move</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="33"/>
        <source>当前页（第 %1 页）</source>
        <translation>Current page (page %1)</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="43"/>
        <source>指定页码</source>
        <translation>These page numbers</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="45"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="62"/>
        <source>移到</source>
        <translation>Move them</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="64"/>
        <location filename="../../src/ui/movepagesdialog.cpp" line="180"/>
        <source>最前面</source>
        <translation>to the front</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="65"/>
        <location filename="../../src/ui/movepagesdialog.cpp" line="186"/>
        <source>最后面</source>
        <translation>to the end</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="76"/>
        <source>指定页之后</source>
        <translation>behind page</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="83"/>
        <source>页码</source>
        <translation>Page number</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="108"/>
        <source>移动这些页</source>
        <translation>Move these pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="109"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="138"/>
        <source>还没有说出要移动哪几页。</source>
        <translation>Nothing has said which pages to move yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="140"/>
        <source>要说移到第几页之后。</source>
        <translation>It has to say which page to move behind.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="142"/>
        <source>这份文档一共 %2 页，没有第 %1 页。</source>
        <translation>This document has %2 pages in all; there is no page %1.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="144"/>
        <source>这些页在前面的修改里已经删掉了，这里没有可以移动的。</source>
        <translation>These pages are already gone in the changes that have not been saved, so there is nothing here to move.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="146"/>
        <source>第 %1 页就在要移动的页里，这样移等于没移。</source>
        <translation>Page %1 is one of the pages being moved, so moving it behind itself changes nothing.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="148"/>
        <source>这些页已经在那个位置上了，这样移不会把它们挪动。</source>
        <translation>Those pages are already in that place, so this move would not shift them.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="153"/>
        <source>把 %1 页移到%2：移完它们连着排在第 %3-%4 页。</source>
        <translation>Moves %1 pages %2: they end up together at pages %3-%4.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="160"/>
        <source>

第 %1 页在还没保存的修改里已经删掉了，所以是排在第 %2 页前面。</source>
        <translation>

Page %1 is already gone in the changes that have not been saved, so they land before page %2.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="163"/>
        <source>

第 %1 页在还没保存的修改里已经删掉了，而它后面没有页了，所以放到最后。</source>
        <translation>

Page %1 is already gone in the changes that have not been saved, and no page is behind it, so they go to the end.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="168"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="182"/>
        <source>第 %1 页之后</source>
        <translation>behind page %1</translation>
    </message>
</context>
<context>
    <name>OcrDialog</name>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="16"/>
        <source>文字识别</source>
        <translation>Text Recognition</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="32"/>
        <source>识别出的文字会出现在这里。</source>
        <translation>The recognized text appears here.</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="36"/>
        <source>复制文字</source>
        <translation>Copy Text</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="44"/>
        <source>复制图片</source>
        <translation>Copy Image</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="56"/>
        <source>翻译</source>
        <translation>Translate</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="62"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="101"/>
        <source>正在识别框选区域内的文字…</source>
        <translation>Recognizing the text in the box…</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="112"/>
        <source>识别失败。</source>
        <translation>Recognition failed.</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="121"/>
        <source>框选的区域里没有识别出文字。</source>
        <translation>Nothing was recognized in the boxed area.</translation>
    </message>
    <message>
        <location filename="../../src/ui/ocrdialog.cpp" line="127"/>
        <source>识别完成，可以复制或翻译：</source>
        <translation>Recognized. Copy it or translate it:</translation>
    </message>
</context>
<context>
    <name>PageView</name>
    <message>
        <location filename="../../src/view/pageview.cpp" line="958"/>
        <source>复制(&amp;C)</source>
        <translation>Copy(&amp;C)</translation>
    </message>
    <message>
        <location filename="../../src/view/pageview.cpp" line="959"/>
        <source>翻译(&amp;T)</source>
        <translation>Translate(&amp;T)</translation>
    </message>
</context>
<context>
    <name>PrintDialog</name>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="22"/>
        <location filename="../../src/ui/printdialog.cpp" line="97"/>
        <source>打印</source>
        <translation>Print</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="32"/>
        <source>页面范围</source>
        <translation>Pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="35"/>
        <source>全部（%1 页）</source>
        <translation>All (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="39"/>
        <source>当前页（第 %1 页）</source>
        <translation>Current page (page %1)</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="43"/>
        <source>自定义</source>
        <translation>Custom</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="45"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="66"/>
        <source>排版</source>
        <translation>Layout</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="70"/>
        <source>适应可打印区域</source>
        <translation>Fit to the printable area</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="71"/>
        <source>实际大小（可能裁切）</source>
        <translation>Actual size (may be cropped)</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="72"/>
        <source>缩放</source>
        <translation>Scaling</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="76"/>
        <source>%1 页/张</source>
        <translation>%1 pages per sheet</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="77"/>
        <source>每页版数</source>
        <translation>Pages per sheet</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="86"/>
        <location filename="../../src/ui/printdialog.cpp" line="113"/>
        <source>打印机：（默认）</source>
        <translation>Printer: (default)</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="87"/>
        <location filename="../../src/ui/printdialog.cpp" line="114"/>
        <source>打印机：%1</source>
        <translation>Printer: %1</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="90"/>
        <source>选择打印机 / 份数 / 双面…</source>
        <translation>Choose printer / copies / duplex…</translation>
    </message>
    <message>
        <location filename="../../src/ui/printdialog.cpp" line="98"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="45"/>
        <source>没有要导出的页面。</source>
        <translation>There are no pages to export.</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="49"/>
        <source>没有指定导出的文件夹或文件名。</source>
        <translation>No folder or file name was given to export into.</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="55"/>
        <source>无法创建导出文件夹：%1</source>
        <translation>The export folder could not be created: %1</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="63"/>
        <source>导出已取消。</source>
        <translation>The export was cancelled.</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="70"/>
        <source>第 %1 页无法读取尺寸，导出中止。</source>
        <translation>Page %1 could not be measured, and the export stopped.</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="87"/>
        <source>第 %1 页渲染失败，导出中止。</source>
        <translation>Page %1 could not be rendered, and the export stopped.</translation>
    </message>
    <message>
        <location filename="../../src/export/imageexport.cpp" line="93"/>
        <source>无法写入 %1。</source>
        <translation>%1 could not be written.</translation>
    </message>
    <message>
        <location filename="../../src/print/printjob.cpp" line="99"/>
        <source>这份文档没有可打印的页面。</source>
        <translation>This document has no pages to print.</translation>
    </message>
    <message>
        <location filename="../../src/print/printjob.cpp" line="116"/>
        <source>页码范围里没有可打印的页面。</source>
        <translation>The page range has no pages to print.</translation>
    </message>
    <message>
        <location filename="../../src/print/printjob.cpp" line="123"/>
        <source>打印机的可打印区域无效。</source>
        <translation>The printer&apos;s printable area is not usable.</translation>
    </message>
    <message>
        <location filename="../../src/print/printjob.cpp" line="135"/>
        <source>无法开始打印任务。</source>
        <translation>The print job could not be started.</translation>
    </message>
    <message>
        <location filename="../../src/print/printjob.cpp" line="143"/>
        <source>打印已取消。</source>
        <translation>Printing was cancelled.</translation>
    </message>
</context>
<context>
    <name>QtPdfBackend</name>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="163"/>
        <source>此文档已加密，需要密码。</source>
        <translation>This document is encrypted and needs a password.</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="166"/>
        <source>这个文件不是有效的 PDF，或者已经损坏。</source>
        <translation>This file is not a valid PDF, or it is damaged.</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="173"/>
        <source>这个 PDF 使用了不支持的加密方式，无法打开。</source>
        <translation>This PDF uses an encryption method that is not supported.</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="176"/>
        <source>找不到文件：%1</source>
        <translation>File not found: %1</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="179"/>
        <source>文件还没有读完，无法打开。</source>
        <translation>The file could not be read to the end.</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="182"/>
        <source>打开 %1 时出错：%2</source>
        <translation>Error opening %1: %2</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="183"/>
        <source>文件无法解析</source>
        <translation>the file cannot be parsed</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="184"/>
        <source>文件不存在</source>
        <translation>the file is not there</translation>
    </message>
    <message>
        <location filename="../../src/document/qtpdfbackend.cpp" line="193"/>
        <source>%1 没有读完，无法打开。</source>
        <translation>%1 could not be read to the end.</translation>
    </message>
</context>
<context>
    <name>RotatePagesDialog</name>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="24"/>
        <source>旋转页面</source>
        <translation>Rotate Pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="30"/>
        <source>转向</source>
        <translation>Direction</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="32"/>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="125"/>
        <source>向右转 90°</source>
        <translation>Turn right 90°</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="34"/>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="122"/>
        <source>旋转 180°</source>
        <translation>Rotate 180°</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="35"/>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="124"/>
        <source>向左转 90°</source>
        <translation>Turn left 90°</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="41"/>
        <source>要旋转的页面</source>
        <translation>Pages to rotate</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="44"/>
        <source>当前页（第 %1 页）</source>
        <translation>Current page (page %1)</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="48"/>
        <source>指定页码</source>
        <translation>These page numbers</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="51"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="76"/>
        <source>旋转这些页</source>
        <translation>Rotate these pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="77"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="97"/>
        <source>还没有说出要转哪几页。</source>
        <translation>No pages have been named yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="99"/>
        <source>这些页在前面的修改里已经删掉了，没有可以转的。</source>
        <translation>Those pages were already taken out by the earlier changes, so there is nothing left to rotate.</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="104"/>
        <source>%1。说好的 %2 页里有 %3 页还在，只转那 %3 页。</source>
        <translation>%1. Of the %2 pages named, %3 are still there, and only those %3 will be turned.</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="109"/>
        <source>%1：%2 页。</source>
        <translation>%1: %2 pages.</translation>
    </message>
    <message>
        <location filename="../../src/ui/rotatepagesdialog.cpp" line="112"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="37"/>
        <source>设置</source>
        <translation>Settings</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="53"/>
        <source>外观</source>
        <translation>Appearance</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="57"/>
        <source>浅色</source>
        <translation>Light</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="58"/>
        <source>深色</source>
        <translation>Dark</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="60"/>
        <source>主题</source>
        <translation>Theme</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="63"/>
        <location filename="../../src/settings/settingsdialog.cpp" line="285"/>
        <source>语言</source>
        <translation>Language</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="78"/>
        <source>阅读</source>
        <translation>Reading</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="83"/>
        <source> MB</source>
        <translation> MB</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="85"/>
        <source>页面缓存上限</source>
        <translation>Page cache limit</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="88"/>
        <source>已渲染的页面留在内存里，超过上限时最久没看的那一页被丢弃。</source>
        <translation>Rendered pages are kept in memory. Past the limit, the one that has been looked at least recently is dropped.</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="103"/>
        <source>翻译</source>
        <translation>Translation</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="107"/>
        <source>谷歌（免费，无需密钥）</source>
        <translation>Google (free, no key needed)</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="108"/>
        <source>DeepL（需密钥）</source>
        <translation>DeepL (needs a key)</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="111"/>
        <source>翻译服务</source>
        <translation>Translation service</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="116"/>
        <source>仅 DeepL 需要</source>
        <translation>Only DeepL needs one</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="117"/>
        <source>API 密钥</source>
        <translation>API key</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="120"/>
        <source>选中后手动（右键菜单 / 工具栏）</source>
        <translation>By hand after selecting (context menu / toolbar)</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="121"/>
        <source>选中即自动翻译</source>
        <translation>Translate as soon as something is selected</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="124"/>
        <source>触发方式</source>
        <translation>Trigger</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="127"/>
        <source>翻译会把选中的文字发送到所选服务。密钥以明文保存在本机设置中。</source>
        <translation>Translating sends the selected text to the service chosen. The key is kept in plain text in this machine&apos;s settings.</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="153"/>
        <source>最近文件</source>
        <translation>Recent documents</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="156"/>
        <source>记住我打开过的文档</source>
        <translation>Remember the documents I open</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="159"/>
        <source>清除列表</source>
        <translation>Clear the list</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="162"/>
        <source>关掉这一项会同时清空已经记下的列表。列表只存在本机。</source>
        <translation>Turning this off clears what has already been recorded. The list stays on this machine.</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="185"/>
        <location filename="../../src/settings/settingsdialog.cpp" line="206"/>
        <source>资源管理器</source>
        <translation>Explorer</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="188"/>
        <source>在右键菜单里显示“用 XYZPDF 打开”</source>
        <translation>Show “Open with XYZPDF” in the context menu</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="220"/>
        <source>确定</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="221"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="256"/>
        <location filename="../../src/settings/settingsdialog.cpp" line="280"/>
        <source>跟随系统</source>
        <translation>Follow the system</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="263"/>
        <source>已写入当前用户的注册表（不需要管理员权限）。Windows 11 上它出现在“显示更多选项”里。</source>
        <translation>Written to the current user’s registry, with no administrator rights needed. On Windows 11 it appears under “Show more options”.</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="265"/>
        <source>只写入当前用户，不影响其他账户，也不需要管理员权限。</source>
        <translation>Written for the current user only: it does not affect other accounts and needs no administrator rights.</translation>
    </message>
    <message>
        <location filename="../../src/settings/settingsdialog.cpp" line="285"/>
        <source>语言更改将在下次启动时生效。</source>
        <translation>The language change takes effect the next time the program starts.</translation>
    </message>
</context>
<context>
    <name>ShellIntegration</name>
    <message>
        <location filename="../../src/windows/shellintegration.cpp" line="212"/>
        <source>找不到 XYZPDF 程序文件：%1</source>
        <translation>Could not find the XYZPDF program: %1</translation>
    </message>
    <message>
        <location filename="../../src/windows/shellintegration.cpp" line="250"/>
        <source>用 XYZPDF 打开</source>
        <translation>Open with XYZPDF</translation>
    </message>
    <message>
        <location filename="../../src/windows/shellintegration.cpp" line="261"/>
        <source>无法写入注册表：%1</source>
        <translation>Could not write to the registry: %1</translation>
    </message>
    <message>
        <location filename="../../src/windows/shellintegration.cpp" line="269"/>
        <source>PDF 文档</source>
        <translation>PDF document</translation>
    </message>
</context>
<context>
    <name>ThirdPartyDialog</name>
    <message>
        <location filename="../../src/ui/thirdpartydialog.cpp" line="81"/>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;许可证：%2&lt;br&gt;随包文件：%3&lt;br&gt;源码：&lt;a href=&quot;%4&quot;&gt;%4&lt;/a&gt;&lt;br&gt;%5</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Licence: %2&lt;br&gt;Shipped file: %3&lt;br&gt;Source: &lt;a href=&quot;%4&quot;&gt;%4&lt;/a&gt;&lt;br&gt;%5</translation>
    </message>
</context>
<context>
    <name>ThirdPartyNotices</name>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="20"/>
        <source>界面框架与 PDF 渲染接口。以动态库形式随包分发，未做修改。</source>
        <translation>The interface framework and the PDF rendering API. Shipped as dynamic libraries, unmodified.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="26"/>
        <source>v1 的渲染引擎：页面光栅化、文本提取、书签与链接都由它完成。由 Qt PDF 编入 Qt6Pdf.dll，未做修改。</source>
        <translation>The v1 rendering engine: rasterising pages, extracting text, and reading bookmarks and links. Built into Qt6Pdf.dll by Qt PDF, unmodified.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="34"/>
        <source>结构编辑器：删除页面后的文档由它写出。以动态库形式随包分发，未做修改。z.dll 与 jpeg62.dll 是它自带的依赖（zlib、libjpeg），一并动态链接。</source>
        <translation>The structural editor: it writes out the document once pages have been taken out. Shipped in the package as dynamic libraries, unmodified. z.dll and jpeg62.dll are its own dependencies (zlib, libjpeg) and are linked dynamically alongside it.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="42"/>
        <source>Qt 自身内含 zlib、libpng、freetype、harfbuzz、libjpeg-turbo 等库，随 Qt 的二进制库一并分发。它们的许可证不影响 XYZPDF 自身代码的授权。</source>
        <translation>Qt carries zlib, libpng, freetype, harfbuzz, libjpeg-turbo and others, distributed with Qt’s own binaries. Their licences do not affect the licensing of XYZPDF’s own code.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="51"/>
        <source>许可证文本缺失：%1</source>
        <translation>Licence text missing: %1</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.h" line="46"/>
        <source>第三方开源许可</source>
        <translation>Third-party licences</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.h" line="53"/>
        <source>XYZPDF 使用了下列开源组件。其中 Qt（含其中的 PDFium）以 LGPL 授权，它编译为独立的动态库，XYZPDF 通过接口调用，并未与之静态链接——你可以用自行修改的版本替换它们。

下方列出每个组件的许可证与源码地址。许可证全文可在此查看。</source>
        <translation>XYZPDF uses the open source components listed below. Qt, which contains PDFium, is under the LGPL: it is built as a separate dynamic library that XYZPDF calls through its interface rather than linking statically, so you may replace it with a version you have modified yourself.

Each component’s licence and source address are listed below, and the full text of each licence can be read here.</translation>
    </message>
</context>
<context>
    <name>TranslateDialog</name>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="14"/>
        <source>翻译</source>
        <translation>Translation</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="26"/>
        <source>原文</source>
        <translation>Original</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="35"/>
        <source>译文</source>
        <translation>Translation</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="39"/>
        <source>译文会出现在这里。</source>
        <translation>The translation appears here.</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="43"/>
        <source>复制译文</source>
        <translation>Copy Translation</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="48"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="56"/>
        <source>正在翻译…</source>
        <translation>Translating…</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="69"/>
        <source>翻译失败。</source>
        <translation>The translation failed.</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="74"/>
        <source>译文（识别为 %1）：</source>
        <translation>Translation (language detected as %1):</translation>
    </message>
    <message>
        <location filename="../../src/ui/translatedialog.cpp" line="74"/>
        <source>未知语言</source>
        <translation>unknown language</translation>
    </message>
</context>
<context>
    <name>WrappingToolBarArea</name>
    <message>
        <location filename="../../src/ui/wrappingtoolbararea.cpp" line="32"/>
        <location filename="../../src/ui/wrappingtoolbararea.cpp" line="56"/>
        <source>展开工具栏，显示放不下的按钮</source>
        <translation>Expand the toolbar and show the buttons that did not fit</translation>
    </message>
    <message>
        <location filename="../../src/ui/wrappingtoolbararea.cpp" line="56"/>
        <source>收起工具栏</source>
        <translation>Collapse the toolbar</translation>
    </message>
</context>
</TS>
