// The catalogues the build claims to have, and the ones it actually carries.
//
// The rule is that a language is offered only when its catalogue is in the
// binary -- offering one and then showing Chinese is worse than not offering it
// at all. That rule is only true if the resource path in languages.cpp matches
// what the build actually puts in the binary, and nothing about a mismatch is
// visible until a user picks a language and nothing happens.
//
// The catalogues are compiled from the .ts files by qt_add_translations and
// attached to this target as well as to the program, which is what makes this
// test able to ask the question at all. Qt's own Chinese catalogue is listed in
// resources.qrc for the same reason.

#include "../src/core/languages.h"
#include "../src/core/thirdpartynotices.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QTranslator>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

bool contains(const QList<Languages::Entry> &entries, const QString &code)
{
    for (const Languages::Entry &entry : entries) {
        if (entry.code == code)
            return true;
    }
    return false;
}

// Whether a string still has Chinese in it. Used to check a translation without
// pinning the wording: an English catalogue that translates a string into
// Chinese is not a translation, whatever it says.
bool hasChinese(const QString &text)
{
    for (const QChar &character : text) {
        const ushort code = character.unicode();
        if (code >= 0x3400 && code <= 0x9FFF) // the CJK ideograph blocks
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // ---- the table ----
    {
        const QList<Languages::Entry> known = Languages::known();
        check(!known.isEmpty(), "there is at least one language");
        check(contains(known, QStringLiteral("zh_CN")), "Chinese is one of them");
        check(contains(known, QStringLiteral("en")), "and so is English");
        for (const Languages::Entry &entry : known) {
            check(!entry.nativeName.isEmpty(),
                  QStringLiteral("%1 has a name to show in a picker").arg(entry.code));
        }
    }

    // ---- the source language needs no catalogue ----
    //
    // The source strings in this program are Chinese, so Chinese is what tr()
    // returns untranslated. It must never be looked for on disk: a build with
    // no catalogue for it would otherwise decide Chinese is unavailable and
    // show English to somebody who asked for Chinese.
    {
        check(Languages::hasCatalogue(QStringLiteral("zh_CN")),
              "Chinese is available without a catalogue");
        check(!Languages::catalogueResource(QStringLiteral("zh_CN")).isEmpty(),
              "though the resource path is still well formed");
    }

    // ---- what this build actually carries ----
    {
        const QList<Languages::Entry> available = Languages::available();
        check(contains(available, QStringLiteral("zh_CN")), "Chinese is on offer");
        check(contains(available, QStringLiteral("en")), "English is on offer");

        for (const Languages::Entry &entry : available) {
            const QString path = Languages::catalogueResource(entry.code);
            if (entry.code == QStringLiteral("zh_CN"))
                continue;
            check(QFile::exists(path),
                  QStringLiteral("the catalogue for %1 is in the binary (%2)")
                      .arg(entry.code, path));
            QTranslator translator;
            check(translator.load(path),
                  QStringLiteral("and %1 loads as a catalogue").arg(entry.code));
        }
    }

    // ---- Qt's own catalogue ----
    //
    // Without it the built-in widgets answer "OK" and "Cancel" in English
    // inside an otherwise Chinese interface, which is what a user notices
    // first.
    {
        const QString path = QStringLiteral(":/translations/qtbase_zh_CN.qm");
        check(QFile::exists(path), "Qt's Chinese catalogue is embedded");
        QTranslator translator;
        check(translator.load(path), "and it loads");
    }

    // ---- a catalogue that actually translates ----
    //
    // Loading is not translating: a .ts with nothing filled in still compiles
    // to a .qm that loads, and English would then be offered in the Language
    // menu with the interface staying Chinese -- which is the failure the rule
    // about "only offer what is translated" exists for. Neither sentence below
    // pins the wording, so reworded translations do not fail the test.
    {
        QTranslator translator;
        check(translator.load(Languages::catalogueResource(QStringLiteral("en"))),
              "the English catalogue loads");
        QCoreApplication::installTranslator(&translator);

        const QString fitted = QCoreApplication::translate("MainWindow", "适应宽度");
        check(fitted != QStringLiteral("适应宽度"),
              "a string in the English catalogue comes back translated");
        check(!hasChinese(fitted), "and comes back in English");

        const QString placeholders = QCoreApplication::translate("MainWindow", "%1 / %2");
        check(placeholders.contains(QStringLiteral("%1"))
                  && placeholders.contains(QStringLiteral("%2")),
              "and a string that is only placeholders keeps them");

        QCoreApplication::removeTranslator(&translator);
        check(QCoreApplication::translate("MainWindow", "适应宽度") == QStringLiteral("适应宽度"),
              "and with the catalogue gone the source string is back");
    }

    // ---- every feature added since the catalogue was last refreshed ----
    //
    // The gap this closes is silent: lupdate adds a new source string as an empty
    // unfinished entry, the .qm still compiles, the .ts still loads, and English
    // shows Chinese for that one string -- one at a time, wherever a reader happens
    // to look. Checking a handful of the newest strings is what turns "the
    // catalogue exists" into "the catalogue covers the program".
    {
        QTranslator translator;
        check(translator.load(Languages::catalogueResource(QStringLiteral("en"))),
              "the English catalogue loads for the sweep");
        QCoreApplication::installTranslator(&translator);

        struct Probe {
            const char *context;
            const char *source;
            // >= 0 asks the plural-aware form for that count. A numerus entry has no
            // translation of its own to check -- what has to hold is that the two forms
            // exist and differ, so `n` says which one to look at and the loop below asks
            // for the other one too. -1 is the ordinary, non-plural lookup.
            int n = -1;
        };
        const Probe probes[] = {
            { "MainWindow", "夜间阅读(&G)" },      { "MainWindow", "全屏(&L)" },
            { "MainWindow", "演示(&P)" },          { "MainWindow", "合并所有窗口(&G)" },
            { "MainWindow", "书籍模式" },          { "MainWindow", "导出为图片(&G)…" },
            { "MainWindow", "松开以在此窗口合并 %1 个文档" },
            { "MainWindow", "已把 %1 个窗口的 %2 个标签并入当前窗口" },
            { "DocumentTab", "缩略图" },           { "DocumentTab", "正在导出为图片…" },
            { "ImageExportDialog", "分辨率" },     { "ImageExportDialog", "保存到文件夹" },
            { "PrintDialog", "每页版数" },         { "PrintDialog", "适应可打印区域" },
            { "TranslateDialog", "复制译文" },     { "OcrDialog", "文字识别" },
            { "OcrDialog", "复制文字" },           { "OcrDialog", "复制图片" },
            { "SettingsDialog", "翻译服务" },      { "FindBar", "区分大小写" },
            // Strings one feature at a time, and each one written out in full:
            // translate() matches on the whole source, so a probe that is only a
            // prefix of its sentence would report the source back and read as an
            // untranslated string. That is why none of these is one of the two
            // sentences with a blank line inside it -- those carry the build's
            // line endings, which is a worse thing to pin in a test than a long
            // string is.
            { "MainWindow", "文档(&D)" },          { "MainWindow", "删除页面(&E)…" },
            { "MainWindow", "旋转页面(&R)…" },     { "MainWindow", "保存(&S)" },
            { "MainWindow", "另存为(&A)…" },
            { "MainWindow", "还有没保存的修改" },
            { "MainWindow", "已写成一个新文件（%n 页）：%1", 1 },
            { "MainWindow", "这个窗口里有 %n 个标签带着没有写进任何文件的修改。", 1 },
            { "MainWindow", "正在导出为 Word…（共 %n 页）", 1 },
            { "MainWindow", "另有 %n 页既没有文字，也没有渲染出内容，已跳过。", 1 },
            { "DocumentTab", "要另存为一份新的吗？" },
            { "DocumentTab", "另存为…" },
            { "DocumentTab", "%1 已修改.pdf" },
            { "DeletePagesDialog", "当前页（第 %1 页）" },
            { "DeletePagesDialog", "现在这份有 %1 页：再删 %2 页，剩下 %3 页。" },
            { "DeletePagesDialog", "还没有说出要删哪几页。" },
            // The rotation dialog and the names the undo menu is built out of:
            // both are newer than the sweep above, and the second is the one that
            // a free helper function can drop out of the catalogue without any
            // build step noticing.
            { "RotatePagesDialog", "旋转这些页" },
            { "RotatePagesDialog", "%1。说好的 %2 页里有 %n 页还在，只转那 %n 页。", 1 },
            { "DocumentEditor", "删除 %n 页", 1 },
            { "DocumentEditor", "向右转 90°（%n 页）", 1 },
            { "DocumentEditor", "向左转 90°（%n 页）", 1 },
            { "DocumentEditor", "旋转 180°（%n 页）", 1 },
            { "DocumentEditor", "另存为不能覆盖正在打开的文件。请换一个文件名。" },
            // §79's neighbour, and the same refusal family: the name is readable, the save simply
            // cannot replace it while something holds it open. Listed beside the two it completes --
            // overwriting this tab's own file, and overwriting a merge donor -- because those are
            // the three ways a destination can already be spoken for.
            { "DocumentEditor",
              "%1 现在盖不掉：它正被读着（可能是别的程序，也可能是这个程序的另一个窗口）。"
              "请换一个文件名，或者先关掉正在读它的那一个。" },
            { "DocumentEditor", "还没有打开任何文档。" },
            // The reorder that came after those two, and the undo/redo item names --
            // those four are here because the sweep found its first real defect
            // through them: the 文档 menu had been hand-written into the catalogue
            // with its mnemonic escaped twice, so the English UI read
            // "Rotate Pages(&amp;R)" on screen. Six entries were like that: rotate,
            // save-as, and these four. Nothing above catches it -- it is neither
            // Chinese nor the source string -- which is why the check beside it
            // exists and why these are probed by name.
            { "MainWindow", "移动页面(&M)…" },
            { "MainWindow", "撤销(&U)" },           { "MainWindow", "重做(&O)" },
            { "MainWindow", "撤销 %1(&U)" },        { "MainWindow", "重做 %1(&O)" },
            { "MovePagesDialog", "指定页之后" },
            { "MovePagesDialog", "还没有说出要移动哪几页。" },
            // The landing sentence is two, because one page moved is not a range: the box
            // that says 它们连着 / "they end up together" over a single page is the same
            // mistake as printing 5-5. Both halves are probed by name, since a wording that
            // branches on a number has two strings to keep translated.
            { "MovePagesDialog", "把 1 页移到%1：移完它就排在第 %2 页。" },
            { "MovePagesDialog", "把 %1 页移到%2：移完它们连着排在第 %3 页。" },
            { "DocumentEditor", "移到最前（%n 页）", 1 },
            { "DocumentEditor", "移到最后（%n 页）", 1 },
            // This one had no probe at all before §32 (the row is in the catalogue, and
            // nothing had ever asked it): the anchor number in the middle makes it the
            // longest of the move rows and the easiest one to word apart from its own
            // dialog, which is exactly what a probe by name is for.
            { "DocumentEditor", "移到第 %1 页之后（%n 页）", 1 },
            { "DocumentEditor", "那些页已经在第 %1 页后面了。" },
            // The merge that came after the reorder: the menu row, the two words a
            // row of the file list is made of, the sentence the summary ends with,
            // and the name the undo item is built from. The last of these four is
            // the one this sweep exists for -- a free helper in DocumentEditor
            // assembles it, and nothing in the build would notice if its context
            // were spelled differently from the class.
            { "MainWindow", "合并文件进来(&F)…" },
            { "MergePagesDialog", "要合入的页码" },
            { "MergePagesDialog", "还没有加上要合并进来的文件。" },
            { "MergePagesDialog", "%1 个文件一共取 1 页，合到%2，按列表的顺序排在第 %3 页。" },
            // The plural half of that pair is probed below, with `%n` and a count of 2 -- the
            // file count is what the English agrees with there, since a landing run of one
            // page cannot reach this branch (sec.31).
            { "DocumentEditor", "合入 %n 页（%1）", 1 },
            { "DocumentEditor", "另存为不能覆盖合并进来的文件（%1）。请换一个文件名。" },
            // The split after the merge, and the same four kinds of it: the menu row,
            // the dialog's own words, the undo item's name, and the status line the
            // save finishes with -- that last one is a second literal beside the first
            // because a split's page count is the whole document's, and "一个文件"
            // would be a lie about what came out.
            { "MainWindow", "拆分文件(&C)…" },
            { "MainWindow", "已写成 %1 个新文件（整份 %2 页）：%3" },
            { "SplitPagesDialog", "拆成 %1 份" },
            { "SplitPagesDialog", "会写成 %1 份，每份最多 %n 页：%2。", 1 },
            { "SplitPagesDialog", "这份文档没有可以拆的页面。" },
            { "DocumentEditor", "拆成每 %n 页一份", 1 },
            { "DocumentEditor", "这份文档已经说了要拆成每 %n 页一份。先撤销那一步，再把文件合并进来。", 1 },
            // The same sentence now also stands in the merge box itself, which since §20's
            // note about it used to light 合并这些文件 and refuse only after the click.
            // Probed in both contexts because it is in both contexts: one Chinese string,
            // one English, and the check further down that the two agree.
            { "MergePagesDialog", "这份文档已经说了要拆成每 %n 页一份。先撤销那一步，再把文件合并进来。", 1 },
            // The two separators the code joins reader-facing text with: the range list
            // in the split dialog and the list of pending operations. They went into the
            // English interface straight out of a QStringLiteral -- "1-21、22-42" on
            // screen -- and none of the checks below can see that, because a CJK comma is
            // neither an ideograph nor the source string of a sentence.
            { "SplitPagesDialog", "、" },
            { "DocumentEditor", "，" },
            // 加密 / 去掉口令, and the two refusals that come with it. The menu row is
            // probed first because it is the one entry here with a mnemonic in it, which is
            // how §15's six double-escaped entries reached the screen; the rest of the list
            // is the dialog's own vocabulary, since a reader who cannot read "Allow
            // inserting, deleting or rotating pages" has no way to know what the six boxes
            // are worth.
            { "MainWindow", "加密 / 去掉口令(&J)…" },
            { "MainWindow",
              "记下要写出的那一份需要什么口令、允许做什么，按保存才写进正在读的这份文件；"
              "那些权限限制也只是写给阅读器看的请求" },
            { "DocumentTab", "加密 / 去掉口令" },
            { "ProtectPagesDialog", "加密 / 去掉口令" },
            { "ProtectPagesDialog", "加上口令和权限" },
            { "ProtectPagesDialog", "去掉口令和权限" },
            { "ProtectPagesDialog", "打开口令" },
            { "ProtectPagesDialog", "再输一遍" },
            { "ProtectPagesDialog", "允许的用途（这些限制写在文件里，不是强制的）" },
            { "ProtectPagesDialog", "允许插入、删除或旋转页面" },
            { "ProtectPagesDialog", "允许复制文字和图片" },
            { "ProtectPagesDialog", "只改权限" },
            { "ProtectPagesDialog",
              "口令那一栏空着，所以写出的这一份不需要口令就能打开 —— 这里留下的只有"
              "下面那六项限制。" },
            { "ProtectPagesDialog", "写出的这一份要输入口令才能打开（AES-256，也就是 PDF 里的 R6）。" },
            { "DocumentEditor", "去掉口令" },
            { "DocumentEditor", "只改权限（不设打开口令）" },
            { "DocumentEditor", "加上打开口令（AES-256）" },
            { "DocumentEditor", "正在打开的这份文件本身没有口令，也没有权限限制，所以没有东西可去掉。" },
            // The third-party table's own columns, which is a different kind of entry from
            // everything above: this context had never been probed at all, and that is how
            // four of its cells stayed QStringLiteral and reached an English interface in
            // Chinese (the loop after the probes is the check that catches the class).
            // Naming them anyway, because a row someone reworded into a different context
            // is a row nothing else would notice.
            { "ThirdPartyNotices", "Qt 内置的第三方库" },
            { "ThirdPartyNotices", "界面框架。以动态库形式随包分发，未做修改。" },
            { "ThirdPartyNotices",
              "渲染引擎：页面光栅化、文本提取、书签与链接都由它完成。"
              "以动态库形式随包分发，未做修改；它自己内含的第三方组件的许可证都在同一份文本里。" },
            { "ThirdPartyNotices", "（编入 Qt6Gui.dll、Qt6Core.dll）" },
            { "ThirdPartyNotices", "各自的原许可证，见许可证全文" },
            // 加水印: the seventh entry in 文档, and the only one of the seven whose effect is
            // something a reader sees on the page rather than something the file says about
            // itself. The last of these is probed for a reason of its own: that sentence was
            // first thrown from a free function, where lupdate sees no class to read as a
            // context, so the string did not come back unfinished -- it came back absent, with
            // nothing in the build to say so. A probe that names it is what turns "no entry in
            // the catalogue" into a red test, which is the §22 hole again in the other
            // direction: not a missing translation but a missing question.
            { "MainWindow", "加水印(&W)…" },
            { "MainWindow",
              "记下要在哪几页上盖什么字，按保存才写进正在读的这份文件；"
              "那些字是画在页面上的一张图，复制、翻译、导出都拿不到它们" },
            { "DocumentTab", "加水印" },
            { "WatermarkDialog", "加水印" },
            { "WatermarkDialog", "水印文字" },
            { "WatermarkDialog", "不透明度" },
            { "WatermarkDialog", "要盖水印的页面" },
            { "WatermarkDialog", "还没有说要盖什么字。" },
            { "WatermarkDialog", "「%1」，%2% 不透明度，盖在 %n 页上。", 1 },
            { "WatermarkDialog", "盖这 %n 页", 1 },
            // The two clauses the box adds once something is already on the list. They carry a
            // leading blank line in the source because they are appended to a summary, and the
            // key has to be that exact text -- which is also why they are written out here
            // rather than reached by a loop over labels nobody has to name.
            { "WatermarkDialog",
              "\n\n这跟前面已经记下的一条一模一样，按下不会多改什么 —— 要改就换一个词，"
              "或者动一下不透明度。" },
            { "WatermarkDialog", "\n\n这会把前面记下的「%1」换掉；撤销一次回到那一个。" },
            { "DocumentEditor", "加水印「%1」（%n 页）", 1 },
            { "DocumentEditor", "不透明度只能是 1% 到 100% 之间的整数。" },
            { "DocumentEditor", "这一页的内容流不是能往上加东西的样子，所以水印没有画上去。" },
            // Every numerus message added by sec.32-33, listed by name. A row that only the
            // catalogue knows about is a row no reader ever sees translated, and the plural
            // forms are the part a single lookup cannot check -- so each of these is asked
            // twice by the loop below (n=1 and n=40) and the two answers have to be
            // different sentences, not one sentence with a different number in it.
            { "MergePagesDialog", "%n 页", 1 },
            { "MergePagesDialog", "这份文档一共 %n 页，没有第 %1 页。", 1 },
            { "MovePagesDialog", "这份文档一共 %n 页，没有第 %1 页。", 1 },
            { "DocumentEditor", "%1 一共 %n 页，没有第 %2 页。", 1 },
            { "MergePagesDialog", "\n\n这份文档里已经有 %n 页是合并进来的了。", 1 },
            { "MergePagesDialog", "%n 个文件一共取 %1 页，合到%2，按列表的顺序连着排在第 %3 页。", 2 },
            { "DocumentEditor", "这份要写出的文档里有 %n 页是合并进来的，而那些页没有你在屏幕上能看到的页号，所以哪几页该进哪一份是说不出来的。先撤销那几次合并再拆。", 1 },
            { "RotatePagesDialog", "%1：%n 页。", 1 },
            { "SplitPagesDialog", "\n\n这会把前面记下的「每 %n 页一份」换掉；撤销一次回到那一个。", 1 },
            { "WatermarkDialog", "「%1」，%2% 不透明度。说好的 %3 页里有 %n 页还在，只盖那 %n 页。", 1 },
            { "WatermarkDialog", "全部（%n 页）", 1 },
            { "PrintDialog", "全部（%n 页）", 1 },
            { "PrintDialog", "%n 页/张", 1 },
            { "ImageExportDialog", "全部（%n 页）", 1 },
            { "DocumentTab", "这份要写出的文档只有 %n 页，拆不出第二个文件来。", 1 },
            // handoff-04 G1 6: the line the close prompt reads when the reader filled
            // fields, and the one place a field count reaches the screen. Without a
            // probe the two English forms could come back identical and every other
            // check would still be green (sec.93's lesson).
            { "DocumentTab", "改了 %n 个表单字段", 1 },
            // sec.33's second half: the rows that sit outside the edit model -- what the
            // export says when it finishes, what the close guard asks, what the dialogs add
            // once a merge has run. Each of these can name exactly one page or one file or
            // one tab, which is the case the English inflects for and the case a
            // count-bearing sentence gets wrong by default.
            { "MainWindow", "已导出 %n 张图片到 %1", 1 },
            { "MainWindow", "其中 %n 个带网格的表格已转换为可编辑 Word 表格。", 1 },
            { "MainWindow", "其中 %n 处图形（图表、框图、照片等）已按图片插入，"
                            "其所在页面的文字仍是可以编辑的。", 1 },
            { "DocumentTab", "\n\n列表里前面 %n 个文件已经记下了，要退回去用撤销。", 1 },
            { "DeletePagesDialog", "\n\n这 %n 页里有 %1 页是合并进来的，屏幕上没有它们的页码，"
                                   "这里删不到它们。", 1 },
            { "WatermarkDialog", "\n\n前面合并进来的那 %n 页屏幕上没有页号，所以不在这 %1 页里，"
                                 "也不会带上水印。", 1 },
            // The row §74 pinned and §93 fixed: the bookmark box counted heading-only bookmarks
            // with `%1` + .arg(), which is not a numerus message at all, so the English catalogue
            // had one form and a reader with exactly one such bookmark was told
            // "1 of them point at no page." -- read off the screen in §88. It is listed here
            // because a `%1`-style row cannot be caught by any catalogue shape rule: `%n` never
            // appears in it, so every numerus check looks past it. Only this probe asks for n=1
            // and n=40 and compares what the reader would actually hear.
            { "BookmarkDialog", "\n\n其中 %n 条不指向任何一页。", 1 },
            // §99 restated §80's positional sentence (the count now means "the ones this request
            // shifts off their slot", not "every position this version cannot keep"). It counts, so
            // it gets a probe of its own: a %n row is invisible to every catalogue shape rule, and
            // this is the only place that asks the catalogue for both forms and compares them with
            // the digits masked.
            { "BookmarkDialog",
              "\n\n其中 %n 条书签指着页面内的某个位置，而这一次它们对不上文件里原来的条目："
              "那几条写出去会变成整页适配。没被改动的书签，位置照旧带走。",
              1 },
            // The search list's last row: the first thousand hits get a row each,
            // and this one says how many were left out. A count sentence like the
            // rest of these -- the catalogue checks its shape, but only this probe
            // asks for the singular as well and compares the two with the digits
            // masked, which is what catches an English plural of one.
            { "SearchPanel", "还有 %n 处没有列出", 1 },
            // F3's two counted sentences -- §47 said before the press in 删除页面, 合并文件进来 and
            // 拆分文件. All three carry `%n`, so all three are invisible to every catalogue shape
            // rule unless something asks the catalogue for n=1 and n=40 and compares the answers
            // with the digits masked. Two of them share a Chinese source? No: the delete and split
            // wordings differ (a delete costs a thread, a split sends the pair to two files), and
            // the merge one counts the annotations on the pages coming in. Each is probed in its own
            // context, which is the only place the pairing of wording and box is recorded.
            { "DeletePagesDialog",
              "\n\n这 %n 条回答会失去所回答的批注，保存出来成为独立的批注（写的字都还在）。",
              1 },
            { "MergePagesDialog",
              "\n\n这些页上有 %n 条是对另一条批注的回答：合并只把页上的批注带进来，带不"
              "进回答与被回答之间的联系，保存出来成为独立的批注（写的字都还在）。",
              1 },
            { "SplitPagesDialog",
              "\n\n这 %n 条回答和所回答的批注会被拆到两个文件里去，在各自的文件里都成为"
              "独立的批注（写的字都还在）。",
              1 },
            // And the five that say the same thing about XFA without a number in them: the two the
            // status line chooses between, and the three box sentences. Not shape-rule bait the way
            // a `%n` row is, but they are the newest strings in the program and a sweep that stops
            // at the counted ones is how an untranslated sentence reaches an English screen.
            { "MainWindow",
              "已打开 %1 —— 这份文档的表单还有一份 XFA 写法，本版既不显示也不编辑它的内容（屏幕上"
              "看的是字段那份）。" },
            { "MainWindow",
              "已打开 %1 —— 这是一份动态 XFA 文档：正文要由支持 XFA 的阅读器现画，本版既不显示也不"
              "编辑它的内容；不含 XFA 的阅读器打开它，一般只看到一页「请稍候」之类的话。" },
            { "DeletePagesDialog",
              "\n\n这份文档还带着一份 XFA 表单：删过页面的文件里不会再有它，别的阅读器会改用表单字"
              "段那一份。" },
            { "MovePagesDialog",
              "\n\n这份文档还带着一份 XFA 表单：重排过页面的文件里不会再有它，别的阅读器会改用表单"
              "字段那一份。" },
            { "MergePagesDialog",
              "\n\n这份文档还带着一份 XFA 表单：合进过页面的文件里不会再有它，别的阅读器会改用表单"
              "字段那一份。" },
            { "SplitPagesDialog",
              "\n\n这份文档还带着一份 XFA 表单：拆出来的文件里不会再有它，别的阅读器会改用表单字段"
              "那一份。" },
        };

        for (const Probe &probe : probes) {
            const QString source = QString::fromUtf8(probe.source);
            // Same call shape the program uses: a numerus message is ONE source string
            // carrying `%n`, the middle slot is still the disambiguation (empty here), and
            // the last argument is the count that picks the form. The first version of this
            // passed the source text into that middle slot, so program and probe agreed with
            // each other on a lookup the catalogue never had -- two wrongs, one green.
            const QString english = QCoreApplication::translate(
                probe.context, probe.source, nullptr, probe.n);
            const QString where = QString::fromLatin1(probe.context) + QStringLiteral(": \"")
                                  + source + QStringLiteral("\" ");
            check(english != source
                      && !hasChinese(english),
                  where + QStringLiteral("is translated (got \"%1\")").arg(english));
            // A translation that still holds a raw XML entity is a hand-edited .ts
            // whose ampersand got escaped twice: the catalogue then carries the
            // file format's own markup into the menu, and the reader sees
            // "Rotate Pages(&amp;R)". Neither of the two checks above can catch
            // that -- it is not Chinese, and it is not the source string -- so it
            // needs its own, and this is where the six escaped-mnemonic entries
            // were found.
            check(!english.contains(QStringLiteral("&amp;")),
                  QStringLiteral("%1: \"%2\" leaks a markup entity (got \"%3\")")
                      .arg(QString::fromLatin1(probe.context), source, english));
            // A translation that dropped a placeholder would be worse than no
            // translation: the arg list would not match the string any more. Plural rows are
            // checked per-slot below instead, because one form may leave out a number that
            // the count it was picked for already implies: 删页's notice asked with n=1 is a
            // document whose only page came in from somewhere else, and there is no second
            // number left in that sentence to repeat.
            if (probe.n < 0 && source.contains(QStringLiteral("%1"))) {
                check(english.contains(QStringLiteral("%1")),
                      QStringLiteral("%1: \"%2\" keeps its first placeholder")
                          .arg(QString::fromLatin1(probe.context), source));
            }
            if (probe.n >= 0) {
                // The point of a numerus entry is that one number is not said like several,
                // so two things have to hold and neither is checkable from a single lookup:
                // the count has to survive into the answer (Qt fills `%n` at translate()
                // time, not by .arg()), and the *other* form has to be a different sentence.
                // A hand-written entry with one `<numerusform>` copied twice passes every
                // check above -- same wording for 1 and for 40, which is exactly the "Delete
                // 1 pages" this conversion exists to end. And it passes the naive version of
                // this check too: "Delete 1 pages" and "Delete 40 pages" differ, so the
                // comparison has to mask the number out first. (Measured: the first version
                // of this check stayed green with both forms written identical.)
                // What has to hold is that Qt *substituted* the count, not that the wording
                // spells the digit out: a singular like "盖这 %n 页" -> "Stamp this page"
                // drops the number on purpose, and the plural keeps it.
                check(!english.contains(QStringLiteral("%n")),
                      where + QStringLiteral("left an unsubstituted %n for n=%1 (got \"%2\")")
                                  .arg(probe.n)
                                  .arg(english));
                const int otherN = probe.n == 1 ? 40 : 1;
                const QString other = QCoreApplication::translate(
                    probe.context, probe.source, nullptr, otherN);
                // A plural row keeps its placeholders between the two forms rather than in
                // each of them: the form for one may say the count and nothing else, as the
                // delete notice's does. What may not happen is a number the reader needs
                // vanishing from the row altogether.
                for (int slot = 1; slot <= 2; ++slot) {
                    const QString marker = QLatin1String("%") + QString::number(slot);
                    if (!source.contains(marker))
                        continue;
                    check(english.contains(marker) || other.contains(marker),
                          where + QStringLiteral("loses %1 from both plural forms (got \"%2\" "
                                                 "and \"%3\")")
                                    .arg(marker, english, other));
                }
                const auto mask = [](QString text) {
                    // Every run of digits that is not itself part of a `%1`-style placeholder.
                    // Two failed attempts are why this is written the way it is: replacing
                    // "1" everywhere also ate the "1" of `%1` (so the masked strings still
                    // differed and the check passed with identical forms), and looking for
                    // " 1 " missed every form that puts the count against punctuation, like
                    // "Turn right 90° (1 page)".
                    return text.replace(QRegularExpression(QStringLiteral("(?<![%\\d])\\d+")),
                                        QStringLiteral("#"));
                };
                // If the mask found nothing to hide, the comparison below proves nothing, so
                // say so instead of passing quietly.
                check(mask(english) != english || mask(other) != other,
                      where + QStringLiteral("hides no count in either form, so the two forms "
                                             "cannot be compared (got \"%1\" and \"%2\")")
                                  .arg(english, other));
                check(other != english && other != source && !hasChinese(other)
                          && mask(other) != mask(english),
                      // Built by concatenation rather than a chain of .arg(): the inserted
                      // text is itself full of `%1`, and a later arg() would fill that one
                      // too -- which is how this message once reported the source as
                      // "合入 %n 页（1）".
                      where
                          + QStringLiteral("needs a second plural form, not the same sentence "
                                           "with another number in it (got \"%1\" and \"%2\")")
                                .arg(english, other));
                // And the form picked for 1 has to read as 1. This is the check that catches
                // the row where both `<numerusform>`s were copied from the plural wording --
                // a mistake the three checks above walk straight past, because two identical
                // forms are the only shape they reject. It is also the shape the catalogue
                // took on its own once: lupdate reads a tr() call by position, hands the count
                // to the disambiguation slot, and rewrites the row to a single form
                // (src/edit/documenteditor.cpp's helper carries the note). The window is
                // deliberately one word wide: "1 of those files" is correct English, so a
                // plural two words after the count is not evidence of anything.
                const QString oneForm = probe.n == 1 ? english : other;
                check(!oneForm.contains(
                          QRegularExpression(QStringLiteral("(?<![%\\d])1\\s+[a-z]{2,}s\\b"))),
                      where
                          + QStringLiteral("agrees with 1 as if it were several (got \"%1\")")
                                .arg(oneForm));
            }
        }

        // The two separators, pinned to the exact bytes rather than to "not Chinese":
        // the English one is a comma *and a space*, and a space at the end of a
        // catalogue entry is the kind of thing a round trip through .ts and .qm can
        // drop. "1-21,22-42" is what it would look like when it did.
        check(QCoreApplication::translate("SplitPagesDialog", "、") == QStringLiteral(", "),
              "the English range separator is a comma and a space");
        check(QCoreApplication::translate("DocumentEditor", "，") == QStringLiteral(", "),
              "and so is the English separator between pending operations");

        // One Chinese string is one catalogue entry, wherever it is shown: 去掉口令 is both
        // the button on the dialog and the name the undo row is built out of, so the two
        // cannot be worded apart -- a short label for the button and a full clause for the
        // menu is not on offer. Pinned because a reword of either is a reword of both, and
        // because the same-text heuristic lupdate applies silently is exactly this rule.
        check(QCoreApplication::translate("ProtectPagesDialog", "去掉口令")
                  == QCoreApplication::translate("DocumentEditor", "去掉口令"),
              "the button and the undo row share one English for one Chinese");
        check(QCoreApplication::translate("ProtectPagesDialog", "加密 / 去掉口令")
                  == QCoreApplication::translate("DocumentTab", "加密 / 去掉口令"),
              "and so do a dialog's title and the box a refusal from it is named by");
        // The same argument for the merge refusal, which §22 moved from the model into the
        // dialog's summary as well: a reader who meets it twice -- once from a greyed
        // button, once from the editor -- must meet it in one English.
        // Asked at both counts now, because the sentence is plural-aware: one Chinese string
        // has to be one English *per form*, and a catalogue where only the plural half agrees
        // would still pass the old single lookup.
        check(QCoreApplication::translate("MergePagesDialog",
                                          "这份文档已经说了要拆成每 %n 页一份。先撤销那一步，"
                                          "再把文件合并进来。", nullptr, 1)
                  == QCoreApplication::translate("DocumentEditor",
                                                 "这份文档已经说了要拆成每 %n 页一份。"
                                                 "先撤销那一步，再把文件合并进来。", nullptr, 1)
                  && QCoreApplication::translate("MergePagesDialog",
                                                 "这份文档已经说了要拆成每 %n 页一份。"
                                                 "先撤销那一步，再把文件合并进来。", nullptr, 40)
                         == QCoreApplication::translate(
                                "DocumentEditor",
                                "这份文档已经说了要拆成每 %n 页一份。先撤销那一步，"
                                "再把文件合并进来。", nullptr, 40),
              "the merge box and the editor refuse a pending split in one English, both forms");
        // The same argument one more time, for the word that names the seventh feature: 加水印
        // is both the dialog's own title and the box a refusal from the tab is titled by, and
        // those two live in different classes -- which is the place a context can be spelled
        // wrong without anything but this check noticing.
        check(QCoreApplication::translate("DocumentTab", "加水印")
                  == QCoreApplication::translate("WatermarkDialog", "加水印"),
              "the dialog and the box a refusal from it is named by say 加水印 in one English");

        // ---- every cell of the third-party table, not a chosen few ----
        //
        // This is the check the hand-written probes above cannot be. lupdate only ever
        // sees a string that is behind tr(), so a cell written as QStringLiteral never
        // becomes an unfinished catalogue entry -- there is no entry, and no probe can be
        // named after a string that is not in the catalogue. That is exactly how §20 found
        // two columns of this table answering in Chinese under an English interface.
        // Asking the table itself for what it holds covers a row nobody has added yet.
        // The URL and the resource path are left alone: they are not prose.
        for (const ThirdPartyNotices::Component &component : ThirdPartyNotices::components()) {
            const QString cells[] = {component.name, component.libraryFile, component.licence,
                                     component.note};
            const char *roles[] = {"name", "library file", "licence", "note"};
            for (int column = 0; column < 4; ++column) {
                check(!hasChinese(cells[column]),
                      QStringLiteral("the third-party row %1 has no Chinese in its %2 "
                                     "(got \"%3\")")
                          .arg(component.name, QString::fromLatin1(roles[column]),
                               cells[column]));
            }
        }

        QCoreApplication::removeTranslator(&translator);
    }

    // ---- which language a setting means ----
    {
        // A code this build has: used as it is.
        check(Languages::resolve(QStringLiteral("zh_CN")) == QStringLiteral("zh_CN"),
              "an available code is resolved to itself");

        // A code this build does not have: English rather than Chinese. Somebody
        // whose system is set to a language this build does not speak is far
        // more likely to read English than Chinese.
        check(Languages::resolve(QStringLiteral("de")) == QStringLiteral("en"),
              "a language without a catalogue falls back to English");

        // "system" follows Windows, and lands on something in the table.
        const QString resolved = Languages::resolve(Languages::systemCode());
        check(Languages::hasCatalogue(resolved),
              "following the system lands on a language this build has");

        check(Languages::nativeName(QStringLiteral("en")) == QStringLiteral("English"),
              "a language's name is written in itself");
        check(Languages::nativeName(QStringLiteral("xx_YY")) == QStringLiteral("xx_YY"),
              "and an unknown code is shown as itself rather than as nothing");
    }

    if (failures == 0) {
        std::cout << "the catalogues hold together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
