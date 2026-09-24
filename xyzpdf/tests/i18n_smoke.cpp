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

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
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
            { "MainWindow", "旋转页面(&R)…" },     { "MainWindow", "保存修改为(&S)…" },
            { "MainWindow", "还有没保存的修改" },
            { "MainWindow", "已写成一个新文件（%1 页）：%2" },
            { "DocumentTab", "把这些修改存成一个新文件" },
            { "DocumentTab", "%1 已修改.pdf" },
            { "DeletePagesDialog", "当前页（第 %1 页）" },
            { "DeletePagesDialog", "现在这份有 %1 页：再删 %2 页，剩下 %3 页。" },
            { "DeletePagesDialog", "还没有说出要删哪几页。" },
            // The rotation dialog and the names the undo menu is built out of:
            // both are newer than the sweep above, and the second is the one that
            // a free helper function can drop out of the catalogue without any
            // build step noticing.
            { "RotatePagesDialog", "旋转这些页" },
            { "RotatePagesDialog", "%1。说好的 %2 页里有 %3 页还在，只转那 %3 页。" },
            { "DocumentEditor", "删除 %1 页" },
            { "DocumentEditor", "向右转 90°（%1 页）" },
            { "DocumentEditor", "另存为不能覆盖正在打开的文件。请换一个文件名。" },
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
            { "MovePagesDialog", "把 %1 页移到%2：移完它们连着排在第 %3-%4 页。" },
            { "DocumentEditor", "移到最前（%1 页）" },
            { "DocumentEditor", "移到最后（%1 页）" },
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
            { "MergePagesDialog",
              "%1 个文件一共取 %2 页，合到%3，按列表的顺序连着排在第 %4-%5 页。" },
            { "DocumentEditor", "合入 %2 页（%1）" },
            { "DocumentEditor", "另存为不能覆盖合并进来的文件（%1）。请换一个文件名。" },
            // The split after the merge, and the same four kinds of it: the menu row,
            // the dialog's own words, the undo item's name, and the status line the
            // save finishes with -- that last one is a second literal beside the first
            // because a split's page count is the whole document's, and "一个文件"
            // would be a lie about what came out.
            { "MainWindow", "拆分文件(&C)…" },
            { "MainWindow", "已写成 %1 个新文件（整份 %2 页）：%3" },
            { "SplitPagesDialog", "拆成 %1 份" },
            { "SplitPagesDialog", "会写成 %1 份，每份最多 %2 页：%3。" },
            { "SplitPagesDialog", "这份文档没有可以拆的页面。" },
            { "DocumentEditor", "拆成每 %1 页一份" },
            { "DocumentEditor", "这份文档已经说了要拆成每 %1 页一份。先撤销那一步，再把文件合并进来。" },
            // The two separators the code joins reader-facing text with: the range list
            // in the split dialog and the list of pending operations. They went into the
            // English interface straight out of a QStringLiteral -- "1-21、22-42" on
            // screen -- and none of the checks below can see that, because a CJK comma is
            // neither an ideograph nor the source string of a sentence.
            { "SplitPagesDialog", "、" },
            { "DocumentEditor", "，" },
        };

        for (const Probe &probe : probes) {
            const QString source = QString::fromUtf8(probe.source);
            const QString english =
                QCoreApplication::translate(probe.context, probe.source);
            check(english != source
                      && !hasChinese(english),
                  QStringLiteral("%1: \"%2\" is translated (got \"%3\")")
                      .arg(QString::fromLatin1(probe.context), source, english));
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
            // translation: the arg list would not match the string any more.
            if (source.contains(QStringLiteral("%1"))) {
                check(english.contains(QStringLiteral("%1")),
                      QStringLiteral("%1: \"%2\" keeps its first placeholder")
                          .arg(QString::fromLatin1(probe.context), source));
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
