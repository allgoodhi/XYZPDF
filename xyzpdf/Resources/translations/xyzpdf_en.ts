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
        <location filename="../../src/ui/deletepagesdialog.cpp" line="105"/>
        <source>

这 %1 页里有 %2 页是合并进来的，屏幕上没有它们的页码，这里删不到它们。</source>
        <translation>

%2 of those %1 pages came in from another file: the screen has no page number for them, so this dialog cannot reach them.</translation>
    </message>
    <message>
        <location filename="../../src/ui/deletepagesdialog.cpp" line="114"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
</context>
<context>
    <name>DocumentEditor</name>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="359"/>
        <source>旋转 180°（%1 页）</source>
        <translation>Rotate 180° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="361"/>
        <source>向左转 90°（%1 页）</source>
        <translation>Turn left 90° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="363"/>
        <source>向右转 90°（%1 页）</source>
        <translation>Turn right 90° (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="370"/>
        <source>移到最前（%1 页）</source>
        <translation>Move to the front (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="372"/>
        <source>移到第 %2 页之后（%1 页）</source>
        <translation>Move after page %2 (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="373"/>
        <source>移到最后（%1 页）</source>
        <translation>Move to the end (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="380"/>
        <source>合入 %2 页（%1）</source>
        <translation>Merge in %2 pages (%1)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="382"/>
        <source>另一份文件</source>
        <translation>another file</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="390"/>
        <source>拆成每 %1 页一份</source>
        <translation>Split into pieces of %1 pages</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="399"/>
        <location filename="../../tests/i18n_smoke.cpp" line="363"/>
        <source>去掉口令</source>
        <translation>Remove password</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="402"/>
        <source>只改权限（不设打开口令）</source>
        <translation>Change the permissions only (no opening password)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="403"/>
        <source>加上打开口令（AES-256）</source>
        <translation>Add an opening password (AES-256)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="405"/>
        <source>按原来的口令写出去</source>
        <translation>Write it out with the original password</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="412"/>
        <source>加水印「%2」（%1 页）</source>
        <translation>Watermark &quot;%2&quot; (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="414"/>
        <source>删除 %1 页</source>
        <translation>Delete %1 pages</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="831"/>
        <source>这份文档正在阅读中，但结构编辑用的解析器打不开它。

解析器说：%1</source>
        <translation>This document is open for reading, but the parser used for structural editing cannot open it.

The parser says: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="838"/>
        <source>这份 PDF 一页也没有，没法在上面做任何页面修改。</source>
        <translation>This PDF has no pages at all, so no page change can be made to it.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="889"/>
        <source>%1 要用口令才能打开，不知道口令的文档合不进来。

想合并它，先把它本身打开一次，或者去掉它的口令。</source>
        <translation>%1 opens only with its password, and a document whose password is not known cannot be merged in.

To merge it, open it on its own once first, or remove its password.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="892"/>
        <location filename="../../src/edit/documenteditor.cpp" line="895"/>
        <source>%1 打不开，所以合不进来。

解析器说：%2</source>
        <translation>%1 cannot be opened, so it cannot be merged in.

The parser says: %2</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="903"/>
        <source>%1 一页也没有，没什么可合并的。</source>
        <translation>%1 has no pages at all, so there is nothing to merge from it.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="914"/>
        <location filename="../../src/edit/documenteditor.cpp" line="949"/>
        <location filename="../../src/edit/documenteditor.cpp" line="981"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1031"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1119"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1159"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1184"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1217"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1598"/>
        <source>还没有打开任何文档。</source>
        <translation>No document has been opened yet.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="923"/>
        <location filename="../../src/edit/documenteditor.cpp" line="995"/>
        <source>那些页在还没保存的修改里已经删掉了。</source>
        <translation>Those pages are already gone in the changes that have not been saved.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="924"/>
        <source>页码范围里没有可删除的页面。</source>
        <translation>The page range names no pages that can be deleted.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="928"/>
        <source>不能把每一页都删掉：一个不剩的 PDF 不是 PDF，任何阅读器都打不开它。想丢掉整份文档，就把文件本身删掉。</source>
        <translation>Every page cannot be deleted: a PDF with no pages is not a PDF, and no reader can open it. To get rid of the whole document, delete the file itself.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="956"/>
        <source>旋转只能是 90° 的整倍数。</source>
        <translation>A rotation can only be a whole multiple of 90°.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="965"/>
        <source>页码范围里没有可旋转的页面。</source>
        <translation>The page range names no pages that can be rotated.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="989"/>
        <source>要说移到第几页之后，而那个页码不在这份文档里。</source>
        <translation>It has to say which page to move behind, and that number is not a page in this document.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="996"/>
        <source>页码范围里没有可移动的页面。</source>
        <translation>The page range names no pages that can be moved.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1000"/>
        <source>第 %1 页就在要移动的页里，这样移等于没移。</source>
        <translation>Page %1 is one of the pages being moved, so moving it behind itself changes nothing.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1007"/>
        <source>那些页已经在最前头了。</source>
        <translation>Those pages are already at the front.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1008"/>
        <source>那些页已经在最后了。</source>
        <translation>Those pages are already at the end.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1009"/>
        <source>那些页已经在第 %1 页后面了。</source>
        <translation>Those pages are already after page %1.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1041"/>
        <location filename="../../tests/i18n_smoke.cpp" line="374"/>
        <source>这份文档已经说了要拆成每 %1 页一份。先撤销那一步，再把文件合并进来。</source>
        <translation>This document has already been told to split into pieces of %1 pages. Undo that step before merging a file in.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1049"/>
        <source>要说合并到第几页之后，而那个页码不在这份文档里。</source>
        <translation>It has to say which page to merge behind, and that number is not a page in this document.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1065"/>
        <source>%1 一共 %2 页，没有第 %3 页。</source>
        <translation>%1 has %2 pages in all; there is no page %3.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1074"/>
        <source>还没有说出要合并 %1 的哪几页。</source>
        <translation>Nothing has said which pages of %1 to merge in.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1099"/>
        <source>没有页可以合并进来。</source>
        <translation>There are no pages to merge in.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1126"/>
        <source>这份要写出的文档里有 %1 页是合并进来的，而那些页没有你在屏幕上能看到的页号，所以哪几页该进哪一份是说不出来的。先撤销那几次合并再拆。</source>
        <translation>%1 pages of the document this would write came in from another file, and those pages have no page number you can see on the screen, so which piece each of them belongs in cannot be said. Undo those merges before splitting.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1134"/>
        <source>每份至少要有 1 页。</source>
        <translation>A piece has to hold at least 1 page.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1141"/>
        <source>这份要写出的文档一共 %1 页，每份 %2 页只会写出一份，那就不用拆了。</source>
        <translation>The document this would write has %1 pages in all, and %2 pages a piece writes one piece -- so there is nothing to split.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1174"/>
        <source>这份要写出的文件已经是这个口令和这些权限了，再要一次不会多改什么。</source>
        <translation>The file this would write already carries this password and these permissions; asking again would change nothing more.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1190"/>
        <source>这份要写出的文件已经说过要去掉口令了，再要一次不会多去掉什么。</source>
        <translation>The file this would write has already been told to drop its password; asking again would remove nothing more.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1199"/>
        <source>正在打开的这份文件本身没有口令，也没有权限限制，所以没有东西可去掉。</source>
        <translation>The file being opened has no password and no permission limits of its own, so there is nothing to remove.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1225"/>
        <source>还没有说水印要写什么字。</source>
        <translation>Nothing has been said about what the watermark should say.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1234"/>
        <source>不透明度只能是 1% 到 100% 之间的整数。</source>
        <translation>The opacity can only be a whole number between 1% and 100%.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1247"/>
        <source>页码范围里没有可加水印的页面。</source>
        <translation>The page range names no pages that can be watermarked.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1248"/>
        <source>那些页在还没保存的修改里已经删掉了，没有可以加水印的。</source>
        <translation>Those pages are already gone in the changes that have not been saved, so there is nothing left to watermark.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1258"/>
        <source>这份要写出的文件已经带着一模一样的水印了，再要一次不会多改什么。</source>
        <translation>The file this would write already carries exactly this watermark; asking again would change nothing more.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1368"/>
        <location filename="../../tests/i18n_smoke.cpp" line="354"/>
        <source>，</source>
        <translation>, </translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1489"/>
        <source>这一页的内容流不是能往上加东西的样子，所以水印没有画上去。</source>
        <translation>This page&apos;s content stream is not a shape that anything can be added to, so the watermark was not drawn.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1519"/>
        <location filename="../../src/edit/documenteditor.cpp" line="1524"/>
        <source>要合并的那一页在这份文件里找不到。</source>
        <translation>The page to merge in cannot be found in that file.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1646"/>
        <source>拆出来的文件里有一个（%1）就是正在打开的这份。请换一个文件名。</source>
        <translation>One of the split-out files (%1) is the document that is open right now. Please choose another file name.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1648"/>
        <source>另存为不能覆盖正在打开的文件。请换一个文件名。</source>
        <translation>Save As cannot overwrite the file that is open. Please choose another name.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1658"/>
        <source>另存为不能覆盖合并进来的文件（%1）。请换一个文件名。</source>
        <translation>Save As cannot overwrite a file that was merged in (%1). Please choose another file name.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1911"/>
        <source>拆出来的 %1 个文件里有 %2 个没能放到位。放到位的那些已经在文件夹里了，下面每一条说的是剩下的该去哪儿找。</source>
        <translation>%2 of the %1 split-out files could not be put in place. The ones that made it are already in the folder; each line below says where to find the rest.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1606"/>
        <source>这些修改会把每一页都删掉，而一个不剩的 PDF 任何阅读器都打不开。</source>
        <translation>These changes would delete every page, and no reader can open a PDF with none left.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1613"/>
        <source>文件夹不存在：%1</source>
        <translation>The folder does not exist: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1669"/>
        <source>%1 那里已经有一个文件了。拆分不会盖掉已经存在的文件，请换一个文件名，或者先把那个文件挪走。</source>
        <translation>There is already a file at %1. Splitting does not overwrite a file that exists: please choose another file name, or move that file away first.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1684"/>
        <source>这份文档里既有合并进来的页，又说了要拆分，这两样凑不成一份读者能核对的文件。请先撤销其中一步。</source>
        <translation>This document holds merged-in pages and has been told to split, and those two do not add up to a file a reader can check. Undo one of those steps first.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1726"/>
        <source>%1 这时候打不开了，所以这些修改写不出来。原来的文件没有改动。

解析器说：%2</source>
        <translation>%1 cannot be opened any more, so these changes cannot be written out. The original files are not touched.

The parser says: %2</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1737"/>
        <source>%1 在这些修改记下来之后变了：当时是 %2 页，现在只有 %3 页。请撤销那次合并，重新选一遍页码。</source>
        <translation>%1 changed after these edits were recorded: it had %2 pages then and only %3 now. Undo that merge and choose the page numbers again.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1778"/>
        <source>这些修改在内存里是一种页序，写到文件时成了另一种，所以这一份没有写。请把这种情况报上来。</source>
        <translation>These edits held one page order in memory and another in the file they would have written, so nothing was written. Please report this.</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1850"/>
        <source>写出文件时出错，原来的文件没有改动。

解析器说：%1</source>
        <translation>Writing the file failed, and the original file was not changed.

The parser says: %1</translation>
    </message>
    <message>
        <location filename="../../src/edit/documenteditor.cpp" line="1892"/>
        <source>新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，或者这个名字不允许覆盖（Windows 错误 %2）。写好的文件留在 %3。</source>
        <translation>The new file was written, but it could not be put in place at %1 — another program may be holding it, or this name may not be replaced (Windows error %2). The finished file has been left at %3.</translation>
    </message>
</context>
<context>
    <name>DocumentTab</name>
    <message>
        <location filename="../../documenttab.cpp" line="111"/>
        <source>大纲</source>
        <translation>Outline</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="112"/>
        <source>缩略图</source>
        <translation>Thumbnails</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="675"/>
        <source>（空标签）</source>
        <translation>(no document)</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1040"/>
        <source>打开链接</source>
        <translation>Open Link</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1041"/>
        <source>要在浏览器中打开这个地址：
%1

继续吗？</source>
        <translation>Open this address in the browser?
%1

Continue?</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1066"/>
        <source>正在打印…</source>
        <translation>Printing…</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1066"/>
        <location filename="../../documenttab.cpp" line="1112"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1084"/>
        <source>打印</source>
        <translation>Print</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1098"/>
        <location filename="../../documenttab.cpp" line="1103"/>
        <location filename="../../documenttab.cpp" line="1132"/>
        <source>导出为图片</source>
        <translation>Export as Images</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1098"/>
        <source>请先选择保存文件夹。</source>
        <translation>Choose a folder to save into first.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1103"/>
        <source>页码范围里没有可导出的页面。</source>
        <translation>The page range has no pages to export.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1112"/>
        <source>正在导出为图片…</source>
        <translation>Exporting images…</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1150"/>
        <source>修改这份文档</source>
        <translation>Editing this document</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1191"/>
        <source>旋转页面</source>
        <translation>Rotate Pages</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1210"/>
        <source>移动页面</source>
        <translation>Move Pages</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1249"/>
        <source>

列表里前面 %1 个文件已经记下了，要退回去用撤销。</source>
        <translation>

The first %1 files in the list have been recorded; use Undo to take them back.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1250"/>
        <source>合并文件进来</source>
        <translation>Merge Files In</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1265"/>
        <location filename="../../documenttab.cpp" line="1277"/>
        <source>拆分文件</source>
        <translation>Split Files</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1266"/>
        <source>这份要写出的文档只有 %1 页，拆不出第二个文件来。</source>
        <translation>The document this would write has only %1 pages, so there is no second file to split off.</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1303"/>
        <location filename="../../tests/i18n_smoke.cpp" line="366"/>
        <source>加密 / 去掉口令</source>
        <translation>Encrypt / Remove Password</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1324"/>
        <location filename="../../tests/i18n_smoke.cpp" line="382"/>
        <source>加水印</source>
        <translation>Add Watermark</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1382"/>
        <source>%1 已修改.pdf</source>
        <translation>%1 modified.pdf</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1387"/>
        <source>把这些修改存成一个新文件</source>
        <translation>Save these changes into a new file</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1404"/>
        <source>保存修改</source>
        <translation>Save Changes</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1387"/>
        <source>PDF 文档 (*.pdf)</source>
        <translation>PDF documents (*.pdf)</translation>
    </message>
    <message>
        <location filename="../../documenttab.cpp" line="1173"/>
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
        <location filename="../../mainwindow.cpp" line="1148"/>
        <location filename="../../tests/i18n_smoke.cpp" line="134"/>
        <location filename="../../tests/i18n_smoke.cpp" line="145"/>
        <source>适应宽度</source>
        <translation>Fit Width</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="724"/>
        <location filename="../../mainwindow.cpp" line="1149"/>
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
        <location filename="../../mainwindow.cpp" line="1423"/>
        <source>已写成一个新文件（%1 页）：%2</source>
        <translation>Wrote a new file (%1 pages): %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="984"/>
        <source>转到(&amp;G)</source>
        <translation>&amp;Go</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="986"/>
        <source>第一页</source>
        <translation>First Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="990"/>
        <source>上一页</source>
        <translation>Previous Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="994"/>
        <source>下一页</source>
        <translation>Next Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="998"/>
        <source>最后一页</source>
        <translation>Last Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1006"/>
        <source>跳转到页码…</source>
        <translation>Go to Page…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1016"/>
        <source>窗口(&amp;N)</source>
        <translation>Window(&amp;N)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1018"/>
        <source>合并所有窗口(&amp;G)</source>
        <translation>Merge All Windows(&amp;G)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1020"/>
        <source>把其它窗口里打开的文档全部并进这个窗口</source>
        <translation>Bring every document open in the other windows into this one</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1036"/>
        <source>跟随系统</source>
        <translation>Follow the system</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1055"/>
        <source>帮助(&amp;H)</source>
        <translation>&amp;Help</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1057"/>
        <source>设置(&amp;P)…</source>
        <translation>&amp;Settings…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1062"/>
        <source>第三方开源许可</source>
        <translation>Third-party licences</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1066"/>
        <source>关于 %1</source>
        <translation>About %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1097"/>
        <source>主工具栏</source>
        <translation>Main toolbar</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1105"/>
        <source>翻页</source>
        <translation>Pages</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1114"/>
        <source>页码</source>
        <translation>Page number</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1134"/>
        <source>缩放与旋转</source>
        <translation>Zoom and Rotate</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1142"/>
        <source>缩放</source>
        <translation>Zoom</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1144"/>
        <location filename="../../mainwindow.cpp" line="2219"/>
        <location filename="../../mainwindow.cpp" line="2246"/>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="273"/>
        <location filename="../../mainwindow.cpp" line="278"/>
        <location filename="../../mainwindow.cpp" line="298"/>
        <location filename="../../mainwindow.cpp" line="313"/>
        <location filename="../../mainwindow.cpp" line="1292"/>
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
        <location filename="../../mainwindow.cpp" line="877"/>
        <source>合并文件进来(&amp;F)…</source>
        <translation>Merge Files In(&amp;F)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="883"/>
        <source>记下要把哪几页并进来，按保存修改为才写成一个新文件；正在读的这份和合并进来的那些都不动</source>
        <translation>Notes down which pages to merge in. Save Changes As is what writes them into a new file; neither the document you are reading nor the files merged in are touched.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="895"/>
        <source>拆分文件(&amp;C)…</source>
        <translation>Split Files(&amp;C)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="902"/>
        <source>记下要拆成每几页一份，按保存修改为才一次写出那几个文件；正在读的这份不动</source>
        <translation>Notes down how many pages a piece should hold. Save Changes As is what writes those files, all of them at once; the document you are reading is not touched.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="912"/>
        <source>加密 / 去掉口令(&amp;J)…</source>
        <translation>Encrypt / Remove Password(&amp;P)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="920"/>
        <source>记下要写出的那一份需要什么口令、允许做什么，按保存修改为才写成一个新文件；正在读的这份不动，那些权限限制也只是写给阅读器看的请求</source>
        <translation>Notes down what password and what permissions the file a save writes should carry. Save Changes As is what writes them into a new file; the document you are reading is not touched, and those permissions are only a request the file makes to whatever opens it.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="931"/>
        <source>加水印(&amp;W)…</source>
        <translation>Add Watermark(&amp;W)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="938"/>
        <source>记下要在哪几页上盖什么字，按保存修改为才写成一个新文件；正在读的这份不动，那些字是画在页面上的一张图，复制、翻译、导出都拿不到它们</source>
        <translation>Notes down what words go on which pages. Save Changes As is what writes them into a new file; the document you are reading is not touched, and those words are a picture drawn on the page, so copying, translating and exporting cannot reach them.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="953"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="961"/>
        <source>重做</source>
        <translation>Redo</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="971"/>
        <source>保存修改为(&amp;S)…</source>
        <translation>Save Changes As(&amp;S)…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="976"/>
        <source>把这些修改写成一个新文件；正在读的这份不动</source>
        <translation>Writes these changes into a new file. The document you are reading stays as it is.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1194"/>
        <source>查找</source>
        <translation>Find</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1198"/>
        <source>在文档中查找</source>
        <translation>Find in the document</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1200"/>
        <source>输入即搜索；Enter 到下一个，Shift+Enter 到上一个，Esc 清除</source>
        <translation>Typing searches; Enter goes to the next, Shift+Enter to the previous, Esc clears</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1228"/>
        <source>当前是第几个匹配，一共几个</source>
        <translation>Which match is in front, and how many there are</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1239"/>
        <source>第 1234 / 1234 个</source>
        <translation>1234 of 1234</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1265"/>
        <source>搜索中…</source>
        <translation>Searching…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1270"/>
        <source>没有找到</source>
        <translation>No matches</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1275"/>
        <source>第 %1 / %2 个</source>
        <translation>%1 of %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1309"/>
        <source>找不到文件：%1</source>
        <translation>File not found: %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1388"/>
        <location filename="../../mainwindow.cpp" line="1668"/>
        <source>正在导出为 Word…（%1 / %2 页）</source>
        <translation>Exporting to Word… (page %1 of %2)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1398"/>
        <source>已导出 %1 张图片到 %2</source>
        <translation>Exported %1 images to %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1422"/>
        <source>已写成 %1 个新文件（整份 %2 页）：%3</source>
        <translation>Wrote %1 new files (%2 pages in all): %3</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1500"/>
        <source>已打开 %1</source>
        <translation>Opened %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1514"/>
        <source>无法打开 %1。

%2</source>
        <translation>Could not open %1.

%2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1535"/>
        <source>需要密码</source>
        <translation>Password required</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1536"/>
        <source>%1 已加密，请输入打开密码：</source>
        <translation>%1 is encrypted. Enter the password it opens with:</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1547"/>
        <source>密码不正确，已放弃打开。</source>
        <translation>The password was not correct, so the document was left closed.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1557"/>
        <source>打开 PDF</source>
        <translation>Open PDF</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1557"/>
        <source>PDF 文档 (*.pdf);;所有文件 (*)</source>
        <translation>PDF documents (*.pdf);;All files (*)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1576"/>
        <location filename="../../mainwindow.cpp" line="1701"/>
        <location filename="../../mainwindow.cpp" line="1746"/>
        <source>导出为 Word</source>
        <translation>Export to Word</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1577"/>
        <source>Word 文档 (*.docx)</source>
        <translation>Word document (*.docx)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1583"/>
        <source>正在导出为 Word…</source>
        <translation>Exporting to Word…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1599"/>
        <source>导出为 Word — %1</source>
        <translation>Export to Word — %1</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1600"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1635"/>
        <source>正在停止…</source>
        <translation>Stopping…</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1669"/>
        <source>正在导出为 Word…（共 %1 页）</source>
        <translation>Exporting to Word… (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1695"/>
        <source>导出已取消</source>
        <translation>Export cancelled</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1700"/>
        <source>导出失败</source>
        <translation>Export failed</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1705"/>
        <source>已导出 %1 页、%2 个段落到：
%3</source>
        <translation>Exported %1 pages and %2 paragraphs to:
%3</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1712"/>
        <source>其中 %1 个带网格的表格已转换为可编辑 Word 表格。</source>
        <translation>Of these, %1 ruled tables were converted into editable Word tables.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1720"/>
        <source>其中 %1 处图形（图表、框图、照片等）已按图片插入，其所在页面的文字仍是可以编辑的。</source>
        <translation>%1 figures (charts, diagrams, photographs) were inserted as images; the text on their pages stays editable.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1731"/>
        <source>其中 %1 页没有文字层或包含复杂图表/表格，已按整页图片还原以保持原貌，但页内的文字不能直接编辑。需要可编辑的文字可以先用“识别”工具。</source>
        <translation>%1 pages had no text layer, or held charts and tables too involved to rebuild; they were reproduced as full-page images to keep their appearance, so the text in them cannot be edited directly. To get editable text out of one, run the Recognize tool first.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1741"/>
        <source>另有 %1 页既没有文字，也没有渲染出内容，已跳过。</source>
        <translation>Another %1 pages had neither text nor any rendered content, and were skipped.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1745"/>
        <source>已导出为 Word</source>
        <translation>Exported to Word</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1763"/>
        <location filename="../../mainwindow.cpp" line="1785"/>
        <source>还有没保存的修改</source>
        <translation>Changes have not been written</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1764"/>
        <source>「%1」还带着没有写进任何文件的修改：%2。

关掉这个标签就会把这些修改丢掉。要关掉吗？</source>
        <translation>&quot;%1&quot; still carries changes that are written in no file: %2.

Closing this tab throws those changes away. Close it anyway?</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1786"/>
        <source>这个窗口里有 %1 个标签带着没有写进任何文件的修改。

关掉窗口就会把这些修改丢掉。要关掉吗？</source>
        <translation>%1 tabs in this window carry changes that are written in no file.

Closing the window throws those changes away. Close it anyway?</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1974"/>
        <source>没有其他窗口可以合并</source>
        <translation>There are no other windows to merge</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="1990"/>
        <source>已把 %1 个窗口的 %2 个标签并入当前窗口</source>
        <translation>Merged %2 tabs from %1 windows into this window</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2152"/>
        <source>撤销(&amp;U)</source>
        <translation>Undo(&amp;U)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2152"/>
        <source>撤销 %1(&amp;U)</source>
        <translation>Undo %1(&amp;U)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2158"/>
        <source>重做(&amp;O)</source>
        <translation>Redo(&amp;O)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2158"/>
        <source>重做 %1(&amp;O)</source>
        <translation>Redo %1(&amp;O)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2245"/>
        <location filename="../../tests/i18n_smoke.cpp" line="139"/>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2264"/>
        <source>%1 — %2</source>
        <translation>%1 — %2</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2283"/>
        <source>（没有）</source>
        <translation>(none)</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2289"/>
        <source>清除列表</source>
        <translation>Clear the list</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2305"/>
        <source>语言更改将在下次启动时生效。</source>
        <translation>The language change takes effect the next time the program starts.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2405"/>
        <location filename="../../mainwindow.cpp" line="2413"/>
        <source>跳转到页码</source>
        <translation>Go to Page</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2406"/>
        <source>页码（1 - %1）：</source>
        <translation>Page number (1 - %1):</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2414"/>
        <source>页码要在 1 到 %1 之间。</source>
        <translation>The page number has to be between 1 and %1.</translation>
    </message>
    <message>
        <location filename="../../mainwindow.cpp" line="2444"/>
        <source>打印将在后续版本中提供。</source>
        <translation>Printing arrives in a later version.</translation>
    </message>
</context>
<context>
    <name>MergePagesDialog</name>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="61"/>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="196"/>
        <source>合并文件进来</source>
        <translation>Merge Files In</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="67"/>
        <source>要合并进来的文件</source>
        <translation>Files to merge in</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="72"/>
        <source>文件</source>
        <translation>File</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="72"/>
        <source>一共</source>
        <translation>Total</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="72"/>
        <source>要合入的页码</source>
        <translation>Pages to merge in</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="96"/>
        <source>添加文件…</source>
        <translation>Add Files…</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="97"/>
        <source>移除</source>
        <translation>Remove</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="98"/>
        <source>上移</source>
        <translation>Move Up</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="99"/>
        <source>下移</source>
        <translation>Move Down</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="107"/>
        <source>双击“要合入的页码”可以改，例如 1-3,5,8-10；那一列写的是那个文件自己的页码。</source>
        <translation>Double-click a “Pages to merge in” cell to change it, for example 1-3,5,8-10; those are the page numbers of that file itself.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="115"/>
        <source>合到哪儿</source>
        <translation>Merge them</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="117"/>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="471"/>
        <source>最前面</source>
        <translation>to the front</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="118"/>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="477"/>
        <source>最后面</source>
        <translation>to the end</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="128"/>
        <source>指定页之后</source>
        <translation>behind page</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="133"/>
        <source>页码</source>
        <translation>Page number</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="150"/>
        <source>合并这些文件</source>
        <translation>Merge these files</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="151"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="188"/>
        <source>要合并进来的 PDF</source>
        <translation>PDFs to merge in</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="188"/>
        <source>PDF 文档 (*.pdf)</source>
        <translation>PDF documents (*.pdf)</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="225"/>
        <source>%1 页</source>
        <translation>%1 pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="391"/>
        <source>%1 还没有说出要合入哪几页。</source>
        <translation>Nothing has said which pages of %1 to merge in yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="396"/>
        <source>还没有加上要合并进来的文件。</source>
        <translation>No file has been added to merge in yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="407"/>
        <location filename="../../tests/i18n_smoke.cpp" line="371"/>
        <source>这份文档已经说了要拆成每 %1 页一份。先撤销那一步，再把文件合并进来。</source>
        <translation>This document has already been told to split into pieces of %1 pages. Undo that step before merging a file in.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="412"/>
        <source>要说合并到第几页之后。</source>
        <translation>It has to say which page to merge behind.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="414"/>
        <source>这份文档一共 %2 页，没有第 %1 页。</source>
        <translation>This document has %2 pages in all; there is no page %1.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="416"/>
        <source>没有页可以合并进来。</source>
        <translation>There are no pages to merge in.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="418"/>
        <source>%1 个文件一共取 %2 页，合到%3，按列表的顺序连着排在第 %4-%5 页。</source>
        <translation>Merges %2 pages from %1 files %3: in the order of the list they end up together at pages %4-%5.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="428"/>
        <source>

第 %1 页在还没保存的修改里已经删掉了，所以是排在它后面那页前面。</source>
        <translation>

Page %1 is already gone in the changes that have not been saved, so they land before the page that was behind it.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="432"/>
        <source>

这份文档里已经有 %1 页是合并进来的了。</source>
        <translation>

%1 pages of this document have already been merged in.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="438"/>
        <source>

合并进来的页没有屏幕上的页码：要改它们或把它们取回去，只能撤销那次合并。</source>
        <translation>

Merged-in pages have no page number on the screen: the only way to change them or take them back out is to undo the merge that brought them.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="442"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
    <message>
        <location filename="../../src/ui/mergepagesdialog.cpp" line="473"/>
        <source>第 %1 页之后</source>
        <translation>behind page %1</translation>
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
        <location filename="../../src/ui/movepagesdialog.cpp" line="183"/>
        <source>最前面</source>
        <translation>to the front</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="65"/>
        <location filename="../../src/ui/movepagesdialog.cpp" line="189"/>
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
        <location filename="../../src/ui/movepagesdialog.cpp" line="111"/>
        <source>移动这些页</source>
        <translation>Move these pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="112"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="141"/>
        <source>还没有说出要移动哪几页。</source>
        <translation>Nothing has said which pages to move yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="143"/>
        <source>要说移到第几页之后。</source>
        <translation>It has to say which page to move behind.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="145"/>
        <source>这份文档一共 %2 页，没有第 %1 页。</source>
        <translation>This document has %2 pages in all; there is no page %1.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="147"/>
        <source>这些页在前面的修改里已经删掉了，这里没有可以移动的。</source>
        <translation>These pages are already gone in the changes that have not been saved, so there is nothing here to move.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="149"/>
        <source>第 %1 页就在要移动的页里，这样移等于没移。</source>
        <translation>Page %1 is one of the pages being moved, so moving it behind itself changes nothing.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="151"/>
        <source>这些页已经在那个位置上了，这样移不会把它们挪动。</source>
        <translation>Those pages are already in that place, so this move would not shift them.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="156"/>
        <source>把 %1 页移到%2：移完它们连着排在第 %3-%4 页。</source>
        <translation>Moves %1 pages %2: they end up together at pages %3-%4.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="163"/>
        <source>

第 %1 页在还没保存的修改里已经删掉了，所以是排在第 %2 页前面。</source>
        <translation>

Page %1 is already gone in the changes that have not been saved, so they land before page %2.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="166"/>
        <source>

第 %1 页在还没保存的修改里已经删掉了，而它后面没有页了，所以放到最后。</source>
        <translation>

Page %1 is already gone in the changes that have not been saved, and no page is behind it, so they go to the end.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="171"/>
        <source>%1

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>%1

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
    <message>
        <location filename="../../src/ui/movepagesdialog.cpp" line="185"/>
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
        <location filename="../../src/view/pageview.cpp" line="986"/>
        <source>复制(&amp;C)</source>
        <translation>Copy(&amp;C)</translation>
    </message>
    <message>
        <location filename="../../src/view/pageview.cpp" line="987"/>
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
    <name>ProtectPagesDialog</name>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="22"/>
        <location filename="../../tests/i18n_smoke.cpp" line="365"/>
        <source>加密 / 去掉口令</source>
        <translation>Encrypt / Remove Password</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="34"/>
        <source>要做的事</source>
        <translation>What to do</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="36"/>
        <source>加上口令和权限</source>
        <translation>Add a password and permissions</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="37"/>
        <source>去掉口令和权限</source>
        <translation>Remove the password and permissions</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="45"/>
        <source>打开口令</source>
        <translation>Opening password</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="48"/>
        <source>口令</source>
        <translation>Password</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="57"/>
        <source>再输一遍</source>
        <translation>Type it again</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="67"/>
        <source>允许的用途（这些限制写在文件里，不是强制的）</source>
        <translation>Allowed uses (these limits are written into the file, not enforced by it)</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="71"/>
        <source>允许打印</source>
        <translation>Allow printing</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="72"/>
        <source>允许复制文字和图片</source>
        <translation>Allow copying text and images</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="73"/>
        <source>允许添加或修改批注</source>
        <translation>Allow adding or changing annotations</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="74"/>
        <source>允许填写表单</source>
        <translation>Allow filling in forms</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="75"/>
        <source>允许插入、删除或旋转页面</source>
        <translation>Allow inserting, deleting or rotating pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="76"/>
        <source>允许其它修改</source>
        <translation>Allow other changes</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="111"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="166"/>
        <source>前面已经记下了一次去掉口令，写出的那一份本来就不带口令，再要一次不会多去掉什么。要退回去用撤销。</source>
        <translation>Removing the password is already recorded, and the file this would write asks for none anyway; asking again would remove nothing more. Use Undo to go back.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="170"/>
        <source>正在打开的这份文件没有口令，也没有写下任何权限限制，所以这里没有东西可去掉。</source>
        <translation>The file being opened has no password and wrote down no permission limits either, so there is nothing here to remove.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="173"/>
        <source>写出的那一份不需要口令就能打开，也不再写下这些权限限制。</source>
        <translation>The file this writes opens without a password, and it no longer writes down these permission limits.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="175"/>
        <source>

原来那份文件是锁着的：它要的口令对写出的这一份不再管用。</source>
        <translation>

The original file is locked: the password it asks for no longer applies to the file this writes.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="177"/>
        <source>

前面记下的那一次加密会被这一步顶掉。</source>
        <translation>

This replaces the encryption recorded earlier.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="181"/>
        <source>写出的这一份要输入口令才能打开（AES-256，也就是 PDF 里的 R6）。</source>
        <translation>The file this writes opens only after the password is typed (AES-256, which is PDF’s R6).</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="182"/>
        <source>口令那一栏空着，所以写出的这一份不需要口令就能打开 —— 这里留下的只有下面那六项限制。</source>
        <translation>The password box was left empty, so the file this writes opens without one — all that survives here are the six limits below.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="185"/>
        <source>

原来那份文件也有口令。写出的这一份只认这里重新填的这一个：这里没有把它原来的口令填进来，把那个口令显示出来正是加密要挡住的事。</source>
        <translation>

The original file has a password too, and the one this writes accepts only the one typed here. The original was not filled in for you: putting that password on screen is the very thing the lock is there to stop.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="187"/>
        <source>

原来那份文件是要口令才能打开的，写出的这一份不要 —— 那个口令就此不管用了。</source>
        <translation>

The original file opens only with a password, and the one this writes does not — that password stops working.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="194"/>
        <source>

两次输入的口令不一样。</source>
        <translation>

The two passwords are not the same.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="200"/>
        <source>

这跟前面已经记下的一条一模一样，按下不会多改什么 —— 要改就改一个勾，或者换一个口令。</source>
        <translation>

This is exactly what is already recorded, so pressing it would change nothing more — change one of the boxes, or choose another password.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="204"/>
        <source>

这会把前面记下的那一条顶掉；撤销一次回到那一个。</source>
        <translation>

This replaces the entry recorded earlier; one Undo goes back to it.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="209"/>
        <source>

那六项是写在文件里的一份请求：别的阅读器照不照办都打得开这份文件，任何一个能打开它的程序也都能把这些限制去掉。只有口令是真的锁 —— 丢了没有办法找回来。</source>
        <translation>

The six are a request written into the file: another reader opens this file whether or not it obeys them, and any program that can open it can take the limits off again. Only the password is a real lock — once it is lost there is no way back.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="211"/>
        <source>

这是 PDF 里较新的 R6（AES-256），比 Acrobat X 更早的阅读器打不开这种文件。</source>
        <translation>

This is PDF’s newer R6 (AES-256): readers older than Acrobat X cannot open a file like this.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="213"/>
        <source>

前面合并进来的那些页也会一起锁上：口令管的是整份文件，不只是屏幕上看到的这些页。</source>
        <translation>

The pages merged in earlier are locked with the rest: a password covers the whole file, not only the pages on the screen.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="218"/>
        <source>

原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，去掉口令的那一份才会出现。</source>
        <translation>

The original file is not touched, and this step writes nothing either: the file without its password only appears when Save Changes As is pressed.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="220"/>
        <source>

原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，带上这些的那一份才会出现。</source>
        <translation>

The original file is not touched, and this step writes nothing either: the file that carries all this only appears when Save Changes As is pressed.</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="224"/>
        <location filename="../../tests/i18n_smoke.cpp" line="362"/>
        <source>去掉口令</source>
        <translation>Remove password</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="225"/>
        <source>加密</source>
        <translation>Encrypt</translation>
    </message>
    <message>
        <location filename="../../src/ui/protectpagesdialog.cpp" line="226"/>
        <source>只改权限</source>
        <translation>Change permissions only</translation>
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
    <name>SplitPagesDialog</name>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="20"/>
        <source>拆分文件</source>
        <translation>Split Files</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="30"/>
        <source>每</source>
        <translation>One file every</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="43"/>
        <source> 页一份</source>
        <translation> pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="54"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="77"/>
        <source>这份文档没有可以拆的页面。</source>
        <translation>This document has no pages to split.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="78"/>
        <source>确定</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="99"/>
        <source>会写成 %1 份，每份最多 %2 页：%3。</source>
        <translation>Writes %1 files of at most %2 pages each: %3.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="102"/>
        <location filename="../../tests/i18n_smoke.cpp" line="352"/>
        <source>、</source>
        <translation>, </translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="110"/>
        <source>

文件名是从保存时填的那个名字切出来的，例如第一份会叫「%1」。</source>
        <translation>

The names are cut from the one you type at the save -- the first file would be called &quot;%1&quot;.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="113"/>
        <source>

这些页号是照着要写出的那一份数的（%1 页），不是屏幕上那一份（%2 页）——前面的修改没有改到界面。</source>
        <translation>

Those page numbers count the document a save would write (%1 pages), not the one on the screen (%2): the earlier changes left the view alone.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="119"/>
        <source>

可是这 %1 页里有 %2 页是合并进来的，屏幕上没有它们的页号，所以哪几页该进哪一份是说不出来的。先撤销那几次合并再拆。</source>
        <translation>

But %2 of those %1 pages came in from another file and have no page number on the screen, so which pages belong in which file cannot be said. Undo those merges before splitting.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="125"/>
        <source>

这会把前面记下的「每 %1 页一份」换掉；撤销一次回到那一个。</source>
        <translation>

This replaces the &quot;one file every %1 pages&quot; recorded earlier; one Undo goes back to it.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="133"/>
        <source>

这要写 %1 个文件，每个都要把整份文档重读一遍，会等一会儿。</source>
        <translation>

This writes %1 files, and every one of them reads the whole document again, so it takes a while.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="138"/>
        <source>

原来那份文件不会改动，这一步也不写任何文件——要等按下保存修改为，这 %1 个文件才会一起出现。</source>
        <translation>

The original file is not touched, and this step writes nothing either: those %1 files only appear together when Save Changes As is pressed.</translation>
    </message>
    <message>
        <location filename="../../src/ui/splitpagesdialog.cpp" line="143"/>
        <source>拆成 %1 份</source>
        <translation>Split into %1 files</translation>
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
        <location filename="../../src/core/thirdpartynotices.cpp" line="26"/>
        <source>界面框架与 PDF 渲染接口。以动态库形式随包分发，未做修改。</source>
        <translation>The interface framework and the PDF rendering API. Shipped as dynamic libraries, unmodified.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="28"/>
        <source>（编入 Qt6Pdf.dll）</source>
        <translation>(built into Qt6Pdf.dll)</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="32"/>
        <source>v1 的渲染引擎：页面光栅化、文本提取、书签与链接都由它完成。由 Qt PDF 编入 Qt6Pdf.dll，未做修改。</source>
        <translation>The v1 rendering engine: rasterising pages, extracting text, and reading bookmarks and links. Built into Qt6Pdf.dll by Qt PDF, unmodified.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="40"/>
        <source>结构编辑器：删除页面后的文档由它写出。以动态库形式随包分发，未做修改。z.dll 与 jpeg62.dll 是它自带的依赖（zlib、libjpeg），一并动态链接。</source>
        <translation>The structural editor: it writes out the document once pages have been taken out. Shipped in the package as dynamic libraries, unmodified. z.dll and jpeg62.dll are its own dependencies (zlib, libjpeg) and are linked dynamically alongside it.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="43"/>
        <source>Qt 内置的第三方库</source>
        <translation>Third-party libraries inside Qt</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="44"/>
        <source>（编入 Qt6Gui.dll、Qt6Core.dll）</source>
        <translation>(built into Qt6Gui.dll, Qt6Core.dll)</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="45"/>
        <source>各自的原许可证，见许可证全文</source>
        <translation>their own original licences, see the full licence text</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="48"/>
        <source>Qt 自身内含 zlib、libpng、freetype、harfbuzz、libjpeg-turbo 等库，随 Qt 的二进制库一并分发。它们的许可证不影响 XYZPDF 自身代码的授权。</source>
        <translation>Qt carries zlib, libpng, freetype, harfbuzz, libjpeg-turbo and others, distributed with Qt’s own binaries. Their licences do not affect the licensing of XYZPDF’s own code.</translation>
    </message>
    <message>
        <location filename="../../src/core/thirdpartynotices.cpp" line="57"/>
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
    <name>WatermarkDialog</name>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="24"/>
        <location filename="../../tests/i18n_smoke.cpp" line="383"/>
        <source>加水印</source>
        <translation>Add Watermark</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="35"/>
        <source>水印文字</source>
        <translation>Watermark text</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="39"/>
        <source>例如 机密、作废、内部资料</source>
        <translation>For example Confidential, Void, Internal use</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="43"/>
        <source>不透明度</source>
        <translation>Opacity</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="64"/>
        <source>要盖水印的页面</source>
        <translation>Pages to watermark</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="67"/>
        <source>全部（%1 页）</source>
        <translation>All (%1 pages)</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="72"/>
        <source>指定页码</source>
        <translation>These page numbers</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="74"/>
        <source>例如 1-3,5,8-10</source>
        <translation>For example 1-3,5,8-10</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="100"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="163"/>
        <source>还没有说要盖什么字。</source>
        <translation>No words have been named yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="165"/>
        <source>还没有说出要盖哪几页。</source>
        <translation>No pages have been named yet.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="167"/>
        <source>这些页在前面的修改里已经删掉了，没有可以盖的。</source>
        <translation>Those pages were already taken out by the earlier changes, so there is nothing left to stamp.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="169"/>
        <source>「%1」，%2% 不透明度。说好的 %3 页里有 %4 页还在，只盖那 %4 页。</source>
        <translation>&quot;%1&quot; at %2% opacity. Of the %3 pages named, %4 are still there, and only those %4 will be stamped.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="175"/>
        <source>「%1」，%2% 不透明度，盖在 %3 页上。</source>
        <translation>&quot;%1&quot; at %2% opacity, on %3 pages.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="185"/>
        <source>

这些字是画上去的一张图，不是文档里的文字：选中、复制、翻译、导出都只会碰到原来的字，看不到这行水印。</source>
        <translation>

Those words are a picture drawn on the page, not text in the document: selecting, copying, translating and exporting only ever reach the words underneath, never this stamp.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="190"/>
        <source>

前面合并进来的那 %1 页屏幕上没有页号，所以不在这 %2 页里，也不会带上水印。</source>
        <translation>

But the %1 pages that came in from another file have no page number on the screen, so they are not among those %2 and carry no watermark either.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="204"/>
        <source>

这跟前面已经记下的一条一模一样，按下不会多改什么 —— 要改就换一个词，或者动一下不透明度。</source>
        <translation>

This is exactly what is already recorded, so pressing it would change nothing more — choose another word, or move the opacity.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="207"/>
        <source>

这会把前面记下的「%1」换掉；撤销一次回到那一个。</source>
        <translation>

This replaces the &quot;%1&quot; recorded earlier; one Undo goes back to it.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="214"/>
        <source>

原来那份文件不会改动。这一步只是记下修改，要它成为一个文件，还得走保存。</source>
        <translation>

The original file is not touched. This step only records the change; a save is still what turns it into a file.</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="217"/>
        <source>盖这 %1 页</source>
        <translation>Stamp these %1 pages</translation>
    </message>
    <message>
        <location filename="../../src/ui/watermarkdialog.cpp" line="217"/>
        <source>确定</source>
        <translation>OK</translation>
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
