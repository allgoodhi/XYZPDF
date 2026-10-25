#include "textlayerwriter.h"

#include "glyphlessfont.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUuid>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// The sentences are given their context with QT_TRANSLATE_NOOP at the call site, the
// way wordexport.cpp does: a helper that only calls translate() is invisible to
// lupdate (AGENTS.md), and a sentence that reaches an English window in Chinese is
// exactly what that rule is about.
QString translated(const char *text)
{
    return QCoreApplication::translate("SearchablePdf", text);
}

// The same name written two ways is still one file: the drives' case and a slash
// rather than a backslash are the two ways a save dialog produces. The same rule
// DocumentEditor uses before it saves, and for the same reason -- this program does
// not write the file it is reading.
QString comparablePath(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower();
}

// The same file reached by another name: a short 8.3 name, a directory junction, a
// symbolic link or a hard link spell a different path and are still the file being
// read. Asked of the file system rather than of the names: the volume's serial
// number and the file's index are what Windows itself calls a file's identity.
// (QFileInfo::canonicalFilePath was tried first and does not see through a
// junction -- Qt 6 stopped treating junctions as links; measured with mklink /J.)
// Only asked when the destination exists: a file that is not there yet cannot be
// the source.
bool sameFileOnDisk(const QString &a, const QString &b)
{
    if (comparablePath(a) == comparablePath(b))
        return true;
    const auto identity = [](const QString &path, BY_HANDLE_FILE_INFORMATION *info) {
        // No access asked for beyond the attributes, and every share mode allowed, so
        // that asking never collides with the reader's own open handle on the file.
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        const BOOL ok = GetFileInformationByHandle(handle, info);
        CloseHandle(handle);
        return ok != FALSE;
    };
    BY_HANDLE_FILE_INFORMATION ia{};
    BY_HANDLE_FILE_INFORMATION ib{};
    if (!identity(a, &ia) || !identity(b, &ib))
        return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber
        && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
}

// A number as a content stream wants it: a C-locale decimal point and no exponent,
// whatever the reader's locale is. Four places is a ten-thousandth of a point, far
// below anything a screen or a printer can show.
QByteArray number(double value)
{
    if (std::fabs(value) < 0.00005)
        value = 0;
    return QByteArray::number(value, 'f', 4);
}

QByteArray hex4(unsigned value)
{
    QByteArray text = QByteArray::number(value & 0xFFFFu, 16).toUpper();
    while (text.size() < 4)
        text.prepend('0');
    return text;
}

// One code point as the UTF-16BE a /ToUnicode CMap names: one value for the BMP, a
// surrogate pair above it. A character outside the BMP is one CID written in four
// bytes in the content stream and two UTF-16 units here, and both halves have to be
// there or the character reads back as two replacement squares.
QByteArray utf16beHex(char32_t code)
{
    if (code <= 0xFFFF)
        return hex4(unsigned(code));
    const char32_t offset = code - 0x10000;
    return hex4(0xD800u + unsigned(offset >> 10)) + hex4(0xDC00u + unsigned(offset & 0x3FF));
}

// The resource name the page's /Font dictionary does not already use. A page that
// came from a writer of our own will not have this one, and a page from elsewhere
// that does gets a numbered one rather than a name that replaces a font it draws.
QString freeFontName(const QPDFObjectHandle &fonts)
{
    const QString base = QStringLiteral("XYZPDFScan");
    if (!fonts.isDictionary() || !fonts.hasKey("/" + base.toStdString()))
        return base;
    for (int index = 2;; ++index) {
        const QString candidate = base + QString::number(index);
        if (!fonts.hasKey("/" + candidate.toStdString()))
            return candidate;
    }
}

// Whether a word is one character above the Latin-1 range.
//
// This exists because of a measured behaviour of the renderer this program reads with:
// a show-text that carries exactly one two-byte code whose /ToUnicode value is above
// U+00FF contributes no character at all -- it is in the file (PDFium counts the page
// object) and it is not in the text (FPDFText_CountChars does not see it). Measured on
// three shapes of the same file: a Chinese page whose seven characters were written as
// seven show-texts reads back empty; the same seven as one show-text reads back all
// seven; and a single-character Latin-1 show-text (é) reads back. So a run of
// one-character words -- what the recognizer hands out for Chinese -- is written as one
// string, which is also what a reader selects: the characters stay adjacent in the text,
// and their box is the run's box.
bool isLoneWideCharacter(const QString &text)
{
    const QVector<uint> codes = text.toUcs4();
    return codes.size() == 1 && codes.first() > 0xFF;
}

// The words as they will be written: one per show-text, with the lone wide characters
// joined to the neighbour they sit beside on the same line.
QVector<TextLayerWord> joinLoneWideCharacters(const QVector<TextLayerWord> &words)
{
    QVector<int> order(words.size());
    for (int i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&words](int a, int b) {
        const TextLayerWord &one = words.at(a);
        const TextLayerWord &two = words.at(b);
        const double tolerance = 0.4 * qMin(one.size, two.size);
        if (std::fabs(one.box.bottom() - two.box.bottom()) > tolerance)
            return one.box.bottom() < two.box.bottom();
        return one.box.left() < two.box.left();
    });

    QVector<TextLayerWord> joined;
    joined.reserve(words.size());
    auto sameLine = [&joined](const TextLayerWord &word) {
        const TextLayerWord &last = joined.last();
        return std::fabs(last.box.bottom() - word.box.bottom()) <= 0.4 * qMin(last.size, word.size)
            && word.box.left() - last.box.right() < 2.0 * word.size;
    };
    for (const int index : std::as_const(order)) {
        const TextLayerWord &word = words.at(index);
        if (!isLoneWideCharacter(word.text) || joined.isEmpty() || !sameLine(word)) {
            joined.append(word);
            continue;
        }
        TextLayerWord &target = joined.last();
        target.text += word.text;
        target.box = target.box.united(word.box);
        // The size is the line's, and the two boxes were the same height; the wider of
        // the two is the one the run's stretching has to cover.
        target.size = qMax(target.size, word.size);
    }
    return joined;
}

// One page's operators: the text that has no pixels.
//
// q ... Q so that nothing the page set (a colour, a clip, a text matrix left behind)
// reaches the appended stream, and BT ... ET because every text object needs both.
// `3 Tr` is "neither fill nor stroke" (PDF 32000-1 sec.9.3.6, table 106), and it is
// what keeps the page's picture pixel for pixel what it was.
//
// The font size is the line's height, and Tz stretches the word along its own reading
// direction so that N one-em glyphs cover the word's measured width. The matrix turns
// that direction into the page's user space, which is what a page marked /Rotate
// needs: on such a page a word that reads left to right on screen runs up or down the
// page's own coordinates.
QByteArray operatorsFor(const TextLayerPage &page, const QHash<uint, quint16> &cids,
                        const QString &resource, int *written)
{
    QByteArray ops;
    ops += "q\nBT\n3 Tr\n";
    const QByteArray font = '/' + resource.toLatin1();

    for (const TextLayerWord &word : joinLoneWideCharacters(page.words)) {
        const QVector<uint> codes = word.text.toUcs4();
        if (codes.isEmpty() || word.size <= 0 || word.box.width() <= 0 || word.box.height() <= 0)
            continue;

        QByteArray hex;
        bool complete = true;
        for (const uint code : codes) {
            const auto found = cids.constFind(code);
            if (found == cids.constEnd()) {
                complete = false;
                break;
            }
            hex += hex4(found.value());
        }
        if (!complete)
            continue;

        const double size = word.size;
        const double tz = 100.0 * word.box.width() / (double(codes.size()) * size);
        if (!(tz > 0) || !std::isfinite(tz))
            continue;

        // The baseline, one descent above the box's bottom: the embedded font is 0.8
        // em above it and 0.2 below, so the glyph box a reader selects spans exactly
        // the box recognition measured.
        const QPointF baseline(word.box.left(), word.box.bottom() - 0.2 * size);
        const QPointF origin = page.displayedToUser.map(baseline);
        const QPointF alongX = page.displayedToUser.map(baseline + QPointF(1, 0)) - origin;
        const QPointF down = page.displayedToUser.map(baseline + QPointF(0, 1)) - origin;

        ops += font + ' ' + number(size) + " Tf\n";
        ops += number(tz) + " Tz\n";
        ops += number(alongX.x()) + ' ' + number(alongX.y()) + ' ' + number(-down.x()) + ' '
            + number(-down.y()) + ' ' + number(origin.x()) + ' ' + number(origin.y())
            + " Tm\n";
        ops += '<' + hex + "> Tj\n";
        ++*written;
    }
    ops += "ET\nQ\n";
    return ops;
}

// One page's content streams, with a new one appended at the end.
//
// The copy-if-indirect rule on the /Contents array is appendWatermarkContent()'s
// (documenteditor.cpp), and it is not qpdf's addPageContents(): a shared indirect
// array is written into, so without the copy two pages that point at one array both
// gain this page's text layer. The reason is at more length there; this is the same
// page shape and the same mistake waiting.
void appendContent(QPDF &document, QPDFPageObjectHelper &page, const QByteArray &ops)
{
    QPDFObjectHandle pageDict = page.getObjectHandle();
    QPDFObjectHandle contents = pageDict.getKey("/Contents");
    if (contents.isNull()) {
        // Nothing was drawn here, so the state our stream begins in is the page's own,
        // and it is the identity one the coordinates were worked out against.
        pageDict.replaceKey("/Contents", document.newStream(
                                             std::string(ops.constData(), size_t(ops.size()))));
        return;
    }

    // ---- q ... Q around the page's own content ----
    //
    // A page's /Contents array is ONE program: the graphics state runs from the first
    // stream into the next. Most pages leave that state as they found it, and some do not
    // -- measured on a page written by Qt's own PDF writer, which sets
    // `1 0 0 -1 0 842 cm` at the top and never restores it. Text written after that, in
    // what this program took for the page's user space, came out mirrored about the page's
    // horizontal centreline: every word was there, and every one of them was on the wrong
    // half of the page and turned over. Wrapping the page's own streams in q ... Q is the
    // smallest thing that fixes it, it does not touch a byte of what the page drew, and it
    // also means a colour or a clip the page left behind cannot reach these words -- which
    // is what the q ... Q inside `ops` has been claiming all along, and could not do on its
    // own.
    QPDFObjectHandle all = QPDFObjectHandle::newArray();
    all.appendItem(document.newStream("q\n"));
    if (contents.isStream()) {
        all.appendItem(contents);
    } else if (contents.isArray()) {
        if (contents.isIndirect()) {
            contents = document.makeIndirectObject(contents.unsafeShallowCopy());
        }
        for (int index = 0; index < contents.getArrayNItems(); ++index)
            all.appendItem(contents.getArrayItem(index));
    } else {
        throw std::runtime_error(
            translated(QT_TRANSLATE_NOOP("SearchablePdf",
                                         "这份 PDF 有一页的内容流结构不对，没法把文字层接在后面。"
                                         "原来的文件没有改动。"))
                .toUtf8()
                .constData());
    }
    const QByteArray closing = QByteArrayLiteral("Q\n") + ops;
    all.appendItem(
        document.newStream(std::string(closing.constData(), size_t(closing.size()))));
    pageDict.replaceKey("/Contents", all);
}

// The /ToUnicode CMap: CID to the UTF-16BE of the character it stands for. This is
// the only thing a reader consults to copy or search these words -- the embedded font
// is glyphless -- and its shape is PDF 32000-1 sec.9.10.3's.
QByteArray toUnicodeCMap(const QVector<char32_t> &codes)
{
    QByteArray cmap;
    cmap += "/CIDInit /ProcSet findresource begin\n"
            "12 dict begin\n"
            "begincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n"
            "/CMapType 2 def\n"
            "1 begincodespacerange\n"
            "<0000> <FFFF>\n"
            "endcodespacerange\n";
    // A CMap may carry at most a hundred mappings per block.
    for (int first = 0; first < codes.size(); first += 100) {
        const int last = qMin(first + 100, codes.size());
        cmap += QByteArray::number(last - first) + " beginbfchar\n";
        for (int index = first; index < last; ++index) {
            cmap += '<' + hex4(unsigned(index + 1)) + "> <" + utf16beHex(codes.at(index))
                + ">\n";
        }
        cmap += "endbfchar\n";
    }
    cmap += "endcmap\n"
            "CMapName currentdict /CMap defineresource pop\n"
            "end\n"
            "end\n";
    return cmap;
}

} // namespace

TextLayerWriteReport writeTextLayer(const QString &source, const QString &destination,
                                    const QString &password,
                                    const QVector<TextLayerPage> &pages)
{
    TextLayerWriteReport report;
    report.path = destination;

    if (source.isEmpty() || destination.isEmpty()) {
        report.error
            = translated(QT_TRANSLATE_NOOP("SearchablePdf", "没有指定要写入的文件。"));
        return report;
    }
    if (sameFileOnDisk(source, destination)) {
        // The rule is AGENTS.md's 永不写用户的 PDF 文件, and the save dialog can hand
        // back the name it was seeded with.
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf", "不能把结果写回正在打开的这份 PDF。请换一个文件名。"));
        return report;
    }
    const QFileInfo target(destination);
    if (!target.absoluteDir().exists()) {
        report.error = translated(QT_TRANSLATE_NOOP("SearchablePdf", "文件夹不存在：%1"))
                           .arg(target.absoluteDir().absolutePath());
        return report;
    }

    QVector<TextLayerPage> described;
    for (const TextLayerPage &page : pages) {
        if (!page.words.isEmpty())
            described.append(page);
    }
    if (described.isEmpty()) {
        report.error = translated(
            QT_TRANSLATE_NOOP("SearchablePdf", "没有可以写入文字层的词。"));
        return report;
    }

    // One CID per distinct character, in the order the document mentions them, so the
    // same input always produces the same file.
    QVector<char32_t> codes;
    QHash<uint, quint16> cids;
    for (const TextLayerPage &page : std::as_const(described)) {
        for (const TextLayerWord &word : page.words) {
            const QVector<uint> units = word.text.toUcs4();
            for (const uint unit : units) {
                const char32_t code = char32_t(unit);
                if (cids.contains(unit))
                    continue;
                if (codes.size() >= 65534) {
                    report.error = translated(QT_TRANSLATE_NOOP(
                        "SearchablePdf", "这份 PDF 里的不同字符太多（%1 个），文字层放不下。"))
                                       .arg(codes.size() + 1);
                    return report;
                }
                cids.insert(unit, quint16(codes.size() + 1));
                codes.append(code);
            }
        }
    }

    QPDF document;
    try {
        const QByteArray key = password.toUtf8();
        document.processFile(source.toUtf8().constData(),
                             key.isEmpty() ? nullptr : key.constData());
    } catch (const std::exception &e) {
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf", "这份 PDF 打不开，没法加文字层。\n\n解析器说：%1"))
                           .arg(QString::fromUtf8(e.what()));
        return report;
    }

    QPDFPageDocumentHelper helper(document);
    std::vector<QPDFPageObjectHelper> pageObjects = helper.getAllPages();
    const int sourcePages = int(pageObjects.size());

    // ---- the one font, shared by every page ----
    const QByteArray program = glyphlessTrueType(codes.size() + 1);
    if (program.isEmpty()) {
        report.error = translated(
            QT_TRANSLATE_NOOP("SearchablePdf", "没有能装下这些字符的字体，文字层没有写。"));
        return report;
    }

    QPDFObjectHandle fontFile = document.newStream(std::string(program.constData(), size_t(program.size())));
    fontFile.getDict().replaceKey("/Length1",
                                  QPDFObjectHandle::newInteger(int(program.size())));
    fontFile = document.makeIndirectObject(fontFile);

    QPDFObjectHandle box = QPDFObjectHandle::newArray();
    for (const int value : {0, -200, 1000, 800})
        box.appendItem(QPDFObjectHandle::newInteger(value));

    QPDFObjectHandle descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey("/FontName", QPDFObjectHandle::newName("/XYZPDFScan"));
    // Symbolic: the font program has no cmap a reader should trust, and the codes are
    // glyph indices rather than characters.
    descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(4));
    descriptor.replaceKey("/FontBBox", box);
    descriptor.replaceKey("/ItalicAngle", QPDFObjectHandle::newInteger(0));
    descriptor.replaceKey("/Ascent", QPDFObjectHandle::newInteger(800));
    descriptor.replaceKey("/Descent", QPDFObjectHandle::newInteger(-200));
    descriptor.replaceKey("/CapHeight", QPDFObjectHandle::newInteger(800));
    descriptor.replaceKey("/StemV", QPDFObjectHandle::newInteger(80));
    descriptor.replaceKey("/FontFile2", fontFile);
    descriptor = document.makeIndirectObject(descriptor);

    QPDFObjectHandle systemInfo = QPDFObjectHandle::newDictionary();
    systemInfo.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    systemInfo.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    systemInfo.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

    // /W says every CID is one em wide, which is what the font program says too. The
    // array is [first [widths...]] for the whole run.
    QPDFObjectHandle widths = QPDFObjectHandle::newArray();
    QPDFObjectHandle run = QPDFObjectHandle::newArray();
    run.appendItem(QPDFObjectHandle::newInteger(1));
    QPDFObjectHandle values = QPDFObjectHandle::newArray();
    for (int index = 0; index < codes.size(); ++index)
        values.appendItem(QPDFObjectHandle::newInteger(1000));
    run.appendItem(values);
    widths.appendItem(run);

    QPDFObjectHandle cidFont = QPDFObjectHandle::newDictionary();
    cidFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    cidFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
    cidFont.replaceKey("/BaseFont", QPDFObjectHandle::newName("/XYZPDFScan"));
    cidFont.replaceKey("/CIDSystemInfo", systemInfo);
    cidFont.replaceKey("/FontDescriptor", descriptor);
    cidFont.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));
    cidFont.replaceKey("/DW", QPDFObjectHandle::newInteger(1000));
    cidFont.replaceKey("/W", widths);
    cidFont = document.makeIndirectObject(cidFont);

    const QByteArray cmap = toUnicodeCMap(codes);
    QPDFObjectHandle toUnicode
        = document.newStream(std::string(cmap.constData(), size_t(cmap.size())));
    toUnicode = document.makeIndirectObject(toUnicode);

    QPDFObjectHandle type0 = QPDFObjectHandle::newDictionary();
    type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    type0.replaceKey("/BaseFont", QPDFObjectHandle::newName("/XYZPDFScan"));
    type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    QPDFObjectHandle descendants = QPDFObjectHandle::newArray();
    descendants.appendItem(cidFont);
    type0.replaceKey("/DescendantFonts", descendants);
    type0.replaceKey("/ToUnicode", toUnicode);
    type0 = document.makeIndirectObject(type0);

    // ---- every page that has words ----
    try {
        for (const TextLayerPage &page : std::as_const(described)) {
            if (page.page < 0 || page.page >= int(pageObjects.size()))
                continue;
            QPDFPageObjectHelper &object = pageObjects.at(size_t(page.page));
            QPDFObjectHandle pageDict = object.getObjectHandle();
            // copy_if_shared: the page's own copy, whether /Resources was inherited
            // from the pages tree or an indirect object other pages also use.
            QPDFObjectHandle resources = object.getAttribute("/Resources", true);
            if (!resources.isDictionary()) {
                pageDict.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
                resources = pageDict.getKey("/Resources");
            }
            QPDFObjectHandle fonts = resources.getKey("/Font");
            if (!fonts.isDictionary()) {
                resources.replaceKey("/Font", QPDFObjectHandle::newDictionary());
                fonts = resources.getKey("/Font");
            } else if (fonts.isIndirect()) {
                fonts = document.makeIndirectObject(fonts.unsafeShallowCopy());
                resources.replaceKey("/Font", fonts);
            }
            const QString resource = freeFontName(fonts);
            fonts.replaceKey("/" + resource.toStdString(), type0);

            int written = 0;
            const QByteArray ops = operatorsFor(page, cids, resource, &written);
            if (written == 0) {
                // Nothing was usable after all (a word with no box, say). The font
                // entry stays -- it costs one dictionary entry -- but a page gains no
                // empty stream, and the report does not call it written.
                fonts.removeKey("/" + resource.toStdString());
                continue;
            }
            appendContent(document, object, ops);
            report.words += written;
            ++report.pages;
        }
    } catch (const std::exception &e) {
        report.error = QString::fromUtf8(e.what());
        return report;
    }

    if (report.pages == 0) {
        report.error = translated(
            QT_TRANSLATE_NOOP("SearchablePdf", "没有可以写入文字层的词。"));
        return report;
    }

    // Beside the destination, because the last step is a rename within one volume.
    // Visible rather than hidden: if the move fails this function names it in the
    // message, and a reader looking for the finished work can find it.
    const QDir directory = target.absoluteDir();
    QString staged;
    for (;;) {
        staged = directory.filePath(
            QStringLiteral("xyzpdf-searchable-%1.pdf")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QFileInfo::exists(staged))
            break;
    }

    try {
        QPDFWriter writer(document, staged.toUtf8().constData());
        // An encrypted document stays encrypted: the reader opened it with a key, and
        // a copy that quietly drops it would be a different file with different
        // permissions. qpdf writes it with the same parameters.
        writer.setPreserveEncryption(true);
        writer.write();
    } catch (const std::exception &e) {
        QFile::remove(staged);
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf", "写出文件时出错，原来的文件没有改动。\n\n解析器说：%1"))
                           .arg(QString::fromUtf8(e.what()));
        return report;
    }

    // The check the ordering exists for: the bytes have to be a PDF qpdf can open
    // again, with the page count the source had, before the destination is touched.
    try {
        QPDF check;
        const QByteArray key = password.toUtf8();
        check.processFile(staged.toUtf8().constData(),
                          key.isEmpty() ? nullptr : key.constData());
        const int written = int(QPDFPageDocumentHelper(check).getAllPages().size());
        if (written != sourcePages) {
            QFile::remove(staged);
            report.error = translated(QT_TRANSLATE_NOOP(
                "SearchablePdf",
                "写出来的文件有 %2 页，而原来的文件是 %1 页，所以没有替换它。"
                "原来的文件没有改动。"))
                               .arg(sourcePages)
                               .arg(written);
            return report;
        }
    } catch (const std::exception &e) {
        QFile::remove(staged);
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf", "写出来的文件没能重新打开，所以没有替换它。"
                             "原来的文件没有改动。\n\n解析器说：%1"))
                           .arg(QString::fromUtf8(e.what()));
        return report;
    }

    const std::wstring from = QDir::toNativeSeparators(staged).toStdWString();
    const std::wstring to = QDir::toNativeSeparators(destination).toStdWString();
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        == 0) {
        report.error = translated(QT_TRANSLATE_NOOP(
            "SearchablePdf",
            "新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，"
            "或者这个名字不允许覆盖（Windows 错误 %2）。写好的文件留在 %3。"))
                           .arg(QDir::toNativeSeparators(destination))
                           .arg(GetLastError())
                           .arg(QDir::toNativeSeparators(staged));
        return report;
    }

    report.ok = true;
    return report;
}
