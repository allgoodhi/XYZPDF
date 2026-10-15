// Taking pages out of a PDF, moving the rest around, putting pages from another file
// in, cutting what is left into several files, drawing marks on the pages that remain,
// and what is promised around all five.
//
// The feature has one interesting failure mode, and it is not the page count: it
// is writing over a file the reader owns. So most of what is pinned down here is
// the promise side of docs/PLAN.md §7.3 and AGENTS.md's 永不写用户的 PDF 文件 --
// the source is never the destination, a refused request changes nothing on disk,
// and a write that cannot land leaves a whole file the reader can find, never a
// truncated one at the path they pointed at.
//
// The round trip is checked through Qt PDF rather than through QPDF alone, for the
// same reason tests/qpdf_smoke.cpp links both: the claim is the handoff between the
// library that writes and the library this program reads with, and one engine
// agreeing with itself proves nothing.
//
// The encrypted cases at the end *measure* rather than assert a belief about what
// an encrypted document does when the structural editor is pointed at it -- that is
// QPDF's decision, not this program's. What is asserted is the invariant that holds
// whatever that turns out to be: nothing damaged, and either a copy that behaves
// like the original or no copy at all. "Behaves like the original" used to mean only
// "opens, and with the password it still needs one"; it now covers the permission
// bits too, because a copy that silently stops saying "not to be edited" is the one
// way this feature could hand someone a document that lies about its protection.
//
// The 加密 cases at the very end are the same feature from the other side: the reader
// asking for the protection rather than inheriting it. Two things keep those checks off
// a guess about qpdf's numbers. Nothing here names a bit value out of the PDF spec -- what
// is asserted is that the six boxes are six switches, each one changing the bytes, none
// granting anything and none sharing a bit with another, which is the claim a dialog with
// six labels makes. And the carry-through comparison from the paragraph above is kept as
// the counterfactual for it: the same read that says "unchanged" when nothing was asked
// has to say "changed" when something was, or one of the two is not looking at the file.
// The one thing asserted about the words rather than the bytes: no password, ever, in a
// menu row or a summary.
//
// The 水印 checks need a painted bitmap, so this file runs on a QGuiApplication with the
// offscreen platform -- the only platform a gate may open. Offscreen resolves no font, so
// those checks ask the file which pages draw a masked image, how many such images it holds
// and what its /ca says; "do the marks read as the words" is a question about a renderer's
// fonts and belongs to tests/watermarkprobe.cpp, which is run under the windows platform by
// hand for exactly that reason.

#include "../src/document/qtpdfbackend.h"
#include "../src/edit/documenteditor.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPointF>
#include <QRegularExpression>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QModelIndex>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFOutlineObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int failures = 0;
int notes = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

// Printed rather than asserted, and for one reason: the answer is QPDF's, and this
// test is where it gets written down before it becomes a sentence in the docs.
void note(const QString &what)
{
    std::cout << "note: " << what.toStdString() << '\n';
    ++notes;
}

QString hashOfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

// How many pages Qt PDF counts in a file, or -1 for "it would not open". The two
// failures are kept apart because "no pages" and "unreadable" are different
// sentences, and only one of them means the editor wrote a broken file.
int pagesAsReadByTheReader(const QString &path, const QString &password = QString())
{
    QtPdfBackend document;
    if (document.load(path, password) != Document::Status::Ready)
        return -1;
    return document.pageCount();
}

bool paintsAnything(const QImage &image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                return true;
        }
    }
    return false;
}

QVector<int> everyPage(int pageCount)
{
    QVector<int> pages;
    pages.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        pages.append(page);
    return pages;
}

using Ref = DocumentEditor::PageRef;

// The same document as the editor keeps it: every page of the one that was opened,
// in the order it is on the screen. A page number a reader types is always a page of
// that file, so every reference below has source 0 -- the tests that need a page from
// a merged-in file say so themselves.
QVector<Ref> everyRef(int pageCount)
{
    QVector<Ref> refs;
    refs.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        refs.append(Ref{0, page});
    return refs;
}

// The same list written out by hand, from the numbers on the screen.
QVector<Ref> onScreen(std::initializer_list<int> numbers)
{
    QVector<Ref> refs;
    for (int number : numbers)
        refs.append(Ref{0, number});
    return refs;
}

// A page order handed back by a plan, read back as numbers so the assertions can say
// "2,3,4" instead of naming pairs. A page that came in from a merged file is written
// as its merge number times a hundred, plus its number in its own file -- the only way
// these tests can point at one, and the reason is the feature's limit rather than a
// shortcut: an imported page has no number on the screen for anyone to type.
QVector<int> asNumbers(const QVector<Ref> &order)
{
    QVector<int> numbers;
    numbers.reserve(order.size());
    for (const Ref &ref : order)
        numbers.append(ref.source == 0 ? ref.page : ref.source * 100 + ref.page);
    return numbers;
}

// A file holding pages 5, 6 and 7 of the fixture and nothing else, written here with
// the editor's own delete: the merge tests need a second document, and a fixture in
// tests/fixtures would be picked up by every other test that globs that folder.
QString writeDonor(const QString &source, const QString &destination, int pagesBefore,
                   const QVector<int> &keep, QString *error)
{
    DocumentEditor prep;
    if (!prep.open(source, QString(), error))
        return {};
    QVector<int> drop;
    for (int page = 0; page < pagesBefore; ++page) {
        if (!keep.contains(page))
            drop.append(page);
    }
    if (!prep.deletePages(drop, error) || !prep.saveAs(destination, error))
        return {};
    return destination;
}

// What the file itself says each page's rotation is, in degrees.
//
// Read out of QPDF rather than off the screen because the renderer's answer to
// "how wide is this page" cannot tell a right turn from a left one, and the thing
// being checked here is that the direction a reader picked in a dialog is the
// direction that got written. The renderer's own reading is checked separately,
// through pageSize(), so the two engines each get asked a question the other
// cannot answer.
QVector<int> rotationsInFile(const QString &path)
{
    QVector<int> angles;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        for (const QPDFPageObjectHelper &page : QPDFPageDocumentHelper(qpdf).getAllPages()) {
            // Asked for rather than read as an integer: most pages have no /Rotate
            // at all, and QPDF answers getIntValue() on a null object with a
            // warning to the console per page.
            const QPDFObjectHandle rotate = page.getObjectHandle().getKey("/Rotate");
            angles.append(rotate.isInteger() ? rotate.getIntValue() : 0);
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the rotations out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return angles;
}

// What order the file says its pages are in, by reading the number each fixture
// page prints on itself.
//
// The only way the reorder can be checked at all: a page count cannot tell a moved
// document from an untouched one, and the whole failure mode of a move is "every
// page is there, in the wrong place". Read through Qt PDF rather than QPDF because
// the claim is about the file a reader would open, not about what the writer thinks
// it wrote. -1 for a page whose number could not be read, so a silent mismatch
// cannot come from the parsing.
QVector<int> orderInFile(const QString &path)
{
    QVector<int> numbers;
    QtPdfBackend document;
    if (document.load(path, QString()) != Document::Status::Ready) {
        std::cerr << "FAIL: the reader would not open " << path.toStdString() << '\n';
        ++failures;
        return numbers;
    }
    static const QRegularExpression printed(QStringLiteral("page (\\d+)"));
    for (int page = 0; page < document.pageCount(); ++page) {
        const QRegularExpressionMatch hit = printed.match(document.text(page).simplified());
        numbers.append(hit.hasMatch() ? hit.captured(1).toInt() : -1);
    }
    return numbers;
}

QString describeOrder(const QVector<int> &numbers)
{
    QStringList parts;
    for (int number : numbers)
        parts.append(QString::number(number));
    return parts.join(QLatin1Char(','));
}

// The same for the /ca values a file says, so a failure line names the numbers the way
// they are written rather than as "3 of them".
QString describeAlphas(const QVector<double> &alphas)
{
    QStringList parts;
    for (double alpha : alphas)
        parts.append(QString::number(alpha, 'f', 3));
    return parts.join(QStringLiteral(", "));
}

// What the marks this program draws say about themselves once they are in a file.
//
// Read page by page and from the *content* side rather than by counting dictionaries,
// because the two ways this can be wrong sit on opposite sides of one object: an entry
// can sit in a page's /Resources and never get invoked, which draws nothing, and a
// content stream can invoke a name that is not in that page's resources, which draws
// something with a reader's own defaults. tests/watermarkprobe.cpp measured the second
// one happening and looking entirely right -- its first version drew a solid black
// watermark because the /ca state was filed at the top of /Resources instead of in the
// /ExtGState sub-dictionary a lookup goes through.
//
// Which image is "ours" is keyed on /SMask: the fixture's pages carry no images at all
// (tools/make_fixtures.py writes text), and the other image fixtures' images are
// uncompressed or flate with no soft mask, so a masked image in a file this test wrote
// is the watermark and nothing else.
struct InkReport
{
    bool readable = false;
    int pageCount = 0;
    QVector<int> drawn;            // pages whose content invokes a masked image
    QVector<double> alphas;        // /ca of the graphics states those pages invoke
    QVector<int> namedButNotDrawn; // pages naming a masked image without drawing it
    int maskedImages = 0;          // distinct masked image XObjects anywhere in the file
    QVector<QVector<double>> matrices; // the six numbers each masked draw is placed with
};

InkReport readInk(const QString &path, const QString &password = QString())
{
    InkReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData(), password.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        report.readable = true;
        report.pageCount = int(pages.size());

        // A `cm` and the image it places, read together: the six numbers are only
        // meaningful as the placement of the draw that follows them, and the placement is
        // the half of the promise a rotated page can break. The operator name is required
        // rather than skipped over because the six numbers *are* this program's own spelling
        // of a matrix -- checking the file against a looser pattern would let a broken one
        // of our own making read back as fine.
        static const QRegularExpression draw(
            QStringLiteral("([-+0-9.eE]+)\\s+([-+0-9.eE]+)\\s+([-+0-9.eE]+)\\s+"
                           "([-+0-9.eE]+)\\s+([-+0-9.eE]+)\\s+([-+0-9.eE]+)\\s+cm\\s+"
                           "/([A-Za-z0-9._]+)\\s+Do"),
            QRegularExpression::UseUnicodePropertiesOption);
        static const QRegularExpression state(
            QStringLiteral("/([A-Za-z0-9._]+)\\s+gs"),
            QRegularExpression::UseUnicodePropertiesOption);

        QSet<int> masked;
        for (size_t index = 0; index < pages.size(); ++index) {
            QPDFPageObjectHelper page = pages.at(index);
            const QPDFObjectHandle resources = page.getAttribute("/Resources", false);
            const QPDFObjectHandle xobjects = resources.getKey("/XObject");
            const QPDFObjectHandle states = resources.getKey("/ExtGState");

            bool namesOne = false;
            if (xobjects.isDictionary()) {
                for (const std::string &name : xobjects.getKeys()) {
                    const QPDFObjectHandle image = xobjects.getKey(name);
                    if (!image.isStream())
                        continue;
                    masked.insert(image.getObjectID());
                    if (image.getDict().getKey("/SMask").isStream())
                        namesOne = true;
                }
            }

            // Every piece of a page's content, decoded. A page this class has watermarked
            // has at least two: the fixture's own flate-compressed stream and the one added
            // on the end.
            QByteArray content;
            const QPDFObjectHandle contents = page.getObjectHandle().getKey("/Contents");
            // A viewer sees a page's content as one stream with a line feed between the pieces
            // (32000-1 §7.7.3.3), so the reading here puts one back. Without it the fixture's
            // `ET` and our `q` fuse into `ETq`, an operator no viewer ever has to resolve, and a
            // match here would be proving the shape of this function's own concatenation.
            const auto append = [&content](QPDFObjectHandle part) {
                if (!part.isStream())
                    return;
                if (!content.isEmpty())
                    content.append('\n');
                const std::shared_ptr<Buffer> bytes = part.getStreamData(qpdf_dl_generalized);
                content.append(reinterpret_cast<const char *>(bytes->getBuffer()),
                               int(bytes->getSize()));
            };
            if (contents.isArray()) {
                for (int part = 0; part < contents.getArrayNItems(); ++part)
                    append(contents.getArrayItem(part));
            } else {
                append(contents);
            }

            const QString asText = QString::fromLatin1(content);
            QVector<QVector<double>> marks;
            for (QRegularExpressionMatchIterator it = draw.globalMatch(asText); it.hasNext();) {
                const QRegularExpressionMatch hit = it.next();
                const QString name = QLatin1Char('/') + hit.captured(7);
                const QPDFObjectHandle image = xobjects.getKey(name.toStdString());
                if (!image.isStream() || !image.getDict().getKey("/SMask").isStream())
                    continue;
                QVector<double> matrix;
                for (int number = 1; number <= 6; ++number)
                    matrix.append(hit.captured(number).toDouble());
                marks.append(matrix);
            }
            if (!marks.isEmpty()) {
                report.drawn.append(int(index));
                report.matrices.append(marks.first());
                for (QRegularExpressionMatchIterator it = state.globalMatch(asText); it.hasNext();) {
                    const QString name = QLatin1Char('/') + it.next().captured(1);
                    const QPDFObjectHandle alpha = states.getKey(name.toStdString()).getKey("/ca");
                    if (alpha.isNumber())
                        report.alphas.append(alpha.getNumericValue());
                }
            }
            if (namesOne && marks.isEmpty())
                report.namedButNotDrawn.append(int(index));
        }
        report.maskedImages = masked.size();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the marks out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return report;
}

// A copy of the document in which two pages read the *same* /Contents array and the same
// /Resources dictionary, which is what a producer that laid down five identical pages
// does. Nothing else in this file can reach the two rules that exist for that shape:
// a page's content array is copied before anything is appended to it, and a resource
// sub-dictionary is copied before a key is added, because /Contents and /Resources are
// both places a file is allowed to point two pages at one object. Written here rather
// than committed as a fixture for the same reason as the donor above, and it is the
// control for both halves of the watermark checks below: without the copies, page 1
// gains marks that only page 2 was promised.
QString writeSharedPagesCopy(const QString &source, const QString &destination)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle first = pages.at(0).getObjectHandle();
        QPDFObjectHandle second = pages.at(1).getObjectHandle();

        QPDFObjectHandle shared = QPDFObjectHandle::newArray();
        shared.appendItem(first.getKey("/Contents"));
        shared = qpdf.makeIndirectObject(shared);
        first.replaceKey("/Contents", shared);
        second.replaceKey("/Contents", shared);

        QPDFObjectHandle resources = qpdf.makeIndirectObject(first.getKey("/Resources"));
        first.replaceKey("/Resources", resources);
        second.replaceKey("/Resources", resources);

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: the shared-page copy could not be written: " << e.what() << '\n';
        ++failures;
        return {};
    }
    return destination;
}

// One page's /Contents rewritten into a shape the save has an opinion about, with nothing
// else in the document touched. Written here rather than committed as a fixture, for the
// same two reasons the donor and the shared-page copy above give.
//
// The two shapes are the two ends of what a page's content can be in a file somebody else
// produced. *Absent* is legal and common -- a blank page, or a page whose ink is all in its
// annotations -- and the mark still has to land there rather than the named page being
// quietly skipped. A *literal string* is not legal (the standard allows a stream, or an array
// of streams) and still appears in the wild from producers that wrote their operators inline;
// for that one the save refuses, because appending to content this program cannot parse is
// how a watermark ends up declared on a page and never drawn on it.
enum class ContentsShape { Absent, LiteralString };

bool writeContentsShape(const QString &source, const QString &destination, int page,
                        ContentsShape shape, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle target = pages.at(size_t(page)).getObjectHandle();
        if (shape == ContentsShape::Absent)
            target.removeKey("/Contents");
        else
            target.replaceKey("/Contents",
                              QPDFObjectHandle::newString(
                                  "BT /F1 24 Tf 100 700 Td (written inline) Tj ET"));
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// A document that needs a password to open, written here rather than committed as a
// fixture: the test needs one, it needs to know the password, and a file in
// tests/fixtures would be swept up by every other test that globs the folder.
// `userPassword` empty with modify forbidden is the shape most protected PDFs in the
// wild actually have -- readable by anyone, "not to be edited" written on it.
QString writeEncryptedCopy(const QString &source, const QString &destination,
                           const QString &userPassword, bool allowModify)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.setR6EncryptionParameters(userPassword.toUtf8().constData(),
                                         "owner",
                                         /* allow_accessibility = */ true,
                                         /* allow_extract = */ true,
                                         /* allow_assemble = */ false,
                                         /* allow_annotate_and_form = */ true,
                                         /* allow_form_filling = */ true,
                                         /* allow_modify_other = */ allowModify,
                                         qpdf_r3p_full,
                                         /* encrypt_metadata_aes = */ true);
        writer.write();
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

// A page that carries annotations, written here for the same reason the donor and the
// shared-page copy are: nothing under tests/fixtures has a /Annot at all -- counted, not
// assumed, eleven fixtures and zero occurrences -- so the repair DocumentEditor calls while
// replaying a merge has never run in this suite, and adding a fixture would put an annotated
// page in front of every other test that globs that folder.
//
// Two kinds of annotation, because a merge has two different jobs. A /Text note only has to
// end up belonging to the page that got copied. A /Widget that is also a text field
// additionally has to be reachable through the written document's own /AcroForm, or the
// answer in it disappears from every reader that shows forms -- which is the half of
// fixCopiedAnnotations() that nothing outside this file can see.
QString writeAnnotatedPageCopy(const QString &source, const QString &destination,
                               QString *error)
{
    auto rect = [](double left, double top, double right, double bottom) {
        QPDFObjectHandle value = QPDFObjectHandle::newArray();
        value.appendItem(QPDFObjectHandle::newReal(left));
        value.appendItem(QPDFObjectHandle::newReal(top));
        value.appendItem(QPDFObjectHandle::newReal(right));
        value.appendItem(QPDFObjectHandle::newReal(bottom));
        return value;
    };
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle note = QPDFObjectHandle::newDictionary();
        note.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        note.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        note.replaceKey("/Rect", rect(60, 700, 90, 730));
        note.replaceKey("/Contents",
                        QPDFObjectHandle::newString("a note that came in with the page"));
        note = qpdf.makeIndirectObject(note);
        note.replaceKey("/P", page);

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        field.replaceKey("/T", QPDFObjectHandle::newString("donorAnswer"));
        field.replaceKey("/V", QPDFObjectHandle::newString("filled in the donor"));
        field.replaceKey("/Rect", rect(60, 640, 260, 670));
        field = qpdf.makeIndirectObject(field);
        field.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(note);
        annots.appendItem(field);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(field);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// A page whose note owns a popup and has a reply attached to it -- the shape qpdf's comment
// about fixCopiedAnnotations says nothing about. A popup is an annotation that is NOT in the
// page's /Annots array (only its owner's /Popup names it), and a reply's /IRT names another
// annotation on the same page, so both are references that a shallow page copy can leave
// pointing at the document the page came out of.
QString writeLinkedAnnotationsCopy(const QString &source, const QString &destination,
                                   QString *error)
{
    auto rect = [](double left, double top, double right, double bottom) {
        QPDFObjectHandle value = QPDFObjectHandle::newArray();
        value.appendItem(QPDFObjectHandle::newReal(left));
        value.appendItem(QPDFObjectHandle::newReal(top));
        value.appendItem(QPDFObjectHandle::newReal(right));
        value.appendItem(QPDFObjectHandle::newReal(bottom));
        return value;
    };
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle note = QPDFObjectHandle::newDictionary();
        note.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        note.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        note.replaceKey("/Rect", rect(60, 700, 90, 730));
        note.replaceKey("/Contents", QPDFObjectHandle::newString("the note a reply answers"));
        note = qpdf.makeIndirectObject(note);
        note.replaceKey("/P", page);

        QPDFObjectHandle popup = QPDFObjectHandle::newDictionary();
        popup.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        popup.replaceKey("/Subtype", QPDFObjectHandle::newName("/Popup"));
        popup.replaceKey("/Rect", rect(95, 700, 240, 780));
        popup = qpdf.makeIndirectObject(popup);
        popup.replaceKey("/Parent", note);
        note.replaceKey("/Popup", popup);

        QPDFObjectHandle reply = QPDFObjectHandle::newDictionary();
        reply.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        reply.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        reply.replaceKey("/Rect", rect(60, 600, 90, 630));
        reply.replaceKey("/Contents", QPDFObjectHandle::newString("a reply to the note"));
        reply.replaceKey("/IRT", note);
        reply.replaceKey("/RT", QPDFObjectHandle::newName("/R"));
        reply = qpdf.makeIndirectObject(reply);
        reply.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(note);
        annots.appendItem(reply);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The same two annotations, but one page apart: the note (with its popup) on page 1 and the reply
// that answers it on page 2.
//
// This is the shape that tells the two readings of §78's rule apart, and the first sample here is
// structurally unable to: with the note and the reply on one page, "the thread is legal because it
// is inside this document" and "the thread dangles because it names the donor's object" arrive as
// the same integer. Only a thread that crosses pages can be left alone by the repair while still
// naming an annotation the file shows -- so this generator exists to hold the fix to its own
// boundary, and the case below that deletes a page neither of them is on is what proves it.
QString writeCrossPageThreadCopy(const QString &source, const QString &destination, QString *error)
{
    auto rect = [](double left, double top, double right, double bottom) {
        QPDFObjectHandle value = QPDFObjectHandle::newArray();
        value.appendItem(QPDFObjectHandle::newReal(left));
        value.appendItem(QPDFObjectHandle::newReal(top));
        value.appendItem(QPDFObjectHandle::newReal(right));
        value.appendItem(QPDFObjectHandle::newReal(bottom));
        return value;
    };
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        // Not const: replaceKey() is a non-const member, which has stopped every generator here
        // the moment it was written (§76's note about aitems(), the same trap one call over).
        QPDFObjectHandle first = pages.at(0).getObjectHandle();
        QPDFObjectHandle second = pages.at(1).getObjectHandle();

        QPDFObjectHandle note = QPDFObjectHandle::newDictionary();
        note.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        note.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        note.replaceKey("/Rect", rect(60, 700, 90, 730));
        note.replaceKey("/Contents", QPDFObjectHandle::newString("the note on the page before"));
        note = qpdf.makeIndirectObject(note);
        note.replaceKey("/P", first);

        QPDFObjectHandle popup = QPDFObjectHandle::newDictionary();
        popup.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        popup.replaceKey("/Subtype", QPDFObjectHandle::newName("/Popup"));
        popup.replaceKey("/Rect", rect(95, 700, 240, 780));
        popup = qpdf.makeIndirectObject(popup);
        popup.replaceKey("/Parent", note);
        note.replaceKey("/Popup", popup);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(note);
        first.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFObjectHandle reply = QPDFObjectHandle::newDictionary();
        reply.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        reply.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        reply.replaceKey("/Rect", rect(60, 600, 90, 630));
        reply.replaceKey("/Contents", QPDFObjectHandle::newString("a reply across the pages"));
        reply.replaceKey("/IRT", note);
        reply.replaceKey("/RT", QPDFObjectHandle::newName("/R"));
        reply = qpdf.makeIndirectObject(reply);
        reply.replaceKey("/P", second);

        QPDFObjectHandle other = QPDFObjectHandle::newArray();
        other.appendItem(reply);
        second.replaceKey("/Annots", qpdf.makeIndirectObject(other));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// What the references between annotations look like from the outside: whether a popup is shared by
// two pages, whether a popup's /Parent names the annotation that points at it, and where a reply's
// /IRT actually lands -- on the reply's own page, on another page this file shows, or on something
// no page of this file shows.
//
// That three-way split of the reply target is §78's instrument, and it exists because two readings
// used to collapse into one number. A thread that crosses pages *inside this document* is legal
// (PDF 2.0 §12.5.5 lets a reply answer an annotation elsewhere in the same file), while a thread
// naming an object no page shows is exactly what a page copy leaves behind. Counting both as "not
// on this page" would judge the repair with a number that also rises when the file is fine -- §77's
// lesson about a predicate that cannot see the difference between two legal shapes.
//
// `popupRects` is here because a save has to be shown to have *kept* the popup it fixed, not
// swapped it for an empty dictionary: the counts alone read the same either way, and object ids
// cannot serve as the fingerprint, since qpdf's writer renumbers every object on each write.
// `texts` is here so "the thread was dropped" can be told apart from "the reply was deleted": the
// link is the thing that goes, never the words.
struct LinkReport
{
    int notes = 0;
    int popups = 0;
    int popupsSharedByTwoPages = 0;
    int popupsNamingWrongOwner = 0;
    int popupsNamingStrayOwner = 0;
    int replies = 0;
    int repliesNamingForeignTarget = 0;
    int repliesOnAnotherPage = 0;
    int repliesOnNoPage = 0;
    QStringList popupRects;
    QStringList texts;
};

LinkReport readLinkedAnnotations(const QString &path)
{
    LinkReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        // Every annotation any page of this file shows, asked of the pages rather than of each
        // annot's /P, for the reason §76 gives.
        QSet<int> shownAnywhere;
        for (const QPDFPageObjectHelper &page : pages) {
            const QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index)
                shownAnywhere.insert(annots.getArrayItem(index).getObjGen().getObj());
        }
        QSet<int> seenPopups;
        for (const QPDFPageObjectHelper &page : pages) {
            const QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            QSet<int> onThisPage;
            for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index)
                onThisPage.insert(annots.getArrayItem(index).getObjGen().getObj());
            for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index) {
                const QPDFObjectHandle item = annots.getArrayItem(index);
                const QPDFObjectHandle popup = item.getKey("/Popup");
                if (popup.isDictionary()) {
                    ++report.popups;
                    const int id = popup.getObjGen().getObj();
                    if (seenPopups.contains(id))
                        ++report.popupsSharedByTwoPages;
                    seenPopups.insert(id);
                    report.popupRects
                        << QString::fromStdString(popup.getKey("/Rect").unparse());
                    const int owner = popup.getKey("/Parent").getObjGen().getObj();
                    if (owner != item.getObjGen().getObj())
                        ++report.popupsNamingWrongOwner;
                    if (!shownAnywhere.contains(owner))
                        ++report.popupsNamingStrayOwner;
                }
                const QPDFObjectHandle target = item.getKey("/IRT");
                if (target.isDictionary()) {
                    ++report.replies;
                    const int landed = target.getObjGen().getObj();
                    if (!onThisPage.contains(landed))
                        ++report.repliesNamingForeignTarget;
                    if (shownAnywhere.contains(landed)) {
                        if (!onThisPage.contains(landed))
                            ++report.repliesOnAnotherPage;
                    } else {
                        ++report.repliesOnNoPage;
                    }
                }
                if (item.getKey("/Subtype").getName() == "/Text" && !item.hasKey("/IRT"))
                    ++report.notes;
                const QPDFObjectHandle body = item.getKey("/Contents");
                if (body.isString())
                    report.texts << QString::fromUtf8(body.getStringValue().c_str());
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the annotation links out of " << path.toStdString()
                  << ": " << e.what() << '\n';
        ++failures;
    }
    return report;
}

// A document whose *own* first page carries one text field of a chosen name, registered in
// its own /AcroForm. The point is the name: merged with a donor whose field uses the same
// one, this is the case qpdf's note about fixCopiedAnnotations warns about ("resolve
// conflicts from common resource and field names"), and it is not reachable with the
// fixtures -- none of them has an /AcroForm at all.
QString writeFieldCopy(const QString &source, const QString &destination,
                       const QString &fieldName, const QString &fieldValue, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        field.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        field.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        field.replaceKey("/Rect", rect);
        field = qpdf.makeIndirectObject(field);
        field.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(field);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(field);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// One page of the given source, carrying exactly one annotation of the asked-for kind, with no
// /AP appearance stream on it.
//
// §81 asks the question §54/§57 asked about form fields, for the layer nobody has asked about: a
// reader highlights a passage in another program, saves, opens the file here -- does a single pixel
// change? Every annotation type in PDF may carry an optional /AP stream, and PDF 2.0 §12.5.3 says a
// viewer without one has to draw its own appearance (a highlight paints its quad points with the
// annotation's colour, a note draws an icon). So a file with no /AP is not "a page with nothing to
// draw" -- it is a page that tests whether this renderer asks the annotation layer at all. That is
// the same distinction §57 built field-appearance.pdf for, one layer over: there it was "the widget
// has its own drawing and still shows nothing", here it is "there is nothing for the widget to
// draw and yet a reader that supports annotations shows something".
QString writeMarkupSample(const QString &source, const QString &destination, const QString &kind,
                          QString *error)
{
    auto rect = [](double left, double top, double right, double bottom) {
        QPDFObjectHandle value = QPDFObjectHandle::newArray();
        value.appendItem(QPDFObjectHandle::newReal(left));
        value.appendItem(QPDFObjectHandle::newReal(top));
        value.appendItem(QPDFObjectHandle::newReal(right));
        value.appendItem(QPDFObjectHandle::newReal(bottom));
        return value;
    };
    auto colour = [&](double red, double green, double blue) {
        QPDFObjectHandle value = QPDFObjectHandle::newArray();
        value.appendItem(QPDFObjectHandle::newReal(red));
        value.appendItem(QPDFObjectHandle::newReal(green));
        value.appendItem(QPDFObjectHandle::newReal(blue));
        return value;
    };
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
        annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        if (kind == QStringLiteral("highlight")) {
            annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Highlight"));
            annot.replaceKey("/Rect", rect(70, 770, 300, 792));
            // Quad points are the highlight's geometry: two per line, counter-clockwise, and a
            // viewer paints these quads with a multiply blend. Without them a highlight has no
            // area to paint, and "nothing was drawn" would be about the file rather than the reader.
            QPDFObjectHandle quads = QPDFObjectHandle::newArray();
            for (double y : {772.0, 790.0}) {
                quads.appendItem(QPDFObjectHandle::newReal(70));
                quads.appendItem(QPDFObjectHandle::newReal(y));
                quads.appendItem(QPDFObjectHandle::newReal(300));
                quads.appendItem(QPDFObjectHandle::newReal(y));
            }
            annot.replaceKey("/QuadPoints", quads);
            annot.replaceKey("/C", colour(1.0, 0.85, 0.20));
        } else if (kind == QStringLiteral("square")) {
            annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Square"));
            annot.replaceKey("/Rect", rect(70, 700, 260, 760));
            annot.replaceKey("/C", colour(0.90, 0.10, 0.10));
        } else {
            annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
            annot.replaceKey("/Rect", rect(320, 770, 344, 794));
            annot.replaceKey("/Name", QPDFObjectHandle::newName("/Comment"));
            annot.replaceKey("/Contents",
                            QPDFObjectHandle::newString("a note left in another reader"));
            annot.replaceKey("/C", colour(1.0, 1.0, 0.0));
            annot.replaceKey("/Open", QPDFObjectHandle::newBool(false));
        }
        annot = qpdf.makeIndirectObject(annot);
        annot.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(annot);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// What one page's annotation layer looks like from the file: the subtype, whether a /QuadPoints or
// /Contents came with it, and -- the part §81's screen reading depends on -- whether it carries an
// /AP appearance stream. A sample that silently lost its annotation would produce a correct-looking
// zero pixel difference, which is §57's objection turned around.
struct MarkupReport
{
    QString subtype;
    bool hasQuadPoints = false;
    bool hasContents = false;
    bool hasAppearance = false;
};

MarkupReport readMarkupAnnotation(const QString &path)
{
    MarkupReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        const QPDFObjectHandle annots = pages.at(0).getObjectHandle().getKey("/Annots");
        if (!annots.isArray() || annots.getArrayNItems() < 1)
            return report;
        const QPDFObjectHandle annot = annots.getArrayItem(0);
        report.subtype = QString::fromUtf8(annot.getKey("/Subtype").getName().c_str());
        report.hasQuadPoints = annot.getKey("/QuadPoints").isArray();
        report.hasContents = annot.getKey("/Contents").isString();
        report.hasAppearance = annot.getKey("/AP").isDictionary();
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the annotation out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return report;
}

// The same page with a rectangle painted into its **own content stream**, at the exact area the
// highlight above claims. No annotation of any kind.
//
// This exists because "0 pixels differ" is only a finding about the annotation layer if the
// instrument would have noticed a mark there at all. Appending a second stream to /Contents is legal
// in PDF (the array is concatenated at draw time), so the page keeps its text and gains one filled
// box -- the same pixels the highlight would have covered, reached by the route this renderer
// definitely reads.
QString writePaintedRectSample(const QString &source, const QString &destination, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle painted = QPDFObjectHandle::newStream(&qpdf);
        painted.replaceStreamData(
            std::make_shared<Buffer>(std::string("q\n0.2 0.6 1.0 rg\n70 770 230 22 re f\nQ\n")),
            QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
        painted = qpdf.makeIndirectObject(painted);

        const QPDFObjectHandle existing = page.getKey("/Contents");
        QPDFObjectHandle contents = QPDFObjectHandle::newArray();
        if (existing.isArray()) {
            for (int index = 0; index < existing.getArrayNItems(); ++index)
                contents.appendItem(existing.getArrayItem(index));
        } else if (!existing.isNull()) {
            contents.appendItem(existing);
        }
        contents.appendItem(painted);
        page.replaceKey("/Contents", contents);

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// Two registered fields, one with a box and one deliberately without.
//
// The second is the shape that decides what may be done to values. PDF 2.0 §12.7.5.2 allows a field
// to exist in /AcroForm /Fields with a value and no widget annotation at all -- a default a script
// reads, a value a template carries. So "this field has no box on any page here" does **not** mean
// "this field's answer belongs to a page that is gone": for the value-only field it never belonged
// to a page at all. Clearing on the weaker predicate would delete content nobody asked to delete,
// which is the same mistake §51 and §52 warned about from the other side.
//
// What tells the two apart on the file is the box's own furniture: /Rect (and /P) on the field
// object means "this object is a widget that lost its page"; their absence means it was never on
// one. That is the predicate this sample exists to measure.
QString writeBoxedAndValueOnlyFields(const QString &source, const QString &destination,
                                     QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle boxed = QPDFObjectHandle::newDictionary();
        boxed.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        boxed.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        boxed.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        boxed.replaceKey("/T", QPDFObjectHandle::newString("boxed"));
        boxed.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        boxed.replaceKey("/Rect", rect);
        boxed = qpdf.makeIndirectObject(boxed);
        boxed.replaceKey("/P", page);

        QPDFObjectHandle valueOnly = QPDFObjectHandle::newDictionary();
        valueOnly.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        valueOnly.replaceKey("/T", QPDFObjectHandle::newString("value only"));
        valueOnly.replaceKey("/V", QPDFObjectHandle::newString("a default with no page"));
        valueOnly = qpdf.makeIndirectObject(valueOnly);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(boxed);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(boxed);
        fields.appendItem(valueOnly);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The layer above that one: the same field, but hanging off a **parent**, which is how a real form
// spells its names. A field's address in a PDF is not its `/T` -- it is the chain of partial names
// joined with a period (PDF 2.0 §12.7.5.2), and only the last one is an annotation on the page. So
// two documents can each hold a field a reader would call `form.answer` while the string `answer`
// appears once in each of them, and a merge then has two questions to answer rather than one: which
// segment of the address gets renamed, and whether the parent comes along at all. A copied widget
// whose `/Parent` points into the file it came out of is reachable from nothing in this one, which
// is a box on the page that no program can ever get an answer out of.
QString writeNestedFieldCopy(const QString &source, const QString &destination,
                             const QString &parentName, const QString &childName,
                             const QString &fieldValue, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle widget = QPDFObjectHandle::newDictionary();
        widget.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        widget.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        widget.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        widget.replaceKey("/T", QPDFObjectHandle::newString(childName.toUtf8().constData()));
        widget.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        widget.replaceKey("/Rect", rect);
        widget = qpdf.makeIndirectObject(widget);
        widget.replaceKey("/P", page);

        // A parent field is a field and not an annotation: no /Rect, no /P, and nothing on the
        // page names it. That is exactly why this case is not the flat one with a longer name.
        QPDFObjectHandle parent = QPDFObjectHandle::newDictionary();
        parent.replaceKey("/T", QPDFObjectHandle::newString(parentName.toUtf8().constData()));
        QPDFObjectHandle kids = QPDFObjectHandle::newArray();
        kids.appendItem(widget);
        parent.replaceKey("/Kids", kids);
        parent = qpdf.makeIndirectObject(parent);
        widget.replaceKey("/Parent", parent);

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(parent);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        form.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(widget);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The field tree the way a form-reading program walks it: addresses assembled from the chain, plus
// the two shapes that are invisible to a reader of `/Fields` alone -- a widget on a page that no
// entry in the form list reaches, and two terminals sharing one address.
struct FieldTreeReport
{
    QStringList addresses;
    QStringList values;
    int terminals = 0;
    int parents = 0;
    int widgetsWithoutOwnName = 0;
    int unregisteredWidgets = 0;
    int duplicateAddresses = 0;
    int brokenBackLinks = 0;
    int parentChildMismatch = 0;
    int terminalsNamingNoPage = 0;
    // Per terminal, in the same order as `addresses`: whether that object carries its own /Rect.
    QVector<bool> hasRect;
    // The object each terminal gets its name from: itself when it carries a /T, its parent when it
    // is a pure widget. Two addresses are only a *collision* when they belong to two different
    // fields -- one field drawn on two pages reuses one address, and that is legal.
    QList<int> terminalOwners;
    QStringList aliases;
    int namelessTerminals = 0;
};

void walkFieldTree(const QPDFObjectHandle &field, const QString &prefix, const QString &inheritedValue,
                   int depth, const QSet<int> &pageIds,
                   QSet<int> &reachable, FieldTreeReport &report, int parentObj = -1)
{
    if (!field.isDictionary() || depth > 8)
        return;
    const QPDFObjectHandle part = field.getKey("/T");
    const bool ownName = part.isString();
    const QString name = ownName ? QString::fromUtf8(part.getStringValue().c_str()) : QString();
    // A pure widget has no name of its own: it is drawn, and what it is drawn *as* comes from the
    // field above it. So its address is the prefix, with no trailing period.
    const QString address =
        prefix.isEmpty() ? name : (name.isEmpty() ? prefix : prefix + QLatin1Char('.') + name);
    reachable.insert(field.getObjGen().getObj());
    // Asked in the other direction, because the two can disagree and only one of them is what a
    // viewer uses: this file assembles the address by walking DOWN from /Fields, while a program
    // that was handed a widget walks UP through /Parent. A copied field whose /Parent still names
    // the parent it came in with would read as `form+1.answer` one way and `form.answer` the other
    // -- the same address twice in one form, which is exactly what the rename exists to prevent.
    const QPDFObjectHandle back = field.getKey("/Parent");
    if (parentObj < 0) {
        if (back.isDictionary())
            ++report.parentChildMismatch;   // a top-level field that claims a parent
    } else if (!back.isDictionary() || back.getObjGen().getObj() != parentObj) {
        ++report.parentChildMismatch;
    }
    // A value lives on the field, not on its widgets, so it has to be carried down the chain:
    // asking a widget for /V would report nothing at all for a field drawn on two pages, and the
    // thing §51 is about is precisely whether that value ends up in a file that cannot show it.
    const QPDFObjectHandle value = field.getKey("/V");
    const QString effective =
        value.isString() ? QString::fromUtf8(value.getStringValue().c_str()) : inheritedValue;
    const QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray() && kids.getArrayNItems() > 0) {
        ++report.parents;
        for (int index = 0; index < kids.getArrayNItems(); ++index)
            walkFieldTree(kids.getArrayItem(index), address, effective, depth + 1, pageIds,
                          reachable, report, field.getObjGen().getObj());
        return;
    }
    ++report.terminals;
    if (!ownName)
        ++report.widgetsWithoutOwnName;
    if (address.isEmpty())
        ++report.namelessTerminals;
    // The owner of an address is the field that says it: the object itself when it carries a /T,
    // its parent when it is a pure widget. A nameless top-level field has no parent to fall back
    // on, so its own id is used -- the sentinel would make two such fields look like ONE owner and
    // hide a real pair behind an accident.
    const int addressOwner = ownName
                          ? field.getObjGen().getObj()
                          : (parentObj >= 0 ? parentObj : field.getObjGen().getObj());
    report.terminalOwners << addressOwner;
    report.addresses << address;
    report.values << effective;
    // Read from the object itself, not inherited: what is asked below is whether an alias survives
    // a merge, not how a viewer would spell a widget's name.
    const QPDFObjectHandle alias = field.getKey("/TM");
    report.aliases << (alias.isString()
                           ? QString::fromUtf8(alias.getStringValue().c_str())
                           : QString());
    const QPDFObjectHandle owner = field.getKey("/P");
    if (owner.isDictionary() && !pageIds.contains(owner.getObjGen().getObj()))
        ++report.terminalsNamingNoPage;
    // Does this object carry its own box? §77's question: "no live box on any page here" has two
    // completely different causes -- a widget whose page was taken out, and a field that was never
    // on a page at all (a default or a script value, legal per PDF 2.0 §12.7.5.2). /Rect is what
    // tells them apart on the file, so the reader has to report it.
    report.hasRect << field.getKey("/Rect").isArray();
    if (back.isDictionary() && !reachable.contains(back.getObjGen().getObj()))
        ++report.brokenBackLinks;
}

FieldTreeReport readFieldTree(const QString &path)
{
    FieldTreeReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QSet<int> pageIds;
        for (const QPDFPageObjectHelper &page : pages)
            pageIds.insert(page.getObjectHandle().getObjGen().getObj());
        QSet<int> reachable;
        const QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = form.getKey("/Fields");
        for (int index = 0; fields.isArray() && index < fields.getArrayNItems(); ++index)
            walkFieldTree(fields.getArrayItem(index), QString(), QString(), 0, pageIds, reachable,
                          report);
        // Same address from two different fields is the collision §46/§50 are about; the same
        // address from two widgets of one field is what a spread field *is*, so the owners have to
        // be compared as well as the names -- counting repeated names alone would call the shape
        // below a collision and be wrong about it.
        for (int a = 0; a < report.addresses.size(); ++a) {
            if (report.addresses.at(a).isEmpty())
                continue;   // a field with no name has no address, so it cannot collide with one
            for (int b = a + 1; b < report.addresses.size(); ++b) {
                if (report.addresses.at(a) == report.addresses.at(b)
                    && report.terminalOwners.value(a) != report.terminalOwners.value(b)) {
                    ++report.duplicateAddresses;
                    break;
                }
            }
        }
        QSet<QString> seen;
        for (const QString &address : report.addresses)
            seen.insert(address);
        for (const QPDFPageObjectHelper &page : pages) {
            const QPDFObjectHandle annots = page.getObjectHandle().getKey("/Annots");
            for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index) {
                const QPDFObjectHandle item = annots.getArrayItem(index);
                const QPDFObjectHandle subtype = item.getKey("/Subtype");
                if (!subtype.isName() || subtype.getName() != "/Widget")
                    continue;
                if (!reachable.contains(item.getObjGen().getObj()))
                    ++report.unregisteredWidgets;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the field tree out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return report;
}

// The shape §51 names as its next rung: **one** field whose widgets sit on two different pages.
// Adobe's own way of doing it is the reverse of writeNestedFieldCopy -- the registered field carries
// no /Rect and belongs to no page, and its /Kids are two pure widget annotations (each with /P and
// /Parent, neither with a /T of its own), because a name is said once and drawn twice. It matters
// for the repair: a field like this is *needed by both halves of a split*, so "drop the field whose
// page went out" would delete a box the reader can still see.
QString writeSpreadFieldCopy(const QString &source, const QString &destination,
                             const QString &fieldName, const QString &fieldValue,
                             int pageA, int pageB, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();

        QPDFObjectHandle parent = QPDFObjectHandle::newDictionary();
        parent.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        parent.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        parent.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        parent = qpdf.makeIndirectObject(parent);

        QPDFObjectHandle kids = QPDFObjectHandle::newArray();
        for (const int which : {pageA, pageB}) {
            QPDFObjectHandle page = pages.at(size_t(which)).getObjectHandle();
            QPDFObjectHandle widget = QPDFObjectHandle::newDictionary();
            widget.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
            widget.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
            QPDFObjectHandle rect = QPDFObjectHandle::newArray();
            rect.appendItem(QPDFObjectHandle::newReal(60));
            rect.appendItem(QPDFObjectHandle::newReal(600));
            rect.appendItem(QPDFObjectHandle::newReal(260));
            rect.appendItem(QPDFObjectHandle::newReal(630));
            widget.replaceKey("/Rect", rect);
            widget = qpdf.makeIndirectObject(widget);
            widget.replaceKey("/P", page);
            widget.replaceKey("/Parent", parent);
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(widget);
            page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
            kids.appendItem(widget);
        }
        parent.replaceKey("/Kids", kids);

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(parent);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The last two shapes at the edge of the naming rule, both asked because §50 and §52 each found that
// a counter built from the shapes I had thought of reports the un-thought-of ones as defects.
//
//  * a field with an **alias**: `/TM` is a second name, from the PDF 2.0 form specification, and the
//    fully-qualified name is still assembled from `/T`. So "the same name twice" has two meanings,
//    and what a merge has to decide is which one it keys on. Nothing in this product draws either
//    name (v1 has no form UI), so the question is not what the reader sees but what the repair
//    promises -- and whether a file can end up holding two fields an XFA-aware viewer would call
//    the same thing.
//  * a field with **no name at all**: legal (an empty partial name is what the spec allows once
//    `/TM` exists), and an empty string is not an address, so it must not be counted as a
//    collision with another one.
QString writeAliasedFieldCopy(const QString &source, const QString &destination,
                              const QString &fieldName, const QString &aliasName,
                              const QString &fieldValue, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        if (!fieldName.isEmpty())
            field.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        if (!aliasName.isEmpty())
            field.replaceKey("/TM", QPDFObjectHandle::newString(aliasName.toUtf8().constData()));
        field.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        field.replaceKey("/Rect", rect);
        QPDFObjectHandle widget = qpdf.makeIndirectObject(field);
        widget.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(widget);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(widget);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// Two nameless fields in one document, which is the case the collision rule has to refuse to
// report: neither has an address, so there is nothing to collide.
QString writeTwoNamelessFields(const QString &source, const QString &destination, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();
        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        for (const int y : {600, 500}) {
            QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
            field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
            field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
            field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
            field.replaceKey("/V", QPDFObjectHandle::newString("no name here"));
            QPDFObjectHandle rect = QPDFObjectHandle::newArray();
            rect.appendItem(QPDFObjectHandle::newReal(60));
            rect.appendItem(QPDFObjectHandle::newReal(y));
            rect.appendItem(QPDFObjectHandle::newReal(260));
            rect.appendItem(QPDFObjectHandle::newReal(y + 30));
            field.replaceKey("/Rect", rect);
            QPDFObjectHandle widget = qpdf.makeIndirectObject(field);
            widget.replaceKey("/P", page);
            annots.appendItem(widget);
            fields.appendItem(widget);
        }
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The layer §53 and §54 both named and neither touched: **XFA**. A document can carry a second,
// complete description of its form -- an XML packet in /AcroForm /XFA, an array of (subtype, stream)
// pairs -- and in viewers that honour it, that packet, not /AcroForm /Fields, is the form. qpdf has
// no XFA facility at all (nothing in its headers even names it), so the only question this program
// can answer is a smaller one with a big consequence: **does anything we do keep it in step, or does
// it ride along untouched?** The packet below is hand-made and deliberately minimal -- a template
// that says the form has a page, and a dataset that carries the typed value. That is enough to
// follow the bytes; it is not a real generator's output, and nothing here claims it is.
QString writeXfaFieldCopy(const QString &source, const QString &destination,
                          const QString &fieldName, const QString &fieldValue, QString *error)
{
    try {
        // One pass from the source, not writeFieldCopy followed by a second opening of the result.
        // The second version was here first and it silently produced a document with the field and
        // *no packet*: qpdf keeps the file it processed open and reads objects lazily while writing,
        // so writing back over that same name is what its own documentation warns against -- the
        // remove-then-rename workaround only turned the silent wrong file into a loud failure.
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        field.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        field.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        field.replaceKey("/Rect", rect);
        field = qpdf.makeIndirectObject(field);
        field.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(field);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(field);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);

        const QString templateXml =
            QStringLiteral("<?xml version=\"1.0\"?><xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xfdf/"
                           "\"><template><pageSet><page name=\"P1\"/></pageSet></template></xdp:xdp>");
        const QString datasetXml = QStringLiteral(
            "<?xml version=\"1.0\"?><xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xfdf/\">"
            "<datasets><form1><%1>%2</%1></form1></datasets></xdp:xdp>")
            .arg(fieldName, fieldValue);

        // A stream handle is not a dictionary: keys go through getDict(), and replacing a key on the
        // stream object itself is answered with a WARNING line and quietly ignored.
        QPDFObjectHandle packet =
            QPDFObjectHandle::newStream(&qpdf, templateXml.toUtf8().constData());
        packet.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
        QPDFObjectHandle data =
            QPDFObjectHandle::newStream(&qpdf, datasetXml.toUtf8().constData());
        data.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));

        QPDFObjectHandle xfa = QPDFObjectHandle::newArray();
        xfa.appendItem(QPDFObjectHandle::newString(""));
        xfa.appendItem(packet);
        xfa.appendItem(QPDFObjectHandle::newString("datasets"));
        xfa.appendItem(data);
        form.replaceKey("/XFA", xfa);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// The packet as the file holds it: how many (subtype, stream) pairs, and what the concatenated
// bytes say. Asked of the bytes rather than of a parser because nothing in this build parses XFA,
// and because the claim under test is exactly "the same XML text is still in there".
struct XfaReport
{
    bool present = false;
    int pairs = 0;
    QString text;
};

XfaReport readXfa(const QString &path)
{
    XfaReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle xfa = form.getKey("/XFA");
        if (!xfa.isArray())
            return report;
        report.present = true;
        for (int index = 0; index < xfa.getArrayNItems(); ++index) {
            QPDFObjectHandle item = xfa.getArrayItem(index);
            if (!item.isStream())
                continue;
            ++report.pairs;
            // getStreamData() decodes (the packet is written plain here, but a real one is almost
            // always /FlateDecode), and it hands back a Buffer whose bytes it owns -- so the text
            // is copied out, not referenced.
            const auto buffer = item.getStreamData();
            if (buffer) {
                report.text += QString::fromUtf8(
                    QByteArray::fromRawData(reinterpret_cast<const char*>(buffer->getBuffer()),
                                           static_cast<int>(buffer->getSize())));
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the XFA packet out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return report;
}

// A widget that carries an **appearance stream**, which is what every real one does. §54 measured
// "our reader draws no field" against a widget holding nothing but `/Rect`, `/T` and `/V` -- the
// shape this file builds because it is the smallest one that is a form field. A file produced by a
// viewer that the reader actually typed into paints the box through `/AP`, and Qt renders annotation
// appearances, so §54's number may belong to a shape nobody ships. This helper is the positive
// control on that conclusion, and its output is also what the screen pass looks at.
// The /AP dictionary §57's and §82's generators share: one /N entry holding a Form XObject that
// fills its own box and says APPEARS-HERE inside it. 200x30 because that is the /Rect both write,
// so nothing has to be scaled to see whether the drawing arrived.
QPDFObjectHandle paintedAppearance(QPDF &qpdf)
{
    const std::string paint =
        "q 0.70 0.70 0.90 rg 0 0 200 30 re f "
        "BT /F1 12 Tf 4 9 Td (APPEARS-HERE) Tj ET Q";
    QPDFObjectHandle form = QPDFObjectHandle::newStream(&qpdf, paint);
    QPDFObjectHandle formDict = form.getDict();
    formDict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    formDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
    bbox.appendItem(QPDFObjectHandle::newReal(0));
    bbox.appendItem(QPDFObjectHandle::newReal(0));
    bbox.appendItem(QPDFObjectHandle::newReal(200));
    bbox.appendItem(QPDFObjectHandle::newReal(30));
    formDict.replaceKey("/BBox", bbox);
    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
    QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
    fonts.replaceKey("/F1", qpdf.makeIndirectObject(font));
    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fonts);
    formDict.replaceKey("/Resources", resources);

    QPDFObjectHandle appearance = QPDFObjectHandle::newDictionary();
    appearance.replaceKey("/N", form);
    return appearance;
}

QString writeFieldWithAppearanceCopy(const QString &source, const QString &destination,
                                     const QString &fieldName, const QString &fieldValue,
                                     int pageIndex, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(size_t(pageIndex)).getObjectHandle();

        const QPDFObjectHandle appearance = paintedAppearance(qpdf);

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        field.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        field.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        field.replaceKey("/AP", appearance);
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        field.replaceKey("/Rect", rect);
        field = qpdf.makeIndirectObject(field);
        field.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(field);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(field);
        QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
        acroForm.replaceKey("/Fields", fields);
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(acroForm));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// §82: one painted appearance, hung on the page several ways.
//
// §81 ended with the annotation layer proven to reach the page image (highlight / square / note all
// draw, all without an /AP) and a widget with no /AP drawing nothing. The one shape nobody had
// measured off this channel was §57's: a widget that *brings* its own /AP /N. That is the difference
// between two sentences with very different owners -- "our renderer ignores appearance streams" (a
// Qt/PDFium statement about a layer) versus "it ignores widget annotations specifically" (a
// statement about one subtype, which every form in the world uses). So each variant changes exactly
// one thing:
//   subtype            -- /Square carries the identical drawing. If that paints and the widget does
//                         not, the stream itself is drawable and only the widget path is closed.
//   registeredField    -- whether the annotation is also listed in /AcroForm /Fields, which is what
//                         makes a viewer's form module own it instead of the annotation layer.
//   needAppearances    -- the document-level instruction "build the drawings yourself" (§58 read it
//                         off a screen and concluded 0 pixels; here it is read as a possible *reason*
//                         a brought-along /AP gets bypassed).
//   fieldKeys          -- /FT, /T and /V on an annotation whose /Subtype is *not* /Widget. This is
//                         the one that decides what the sentence is about: if a square stops drawing
//                         as soon as it looks like a field, the renderer keys off field-ness; if it
//                         keeps drawing, only the /Widget name is being refused -- which matters,
//                         because a document may carry a widget no viewer ever registered.
struct AppearanceProbe
{
    const char *subtype = "/Widget";
    bool registeredField = false;
    bool needAppearances = false;
    bool fieldKeys = false;
};

QString writeAppearanceProbeCopy(const QString &source, const QString &destination,
                                 const AppearanceProbe &probe, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
        annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        annot.replaceKey("/Subtype", QPDFObjectHandle::newName(probe.subtype));
        annot.replaceKey("/AP", paintedAppearance(qpdf));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        annot.replaceKey("/Rect", rect);
        annot = qpdf.makeIndirectObject(annot);
        annot.replaceKey("/P", page);
        if (probe.registeredField || probe.fieldKeys) {
            annot.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
            annot.replaceKey("/T", QPDFObjectHandle::newString("probeField"));
            annot.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
        }

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(annot);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
        if (probe.registeredField || probe.needAppearances) {
            QPDFObjectHandle acroForm = QPDFObjectHandle::newDictionary();
            if (probe.registeredField) {
                QPDFObjectHandle fields = QPDFObjectHandle::newArray();
                fields.appendItem(annot);
                acroForm.replaceKey("/Fields", fields);
            }
            if (probe.needAppearances)
                acroForm.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(true));
            qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(acroForm));
        }

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// How many annotations *page one* carries a /AP /N that still decodes to a drawing saying `says`.
// `appearancesWithPaint` above walks /AcroForm /Fields, which the two unregistered variants do not
// have -- and the file-level half of §82 has to ask "is the drawing on this page" rather than "is it
// in the form", or a variant would be reported as carrying nothing for a reason that has nothing to
// do with the renderer.
int pageAnnotationPaints(const QString &path, const QString &says)
{
    int matched = 0;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle annots = pages.at(0).getObjectHandle().getKey("/Annots");
        for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index) {
            QPDFObjectHandle normal = annots.getArrayItem(index).getKey("/AP").getKey("/N");
            if (!normal.isStream())
                continue;
            const std::shared_ptr<Buffer> bytes = normal.getStreamData();
            if (!bytes)
                continue;
            const QByteArray paint = QByteArray::fromRawData(
                reinterpret_cast<const char*>(bytes->getBuffer()), int(bytes->getSize()));
            if (paint.contains(says.toUtf8()))
                ++matched;
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the page annotations out of " << path.toStdString()
                  << ": " << e.what() << '\n';
        ++failures;
    }
    return matched;
}

// What the appearance of each registered field looks like from the outside: is there an /AP /N
// stream, and does its own content still say the one thing the test paints into it. The second half
// matters because a stream that survives as bytes but never as a *decoded* drawing is a field the
// reader sees nothing of, which is the difference between "carried" and "kept".
int appearancesWithPaint(const QString &path, const QString &says)
{
    int matched = 0;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const QPDFObjectHandle fields =
            qpdf.getRoot().getKey("/AcroForm").getKey("/Fields");
        for (int index = 0; fields.isArray() && index < fields.getArrayNItems(); ++index) {
            QPDFObjectHandle normal =
                fields.getArrayItem(index).getKey("/AP").getKey("/N");
            if (!normal.isStream())
                continue;
            const std::shared_ptr<Buffer> bytes = normal.getStreamData();
            if (!bytes)
                continue;
            const QByteArray paint = QByteArray::fromRawData(
                reinterpret_cast<const char*>(bytes->getBuffer()), int(bytes->getSize()));
            if (paint.contains(says.toUtf8()))
                ++matched;
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the appearances out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return matched;
}

// A form the way a generator that someone has actually typed into leaves it. §57's file carries a
// widget with its own drawing; this one carries the layer that drawing stands on, which a real form
// puts in the **document** rather than on the page or the field:
//
//   /AcroForm /DA  the operators to draw a field's text with (font, size, colour) -- inherited by
//                  every field that does not state its own (PDF 2.0 §12.7.3.5);
//   /AcroForm /DR  the resources that string may name. qpdf's own header notes that Acrobat and
//                  Reader ignore a /DR on the field, so the document is the only place it can live;
//   /AcroForm /Q   the alignment, inherited the same way;
//   /AcroForm /NeedAppearances  "the drawings in here are not to be trusted: make your own";
//   and the appearance stream's own /Resources, where the font that drawing invokes has to be
//   findable.
//
// Every one of those belongs to the document, and a page is all our write path copies. So this
// helper builds the shape, and the block that uses it asks what arrives and what stays behind.
QString writeDirectiveFieldCopy(const QString &source, const QString &destination,
                                const QString &fieldName, const QString &fieldValue,
                                const QString &fontKey, const QString &baseFont, int quadding,
                                bool needAppearances, bool withAppearance, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QPDFObjectHandle page = pages.at(0).getObjectHandle();

        QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName(baseFont.toUtf8().constData()));
        // One indirect font object, named from two places (the document's /DR and the drawing's own
        // /Resources) under one key. That is what makes "did the reference come along, or only the
        // name" a question with an answer.
        QPDFObjectHandle fontRef = qpdf.makeIndirectObject(font);

        QPDFObjectHandle appearance;
        if (withAppearance) {
            // The drawing invokes the font **by the name the document's /DA uses**, and paints the
            // field's value with it -- the shape a file has once anyone typed into it.
            const std::string paint = "q 0.92 0.92 0.92 rg 0 0 200 30 re f BT "
                + fontKey.toStdString() + " 12 Tf 4 9 Td (" + fieldValue.toStdString() + ") Tj ET Q";
            appearance = QPDFObjectHandle::newStream(&qpdf, paint);
            QPDFObjectHandle appearanceDict = appearance.getDict();
            appearanceDict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
            appearanceDict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
            QPDFObjectHandle bbox = QPDFObjectHandle::newArray();
            bbox.appendItem(QPDFObjectHandle::newReal(0));
            bbox.appendItem(QPDFObjectHandle::newReal(0));
            bbox.appendItem(QPDFObjectHandle::newReal(200));
            bbox.appendItem(QPDFObjectHandle::newReal(30));
            appearanceDict.replaceKey("/BBox", bbox);
            QPDFObjectHandle ownFonts = QPDFObjectHandle::newDictionary();
            ownFonts.replaceKey(fontKey.toStdString(), fontRef);
            QPDFObjectHandle ownResources = QPDFObjectHandle::newDictionary();
            ownResources.replaceKey("/Font", ownFonts);
            appearanceDict.replaceKey("/Resources", ownResources);
        }

        QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
        field.replaceKey("/T", QPDFObjectHandle::newString(fieldName.toUtf8().constData()));
        field.replaceKey("/V", QPDFObjectHandle::newString(fieldValue.toUtf8().constData()));
        // Deliberately no /DA, no /Q, no /DR on the field: its typography comes from the document,
        // which is the whole thing under test. And withAppearance=false is the other half of the
        // real world -- a form whose fields have no drawing at all because the file says whoever
        // opens it will make one.
        if (withAppearance) {
            QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
            states.replaceKey("/N", appearance);
            field.replaceKey("/AP", states);
        }
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        rect.appendItem(QPDFObjectHandle::newReal(60));
        rect.appendItem(QPDFObjectHandle::newReal(600));
        rect.appendItem(QPDFObjectHandle::newReal(260));
        rect.appendItem(QPDFObjectHandle::newReal(630));
        field.replaceKey("/Rect", rect);
        field = qpdf.makeIndirectObject(field);
        field.replaceKey("/P", page);

        QPDFObjectHandle annots = QPDFObjectHandle::newArray();
        annots.appendItem(field);
        page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

        QPDFObjectHandle fields = QPDFObjectHandle::newArray();
        fields.appendItem(field);
        QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", fields);
        form.replaceKey("/DA",
                        QPDFObjectHandle::newString(
                            QStringLiteral("%1 0 Tf 0 g").arg(fontKey).toUtf8().constData()));
        QPDFObjectHandle documentFonts = QPDFObjectHandle::newDictionary();
        documentFonts.replaceKey(fontKey.toStdString(), fontRef);
        QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Font", documentFonts);
        form.replaceKey("/DR", resources);
        form.replaceKey("/Q", QPDFObjectHandle::newInteger(quadding));
        form.replaceKey("/NeedAppearances", QPDFObjectHandle::newBool(needAppearances));
        qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// A bookmark tree the way a generator leaves one: two chapters, the first holding two sections, the
// first of those holding one sub-section -- five items over three depths, every destination an
// explicit array.
//
// This exists because there is no other way to put a tree in a file here: QPDF's outline helper is
// read-only (its constructors are private and it has no setter), so writing bookmarks means the
// seven keys by hand -- /Title /Parent /First /Last /Prev /Next /Count plus /Dest. The shapes and
// the four deliberate breakages are measured in tests/outlineprobe.cpp; what this file needs is the
// one healthy tree that the read path can be checked against, because that check runs through Qt's
// bookmark model, and the model alone decides whether the nesting survives.
bool writeNestedOutlineSample(const QString &source, const QString &destination, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };

        QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
        root = qpdf.makeIndirectObject(root);

        auto item = [&qpdf, &pageAt](const std::string &title, QPDFObjectHandle parent,
                                     int pageIndex) {
            QPDFObjectHandle handle = QPDFObjectHandle::newDictionary();
            handle.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
            handle.replaceKey("/Parent", parent);
            QPDFObjectHandle dest = QPDFObjectHandle::newArray();
            dest.appendItem(pageAt(pageIndex));
            dest.appendItem(QPDFObjectHandle::newName("/Fit"));
            handle.replaceKey("/Dest", dest);
            return qpdf.makeIndirectObject(handle);
        };

        // One level of siblings: /Prev and /Next along the row, /First and /Last from the parent,
        // and /Count as how many items hang below it. §63's controls are why this computes the
        // links instead of typing them: a root that lists no /First loses the whole outline in all
        // three readers, and a broken /Next on a top-level item truncates everything after it.
        //
        // The row is taken by value because replaceKey() is non-const on a handle that has to be
        // written into -- the same shape that has stopped every qpdf helper in this file before it.
        auto link = [](QPDFObjectHandle parent, std::vector<QPDFObjectHandle> children,
                       int descendants) {
            for (size_t i = 0; i < children.size(); ++i) {
                if (i > 0)
                    children[i].replaceKey("/Prev", children[i - 1]);
                if (i + 1 < children.size())
                    children[i].replaceKey("/Next", children[i + 1]);
            }
            parent.replaceKey("/First", children.front());
            parent.replaceKey("/Last", children.back());
            parent.replaceKey("/Count", QPDFObjectHandle::newInteger(descendants));
        };

        const QPDFObjectHandle chapterOne = item("Chapter 1", root, 0);
        const QPDFObjectHandle chapterTwo = item("Chapter 2", root, 10);
        link(root, { chapterOne, chapterTwo }, 5);

        const QPDFObjectHandle sectionOne = item("Section 1.1", chapterOne, 3);
        const QPDFObjectHandle sectionTwo = item("Section 1.2", chapterOne, 6);
        link(chapterOne, { sectionOne, sectionTwo }, 3);

        const QPDFObjectHandle subSection = item("Sub 1.1.1", sectionOne, 4);
        link(sectionOne, { subSection }, 1);

        qpdf.getRoot().replaceKey("/Outlines", root);
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// Four bookmarks whose destinations are not the page's top: one /XYZ naming a spot, one /FitH
// naming a height, one plain /Fit, one with no destination at all.
//
// Nothing else in this file can produce this shape, because our writer only ever emits /Fit (§68).
// So the input §75 asks about -- a file whose bookmarks name a place *inside* a page -- has to be
// hand-built. And the two positional shapes are not decoration: tests/outlineprobe.cpp measured
// that a real book's 184 bookmarks are all /FitH with a top coordinate, so /FitH is what the wild
// looks like while /XYZ is the shape Qt's own model reports a position for. A fix that only asks
// Qt would save one and flatten the other, and that distinction is why this sample carries both.
bool writePositionalOutlineSample(const QString &source, const QString &destination, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };

        QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
        root = qpdf.makeIndirectObject(root);

        auto item = [&qpdf](const std::string &title, QPDFObjectHandle parent) {
            QPDFObjectHandle handle = QPDFObjectHandle::newDictionary();
            handle.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
            handle.replaceKey("/Parent", parent);
            return qpdf.makeIndirectObject(handle);
        };
        // By value on purpose: replaceKey() is non-const, and a handle is a reference to the object,
        // so writing through the copy writes into the item.
        auto addDest = [](QPDFObjectHandle handle, std::vector<QPDFObjectHandle> parts) {
            QPDFObjectHandle dest = QPDFObjectHandle::newArray();
            for (QPDFObjectHandle &part : parts)
                dest.appendItem(part);
            handle.replaceKey("/Dest", dest);
        };

        const QPDFObjectHandle xyz = item("Xyz spot", root);
        addDest(xyz, { pageAt(1), QPDFObjectHandle::newName("/XYZ"),
                       QPDFObjectHandle::newReal(0), QPDFObjectHandle::newReal(700),
                       QPDFObjectHandle::newReal(0) });
        const QPDFObjectHandle fitHorizontal = item("FitH spot", root);
        addDest(fitHorizontal, { pageAt(4), QPDFObjectHandle::newName("/FitH"),
                                 QPDFObjectHandle::newReal(650) });
        const QPDFObjectHandle fit = item("Fit page", root);
        addDest(fit, { pageAt(7), QPDFObjectHandle::newName("/Fit") });
        const QPDFObjectHandle heading = item("Heading", root);

        std::vector<QPDFObjectHandle> row = { xyz, fitHorizontal, fit, heading };
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0)
                row[i].replaceKey("/Prev", row[i - 1]);
            if (i + 1 < row.size())
                row[i].replaceKey("/Next", row[i + 1]);
        }
        root.replaceKey("/First", row.front());
        root.replaceKey("/Last", row.back());
        root.replaceKey("/Count", QPDFObjectHandle::newInteger(int(row.size())));

        qpdf.getRoot().replaceKey("/Outlines", root);
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// A row for DocumentEditor::writeOutline, in the flat shape the writer takes (1-based depth, the
// same one outlineFromRows assembles against).
OutlineRow outlineRow(const QString &title, int level, int page)
{
    OutlineRow row;
    row.title = title;
    row.level = level;
    row.page = page;
    return row;
}

// Write `rows` into a copy of `source` through the product's own writer, and save it.
bool writeOutlineWithProduct(const QString &source, const QString &destination,
                             const QVector<OutlineRow> &rows, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        if (!DocumentEditor::writeOutline(qpdf, rows, error))
            return false;
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// The outline as qpdf walks it: how many items in the whole tree, and how many under the root.
// Walking rather than counting dictionary keys is the point -- the thing a reader can reach is the
// thing that was written.
struct OutlineWalk
{
    int total = 0;
    int top = 0;
    QStringList titles;
    // One entry per item, in the same order as `titles`: the destination in its own words, e.g.
    // "[/XYZ 0 700 0]" or "[/Fit]" or "none". §75 needs the *kind*, because the question there is
    // whether a position survives a save, and an item count cannot answer a question about the
    // shape of a /Dest.
    QStringList destinations;
};

// A /Dest array read as name-plus-numbers. An array's length cannot tell /XYZ 0 700 0 from
// /FitH 650 from /Fit, so the length is not what this reports.
QString describeDestination(const QPDFObjectHandle &dest)
{
    if (dest.isNull())
        return QStringLiteral("none");
    if (dest.isName())
        return QStringLiteral("named:%1").arg(QString::fromUtf8(dest.getName().c_str()));
    if (dest.isString())
        return QStringLiteral("named:%1").arg(QString::fromUtf8(dest.getStringValue().c_str()));
    if (!dest.isArray())
        return QStringLiteral("other");
    QStringList parts;
    for (int i = 1; i < dest.getArrayNItems(); ++i) {
        const QPDFObjectHandle arg = dest.getArrayItem(i);
        if (arg.isName())
            parts << QString::fromUtf8(arg.getName().c_str());
        else if (arg.isNull())
            parts << QStringLiteral("null");
        else if (arg.isNumber())
            parts << QString::number(arg.getNumericValue(), 'g', 6);
        else
            parts << QStringLiteral("...");
    }
    return QStringLiteral("[%1]").arg(parts.join(QLatin1Char(' ')));
}

bool walkOutlineFile(const QString &path, OutlineWalk &out, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        QPDFOutlineDocumentHelper helper(qpdf);
        std::function<void(std::vector<QPDFOutlineObjectHelper>)> walk =
            [&](std::vector<QPDFOutlineObjectHelper> items) {
                for (QPDFOutlineObjectHelper &item : items) {
                    ++out.total;
                    out.titles.append(QString::fromUtf8(item.getTitle().c_str()));
                    out.destinations.append(describeDestination(item.getDest()));
                    walk(item.getKids());
                }
            };
        const std::vector<QPDFOutlineObjectHelper> tops = helper.getTopLevelOutlines();
        out.top = int(tops.size());
        walk(tops);
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// Take one key out of a file the product's own writer produced. This is §63's control promoted to
// a standing assertion: it says the tree we wrote is held up by those keys rather than by anything
// a reader guesses, and it is the only way the "5 items" check below can be told apart from a
// reader that ignores the structure entirely.
bool breakOutlineKey(const QString &source, const QString &destination, const QString &which,
                     QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        QPDFObjectHandle root = qpdf.getRoot().getKey("/Outlines");
        if (root.isNull()) {
            *error = QStringLiteral("no /Outlines to break");
            return false;
        }
        if (which == QStringLiteral("root-ends")) {
            root.removeKey("/First");
            root.removeKey("/Last");
        } else if (which == QStringLiteral("first-next")) {
            root.getKey("/First").removeKey("/Next");
        } else {
            *error = QStringLiteral("unknown break %1").arg(which);
            return false;
        }
        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

// One bookmark as the sidebar's own tree reports it: what it is called and which page it reaches.
// Read off the assembled tree rather than off the file's keys, because the question §69's design
// rests on is "which page does this bookmark land on once the pages have moved", and that is a
// question about what a reader can click.
struct BookmarkRead
{
    QString title;
    int page = -1;
};

// Depth-first, in the order a sidebar draws them, using Outline's own index so the nesting is the
// reader's rather than something this test re-walks.
QVector<BookmarkRead> bookmarksAsTheSidebarSees(const Outline &tree)
{
    QVector<BookmarkRead> seen;
    for (int index = 0; index < tree.count(); ++index) {
        const OutlineNode *node = tree.nodeAt(index);
        if (node != nullptr)
            seen.append({node->title, node->page});
    }
    return seen;
}

QString bookmarksAsText(const QVector<BookmarkRead> &seen)
{
    QStringList parts;
    parts.reserve(seen.size());
    for (const BookmarkRead &one : seen)
        parts << QStringLiteral("%1@%2").arg(one.title).arg(one.page);
    return parts.join(QStringLiteral(", "));
}

// What the sidebar would show for a file on disk, as "title@page" pairs. A helper because a split
// asks the same question of two files in one block, and asking it inline needs a backend declared
// beside the assertion that reads it.
QString bookmarksShownInFile(const QString &path)
{
    QtPdfBackend reader;
    if (reader.load(path, QString()) != Document::Status::Ready)
        return QStringLiteral("(the file would not open)");
    return bookmarksAsText(bookmarksAsTheSidebarSees(reader.outline()));
}

// The directive layer as the file states it, read from the outside: what the document says, what
// each field is therefore told, and whether the drawing each field carries can still find the font
// it invokes.
struct DirectiveReport
{
    bool hasForm = false;
    bool formHasDA = false;
    bool formHasDR = false;
    bool needAppearances = false;
    QStringList formFonts;      // "<key>=<BaseFont>" for every font the document offers
    int terminals = 0;
    QStringList ownDirectives;  // per field: "own /DA | own /Q", the ones it states itself
    QStringList givenDirectives;  // per field: what the inheritance walk hands it
    QStringList givenFonts;   // per field: the font that /DA names, resolved against /DR
    int drawings = 0;
    QStringList drawingFonts;   // per drawing: "invoked=<name> resolves=<BaseFont>"
    QStringList unresolved;     // per drawing: names invoked that its own /Resources cannot give
};

DirectiveReport readDirectives(const QString &path)
{
    DirectiveReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
        report.hasForm = form.isDictionary();
        if (!report.hasForm)
            return report;
        report.formHasDA = form.getKey("/DA").isString();
        report.formHasDR = form.getKey("/DR").isDictionary();
        report.needAppearances = form.getKey("/NeedAppearances").getBoolValue();
        const QPDFObjectHandle documentFonts = form.getKey("/DR").getKey("/Font");
        if (documentFonts.isDictionary()) {
            for (const std::string &key : documentFonts.getKeys()) {
                report.formFonts << QStringLiteral("%1=%2")
                     .arg(QString::fromUtf8(key.c_str()),
                          QString::fromUtf8(documentFonts.getKey(key)
                                .getKey("/BaseFont").getName().c_str()));
            }
        }

        static const QRegularExpression fontUse(
            QStringLiteral("/([A-Za-z0-9._]+)\\s+[0-9.]+\\s+Tf"),
            QRegularExpression::UseUnicodePropertiesOption);

        const QPDFObjectHandle fields = form.getKey("/Fields");
        for (int index = 0; fields.isArray() && index < fields.getArrayNItems(); ++index) {
            QPDFObjectHandle field = fields.getArrayItem(index);
            ++report.terminals;

            // Asked of qpdf's own inheritance walk rather than of the dictionary, because that walk
            // is what a viewer does: a field with no /DA of its own is handed the document's. The
            // helper is non-const by nature (fourth time this suite has been told a QPDFObjectHandle
            // it wraps can be modified through it), so no const here.
            QPDFFormFieldObjectHelper helper(field);
            const std::string given = helper.getDefaultAppearance();
            report.givenDirectives << QStringLiteral("%1|Q%2")
                 .arg(QString::fromUtf8(given.c_str())).arg(helper.getQuadding());
            // And then the question the collision case turns on: the /DA it is handed names a font
            // **by key**, and the key means whatever the /DR of the document it now sits in says.
            if (given.empty()) {
                report.givenFonts << QStringLiteral("<no /DA handed>");
            } else {
                const QRegularExpressionMatch named =
                    fontUse.match(QString::fromUtf8(given.c_str()));
                if (!named.hasMatch()) {
                    report.givenFonts << QStringLiteral("<no font in the /DA>");
                } else {
                    const QString key = QStringLiteral("/%1").arg(named.captured(1));
                    const QPDFObjectHandle offered =
                        documentFonts.getKey(key.toUtf8().constData());
                    report.givenFonts << QStringLiteral("%1 means=%2")
                         .arg(key, offered.isDictionary()
                                   ? QString::fromUtf8(
                                         offered.getKey("/BaseFont").getName().c_str())
                                   : QStringLiteral("<not offered by this document>"));
                }
            }
            report.ownDirectives << QStringLiteral("%1|Q%2")
                 .arg(field.getKey("/DA").isString()
                          ? QString::fromUtf8(field.getKey("/DA").getStringValue().c_str())
                          : QStringLiteral("<none>"),
                      field.getKey("/Q").isInteger()
                          ? QString::number(int(field.getKey("/Q").getIntValue()))
                          : QStringLiteral("<none>"));

            // Descend only into a dictionary: asking a null object for a key is answered with a
            // WARNING line, and the undrawn field below has no /AP at all by construction.
            const QPDFObjectHandle states = field.getKey("/AP");
            QPDFObjectHandle normal =
                states.isDictionary() ? states.getKey("/N") : QPDFObjectHandle::newNull();
            if (!normal.isStream())
                continue;
            ++report.drawings;
            const std::shared_ptr<Buffer> bytes = normal.getStreamData();
            if (!bytes)
                continue;
            const QByteArray paint = QByteArray::fromRawData(
                reinterpret_cast<const char*>(bytes->getBuffer()), int(bytes->getSize()));
            const QRegularExpressionMatch match = fontUse.match(paint);
            if (!match.hasMatch()) {
                report.unresolved << QStringLiteral("<no Tf in the drawing>");
                continue;
            }
            const QString used = QStringLiteral("/%1").arg(match.captured(1));
            // Through getDict(), like the write side (§55's trap, and the same one on the read
            // side): asking a stream handle for a key is answered with a WARNING line and a null,
            // which is how "the drawing cannot find its font" got reported before it was checked.
            const QPDFObjectHandle resolved = normal.getDict()
                                                 .getKey("/Resources")
                                                 .getKey("/Font")
                                                 .getKey(used.toUtf8().constData());
            if (resolved.isDictionary()) {
                report.drawingFonts << QStringLiteral("%1 resolves=%2")
                     .arg(used, QString::fromUtf8(resolved.getKey("/BaseFont").getName().c_str()));
            } else {
                report.unresolved << used;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the directive layer out of " << path.toStdString()
                  << ": " << e.what() << '\n';
        ++failures;
    }
    return report;
}

// How a file stands with respect to annotations: what each page carries, whether the page
// that carries an annotation is the one its own `/P` names, whether any annotation object is
// shared between two pages, and how many fields the document's /AcroForm points at.
//
// The three counts are the three ways a shallow page copy can go wrong, and they are asked
// of the file rather than of the library call: qpdf documents fixCopiedAnnotations() as
// "ensures that each separate copy of a page has private annotations", and that sentence is
// only checkable from the outside.
struct AnnotationReport
{
    QVector<int> countPerPage;
    int notesNamingAnotherPage = 0;
    int objectsSharedByTwoPages = 0;
    int fields = 0;
    int fieldsNamingNoPage = 0;
    QStringList fieldNames;
    QStringList fieldValues;
};

AnnotationReport readAnnotations(const QString &path)
{
    AnnotationReport report;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        QSet<int> pageIds;
        QSet<int> seenAnnotations;
        for (const QPDFPageObjectHelper &page : pages) {
            const QPDFObjectHandle handle = page.getObjectHandle();
            pageIds.insert(handle.getObjGen().getObj());
            const QPDFObjectHandle annots = handle.getKey("/Annots");
            int counted = 0;
            for (int index = 0; annots.isArray() && index < annots.getArrayNItems(); ++index) {
                const QPDFObjectHandle item = annots.getArrayItem(index);
                ++counted;
                const QPDFObjectHandle owner = item.getKey("/P");
                if (!owner.isDictionary()
                    || owner.getObjGen().getObj() != handle.getObjGen().getObj())
                    ++report.notesNamingAnotherPage;
                if (seenAnnotations.contains(item.getObjGen().getObj()))
                    ++report.objectsSharedByTwoPages;
                seenAnnotations.insert(item.getObjGen().getObj());
            }
            report.countPerPage.append(counted);
        }
        const QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
        const QPDFObjectHandle fields = form.getKey("/Fields");
        for (int index = 0; fields.isArray() && index < fields.getArrayNItems(); ++index) {
            ++report.fields;
            const QPDFObjectHandle item = fields.getArrayItem(index);
            // Asked with a type check rather than directly, because once names are assembled a
            // /Fields entry is usually a *parent*, which carries no value at all -- and qpdf
            // answers a type mismatch with a WARNING line on stderr plus an empty string, so an
            // unguarded read of a whole form list prints three complaints that look like the
            // file being broken when it is the reader being rude.
            const QPDFObjectHandle name = item.getKey("/T");
            const QPDFObjectHandle value = item.getKey("/V");
            report.fieldNames << (name.isString()
                                      ? QString::fromUtf8(name.getStringValue().c_str())
                                      : QString());
            report.fieldValues << (value.isString()
                                       ? QString::fromUtf8(value.getStringValue().c_str())
                                       : QString());
            const QPDFObjectHandle owner = item.getKey("/P");
            if (!owner.isDictionary() || !pageIds.contains(owner.getObjGen().getObj()))
                ++report.fieldsNamingNoPage;
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL: could not read the annotations out of " << path.toStdString() << ": "
                  << e.what() << '\n';
        ++failures;
    }
    return report;
}
//
// This is the same number QPDF reports through isEncrypted(R, P), read a second way,
// and the reason for the second way is that the claim under test spans two files that
// one parser reads: "the copy carries the protection the original had". If the writer
// dropped the dictionary and the reader quietly reported a default for both, asking
// QPDF alone would hear the same wrong answer twice. The dictionary is never itself
// encrypted and never lives in an object stream, so these bytes are in the file
// exactly as the writer put them.
qint64 rawPermissionBits(const QString &path, bool *found)
{
    *found = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray bytes = file.readAll();
    file.close();

    // Trailer first: "/Encrypt 12 0 R" is what names the dictionary's object number.
    static const QRegularExpression reference(
        QStringLiteral("/Encrypt\\s+(\\d+)\\s+\\d+\\s+R"));
    const QRegularExpressionMatch ref = reference.match(QString::fromLatin1(bytes));
    if (!ref.hasMatch())
        return 0;
    const QByteArray object = ref.captured(1).toLatin1() + " 0 obj";
    const int start = bytes.indexOf(object);
    const int stop = bytes.indexOf("endobj", start);
    if (start < 0 || stop < 0)
        return 0;

    // The "\s" is what keeps this off /Perms and /PageRange: the bitfield is written
    // as its own key, followed by whitespace and a signed integer.
    const QRegularExpression bits(
        QStringLiteral("/P\\s+(-?\\d+)"), QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch hit = bits.match(QString::fromLatin1(
        bytes.mid(start, stop - start)));
    if (!hit.hasMatch())
        return 0;
    *found = true;
    return hit.captured(1).toLongLong();
}

// What a file says about what may be done with it, from both readers at once.
struct Permissions
{
    bool readable = false;   // QPDF opened it at all with this password
    bool encrypted = false;
    int r = 0;
    qint64 p = 0;
    qint64 rawP = 0;
    bool rawPFound = false;
    bool modifyAssembly = false;
    bool modifyAnnotation = false;
    bool modifyOther = false;
    QString complaint;
};

Permissions readPermissions(const QString &path, const QString &password)
{
    Permissions out;
    out.rawP = rawPermissionBits(path, &out.rawPFound);
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData(), password.toUtf8().constData());
        out.readable = true;
        // isEncrypted takes its R and P as int&, so the struct's qint64 (which is
        // what /P holds in the file) is filled from a local rather than passed in.
        int bits = 0;
        out.encrypted = qpdf.isEncrypted(out.r, bits);
        out.p = bits;
        if (out.encrypted) {
            out.modifyAssembly = qpdf.allowModifyAssembly();
            out.modifyAnnotation = qpdf.allowModifyAnnotation();
            out.modifyOther = qpdf.allowModifyOther();
        }
    } catch (const std::exception &e) {
        out.complaint = QString::fromUtf8(e.what());
    }
    return out;
}

QString describe(const Permissions &p)
{
    return QStringLiteral("R%1 P%2 (raw %3%4) assembly=%5 annotate=%6 other=%7")
        .arg(p.r)
        .arg(p.p)
        .arg(p.rawPFound ? QString::number(p.rawP) : QStringLiteral("absent"))
        .arg(p.encrypted ? QString() : QStringLiteral(", not encrypted"))
        .arg(p.modifyAssembly)
        .arg(p.modifyAnnotation)
        .arg(p.modifyOther);
}

// The six permission boxes as the file itself answers them.
//
// Read back with the same six accessors DocumentEditor::open() uses to fill
// `sourceProtection()`, and that pairing is the point of the struct: the dialog opens on
// what these read, and a save writes what the boxes say, so "the box that is ticked" and
// "the box that reaches the file" are the same six words only if both ends look at the same
// bit. The numbers in PDF's /P field belong to qpdf's writer, so they are not named here --
// what is checked is that the six are six, which is what a dialog with six boxes claims.
struct BoxFlags
{
    bool print = false;
    bool extract = false;
    bool assemble = false;
    bool annotate = false;
    bool form = false;
    bool other = false;
    // False when there was no /Encrypt to ask, or the file would not open with the
    // password given. Without it a refused open and an unrestricted file look the same.
    bool answered = false;
};

BoxFlags boxesAsWritten(const QString &path, const QString &password)
{
    BoxFlags out;
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData(), password.toUtf8().constData());
        if (!qpdf.isEncrypted())
            return out;
        out.print = qpdf.allowPrintHighRes();
        out.extract = qpdf.allowExtractAll();
        out.assemble = qpdf.allowModifyAssembly();
        out.annotate = qpdf.allowModifyAnnotation();
        out.form = qpdf.allowModifyForm();
        out.other = qpdf.allowModifyOther();
        out.answered = true;
    } catch (const std::exception &) {
        // The caller asks a different question -- "did it need this password" is checked by
        // the reader above -- and this is the empty answer, with `answered` saying so.
    }
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    // A QGuiApplication, not a core one, because 水印 paints: the watermark is drawn by Qt into
    // a bitmap before QPDF carries the pixels anywhere, and QPainter refuses to work without a
    // platform. Offscreen rather than the desktop window platform because this runs in a gate
    // that must not open a window.
    //
    // That choice has one consequence worth naming where it is made: under offscreen Qt resolves
    // no real font, so every glyph comes out as a box (tests/watermarkprobe.cpp control C6 is
    // what measured that). The watermark checks below therefore assert *structure* -- which page
    // draws a masked image, how many such images a file holds, what /ca says -- and leave "do
    // the marks read as the words" to the probe, which is run under the windows platform by hand.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);

    // ---- what a delete request means, with no document anywhere near it ----
    //
    // The arithmetic the dialog shows while the range is being typed, so the two
    // numbers a reader acts on -- how many go, how many are left -- come from the
    // function that also decides whether OK is pressable. `live` is spelled out
    // here because that is the whole point of it: a request means something against
    // the pages that are still there, and the dialog and the editor have to agree
    // about which those are.
    using Plan = DocumentEditor::DeletePlan;
    {
        const Plan simple = DocumentEditor::planDelete(everyRef(10), {2, 3, 4});
        check(simple.pagesToRemove == 3 && simple.pagesRemaining == 7 && !simple.leavesNothing,
              "three pages out of ten leaves seven");

        const Plan repeated = DocumentEditor::planDelete(everyRef(10), {2, 3, 2, 99, -1});
        check(repeated.pagesToRemove == 2 && repeated.pagesRemaining == 8,
              "a page named twice comes out once, and a number outside the document names nothing");

        const Plan all = DocumentEditor::planDelete(everyRef(3), {0, 1, 2});
        check(all.pagesToRemove == 3 && all.pagesRemaining == 0 && all.leavesNothing,
              "naming every page is the request that cannot be granted");

        const Plan none = DocumentEditor::planDelete(everyRef(3), {});
        check(none.pagesToRemove == 0 && !none.leavesNothing,
              "naming no page is a different problem from naming every one");

        const Plan oneLeft = DocumentEditor::planDelete(everyRef(3), {0, 2});
        check(oneLeft.pagesToRemove == 2 && oneLeft.pagesRemaining == 1 && !oneLeft.leavesNothing,
              "leaving exactly one page is allowed -- it is the smallest document there is");

        // The dialog's "these are already gone" sentence, and the count behind it:
        // pages 0 and 1 are not in the document any more, so a second request that
        // names them removes nothing -- but it is not the same nothing as naming no
        // page at all, which is why alreadyGone is its own number.
        const Plan halfGone = DocumentEditor::planDelete(onScreen({2, 3, 4}), {0, 1, 3});
        check(halfGone.pagesToRemove == 1 && halfGone.alreadyGone == 2
                && halfGone.pagesRemaining == 2,
              "pages an earlier change took out are counted separately from pages never named");

        const Plan allGone = DocumentEditor::planDelete(onScreen({2, 3}), {0, 1});
        check(allGone.pagesToRemove == 0 && allGone.alreadyGone == 2 && !allGone.leavesNothing,
              "naming only gone pages removes nothing, and says why rather than \"you named nothing\"");

        // Naming every page that is left is the ungrantable one, even when the
        // document has already shrunk: what matters is the count going out.
        const Plan restOfThem = DocumentEditor::planDelete(onScreen({2, 3}), {2, 3});
        check(restOfThem.pagesToRemove == 2 && restOfThem.leavesNothing,
              "the second delete that empties the document is refused as surely as the first");

        // A merge is what made these counts worth re-reading: the list on the left
        // now holds pages the screen never showed, and they are counted in what is
        // left but are unreachable from the numbers typed on the right. The dialog
        // has to say so, because "剩下 40 页" beside a 40-page document that is
        // losing three is otherwise a sentence a reader can check and find wrong.
        const Plan pastTheEnd = DocumentEditor::planDelete(
            everyRef(3) << Ref{1, 0} << Ref{1, 1}, {0, 1, 2});
        check(pastTheEnd.pagesToRemove == 3 && pastTheEnd.pagesRemaining == 2
                && !pastTheEnd.leavesNothing,
              "deleting every page of the opened file is not deleting every page of the "
              "document, once a merge has put two more into it");

        const Plan imported = DocumentEditor::planDelete(everyRef(3) << Ref{1, 0}, {0});
        check(imported.pagesToRemove == 1 && imported.pagesRemaining == 3,
              "and the page that came in from another file is in that remaining count, but "
              "is not reachable from the numbers a reader can type");
    }

    // ---- what a move request means, with no document anywhere near it ----
    //
    // The same arithmetic the dialog shows and the save replays, run against a
    // `present` list written out by hand. Every one of these orders is the whole
    // answer rather than a count, because the thing a move can get wrong is which
    // page sits where.
    using Move = DocumentEditor::MoveTarget;
    {
        const DocumentEditor::MovePlan toBack = DocumentEditor::planMove(everyRef(10), {2, 3, 4},
                                                                        Move::Back);
        check(toBack.pagesMoved == 3 && asNumbers(toBack.order).last() == 4 && toBack.landsAt == 7
                && toBack.changesOrder && toBack.usable(),
              QStringLiteral("three pages moved to the back land at 8, 9, 10: %1")
                  .arg(describeOrder(asNumbers(toBack.order))));

        const DocumentEditor::MovePlan toFront = DocumentEditor::planMove(everyRef(10), {7, 8},
                                                                         Move::Front);
        check(asNumbers(toFront.order).front() == 7 && asNumbers(toFront.order).at(1) == 8
                && asNumbers(toFront.order).at(2) == 0 && toFront.landsAt == 0,
              QStringLiteral("the block keeps its own internal order at the front: %1")
                  .arg(describeOrder(asNumbers(toFront.order))));

        // Typed the other way round, and it still comes out 2,3,4: the numbers name
        // which pages travel, not the order they arrive in.
        const DocumentEditor::MovePlan typedBackwards = DocumentEditor::planMove(
            everyRef(10), {4, 2, 3}, Move::Back);
        check(asNumbers(typedBackwards.order).mid(7) == QVector<int>({2, 3, 4}),
              QStringLiteral("naming them out of order does not shuffle them: %1")
                  .arg(describeOrder(asNumbers(typedBackwards.order))));

        const DocumentEditor::MovePlan behind = DocumentEditor::planMove(everyRef(10), {0, 1},
                                                                        Move::AfterPage, 4);
        check(asNumbers(behind.order) == QVector<int>({2, 3, 4, 0, 1, 5, 6, 7, 8, 9})
                && behind.landsAt == 3 && behind.anchorUsed == 4 && !behind.anchorWasGone,
              QStringLiteral("the first two pages move behind the fifth, which is page 5 on the "
                             "screen and index 4 here: %1")
                  .arg(describeOrder(asNumbers(behind.order))));

        // Three ways to ask for the order the document already has, and each one has
        // to come out false so that the button greys and nothing is recorded.
        check(!DocumentEditor::planMove(everyRef(5), {3, 4}, Move::Back).changesOrder,
              "the last two pages are already at the back");
        check(!DocumentEditor::planMove(everyRef(5), {0}, Move::Front).changesOrder,
              "the first page is already at the front");
        check(!DocumentEditor::planMove(everyRef(5), {2}, Move::AfterPage, 1).changesOrder,
              "page 3 is already behind page 2");

        const DocumentEditor::MovePlan ontoItself = DocumentEditor::planMove(everyRef(5), {1, 2},
                                                                            Move::AfterPage, 2);
        check(ontoItself.anchorIsMoved && !ontoItself.usable(),
              "moving a block behind a page inside itself is not a move, and says which page "
              "makes it that way");

        const DocumentEditor::MovePlan namedNothing = DocumentEditor::planMove(everyRef(5), {},
                                                                              Move::Back);
        check(namedNothing.pagesMoved == 0 && !namedNothing.changesOrder && !namedNothing.usable()
                && asNumbers(namedNothing.order) == everyPage(5),
              "naming no page moves nothing, and the order it hands back is the one it was given");

        const DocumentEditor::MovePlan repeats = DocumentEditor::planMove(everyRef(10), {2, 2, 3},
                                                                         Move::Back);
        check(repeats.pagesMoved == 2 && asNumbers(repeats.order).mid(8) == QVector<int>({2, 3}),
              QStringLiteral("a page named twice travels once: %1")
                  .arg(describeOrder(asNumbers(repeats.order))));

        // The two questions a page number raises once unsaved changes exist are
        // separate: is it still there, and where does the anchor land if it is not.
        const DocumentEditor::MovePlan gonePages = DocumentEditor::planMove(onScreen({2, 3, 4}),
                                                                           {0, 1, 3}, Move::Back);
        check(gonePages.pagesMoved == 1 && gonePages.alreadyGone == 2
                && asNumbers(gonePages.order) == QVector<int>({2, 4, 3}),
              "pages an earlier change deleted are counted apart from the one still there");

        const DocumentEditor::MovePlan goneAnchor = DocumentEditor::planMove(
            onScreen({0, 1, 2, 4, 5}), {5}, Move::AfterPage, 3);
        check(goneAnchor.anchorWasGone && goneAnchor.anchorUsed == 4
                && asNumbers(goneAnchor.order) == QVector<int>({0, 1, 2, 5, 4})
                && goneAnchor.usable(),
              QStringLiteral("an anchor a delete took out becomes the gap it left, in front of "
                             "the page that is now there: %1")
                  .arg(describeOrder(asNumbers(goneAnchor.order))));

        const DocumentEditor::MovePlan anchorAtEnd = DocumentEditor::planMove(onScreen({0, 1, 2}),
                                                                             {0}, Move::AfterPage, 5);
        check(anchorAtEnd.anchorWasGone && anchorAtEnd.anchorUsed == -1
                && asNumbers(anchorAtEnd.order) == QVector<int>({1, 2, 0}),
              "and an anchor past the last surviving page is the same answer as moving to the end");

        // A block of imported pages: they travel as a block and keep their own order,
        // but no number a reader types reaches them, so this is the undo-and-move case
        // rather than a dialog. The anchor on the right is still a page of the opened
        // document -- the imported pages in the list do not shift what 第 N 页 means.
        const DocumentEditor::MovePlan importedBlock = DocumentEditor::planMove(
            QVector<Ref>() << Ref{1, 0} << Ref{1, 1} << Ref{0, 0} << Ref{0, 1},
            {}, Move::AfterPage, 0);
        check(importedBlock.pagesMoved == 0 && !importedBlock.changesOrder,
              "naming no page of the opened document moves nothing, even with imported pages "
              "in front");
        const DocumentEditor::MovePlan behindTheImported = DocumentEditor::planMove(
            QVector<Ref>() << Ref{0, 0} << Ref{1, 0} << Ref{1, 1} << Ref{0, 1},
            {0}, Move::AfterPage, 1);
        check(behindTheImported.usable()
                && asNumbers(behindTheImported.order) == QVector<int>({100, 101, 1, 0}),
              QStringLiteral("page 1 moves behind page 2, which is sitting behind two imported "
                             "ones, so it lands after them: %1")
                  .arg(describeOrder(asNumbers(behindTheImported.order))));
    }

    // ---- what a merge request means, with no document anywhere near it ----
    //
    // The same shape as the two blocks above, and the same reason for existing: the
    // dialog's summary and the save's replay both go through planMerge(), so the place
    // a block of pages lands is worked out once. That is the whole point of the
    // landing spot being shared with planMove() -- "第 12 页之后" is one place in this
    // program, not one place per operation.
    {
        const DocumentEditor::MergePlan atBack = DocumentEditor::planMerge(
            everyRef(5), {0, 1, 2}, 1, Move::Back);
        check(atBack.pagesAdded == 3 && atBack.landsAt == 5 && atBack.usable()
                && asNumbers(atBack.order) == QVector<int>({0, 1, 2, 3, 4, 100, 101, 102}),
              QStringLiteral("three pages from the other file join the end, in the order that "
                             "file has them: %1")
                  .arg(describeOrder(asNumbers(atBack.order))));

        const DocumentEditor::MergePlan atFront = DocumentEditor::planMerge(
            everyRef(5), {1}, 2, Move::Front);
        check(atFront.pagesAdded == 1 && atFront.landsAt == 0
                && asNumbers(atFront.order) == QVector<int>({201, 0, 1, 2, 3, 4}),
              QStringLiteral("one page from a second merged file goes to the very front: %1")
                  .arg(describeOrder(asNumbers(atFront.order))));

        // Typed the other way round in the range box, and it still arrives 1 then 3:
        // the numbers say which pages come, not the order they come in.
        check(asNumbers(DocumentEditor::planMerge(everyRef(2), {3, 1}, 1, Move::Back).order)
                  == QVector<int>({0, 1, 101, 103}),
              "naming them out of order does not shuffle the block, either here");

        const DocumentEditor::MergePlan behind = DocumentEditor::planMerge(
            everyRef(5), {0}, 1, Move::AfterPage, 2);
        check(behind.landsAt == 3 && behind.anchorUsed == 2 && !behind.anchorWasGone
                && asNumbers(behind.order) == QVector<int>({0, 1, 2, 100, 3, 4}),
              QStringLiteral("the merge lands behind the third page, which is where a move "
                             "behind it would have gone: %1")
                  .arg(describeOrder(asNumbers(behind.order))));

        // The anchor a delete left behind, asked of a merge rather than of a move. It
        // has to come to the same place, or the two dialogs would be pointing at
        // different gaps with the same sentence.
        const DocumentEditor::MergePlan goneAnchor = DocumentEditor::planMerge(
            onScreen({0, 1, 2, 4, 5}), {0}, 1, Move::AfterPage, 3);
        check(goneAnchor.anchorWasGone && goneAnchor.anchorUsed == 4
                && asNumbers(goneAnchor.order) == QVector<int>({0, 1, 2, 100, 4, 5}),
              "a merged block goes into the gap a deleted anchor left, exactly where a move "
              "would have put it");

        // Repeats and out-of-range numbers cannot reach this function from a dialog --
        // the range box is bounded by the other file's own page count -- but a merge of
        // one page named twice is one page, and that is the only thing that varies.
        check(DocumentEditor::planMerge(everyRef(3), {2, 2}, 1, Move::Back).pagesAdded == 1,
              "a page named twice comes in once");

        // The one case with no counterpart in a move: a merge never takes anything out,
        // so the document cannot be emptied by one, and nothing here refuses a merge of
        // zero pages except the dialog's greyed button. planMerge hands back the list it
        // was given.
        const DocumentEditor::MergePlan namedNothing = DocumentEditor::planMerge(
            everyRef(3), {}, 1, Move::Back);
        check(namedNothing.pagesAdded == 0 && !namedNothing.usable()
                && asNumbers(namedNothing.order) == everyPage(3),
              "naming no page adds nothing, and the order it hands back is the one it was given");

        // Merging into a document that already holds imported pages: the anchor is a
        // page of the opened file, so it resolves against that file's own order rather
        // than against the position the imported pages happen to sit in.
        const DocumentEditor::MergePlan secondMerge = DocumentEditor::planMerge(
            QVector<Ref>() << Ref{0, 0} << Ref{1, 0} << Ref{0, 1}, {0}, 2, Move::AfterPage, 0);
        check(secondMerge.landsAt == 1
                && asNumbers(secondMerge.order) == QVector<int>({0, 200, 100, 1}),
              QStringLiteral("behind page 1 means behind it, with the earlier merge's page "
                             "pushed along: %1")
                  .arg(describeOrder(asNumbers(secondMerge.order))));
    }

    // ---- whether two paths name one file ----
    {
        check(DocumentEditor::sameFile(QStringLiteral("CMakeLists.txt"),
                                       QFileInfo(QStringLiteral("CMakeLists.txt"))
                                           .absoluteFilePath()),
              "a path and its own absolute form are the same file");
        const QString absolute = QFileInfo(QStringLiteral("CMakeLists.txt")).absoluteFilePath();
        check(DocumentEditor::sameFile(absolute, absolute.toUpper()),
              "and the compare does not care about case, because Windows does not either");
        check(DocumentEditor::sameFile(absolute,
                                       QString(absolute).replace(QLatin1Char('/'),
                                                                QLatin1Char('\\'))),
              "nor about which way the separator leans");
        check(!DocumentEditor::sameFile(absolute, QStringLiteral("CMakeCache.txt")),
              "a different name in the same folder is a different file");
        check(!DocumentEditor::sameFile(QString(), absolute), "and an empty path is nobody's file");
    }

    // ---- the round trip ----
    const QString fixture = QStringLiteral("tests/fixtures/many-pages.pdf");
    if (!QFile::exists(fixture)) {
        std::cerr << "FAIL: the fixture this test needs is not there\n";
        return 1;
    }
    QTemporaryDir scratch;
    check(scratch.isValid(), "the test gets a temporary directory of its own");
    if (!scratch.isValid())
        return 1;

    const QString originalHash = hashOfFile(fixture);
    const QString source = scratch.filePath(QStringLiteral("source.pdf"));
    check(QFile::copy(fixture, source), "the document worked on is a copy in the scratch folder");
    const int pagesBefore = pagesAsReadByTheReader(source);
    check(pagesBefore > 3, "and it has enough pages to take two out of");

    {
        DocumentEditor editor;
        check(editor.pageCount() == 0, "an editor that has not opened anything holds no pages");
        QString error;
        // The call is hoisted out of check()'s second argument wherever that
        // argument quotes the error: arguments are built before the call inside
        // them has run, so writing check(f(&error), "...%1".arg(error)) prints an
        // empty explanation for a failure it is reporting.
        const bool opened = editor.open(source, QString(), &error);
        check(opened, QStringLiteral("the editor opens the copy: %1").arg(error));
        check(editor.pageCount() == pagesBefore,
              "and counts its pages the way the reader counts them");

        // Refused before a byte is written, and refused by name rather than by
        // accident: a save dialog is entirely capable of handing back the file it
        // was seeded with.
        error.clear();
        check(!editor.saveAs(source, &error), "saving back over the document it opened is refused");
        check(!error.isEmpty(), "and the refusal says why, rather than returning false silently");
        check(hashOfFile(source) == originalHash, "the file it tried to overwrite is unchanged");

        error.clear();
        check(!editor.deletePages(everyPage(pagesBefore), &error) && !error.isEmpty(),
              "deleting every page is refused, with a sentence");
        check(editor.pageCount() == pagesBefore, "and the refusal changed nothing");

        error.clear();
        const bool removed = editor.deletePages({2, 4}, &error);
        check(removed, QStringLiteral("deleting two named pages is accepted: %1").arg(error));
        check(editor.pageCount() == pagesBefore - 2, "and the document in memory is two pages short");

        // ---- the list, moved back and forth ----
        //
        // Undo and redo are moves between two lists, so what is being checked here
        // is not "does the page come back" -- it is that nothing else moves with it:
        // the page count after a redo has to be the count before the undo, and the
        // save below has to write the same file it would have written had nobody
        // pressed anything.
        check(editor.isDirty() && editor.pendingCount() == 1,
              "one operation is pending, and the tab would say so");
        check(editor.canUndo() && !editor.canRedo(), "there is something to undo and nothing to redo");
        check(!editor.undoDescription().isEmpty() && !editor.pendingSummary().isEmpty(),
              "and the list can name what it would take back");
        // Spelled out in full rather than "non-empty", because this row is built by a
        // plural-aware lookup and `%n` is filled in by translate() itself. If it were not
        // filled -- which is what happens on the path with no catalogue installed, i.e. the
        // Chinese UI -- the row would read 删除 %n 页 on screen, and every check above would
        // still be green because they only ever asked whether a row exists.
        check(editor.undoDescription() == QStringLiteral("删除 2 页"),
              QStringLiteral("the Chinese row names its own count (got \"%1\")")
                  .arg(editor.undoDescription()));
        check(editor.undo(), "undo gives back the last operation");
        check(editor.pageCount() == pagesBefore, "which puts both pages back where they were");
        check(!editor.isDirty() && !editor.canUndo() && editor.canRedo(),
              "an undone list is an empty list: the star goes and there is something to redo");
        check(editor.redo(), "redo takes it back out again");
        check(editor.pageCount() == pagesBefore - 2, "and the count is the one before the undo");

        // A new operation after an undo throws the undone branch away, which is the
        // convention everywhere else in computing and the one that keeps two
        // histories from disagreeing about what the document is.
        check(editor.undo(), "setup: the delete is taken back once more");
        check(editor.rotatePages({1}, 1, &error), "rotating a page while the delete is undone");
        check(!editor.canRedo(), "and the redo branch is gone, because the list went another way");
        check(editor.undo(), "setup: the rotation is taken back too");
        check(editor.redo(), "setup: and put back");
        check(editor.pageCount() == pagesBefore, "a rotation does not change how many pages there are");
        check(editor.undo(), "setup: back to nothing pending");
        check(editor.deletePages({2, 4}, &error), "setup: the list is the one the save writes");

        const QString result = scratch.filePath(QStringLiteral("two-shorter.pdf"));
        QElapsedTimer clock;
        clock.start();
        error.clear();
        const bool wrote = editor.saveAs(result, &error);
        check(wrote, QStringLiteral("and the new file is written: %1").arg(error));
        note(QStringLiteral("%1 pages, a %2 KB document: open + delete + write took %3 ms")
                 .arg(pagesBefore)
                 .arg(QFileInfo(source).size() / 1024)
                 .arg(clock.elapsed()));

        check(pagesAsReadByTheReader(result) == pagesBefore - 2,
              "the reader counts the same number of pages in the file the editor wrote");
        QtPdfBackend written;
        if (written.load(result, QString()) == Document::Status::Ready) {
            const QImage image = written.render(0, QSize(300, 400), RenderOptions{});
            check(!image.isNull() && paintsAnything(image),
                  "and the first page of it still has something on it");
        } else {
            check(false, "the reader opens the file the editor wrote");
        }

        // The two things this feature is not allowed to do, checked at the end
        // rather than assumed throughout.
        check(hashOfFile(source) == originalHash, "the document that was edited is unchanged");
        check(hashOfFile(fixture) == originalHash, "and so is the fixture it was copied from");
    }

    // ---- a page number means the page on the screen ----
    //
    // The one arithmetic mistake this design can make, and the reason operations
    // store original page numbers rather than positions: a reader who deleted the
    // first two pages and then types "3" is pointing at the third page of what they
    // can still see, not at the third page of the document a save will write. Had
    // the number been taken as a position, the checks below would come out shifted
    // by exactly the pages that had already gone -- a page deleted that nobody
    // named, and a page kept that somebody did.
    //
    // The fixture prints its own page number on each page, which is what makes this
    // answerable at all: the alternative is a page count, and a page count says the
    // right number of pages went out without saying which.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({0, 1}, &error), "the first delete takes the two pages at the front");
        check(editor.rotatePages({2, 3}, 1, &error), "then the third and fourth are turned right");
        check(editor.deletePages({3}, &error), "and the fourth is taken out, by the number on the screen");
        check(editor.rotatePages({5}, 3, &error), "and the sixth, which is further down still, is turned left");

        const QString out = scratch.filePath(QStringLiteral("original-numbers.pdf"));
        error.clear();
        check(editor.saveAs(out, &error),
            QStringLiteral("the whole list is written in one go: %1").arg(error));

        QtPdfBackend written;
        check(written.load(out, QString()) == Document::Status::Ready, "and the result opens");
        check(written.pageCount() == pagesBefore - 3,
              "three pages went out of the forty, and no more");
        // What is left, in order, is pages 3, 5, 6 … 40: page 4 was named for
        // deletion after pages 1 and 2 went, and it is the page the reader was
        // looking at when they typed it.
        check(written.text(0).simplified().contains(QStringLiteral("page 3")),
              "the first page of the result is the third page of the original");
        check(written.text(1).simplified().contains(QStringLiteral("page 5")),
              "and the one after it skips the page that was named second, not the one that "
              "happened to sit at that position by then");
        check(written.text(2).simplified().contains(QStringLiteral("page 6")),
              "and the third page of the result is the sixth of the original, which was "
              "numbered against the screen it was named on, not against the list");

        // Qt PDF folds a page's own /Rotate into the size it reports (see
        // pageSize()'s note), so a turned page comes back landscape and this needs
        // no second parser to tell which way the file says the page is.
        check(written.pageSize(0).width() > written.pageSize(0).height(),
              "and the turn landed on the page that was named");
        check(written.pageSize(1).width() < written.pageSize(1).height(),
              "while the page beside it, which was never named, is still upright");

        // The direction, which the size above cannot see: a right turn and a left
        // one both make a portrait page landscape, and a reader who asked for one
        // and got the other has a document whose pages lie the wrong way round
        // until somebody turns them back.
        const QVector<int> angles = rotationsInFile(out);
        check(angles.size() == written.pageCount(), "the file names a rotation for every page");
        if (angles.size() == written.pageCount()) {
            check(angles.at(0) == 90, "the page turned right says 90");
            check(angles.at(1) == 0, "the untouched page beside it says nothing");
            check(angles.at(2) == 270, "the one turned left says 270, which is what a left turn is");
        }
        check(pagesAsReadByTheReader(source) == pagesBefore,
              "and reading the result never changed the document it was made from");
    }

    // ---- a move writes the order it promised ----
    //
    // The one operation in this program whose failure is silent in every other
    // measurement: a wrongly reordered document has the right number of pages in
    // it and opens fine. So the order is read back off the page labels the fixture
    // prints, and it is read back after each of undo and redo as well -- an undo
    // that moved the wrong block, or a redo that did not move it back, would
    // otherwise leave this file looking exactly like the one before it.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({9}, &error),
              "page 10 goes out first, so the numbering the moves use has a hole in it");
        check(editor.pageCount() == pagesBefore - 1, "setup: and the document is one page short");

        error.clear();
        check(!editor.movePages({0}, DocumentEditor::MoveTarget::Front, -1, &error)
                && !error.isEmpty(),
              "moving the first page to the front is refused, with a sentence");
        check(!editor.movePages({2, 3}, DocumentEditor::MoveTarget::AfterPage, 2, &error),
              "moving a block behind a page that is inside the block is refused too");
        error.clear();
        check(!editor.movePages({5}, DocumentEditor::MoveTarget::AfterPage, pagesBefore, &error)
                && !error.isEmpty(),
              "and so is an anchor past the last page of the file -- there is no such page to "
              "move behind, which is a different thing from the page 10 this list has deleted");
        check(!editor.movePages({}, DocumentEditor::MoveTarget::Back, -1, &error),
              "naming no page is refused as surely as naming an impossible one");
        check(editor.pendingCount() == 1, "and not one of the four refusals was recorded");

        error.clear();
        check(editor.movePages({2, 3}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
            QStringLiteral("pages 3 and 4 move behind page 1: %1").arg(error));
        check(editor.pageCount() == pagesBefore - 1, "a move changes no page count at all");
        check(editor.pendingCount() == 2 && editor.isDirty(), "and it is the second thing on the list");
        check(editor.undoDescription().contains(QStringLiteral("第 1 页")),
              QStringLiteral("the undo item names the anchor the reader used: %1")
                  .arg(editor.undoDescription()));
        check(editor.movePages({8}, DocumentEditor::MoveTarget::Front, -1, &error),
              "then page 9 goes to the very front");

        const QString out = scratch.filePath(QStringLiteral("moved.pdf"));
        error.clear();
        check(editor.saveAs(out, &error),
            QStringLiteral("the whole list is written in one go: %1").arg(error));
        QVector<int> wanted;
        wanted << 9 << 1 << 3 << 4 << 2 << 5 << 6 << 7 << 8;
        for (int label = 11; label <= pagesBefore; ++label)
            wanted.append(label);
        const QVector<int> written = orderInFile(out);
        check(written == wanted,
              QStringLiteral("the file reads back as the two moves and one delete asked for: got "
                             "%1, wanted %2")
                  .arg(describeOrder(wanted), describeOrder(written)));

        // Undo is not "the count goes back" here; it is the block going home.
        check(editor.undo(), "the move to the front is taken back");
        const QString undoneFile = scratch.filePath(QStringLiteral("moved-one-back.pdf"));
        check(editor.saveAs(undoneFile, &error), "and the shorter list writes again");
        QVector<int> wantedUndone;
        wantedUndone << 1 << 3 << 4 << 2 << 5 << 6 << 7 << 8 << 9;
        for (int label = 11; label <= pagesBefore; ++label)
            wantedUndone.append(label);
        const QVector<int> undone = orderInFile(undoneFile);
        check(undone == wantedUndone,
              QStringLiteral("with only the second move gone, page 9 is back in the middle and "
                             "nothing else shifted: got %1, wanted %2")
                  .arg(describeOrder(wantedUndone), describeOrder(undone)));

        check(editor.redo(), "redo puts the move back on the list");
        const QString redoneFile = scratch.filePath(QStringLiteral("moved-redone.pdf"));
        check(editor.saveAs(redoneFile, &error), "and it writes");
        check(orderInFile(redoneFile) == written,
              "the redone document is the same order as the one before the undo");

        // Two moves whose blocks overlap, which is the case a position-based design
        // gets wrong: the second one names a page the first one relocated, and the
        // number the reader typed is still the number on the screen.
        DocumentEditor twice;
        check(twice.open(source, QString(), &error), "setup: a fresh editor, nothing deleted");
        check(twice.movePages({0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the first two pages go to the end");
        check(twice.movePages({0}, DocumentEditor::MoveTarget::AfterPage, 3, &error),
              "then page 1, which is now near the end, is asked for again -- behind page 4");
        const QString twiceOut = scratch.filePath(QStringLiteral("moved-twice.pdf"));
        check(twice.saveAs(twiceOut, &error), "and the two are written together");
        // Page 1 came back from the end to sit behind page 4, page 2 -- which was
        // moved to the end by the first request and never named by the second --
        // stays there, and everything between them is what it always was.
        QVector<int> wantedTwice;
        wantedTwice << 3 << 4 << 1;
        for (int label = 5; label <= pagesBefore; ++label)
            wantedTwice.append(label);
        wantedTwice << 2;
        const QVector<int> twiceWritten = orderInFile(twiceOut);
        check(twiceWritten == wantedTwice,
              QStringLiteral("the second move found page 1 where the first one left it, and the "
                             "two pages that went to the end stay in their own order: got %1, "
                             "wanted %2")
                  .arg(describeOrder(wantedTwice), describeOrder(twiceWritten)));

        check(hashOfFile(source) == originalHash, "and none of this touched the document");
    }

    // ---- a merge writes in the pages it promised, out of a file it never opened ----
    //
    // This is the operation whose failure cannot be caught by a page count, because a
    // merge is the one thing here that changes the count: a wrongly copied page looks
    // like a right one from across the room. So every case below reads the written file
    // back through the reader and asks each page what its own label says -- which also
    // proves the handoff between the library that copies a page out of one file and the
    // one that displays it, since these fixtures print their number inside the page.
    //
    // The second document is written here by deleting pages out of the first, so the
    // labels the two files share are known without adding a fixture to the folder every
    // other test globs.
    {
        const QString donor = scratch.filePath(QStringLiteral("donor.pdf"));
        QString error;
        check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
              QStringLiteral("setup: a second document, holding pages 5, 6 and 7 of the "
                             "first: %1").arg(error));
        const QString donorHash = hashOfFile(donor);

        int importPages = 0;
        error.clear();
        check(DocumentEditor::inspectImport(donor, &importPages, &error) && importPages == 3,
              QStringLiteral("and looking one over says how many pages it has: %1").arg(error));

        // Refused at the moment the file is chosen, which is the decision made in this
        // slice: no password box here, and the sentence that says which file and why is
        // the whole of what a reader gets to act on.
        const QString locked = scratch.filePath(QStringLiteral("locked-donor.pdf"));
        check(writeEncryptedCopy(source, locked, QStringLiteral("hunter2"), true).isEmpty(),
              "setup: a donor that needs a password to open");
        QString complaint;
        int lockedPages = -1;
        check(!DocumentEditor::inspectImport(locked, &lockedPages, &complaint)
                && lockedPages == 0
                && complaint.contains(QStringLiteral("口令"))
                && complaint.contains(QStringLiteral("locked-donor.pdf")),
              QStringLiteral("a document that needs a password is refused, by name, before "
                             "anything is recorded: %1").arg(complaint));

        // A file merely marked not-to-modify is a different thing: it opens for anyone,
        // so it merges. The round trip below is what says whether the copy survives.
        const QString restricted = scratch.filePath(QStringLiteral("restricted-donor.pdf"));
        check(writeEncryptedCopy(source, restricted, QString(), false).isEmpty(),
              "setup: a donor marked not to be modified");
        complaint.clear();
        int restrictedPages = 0;
        check(DocumentEditor::inspectImport(restricted, &restrictedPages, &complaint)
                && restrictedPages == pagesBefore,
              QStringLiteral("and a file that only forbids editing is looked over fine: %1")
                  .arg(complaint));

        // ---- the whole donor at the back ----
        DocumentEditor editor;
        check(editor.open(source, QString(), &error),
              QStringLiteral("setup: the edited document opens: %1").arg(error));
        check(editor.importedPageCount() == 0, "and nothing has come in yet");
        error.clear();
        check(!editor.mergeFile(donor, {}, DocumentEditor::MoveTarget::Back, -1, &error)
                && !error.isEmpty(),
              "naming no page of the other file is refused, with a sentence");
        check(!editor.isDirty(), "and the refusal recorded nothing");
        error.clear();
        check(!editor.mergeFile(donor, {0, 1, 9}, DocumentEditor::MoveTarget::Back, -1, &error)
                && error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("a page the other file does not have is refused by its own number, "
                             "against that file's own length: %1").arg(error));
        check(!editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::AfterPage,
                                pagesBefore, &error),
              "and so is an anchor past the last page of the document on the screen");
        check(editor.pendingCount() == 0, "and none of the three refusals was recorded");

        error.clear();
        // Timed, because a merge is the first edit whose save opens a second file:
        // the "重放 + 整份写 is milliseconds" claim in docs/PLAN.md was measured with
        // deletions only, and this is the case that could have quietly doubled it.
        QElapsedTimer mergeClock;
        mergeClock.start();
        check(editor.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("the three pages of the donor come in at the end: %1").arg(error));
        check(editor.pageCount() == pagesBefore + 3,
              "and the document a save would write is three pages longer");
        check(editor.importedPageCount() == 3,
              "and the editor can say how many of those the screen has never shown");
        check(editor.undoDescription().contains(QStringLiteral("donor.pdf")),
              QStringLiteral("the undo item names which file: %1").arg(editor.undoDescription()));

        const QString merged = scratch.filePath(QStringLiteral("merged.pdf"));
        error.clear();
        check(editor.saveAs(merged, &error),
              QStringLiteral("and the merge is written into a new file: %1").arg(error));
        note(QStringLiteral("%1 pages plus %2 merged in, a %3 KB document: open + merge + write "
                            "took %4 ms")
                 .arg(pagesBefore)
                 .arg(3)
                 .arg(QFileInfo(source).size() / 1024)
                 .arg(mergeClock.elapsed()));
        QVector<int> wanted;
        for (int label = 1; label <= pagesBefore; ++label)
            wanted.append(label);
        wanted << 5 << 6 << 7;
        check(orderInFile(merged) == wanted,
              QStringLiteral("the file reads back as the forty pages then the donor's three, in "
                             "that order: got %1")
                  .arg(describeOrder(orderInFile(merged))));
        check(pagesAsReadByTheReader(merged) == pagesBefore + 3,
              "and the reader counts every one of them");

        // The donor was read from, and nothing more: the same promise the source has had
        // since the first test in this file, now extended to a second file on the same
        // save -- and the destination is refused for exactly this reason.
        check(hashOfFile(donor) == donorHash, "the file that was merged in is unchanged");
        check(hashOfFile(source) == originalHash, "and so is the one that was opened");
        error.clear();
        check(!editor.saveAs(donor, &error) && error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("saving over the file that was merged in is refused: %1").arg(error));
        check(hashOfFile(donor) == donorHash, "and the refusal left it alone");

        // Undo is the only way an imported page can be taken away, so it is checked the
        // same way the merge was: by what the file comes back holding.
        check(editor.undo(), "the merge is taken back");
        check(editor.pageCount() == pagesBefore && editor.importedPageCount() == 0,
              "and the document is the opened file again, page for page");
        const QString undoneFile = scratch.filePath(QStringLiteral("merged-undone.pdf"));
        check(editor.saveAs(undoneFile, &error), "and the shorter list writes");
        QVector<int> wantedUndone;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedUndone.append(label);
        check(orderInFile(undoneFile) == wantedUndone,
              QStringLiteral("nothing is left of the merge in the file: got %1")
                  .arg(describeOrder(orderInFile(undoneFile))));

        check(editor.redo(), "redo puts the merge back on the list");
        check(editor.importedPageCount() == 3, "and the three pages with it");
        const QString redoneFile = scratch.filePath(QStringLiteral("merged-redone.pdf"));
        check(editor.saveAs(redoneFile, &error), "and it writes");
        check(orderInFile(redoneFile) == wanted,
              "the redone document is the one the merge wrote the first time");

        // ---- a merged page in front, and a number that cannot reach it ----
        //
        // The consequence of numbering by the screen, checked where it would do the most
        // damage: a reader who deletes "page 1" after a merge means the first page they
        // can see, which is the first page of the file they opened -- not the imported
        // page that a save now happens to write in front of it. Had the number been taken
        // as a position in the saved document, the page that went out here would be one
        // nobody named, and the file would still have the right count.
        DocumentEditor front;
        check(front.open(source, QString(), &error), "setup: the copy opens again");
        check(front.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Front, -1, &error),
              "the donor's three pages land at the very front");
        check(front.deletePages({0}, &error),
              "then page 1 is deleted -- which is the opened file's first page, not the "
              "imported one now sitting in front of it");
        check(front.pageCount() == pagesBefore + 2, "and the count says which of the two went");
        const QString frontOut = scratch.filePath(QStringLiteral("merged-front.pdf"));
        check(front.saveAs(frontOut, &error),
            QStringLiteral("and it is written: %1").arg(error));
        QVector<int> wantedFront;
        wantedFront << 5 << 6 << 7;
        for (int label = 2; label <= pagesBefore; ++label)
            wantedFront.append(label);
        check(orderInFile(frontOut) == wantedFront,
              QStringLiteral("the file holds the donor's three pages, then the opened document "
                             "from its second page: got %1")
                  .arg(describeOrder(orderInFile(frontOut))));
        check(front.deletePages({0}, &error) == false,
              "and asking again for the page that has gone says so rather than taking the "
              "imported one out instead");

        // ---- one page, twice ----
        //
        // The case a merge is free to make and no other operation is: the same page of
        // the same file twice in one document. It is not a bug to allow -- "把第 3 页再来
        // 一遍" is a thing a reader can mean -- so what is pinned is that the two copies
        // are two pages, and that undoing one leaves the other.
        DocumentEditor twice;
        check(twice.open(source, QString(), &error), "setup: the copy opens again");
        check(twice.mergeFile(donor, {1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "one page of the donor goes in");
        check(twice.mergeFile(donor, {1}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
              "and the same page again, behind the first page of the opened document");
        check(twice.pendingCount() == 2, "two operations, one per merge");
        check(twice.importedPageCount() == 2, "two pages, the same page twice");
        const QString twiceOut = scratch.filePath(QStringLiteral("merged-twice.pdf"));
        check(twice.saveAs(twiceOut, &error),
            QStringLiteral("and the two are written together: %1").arg(error));
        QVector<int> wantedTwice;
        wantedTwice << 1 << 6;
        for (int label = 2; label <= pagesBefore; ++label)
            wantedTwice.append(label);
        wantedTwice << 6;
        check(orderInFile(twiceOut) == wantedTwice,
              QStringLiteral("page 6 of the donor appears twice, once behind page 1 and once at "
                             "the end, and they are separate pages: got %1")
                  .arg(describeOrder(orderInFile(twiceOut))));
        check(twice.undo(), "taking back the second merge");
        check(twice.importedPageCount() == 1, "leaves the first one where it was");
        const QString oneOut = scratch.filePath(QStringLiteral("merged-twice-one.pdf"));
        check(twice.saveAs(oneOut, &error), "and the file says so too");
        QVector<int> wantedOne;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedOne.append(label);
        wantedOne << 6;
        check(orderInFile(oneOut) == wantedOne,
              QStringLiteral("the surviving copy is the one at the end: got %1")
                  .arg(describeOrder(orderInFile(oneOut))));

        // ---- what the annotations of a merged page have to become ----
        //
        // The repair has been in the replay since the merge landed, and it had never run:
        // there is no annotated page anywhere under tests/fixtures, and a second one would be
        // globbed by every other test that walks that folder. Merging the same annotated page
        // *twice* is the smallest case that can show the difference at all -- with one copy,
        // "the copy shares the donor's annotations" and "the copy got its own" read the same
        // from the outside. qpdf's own promise is the first of those two being impossible.
        {
            const QString marked = scratch.filePath(QStringLiteral("annotated-donor.pdf"));
            error.clear();
            check(!writeAnnotatedPageCopy(donor, marked, &error).isEmpty(),
                  QStringLiteral("setup: one page of the donor gets a note and a text field: %1")
                      .arg(error));
            const AnnotationReport built = readAnnotations(marked);
            check(built.countPerPage.value(0) == 2 && built.fields == 1
                      && built.notesNamingAnotherPage == 0 && built.objectsSharedByTwoPages == 0
                      && built.fieldsNamingNoPage == 0,
                  QStringLiteral("setup: that page carries both, each naming itself, and the "
                                 "field is in that file's own form list: %1 annotations, %2 "
                                 "fields")
                      .arg(built.countPerPage.value(0))
                      .arg(built.fields));
            const QString markedHash = hashOfFile(marked);

            DocumentEditor inked;
            check(inked.open(source, QString(), &error),
                  QStringLiteral("setup: the document to merge into: %1").arg(error));
            check(inked.mergeFile(marked, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("the annotated page comes in once: %1").arg(error));
            check(inked.mergeFile(marked, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("and the same page again, as a second copy: %1").arg(error));
            const QString inkedOut = scratch.filePath(QStringLiteral("merged-annotated.pdf"));
            error.clear();
            check(inked.saveAs(inkedOut, &error),
                  QStringLiteral("and both copies are written: %1").arg(error));

            const AnnotationReport written = readAnnotations(inkedOut);
            check(written.countPerPage.value(pagesBefore) == 2
                      && written.countPerPage.value(pagesBefore + 1) == 2,
                  QStringLiteral("the two copies kept the annotations the page came with: %1 "
                                 "and %2 at the end of the file")
                      .arg(written.countPerPage.value(pagesBefore))
                      .arg(written.countPerPage.value(pagesBefore + 1)));
            check(written.notesNamingAnotherPage == 0,
                  QStringLiteral("every annotation names the page that carries it, which after "
                                 "two copies of one page cannot be true of a shared object: %1 "
                                 "do not")
                      .arg(written.notesNamingAnotherPage));
            check(written.objectsSharedByTwoPages == 0,
                  QStringLiteral("and no annotation object is pointed at by two pages: %1 are")
                      .arg(written.objectsSharedByTwoPages));
            check(written.fields == 2 && written.fieldsNamingNoPage == 0,
                  QStringLiteral("the form answers survive as fields of *this* document: %1 "
                                 "fields, %2 naming no page of it")
                      .arg(written.fields)
                      .arg(written.fieldsNamingNoPage));
            check(hashOfFile(marked) == markedHash,
                  "and the file the annotations came out of was only ever read");
        }

        // ---- when both documents already carry a field of the same name ----
        //
        // The layer above that one, and the half of qpdf's sentence its repair does not
        // claim to cover: "resolve conflicts from common resource and field names". A
        // field name is an address -- two fields called the same thing in one document is
        // what a form-reading program cannot answer -- so the question here is not whether
        // the merge throws but what the surviving form list says.
        {
            const QString marked = scratch.filePath(QStringLiteral("annotated-donor.pdf"));
            const QString both = scratch.filePath(QStringLiteral("target-has-same-field.pdf"));
            error.clear();
            check(!writeAnnotatedPageCopy(donor, marked, &error).isEmpty(),
                  QStringLiteral("setup: the donor's page carries a field: %1").arg(error));
            check(!writeFieldCopy(source, both, QStringLiteral("donorAnswer"),
                                  QStringLiteral("typed in the target"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: and so does the target's own first page, under the "
                                 "same name: %1")
                      .arg(error));
            const AnnotationReport built = readAnnotations(both);
            check(built.fields == 1 && built.fieldNames.value(0) == QStringLiteral("donorAnswer"),
                  QStringLiteral("setup: the target has one field, named %1")
                      .arg(built.fieldNames.value(0)));

            DocumentEditor clash;
            check(clash.open(both, QString(), &error),
                  QStringLiteral("setup: the document with a field of its own opens: %1")
                      .arg(error));
            check(clash.mergeFile(marked, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("the donor's fielded page comes in beside it: %1").arg(error));
            const QString clashOut = scratch.filePath(QStringLiteral("merged-same-field.pdf"));
            error.clear();
            check(clash.saveAs(clashOut, &error),
                  QStringLiteral("the document with both fields is written: %1").arg(error));

            const AnnotationReport merged = readAnnotations(clashOut);
            // What qpdf's repair does with the name is its own business (this build renames the
            // incoming field rather than the one already in the tree), so the assertions are
            // about the two properties a reader can act on: the field that was already here is
            // untouched, and the one that came in kept its answer rather than its label.
            check(merged.fields == 2,
                  QStringLiteral("both fields survive the merge: %1 in the form list")
                      .arg(merged.fields));
            check(merged.fieldNames.value(0) == QStringLiteral("donorAnswer")
                      && merged.fieldValues.value(0) == QStringLiteral("typed in the target"),
                  QStringLiteral("the document's own field is still named %1 and still says "
                                 "\"%2\"")
                      .arg(merged.fieldNames.value(0), merged.fieldValues.value(0)));
            check(merged.fieldNames.size() == 2 && merged.fieldNames.value(0) != merged.fieldNames.value(1),
                  QStringLiteral("and the two names are not the same, which is the collision "
                                 "this case is about: [%1] vs [%2]")
                      .arg(merged.fieldNames.value(0), merged.fieldNames.value(1)));
            check(merged.fieldValues.value(1) == QStringLiteral("filled in the donor"),
                  QStringLiteral("the answer that came in with the page is the one stored now "
                                 "(\"%1\"), not the target's")
                      .arg(merged.fieldValues.value(1)));
            check(merged.fieldsNamingNoPage == 0 && merged.objectsSharedByTwoPages == 0
                      && merged.notesNamingAnotherPage == 0,
                  QStringLiteral("and the same three ownership facts hold as without the "
                                 "collision: %1 fields name no page, %2 objects shared, %3 "
                                 "notes naming another page")
                      .arg(merged.fieldsNamingNoPage)
                      .arg(merged.objectsSharedByTwoPages)
                      .arg(merged.notesNamingAnotherPage));
            check(merged.countPerPage.value(merged.countPerPage.size() - 1) == 2,
                  QStringLiteral("the page that came in still carries its two annotations: %1")
                      .arg(merged.countPerPage.value(merged.countPerPage.size() - 1)));
        }

        // ---- annotations that point at other annotations ----
        //
        // The last shape §36 left unasked, and the one qpdf's comment is silent about: a popup
        // lives outside the page's /Annots array (only its owner's /Popup names it) and a reply
        // names another annotation with /IRT. Both are references a page copy can leave pointing
        // back at the document the page came from. Two copies again, because that is the only
        // way "private" and "shared" read differently from the file.
        {
            const QString linked = scratch.filePath(QStringLiteral("linked-donor.pdf"));
            error.clear();
            check(!writeLinkedAnnotationsCopy(donor, linked, &error).isEmpty(),
                  QStringLiteral("setup: a note with a popup and a reply: %1").arg(error));
            const LinkReport built = readLinkedAnnotations(linked);
            check(built.notes == 1 && built.popups == 1 && built.replies == 1
                      && built.popupsSharedByTwoPages == 0 && built.popupsNamingWrongOwner == 0
                      && built.repliesNamingForeignTarget == 0,
                  QStringLiteral("setup: that page reads as one note, one popup naming it, one "
                                 "reply naming it: %1 popups, %2 replies")
                      .arg(built.popups)
                      .arg(built.replies));

            DocumentEditor chain;
            check(chain.open(source, QString(), &error),
                  QStringLiteral("setup: the document to merge into: %1").arg(error));
            check(chain.mergeFile(linked, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("the linked page comes in once: %1").arg(error));
            check(chain.mergeFile(linked, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("and again, as a second copy: %1").arg(error));
            const QString chainOut = scratch.filePath(QStringLiteral("merged-linked.pdf"));
            error.clear();
            check(chain.saveAs(chainOut, &error),
                  QStringLiteral("and both copies are written: %1").arg(error));

            const LinkReport joined = readLinkedAnnotations(chainOut);
            note(QStringLiteral("sec78 merged links notes=%1 popups=%2 replies=%3 shared=%4 "
                                "wrongOwner=%5 strayOwner=%6 dangling=%7 reply texts=%8")
                     .arg(joined.notes)
                     .arg(joined.popups)
                     .arg(joined.replies)
                     .arg(joined.popupsSharedByTwoPages)
                     .arg(joined.popupsNamingWrongOwner)
                     .arg(joined.popupsNamingStrayOwner)
                     .arg(joined.repliesOnNoPage)
                     .arg(joined.texts.count(QStringLiteral("a reply to the note"))));
            // What the repair does guarantee, measured: each copy got its own annotation
            // objects, and each of those names the page that carries it.
            check(joined.notes == 4 && joined.popups == 2 && joined.replies == 0,
                  QStringLiteral("both copies came out with their own note, their own popup and "
                                 "no thread that names something this file does not show: %1 notes, "
                                 "%2 popups, %3 replies")
                      .arg(joined.notes)
                      .arg(joined.popups)
                      .arg(joined.replies));
            // §78: what §47 pinned as the gap, now asserted the other way. Each note's popup is
            // its own and answers to it, and neither reply still threads to the donor's note.
            // The counts alone would also be satisfied by *deleting* the popups and the replies
            // (§50's lesson about a consistency check that absence satisfies), so the popup's own
            // geometry and the reply text are read in the same breath below.
            check(joined.popupsSharedByTwoPages == 0 && joined.popupsNamingWrongOwner == 0
                      && joined.popupsNamingStrayOwner == 0 && joined.popupRects.size() == 2
                      && joined.popupRects.at(0) == joined.popupRects.at(1),
                  QStringLiteral("each copy's popup is a separate object that names the note "
                                 "carrying it, and both carry the geometry the donor's had: %1 "
                                 "shared, %2 wrong owner, %3 stray owner, rects %4")
                      .arg(joined.popupsSharedByTwoPages)
                      .arg(joined.popupsNamingWrongOwner)
                      .arg(joined.popupsNamingStrayOwner)
                      .arg(joined.popupRects.join(QStringLiteral(" | "))));
            // The dropped half, stated as what it is: the *link* goes, the words stay. §47 left
            // three exits open and this is the middle one -- take out what should not have
            // travelled, and never edit content to make a reference look tidy.
            check(joined.repliesOnNoPage == 0 && joined.repliesNamingForeignTarget == 0
                      && joined.texts.count(QStringLiteral("a reply to the note")) == 2,
                  QStringLiteral("no reply threads to an annotation no page shows, while both "
                                 "reply texts are still in the file: %1 dangling, %2 foreign, %3 "
                                 "copies of the reply text")
                      .arg(joined.repliesOnNoPage)
                      .arg(joined.repliesNamingForeignTarget)
                      .arg(joined.texts.count(QStringLiteral("a reply to the note"))));

            // A save with nothing broken must come out the same shape it went in: one note, one
            // popup naming it, one reply naming the note, and the popup still carrying its own
            // geometry.
            //
            // What this guard cannot see, stated rather than glossed: object ids are renumbered by
            // every write, so "the same popup object came back" is not a fact the saved file can
            // state, and a pass that rebuilt an already-correct popup would leave exactly this
            // reading. The copy is therefore only ever *reachable* in the file through the shape
            // above, and what this check really holds is that the repair neither loses a popup,
            // loses a thread, nor moves a box -- the churn half stays inside the code's own
            // condition (/Parent already naming the owner means nothing to do).
            DocumentEditor retouched;
            check(retouched.open(linked, QString(), &error),
                  QStringLiteral("setup: the linked document opened again: %1").arg(error));
            check(retouched.deletePages({2}, &error),
                  QStringLiteral("and a page that carries none of this is dropped: %1").arg(error));
            const QString retouchedOut = scratch.filePath(QStringLiteral("linked-untouched.pdf"));
            error.clear();
            check(retouched.saveAs(retouchedOut, &error),
                  QStringLiteral("and it is written: %1").arg(error));
            const LinkReport before = readLinkedAnnotations(linked);
            const LinkReport intact = readLinkedAnnotations(retouchedOut);
            check(intact.notes == before.notes && intact.popups == before.popups
                      && intact.replies == before.replies && intact.popupRects == before.popupRects
                      && intact.popupsNamingWrongOwner == 0 && intact.popupsNamingStrayOwner == 0
                      && intact.repliesNamingForeignTarget == 0 && intact.repliesOnNoPage == 0,
                  QStringLiteral("a save that had nothing to repair loses nothing and moves "
                                 "nothing: was %1 notes / %2 popups / %3 replies at %4, now %5 "
                                 "notes / %6 popups / %7 replies at %8")
                      .arg(before.notes)
                      .arg(before.popups)
                      .arg(before.replies)
                      .arg(before.popupRects.join(QStringLiteral(" | ")))
                      .arg(intact.notes)
                      .arg(intact.popups)
                      .arg(intact.replies)
                      .arg(intact.popupRects.join(QStringLiteral(" | "))));
        }

        // ---- a thread that crosses pages of the same document is not a defect ----
        //
        // §78's boundary, and the case the merged sample above cannot express: with the note and
        // the reply on one page, "legal, inside this file" and "left behind by the copy" are the
        // same reading. Two saves out of one cross-page thread, differing only in which page goes.
        {
            const QString threaded = scratch.filePath(QStringLiteral("threaded-donor.pdf"));
            error.clear();
            check(!writeCrossPageThreadCopy(donor, threaded, &error).isEmpty(),
                  QStringLiteral("setup: a note on one page and a reply to it on the next: %1")
                      .arg(error));
            const LinkReport built = readLinkedAnnotations(threaded);
            check(built.notes == 1 && built.popups == 1 && built.replies == 1
                      && built.repliesOnAnotherPage == 1 && built.repliesOnNoPage == 0
                      && built.popupsNamingStrayOwner == 0,
                  QStringLiteral("setup: the thread crosses pages of the file itself, which is "
                                 "legal: %1 replies across pages, %2 with nothing under them")
                      .arg(built.repliesOnAnotherPage)
                      .arg(built.repliesOnNoPage));

            // Control: the page neither annotation is on goes, the thread stays where it was.
            DocumentEditor keeper;
            check(keeper.open(threaded, QString(), &error),
                  QStringLiteral("setup: the threaded document opened: %1").arg(error));
            check(keeper.deletePages({2}, &error),
                  QStringLiteral("a third page, carrying neither note nor reply, is dropped: %1")
                      .arg(error));
            const QString keptOut = scratch.filePath(QStringLiteral("thread-kept.pdf"));
            error.clear();
            check(keeper.saveAs(keptOut, &error),
                  QStringLiteral("and it is written: %1").arg(error));
            const LinkReport kept = readLinkedAnnotations(keptOut);
            check(kept.replies == 1 && kept.repliesOnAnotherPage == 1
                      && kept.repliesOnNoPage == 0 && kept.popupsNamingWrongOwner == 0,
                  QStringLiteral("the repair leaves a legal cross-page thread alone: %1 replies, "
                                 "%2 across pages, %3 dangling")
                      .arg(kept.replies)
                      .arg(kept.repliesOnAnotherPage)
                      .arg(kept.repliesOnNoPage));

            // The fix case: the note's page goes, so the reply is left naming an annotation this
            // file cannot show. Its /IRT comes off and the reply itself stays -- with its text.
            DocumentEditor cutter;
            check(cutter.open(threaded, QString(), &error),
                  QStringLiteral("setup: the threaded document opened again: %1").arg(error));
            check(cutter.deletePages({0}, &error),
                  QStringLiteral("and the page holding the note is dropped: %1").arg(error));
            const QString cutOut = scratch.filePath(QStringLiteral("thread-cut.pdf"));
            error.clear();
            check(cutter.saveAs(cutOut, &error),
                  QStringLiteral("and it is written: %1").arg(error));
            const LinkReport cut = readLinkedAnnotations(cutOut);
            note(QStringLiteral("sec78 cross-page kept replies=%1 onAnotherPage=%2 popups=%3 | "
                                "cut replies=%4 onNoPage=%5 notes=%6 text kept=%7")
                     .arg(kept.replies)
                     .arg(kept.repliesOnAnotherPage)
                     .arg(kept.popups)
                     .arg(cut.replies)
                     .arg(cut.repliesOnNoPage)
                     .arg(cut.notes)
                     .arg(cut.texts.contains(QStringLiteral("a reply across the pages")) ? 1 : 0));
            check(cut.replies == 0 && cut.repliesOnNoPage == 0
                      && cut.texts.contains(QStringLiteral("a reply across the pages")),
                  QStringLiteral("the dead thread comes off the reply while the reply stays: %1 "
                                 "replies, %2 dangling, text still present: %3")
                      .arg(cut.replies)
                      .arg(cut.repliesOnNoPage)
                      .arg(cut.texts.contains(QStringLiteral("a reply across the pages")) ? 1 : 0));
        }

        // ---- the same collision, one level up: names inside a hierarchy ----
        //
        // §46's layer above it, and the reason "no genuine form PDF exists on this machine" was
        // only half a blocker: what a real file adds is generators and XFA, but the *question* --
        // what does a field's name mean when it is assembled out of several dictionaries -- can be
        // asked of a tree built here. Both documents carry a field a reader would address as
        // `form.answer`; only the leaf is on the page, and the parent is reachable from nothing but
        // the form list.
        {
            const QString nestedDonor = scratch.filePath(QStringLiteral("nested-donor.pdf"));
            const QString nestedTarget =
                scratch.filePath(QStringLiteral("target-has-same-nested-field.pdf"));
            error.clear();
            check(!writeNestedFieldCopy(donor, nestedDonor, QStringLiteral("form"),
                                        QStringLiteral("answer"),
                                        QStringLiteral("filled in the donor"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: the donor's page carries a field at form.answer: %1")
                      .arg(error));
            check(!writeNestedFieldCopy(source, nestedTarget, QStringLiteral("form"),
                                        QStringLiteral("answer"),
                                        QStringLiteral("typed in the target"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: and so does the target's own first page: %1").arg(error));

            const FieldTreeReport built = readFieldTree(nestedTarget);
            check(built.terminals == 1 && built.addresses.value(0) == QStringLiteral("form.answer")
                      && built.parents == 1 && built.unregisteredWidgets == 0
                      && built.duplicateAddresses == 0 && built.brokenBackLinks == 0,
                  QStringLiteral("setup: read as a tree that is one address under one parent, "
                                 "with the page's widget reachable from it: %1 addresses, %2 "
                                 "parents, %3 unregistered widgets, %4 broken back-links")
                      .arg(built.addresses.size())
                      .arg(built.parents)
                      .arg(built.unregisteredWidgets)
                      .arg(built.brokenBackLinks));
            const AnnotationReport builtFlat = readAnnotations(nestedTarget);
            check(builtFlat.fields == 1,
                  QStringLiteral("setup: and the form list itself holds only the parent, which "
                                 "is why reading /Fields is not reading the form: %1 entries")
                      .arg(builtFlat.fields));

            DocumentEditor joined;
            check(joined.open(nestedTarget, QString(), &error),
                  QStringLiteral("setup: the document with the nested field opens: %1")
                      .arg(error));
            check(joined.mergeFile(nestedDonor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("the donor's page comes in beside it: %1").arg(error));
            const QString joinedOut = scratch.filePath(QStringLiteral("merged-nested-field.pdf"));
            error.clear();
            const bool wrote = joined.saveAs(joinedOut, &error);
            std::cout << "MEASURE nested save: " << (wrote ? "ok" : "refused") << " "
                      << error.toStdString() << '\n';

            const FieldTreeReport after = readFieldTree(joinedOut);
            std::cout << "MEASURE nested: terminals=" << after.terminals << " parents="
                      << after.parents << " addresses=["
                      << after.addresses.join(QStringLiteral("|")).toStdString() << "] values=["
                      << after.values.join(QStringLiteral("|")).toStdString()
                      << "] unregisteredWidgets=" << after.unregisteredWidgets
                      << " duplicateAddresses=" << after.duplicateAddresses
                      << " brokenBackLinks=" << after.brokenBackLinks
                      << " parentChildMismatch=" << after.parentChildMismatch << '\n';
            const AnnotationReport afterFlat = readAnnotations(joinedOut);
            std::cout << "MEASURE nested /Fields: entries=" << afterFlat.fields << " names=["
                      << afterFlat.fieldNames.join(QStringLiteral("|")).toStdString() << "] values=["
                      << afterFlat.fieldValues.join(QStringLiteral("|")).toStdString() << "]\n";
            check(wrote, QStringLiteral("the merge of two hierarchical fields is written: %1")
                             .arg(error));
            // The same two properties §46 asked of the flat case -- the field that was already
            // here is untouched, the answer that came in survived -- and two that only exist once
            // names are assembled: the whole subtree came along rather than the leaf being hung
            // off somebody else's parent, and the tree reads the same in both directions.
            check(after.terminals == 2 && after.parents == 2,
                  QStringLiteral("the form now has two answers under two parents, i.e. the "
                                 "incoming subtree arrived whole rather than the leaf alone: %1 "
                                 "terminals, %2 parents")
                      .arg(after.terminals)
                      .arg(after.parents));
            check(after.addresses.value(0) == QStringLiteral("form.answer")
                      && after.values.value(0) == QStringLiteral("typed in the target"),
                  QStringLiteral("the document's own field is still at %1 and still says \"%2\"")
                      .arg(after.addresses.value(0), after.values.value(0)));
            check(after.addresses.size() == 2 && after.addresses.value(1) != after.addresses.value(0)
                      && after.duplicateAddresses == 0,
                  QStringLiteral("and the two addresses are not the same, which is what the "
                                 "collision is about: [%1] vs [%2]")
                      .arg(after.addresses.value(0), after.addresses.value(1)));
            check(after.values.value(1) == QStringLiteral("filled in the donor"),
                  QStringLiteral("the answer that came in with the page is stored under the new "
                                 "address (\"%1\")").arg(after.values.value(1)));
            check(after.unregisteredWidgets == 0,
                  QStringLiteral("every widget on a page is reachable from this document's form "
                                 "list, so none of them is a box nothing can read back: %1 are not")
                      .arg(after.unregisteredWidgets));
            check(after.parentChildMismatch == 0 && after.brokenBackLinks == 0,
                  QStringLiteral("and walking down from /Fields agrees with walking up from each "
                                 "widget's /Parent: %1 mismatched links, %2 naming an object this "
                                 "document does not register")
                      .arg(after.parentChildMismatch)
                      .arg(after.brokenBackLinks));
            // What qpdf's repair does with the *string* is its own business, exactly as in §46 (this
            // build moves the parent segment, so the subtree keeps its internal names). Pinning the
            // literal would write the dependency's version into the test; the sentence below is said
            // out loud rather than asserted so that a change of strategy is visible in the MEASURE
            // line instead of turning into a failure that has to be read back to a qpdf changelog.
            std::cout << "MEASURE nested rename: the incoming address differs from the target's at "
                         "segment " << (after.addresses.value(1).section(QLatin1Char('.'), 0, 0)
                                        == QStringLiteral("form")
                                            ? "after the first (the leaf moved)"
                                            : "one (the parent moved)")
                      << '\n';
        }

        // ---- and what a page going OUT leaves in the form list ----
        //
        // The mirror of the four slices above. §36/§46/§47/§50 all copy pages *in*; nothing in
        // this file ever took a page out while the document carried an /AcroForm, because the
        // fixtures have none. Two reader commands do exactly that: 删除页面 drops the page a field
        // lived on, and 拆分 writes pieces of which only one holds it. A field is registered in the
        // root's /AcroForm and only *points* at its page through /P, so a page leaving can leave an
        // entry naming a page this file does not have -- the same reference-left-behind family §47
        // found, reached by our own edit rather than by qpdf's copy.
        {
            const QString homeFile = scratch.filePath(QStringLiteral("field-on-page-one.pdf"));
            error.clear();
            check(!writeFieldCopy(source, homeFile, QStringLiteral("onOne"),
                                  QStringLiteral("typed on page 1"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: a document whose only field lives on its first page: %1")
                      .arg(error));
            const FieldTreeReport home = readFieldTree(homeFile);
            check(home.terminals == 1 && home.addresses.value(0) == QStringLiteral("onOne")
                      && home.unregisteredWidgets == 0 && home.brokenBackLinks == 0,
                  QStringLiteral("setup: one field, on the page, reachable from the form list: %1")
                      .arg(home.terminals));

            DocumentEditor out;
            check(out.open(homeFile, QString(), &error),
                  QStringLiteral("setup: it opens: %1").arg(error));
            check(out.deletePages({0}, &error),
                  QStringLiteral("the page the field lives on is deleted: %1").arg(error));
            const QString outPath = scratch.filePath(QStringLiteral("field-page-deleted.pdf"));
            error.clear();
            check(out.saveAs(outPath, &error),
                  QStringLiteral("and the deletion is written: %1").arg(error));
            const FieldTreeReport orphan = readFieldTree(outPath);
            const AnnotationReport orphanFlat = readAnnotations(outPath);
            int onPages = 0;
            for (int index = 0; index < orphanFlat.countPerPage.size(); ++index)
                onPages += orphanFlat.countPerPage.at(index);
            std::cout << "MEASURE out delete: pages=" << orphanFlat.countPerPage.size()
                      << " annotationsOnPages=" << onPages << " terminals=" << orphan.terminals
                      << " addresses=["
                      << orphan.addresses.join(QStringLiteral("|")).toStdString() << "] values=["
                      << orphan.values.join(QStringLiteral("|")).toStdString()
                      << "] /P naming no page=" << orphanFlat.fieldsNamingNoPage
                      << " unregisteredWidgets=" << orphan.unregisteredWidgets << '\n';

            // The same field, this time reached through 拆分: only one of the pieces holds the
            // page, and the question is what the other pieces' /AcroForm says.
            DocumentEditor cut;
            check(cut.open(homeFile, QString(), &error),
                  QStringLiteral("setup: the same document opens again for a split: %1")
                      .arg(error));
            check(cut.splitInto(20, &error), QStringLiteral("a split into two pieces is recorded: %1").arg(error));
            const QString cutPath = scratch.filePath(QStringLiteral("field-cut.pdf"));
            QStringList written;
            error.clear();
            check(cut.saveAs(cutPath, &error, &written) && written.size() == 2,
                  QStringLiteral("and the two pieces are written: %1").arg(error));
            QList<FieldTreeReport> pieceTrees;
            QVector<int> pieceAnnotations;
            QVector<int> pieceDangling;
            for (int index = 0; index < written.size(); ++index) {
                const FieldTreeReport pieceTree = readFieldTree(written.at(index));
                const AnnotationReport pieceFlat = readAnnotations(written.at(index));
                int inked = 0;
                for (int page = 0; page < pieceFlat.countPerPage.size(); ++page)
                    inked += pieceFlat.countPerPage.at(page);
                pieceTrees << pieceTree;
                pieceAnnotations << inked;
                pieceDangling << pieceFlat.fieldsNamingNoPage;
                std::cout << "MEASURE out piece " << index + 1 << ": pages="
                          << pieceFlat.countPerPage.size() << " annotationsOnPages=" << inked
                          << " terminals=" << pieceTree.terminals << " addresses=["
                          << pieceTree.addresses.join(QStringLiteral("|")).toStdString()
                          << "] values=["
                          << pieceTree.values.join(QStringLiteral("|")).toStdString()
                          << "] /P naming no page=" << pieceFlat.fieldsNamingNoPage << '\n';
            }

            // A control on the same route: the page that stays must keep its field, or the rule
            // "a page went out" would be indistinguishable from "the form list is always dropped".
            DocumentEditor kept;
            check(kept.open(homeFile, QString(), &error) && kept.deletePages({5}, &error),
                  QStringLiteral("setup: the same document, this time losing a page it did not "
                                 "need: %1")
                      .arg(error));
            const QString keptPath = scratch.filePath(QStringLiteral("field-page-kept.pdf"));
            error.clear();
            check(kept.saveAs(keptPath, &error),
                  QStringLiteral("and that deletion is written: %1").arg(error));
            const FieldTreeReport stillThere = readFieldTree(keptPath);
            const AnnotationReport stillFlat = readAnnotations(keptPath);
            std::cout << "MEASURE out control: pages=" << stillFlat.countPerPage.size()
                      << " terminals=" << stillThere.terminals << " addresses=["
                      << stillThere.addresses.join(QStringLiteral("|")).toStdString()
                      << "] values=["
                      << stillThere.values.join(QStringLiteral("|")).toStdString()
                      << "] /P naming no page=" << stillFlat.fieldsNamingNoPage << '\n';

            // And the same edit on a name that is assembled out of two dictionaries: does the
            // parent stay registered over a child whose page is gone?
            const QString nestedHome = scratch.filePath(QStringLiteral("nested-on-page-one.pdf"));
            check(!writeNestedFieldCopy(source, nestedHome, QStringLiteral("form"),
                                        QStringLiteral("answer"),
                                        QStringLiteral("typed on page 1"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: a hierarchical field on the first page: %1").arg(error));
            DocumentEditor lost;
            check(lost.open(nestedHome, QString(), &error) && lost.deletePages({0}, &error),
                  QStringLiteral("setup: its page is deleted: %1").arg(error));
            const QString lostPath = scratch.filePath(QStringLiteral("nested-page-deleted.pdf"));
            error.clear();
            check(lost.saveAs(lostPath, &error),
                  QStringLiteral("and it is written: %1").arg(error));
            const FieldTreeReport lostTree = readFieldTree(lostPath);
            const AnnotationReport lostFlat = readAnnotations(lostPath);
            std::cout << "MEASURE out nested: pages=" << lostFlat.countPerPage.size()
                      << " parents=" << lostTree.parents << " terminals=" << lostTree.terminals
                      << " addresses=["
                      << lostTree.addresses.join(QStringLiteral("|")).toStdString() << "] values=["
                      << lostTree.values.join(QStringLiteral("|")).toStdString()
                      << "] /P naming no page=" << lostFlat.fieldsNamingNoPage
                      << " brokenBackLinks=" << lostTree.brokenBackLinks << '\n';

            // What the pages say is not in question -- the deletion took the page, and a split
            // gives each piece only its own. Pinned here is what the *form list* says about it,
            // which no slice above ever asked, and which the control above rules out being an
            // artefact of how this test reads the file.
            check(orphanFlat.countPerPage.size() == pagesBefore - 1 && onPages == 0,
                  QStringLiteral("the deleted document really has one page fewer and no widget on "
                                 "any of them: %1 pages, %2 annotations left on them")
                      .arg(orphanFlat.countPerPage.size())
                      .arg(onPages));
            check(stillThere.terminals == 1 && stillThere.values.value(0) == QStringLiteral("typed on page 1")
                      && stillFlat.fieldsNamingNoPage == 0,
                  QStringLiteral("control: deleting a different page leaves the field where it is, "
                                 "so the reading below is about the page going out and not about "
                                 "this reader: %1 fields name no page")
                      .arg(stillFlat.fieldsNamingNoPage));
            check(orphan.terminals == 1 && orphanFlat.fieldsNamingNoPage == 1
                      && orphan.values.value(0).isEmpty(),
                  QStringLiteral("§77 closed half of §51's row: after its page is deleted the file "
                                 "still registers the field (%1) and its /P still names a page this "
                                 "document lacks (%2), but the answer is no longer carried -- the "
                                 "value reads \"%3\"")
                      .arg(orphan.terminals)
                      .arg(orphanFlat.fieldsNamingNoPage)
                      .arg(orphan.values.value(0)));
            check(pieceTrees.size() == written.size() && pieceTrees.at(0).terminals == 1
                      && pieceTrees.at(1).terminals == 1 && pieceAnnotations.at(1) == 0
                      && pieceDangling.at(1) == 1
                      && pieceTrees.at(0).values.value(0) == QStringLiteral("typed on page 1")
                      && pieceTrees.at(1).values.value(0).isEmpty(),
                  QStringLiteral("and the row §51 called the serious one is closed: the piece that "
                                 "got the page keeps the answer (\"%1\") while the piece that did not "
                                 "carries nothing (\"%2\") -- it still registers the field, which is "
                                 "the half that stays")
                      .arg(pieceTrees.at(0).values.value(0))
                      .arg(pieceTrees.at(1).values.value(0)));
            check(lostTree.parents == 1 && lostTree.terminals == 1
                      && lostTree.addresses.value(0) == QStringLiteral("form.answer")
                      && lostTree.values.value(0).isEmpty()
                      && lostFlat.fieldsNamingNoPage == 1,
                  QStringLiteral("a name assembled out of two dictionaries is no better off, and no "
                                 "worse either: the parent stays registered over a child whose page "
                                 "is gone (%1 parents, %2 terminals, %3 naming no page) while the "
                                 "answer under that address reads \"%4\"")
                      .arg(lostTree.parents)
                      .arg(lostTree.terminals)
                      .arg(lostFlat.fieldsNamingNoPage)
                      .arg(lostTree.values.value(0)));
        }

        // ---- one field drawn on two pages, and a split that pulls them apart ----
        //
        // The shape §51 named as its next rung, and the reason it is worth building: it is the
        // case where "drop the field whose page went out" would be *wrong*. A field with two
        // widgets is needed by both halves of a split, so the repair §51 leaves open has to work
        // on widgets, not on the registered name.
        {
            const QString spread = scratch.filePath(QStringLiteral("spread-field.pdf"));
            error.clear();
            check(!writeSpreadFieldCopy(source, spread, QStringLiteral("spread"),
                                        QStringLiteral("typed across two pages"), 0, 20, &error)
                       .isEmpty(),
                  QStringLiteral("setup: one field with a widget on page 1 and another on page 21: %1")
                      .arg(error));
            const FieldTreeReport built = readFieldTree(spread);
            check(built.terminals == 2 && built.widgetsWithoutOwnName == 2 && built.parents == 1
                      && built.addresses.size() == 2
                      && built.addresses.value(0) == built.addresses.value(1)
                      && built.duplicateAddresses == 0 && built.terminalsNamingNoPage == 0
                      && built.unregisteredWidgets == 0 && built.parentChildMismatch == 0,
                  QStringLiteral("setup: that reads as ONE field drawn twice -- two widgets, one "
                                 "address %1, and two widgets of one field are not a collision: %2 "
                                 "terminals, %3 collisions")
                      .arg(built.addresses.value(0))
                      .arg(built.terminals)
                      .arg(built.duplicateAddresses));

            DocumentEditor apart;
            check(apart.open(spread, QString(), &error) && apart.splitInto(20, &error),
                  QStringLiteral("setup: the spread document opens and is split in two: %1")
                      .arg(error));
            const QString apartPath = scratch.filePath(QStringLiteral("spread-cut.pdf"));
            QStringList apartWritten;
            error.clear();
            check(apart.saveAs(apartPath, &error, &apartWritten) && apartWritten.size() == 2,
                  QStringLiteral("and the two pieces are written: %1").arg(error));
            QList<FieldTreeReport> spreadTrees;
            QVector<int> spreadOnPages;
            for (int index = 0; index < apartWritten.size(); ++index) {
                const FieldTreeReport piece = readFieldTree(apartWritten.at(index));
                const AnnotationReport pieceFlat = readAnnotations(apartWritten.at(index));
                int onPages = 0;
                for (int page = 0; page < pieceFlat.countPerPage.size(); ++page)
                    onPages += pieceFlat.countPerPage.at(page);
                spreadTrees << piece;
                spreadOnPages << onPages;
                std::cout << "MEASURE spread piece " << index + 1 << ": pages="
                          << pieceFlat.countPerPage.size() << " widgetsOnPages=" << onPages
                          << " parents=" << piece.parents << " terminals=" << piece.terminals
                          << " values=["
                          << piece.values.join(QStringLiteral("|")).toStdString()
                          << "] widgets naming no page=" << piece.terminalsNamingNoPage
                          << " unregisteredWidgets=" << piece.unregisteredWidgets << '\n';
            }
            // §52 pinned this as a gap and §76 closed the widget half of it. The readings that made
            // the repair narrow are unchanged and still asserted below -- a split gives **both**
            // pieces the whole field, and each piece has a box on its own pages, so the field cannot
            // be dropped. What changed is what hangs in /Kids: the box belonging to the other file's
            // page is no longer named here (2 terminals → 1, and nothing naming a page this file
            // lacks). The answer still travels, because it is on the field -- that half is still §51's
            // open decision, pinned further down and untouched by this fix.
            check(spreadTrees.size() == 2 && spreadOnPages.at(0) == 1 && spreadOnPages.at(1) == 1
                      && spreadTrees.at(0).terminals == 1 && spreadTrees.at(1).terminals == 1
                      && spreadTrees.at(0).terminalsNamingNoPage == 0
                      && spreadTrees.at(1).terminalsNamingNoPage == 0,
                  QStringLiteral("§76: each piece keeps exactly the one box that is on its own "
                                 "pages -- terminals=%1/%2, naming no page=%3/%4, boxes drawn=%5/%6")
                      .arg(spreadTrees.at(0).terminals).arg(spreadTrees.at(1).terminals)
                      .arg(spreadTrees.at(0).terminalsNamingNoPage)
                      .arg(spreadTrees.at(1).terminalsNamingNoPage)
                      .arg(spreadOnPages.at(0)).arg(spreadOnPages.at(1)));
            check(spreadTrees.at(0).parents == 1 && spreadTrees.at(1).parents == 1,
                  QStringLiteral("and the field itself stayed in both pieces, which is the half "
                                 "§52 ruled out pruning (parents=%1/%2)")
                      .arg(spreadTrees.at(0).parents).arg(spreadTrees.at(1).parents));
            check(spreadTrees.at(0).values.value(0) == QStringLiteral("typed across two pages")
                      && spreadTrees.at(1).values.value(0) == QStringLiteral("typed across two pages"),
                  QStringLiteral("and the value the reader typed once is still carried by both "
                                 "pieces (\"%1\" in each) -- this fix prunes boxes, not answers; "
                                 "whether the answer should travel is still §51's decision")
                      .arg(spreadTrees.at(0).values.value(0)));

            // And the same field with one of its two pages deleted: the name must stay, because a
            // box the reader can still see belongs to it. What goes dangling is the *kid*, which is
            // why a per-field repair would be the wrong shape here.
            DocumentEditor halved;
            check(halved.open(spread, QString(), &error) && halved.deletePages({0}, &error),
                  QStringLiteral("setup: the first of the two widget pages is deleted: %1")
                      .arg(error));
            const QString halvedPath = scratch.filePath(QStringLiteral("spread-one-page-gone.pdf"));
            error.clear();
            check(halved.saveAs(halvedPath, &error),
                  QStringLiteral("and that is written: %1").arg(error));
            const FieldTreeReport half = readFieldTree(halvedPath);
            const AnnotationReport halfFlat = readAnnotations(halvedPath);
            int halfOnPages = 0;
            for (int page = 0; page < halfFlat.countPerPage.size(); ++page)
                halfOnPages += halfFlat.countPerPage.at(page);
            std::cout << "MEASURE spread halved: pages=" << halfFlat.countPerPage.size()
                      << " widgetsOnPages=" << halfOnPages << " parents=" << half.parents
                      << " terminals=" << half.terminals
                      << " values=[" << half.values.join(QStringLiteral("|")).toStdString()
                      << "] widgets naming no page=" << half.terminalsNamingNoPage << '\n';

            check(half.terminals == 1 && half.widgetsWithoutOwnName == 1 && half.parents == 1
                      && halfOnPages == 1 && half.terminalsNamingNoPage == 0,
                  QStringLiteral("§76: with one of its two pages deleted the field now lists only "
                                 "the box that is still on a page here (terminals=%1, boxes drawn=%2, "
                                 "naming no page=%3) -- the name stayed, the answer stayed, and what "
                                 "went is the widget that could never draw")
                      .arg(half.terminals).arg(halfOnPages).arg(half.terminalsNamingNoPage));
            check(half.values.value(0) == QStringLiteral("typed across two pages"),
                  QStringLiteral("and the value is still the one the reader typed (\"%1\"): pruning "
                                 "a box must not reach the answer sitting on the field")
                      .arg(half.values.value(0)));

            {
                // The safety half nobody had a case for yet: a save that removes no page must not
                // lose a single box. Without this, "prune the widgets no page carries" is only ever
                // tested by the files where something is already gone, and a predicate written
                // against the wrong list would look correct in every check above.
                DocumentEditor untouchedSpread;
                error.clear();
                check(untouchedSpread.open(spread, QString(), &error),
                      QStringLiteral("setup: the spread form opens with nothing asked of it: %1")
                          .arg(error));
                const QString roundPath = scratch.filePath(QStringLiteral("spread-no-edit.pdf"));
                error.clear();
                check(untouchedSpread.saveAs(roundPath, &error),
                      QStringLiteral("setup: and a plain save writes it: %1").arg(error));
                const FieldTreeReport round = readFieldTree(roundPath);
                const AnnotationReport roundFlat = readAnnotations(roundPath);
                int roundOnPages = 0;
                for (int page = 0; page < roundFlat.countPerPage.size(); ++page)
                    roundOnPages += roundFlat.countPerPage.at(page);
                std::cout << "MEASURE spread no-edit: pages=" << roundFlat.countPerPage.size()
                          << " widgetsOnPages=" << roundOnPages << " terminals=" << round.terminals
                          << " naming no page=" << round.terminalsNamingNoPage << '\n';
                check(round.terminals == 2 && roundOnPages == 2 && round.terminalsNamingNoPage == 0,
                      QStringLiteral("a save with no page gone keeps both boxes on both pages "
                                     "(terminals=%1, drawn=%2) -- the prune is a repair for absent "
                                     "pages, not a cleaner")
                          .arg(round.terminals).arg(roundOnPages));
            }
        }

        // ---- §77: the field that was never on a page at all ----
        //
        // §51 left "should the answer travel" open, and that question cannot be answered without this
        // shape: a field registered in /Fields with a value and no widget, which PDF 2.0 §12.7.5.2
        // allows (a default, or a value a script reads). "Has no box on any page of this file" is
        // true of it AND of a widget whose page was deleted -- so the weaker predicate would delete a
        // legitimate value. What separates them on the file is /Rect, which the reader reports per
        // terminal from here on. The lines below are measurements first; the assertions follow what
        // they say.
        {
            const QString twoShapes = scratch.filePath(QStringLiteral("boxed-and-value-only.pdf"));
            error.clear();
            check(!writeBoxedAndValueOnlyFields(source, twoShapes, &error).isEmpty(),
                  QStringLiteral("setup: one field with a box, one registered without: %1")
                      .arg(error));
            const FieldTreeReport both = readFieldTree(twoShapes);
            std::cout << "MEASURE value-only as built: terminals=" << both.terminals
                      << " withRect=" << both.hasRect.count(true)
                      << " namingNoPage=" << both.terminalsNamingNoPage << " values=["
                      << both.values.join(QStringLiteral("|")).toStdString() << "]\n";

            DocumentEditor out;
            error.clear();
            check(out.open(twoShapes, QString(), &error) && out.deletePages({0}, &error),
                  QStringLiteral("setup: the page under the boxed one is deleted: %1").arg(error));
            const QString gonePath = scratch.filePath(QStringLiteral("boxed-and-value-only-gone.pdf"));
            error.clear();
            check(out.saveAs(gonePath, &error),
                  QStringLiteral("setup: and that is written: %1").arg(error));
            const FieldTreeReport gone = readFieldTree(gonePath);
            std::cout << "MEASURE value-only after the page is gone: terminals=" << gone.terminals
                      << " withRect=" << gone.hasRect.count(true)
                      << " namingNoPage=" << gone.terminalsNamingNoPage << " values=["
                      << gone.values.join(QStringLiteral("|")).toStdString() << "]\n";

            check(both.terminals == 2 && both.hasRect.count(true) == 1,
                  QStringLiteral("the two shapes are told apart on the file: %1 of %2 terminals "
                                 "carry a /Rect")
                      .arg(both.hasRect.count(true)).arg(both.terminals));
            check(both.values.value(0) == QStringLiteral("typed on page 1")
                      && both.values.value(1) == QStringLiteral("a default with no page"),
                  QStringLiteral("and as built both hold their value (\"%1\" / \"%2\")")
                      .arg(both.values.value(0)).arg(both.values.value(1)));

            check(gone.terminals == 2 && gone.hasRect.count(true) == 1
                      && gone.terminalsNamingNoPage == 1,
                  QStringLiteral("after the page goes, both have no live box and the file still "
                                 "registers both (%1 terminals, %2 naming a page that is gone) -- "
                                 "which is exactly why \"no box here\" cannot be the rule")
                      .arg(gone.terminals).arg(gone.terminalsNamingNoPage));
            check(gone.values.value(0).isEmpty()
                      && gone.values.value(1) == QStringLiteral("a default with no page"),
                  QStringLiteral("§77: the answer that belonged to the deleted page is not written "
                                 "out (\"%1\"), while the value that was never on a page is left "
                                 "exactly where its author put it (\"%2\") -- /Rect is what "
                                 "separates them")
                      .arg(gone.values.value(0)).arg(gone.values.value(1)));

            {
                // The guard §76 needed for boxes, now on the value side: a save that takes no page
                // out must not clear anything. Without it, "drawn nowhere here" could be satisfied by
                // a broken live-box set and every check above would still pass -- §76's second
                // control found exactly that failure mode on the other object.
                DocumentEditor kept;
                error.clear();
                check(kept.open(twoShapes, QString(), &error),
                      QStringLiteral("setup: the two-shape form opens with nothing asked: %1")
                          .arg(error));
                const QString keptPath =
                    scratch.filePath(QStringLiteral("boxed-and-value-only-noedit.pdf"));
                error.clear();
                check(kept.saveAs(keptPath, &error),
                      QStringLiteral("setup: and a plain save writes it: %1").arg(error));
                const FieldTreeReport held = readFieldTree(keptPath);
                std::cout << "MEASURE value-only no-edit save: terminals=" << held.terminals
                          << " values=[" << held.values.join(QStringLiteral("|")).toStdString()
                          << "]\n";
                check(held.terminals == 2
                          && held.values.value(0) == QStringLiteral("typed on page 1")
                          && held.values.value(1) == QStringLiteral("a default with no page"),
                      QStringLiteral("a save with no page gone writes both answers out (\"%1\" / "
                                     "\"%2\") -- the value rule fires on absent pages only")
                          .arg(held.values.value(0)).arg(held.values.value(1)));
                check(kept.hasFormFields(),
                      "and the model answers the question the two boxes ask before they say anything "
                      "about answers: this document has a form");
            }
            DocumentEditor plain;
            error.clear();
            check(plain.open(source, QString(), &error) && !plain.hasFormFields(),
                  QStringLiteral("the same question on a document with no form says no: %1")
                      .arg(error));
        }

        // ---- the last two shapes at the edge of the naming rule: an alias, and no name ----
        //
        // §50 asked what a merge renames when two *names* collide; §52 asked what an address is when
        // one name is drawn twice. What is left is the two ways a field's "name" is not the string
        // in /T: an alias (/TM, from the form specification) and no name at all. Both are asked here
        // because each one is a case where my own collision counter could be wrong, and because
        // "the same name twice" turns out to have a third meaning that nobody renames.
        {
            const QString aliasTarget = scratch.filePath(QStringLiteral("target-alias-beta.pdf"));
            const QString aliasDonor = scratch.filePath(QStringLiteral("donor-named-beta.pdf"));
            error.clear();
            check(!writeAliasedFieldCopy(source, aliasTarget, QStringLiteral("alpha"),
                                         QStringLiteral("beta"), QStringLiteral("typed as alpha"),
                                         &error)
                       .isEmpty(),
                  QStringLiteral("setup: the target's field is named alpha and aliased beta: %1")
                      .arg(error));
            check(!writeAliasedFieldCopy(donor, aliasDonor, QStringLiteral("beta"), QString(),
                                         QStringLiteral("typed as beta"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: the donor's field is *named* what the target's is aliased: %1")
                      .arg(error));

            DocumentEditor mixed;
            check(mixed.open(aliasTarget, QString(), &error)
                      && mixed.mergeFile(aliasDonor, {0}, DocumentEditor::MoveTarget::Back, -1,
                                         &error),
                  QStringLiteral("setup: the two are opened and merged: %1").arg(error));
            const QString mixedOut = scratch.filePath(QStringLiteral("merged-alias.pdf"));
            error.clear();
            check(mixed.saveAs(mixedOut, &error),
                  QStringLiteral("and written: %1").arg(error));
            const FieldTreeReport mixedTree = readFieldTree(mixedOut);
            std::cout << "MEASURE alias: terminals=" << mixedTree.terminals << " addresses=["
                      << mixedTree.addresses.join(QStringLiteral("|")).toStdString() << "] aliases=["
                      << mixedTree.aliases.join(QStringLiteral("|")).toStdString()
                      << "] collisions=" << mixedTree.duplicateAddresses
                      << " values=[" << mixedTree.values.join(QStringLiteral("|")).toStdString()
                      << "]\n";
            check(mixedTree.terminals == 2 && mixedTree.addresses.value(0) == QStringLiteral("alpha")
                      && mixedTree.addresses.value(1) == QStringLiteral("beta")
                      && mixedTree.duplicateAddresses == 0,
                  QStringLiteral("a name and an alias that are the same string are NOT a collision "
                                 "-- nothing was renamed, the form still reads [%1] and [%2]: %3 "
                                 "collisions")
                      .arg(mixedTree.addresses.value(0), mixedTree.addresses.value(1))
                      .arg(mixedTree.duplicateAddresses));
            check(mixedTree.aliases.value(0) == QStringLiteral("beta")
                      && mixedTree.values.value(0) == QStringLiteral("typed as alpha"),
                  QStringLiteral("and the alias survives the round trip with its owner: %1 still "
                                 "carries %2")
                      .arg(mixedTree.addresses.value(0), mixedTree.aliases.value(0)));
            check(pagesAsReadByTheReader(mixedOut) == pagesBefore + 1,
                  QStringLiteral("and Qt PDF opens the file with the alias in it: %1 pages, "
                                 "asked for %2")
                      .arg(pagesAsReadByTheReader(mixedOut))
                      .arg(pagesBefore + 1));

            // The other direction: two fields whose *names* differ but whose aliases match.
            const QString aliasDonor2 = scratch.filePath(QStringLiteral("donor-gamma-alias-beta.pdf"));
            error.clear();
            check(!writeAliasedFieldCopy(donor, aliasDonor2, QStringLiteral("gamma"),
                                         QStringLiteral("beta"), QStringLiteral("typed as gamma"),
                                         &error)
                       .isEmpty(),
                  QStringLiteral("setup: a donor field named gamma that borrows the same alias: %1")
                      .arg(error));
            DocumentEditor twin;
            check(twin.open(aliasTarget, QString(), &error)
                      && twin.mergeFile(aliasDonor2, {0}, DocumentEditor::MoveTarget::Back, -1,
                                         &error),
                  QStringLiteral("setup: merged into the aliased target: %1").arg(error));
            const QString twinOut = scratch.filePath(QStringLiteral("merged-same-alias.pdf"));
            error.clear();
            check(twin.saveAs(twinOut, &error), QStringLiteral("and written: %1").arg(error));
            const FieldTreeReport twinTree = readFieldTree(twinOut);
            std::cout << "MEASURE alias twin: addresses=["
                      << twinTree.addresses.join(QStringLiteral("|")).toStdString() << "] aliases=["
                      << twinTree.aliases.join(QStringLiteral("|")).toStdString()
                      << "] collisions=" << twinTree.duplicateAddresses << '\n';
            check(twinTree.terminals == 2 && twinTree.addresses.value(1) == QStringLiteral("gamma")
                      && twinTree.aliases.value(0) == twinTree.aliases.value(1)
                      && twinTree.duplicateAddresses == 0,
                  QStringLiteral("KNOWN, measured rather than assumed (docs §53): two fields in one "
                                 "document may carry the SAME alias (%1) and nothing renames them -- "
                                 "the repair keys on /T alone, so a viewer that addresses fields by "
                                 "/TM would see one name in two boxes; this product draws neither "
                                 "name, which is why it is recorded and not fixed")
                      .arg(twinTree.aliases.value(0)));

            // And no name at all: two fields that cannot collide because neither has an address.
            const QString nameless = scratch.filePath(QStringLiteral("two-nameless-fields.pdf"));
            error.clear();
            check(!writeTwoNamelessFields(source, nameless, &error).isEmpty(),
                  QStringLiteral("setup: two fields with no /T on one page: %1").arg(error));
            const FieldTreeReport namelessTree = readFieldTree(nameless);
            std::cout << "MEASURE nameless: terminals=" << namelessTree.terminals
                      << " nameless=" << namelessTree.namelessTerminals << " addresses=["
                      << namelessTree.addresses.join(QStringLiteral("|")).toStdString()
                      << "] collisions=" << namelessTree.duplicateAddresses
                      << " unregisteredWidgets=" << namelessTree.unregisteredWidgets << '\n';
            check(namelessTree.terminals == 2 && namelessTree.namelessTerminals == 2
                      && namelessTree.duplicateAddresses == 0
                      && namelessTree.unregisteredWidgets == 0,
                  QStringLiteral("two nameless fields are not a collision, because an empty address "
                                 "is not an address: %1 terminals, %2 nameless, %3 collisions -- and "
                                 "both boxes are still reachable from the form list (%4 orphaned)")
                      .arg(namelessTree.terminals)
                      .arg(namelessTree.namelessTerminals)
                      .arg(namelessTree.duplicateAddresses)
                      .arg(namelessTree.unregisteredWidgets));
        }

        // ---- XFA: the form's second copy, and whether anything keeps it in step ----
        //
        // §53 and §54 both left this named and untouched. It is the same question as §51 -- what is
        // left dangling when a page goes out -- asked of a copy of the form that lives in XML rather
        // than in dictionaries, and that some viewers treat as the form.
        {
            const QString xfaFile = scratch.filePath(QStringLiteral("target-with-xfa.pdf"));
            const QString xfaDonorField = scratch.filePath(QStringLiteral("donor-with-plain-field.pdf"));
            error.clear();
            check(!writeXfaFieldCopy(source, xfaFile, QStringLiteral("xfaField"),
                                     QStringLiteral("typed on page 1"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: a document whose form exists twice, as dictionaries and as XML: %1")
                      .arg(error));
            const XfaReport built = readXfa(xfaFile);
            check(built.present && built.pairs == 2
                      && built.text.contains(QStringLiteral("<xfaField>typed on page 1</xfaField>"))
                      && built.text.contains(QStringLiteral("name=\"P1\"")),
                  QStringLiteral("setup: the packet is really in there -- %1 pairs, the value and a "
                                 "page named P1 both readable in its bytes")
                      .arg(built.pairs));
            check(!writeFieldCopy(donor, xfaDonorField, QStringLiteral("donorField"),
                                  QStringLiteral("typed in the donor"), &error)
                       .isEmpty(),
                  QStringLiteral("setup: and a donor page carrying an ordinary field: %1").arg(error));

            // A. the field's page deleted: does the XML follow?
            DocumentEditor gone;
            check(gone.open(xfaFile, QString(), &error) && gone.deletePages({0}, &error),
                  QStringLiteral("setup: the packet document opens and loses its first page: %1")
                      .arg(error));
            const QString goneOut = scratch.filePath(QStringLiteral("xfa-page-deleted.pdf"));
            error.clear();
            check(gone.saveAs(goneOut, &error),
                  QStringLiteral("and the deletion is written: %1").arg(error));
            const XfaReport afterDelete = readXfa(goneOut);
            std::cout << "MEASURE xfa delete: present=" << afterDelete.present
                      << " pairs=" << afterDelete.pairs << " bytes="
                      << afterDelete.text.toUtf8().size() << " still says P1="
                      << afterDelete.text.contains(QStringLiteral("name=\"P1\""))
                      << " still says the answer="
                      << afterDelete.text.contains(
                             QStringLiteral("<xfaField>typed on page 1</xfaField>"))
                      << " pages=" << readAnnotations(goneOut).countPerPage.size() << '\n';
            check(afterDelete.present && afterDelete.pairs == 2
                      && afterDelete.text.contains(QStringLiteral("name=\"P1\"")),
                  QStringLiteral("KNOWN, measured (docs §55): the packet is carried through the "
                                 "deletion byte for byte and still describes a page this file no "
                                 "longer has (%1 pairs in a document of %2 pages) -- nothing in the "
                                 "write path reads XFA, and qpdf has no XFA facility at all")
                      .arg(afterDelete.pairs)
                      .arg(readAnnotations(goneOut).countPerPage.size()));

            // B. a split: every piece gets the whole form.
            DocumentEditor apart;
            check(apart.open(xfaFile, QString(), &error) && apart.splitInto(20, &error),
                  QStringLiteral("setup: the same document is split into two: %1").arg(error));
            const QString apartPath = scratch.filePath(QStringLiteral("xfa-cut.pdf"));
            QStringList apartWritten;
            error.clear();
            check(apart.saveAs(apartPath, &error, &apartWritten) && apartWritten.size() == 2,
                  QStringLiteral("and the two pieces are written: %1").arg(error));
            const XfaReport pieceOne = readXfa(apartWritten.at(0));
            const XfaReport pieceTwo = readXfa(apartWritten.at(1));
            std::cout << "MEASURE xfa pieces: one present=" << pieceOne.present << " pairs="
                      << pieceOne.pairs << " bytes=" << pieceOne.text.toUtf8().size()
                      << " ; two present=" << pieceTwo.present << " pairs=" << pieceTwo.pairs
                      << " bytes=" << pieceTwo.text.toUtf8().size() << " same text="
                      << (pieceOne.text == pieceTwo.text) << '\n';
            check(pieceOne.present && pieceTwo.present && pieceOne.text == built.text
                      && pieceTwo.text == built.text,
                  QStringLiteral("KNOWN, measured (docs §55): both pieces carry the entire packet, "
                                 "byte-identical to the source's -- so the form definition and every "
                                 "answer in it reach files that hold only half the pages (%1 and %2 "
                                 "bytes)")
                      .arg(pieceOne.text.toUtf8().size())
                      .arg(pieceTwo.text.toUtf8().size()));

            // C. a field merged INTO the packet document: the packet says nothing about it.
            DocumentEditor joined;
            check(joined.open(xfaFile, QString(), &error)
                      && joined.mergeFile(xfaDonorField, {0}, DocumentEditor::MoveTarget::Back, -1,
                                          &error),
                  QStringLiteral("setup: a donor field is merged into the packet document: %1")
                      .arg(error));
            const QString joinedOut = scratch.filePath(QStringLiteral("xfa-gained-field.pdf"));
            error.clear();
            check(joined.saveAs(joinedOut, &error),
                  QStringLiteral("and that is written: %1").arg(error));
            const XfaReport afterMerge = readXfa(joinedOut);
            const AnnotationReport mergedFields = readAnnotations(joinedOut);
            std::cout << "MEASURE xfa merge in: pairs=" << afterMerge.pairs << " bytes="
                      << afterMerge.text.toUtf8().size() << " mentions the new field="
                      << afterMerge.text.contains(QStringLiteral("donorField"))
                      << " acroform entries=" << mergedFields.fields << '\n';
            check(mergedFields.fields == 2 && !afterMerge.text.contains(QStringLiteral("donorField")),
                  QStringLiteral("the form list grows to %1 fields while the packet stays the size "
                                 "it was and never mentions the field that arrived -- the two copies "
                                 "of this form are now different forms (%2 bytes of XML)")
                      .arg(mergedFields.fields)
                      .arg(afterMerge.text.toUtf8().size()));
            check(pagesAsReadByTheReader(joinedOut) == pagesBefore + 1,
                  "and Qt PDF opens the file whose two form copies disagree");

            // D. a page merged OUT of the packet document: the packet does not travel with the page.
            DocumentEditor plain;
            check(plain.open(source, QString(), &error)
                      && plain.mergeFile(xfaFile, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: a packet-carrying page is merged into a plain document: %1")
                      .arg(error));
            const QString plainOut = scratch.filePath(QStringLiteral("xfa-arrived-page.pdf"));
            error.clear();
            check(plain.saveAs(plainOut, &error),
                  QStringLiteral("and that is written: %1").arg(error));
            const XfaReport arrived = readXfa(plainOut);
            const AnnotationReport arrivedFields = readAnnotations(plainOut);
            std::cout << "MEASURE xfa merge out: present=" << arrived.present << " acroform entries="
                      << arrivedFields.fields << " names=["
                      << arrivedFields.fieldNames.join(QStringLiteral("|")).toStdString() << "]\n";
            check(!arrived.present && arrivedFields.fields == 1,
                  QStringLiteral("the page came, its field came with it, and the packet did not: "
                                 "present=%1, %2 fields in the form list -- XFA belongs to the "
                                 "document, not the page, so a merged-in form arrives with only half "
                                 "of its description")
                      .arg(arrived.present)
                      .arg(arrivedFields.fields));
        }

        // ---- a field that brings its own drawing: does §54's "zero pixels" survive that? ----
        //
        // §54 measured a widget with nothing but /Rect, /T and /V, which is the smallest shape that
        // is a form field and not the shape a file carries once anyone has typed into it in a real
        // viewer. A real widget has an /AP appearance stream -- a little Form XObject that says what
        // to paint -- and Qt draws those for annotations. So the conclusion needed its own positive
        // control, and the file-level half of that control is this block.
        {
            const QString apFile = scratch.filePath(QStringLiteral("field-with-appearance.pdf"));
            error.clear();
            check(!writeFieldWithAppearanceCopy(source, apFile, QStringLiteral("drawnField"),
                                                QStringLiteral("typed on page 1"), 0, &error)
                       .isEmpty(),
                  QStringLiteral("setup: a widget carrying its own appearance stream: %1").arg(error));
            check(appearancesWithPaint(apFile, QStringLiteral("APPEARS-HERE")) == 1
                      && readFieldTree(apFile).terminals == 1,
                  "setup: one field, and its /AP /N decodes to the drawing the test painted");

            // Carried into a document that receives the page.
            DocumentEditor in;
            check(in.open(source, QString(), &error)
                      && in.mergeFile(apFile, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: the drawn page is merged into a plain document: %1")
                      .arg(error));
            const QString inOut = scratch.filePath(QStringLiteral("merged-with-appearance.pdf"));
            error.clear();
            check(in.saveAs(inOut, &error),
                  QStringLiteral("and the merge is written: %1").arg(error));
            std::cout << "MEASURE ap merged in: fields=" << readFieldTree(inOut).terminals
                      << " appearances carrying the drawing="
                      << appearancesWithPaint(inOut, QStringLiteral("APPEARS-HERE")) << '\n';
            check(readFieldTree(inOut).terminals == 1
                      && appearancesWithPaint(inOut, QStringLiteral("APPEARS-HERE")) == 1,
                  QStringLiteral("the appearance survives the copy whole: the field arrives with "
                                 "the drawing that paints it, not just with its value"));
            check(pagesAsReadByTheReader(inOut) == pagesBefore + 1,
                  "and the reader still opens the document that gained a drawn field");

            // Carried into a document that no longer has the page -- §51's dangling case, with the
            // addition that this time what dangles includes pixels.
            DocumentEditor out;
            check(out.open(apFile, QString(), &error) && out.deletePages({0}, &error),
                  QStringLiteral("setup: the drawn field's page is deleted: %1").arg(error));
            const QString outOut = scratch.filePath(QStringLiteral("appearance-page-deleted.pdf"));
            error.clear();
            check(out.saveAs(outOut, &error),
                  QStringLiteral("and that deletion is written: %1").arg(error));
            std::cout << "MEASURE ap dangling: fields=" << readFieldTree(outOut).terminals
                      << " appearances carrying the drawing="
                      << appearancesWithPaint(outOut, QStringLiteral("APPEARS-HERE"))
                      << " pages=" << readAnnotations(outOut).countPerPage.size() << '\n';
            check(readFieldTree(outOut).terminals == 1
                      && appearancesWithPaint(outOut, QStringLiteral("APPEARS-HERE")) == 1,
                  QStringLiteral("KNOWN GAP, same one §51 pins, widened here (docs §57): the file "
                                 "no longer holds the page and still registers the field **and** the "
                                 "XObject that draws it -- what rides along is not only the answer but "
                                 "the picture of a box on a page that is gone"));

            // And through 拆分: the piece without the page still gets the whole drawing.
            DocumentEditor cut;
            check(cut.open(apFile, QString(), &error) && cut.splitInto(20, &error),
                  QStringLiteral("setup: the drawn field is split apart: %1").arg(error));
            const QString cutPath = scratch.filePath(QStringLiteral("appearance-cut.pdf"));
            QStringList cutWritten;
            error.clear();
            check(cut.saveAs(cutPath, &error, &cutWritten) && cutWritten.size() == 2,
                  QStringLiteral("and the two pieces are written: %1").arg(error));
            const int firstPiece = appearancesWithPaint(cutWritten.at(0), QStringLiteral("APPEARS-HERE"));
            const int secondPiece = appearancesWithPaint(cutWritten.at(1), QStringLiteral("APPEARS-HERE"));
            std::cout << "MEASURE ap split: piece1 appearances=" << firstPiece
                      << " piece2 appearances=" << secondPiece << '\n';
            check(firstPiece == 1 && secondPiece == 1,
                  QStringLiteral("both pieces carry the appearance (piece 2 holds no page it could "
                                 "be drawn on): %1 and %2 copies of the drawing in the two files")
                      .arg(firstPiece)
                      .arg(secondPiece));
        }

        // ---- the instructions a form's drawing stands on: /DA, /DR, /Q, /NeedAppearances ----
        //
        // §57 proved the picture travels. A picture is drawn with a font, and in a real form the
        // font, the size, the colour and the alignment of a field's text live in the **document's**
        // /AcroForm and are inherited from there (PDF 2.0 §12.7.3.5), while /NeedAppearances is the
        // file saying "the drawings in here are not to be trusted, make your own". Our write path
        // copies pages. So four directions, asked of the bytes: what arrives, what stays behind, and
        // what the file a reader ends up with then tells whoever opens it.
        {
            auto dumpDirectives = [](const char *label, const DirectiveReport &r) {
                std::cout << "MEASURE " << label << ": form=" << r.hasForm
                          << " fields=" << r.terminals << " drawings=" << r.drawings
                          << " docDA=" << r.formHasDA << " docDR=" << r.formHasDR
                          << " needAppearances=" << r.needAppearances
                          << " docFonts=[" << r.formFonts.join(QStringLiteral(", ")).toStdString()
                          << "] given=[" << r.givenDirectives.join(QStringLiteral(", ")).toStdString()
                          << "] givenFonts=[" << r.givenFonts.join(QStringLiteral(", ")).toStdString()
                          << "] own=[" << r.ownDirectives.join(QStringLiteral(", ")).toStdString()
                          << "] drawingFonts=["
                          << r.drawingFonts.join(QStringLiteral(", ")).toStdString()
                          << "] unresolved=[" << r.unresolved.join(QStringLiteral(", ")).toStdString()
                          << "]\n";
            };

            // Two documents that both call a font /Helv, one meaning Helvetica and the other Times,
            // so the collision §46/§50 asked about names has its resource-layer twin on hand.
            const QString helvFile = scratch.filePath(QStringLiteral("directive-helvetica.pdf"));
            const QString timesFile = scratch.filePath(QStringLiteral("directive-times-donor.pdf"));
            const QString blankFile = scratch.filePath(QStringLiteral("directive-no-drawing.pdf"));
            error.clear();
            check(!writeDirectiveFieldCopy(source, helvFile, QStringLiteral("targetField"),
                                           QStringLiteral("typed in the target"),
                                           QStringLiteral("/Helv"), QStringLiteral("/Helvetica"),
                                           0, false, true, &error)
                          .isEmpty(),
                  QStringLiteral("setup: a form whose document offers Helvetica: %1").arg(error));
            check(!writeDirectiveFieldCopy(source, timesFile, QStringLiteral("donorField"),
                                           QStringLiteral("typed in the donor"),
                                           QStringLiteral("/Helv"), QStringLiteral("/Times-Roman"),
                                           1, true, true, &error)
                          .isEmpty(),
                  QStringLiteral("and a donor whose document offers Times under that same name: %1")
                      .arg(error));
            check(!writeDirectiveFieldCopy(source, blankFile, QStringLiteral("blankField"),
                                           QStringLiteral("nothing drawn"),
                                           QStringLiteral("/Helv"), QStringLiteral("/Times-Roman"),
                                           0, true, false, &error)
                          .isEmpty(),
                  QStringLiteral("and a donor that draws nothing because it asks the viewer to: %1")
                      .arg(error));

            const DirectiveReport built = readDirectives(timesFile);
            dumpDirectives("donor as built", built);
            check(built.terminals == 1 && built.drawings == 1 && built.unresolved.isEmpty(),
                  QStringLiteral("setup: one field, and its drawing finds the font it invokes: %1")
                      .arg(built.drawingFonts.join(QStringLiteral(" | "))));
            check(built.ownDirectives.value(0) == QStringLiteral("<none>|Q<none>")
                      && built.givenDirectives.value(0) == QStringLiteral("/Helv 0 Tf 0 g|Q1"),
                  QStringLiteral("setup, and the premise of everything below: the field states "
                                 "nothing about its own typography (%1) and is handed the document's "
                                 "by inheritance (%2)")
                      .arg(built.ownDirectives.value(0), built.givenDirectives.value(0)));

            // ---- in, into a document that never had a form ----
            DocumentEditor noForm;
            check(noForm.open(source, QString(), &error)
                      && noForm.mergeFile(timesFile, {0}, DocumentEditor::MoveTarget::Back, -1,
                                          &error),
                  QStringLiteral("setup: the Times donor's page goes into a document with no form: %1")
                      .arg(error));
            const QString noFormOut = scratch.filePath(QStringLiteral("directive-into-formless.pdf"));
            error.clear();
            check(noForm.saveAs(noFormOut, &error),
                  QStringLiteral("and that merge is written: %1").arg(error));
            const DirectiveReport arrived = readDirectives(noFormOut);
            dumpDirectives("into formless", arrived);
            check(arrived.hasForm && arrived.terminals == 1,
                  "the field itself arrives, which is what §36 and §50 pinned");
            check(arrived.drawings == 1 && arrived.unresolved.isEmpty()
                      && arrived.drawingFonts.value(0)
                          == QStringLiteral("/Helv resolves=/Times-Roman"),
                  QStringLiteral("and it arrives paintable: a drawing carries its own /Resources, so "
                                 "the font it names is still findable in the file it landed in: %1")
                      .arg(arrived.drawingFonts.join(QStringLiteral(" | "))));
            check(arrived.givenDirectives.value(0) == built.givenDirectives.value(0)
                      && arrived.givenFonts.value(0) == built.givenFonts.value(0),
                  QStringLiteral("the arriving field is handed the same instruction it was handed at "
                                 "home (%1, resolving to %2) -- so a viewer that regenerates its "
                                 "drawing gets the donor's typeface, not the receiver's")
                      .arg(arrived.givenDirectives.value(0))
                      .arg(arrived.givenFonts.value(0)));
            check(arrived.ownDirectives.value(0) == built.givenDirectives.value(0)
                      && arrived.givenDirectives.value(0) != built.ownDirectives.value(0),
                  QStringLiteral("and the mechanism, read off the file: the donor's *document* said "
                                 "%1 and its field said nothing (%2); in the merged file the field "
                                 "now says that itself (%3). qpdf pushes the inherited attributes down "
                                 "onto the field it copies, which is the one way a page can leave its "
                                 "document and keep its typography (docs §58: the claim this block was "
                                 "written to refute, and failed to)")
                      .arg(built.givenDirectives.value(0))
                      .arg(built.ownDirectives.value(0))
                      .arg(arrived.ownDirectives.value(0)));
            check(arrived.formHasDR && arrived.formFonts == built.formFonts
                      && arrived.needAppearances && !arrived.formHasDA,
                  QStringLiteral("the document-level /DR did come across with the page (fonts %1) and "
                                 "/NeedAppearances came with it (%2), while the document-level /DA did "
                                 "not -- it is the one that moved onto the field instead: docDA=%3")
                      .arg(arrived.formFonts.join(QStringLiteral(", ")))
                      .arg(int(arrived.needAppearances))
                      .arg(int(arrived.formHasDA)));
            check(pagesAsReadByTheReader(noFormOut) == pagesBefore + 1,
                  "and the reader still opens the document that gained the field");

            // ---- in, where both documents name /Helv and mean different fonts ----
            DocumentEditor both;
            check(both.open(helvFile, QString(), &error)
                      && both.mergeFile(timesFile, {0}, DocumentEditor::MoveTarget::Back, -1,
                                        &error),
                  QStringLiteral("setup: the Times donor goes into the Helvetica document: %1")
                      .arg(error));
            const QString bothOut = scratch.filePath(QStringLiteral("directive-font-collision.pdf"));
            error.clear();
            check(both.saveAs(bothOut, &error),
                  QStringLiteral("and that merge is written: %1").arg(error));
            const DirectiveReport collided = readDirectives(bothOut);
            dumpDirectives("font collision", collided);
            // Which entry is the arriving one is pinned by its own alignment (the donor's document
            // said /Q 1, the receiving one said nothing), not by a guess about the order of
            // /Fields -- a positional assumption is exactly the thing this suite keeps catching
            // itself making, and the two checks below are worth nothing if they read the wrong row.
            const int arrivedIndex = collided.givenDirectives.value(0).endsWith(QStringLiteral("|Q1"))
                ? 0 : 1;
            check(collided.givenDirectives.value(arrivedIndex)
                      != collided.givenDirectives.value(1 - arrivedIndex),
                  QStringLiteral("setup for the two claims below: the two fields are handed "
                                 "different instructions (%1 against %2), so they are separable and "
                                 "the row being read is identified")
                      .arg(collided.givenDirectives.value(arrivedIndex))
                      .arg(collided.givenDirectives.value(1 - arrivedIndex)));
            check(collided.terminals == 2 && collided.drawings == 2,
                  QStringLiteral("both fields are registered and both still carry a drawing: "
                                 "fields=%1 drawings=%2")
                      .arg(collided.terminals)
                      .arg(collided.drawings));
            check(collided.unresolved.isEmpty(),
                  QStringLiteral("the invariant this case exists to test: **every drawing in the "
                                 "merged file resolves the font name it invokes** -- one name, two "
                                 "fonts is survivable because the incoming drawing's key was "
                                 "renamed in step with its own content; unresolved: [%1]")
                      .arg(collided.unresolved.join(QStringLiteral(", "))));
            // The reading qpdf's header promises ("resolve conflicts ... common resource names
            // across documents") and delivers, plus the one asymmetry it leaves behind: the
            // arriving drawing's font key is renamed everywhere the *drawing* is looked at, so the
            // picture keeps its typeface -- but the arriving *field* has no /DA of its own here, so
            // the /DA it inherits is the receiving document's string, and in the merged /DR that
            // name now means the receiving document's font.
            check(collided.givenFonts.value(arrivedIndex) == QStringLiteral("/Helv means=/Helvetica")
                      && collided.drawingFonts.value(arrivedIndex)
                          == QStringLiteral("/Helv_1 resolves=/Times-Roman"),
                  QStringLiteral("MEASURED AND KNOWN (docs §58), not something this program is "
                                 "asked to fix: the field that came in is told to draw its text in "
                                 "%1 while the drawing it brought invokes %2 -- a viewer that "
                                 "regenerates appearances repaints it in the receiving document's "
                                 "font, and one that trusts the baked drawing shows the original")
                      .arg(collided.givenFonts.value(arrivedIndex))
                      .arg(collided.drawingFonts.value(arrivedIndex)));
            check(collided.ownDirectives.value(arrivedIndex) == QStringLiteral("<none>|Q1"),
                  QStringLiteral("and where the two keys parted: the alignment was pushed onto the "
                                 "arriving field (its own says %1), while the typeface was not -- "
                                 "with a /DA already on the receiving document there was nothing to "
                                 "push, so the field inherits a string whose font key now belongs to "
                                 "the other file. Same push-down mechanism, two different answers")
                      .arg(collided.ownDirectives.value(arrivedIndex)));

            // ---- in, the field that has no drawing at all ----
            //
            // The donor above said "make my drawing, and here is the font to make it with". That is
            // a complete, ordinary form -- and what it depends on is two document-level keys.
            DocumentEditor none;
            check(none.open(source, QString(), &error)
                      && none.mergeFile(blankFile, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: the undrawn field goes into a document with no form: %1")
                      .arg(error));
            const QString noneOut = scratch.filePath(QStringLiteral("directive-undrawn-in.pdf"));
            error.clear();
            check(none.saveAs(noneOut, &error),
                  QStringLiteral("and that merge is written: %1").arg(error));
            const DirectiveReport undrawn = readDirectives(noneOut);
            dumpDirectives("undrawn field in", undrawn);
            const DirectiveReport undrawnSource = readDirectives(blankFile);
            dumpDirectives("undrawn as built", undrawnSource);
            check(undrawn.terminals == 1 && undrawn.drawings == 0,
                  QStringLiteral("the field arrives and has no drawing: fields=%1 drawings=%2 -- "
                                 "nothing was ever supposed to draw it")
                      .arg(undrawn.terminals)
                      .arg(undrawn.drawings));
            check(undrawnSource.needAppearances && undrawnSource.formHasDA
                      && undrawnSource.drawings == 0,
                  QStringLiteral("control, the same field in the file it came from: it says "
                                 "\"make my appearance\" (%1) and offers the font to make it with (%2)"
                                 " -- so it is a field any viewer can draw")
                      .arg(int(undrawnSource.needAppearances))
                      .arg(int(undrawnSource.formHasDA)));
            check(undrawn.needAppearances && undrawn.formHasDR
                      && undrawn.givenDirectives.value(0) == QStringLiteral("/Helv 0 Tf 0 g|Q0")
                      && undrawn.drawings == 0,
                  QStringLiteral("and the field with no drawing does not arrive mute: the merged "
                                 "document still says \"make the appearances\" (%1), still offers the "
                                 "font to make them with (%2), and the field is handed %3 -- so the "
                                 "box a viewer would have drawn at home it can still draw here. The "
                                 "predicted gap here was wrong, and the reason it was wrong is the "
                                 "finding (docs §58)")
                      .arg(int(undrawn.needAppearances))
                      .arg(undrawn.givenFonts.value(0))
                      .arg(undrawn.givenDirectives.value(0)));
            check(undrawn.formFonts == undrawnSource.formFonts
                      && undrawn.needAppearances == undrawnSource.needAppearances,
                  QStringLiteral("measured rather than assumed: the document-level layer that came "
                                 "across is the same one the donor had -- fonts %1 against %2, "
                                 "/NeedAppearances %3 against %4")
                      .arg(undrawn.formFonts.join(QStringLiteral(", ")))
                      .arg(undrawnSource.formFonts.join(QStringLiteral(", ")))
                      .arg(int(undrawn.needAppearances))
                      .arg(int(undrawnSource.needAppearances)));
            check(pagesAsReadByTheReader(noneOut) == pagesBefore + 1,
                  "and this one opens too: the file gives no sign it contains an unpaintable field");

            // ---- out: the directives belong to the document, and every piece is a document ----
            DocumentEditor dCut;
            check(dCut.open(timesFile, QString(), &error) && dCut.splitInto(20, &error),
                  QStringLiteral("setup: the Times donor document is split: %1").arg(error));
            const QString dCutPath = scratch.filePath(QStringLiteral("directive-cut.pdf"));
            QStringList dCutWritten;
            error.clear();
            check(dCut.saveAs(dCutPath, &error, &dCutWritten) && dCutWritten.size() == 2,
                  QStringLiteral("and the two pieces are written: %1").arg(error));
            const DirectiveReport pieceOne = readDirectives(dCutWritten.at(0));
            const DirectiveReport pieceTwo = readDirectives(dCutWritten.at(1));
            dumpDirectives("piece holding the page", pieceOne);
            dumpDirectives("piece without it", pieceTwo);
            check(pieceOne.hasForm && pieceOne.formHasDA && pieceOne.formHasDR
                      && pieceOne.needAppearances && pieceOne.formFonts == built.formFonts
                      && pieceOne.drawings == 1,
                  QStringLiteral("the piece that holds the page keeps the whole directive layer: "
                                 "fonts=%1, 'make your own' still set")
                      .arg(pieceOne.formFonts.join(QStringLiteral(", "))));
            check(pieceTwo.hasForm && pieceTwo.formHasDA && pieceTwo.formHasDR
                      && pieceTwo.needAppearances && pieceTwo.terminals == 1
                      && pieceTwo.drawings == 1,
                  QStringLiteral("KNOWN GAP, the §51 leak widened a third time (docs §58): the piece "
                                 "with no page for the field to sit on still registers it (%1), still "
                                 "carries the drawing that would paint it (%2) and now also carries "
                                 "the font and the instruction to make one -- the answer, the picture, "
                                 "the type and the order to redraw travel together")
                      .arg(pieceTwo.terminals)
                      .arg(pieceTwo.drawings));

            DocumentEditor dGone;
            check(dGone.open(timesFile, QString(), &error) && dGone.deletePages({0}, &error),
                  QStringLiteral("setup: the field's page is deleted from the donor document: %1")
                      .arg(error));
            const QString dGoneOut = scratch.filePath(QStringLiteral("directive-page-gone.pdf"));
            error.clear();
            check(dGone.saveAs(dGoneOut, &error),
                  QStringLiteral("and that deletion is written: %1").arg(error));
            const DirectiveReport gone = readDirectives(dGoneOut);
            dumpDirectives("page deleted", gone);
            check(gone.terminals == 1 && gone.drawings == 1 && gone.formHasDA && gone.formHasDR
                      && gone.needAppearances && gone.unresolved.isEmpty(),
                  QStringLiteral("same shape from 删除页面: no page is left, and the file still "
                                 "registers the field, its drawing, its font and 'make your own'"));
        }

        // ---- and a move that leans against one of the two copies ----
        //
        // Where an identity that only named the file would go wrong. Two pages from one
        // file with one number are the same reference, so looking one of them up answers
        // for whichever comes first -- and a move that inserts behind the second copy
        // lands it behind the first. Nothing downstream notices: the document has every
        // page in it and the right count, which is the failure class this file keeps
        // reading labels for.
        DocumentEditor lean;
        check(lean.open(source, QString(), &error), "setup: the copy opens again");
        check(lean.mergeFile(donor, {1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the donor's second page goes in at the end");
        check(lean.mergeFile(donor, {1}, DocumentEditor::MoveTarget::AfterPage, 0, &error),
              "and that same page again, behind page 1");
        check(lean.movePages({1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "then page 2 of the opened document moves to the very end, behind both copies");
        const QString leanOut = scratch.filePath(QStringLiteral("merged-lean.pdf"));
        error.clear();
        check(lean.saveAs(leanOut, &error),
            QStringLiteral("and the three operations are written together: %1").arg(error));
        QVector<int> wantedLean;
        wantedLean << 1 << 6;
        for (int label = 3; label <= pagesBefore; ++label)
            wantedLean.append(label);
        wantedLean << 6 << 2;
        const QVector<int> leanWritten = orderInFile(leanOut);
        check(leanWritten == wantedLean,
              QStringLiteral("page 2 came to the end, behind the second copy of page 6 rather "
                             "than the first: got %1, wanted %2")
                  .arg(describeOrder(leanWritten), describeOrder(wantedLean)));

        // ---- the opened document merged into itself ----
        //
        // Nothing special about it to the copy path -- the pages are foreign objects in a
        // second handle on the same file -- and it is the merge a reader can most plausibly
        // ask for ("把这份的前两页也放到末尾"), so it is checked end to end, including the
        // refusal that has to come with it: the destination cannot be that same file.
        DocumentEditor itself;
        check(itself.open(source, QString(), &error), "setup: the copy opens again");
        check(itself.mergeFile(source, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
              "the document can be merged into itself");
        error.clear();
        check(!itself.saveAs(source, &error),
              "and saving it back over the file it opened is still refused");
        const QString itselfOut = scratch.filePath(QStringLiteral("merged-itself.pdf"));
        error.clear();
        check(itself.saveAs(itselfOut, &error),
            QStringLiteral("the doubled document writes elsewhere: %1").arg(error));
        QVector<int> wantedItself;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedItself.append(label);
        wantedItself << 1 << 2;
        check(orderInFile(itselfOut) == wantedItself,
              QStringLiteral("with the two pages at the end as well as where they were: got %1")
                  .arg(describeOrder(orderInFile(itselfOut))));
        check(hashOfFile(source) == originalHash, "and the file it read twice is unchanged");

        // ---- a file that changed after the merge was recorded ----
        //
        // The merge was promised against a document the reader could look at, and the save
        // reads it again. If it is no longer the file that was promised -- trimmed,
        // replaced, saved over by another program -- then the summary the reader acted on
        // names pages that may not exist, and the only honest answers are to refuse or to
        // say nothing at all. This is the case the up-front re-check exists for: the same
        // arithmetic planMerge() ran is run again over the real page tree at the end, and
        // the two have to agree before any bytes are written.
        DocumentEditor stale;
        check(stale.open(source, QString(), &error), "setup: the copy opens again");
        check(stale.mergeFile(donor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1, &error),
              "setup: three pages of the donor are recorded");
        check(writeDonor(source, donor, pagesBefore, {4}, &error) == donor,
              "setup: and then the donor is replaced with a one-page version of itself");
        const QString staleOut = scratch.filePath(QStringLiteral("merged-stale.pdf"));
        error.clear();
        check(!stale.saveAs(staleOut, &error) && !error.isEmpty(),
              QStringLiteral("the save notices, and refuses rather than writing what it can: %1")
                  .arg(error));
        check(error.contains(QStringLiteral("donor.pdf")),
              QStringLiteral("and the refusal names which file stopped matching: %1").arg(error));
        check(!QFile::exists(staleOut), "and no file was written on the way to refusing");
        check(hashOfFile(source) == originalHash, "and the opened document is untouched");

        // Restoring the donor is enough: the list is replayed from the file every save,
        // so what a save writes is what the files hold now, not what they held when the
        // dialog closed.
        check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
              "setup: the donor is put back the way it was");
        error.clear();
        check(stale.saveAs(staleOut, &error),
            QStringLiteral("and the merge that was refused now writes: %1").arg(error));
        QVector<int> wantedStale;
        for (int label = 1; label <= pagesBefore; ++label)
            wantedStale.append(label);
        wantedStale << 5 << 6 << 7;
        const QVector<int> staleWritten = orderInFile(staleOut);
        check(staleWritten == wantedStale,
              QStringLiteral("with the three pages it was promised holding: got %1")
                  .arg(describeOrder(staleWritten)));
        check(pagesAsReadByTheReader(donor) == 3,
              "and the donor is the three-page file it was again");

        // ---- merging out of a file that forbids being modified ----
        {
            DocumentEditor fromRestricted;
            check(fromRestricted.open(source, QString(), &error), "setup: the copy opens again");
            const bool recorded = fromRestricted.mergeFile(
                restricted, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error);
            note(QStringLiteral("merging two pages out of a document marked not-to-modify: %1")
                     .arg(recorded ? QStringLiteral("recorded") : error));
            if (recorded) {
                const QString out = scratch.filePath(QStringLiteral("merged-restricted.pdf"));
                error.clear();
                const bool wrote = fromRestricted.saveAs(out, &error);
                note(QStringLiteral("writing it: %1")
                         .arg(wrote ? QStringLiteral("succeeded") : error));
                if (wrote) {
                    QVector<int> labels = orderInFile(out);
                    check(labels.size() == pagesBefore + 2,
                          "and the copy the reader gets is the two documents' pages");
                    if (labels.size() == pagesBefore + 2) {
                        check(labels.at(pagesBefore) == 1 && labels.at(pagesBefore + 1) == 2,
                              "and the pages that came from the protected file are the pages "
                              "that were named, not a blank stand-in for them");
                    }
                }
            }
        }

        check(hashOfFile(source) == originalHash, "and none of the merge cases touched the "
                                                 "document that was opened");
    }

    // ---- a refused write leaves nothing behind ----
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens again");
        check(editor.deletePages({0}, &error), "setup: one page is taken out");
        const QString missing = scratch.filePath(QStringLiteral("no-such-folder/out.pdf"));
        error.clear();
        check(!editor.saveAs(missing, &error) && !error.isEmpty(),
              "a destination whose folder does not exist is refused, with a sentence");
        check(!QFile::exists(missing), "and no file was created on the way to refusing");
    }

    // ---- a destination that cannot be replaced keeps its own bytes ----
    //
    // The hazard is QPDFWriter having opened, and half-filled, a file the reader chose to replace.
    // The staged write is what makes that survivable, so it is tested by making the destination
    // unreplaceable in the one way §79's preflight cannot see: a read-only file. Asking that name
    // for the DELETE right succeeds -- the attribute stops writes to the contents, not a
    // delete-open -- while the move itself refuses it, so the finished document really is left
    // where the message can name it.
    //
    // The other cause, a name some program is reading, is refused before anything is staged, in
    // its own block near the end of this file. Two causes, two sentences, one assertion each --
    // §70's rule that a contract with two halves cannot be pinned by a single check, and the
    // reason this block used to occupy the destination with an open handle instead: that shape is
    // the early refusal now, so leaving it here would have tested one message twice and the other
    // one never.
    {
        const QString occupied = scratch.filePath(QStringLiteral("occupied.pdf"));
        {
            QFile seed(occupied);
            check(seed.open(QIODevice::ReadWrite), "a file is sitting at the destination");
            seed.write("this is not a pdf and must still be here afterwards");
            seed.flush();
        }
        const QString heldHash = hashOfFile(occupied);
        check(!heldHash.isEmpty(), "and it can be hashed before anything touches it");
        check(QFile::setPermissions(occupied, QFileDevice::ReadOwner),
              "and it is marked read-only, the one refusal the preflight cannot see");

        DocumentEditor editor;
        QString error;
        check(editor.open(source, QString(), &error), "setup: the copy opens");
        check(editor.deletePages({0}, &error), "setup: one page is taken out");
        const bool saved = editor.saveAs(occupied, &error);
        QFile::setPermissions(occupied, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        check(!saved, "replacing a read-only destination is refused");
        check(!error.isEmpty(), "the failure says what happened");
        // The distinction the two blocks together make: this name got *past* the preflight, so the
        // sentence must be the late one that names a staged file, not §79's early one. Without this
        // the pair would be two checks on the same message and nothing on the other.
        check(!error.contains(QStringLiteral("盖不掉")),
              QStringLiteral("and it is the late message rather than the early refusal: the "
                             "preflight let this name through -- %1").arg(error));
        check(hashOfFile(occupied) == heldHash,
              "the file that was there is byte for byte still there -- no half-written "
              "replacement, which is what the staged write exists to prevent");
        const QStringList left =
            QDir(scratch.path()).entryList(QStringList() << QStringLiteral("xyzpdf-save-*.pdf"),
                                           QDir::Files);
        check(left.size() == 1, "the finished copy is kept, where the message can name it");
        if (left.size() == 1) {
            const QString kept = scratch.filePath(left.first());
            check(pagesAsReadByTheReader(kept) == pagesBefore - 1,
                  "and the copy that was kept is a whole document with the page out of it");
            check(error.contains(left.first()),
                  QStringLiteral("the message names the file it left behind: %1").arg(error));
            QFile::remove(kept);
        }
    }

    // ---- encrypted documents, measured ----
    {
        // Each round trip below asserts that the copy's permission bits equal the
        // original's. Two numbers carried out here are what stop that from passing
        // vacuously: see the check at the end of the block.
        qint64 permissiveP = 0;
        qint64 restrictedP = 0;

        const QString locked = scratch.filePath(QStringLiteral("encrypted.pdf"));
        const QString complaint = writeEncryptedCopy(source, locked, QStringLiteral("hunter2"), true);
        check(complaint.isEmpty(),
              QStringLiteral("the password-protected test file could be written: %1").arg(complaint));
        check(pagesAsReadByTheReader(locked) == -1,
              "and the reader cannot open it without the password");

        DocumentEditor editor;
        QString error;
        const bool openedWithout = editor.open(locked, QString(), &error);
        note(QStringLiteral("opening a user-password file with no password: %1")
                 .arg(openedWithout ? QStringLiteral("succeeded") : error));
        check(!openedWithout, "the editor does not quietly read a file it was not given the key to");

        error.clear();
        const bool openedWith = editor.open(locked, QStringLiteral("hunter2"), &error);
        note(QStringLiteral("opening it with the password: %1")
                 .arg(openedWith ? QStringLiteral("succeeded") : error));
        if (openedWith) {
            check(editor.pageCount() == pagesBefore, "and the page count survives the key");
            check(editor.deletePages({1}, &error), "setup: a page comes out of the decrypted copy");
            const QString out = scratch.filePath(QStringLiteral("encrypted-result.pdf"));
            error.clear();
            const bool wrote = editor.saveAs(out, &error);
            note(QStringLiteral("writing an edited copy of an encrypted file: %1")
                     .arg(wrote ? QStringLiteral("succeeded") : error));
            if (wrote) {
                check(pagesAsReadByTheReader(out) == -1,
                      "the copy is still encrypted -- losing the protection on the way "
                      "through an edit would be worse than refusing the edit");
                check(pagesAsReadByTheReader(out, QStringLiteral("hunter2")) == pagesBefore - 1,
                      "and with the password it is the document that was asked for");

                // "Still encrypted" above only says Qt PDF could not open it without a
                // key, which a copy re-encrypted under some *other* key would satisfy
                // too. So the protection is compared as numbers. The raw read is there
                // because both files are parsed by the same library: a writer that
                // dropped the dictionary and a reader that defaulted to a value would
                // agree with each other and with itself.
                const Permissions before = readPermissions(locked, QStringLiteral("hunter2"));
                const Permissions after = readPermissions(out, QStringLiteral("hunter2"));
                note(QStringLiteral("permissions across the key-and-edit round trip: %1 -> %2")
                         .arg(describe(before), describe(after)));
                check(before.rawPFound && after.rawPFound && before.rawP == after.rawP,
                      "the copy's /P is the original's, out of the bytes and not out of "
                      "the same parser twice");
                check(after.encrypted && after.r == before.r && after.p == before.p,
                      "and QPDF agrees on R and P for both files");
                check(after.modifyAssembly == before.modifyAssembly
                        && after.modifyAnnotation == before.modifyAnnotation
                        && after.modifyOther == before.modifyOther,
                      "the three modify flags mean the same thing on both files");
                check(after.modifyOther, "this fixture does allow modification, and says so");
                permissiveP = after.rawP;
            } else {
                check(!QFile::exists(out), "a refused write left no file behind");
            }
        }

        const QString restricted = scratch.filePath(QStringLiteral("restricted.pdf"));
        const QString secondComplaint = writeEncryptedCopy(source, restricted, QString(), false);
        check(secondComplaint.isEmpty(),
              QStringLiteral("the restricted test file could be written: %1").arg(secondComplaint));
        check(pagesAsReadByTheReader(restricted) == pagesBefore,
              "the reader opens a restricted file as if nothing were wrong");

        DocumentEditor editor2;
        error.clear();
        const bool openedRestricted = editor2.open(restricted, QString(), &error);
        note(QStringLiteral("opening a file marked not-to-modify: %1")
                 .arg(openedRestricted ? QStringLiteral("succeeded") : error));
        if (openedRestricted) {
            check(editor2.deletePages({1}, &error), "setup: a page comes out");
            const QString out2 = scratch.filePath(QStringLiteral("restricted-result.pdf"));
            error.clear();
            const bool wrote = editor2.saveAs(out2, &error);
            note(QStringLiteral("writing an edited copy of a file marked not-to-modify: %1")
                     .arg(wrote ? QStringLiteral("succeeded") : error));
            if (wrote) {
                check(pagesAsReadByTheReader(out2) == pagesBefore - 1,
                      "the copy opens, with one page fewer than the original");
                check(pagesAsReadByTheReader(restricted) == pagesBefore,
                      "and the original is still the document it was");

                // The shape this case exists for: readable by anyone, "not to be
                // edited" written on it, and the reader who asks for that is the one
                // whose copy must not come back without the words.
                const Permissions before = readPermissions(restricted, QString());
                const Permissions after = readPermissions(out2, QString());
                note(QStringLiteral("not-to-modify across the round trip: %1 -> %2")
                         .arg(describe(before), describe(after)));
                check(before.rawPFound && after.rawPFound && before.rawP == after.rawP,
                      "the restricted copy carries the same /P as the restricted original");
                check(after.encrypted && after.r == before.r && after.p == before.p,
                      "same R, same P, still an encrypted document");
                check(after.modifyAssembly == before.modifyAssembly
                        && after.modifyAnnotation == before.modifyAnnotation
                        && after.modifyOther == before.modifyOther,
                      "and every modify flag is the one it was");
                check(!after.modifyOther,
                      "the copy still says the document may not be modified -- a round trip "
                      "that quietly drops that word would hand someone a file lying about "
                      "its own protection");
                restrictedP = after.rawP;
            } else {
                check(!QFile::exists(out2), "a refused write left no file behind");
            }
        }
        // The two fixtures differ by exactly one permission bit, and the numbers the
        // equality checks above compared differ by that same bit. A raw reader stuck at
        // zero, or one that always found the same integer, would satisfy every
        // before==after assertion in this block and fail here.
        check(permissiveP != restrictedP,
              QStringLiteral("the two fixtures disagree about modification (%1 vs %2), "
                             "so agreeing with itself is not what the checks above did")
                  .arg(permissiveP)
                  .arg(restrictedP));
        check(hashOfFile(source) == originalHash, "none of the encrypted cases touched the source");
    }

    // ---- the same round trip at a size a real document has ----
    //
    // The measurement above is on a 12 KB fixture, and a number from it that is
    // printed without the second one next to it says "this is instant", which is a
    // claim about a 40-page file rather than about the 500-page ones the delete
    // dialog will actually be pointed at. Same three calls, timed the same way.
    {
        const QString big = QStringLiteral("tests/fixtures/huge.pdf");
        if (!QFile::exists(big)) {
            note("tests/fixtures/huge.pdf is not there, so the scale measurement was skipped");
        } else {
            const QString bigFixtureHash = hashOfFile(big);
            const QString bigSource = scratch.filePath(QStringLiteral("big-source.pdf"));
            check(QFile::copy(big, bigSource), "setup: the large fixture was copied into the scratch folder");

            const QString bigResult = scratch.filePath(QStringLiteral("big-result.pdf"));
            const int bigPagesBefore = pagesAsReadByTheReader(bigSource);
            check(bigPagesBefore > 100, "and it is a document with enough pages to mean something");

            DocumentEditor editor;
            QString error;
            QElapsedTimer clock;
            clock.start();
            const bool done = editor.open(bigSource, QString(), &error)
                && editor.deletePages({0, bigPagesBefore / 2, bigPagesBefore - 1}, &error)
                && editor.saveAs(bigResult, &error);
            const qint64 taken = clock.elapsed();
            check(done, QStringLiteral("opening, taking three pages out of the big one and writing it: %1")
                            .arg(error));
            const int writtenPages = pagesAsReadByTheReader(bigResult);
            check(writtenPages == bigPagesBefore - 3,
                  "and the reader counts three pages fewer in the copy it wrote");
            note(QStringLiteral("%1 pages, %2 KB: open + delete + write took %3 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(taken));

            // The same at scale with a merge in it. This is the one worth the second
            // number: a deletion's save holds one document open, a merge's holds two,
            // and every page copied over brings its objects into the first one's tree.
            const QString bigDonor = scratch.filePath(QStringLiteral("big-donor.pdf"));
            check(writeDonor(bigSource, bigDonor, bigPagesBefore, {4, 5, 6}, &error) == bigDonor,
                  QStringLiteral("setup: a three-page donor cut out of the large fixture: %1")
                      .arg(error));

            const QString bigMerged = scratch.filePath(QStringLiteral("big-merged.pdf"));
            DocumentEditor merging;
            QElapsedTimer bigMergeClock;
            bigMergeClock.start();
            const bool mergedIn = merging.open(bigSource, QString(), &error)
                && merging.mergeFile(bigDonor, {0, 1, 2}, DocumentEditor::MoveTarget::Back, -1,
                                     &error)
                && merging.saveAs(bigMerged, &error);
            check(mergedIn,
                  QStringLiteral("opening, merging three pages into the big one and writing it: %1")
                      .arg(error));
            check(pagesAsReadByTheReader(bigMerged) == bigPagesBefore + 3,
                  "and the file it wrote is three pages longer, which is the merged three");
            note(QStringLiteral("%1 pages plus 3 merged in, %2 KB and a %3 KB donor: "
                                "open + merge + write took %4 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(QFileInfo(bigDonor).size() / 1024)
                     .arg(bigMergeClock.elapsed()));

            // And the same at scale for a split, which is the one save whose cost is a
            // multiple of everything above: a split replays and writes once per piece, so
            // this is the number that says whether "每 50 页一份" is a 12× save or a 3× one.
            const QString bigCut = scratch.filePath(QStringLiteral("big-cut.pdf"));
            DocumentEditor cutting;
            QElapsedTimer bigSplitClock;
            bigSplitClock.start();
            const bool cutUp = cutting.open(bigSource, QString(), &error)
                && cutting.splitInto(bigPagesBefore / 4, &error)
                && cutting.saveAs(bigCut, &error);
            check(cutUp,
                  QStringLiteral("opening, splitting the big one into four and writing it: %1")
                      .arg(error));
            const QVector<DocumentEditor::PieceRange> bigPieces = cutting.pieces();
            int acrossPieces = 0;
            for (int index = 0; index < bigPieces.size(); ++index)
                acrossPieces += pagesAsReadByTheReader(
                    DocumentEditor::piecePath(bigCut, index, bigPieces.size(), bigPieces.at(index)));
            check(acrossPieces == bigPagesBefore,
                  "and the four pieces together hold every page the document had, with none doubled");
            note(QStringLiteral("%1 pages, %2 KB: split four ways, four replays and four writes took "
                                "%3 ms")
                     .arg(bigPagesBefore)
                     .arg(QFileInfo(bigSource).size() / 1024)
                     .arg(bigSplitClock.elapsed()));

            check(hashOfFile(big) == bigFixtureHash, "and the large fixture is byte for byte what it was");
        }
    }

    // ---- 拆分: which pages go together, and what each file is called ----
    //
    // A split is the one operation in this class that changes no page, so none of the
    // checks above say anything about it. What is pinned down here is the set of files:
    // which pages land in one piece, what each piece is named, that the naming the dialog
    // showed is the naming the folder ends up with, and that a name already taken stops the
    // whole save rather than writing the pieces that happened to be free.
    {
        using Piece = DocumentEditor::PieceRange;
        const auto piece = [](int first, int last) {
            Piece range;
            range.first = first;
            range.last = last;
            return range;
        };
        const auto widths = [](const QVector<Piece> &pieces) {
            QStringList out;
            for (const Piece &one : pieces)
                out << QString::number(one.count());
            return out.join(QLatin1Char(','));
        };
        const auto bounds = [](const QVector<Piece> &pieces) {
            QStringList out;
            for (const Piece &one : pieces)
                out << QStringLiteral("%1-%2").arg(one.first).arg(one.last);
            return out.join(QLatin1Char(','));
        };
        const auto namesOf = [](const QString &base, const QVector<Piece> &pieces) {
            QStringList out;
            for (int index = 0; index < pieces.size(); ++index)
                out << QFileInfo(DocumentEditor::piecePath(base, index, pieces.size(),
                                                           pieces.at(index)))
                           .fileName();
            return out;
        };

        check(DocumentEditor::pieceRanges(10, 0).size() == 1,
              "no split pending still names one piece: the whole document");
        check(bounds(DocumentEditor::pieceRanges(10, 3)) == QStringLiteral("0-2,3-5,6-8,9-9"),
              "every third page: the last piece is what is left over, not another full one");
        check(widths(DocumentEditor::pieceRanges(10, 5)) == QStringLiteral("5,5"),
              "an exact multiple does not add an empty piece at the end");
        check(DocumentEditor::pieceRanges(10, 1).size() == 10,
              "one page per piece is ten pieces, the smallest split there is");
        check(DocumentEditor::pieceRanges(10, 10).size() == 1
                  && DocumentEditor::pieceRanges(10, 11).size() == 1,
              "a piece as big as the document, or bigger, comes out as one piece");
        check(DocumentEditor::pieceRanges(0, 3).isEmpty(),
              "and a document with no pages has no pieces either");

        // Contiguity over the shapes a remainder can take. Nothing here may drop a page or
        // put one page in two files, and "every piece starts where the last one ended, and
        // the two ends are the first and last page" is the whole claim.
        for (const int size : {3, 7, 39, 40}) {
            const QVector<Piece> pieces = DocumentEditor::pieceRanges(40, size);
            bool inOneRun = !pieces.isEmpty() && pieces.first().first == 0
                            && pieces.last().last == 39;
            int covered = 0;
            for (int index = 0; index < pieces.size(); ++index) {
                covered += pieces.at(index).count();
                if (index > 0 && pieces.at(index).first != pieces.at(index - 1).last + 1)
                    inOneRun = false;
            }
            check(inOneRun && covered == 40,
                  QStringLiteral("每 %1 页一份 covers pages 1 to 40 once each").arg(size));
        }

        // ---- the names ----
        check(DocumentEditor::piecePath(QStringLiteral("C:/docs/report.pdf"), 0, 3, piece(0, 14))
                  == QStringLiteral("C:/docs/report 01 (1-15).pdf"),
              "the first piece of three is 01 and says which pages it holds");
        check(DocumentEditor::piecePath(QStringLiteral("C:/docs/report.pdf"), 10, 12,
                                        piece(20, 20))
                  == QStringLiteral("C:/docs/report 11 (21).pdf"),
              "a one-page piece says 21, not 21-21");
        check(DocumentEditor::piecePath(QStringLiteral("D:/a/my.report.v2.pdf"), 2, 3,
                                        piece(54, 71))
                  == QStringLiteral("D:/a/my.report.v2 03 (55-72).pdf"),
              "dots inside a name are not the extension -- only the last one is");
        check(DocumentEditor::piecePath(QStringLiteral("D:/a/no-extension"), 0, 2, piece(0, 3))
                  == QStringLiteral("D:/a/no-extension 01 (1-4)"),
              "a destination typed with no extension gives pieces with no extension: the reader "
              "chose that for the name they typed, and this is not the place to second-guess it");

        // ---- the one rule those names and the two landing sentences share ----
        //
        // The check above is why this exists: §20 pinned "one page is a number, not 21-21" on
        // the file name, and that was the only exit it was pinned on. 移动页面 and
        // 合并文件进来 kept printing 5-5 / 40-40 for a single page, so the rule lives in
        // pageSpan() now and every caller reads it from here. Asked directly, because a rule
        // with four exits cannot be trusted from whichever one of them happens to have a test.
        check(DocumentEditor::pageSpan(0, 0) == QStringLiteral("1"),
              "the first page alone is 1");
        check(DocumentEditor::pageSpan(20, 20) == QStringLiteral("21"),
              "one page in the middle of a document is one number");
        check(!DocumentEditor::pageSpan(39, 39).contains(QLatin1Char('-')),
              "and there is no dash in it at all -- 40-40 was the shape that reached the screen");
        check(DocumentEditor::pageSpan(0, 14) == QStringLiteral("1-15"),
              "a run is its first and its last, 1-based like everything a reader counts");
        check(DocumentEditor::pageSpan(38, 39) == QStringLiteral("39-40"),
              "the last two pages of a 40-page document");
        // The two halves have to agree on the same page: a name from piecePath() and a number
        // from this function that drifted apart would put one range in a folder and another on
        // the screen.
        check(DocumentEditor::piecePath(QStringLiteral("C:/docs/r.pdf"), 0, 1, piece(39, 39))
                      .contains(DocumentEditor::pageSpan(39, 39)),
              "a one-page piece's name carries exactly this function's answer");

        // The padding is not decoration: a folder sorts by name, and pieces that read 1,
        // 10, 11, 2 are a page order nobody can check by eye.
        {
            const QVector<Piece> pieces = DocumentEditor::pieceRanges(60, 5);
            const QStringList ordered = namesOf(QStringLiteral("book.pdf"), pieces);
            QStringList sorted = ordered;
            std::sort(sorted.begin(), sorted.end());
            check(ordered == sorted,
                  QStringLiteral("twelve pieces of a 60-page book sort by name into page order: %1")
                      .arg(ordered.join(QLatin1Char(' '))));
            check(ordered.first() == QStringLiteral("book 01 (1-5).pdf")
                      && ordered.last() == QStringLiteral("book 12 (56-60).pdf"),
              QStringLiteral("wide enough for the last piece, not just the first: %1 ... %2")
                  .arg(ordered.first(), ordered.last()));
        }

        // The换档, pinned on both sides of it. Twelve pieces cannot tell a width that is
        // worked out from the piece list apart from a constant 2, and a constant 2 is the
        // bug: the folder a reader sees is sorted by name, so the first hundredth piece has
        // to drag every other name wider with it or `book 100 (100).pdf` lands between
        // `book 1 ...` and `book 2 ...`. 99 and 100 are the two names either side of the
        // line, and 99 is on the wrong side of it -- two digits there is correct, and it is
        // also what a fix that hard-wired the width would still do.
        {
            const QStringList ninetyNine = namesOf(QStringLiteral("book.pdf"),
                                                   DocumentEditor::pieceRanges(99, 1));
            check(ninetyNine.first() == QStringLiteral("book 01 (1).pdf")
                      && ninetyNine.last() == QStringLiteral("book 99 (99).pdf"),
                  QStringLiteral("99 pieces is still two digits wide, and the first name is "
                                 "padded with them: %1 ... %2")
                      .arg(ninetyNine.first(), ninetyNine.last()));

            const QVector<Piece> pieces = DocumentEditor::pieceRanges(100, 1);
            const QStringList hundred = namesOf(QStringLiteral("book.pdf"), pieces);
            check(hundred.first() == QStringLiteral("book 001 (1).pdf")
                      && hundred[98] == QStringLiteral("book 099 (99).pdf")
                      && hundred.last() == QStringLiteral("book 100 (100).pdf"),
                  QStringLiteral("the hundredth piece widens every name in the set, the first "
                                 "one included: %1 / %2 / %3")
                      .arg(hundred.first(), hundred[98], hundred.last()));
            QStringList sorted = hundred;
            std::sort(sorted.begin(), sorted.end());
            check(hundred == sorted,
                  "and a hundred one-page pieces still sort by name into page order");
        }

        // ---- the staged names, which nobody reads unless a write was refused ----
        //
        // Every piece is written under a name of this shape and only moved into place once
        // the whole set is on disk, so one rule holds for all of them: the path that comes
        // back must not already have a file behind it. Tested against a stamp fixed here,
        // because the stamp is what the caller brings in and this is the only way to reach
        // the counter's *ceiling* without a 200-page fixture and a hundred saves. In a
        // subfolder, so that the scan above for leftover files in the scratch folder stays
        // the check on that leftover file rather than a count of these seeds.
        {
            const QString folderName = QStringLiteral("staging");
            check(QDir(scratch.path()).mkpath(folderName), "a folder to work out names in");
            const QDir staging(scratch.filePath(folderName));
            const qint64 stamp = 1758765432100LL;
            const auto shape = [&](qint64 s, int piece, int attempt) {
                const QString name = attempt == 0
                    ? QStringLiteral("xyzpdf-save-%1-%2.pdf").arg(s).arg(piece)
                    : QStringLiteral("xyzpdf-save-%1-%2-%3.pdf").arg(s).arg(piece).arg(attempt);
                return staging.filePath(name);
            };
            const auto seed = [&](const QString &path) {
                QFile file(path);
                check(file.open(QIODevice::WriteOnly),
                      QStringLiteral("setup: %1 is put on disk before anything asks for a name")
                          .arg(QFileInfo(path).fileName()));
                file.write("not a pdf, and meant to stay exactly this size");
            };

            check(DocumentEditor::stagedPath(staging, stamp, 0) == shape(stamp, 0, 0),
                  "an empty folder gives the plain name -- pinned first, so a wrong answer "
                  "below is about the counter and not about what the name is supposed to look "
                  "like");

            seed(shape(stamp, 3, 0));
            seed(shape(stamp, 3, 2));
            check(DocumentEditor::stagedPath(staging, stamp, 3) == shape(stamp, 3, 3),
                  "a name that is taken gets stepped over, twice, until one is not");
            check(DocumentEditor::stagedPath(staging, stamp, 4) == shape(stamp, 4, 0),
                  "a neighbour piece's taken names do not move this piece's counter: the piece "
                  "number is what keeps the two writers apart");
            check(DocumentEditor::stagedPath(staging, stamp + 1, 3) == shape(stamp + 1, 3, 0),
                  "and the next save, a millisecond later, is not the one inheriting the "
                  "stepping");

            const int crowded = 7;
            seed(shape(stamp, crowded, 0));
            for (int attempt = 2; attempt <= 99; ++attempt)
                seed(shape(stamp, crowded, attempt));
            const QString lastTaken = shape(stamp, crowded, 99);
            const QString lastTakenHash = hashOfFile(lastTaken);
            check(!lastTakenHash.isEmpty(),
                  "setup: all 99 of that piece's names are on disk, including the 99th");

            const QString answered = DocumentEditor::stagedPath(staging, stamp, crowded);
            check(answered == shape(stamp, crowded, 100),
                  QStringLiteral("99 taken names is not the end of the counter: it answers %1")
                      .arg(QFileInfo(answered).fileName()));
            check(!QFile::exists(answered),
                  "and no file is behind the name it hands back -- the counter used to stop at "
                  "99 and return the 99th candidate untested, which is a finished document a "
                  "second writer would then truncate");
            check(hashOfFile(lastTaken) == lastTakenHash,
                  "and stepping past the 99th name leaves it alone, rather than opening it to "
                  "look");
        }

        // ---- recording it, and taking it back ----
        {
            DocumentEditor editor;
            QString error;
            check(editor.open(source, QString(), &error), "setup: the document to split opens");
            const int total = editor.pageCount();
            check(total == 40, QStringLiteral("setup: %1 pages").arg(total));

            check(editor.splitPieceSize() == 0 && editor.pieceCount() == 1,
                  "nothing is split yet, so a save would write one file");
            error.clear();
            check(!editor.splitInto(0, &error) && !error.isEmpty(),
                  "a piece of no pages is refused, and says so");
            check(!editor.splitInto(total, &error) && !error.isEmpty(),
                  "a piece as big as the document is refused too -- it writes one file, which "
                  "is what the reader already has");
            check(!editor.isDirty() && editor.pieceCount() == 1,
                  "and a refused split leaves nothing pending: no dirty tab, no undo row");

            check(editor.splitInto(15, &error), QStringLiteral("每 15 页一份 is recorded: %1").arg(error));
            check(editor.isDirty(), "a split on its own dirties the tab");
            check(editor.pageCount() == total, "and no page moved, turned or went away");
            check(editor.splitPieceSize() == 15 && editor.pieceCount() == 3,
                  "three pieces, counted by the same function the save loops over");
            check(editor.undoDescription().contains(QStringLiteral("15")),
                  QStringLiteral("the undo row says the size: %1").arg(editor.undoDescription()));
            check(editor.pendingSummary() == QStringLiteral("拆成每 15 页一份"),
                  QStringLiteral("and so is the whole status line: %1")
                      .arg(editor.pendingSummary()));

            check(editor.splitInto(8, &error), "a second split is the reader changing their mind");
            check(editor.splitPieceSize() == 8,
                  "the last one is what counts, not the first still sitting in the list");
            check(editor.undo() && editor.splitPieceSize() == 15,
                  "undo lands back on the first, which was still there");
            check(editor.undo() && editor.splitPieceSize() == 0 && !editor.isDirty(),
                  "a split alone undoes to a clean document");
            check(editor.redo() && editor.splitPieceSize() == 15,
                  "and redo brings it back with the size it was given");

            // The pieces a save lays down are cut from the document the *list* describes,
            // which is the same page order every other dialog here promises.
            check(editor.undo() && editor.deletePages({0, 1}, &error) && editor.splitInto(19, &error),
                  "setup: two pages out, then nineteen to a piece");
            check(editor.pageCount() == 38 && editor.pieceCount() == 2
                      && editor.pieces().last().last == 37,
                  "the split is numbered against the 38 pages a save writes, not the 40 on screen");
        }

        // ---- 拆分 and 合并 are the two that cannot share a document, either way round ----
        //
        // An imported page carries a number from a file the reader is no longer looking at
        // (docs/store-submission.md §17), so "第 6-10 页" would name pages of two different
        // documents and no piece boundary would mean anything. The rule has to be enforced
        // from both ends because the dialogs open in no fixed order.
        {
            const QString donor = scratch.filePath(QStringLiteral("split-donor.pdf"));
            QString error;
            check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
                  QStringLiteral("setup: a three-page donor: %1").arg(error));

            DocumentEditor mergedFirst;
            check(mergedFirst.open(source, QString(), &error), "setup: the document opens");
            check(mergedFirst.mergeFile(donor, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: two pages in: %1").arg(error));
            check(mergedFirst.importedPageCount() == 2, "setup: and they count as imported");
            error.clear();
            check(!mergedFirst.splitInto(10, &error),
                  "a split is refused while pages came in from another file");
            check(error.contains(QStringLiteral("2")),
                  QStringLiteral("and the refusal says how many pages make it impossible: %1")
                      .arg(error));
            check(mergedFirst.undo(), "setup: the merge is undone");
            check(mergedFirst.importedPageCount() == 0 && mergedFirst.splitInto(10, &error),
                  "and the very same split is accepted once they are gone");

            DocumentEditor splitFirst;
            check(splitFirst.open(source, QString(), &error), "setup: the document opens again");
            check(splitFirst.splitInto(10, &error), "setup: split first this time");
            error.clear();
            check(!splitFirst.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  "and a merge is refused while a split is pending -- the same rule, other end");
            check(splitFirst.undo()
                      && splitFirst.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1,
                                              &error),
                  QStringLiteral("undo the split and the merge goes through: %1").arg(error));
        }

        // ---- a save with a split pending writes a set, not a file ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.splitInto(15, &error), "setup: 15, 15 and the 10 left over");

            const QString base = scratch.filePath(QStringLiteral("cut.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            QElapsedTimer clock;
            clock.start();
            const bool wrote = editor.saveAs(base, &error);
            const qint64 taken = clock.elapsed();
            note(QStringLiteral("40 pages split 15/15/10: three replays and three writes took %1 ms "
                                "for a %2 KB source")
                     .arg(taken)
                     .arg(QFileInfo(source).size() / 1024));
            check(wrote, QStringLiteral("the split save goes through: %1").arg(error));
            check(!QFile::exists(base),
                  "and the name the reader typed is not itself written -- there is no document "
                  "with all forty pages in it any more");

            QStringList made;
            for (int index = 0; index < pieces.size(); ++index)
                made << DocumentEditor::piecePath(base, index, pieces.size(), pieces.at(index));
            check(made.size() == 3, "three pieces were promised, and");
            bool everyOne = true;
            for (const QString &one : std::as_const(made))
                everyOne = everyOne && QFile::exists(one);
            check(everyOne,
                  QStringLiteral("they are exactly the three files named: %1")
                      .arg(namesOf(base, pieces).join(QStringLiteral(", "))));

            // Contents page by page. A piece of the right length made of the wrong pages is
            // the failure a page count cannot see, and it is the one a hand-cut page tree
            // would make.
            for (int index = 0; index < made.size(); ++index) {
                const QVector<int> order = orderInFile(made.at(index));
                const Piece &one = pieces.at(index);
                bool rightRun = order.size() == one.count();
                for (int at = 0; at < order.size(); ++at)
                    rightRun = rightRun && order.at(at) == one.first + at + 1;
                check(rightRun,
                      QStringLiteral("piece %1 holds pages %2-%3, in that order: got %4")
                          .arg(index + 1)
                          .arg(one.first + 1)
                          .arg(one.last + 1)
                          .arg(describeOrder(order)));
            }
            check(hashOfFile(source) == originalHash,
                  "and the document that was split is byte for byte what it was");

            // The undo case, seen from the file side rather than the list side: taking the
            // split back has to go back to writing the one name the reader chose.
            check(editor.undo() && editor.splitPieceSize() == 0, "setup: the split is undone");
            const QString plain = scratch.filePath(QStringLiteral("whole.pdf"));
            check(editor.saveAs(plain, &error),
                  QStringLiteral("and a plain save still works with a split in the undo list: %1")
                      .arg(error));
            check(QFile::exists(plain) && !QFile::exists(scratch.filePath(QStringLiteral("whole 01 (1-15).pdf"))),
                  "one file, named exactly what was typed, with no piece names beside it");
        }

        // ---- a lot of pieces at once ----
        //
        // 每 1 页一份 of a forty-page document is forty finished files sitting in the
        // scratch folder waiting to be renamed. That shape is the one worth testing even
        // though nobody will thank us for it: it is where a staged name that two pieces
        // could use turns a second writer into a truncation of the first one's file, and
        // where "all of them are built before any is moved" stops being a figure of speech.
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.splitInto(1, &error), "setup: one page to a file");
            check(editor.pieceCount() == 40, "and the document has forty pieces");
            const QString base = scratch.filePath(QStringLiteral("single.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            QElapsedTimer clock;
            clock.start();
            const bool forty = editor.saveAs(base, &error);
            note(QStringLiteral("40 pages 每 1 页一份: forty replays and forty writes took %1 ms")
                     .arg(clock.elapsed()));
            check(forty, QStringLiteral("forty pieces are written: %1").arg(error));
            bool everyOne = !QFile::exists(base);
            for (int index = 0; index < pieces.size(); ++index) {
                const QString one = DocumentEditor::piecePath(base, index, pieces.size(),
                                                              pieces.at(index));
                const QVector<int> order = QFile::exists(one) ? orderInFile(one) : QVector<int>();
                everyOne = everyOne && order.size() == 1 && order.value(0) == index + 1;
            }
            check(everyOne,
                  "and there are forty files, each holding one page, each holding the page its "
                  "own name says it holds");
            check(QDir(scratch.path())
                      .entryList({QStringLiteral("xyzpdf-save-*.pdf")}, QDir::Files)
                      .isEmpty(),
                  "and nothing of ours is left in the folder under a temporary name");
        }

        // ---- a name already taken refuses the whole save ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            editor.splitInto(15, &error);
            const QString base = scratch.filePath(QStringLiteral("blocked.pdf"));
            const QVector<Piece> pieces = editor.pieces();
            const QString taken = DocumentEditor::piecePath(base, 1, pieces.size(), pieces.at(1));
            const QString first = DocumentEditor::piecePath(base, 0, pieces.size(), pieces.at(0));
            {
                QFile squatter(taken);
                check(squatter.open(QIODevice::WriteOnly),
                      "setup: one of the piece names is already somebody's file");
                squatter.write("not a pdf, and not ours");
            }
            error.clear();
            check(!editor.saveAs(base, &error), "the save is refused");
            check(error.contains(QFileInfo(taken).fileName()),
                  QStringLiteral("and the sentence names which one: %1").arg(error));
            check(!QFile::exists(first) && !QFile::exists(base),
                  "nothing was written: a folder with two of three pieces in it looks like a "
                  "split that worked");
            check(QFile::remove(taken), "setup: the name is freed again");
            error.clear();
            check(editor.saveAs(base, &error),
                  QStringLiteral("and the same save now goes through, so it was the name that "
                                 "was refused and nothing else: %1").arg(error));
            check(QFile::exists(first), "including the piece that had nowhere to go before");
            check(QFileInfo(taken).size() > 200,
                  "and the file that had been in the way is now a piece of the document rather "
                  "than the twenty-odd bytes that were there");
        }

        // ---- a piece that would land on the document being read ----
        //
        // Reachable without anybody meaning to: split a document, keep one piece, open that
        // piece later and split it again choosing the old base name. Same rule as 另存为不能
        // 覆盖正在打开的文件, reached one piece at a time.
        {
            const QString awkward = scratch.filePath(QStringLiteral("again 02 (21-40).pdf"));
            QString error;
            check(QFile::copy(source, awkward),
                  "setup: a forty-page copy named exactly the way a piece of 每 20 页一份 is named");
            DocumentEditor editor;
            check(editor.open(awkward, QString(), &error), "setup: it opens as a document");
            check(editor.splitInto(20, &error), "setup: two pieces, 1-20 and 21-40");
            check(!editor.saveAs(scratch.filePath(QStringLiteral("again.pdf")), &error),
                  "refused: the second piece would overwrite the document being read");
            check(error.contains(QFileInfo(awkward).fileName()),
                  QStringLiteral("and the sentence names it: %1").arg(error));
            check(!QFile::exists(scratch.filePath(QStringLiteral("again 01 (1-20).pdf"))),
                  "the piece that did not collide was not written either");
            check(hashOfFile(awkward) == originalHash, "and the source is untouched");
        }

        // ---- a split of a moved document follows the move, not the file ----
        {
            DocumentEditor editor;
            QString error;
            editor.open(source, QString(), &error);
            check(editor.movePages({8}, DocumentEditor::MoveTarget::Front, -1, &error),
                  "setup: page 9 goes to the very front");
            check(editor.splitInto(20, &error), "setup: and the document is cut in half");
            const QString base = scratch.filePath(QStringLiteral("moved-cut.pdf"));
            check(editor.saveAs(base, &error),
                  QStringLiteral("the split save of a moved document goes through: %1").arg(error));
            const QVector<Piece> pieces = editor.pieces();
            const QVector<int> head = orderInFile(
                DocumentEditor::piecePath(base, 0, pieces.size(), pieces.at(0)));
            check(head.size() == 20 && head.first() == 9 && head.at(1) == 1 && head.last() == 20,
                  QStringLiteral("the first piece is 9 then 1-8 then 10-20, so the move reached "
                                 "the file: %1").arg(describeOrder(head)));
        }

        // ---- every piece carries the protection the original said it had ----
        //
        // The single-save version of this is checked above; what a split adds is that the
        // writer is pointed at N destinations, and one piece arriving unprotected is as
        // much a lie as the whole copy arriving unprotected.
        {
            const QString locked = scratch.filePath(QStringLiteral("locked-for-split.pdf"));
            QString error;
            check(writeEncryptedCopy(source, locked, QString(), false).isEmpty(),
                  "setup: a copy readable by anyone and marked not to be edited");
            DocumentEditor editor;
            if (!editor.open(locked, QString(), &error)) {
                note(QStringLiteral("a file marked not-to-modify would not open for editing: %1")
                         .arg(error));
                check(false, "the encrypted split could not be set up -- see §11's measurement");
            } else {
                check(editor.splitInto(20, &error), "setup: two pieces");
                const QString base = scratch.filePath(QStringLiteral("locked-cut.pdf"));
                const QVector<Piece> pieces = editor.pieces();
                if (editor.saveAs(base, &error)) {
                    const Permissions before = readPermissions(locked, QString());
                    bool everyPiece = pieces.size() == 2;
                    for (int index = 0; index < pieces.size(); ++index) {
                        const Permissions after = readPermissions(
                            DocumentEditor::piecePath(base, index, pieces.size(),
                                                      pieces.at(index)),
                            QString());
                        everyPiece = everyPiece && after.rawPFound && after.encrypted
                                     && after.rawP == before.rawP && after.r == before.r;
                    }
                    check(everyPiece,
                          QStringLiteral("both pieces carry the same /P as the original: %1")
                              .arg(describe(before)));
                } else {
                    note(QStringLiteral("a split save of a file marked not-to-modify was "
                                        "refused: %1").arg(error));
                    check(false, "the pieces were written at all -- see §11's measurement");
                }
            }
        }
    }

    // ---- 加密 / 去掉口令: what a save hands the writer ----
    //
    // The second inert operation, and the one whose failure is not a wrong page count but a
    // wrong document. Two things can go wrong and both are checked here: the bytes (a copy
    // that stops saying "not to be edited" is a file lying about its own protection, and a
    // file that says it needs a password but opens without one is the same lie the other way
    // round), and the words (a password that turns up in an undo row or a discard prompt is a
    // password read over somebody's shoulder).
    {
        using Guard = DocumentEditor::Protection;
        const QString key = QStringLiteral("sw0rdfish!");
        QString error;

        const auto guardOf = [](const QString &password, bool print, bool extract,
                                bool assemble, bool annotate, bool form, bool other) {
            Guard guard;
            guard.what = Guard::What::Encrypt;
            guard.password = password;
            guard.allowPrint = print;
            guard.allowExtract = extract;
            guard.allowAssembly = assemble;
            guard.allowAnnotate = annotate;
            guard.allowFormFilling = form;
            guard.allowModifyOther = other;
            return guard;
        };
        const auto allOn = [&guardOf](const QString &password) {
            return guardOf(password, true, true, true, true, true, true);
        };
        // One box cleared, the other five left alone.
        const auto oneOff = [&guardOf](int which, const QString &password) {
            bool box[6] = { true, true, true, true, true, true };
            box[which] = false;
            return guardOf(password, box[0], box[1], box[2], box[3], box[4], box[5]);
        };
        const auto asBoxes = [](const Guard &guard) {
            BoxFlags flags;
            flags.print = guard.allowPrint;
            flags.extract = guard.allowExtract;
            flags.assemble = guard.allowAssembly;
            flags.annotate = guard.allowAnnotate;
            flags.form = guard.allowFormFilling;
            flags.other = guard.allowModifyOther;
            flags.answered = true;
            return flags;
        };
        // The six in the order `which` numbers them below, and in the order the dialog will
        // show them: one list for both, so a box that moves in the dialog has to move here.
        const char *boxNames[6] = { "print", "extract", "assemble", "annotate", "form", "other" };
        const auto flagOf = [](const BoxFlags &flags, int which) {
            switch (which) {
            case 0:
                return flags.print;
            case 1:
                return flags.extract;
            case 2:
                return flags.assemble;
            case 3:
                return flags.annotate;
            case 4:
                return flags.form;
            default:
                return flags.other;
            }
        };
        const auto sameBoxes = [&flagOf](const BoxFlags &one, const BoxFlags &other) {
            for (int which = 0; which < 6; ++which) {
                if (flagOf(one, which) != flagOf(other, which))
                    return false;
            }
            return true;
        };
        // The six as one string, because the notes below are the sentences §21 quotes.
        const auto said = [](const BoxFlags &flags) {
            return QStringLiteral("print=%1 extract=%2 assemble=%3 annotate=%4 form=%5 other=%6")
                .arg(flags.print)
                .arg(flags.extract)
                .arg(flags.assemble)
                .arg(flags.annotate)
                .arg(flags.form)
                .arg(flags.other);
        };
        // A file written from the plain source with one request hanging on it. An empty
        // answer means the setup did not go through, and `error` says which step stopped.
        const auto writeFromSource = [&](const QString &name, const Guard &guard) {
            const QString path = scratch.filePath(name);
            DocumentEditor editor;
            if (!editor.open(source, QString(), &error) || !editor.protectWith(guard, &error)
                || !editor.saveAs(path, &error))
                return QString();
            return path;
        };

        // ---- what the file on disk says, read once when it opens ----
        {
            DocumentEditor plain;
            check(plain.open(source, QString(), &error), "setup: the plain copy opens");
            check(!plain.sourceIsProtected(), "a document with no /Encrypt says it has none");
            check(plain.protectionToWrite().what == Guard::What::Carry,
                  "and with nothing pending a save would carry the source's own dictionary, "
                  "which for this file means writing no encryption parameters at all");
            check(plain.sourceProtection() == Guard(),
                  "an unencrypted source's six boxes stay at the defaults a dialog would open "
                  "on, rather than at answers QPDF gives a file that writes no bits");

            const QString keyed = scratch.filePath(QStringLiteral("protect-keyed.pdf"));
            check(writeEncryptedCopy(source, keyed, QStringLiteral("hunter2"), true).isEmpty(),
                  "setup: a copy that needs a password and does allow modifying");
            DocumentEditor kept;
            error.clear();
            check(kept.open(keyed, QStringLiteral("hunter2"), &error),
                  QStringLiteral("the editor opens it with the key: %1").arg(error));
            check(kept.sourceIsProtected(), "and it says the file it came from is protected");
            check(kept.protectionToWrite() == Guard(),
                  "opening a protected file is not itself a change: what a save would write is "
                  "still the carry-through default");
            note(QStringLiteral("what the source's own /P says: %1")
                     .arg(said(boxesAsWritten(keyed, QStringLiteral("hunter2")))));
            // The dialog's opening state, checked against the file rather than against
            // anything this program computed: these are the six answers a reader sees in six
            // boxes the moment 加密 / 去掉口令 opens, and the reader's first act in that
            // dialog is to notice which ones are already ticked.
            const BoxFlags onDisk = boxesAsWritten(keyed, QStringLiteral("hunter2"));
            const Guard read = kept.sourceProtection();
            check(sameBoxes(asBoxes(read), onDisk),
                  QStringLiteral("the six boxes open on what the file says, not on this "
                                 "program's defaults: %1 against %2")
                      .arg(said(asBoxes(read)), said(onDisk)));
            check(onDisk.answered && read.allowPrint && read.allowExtract
                      && !read.allowAssembly && read.allowAnnotate && read.allowFormFilling
                      && read.allowModifyOther,
                  QStringLiteral("and the fixture the check above compares against is the one "
                                 "this test asked for: %1").arg(said(onDisk)));
        }

        // ---- a request that is not a change ----
        {
            DocumentEditor editor;
            check(editor.open(source, QString(), &error), "setup: the plain copy opens again");
            check(!editor.isDirty(), "and it starts out clean");
            error.clear();
            check(!editor.removeProtection(&error) && !error.isEmpty(),
                  "去掉口令 on a document with no口令 is refused, and says why");
            check(!editor.isDirty() && editor.pendingCount() == 0,
                  "and a refused request records nothing: no dirty tab, no undo row");

            check(editor.protectWith(allOn(key), &error),
                  QStringLiteral("加上口令 is recorded: %1").arg(error));
            check(editor.isDirty() && editor.pendingCount() == 1,
                  "and it dirties the tab exactly the way a page edit does");
            check(editor.protectionToWrite().what == Guard::What::Encrypt
                      && editor.protectionToWrite().password == key,
                  "the protection a save would write is the one that was asked for");

            error.clear();
            check(!editor.protectWith(allOn(key), &error) && !error.isEmpty(),
                  "the same request a second time is refused -- it is not a change, and an undo "
                  "row for it would promise to take back something that did not happen");
            check(editor.pendingCount() == 1, "and the refused one left one operation, not two");

            check(editor.removeProtection(&error),
                  QStringLiteral("去掉口令 does cancel a 加密 that is only pending, even on a "
                                 "file with no口令 of its own: %1").arg(error));
            check(editor.pendingCount() == 2
                      && editor.protectionToWrite().what == Guard::What::Decrypt,
                  "as an operation of its own, so the first one is still there to undo back to");
            check(editor.undo() && editor.undo() && !editor.isDirty(),
                  "and both come off the list the way a delete does");
            check(editor.protectionToWrite() == Guard(),
                  "leaving nothing asked for, which is the state the document was opened in");
        }

        // ---- the last one asked for is the one a save writes ----
        {
            DocumentEditor editor;
            check(editor.open(source, QString(), &error), "setup: the plain copy opens again");
            const QString other = QStringLiteral("zwx9q!k");
            check(editor.protectWith(allOn(key), &error), "setup: the first password");
            check(editor.protectWith(guardOf(other, true, true, false, true, true, false), &error),
                  "setup: a second, different request -- the reader changing their mind");
            check(editor.pendingCount() == 2, "recorded as a second operation, not folded in");
            check(editor.protectionToWrite().password == other
                      && !editor.protectionToWrite().allowAssembly
                      && editor.protectionToWrite().allowExtract,
                  "and the save reads the last one: the second request's口令 and the second "
                  "request's bits, not a mix of the two");
            check(editor.undo() && editor.protectionToWrite().password == key
                      && editor.protectionToWrite().allowAssembly,
                  "one undo back is the earlier request standing again, whole");
            check(editor.undo() && editor.protectionToWrite() == Guard(),
                  "and undoing both asks for nothing at all");
            check(editor.redo() && editor.redo()
                      && editor.protectionToWrite().password == other,
                  "redo walks forward through the same two again");
        }

        // ---- the words: no row, summary or prompt ever carries the password ----
        {
            DocumentEditor editor;
            check(editor.open(source, QString(), &error), "setup: the plain copy opens again");
            check(editor.protectWith(allOn(key), &error), "setup: a口令 with something in it");
            const QString row = editor.undoDescription();
            note(QStringLiteral("what the undo row says about a 加密: %1").arg(row));
            check(!row.contains(key) && !row.contains(QStringLiteral("sw0rdfish")),
                  "the row does not carry the password, not even part of it");
            check(row.contains(QStringLiteral("口令")),
                  QStringLiteral("and still says what is about to be taken back: %1").arg(row));

            check(editor.deletePages({3}, &error), "setup: a page edit beside the口令");
            const QString summary = editor.pendingSummary();
            note(QStringLiteral("the summary beside a page edit: %1").arg(summary));
            check(!summary.contains(key) && !summary.contains(QStringLiteral("sw0rdfish")),
                  "the discard prompt is built out of this line, and it is the one text in this "
                  "program that is shown while a dialog is closed");

            check(editor.removeProtection(&error), "setup: take it off again");
            note(QStringLiteral("what the undo row says about 去掉口令: %1")
                     .arg(editor.undoDescription()));
            check(editor.undoDescription().contains(QStringLiteral("口令")),
                  "and the other direction is legible too");

            check(editor.protectWith(guardOf(QString(), true, true, false, true, true, false),
                                     &error),
                  "setup: restrictions with no password to open is a request that means something");
            const QString bitsOnly = editor.undoDescription();
            note(QStringLiteral("what the undo row says about bits without a口令: %1").arg(bitsOnly));
            check(bitsOnly.contains(QStringLiteral("只改权限"))
                      && !bitsOnly.contains(QStringLiteral("加上打开口令")),
                  QStringLiteral("and the row says which of the two it was, because a menu that "
                                 "offers 加上打开口令 over a file anybody can open is describing "
                                 "something that did not happen: %1").arg(bitsOnly));
        }

        // ---- the bytes: a file that says it needs a password, and needs it ----
        {
            error.clear();
            const QString out = writeFromSource(QStringLiteral("encrypt-out.pdf"), allOn(key));
            check(!out.isEmpty(),
                  QStringLiteral("a save with 加密 on it writes: %1").arg(error));
            if (!out.isEmpty()) {
                check(pagesAsReadByTheReader(out) == -1,
                      "this program's own reader will not open it without the口令");
                check(pagesAsReadByTheReader(out, key) == pagesBefore,
                      "and with it the document is whole, not shorter");
                check(pagesAsReadByTheReader(out, QStringLiteral("not-the-password")) == -1,
                      "and the口令 is what opens it, not any string at all");
                const Permissions got = readPermissions(out, key);
                note(QStringLiteral("what the written file says: %1 (%2)")
                         .arg(describe(got), said(boxesAsWritten(out, key))));
                check(got.encrypted && got.r == 6,
                      "R6, which is the only thing this program offers, and the dialog has to "
                      "say what that locks out");
                check(got.rawPFound, "the /Encrypt dictionary is in the file's own bytes");
                QtPdfBackend written;
                if (written.load(out, key) == Document::Status::Ready) {
                    const QImage image = written.render(0, QSize(300, 400), RenderOptions{});
                    check(!image.isNull() && paintsAnything(image),
                          "and the first page still has something on it -- an encrypted copy "
                          "that reads as blank pages is a copy that was not really written");
                } else {
                    check(false, "the reader opens the encrypted copy with its口令");
                }
                check(hashOfFile(source) == originalHash,
                      "and encrypting a copy left the source alone");
            }
        }

        // ---- §11.2, forked: the carry-through still stands, and a request moves it ----
        //
        // The check above the two numbers were compared for one reason -- to catch a writer
        // that dropped the dictionary and a reader that defaulted to a value. Now that a save
        // can be told to change the protection, the same read has to answer two ways: the
        // carry case must still come out identical (nothing was asked for), and the case with
        // a request on it must come out different. If both agreed, the second check would be
        // reading nothing.
        {
            const QString marked = scratch.filePath(QStringLiteral("protect-marked.pdf"));
            check(writeEncryptedCopy(source, marked, QString(), false).isEmpty(),
                  "setup: a copy readable by anyone and marked not to be edited");
            const QString markedHash = hashOfFile(marked);
            const Permissions was = readPermissions(marked, QString());
            check(was.rawPFound && was.encrypted && !was.modifyOther,
                  QStringLiteral("and it says what this test means by marked: %1").arg(describe(was)));

            DocumentEditor carry;
            check(carry.open(marked, QString(), &error), "setup: it opens for editing");
            check(carry.sourceIsProtected() && !carry.sourceProtection().allowModifyOther,
                  "and the editor read that from the file, not from a default");
            check(carry.protectionToWrite() == Guard(), "setup: nothing asked for");
            check(carry.deletePages({1}, &error), "a page out, and no protection request");
            const QString carried = scratch.filePath(QStringLiteral("protect-carried.pdf"));
            if (carry.saveAs(carried, &error)) {
                const Permissions now = readPermissions(carried, QString());
                check(now.rawPFound && now.encrypted && now.rawP == was.rawP,
                      QStringLiteral("the carry-through promise still holds for a save with no "
                                     "加密 on it: %1 -> %2").arg(describe(was), describe(now)));
            } else {
                note(QStringLiteral("a plain save of a marked file was refused: %1").arg(error));
                check(false, "the carried save wrote at all");
            }

            DocumentEditor changed;
            check(changed.open(marked, QString(), &error), "setup: the same file opens again");
            check(changed.deletePages({1}, &error), "setup: the same page edit");
            check(changed.protectWith(allOn(key), &error),
                  "setup: and a 加密 in the same save as it");
            const QString written = scratch.filePath(QStringLiteral("protect-changed.pdf"));
            if (changed.saveAs(written, &error)) {
                const Permissions now = readPermissions(written, key);
                note(QStringLiteral("the same read, with a request on it: %1 -> %2")
                         .arg(describe(was), describe(now)));
                check(now.rawPFound && now.rawP != was.rawP,
                      "the numbers moved, out of the bytes and not out of the same parser "
                      "twice -- which is what makes the check above a statement about the file");
                check(now.modifyOther && pagesAsReadByTheReader(written, key) == pagesBefore - 1,
                      "one save wrote both halves of the request: the page is gone and the "
                      "document carries the new口令 and the new bits");
                check(pagesAsReadByTheReader(written) == -1,
                      "and it does not open without one");
            } else {
                note(QStringLiteral("a save asked to re-protect was refused: %1").arg(error));
                check(false, "the re-protected save wrote at all");
            }

            DocumentEditor off;
            check(off.open(marked, QString(), &error), "setup: the marked file opens a third time");
            check(off.removeProtection(&error), "setup: 去掉口令 on a file that is protected");
            const QString freed = scratch.filePath(QStringLiteral("protect-off.pdf"));
            if (off.saveAs(freed, &error)) {
                const Permissions now = readPermissions(freed, QString());
                note(QStringLiteral("after 去掉口令: %1").arg(describe(now)));
                check(!now.encrypted && !now.rawPFound,
                      "the dictionary is gone out of the bytes too, not just out of what QPDF "
                      "answers when asked");
                check(pagesAsReadByTheReader(freed) == pagesBefore,
                      "and this program's reader opens it with no口令 at all, whole");
            } else {
                note(QStringLiteral("a save asked to strip the protection was refused: %1")
                         .arg(error));
                check(false, "the stripped save wrote at all");
            }
            check(hashOfFile(marked) == markedHash,
                  "and none of the four saves above touched the protected file they read");
        }

        // ---- the six boxes are six switches, and each one reaches the file ----
        //
        // What is asserted is structure rather than a table of bit values: the numbers in
        // PDF's /P belong to qpdf's writer, and hard-coding them here would have this test
        // agree with a guess about them instead of with the file. Three things have to be
        // true for a dialog with six boxes to be telling the truth:
        //   * each box changes the bytes (a box that writes nothing is decoration),
        //   * un-ticking one never grants anything (a box that added permission when cleared
        //     is the opposite of the word "allow"),
        //   * no two of them are the same switch (where two boxes share a bit, the second
        //     silently overrules the first and the summary the reader acted on is wrong).
        {
            error.clear();
            const QString every = writeFromSource(QStringLiteral("bits-all.pdf"), allOn(key));
            check(!every.isEmpty(), QStringLiteral("setup: the all-on file: %1").arg(error));
            const BoxFlags all = boxesAsWritten(every, key);
            note(QStringLiteral("all six ticked reads back: %1").arg(said(all)));
            check(all.answered && all.print && all.extract && all.assemble && all.annotate
                      && all.form && all.other,
                  "every box ticked is every flag set in the file, so the six have something to "
                  "be different about");
            bool foundOn = false;
            const qint64 onP = rawPermissionBits(every, &foundOn);
            check(foundOn, "and the all-on file has a /P of its own to compare against");
            // Narrowed to the 32 bits the field is: /P is a signed integer in the file, and
            // ~ on a 64-bit value would count 32 bits that are not there.
            const quint32 on = quint32(qint32(onP));

            bool eachOneDoesSomething = true;
            bool neverGrants = true;
            bool sixSeparateSwitches = true;
            bool oneFlagEach = true;
            QStringList owners;
            quint32 claimed = 0;
            for (int which = 0; which < 6; ++which) {
                const QString path = writeFromSource(QStringLiteral("bits-%1.pdf").arg(which),
                                                     oneOff(which, key));
                if (path.isEmpty()) {
                    check(false, QStringLiteral("setup: the file with %1 off: %2")
                                     .arg(QLatin1String(boxNames[which]), error));
                    eachOneDoesSomething = false;
                    continue;
                }
                const BoxFlags off = boxesAsWritten(path, key);
                bool foundOff = false;
                const qint64 offP = rawPermissionBits(path, &foundOff);
                // Printed whatever the checks decide, because this is where the mapping
                // between six checkboxes and one integer gets written down.
                note(QStringLiteral("%1 off: /P %2 -> %3, flags %4")
                         .arg(QLatin1String(boxNames[which]))
                         .arg(onP)
                         .arg(offP)
                         .arg(said(off)));
                const quint32 offBits = quint32(qint32(offP));
                const quint32 cleared = on & ~offBits;
                const quint32 granted = ~on & offBits;
                eachOneDoesSomething = eachOneDoesSomething && foundOff && off.answered
                    && cleared != 0;
                neverGrants = neverGrants && granted == 0;
                sixSeparateSwitches = sixSeparateSwitches && (claimed & cleared) == 0;
                claimed |= cleared;
                // The flag this program reads has to be the one this box writes, and the
                // other five have to be untouched by it -- which is the same claim the
                // dialog makes when it puts six labels on six boxes.
                for (int other = 0; other < 6; ++other) {
                    if (other == which)
                        oneFlagEach = oneFlagEach && !flagOf(off, other);
                    else
                        oneFlagEach = oneFlagEach && flagOf(off, other);
                }
                owners << QStringLiteral("%1=0x%2")
                              .arg(QLatin1String(boxNames[which]), QString::number(cleared, 16));
            }
            check(eachOneDoesSomething,
                  "clearing one box takes something away in the file's own bytes");
            check(neverGrants,
                  "and clearing a box never adds a permission back, whatever the mapping is");
            check(sixSeparateSwitches,
                  QStringLiteral("no two boxes own the same bits: %1")
                      .arg(owners.join(QLatin1Char(' '))));
            check(oneFlagEach,
                  "and this program's reader answers exactly one of its six flags per box -- "
                  "the round trip the dialog depends on, since the same six accessors are what "
                  "open() fills the boxes from next time");
            // Which integers those are belongs to qpdf's writer. Measured here, and the
            // sentence worth having in §21 is not "box 3 is PDF bit 6" but "the six are
            // independent and monotone, and this program's reader sees the same six" --
            // whether another vendor's reader draws the same words around the same integers
            // is not something this test can ask, and it is one more reason the dialog has
            // to say the permission bits are advisory rather than enforced.
            note(QStringLiteral("which bits each box owns: %1").arg(owners.join(QLatin1Char(' '))));
        }

        // ---- every piece of a split carries it ----
        //
        // The §20 check carried the source's own dictionary into N files; this one has N
        // writers handed a protection the reader typed, which is N chances to get one piece
        // wrong -- and a folder of four files where one opens for anybody is not four files.
        {
            DocumentEditor editor;
            check(editor.open(source, QString(), &error), "setup: the plain copy opens again");
            check(editor.protectWith(allOn(key), &error), "setup: 加密");
            check(editor.splitInto(20, &error), QStringLiteral("setup: 每 20 页一份: %1").arg(error));
            const QString base = scratch.filePath(QStringLiteral("encrypt-cut.pdf"));
            if (editor.saveAs(base, &error)) {
                const auto pieces = editor.pieces();
                bool everyPiece = pieces.size() == 2;
                for (int index = 0; index < pieces.size(); ++index) {
                    const QString one = DocumentEditor::piecePath(base, index, pieces.size(),
                                                                  pieces.at(index));
                    everyPiece = everyPiece
                        && pagesAsReadByTheReader(one) == -1
                        && pagesAsReadByTheReader(one, key) == pieces.at(index).count()
                        && readPermissions(one, key).r == 6;
                }
                check(everyPiece,
                      QStringLiteral("both pieces ask for the口令 and each holds the pages it "
                                     "was given: %1 pieces").arg(pieces.size()));
            } else {
                note(QStringLiteral("a split save with a口令 on it was refused: %1").arg(error));
                check(false, "the encrypted pieces were written at all");
            }
        }

        // ---- the key the copy asks for ----
        //
        // The tab that opens a saved result reads this rather than guessing, and both ways of
        // getting it wrong are things a reader sees: a prompt for a file that needs no key, or
        // a "cannot open" for one that does. The case worth pinning is the Carry over a
        // protected source -- nothing at all was asked for about protection there, and the
        // copy is still locked with the key the original was opened by.
        {
            DocumentEditor plain;
            check(plain.open(source, QString(), &error), "setup: the plain copy opens once more");
            check(plain.passwordOfWrittenFile().isEmpty(),
                  "an unprotected source with nothing pending writes a copy that asks for nothing");

            const QString keyed = scratch.filePath(QStringLiteral("keyed-for-prompt.pdf"));
            check(writeEncryptedCopy(source, keyed, QStringLiteral("hunter2"), true).isEmpty(),
                  "setup: a copy that needs a password");
            DocumentEditor kept;
            error.clear();
            check(kept.open(keyed, QStringLiteral("hunter2"), &error),
                  QStringLiteral("setup: it opens with the key: %1").arg(error));
            check(kept.passwordOfWrittenFile() == QStringLiteral("hunter2"),
                  "a save with nothing pending carries the source's own key into the copy, so "
                  "the tab that opens that copy has no business asking for it again");

            check(kept.protectWith(allOn(key), &error), "setup: 加密 over a protected source");
            check(kept.passwordOfWrittenFile() == key,
                  "with a new key on it the copy asks for the new one, not the source's");

            check(kept.removeProtection(&error), "setup: and then 去掉口令");
            check(kept.passwordOfWrittenFile().isEmpty(),
                  "once the protection is being taken off, the copy asks for nothing at all");

            check(kept.undo(), QStringLiteral("setup: 撤销 that: %1").arg(error));
            check(kept.passwordOfWrittenFile() == key,
                  "undoing it brings the new key back -- the answer follows the list, "
                  "which is the same rule the writer's parameters follow");
        }

        // ---- what it costs ----
        {
            QElapsedTimer clock;
            error.clear();
            DocumentEditor editor;
            clock.start();
            const bool done = editor.open(source, QString(), &error)
                && editor.protectWith(allOn(key), &error) && editor.isDirty()
                && editor.saveAs(scratch.filePath(QStringLiteral("timed-encrypt.pdf")), &error);
            const qint64 taken = clock.elapsed();
            note(QStringLiteral("open + 加密 + write of %1 pages took %2 ms (%3 KB out)")
                     .arg(pagesBefore)
                     .arg(taken)
                     .arg(done ? QFileInfo(scratch.filePath(QStringLiteral("timed-encrypt.pdf")))
                                    .size()
                                    / 1024
                               : 0));
            check(done, "and the timed save went through");
        }
    }

    // ---- 水印: where the ink goes, and which pages get it ----
    //
    // The third operation that changes no page's place, and the only one of the seven
    // that changes what is *drawn* on one. So the checks below are of two kinds: the
    // list-side ones that any of the inert operations already have (undo takes back one
    // row, a repeat replaces rather than stacks, nothing is on disk before 保存修改为),
    // and the pixels-side ones that only this one can need -- which pages end up drawing
    // a masked image, how many such images the file holds, whether a page that shares its
    // content array with an inked page stayed clean.
    //
    // What is deliberately *not* asserted here: that the marks read as the words that were
    // typed. The gate runs offscreen, where Qt resolves no font at all, so a check on the
    // rendered letters would be a check on the machine. tests/watermarkprobe.cpp is the one
    // that measured the pixels, and it is run under the windows platform by hand.
    {
        using Placement = DocumentEditor::Placement;
        using Mark = DocumentEditor::Watermark;

        // The corners of what gets drawn, the way a renderer would work them out from the
        // six numbers: the unit square's four corners through [a b c d e f].
        const auto corners = [](const Placement &m) {
            return QVector<QPointF>{ QPointF(m.e, m.f),
                                     QPointF(m.e + m.a, m.f + m.b),
                                     QPointF(m.e + m.c, m.f + m.d),
                                     QPointF(m.e + m.a + m.c, m.f + m.b + m.d) };
        };
        const auto inside = [](const QRectF &box, const QPointF &point) {
            return point.x() >= box.left() - 1e-6 && point.x() <= box.right() + 1e-6
                   && point.y() >= box.top() - 1e-6 && point.y() <= box.bottom() + 1e-6;
        };
        const auto length = [](const QPointF &p) { return std::hypot(p.x(), p.y()); };
        const auto same = [](const Placement &a, const Placement &b) {
            return qAbs(a.a - b.a) < 1e-9 && qAbs(a.b - b.b) < 1e-9 && qAbs(a.c - b.c) < 1e-9
                && qAbs(a.d - b.d) < 1e-9 && qAbs(a.e - b.e) < 1e-9 && qAbs(a.f - b.f) < 1e-9;
        };
        const auto finite = [](const Placement &m) {
            return std::isfinite(m.a) && std::isfinite(m.b) && std::isfinite(m.c)
                && std::isfinite(m.d) && std::isfinite(m.e) && std::isfinite(m.f);
        };

        // A wide canvas, because that is what a line of text is: fitting a square canvas
        // would hide a non-uniform scale, which is the failure that turns letters into a
        // stretched smear.
        const double canvasW = 1200.0;
        const double canvasH = 400.0;
        const QRectF portrait(0.0, 0.0, 595.0, 842.0);
        for (int degrees : {0, 90, 180, 270}) {
            const Placement m = DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH,
                                                                   degrees);
            check(finite(m), QStringLiteral("a %1° page gives six finite numbers").arg(degrees));
            const QVector<QPointF> four = corners(m);
            bool allInside = true;
            QPointF centre(0.0, 0.0);
            for (const QPointF &corner : four) {
                allInside = allInside && inside(portrait, corner);
                centre += corner;
            }
            // "Stays on the page" and "is in the middle of it" -- the two things a reader
            // looks at first, and the two that a matrix computed from the wrong edge breaks.
            check(allInside,
                  QStringLiteral("the marks on a %1° page are inside the page: (%2,%3)…(%4,%5)")
                      .arg(degrees)
                      .arg(four.at(0).x())
                      .arg(four.at(0).y())
                      .arg(four.at(3).x())
                      .arg(four.at(3).y()));
            centre /= 4.0;
            check(length(centre - portrait.center()) < 1e-6,
                  QStringLiteral("and centred on a %1° page: the four corners average (%2,%3)")
                      .arg(degrees)
                      .arg(centre.x())
                      .arg(centre.y()));
            // One scale for both axes: the drawn canvas keeps the shape it was painted with.
            const QPointF along(m.a, m.b);
            const QPointF across(m.c, m.d);
            check(qAbs(length(along) / length(across) - canvasW / canvasH) < 1e-9,
                  QStringLiteral("a %1° page stretches the bitmap by the same factor both ways")
                      .arg(degrees));
            // Which way is up. /Rotate is clockwise, so the marks turn counter-clockwise by
            // the same quarter turn to come out upright on the screen -- and a placement that
            // ignored the rotation entirely would look exactly like the 0° row four times.
            const int quarter = degrees / 90;
            const bool upright = quarter == 0 ? (qAbs(m.a) > 1e-6 && qAbs(m.b) < 1e-6)
                                 : quarter == 1 ? (qAbs(m.a) < 1e-6 && m.b > 1e-6)
                                 : quarter == 2 ? (m.a < -1e-6 && qAbs(m.b) < 1e-6)
                                                : (qAbs(m.a) < 1e-6 && m.b < -1e-6);
            check(upright, QStringLiteral("a %1° page turns the marks a quarter turn per %1° "
                                          "(a=%2 b=%3)")
                              .arg(degrees)
                              .arg(m.a)
                              .arg(m.b));
        }

        // The same page stored sideways and marked 90 is what a rotated document actually is,
        // and it is the case where using the unrotated edges shows: the long side of the box
        // is the one the marks have to run along once the viewer has turned the paper.
        {
            const QRectF asStored(0.0, 0.0, 842.0, 595.0);
            const Placement turned = DocumentEditor::watermarkPlacement(asStored, canvasW,
                                                                       canvasH, 90);
            const Placement standing = DocumentEditor::watermarkPlacement(portrait, canvasW,
                                                                         canvasH, 0);
            // hypot(a, b) is the image's own x axis in page space -- how long the marks are
            // along the direction they read, whichever way the paper is turned.
            check(qAbs(length(QPointF(turned.a, turned.b))
                       - length(QPointF(standing.a, standing.b))) < 1e-6,
                  "a page stored landscape and marked 90 gets the marks as long as the page's "
                  "displayed width, not its stored one");
            check(length(QPointF(turned.a, turned.b)) > 0.0,
                  "setup: the landscape page's marks have a length to compare at all");
        }

        // A rotation that is not a quarter turn, and the same turn written two ways. Both are
        // answers real files give -- /Rotate is a plain integer a producer can write as -90 or
        // 450 -- and neither may reach a content stream as dust: std::cos(M_PI / 2.0) is
        // 6.1e-17, which is a watermark a twentieth of a point off its centre for no reason.
        check(same(DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, -90),
                   DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, 270)),
              "-90 and 270 are one turn");
        check(same(DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, 450),
                   DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, 90)),
              "450 is 90, not a turn and a half of anything else");
        check(same(DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, 45),
                   DocumentEditor::watermarkPlacement(portrait, canvasW, canvasH, 0)),
              "and a /Rotate that is not a multiple of 90 is read as the quarter turn it "
              "contains rather than turned through an angle nobody can check");

        // A box that does not start at the origin (a page whose /CropBox is a window onto a
        // bigger /MediaBox) and the two degenerates a damaged file can hand back.
        {
            const QRectF offset(30.0, 40.0, 595.0, 842.0);
            const Placement m = DocumentEditor::watermarkPlacement(offset, canvasW, canvasH, 0);
            bool allInside = true;
            for (const QPointF &corner : corners(m))
                allInside = allInside && inside(offset, corner);
            check(allInside, "the marks follow the box's own corner, not the page's origin");
        }
        check(same(DocumentEditor::watermarkPlacement(QRectF(0, 0, 0, 842), canvasW, canvasH, 0),
                   Placement()),
              "a page with no width is a matrix that draws nothing, not a division by zero");
        check(same(DocumentEditor::watermarkPlacement(portrait, 0.0, canvasH, 0), Placement()),
              "and so is a canvas with no width");
        check(finite(DocumentEditor::watermarkPlacement(QRectF(0, 0, 14400, 14400), canvasW,
                                                       canvasH, 90)),
              "the largest page PDF allows still gives six numbers a parser accepts");

        // ---- recording it ----
        DocumentEditor mark;
        QString error;
        const auto request = [](const QString &text, int opacity, const QVector<int> &pages) {
            DocumentEditor::Watermark watermark;
            watermark.text = text;
            watermark.opacityPercent = opacity;
            watermark.pages = pages;
            return watermark;
        };

        error.clear();
        check(!mark.addWatermark(request("机密", 20, {0}), &error) && !error.isEmpty(),
              "加水印 on a document that was never opened is refused with a sentence");

        check(mark.open(source, QString(), &error), "setup: the document to ink opens");
        check(mark.watermarkToWrite().text.isEmpty(),
              "nothing pending is a watermark with no text, which is what the dialog opens on");

        error.clear();
        check(!mark.addWatermark(request("  ", 20, {0}), &error) && !error.isEmpty(),
              "blank text is refused -- an empty canvas is still a rectangle over the page");
        check(!mark.addWatermark(request("机密", 0, {0}), &error),
              "0% is not a pale watermark, it is no watermark, and the reader should be told "
              "rather than handed a save that writes nothing");
        check(!mark.addWatermark(request("机密", 101, {0}), &error),
              "and so is a number a writer cannot be handed");
        check(!mark.addWatermark(request("机密", 20, {}), &error),
              "naming no page names nothing to ink");
        check(!mark.addWatermark(request("机密", 20, {pagesBefore + 5}), &error),
              "and a page number past the end is the same nothing");
        check(!mark.isDirty() && !mark.canUndo(),
              "none of those refusals left a row in the list");

        const Mark asked = request("机密", 20, {2, 0, 2, 1});
        check(mark.addWatermark(asked, &error),
              QStringLiteral("an odd order with a repeat is accepted: %1").arg(error));
        check(mark.watermarkToWrite().pages == QVector<int>({0, 1, 2}),
              QStringLiteral("and it is normalised into page order, once each: %1")
                  .arg(describeOrder(mark.watermarkToWrite().pages)));
        check(mark.isDirty() && mark.pageCount() == pagesBefore,
              "the tab is dirty and no page moved, turned or went away");
        const QString row = mark.undoDescription();
        check(row.contains(QStringLiteral("水印")) && row.contains(QStringLiteral("机密")),
              QStringLiteral("the undo row names the operation and the words: %1").arg(row));
        check(!row.contains(QStringLiteral("%")),
              QStringLiteral("and the row says nothing about the opacity beyond what it can "
                             "say safely: %1").arg(row));
        // The number here is the number of pages the marks land on. Read off the generic page list
        // it was 0 -- a 水印 records no structural pages -- so the row named a watermark over
        // nothing while the dialog the reader had just closed promised three. That is the same
        // "one promise, one function" rule the split and the protection rows follow, and this is
        // the row that catches it being broken.
        check(row.contains(QStringLiteral("3 页")) && !row.contains(QStringLiteral("0 页")),
              QStringLiteral("and it counts the pages it actually names: %1").arg(row));

        check(mark.addWatermark(request("草稿", 35, {3}), &error),
              QStringLiteral("a second 水印 is the reader changing the wording: %1").arg(error));
        check(mark.watermarkToWrite().text == QStringLiteral("草稿")
                  && mark.pendingCount() == 2,
              "the last one is what a save draws, and the first is still in the list behind it "
              "-- the rule splitInto() and protectWith() follow, so one list answers both");
        error.clear();
        check(!mark.addWatermark(request("草稿", 35, {3, 3}), &error) && !error.isEmpty(),
              "and asking for the very same thing again is refused, because it would change "
              "nothing and dirty the tab for it");
        check(mark.undo() && mark.watermarkToWrite().text == QStringLiteral("机密")
                  && mark.watermarkToWrite().pages == QVector<int>({0, 1, 2}),
              "one undo lands back on the first wording, not on nothing");
        check(mark.undo() && mark.watermarkToWrite().text.isEmpty() && !mark.isDirty(),
              "a 水印 alone undoes to a clean document, and the star goes with it");
        check(mark.redo() && mark.watermarkToWrite().text == QStringLiteral("机密"),
              "redo walks the branch forward again from the first of the two, which is the way "
              "undo and redo work for every operation in this list");
        check(mark.redo() && mark.watermarkToWrite().text == QStringLiteral("草稿"),
              "and the second redo puts the replaced wording back");
        check(mark.undo() && mark.undo() && !mark.isDirty(), "setup: back to nothing pending");

        // Pages an earlier edit took out are not pages to ink, and the two ways that can fail
        // are worth telling apart: naming a gone page is a mistake about the document, and a
        // range that is *only* gone pages is the one that cannot be carried out at all.
        check(mark.deletePages({4, 5}, &error), "setup: two pages out first");
        error.clear();
        check(!mark.addWatermark(request("机密", 20, {4}), &error) && !error.isEmpty(),
              QStringLiteral("naming only deleted pages is refused, with a sentence: %1")
                  .arg(error));
        check(mark.addWatermark(request("机密", 20, {3, 4}), &error),
              QStringLiteral("a range that straddles the deletion is not: %1").arg(error));
        check(mark.watermarkToWrite().pages == QVector<int>({3}),
              "the page that is already gone is dropped when the request is taken, which is the "
              "filter rotatePages() applies too -- so the count the undo row names and the count "
              "the save draws ink for are one number, not two");
        check(mark.undo(), "setup: the 水印 is taken back");
        check(mark.deletePages(everyPage(pagesBefore).mid(0, pagesBefore - 1), &error),
              "setup: down to one page");
        error.clear();
        check(!mark.addWatermark(request("机密", 20, {0, 1, 2}), &error),
              "after everything else has gone, a range naming only absent pages is refused");
        check(mark.undo() && mark.undo(), "setup: back to the whole document");
        check(mark.pageCount() == pagesBefore, QStringLiteral("setup: %1 pages").arg(pagesBefore));

        // ---- a save, page by page ----
        {
            DocumentEditor editor;
            check(editor.open(source, QString(), &error), "setup: the copy opens again");
            check(editor.addWatermark(request("机密", 20, {0, 1, 2}), &error),
                  QStringLiteral("setup: three pages inked: %1").arg(error));

            const QString result = scratch.filePath(QStringLiteral("inked.pdf"));
            QElapsedTimer clock;
            clock.start();
            const bool wrote = editor.saveAs(result, &error);
            const qint64 taken = clock.elapsed();
            check(wrote, QStringLiteral("a save with a 水印 on it writes: %1").arg(error));

            const InkReport ink = readInk(result);
            check(ink.readable && ink.pageCount == pagesBefore,
                  "and the reader still counts the same number of pages in it");
            check(ink.drawn == QVector<int>({0, 1, 2}),
                  QStringLiteral("the three named pages draw the marks, and only they do: %1")
                      .arg(describeOrder(ink.drawn)));
            check(ink.namedButNotDrawn.isEmpty(),
                  QStringLiteral("no page names the marks without drawing them: %1")
                      .arg(describeOrder(ink.namedButNotDrawn)));
            check(ink.maskedImages == 1,
                  QStringLiteral("the whole file carries one bitmap, shared: %1")
                      .arg(ink.maskedImages));
            bool pale = !ink.alphas.isEmpty();
            for (double alpha : std::as_const(ink.alphas))
                pale = pale && qAbs(alpha - 0.2) < 1e-4;
            check(pale, QStringLiteral("and every one of them is drawn at the /ca the dialog "
                                       "showed: %1")
                            .arg(describeAlphas(ink.alphas)));

            check(orderInFile(result) == orderInFile(source),
                  "the pages are still in the order they were, which is the promise a content "
                  "stream write has no business breaking");
            check(hashOfFile(source) == originalHash,
                  "and the document that was inked is byte for byte what it was");

            const qint64 inked = QFileInfo(result).size();
            DocumentEditor plain;
            QString plainError;
            plain.open(source, QString(), &plainError);
            const QString plainPath = scratch.filePath(QStringLiteral("uninked.pdf"));
            const qint64 untouchedSize = plain.saveAs(plainPath, &plainError)
                ? QFileInfo(plainPath).size()
                : 0;
            note(QStringLiteral("%1 pages, 3 of them watermarked: %2 KB out against %3 KB "
                                "unwatermarked, i.e. %4 bytes per inked page; %5 ms")
                     .arg(pagesBefore)
                     .arg(inked / 1024)
                     .arg(untouchedSize / 1024)
                     .arg(untouchedSize > 0 ? (inked - untouchedSize) / 3 : 0)
                     .arg(taken));
            check(inked > untouchedSize && inked - untouchedSize < 200 * 1024,
                  QStringLiteral("the marks are in the file and they are not a megabyte: %1 -> %2")
                      .arg(untouchedSize)
                      .arg(inked));

            // The pages this request did not name must not have lost anything either: the
            // same text, the same count, read through the renderer.
            check(pagesAsReadByTheReader(result) == pagesBefore,
                  "and Qt PDF opens the inked file");
        }

        // ---- a rotated page, in the file rather than in the arithmetic ----
        {
            const QString turnedSource = scratch.filePath(QStringLiteral("turned.pdf"));
            check(QFile::copy(QStringLiteral("tests/fixtures/rotated.pdf"), turnedSource),
                  "setup: the one-page fixture marked /Rotate 90 is copied");
            DocumentEditor editor;
            check(editor.open(turnedSource, QString(), &error), "setup: it opens");
            check(editor.addWatermark(request("机密", 50, {0}), &error), "setup: inked");
            const QString result = scratch.filePath(QStringLiteral("turned-inked.pdf"));
            check(editor.saveAs(result, &error),
                  QStringLiteral("setup: and saved: %1").arg(error));

            const InkReport ink = readInk(result);
            check(ink.drawn == QVector<int>({0}), "the rotated page is the inked one");
            if (ink.matrices.size() == 1) {
                // The page is stored 300x400 and displayed 400x300. A placement that ignored
                // /Rotate -- which is what both routes the probe measured did, question 4 --
                // puts the long run along the stored x axis and reads sideways on the screen.
                const QVector<double> m = ink.matrices.at(0);
                check(qAbs(m[0]) < 1e-4 && m[1] > 1.0,
                      QStringLiteral("the marks run up the page, along y, because the paper is "
                                     "turned: a=%1 b=%2 c=%3 d=%4")
                          .arg(m[0])
                          .arg(m[1])
                          .arg(m[2])
                          .arg(m[3]));
            } else {
                check(false, "the rotated page's placement could not be read back at all");
            }
            check(!ink.alphas.isEmpty() && qAbs(ink.alphas.first() - 0.5) < 1e-4,
                  QStringLiteral("and 50% is the /ca that reached it: %1")
                      .arg(describeAlphas(ink.alphas)));
        }

        // ---- two pages that share one content array and one resources dictionary ----
        //
        // The control for the two copy-before-write rules in the save. A real producer does
        // this for every page of a form's letterhead, and qpdf's own addPageContents() does
        // not guard it (its copy-if-shared covers inheritable attributes, and /Contents is not
        // one), so the version that reaches for the library's helper puts a watermark on a
        // page nobody named and passes every check above.
        {
            const QString shared = writeSharedPagesCopy(
                source, scratch.filePath(QStringLiteral("shared-pages.pdf")));
            check(!shared.isEmpty(), "setup: a file with two pages reading one content array");
            DocumentEditor editor;
            check(editor.open(shared, QString(), &error), "setup: it opens for editing");
            check(editor.addWatermark(request("机密", 20, {1}), &error),
                  QStringLiteral("setup: only the second page is named: %1").arg(error));
            const QString result = scratch.filePath(QStringLiteral("shared-inked.pdf"));
            check(editor.saveAs(result, &error), QStringLiteral("the save goes through: %1")
                                                      .arg(error));

            const InkReport ink = readInk(result);
            check(ink.drawn == QVector<int>({1}),
                  QStringLiteral("and the page that was named is the page that draws them: %1")
                      .arg(describeOrder(ink.drawn)));
            check(ink.namedButNotDrawn.isEmpty(),
                  "the page that shared its resources with that one did not gain the marks "
                  "either, in the dictionary or on the page");
            check(ink.maskedImages == 1, "and the two pages still share the one bitmap");
            check(ink.pageCount == pagesBefore, "no page was lost along the way");
        }

        // ---- a page whose content is neither a stream nor an array ----
        //
        // §26 recorded this branch as reachable in the source and never asked by a check: the
        // only thing that had touched it was an i18n probe, which proves the sentence is in the
        // catalogue and says nothing about whether the code ever gets there. Three promises are
        // read below -- the save stops, it says which judgement stopped it, and the request is
        // still on the list afterwards. That last one is the reason this refuses instead of
        // writing the page without its marks: nothing reached disk, so 撤销 has to stay a real
        // option rather than a menu row describing a file that already exists.
        {
            const QString literal
                = scratch.filePath(QStringLiteral("contents-as-literal.pdf"));
            QString built;
            check(writeContentsShape(source, literal, 1, ContentsShape::LiteralString, &built),
                  QStringLiteral("setup: a file whose second page carries its operators as a "
                                 "string instead of a stream: %1").arg(built));
            const QString literalHash = hashOfFile(literal);
            DocumentEditor editor;
            check(editor.open(literal, QString(), &error),
                  QStringLiteral("and the document still opens -- reading a page like that is "
                                 "Qt PDF's business, not this one: %1").arg(error));
            check(editor.addWatermark(request("机密", 20, {1}), &error),
                  QStringLiteral("setup: the damaged page is the one the reader named: %1")
                      .arg(error));

            const QString refused = scratch.filePath(QStringLiteral("contents-as-literal-out.pdf"));
            error.clear();
            check(!editor.saveAs(refused, &error),
                  "the save refuses rather than appending to content it cannot parse");
            check(error.contains(QStringLiteral("内容流")),
                  QStringLiteral("and the sentence says which judgement it made: %1").arg(error));
            check(!QFile::exists(refused), "and no file is at the destination it was asked for");
            check(QDir(scratch.path())
                      .entryList({QStringLiteral("xyzpdf-save-*.pdf")}, QDir::Files)
                      .isEmpty(),
                  "and nothing of ours is left in the folder under a temporary name");
            check(editor.isDirty() && editor.undoDescription().contains(QStringLiteral("水印")),
                  QStringLiteral("the request is still pending, so 撤销 still describes "
                                 "something: %1").arg(editor.undoDescription()));
            check(hashOfFile(literal) == literalHash,
                  "and the document worked on is byte for byte what it was");

            // The same file with the *healthy* page named. Without this control the chain above
            // reads as "水印 on this scratch file fails", which a scratch file broken in some
            // unrelated way also produces; this is what makes the refusal that one page's shape.
            check(editor.undo(), "setup: the refused request taken back");
            check(editor.addWatermark(request("机密", 20, {0}), &error),
                  QStringLiteral("setup: page 1, whose content is an ordinary stream: %1")
                      .arg(error));
            const QString sibling = scratch.filePath(QStringLiteral("contents-sibling-ok.pdf"));
            error.clear();
            check(editor.saveAs(sibling, &error),
                  QStringLiteral("and the same document does write: %1").arg(error));
            const InkReport inked = readInk(sibling);
            check(inked.drawn == QVector<int>({0}),
                  QStringLiteral("on the page that was named: %1")
                      .arg(describeOrder(inked.drawn)));
            check(inked.pageCount == pagesBefore, "and nothing else about the file changed shape");
        }

        // ---- a page with no content stream at all ----
        //
        // The other end of the same function. Legal, and what a blank page looks like; the mark
        // still has to land on it. Skipping it instead is exactly the failure §26's first probe
        // measured from the other side -- a dialog that said 盖在 2 页上 over a file carrying one
        // page of ink -- and a page with nothing on it is where a "just leave that one alone"
        // shortcut would hide, because nothing visibly breaks.
        {
            const QString bare = scratch.filePath(QStringLiteral("contents-absent.pdf"));
            QString built;
            check(writeContentsShape(source, bare, 0, ContentsShape::Absent, &built),
                  QStringLiteral("setup: a file whose first page has no /Contents: %1").arg(built));
            DocumentEditor editor;
            check(editor.open(bare, QString(), &error), "setup: it opens");
            check(editor.addWatermark(request("机密", 20, {0, 1}), &error),
                  QStringLiteral("setup: the bare page and a normal one, in one request: %1")
                      .arg(error));
            const QString out = scratch.filePath(QStringLiteral("contents-absent-out.pdf"));
            error.clear();
            check(editor.saveAs(out, &error),
                  QStringLiteral("and both are inked, not one: %1").arg(error));
            const InkReport ink = readInk(out);
            check(ink.drawn == QVector<int>({0, 1}),
                  QStringLiteral("the page with no content of its own is in the list of pages "
                                 "that draw: %1").arg(describeOrder(ink.drawn)));
            check(ink.namedButNotDrawn.isEmpty(),
                  "and neither page merely names the bitmap without drawing it");
            check(ink.maskedImages == 1, "for the one bitmap the two pages share");
            check(ink.pageCount == pagesBefore, "and the page count is the one the dialog said");
        }

        // ---- 水印 beside the other six ----
        {
            const QString donor = scratch.filePath(QStringLiteral("ink-donor.pdf"));
            check(writeDonor(source, donor, pagesBefore, {4, 5, 6}, &error) == donor,
                  QStringLiteral("setup: a three-page donor: %1").arg(error));

            // A merged page has no number on the screen, so it cannot be named -- and unlike
            // 拆分, which has to refuse the whole document for that reason, 水印 can simply
            // leave those pages alone and still say exactly which pages it inked.
            DocumentEditor merged;
            check(merged.open(source, QString(), &error), "setup: the document opens");
            check(merged.mergeFile(donor, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: two pages in: %1").arg(error));
            check(merged.addWatermark(request("机密", 20, {0}), &error),
                  QStringLiteral("and a 水印 on the opened document's own page is accepted "
                                 "beside a merge: %1").arg(error));
            const QString mergedPath = scratch.filePath(QStringLiteral("merged-inked.pdf"));
            check(merged.saveAs(mergedPath, &error),
                  QStringLiteral("the merge and the ink ride one save: %1").arg(error));
            const InkReport ink = readInk(mergedPath);
            check(ink.pageCount == pagesBefore + 2,
                  QStringLiteral("setup: %1 pages out of the merge+水印").arg(ink.pageCount));
            check(ink.drawn == QVector<int>({0}),
                  "the pages that came in from the donor are not inked: they were never "
                  "nameable, and drawing something on them would be a promise nobody made");
            check(ink.maskedImages == 1, "one bitmap for the file, inked pages or none");

            // 加密 and 拆分 are the other two that say something about the bytes rather than
            // the tree, so the question is whether all four of these can be one save. The
            // probe measured it for one pair (question 5); this is the same claim with the
            // third one in, on the tool chain this program ships with.
            DocumentEditor all;
            check(all.open(source, QString(), &error), "setup: the document opens again");
            check(all.splitInto(15, &error), "setup: three pieces");
            check(all.addWatermark(request("机密", 20, {0, 16, 39}), &error), "setup: three 水印");
            const QString key = QStringLiteral("ink-key");
            DocumentEditor::Protection guard;
            guard.what = DocumentEditor::Protection::What::Encrypt;
            guard.password = key;
            guard.allowPrint = guard.allowExtract = guard.allowAssembly = true;
            guard.allowAnnotate = guard.allowFormFilling = guard.allowModifyOther = true;
            check(all.protectWith(guard, &error), "setup: and a 加密");
            const QString base = scratch.filePath(QStringLiteral("ink-cut.pdf"));
            check(all.saveAs(base, &error),
                  QStringLiteral("水印 + 拆分 + 加密 in one save writes: %1").arg(error));

            int inkedPages = 0;
            bool everyPieceLocked = true;
            for (int index = 0; index < all.pieceCount(); ++index) {
                const QString piecePath = DocumentEditor::piecePath(
                    base, index, all.pieceCount(), all.pieces().at(index));
                InkReport one;
                try {
                    QPDF open;
                    open.processFile(piecePath.toUtf8().constData(), "");
                    everyPieceLocked = false;
                } catch (const std::exception &) {
                    // refused without the口令, which is the claim
                }
                one = readInk(piecePath, key);
                inkedPages += one.drawn.size();
            }
            check(everyPieceLocked, "every piece of the split is encrypted");
            check(inkedPages == 3,
                  QStringLiteral("and the three inked pages are spread over the three pieces, "
                                 "once each: %1").arg(inkedPages));
        }

        // ---- what it costs ----
        {
            DocumentEditor editor;
            QElapsedTimer clock;
            clock.start();
            const QString every = scratch.filePath(QStringLiteral("timed-ink.pdf"));
            const bool done = editor.open(source, QString(), &error)
                && editor.addWatermark(request("机密", 20, everyPage(pagesBefore)), &error)
                && editor.saveAs(every, &error);
            note(QStringLiteral("open + 水印 on all %1 pages + write took %2 ms (%3 KB out)")
                     .arg(pagesBefore)
                     .arg(clock.elapsed())
                     .arg(done ? QFileInfo(every).size() / 1024 : 0));
            check(done, "and the timed save went through");
        }
    }

    // ---- and, when handed one, a document from outside the repository ----
    //
    // docs/perf.md keeps its real-book numbers the same way: a path given on the
    // command line, so the gate itself never depends on a file that is not in the
    // tree. Run as
    //   build-local-release\tests\xyzpdf_documenteditor_smoke.exe ^
    //       H:\pdf_test\VerilogHDL硬件描述语言.pdf
    // This is a measurement, not a check that may go red on somebody else's
    // machine: only the source-not-touched promise is asserted, because that is
    // the one this feature is not allowed to break on a file that cannot be
    // restored from the repository.
    if (argc > 1) {
        const QString real = QString::fromLocal8Bit(argv[1]);
        if (!QFile::exists(real)) {
            std::cerr << "FAIL: the file named on the command line is not there\n";
            ++failures;
        } else {
            const QString before = hashOfFile(real);
            DocumentEditor editor;
            QString error;
            QElapsedTimer clock;
            clock.start();
            bool done = editor.open(real, QString(), &error);
            const int realPages = done ? editor.pageCount() : 0;
            const QString realResult = scratch.filePath(QStringLiteral("real-result.pdf"));
            done = done && editor.deletePages({0, realPages / 2, realPages - 1}, &error)
                && editor.saveAs(realResult, &error);
            const qint64 taken = clock.elapsed();
            note(QStringLiteral("%1: %2 pages, %3 KB, open + delete + write took %4 ms")
                     .arg(QFileInfo(real).fileName())
                     .arg(realPages)
                     .arg(QFileInfo(real).size() / 1024)
                     .arg(taken));
            // §63 measured what the sidebar used to lose on this very file: 14 of 184 bookmarks.
            // A note rather than a check, because the number belongs to a file that is not in the
            // repository and another machine's book will disagree.
            QtPdfBackend realReader;
            if (realReader.load(real, QString()) == Document::Status::Ready) {
                const Outline realTree = realReader.outline();
                note(QStringLiteral("%1 holds %2 bookmarks, %3 of them at the top level")
                         .arg(QFileInfo(real).fileName())
                         .arg(realTree.count())
                         .arg(realTree.nodes.size()));
            }
            if (!done) {
                std::cerr << "FAIL: the real document could not be edited: "
                          << error.toStdString() << '\n';
                // Counted, not just printed. A path that exists but cannot be opened -- naming the
                // fixtures *folder* here is enough, and is what I did -- used to print this line and
                // still exit 0 under "all checks passed", so a reading nobody had to act on.
                ++failures;
            } else {
                check(pagesAsReadByTheReader(realResult) == realPages - 3,
                      "and the copy of the real document reads back three pages shorter");
            }
            check(hashOfFile(real) == before,
                  "and the document on disk outside the repository is byte for byte what it was");
        }
    }

    // ---- the sidebar's read path: a tree rather than the top level of one (§63) ----
    //
    // The two counting checks are written as a pair on purpose, because the two ways this went
    // wrong have the same symptom. Reading Qt's model as a flat list (which is what
    // qtpdfbackend.cpp used to do, on the strength of a comment claiming the model was flat) hands
    // back only the rows under the root -- 14 of a real book's 184 bookmarks. And Qt's Level role
    // counts from 0 while outlineFromRows assembles against levels counting from 1, so a tree that
    // *was* walked correctly still lands every chapter and every section at the top. Either half
    // alone leaves "the top level has two roots" green; only the total alongside it goes red.
    {
        const QString outlineFile = scratch.filePath(QStringLiteral("nested-outline.pdf"));
        QString outlineError;
        check(writeNestedOutlineSample(source, outlineFile, &outlineError),
              QStringLiteral("a bookmark tree can be written by hand: %1").arg(outlineError));
        QtPdfBackend withBookmarks;
        const bool opened = withBookmarks.load(outlineFile, QString()) == Document::Status::Ready;
        check(opened, "and the file carrying it opens in the reader");
        const Outline tree = opened ? withBookmarks.outline() : Outline();
        check(tree.count() == 5,
              "all five bookmarks are in the tree, not just the two the model lists under its root");
        check(tree.nodes.size() == 2, "and exactly two of them are at the top level");

        const OutlineNode *chapter = tree.nodes.isEmpty() ? nullptr : &tree.nodes.at(0);
        check(chapter && chapter->title == QStringLiteral("Chapter 1")
                  && chapter->children.size() == 2,
              "the first chapter holds its two sections");
        const OutlineNode *section =
            chapter && !chapter->children.isEmpty() ? &chapter->children.at(0) : nullptr;
        check(section && section->title == QStringLiteral("Section 1.1")
                  && section->children.size() == 1,
              "and the first of those holds a sub-section -- the third depth a root-only read never reached");
        const OutlineNode *sub =
            section && !section->children.isEmpty() ? &section->children.at(0) : nullptr;
        check(sub && sub->title == QStringLiteral("Sub 1.1.1") && sub->page == 4,
              "and three levels down the bookmark still names the page it was written against");
        check(tree.nodes.size() == 2 && tree.nodes.at(1).title == QStringLiteral("Chapter 2")
                  && tree.nodes.at(1).page == 10 && tree.nodes.at(1).children.isEmpty(),
              "the second chapter stays a root rather than being adopted by the first");
    }

    // ---- and the same tree written *out*, by the product ----
    //
    // §63 measured what the seven keys do when one of them is wrong; §64 fixed the way in; this is
    // the way out. The tree is written by DocumentEditor::writeOutline and then walked back by two
    // readers that have no knowledge of each other -- qpdf's outline helper and our own backend,
    // which is what the sidebar is built from. The two deliberate breakages below are what make the
    // counts evidence: a file whose items are all in one flat row would satisfy "5 titles" too.
    {
        const QString written = scratch.filePath(QStringLiteral("written-outline.pdf"));
        const QVector<OutlineRow> rows = {
            outlineRow(QStringLiteral("Chapter 1"), 1, 0),
            outlineRow(QStringLiteral("Section 1.1"), 2, 3),
            outlineRow(QStringLiteral("Sub 1.1.1"), 3, 4),
            outlineRow(QStringLiteral("Section 1.2"), 2, 6),
            outlineRow(QStringLiteral("Chapter 2"), 1, 10),
        };
        QString error;
        check(writeOutlineWithProduct(source, written, rows, &error),
              QStringLiteral("the product writes a five-item, three-deep outline: %1").arg(error));

        OutlineWalk walked;
        error.clear();
        check(walkOutlineFile(written, walked, &error),
              QStringLiteral("and qpdf can walk the file it wrote: %1").arg(error));
        check(walked.total == 5 && walked.top == 2,
              QStringLiteral("5 items with 2 under the root, not %1 with %2")
                  .arg(walked.total)
                  .arg(walked.top));
        const QStringList expected = {QStringLiteral("Chapter 1"), QStringLiteral("Section 1.1"),
                                      QStringLiteral("Sub 1.1.1"), QStringLiteral("Section 1.2"),
                                      QStringLiteral("Chapter 2")};
        check(walked.titles == expected,
              QStringLiteral("and depth-first, the order a sidebar draws them (%1)")
                  .arg(walked.titles.join(QStringLiteral(", "))));

        QtPdfBackend reader;
        check(reader.load(written, QString()) == Document::Status::Ready,
              "the written file opens in the reader");
        const Outline tree = reader.outline();
        check(tree.count() == 5 && tree.nodes.size() == 2,
              QStringLiteral("and the sidebar's own tree agrees: %1 items, %2 at the top")
                  .arg(tree.count())
                  .arg(tree.nodes.size()));
        check(!tree.nodes.isEmpty() && tree.nodes.at(0).children.size() == 2
                  && !tree.nodes.at(0).children.isEmpty()
                  && tree.nodes.at(0).children.at(0).children.size() == 1,
              "with the nesting intact two levels down, not a flat list of five");

        // A second write replaces the tree rather than appending to it -- that is what "the reader
        // undid their edit" has to mean for a bookmark list, and it is the one thing about this
        // writer that a reader would notice as a bug if it worked the other way.
        const QString rewritten = scratch.filePath(QStringLiteral("rewritten-outline.pdf"));
        const QVector<OutlineRow> twoRows = {outlineRow(QStringLiteral("Only one"), 1, 2),
                                             outlineRow(QStringLiteral("Only two"), 1, 5)};
        error.clear();
        check(writeOutlineWithProduct(written, rewritten, twoRows, &error),
              QStringLiteral("writing again over a file that already has an outline works: %1")
                  .arg(error));
        OutlineWalk second;
        error.clear();
        check(walkOutlineFile(rewritten, second, &error), "and that file walks back");
        check(second.total == 2 && second.top == 2 && !second.titles.contains(expected.at(2)),
              QStringLiteral("the old tree is gone rather than left behind (%1)")
                  .arg(second.titles.join(QStringLiteral(", "))));

        // Empty means "no bookmarks", and the file for that has no outline at all.
        const QString emptied = scratch.filePath(QStringLiteral("outline-removed.pdf"));
        error.clear();
        check(writeOutlineWithProduct(rewritten, emptied, QVector<OutlineRow>{}, &error),
              QStringLiteral("an empty list is accepted as removing them: %1").arg(error));
        OutlineWalk none;
        error.clear();
        check(walkOutlineFile(emptied, none, &error) && none.total == 0,
              "and nothing is left to walk");

        // A row naming a page the document does not have is refused with a sentence, because every
        // other dialog here numbers pages the same way and the reader checks them against the
        // status bar.
        error.clear();
        const QString beyond = scratch.filePath(QStringLiteral("outline-beyond.pdf"));
        check(!writeOutlineWithProduct(source, beyond,
                                       {outlineRow(QStringLiteral("Nowhere"), 1, 999)}, &error),
              "a bookmark naming a page that is not there is refused");
        // 1000 rather than 999: the sentence is for a reader, and every page number they have ever
        // seen in this program is 1-based on the way out.
        check(!error.isEmpty() && error.contains(QStringLiteral("1000")),
              QStringLiteral("and the sentence says which page: %1").arg(error));

        // ---- the two controls, as standing assertions ----
        //
        // §63 measured these on hand-built files; here they run against the file this writer
        // produced, which is the only version of the claim that can rot. One key out of a chain of
        // five is not a smaller outline -- it is a much smaller one, in the way the arrows say.
        const QString lostNext = scratch.filePath(QStringLiteral("outline-lost-next.pdf"));
        error.clear();
        check(breakOutlineKey(written, lostNext, QStringLiteral("first-next"), &error),
              QStringLiteral("the control can take the first item's /Next out: %1").arg(error));
        OutlineWalk afterNext;
        error.clear();
        const bool nextWalked = walkOutlineFile(lostNext, afterNext, &error);
        check(nextWalked, QStringLiteral("the file with a broken /Next still walks: %1").arg(error));
        // Measured, and it is not the number §63 printed: 4 rather than 1, because §63 counted
        // top-level items while this counts the whole tree. Chapter 1 and its three descendants are
        // still reachable from /First; everything past the missing /Next -- Chapter 2 -- is gone.
        // One broken link loses a span, which is the sentence the writer's design rests on.
        check(afterNext.total == 4 && afterNext.top == 1 && !afterNext.titles.contains(expected.at(4)),
              QStringLiteral("one broken /Next leaves 4 items under 1 root rather than 5 under 2 "
                             "(it left %1 under %2)")
                  .arg(afterNext.total)
                  .arg(afterNext.top));

        const QString lostEnds = scratch.filePath(QStringLiteral("outline-lost-ends.pdf"));
        error.clear();
        check(breakOutlineKey(written, lostEnds, QStringLiteral("root-ends"), &error),
              QStringLiteral("the control can take the root's /First and /Last out: %1").arg(error));
        OutlineWalk afterEnds;
        error.clear();
        const bool endsWalked = walkOutlineFile(lostEnds, afterEnds, &error);
        check(endsWalked, QStringLiteral("the file with no ends still walks: %1").arg(error));
        check(afterEnds.total == 0 && afterEnds.top == 0,
              "and a root that names no /First /Last loses the entire outline, not one item");
    }

    // ---- the tree a reader asks for, through the pending list and out through a save (§69) ----
    //
    // §68 handed DocumentEditor::writeOutline a tree and read the bytes back. This is the seam above
    // it, where the two things that only exist there can go wrong: a bookmark request goes through
    // the same pending list as a 删页, so 撤销 has to take it back before anything reaches disk; and
    // it has to survive pages moving under it, which is why an OutlineEdit holds a page *reference*
    // rather than the number the reader typed. Both are read off what our own reader hands the
    // sidebar, because "the file has an outline" was never the claim -- reaching the page the reader
    // pointed at is.
    {
        DocumentEditor nothing;
        QString error;
        const QVector<OutlineRow> oneRow = {outlineRow(QStringLiteral("Only"), 1, 0)};
        const bool refused = nothing.setOutline(oneRow, &error);
        check(!refused && !error.isEmpty(),
              "an editor that has opened nothing refuses a bookmark request, with a sentence");

        DocumentEditor asked;
        const bool opened = asked.open(source, QString(), &error);
        check(opened, QStringLiteral("setup: the editor opens the source: %1").arg(error));

        // Refused before anything is recorded, and said with the numbers the reader has: the level
        // as it was written, the page 1-based on the way out like every other sentence here.
        error.clear();
        const bool badLevel = asked.setOutline({outlineRow(QStringLiteral("Low"), 0, 1)}, &error);
        check(!badLevel && error.contains(QStringLiteral("Low"))
                  && error.contains(QStringLiteral("层级")),
              QStringLiteral("a bookmark at level 0 is refused, naming it and its level: %1")
                  .arg(error));
        error.clear();
        const bool badPage = asked.setOutline({outlineRow(QStringLiteral("Far"), 1, pagesBefore)},
                                              &error);
        check(!badPage && error.contains(QStringLiteral("Far"))
                  && error.contains(QString::number(pagesBefore + 1)),
              QStringLiteral("a bookmark naming page %1 of a %2-page document is refused, and the "
                             "sentence says that page: %3")
                  .arg(pagesBefore + 1)
                  .arg(pagesBefore)
                  .arg(error));
        check(!asked.isDirty() && asked.pendingCount() == 0,
              "and neither refusal put anything in the pending list");

        // Five bookmarks, three depths, one of them landing on the page the next block deletes.
        const QVector<OutlineRow> five = {
            outlineRow(QStringLiteral("Root A"), 1, 0),
            outlineRow(QStringLiteral("Child A1"), 2, 2),
            outlineRow(QStringLiteral("Grandchild A1a"), 3, 4),
            outlineRow(QStringLiteral("Child A2"), 2, 6),
            outlineRow(QStringLiteral("Root B"), 1, 10),
        };
        const QStringList fiveTitles = {QStringLiteral("Root A"), QStringLiteral("Child A1"),
                                        QStringLiteral("Grandchild A1a"), QStringLiteral("Child A2"),
                                        QStringLiteral("Root B")};

        error.clear();
        const bool recorded = asked.setOutline(five, &error);
        check(recorded, QStringLiteral("asking for a bookmark tree is recorded: %1").arg(error));
        check(asked.isDirty() && asked.pendingCount() == 1,
              "and exactly one operation is pending, which is what makes the tab say it has work");
        check(asked.outlineAsked() && asked.outlineToWrite().size() == 5,
              "and a save would write this tree");
        // Spelled out in full, for the same reason 删除 2 页 is: `%n` is filled by the lookup, so a
        // row that read 设置书签（%n 条） on screen would still satisfy every check above.
        const QString undoRow = asked.undoDescription();
        check(undoRow == QStringLiteral("设置书签（5 条）"),
              QStringLiteral("the undo row names its own count (got \"%1\")").arg(undoRow));

        error.clear();
        const bool twice = asked.setOutline(five, &error);
        check(!twice && asked.pendingCount() == 1 && asked.canUndo(),
              "asking twice for the identical tree is refused, not recorded as a second change");
        check(!error.isEmpty(), "and the refusal says why");

        const QString asAsked = scratch.filePath(QStringLiteral("outline-as-asked.pdf"));
        const QString sourceBefore = hashOfFile(source);
        error.clear();
        const bool wrote = asked.saveAs(asAsked, &error);
        check(wrote, QStringLiteral("and the save writes it: %1").arg(error));
        check(hashOfFile(source) == sourceBefore,
              "the document on disk is byte for byte what it was");

        OutlineWalk walked;
        error.clear();
        const bool walkedBack = walkOutlineFile(asAsked, walked, &error);
        check(walkedBack, QStringLiteral("qpdf walks the file a reader asked for: %1").arg(error));
        check(walked.total == 5 && walked.top == 2,
              QStringLiteral("5 items with 2 under the root, not %1 with %2")
                  .arg(walked.total)
                  .arg(walked.top));
        check(walked.titles == fiveTitles,
              QStringLiteral("depth-first, in the order a sidebar draws them (%1)")
                  .arg(walked.titles.join(QStringLiteral(", "))));

        QtPdfBackend pane;
        check(pane.load(asAsked, QString()) == Document::Status::Ready,
              "the file a reader asked for opens in the reader");
        const QString asShown = bookmarksAsText(bookmarksAsTheSidebarSees(pane.outline()));
        check(asShown == QStringLiteral("Root A@0, Child A1@2, Grandchild A1a@4, Child A2@6, Root B@10"),
              QStringLiteral("and the sidebar reaches the page each bookmark was asked for (%1)")
                  .arg(asShown));

        // ---- taking it back before anything was written ----
        check(asked.undo(), "undo takes the bookmark request back");
        check(!asked.outlineAsked() && !asked.isDirty(),
              "and the bookmark question now answers \"nobody asked\", not \"an empty tree\"");
        const QString undoneFile = scratch.filePath(QStringLiteral("outline-undone.pdf"));
        error.clear();
        const bool wroteNone = asked.saveAs(undoneFile, &error);
        check(wroteNone, QStringLiteral("the save after the undo still writes the document: %1")
                             .arg(error));
        OutlineWalk none;
        error.clear();
        const bool noneWalked = walkOutlineFile(undoneFile, none, &error);
        check(noneWalked && none.total == 0 && none.top == 0,
              QStringLiteral("and it holds no outline at all -- not an empty root left behind (%1 "
                             "items)")
                  .arg(none.total));
        check(asked.redo(), "redo puts the request back");
        const QString redoneFile = scratch.filePath(QStringLiteral("outline-redone.pdf"));
        error.clear();
        const bool wroteRedo = asked.saveAs(redoneFile, &error);
        OutlineWalk redone;
        error.clear();
        const bool redoWalked = walkOutlineFile(redoneFile, redone, &error);
        check(wroteRedo && redoWalked && redone.total == 5,
              QStringLiteral("and the same save writes the tree again once the list is put back (%1 "
                             "items)")
                  .arg(redone.total));

        // ---- a page that goes away between the asking and the writing ----
        //
        // The whole reason OutlineEdit stores a reference. The reader points five bookmarks at five
        // pages, then takes one of those pages out; what has to come back is the four that still
        // have pages, on the pages they were pointed at, and the fifth still there with no target.
        {
            DocumentEditor moved;
            error.clear();
            const bool openedMoved = moved.open(source, QString(), &error);
            check(openedMoved, QStringLiteral("setup: a fresh editor on the same document: %1")
                                   .arg(error));
            const bool marked = moved.setOutline(five, &error);
            check(marked, QStringLiteral("the tree is asked for first: %1").arg(error));
            const QString afterDelete = scratch.filePath(QStringLiteral("outline-after-delete.pdf"));
            error.clear();
            const bool deleted = moved.deletePages({2}, &error);
            const bool movedWrote = deleted && moved.saveAs(afterDelete, &error);
            check(movedWrote, QStringLiteral("then a page is taken out and the file written: %1")
                                  .arg(error));
            check(pagesAsReadByTheReader(afterDelete) == pagesBefore - 1,
                  "setup: that save really did take a page out");
            QtPdfBackend movedReader;
            check(movedReader.load(afterDelete, QString()) == Document::Status::Ready,
                  "and the file opens");
            const QVector<BookmarkRead> seen = bookmarksAsTheSidebarSees(movedReader.outline());
            const QString seenText = bookmarksAsText(seen);
            check(seen.size() == 5,
                  QStringLiteral("all five bookmarks survive the page that went: %1 of them are in "
                                 "the file")
                      .arg(seen.size()));
            // The one on the deleted page does not borrow its neighbour's page: a bookmark whose
            // target is gone is a heading, which is what outline.h already says to draw.
            check(seenText
                      == QStringLiteral(
                             "Root A@0, Child A1@-1, Grandchild A1a@3, Child A2@5, Root B@9"),
                  QStringLiteral("each bookmark reached the page it was pointed at, and nothing "
                                 "kept the stale number (read: %1)")
                      .arg(seenText));
        }

        // ---- a bookmark that names no page at all ----
        {
            const QString headingFile = scratch.filePath(QStringLiteral("outline-heading.pdf"));
            DocumentEditor heading;
            error.clear();
            const bool openedHeading = heading.open(source, QString(), &error);
            check(openedHeading, QStringLiteral("setup: the document for a bookmark with no page: %1")
                                     .arg(error));
            const bool askedHeading = heading.setOutline(
                {outlineRow(QStringLiteral("Part One"), 1, 0),
                 outlineRow(QStringLiteral("Notes"), 2, -1)},
                &error);
            check(askedHeading, QStringLiteral("a bookmark with no page is accepted: %1").arg(error));
            const bool wroteHeading = heading.saveAs(headingFile, &error);
            check(wroteHeading, QStringLiteral("and it writes: %1").arg(error));
            OutlineWalk headingWalk;
            error.clear();
            const bool headingBack = walkOutlineFile(headingFile, headingWalk, &error);
            check(headingBack && headingWalk.total == 2,
                  QStringLiteral("qpdf walks both items, the one with no destination included (%1)")
                      .arg(headingWalk.total));
            QtPdfBackend headingReader;
            check(headingReader.load(headingFile, QString()) == Document::Status::Ready,
                  "and the file opens");
            const QString headingText = bookmarksAsText(bookmarksAsTheSidebarSees(headingReader.outline()));
            check(headingText == QStringLiteral("Part One@0, Notes@-1"),
                  QStringLiteral("and the one with no page still has no page on the way back, "
                                 "rather than becoming page 1 (%1)")
                      .arg(headingText));
        }

        // ---- the counterfactual: not asked for is not rewritten ----
        //
        // §63 measured this shape on a hand-built file and found the two readers disagree about a
        // bookmark whose page has gone: qpdf still resolves the object, our own reader says it is
        // not in the document. Pinned here because it is what a save that was never asked about
        // bookmarks does -- the tree comes through as the document had it -- and because that is a
        // leak worth naming, not a behaviour to describe as correct.
        {
            const QString handwritten = scratch.filePath(QStringLiteral("handwritten-outline.pdf"));
            error.clear();
            const bool built = writeNestedOutlineSample(source, handwritten, &error);
            check(built, QStringLiteral("setup: a file whose outline this test did not write: %1")
                             .arg(error));
            const QString carried = scratch.filePath(QStringLiteral("outline-carried.pdf"));
            DocumentEditor untouched;
            error.clear();
            const bool openedCarried = untouched.open(handwritten, QString(), &error);
            check(openedCarried, QStringLiteral("and it opens in the editor: %1").arg(error));
            check(!untouched.outlineAsked(), "and nothing was asked of the bookmarks");
            const bool deletedCarried = untouched.deletePages({0}, &error);
            const bool carriedWrote = deletedCarried && untouched.saveAs(carried, &error);
            check(carriedWrote, QStringLiteral("a page is taken out, with no bookmark request: %1")
                                    .arg(error));
            OutlineWalk still;
            error.clear();
            const bool carriedWalked = walkOutlineFile(carried, still, &error);
            check(carriedWalked && still.total == 5,
                  QStringLiteral("the document's own tree comes through untouched, all %1 items")
                      .arg(still.total));
            QtPdfBackend carriedReader;
            check(carriedReader.load(carried, QString()) == Document::Status::Ready,
                  "and the copy opens in the reader");
            const QString carriedText = bookmarksAsText(bookmarksAsTheSidebarSees(carriedReader.outline()));
            check(carriedText
                      == QStringLiteral(
                             "Chapter 1@-1, Section 1.1@2, Sub 1.1.1@3, Section 1.2@5, Chapter 2@9"),
                  QStringLiteral("and what the sidebar makes of it: the bookmark whose page was "
                                 "deleted is still in the file and reaches nothing, while the other "
                                 "four follow their pages (%1)")
                      .arg(carriedText));
        }

        // ---- §75: does a bookmark naming a spot *inside* a page survive the round trip? ----
        //
        // §71 recorded this as an inferred consequence and never tested it. The inference holds, and
        // the test adds something the inference did not have: which of the two candidate fixes would
        // actually work. The read path asks Qt's model for Title and Page and nothing else
        // (qtpdfbackend.cpp:527-534), and the writer emits /Fit for every row
        // (documenteditor.cpp:713-719) -- so both halves are pinned below. The second is a KNOWN GAP
        // in §47's sense: it says what the product does today, not what it should do, so a real fix
        // reddens it on purpose.
        {
            const QString positional = scratch.filePath(QStringLiteral("positional-outline.pdf"));
            error.clear();
            check(writePositionalOutlineSample(source, positional, &error),
                  QStringLiteral("setup: a file whose bookmarks name a spot inside a page: %1")
                      .arg(error));
            OutlineWalk before;
            error.clear();
            check(walkOutlineFile(positional, before, &error) && before.total == 4,
                  QStringLiteral("and all four items are readable from the file: %1")
                      .arg(before.total));
            check(before.destinations.join(QStringLiteral(", "))
                      == QStringLiteral("[/XYZ 0 700 0], [/FitH 650], [/Fit], none"),
                  QStringLiteral("the four destination shapes, as written: %1")
                      .arg(before.destinations.join(QStringLiteral(", "))));

            // What Qt's own model sees, because that decides the cost of a fix. It reports a
            // position for /XYZ and none for /FitH -- and outlineprobe measured that a real book's
            // 184 bookmarks are all /FitH. So "just ask the model for its Location role" would
            // rescue one row out of four here, and nothing in that book.
            QPdfDocument qtDocument;
            check(qtDocument.load(positional) == QPdfDocument::Error::None,
                  "and Qt opens the same file");
            QPdfBookmarkModel qtBookmarks;
            qtBookmarks.setDocument(&qtDocument);
            const auto qtLocationOf = [&qtBookmarks](int row) {
                const QModelIndex index = qtBookmarks.index(row, 0, QModelIndex());
                if (!index.isValid())
                    return QPointF(-1, -1);
                return qtBookmarks.data(index, static_cast<int>(QPdfBookmarkModel::Role::Location))
                    .toPointF();
            };
            const QPointF xyzSeen = qtLocationOf(0);
            const QPointF fitHorizontalSeen = qtLocationOf(1);
            check(xyzSeen.y() > 0.0,
                  QStringLiteral("Qt reports the /XYZ bookmark's position at (%1,%2) -- in "
                                 "top-left origin, so a writer has to convert it back")
                      .arg(xyzSeen.x()).arg(xyzSeen.y()));
            check(fitHorizontalSeen.isNull(),
                  QStringLiteral("and no position at all for the /FitH one, at (%1,%2) -- the "
                                 "shape real books use")
                      .arg(fitHorizontalSeen.x()).arg(fitHorizontalSeen.y()));

            QtPdfBackend ours;
            check(ours.load(positional, QString()) == Document::Status::Ready,
                  "our reader opens it too");
            const QVector<OutlineRow> asSeen = outlineRowsOf(ours.outline());
            check(asSeen.size() == 4,
                  QStringLiteral("four rows reach the flat list the box would show (%1)")
                      .arg(asSeen.size()));
            check(asSeen.at(0).location.isNull() && asSeen.at(1).location.isNull(),
                  "KNOWN GAP §75, the cause: our read path hands the sidebar no position for "
                  "either of them, though the file has both and Qt reports the first");

            DocumentEditor flattener;
            error.clear();
            check(flattener.open(positional, QString(), &error)
                      && flattener.setOutline(asSeen, &error),
                  QStringLiteral("setup: asking for the tree exactly as read is accepted: %1")
                      .arg(error));
            const QString afterPath = scratch.filePath(QStringLiteral("positional-after.pdf"));
            error.clear();
            check(flattener.saveAs(afterPath, &error),
                  QStringLiteral("setup: and that request is written: %1").arg(error));
            OutlineWalk after;
            error.clear();
            check(walkOutlineFile(afterPath, after, &error) && after.total == 4,
                  QStringLiteral("the copy still holds all four bookmarks: %1").arg(after.total));
            check(after.destinations.join(QStringLiteral(", "))
                      == QStringLiteral("[/Fit], [/Fit], [/Fit], none"),
                  QStringLiteral("KNOWN GAP §75, the effect: every position is gone -- both the "
                                 "/XYZ and the /FitH came out as /Fit, so a reader who renames one "
                                 "bookmark moves all the others to the top of their pages: %1")
                      .arg(after.destinations.join(QStringLiteral(", "))));

            // §80 turns that same fact into the number 编辑书签 says before the press, and pins the
            // number at both ends: two of the four carry a position as written, none carry one after
            // a save through the box. The pair is what makes the check mean something -- a counter
            // that answered 0 for every file would satisfy the second reading alone (§67's shape of a
            // vacuous count), and the first reading is also what proves the count is not being taken
            // from the flat list, which has already lost the difference.
            DocumentEditor carrying;
            error.clear();
            check(carrying.open(positional, QString(), &error),
                  QStringLiteral("setup: the file as written opens for the count: %1").arg(error));
            check(carrying.sourcePositionalBookmarks() == 2,
                  QStringLiteral("two of the four name a place inside a page -- /XYZ and /FitH, "
                                 "not /Fit and not the heading: %1")
                      .arg(carrying.sourcePositionalBookmarks()));
            DocumentEditor flattened;
            error.clear();
            check(flattened.open(afterPath, QString(), &error),
                  QStringLiteral("setup: the saved copy opens for the count: %1").arg(error));
            check(flattened.sourcePositionalBookmarks() == 0,
                  QStringLiteral("and after one save through the box none do, which is the whole of "
                                 "what the sentence exists to say: %1")
                      .arg(flattened.sourcePositionalBookmarks()));
        }

        // ---- under a split, where each piece is a separate document ----
        //
        // The bookmark request is one, the files are several, and each of them gets the tree
        // resolved against *its own* pages -- which is what outlineRowsFor() being called per piece
        // buys. What the two reads below also pin is the shape of the leak that comes with it: a
        // piece carries the whole tree, with the pages belonging to the other piece reduced to
        // headings. Nothing is dropped, and nothing is filtered either. docs/store-submission.md
        // §69 records that as a decision rather than an accident.
        {
            const int pieceSize = pagesBefore / 2;
            DocumentEditor apart;
            error.clear();
            const bool openedSplit = apart.open(source, QString(), &error);
            check(openedSplit, QStringLiteral("setup: the document to split: %1").arg(error));
            const QVector<OutlineRow> spread = {
                outlineRow(QStringLiteral("Front"), 1, 0),
                outlineRow(QStringLiteral("Front child"), 2, 2),
                outlineRow(QStringLiteral("Back"), 1, pieceSize),
                outlineRow(QStringLiteral("Back child"), 2, pieceSize + 5),
                outlineRow(QStringLiteral("Last"), 2, pagesBefore - 1),
            };
            const bool markedApart = apart.setOutline(spread, &error);
            check(markedApart, QStringLiteral("a tree spread over both halves is asked for: %1")
                                   .arg(error));
            check(apart.splitInto(pieceSize, &error), "and the document is told to split in two");
            const QString splitPath = scratch.filePath(QStringLiteral("outline-split.pdf"));
            QStringList pieces;
            error.clear();
            const bool splitWrote = apart.saveAs(splitPath, &error, &pieces);
            check(splitWrote && pieces.size() == 2,
                  QStringLiteral("and both pieces are written: %1").arg(error));
            if (pieces.size() == 2) {
                const QString firstPiece = bookmarksShownInFile(pieces.at(0));
                check(firstPiece == QStringLiteral(
                          "Front@0, Front child@2, Back@-1, Back child@-1, Last@-1"),
                      QStringLiteral("the first piece holds all five bookmarks, three of them as "
                                     "headings pointing at nothing it has (%1)")
                          .arg(firstPiece));
                const QString secondPiece = bookmarksShownInFile(pieces.at(1));
                check(secondPiece == QStringLiteral(
                          "Front@-1, Front child@-1, Back@0, Back child@5, Last@19"),
                      QStringLiteral("and the second resolves the same request against its own "
                                     "pages: the pages renumber from its first, the others are "
                                     "headings (%1)")
                          .arg(secondPiece));
            } else {
                check(false, "the split produced two files to read");
            }
        }
    }

    // ---- which numbering a bookmark's page number is in (§70, asked before any dialog exists) ----
    //
    // §69 stored `live.at(row.page)` and its comment claimed that is "the numbering every other
    // dialog here uses". It is not: the six siblings read a typed number as a page of the file
    // **as opened** (`live.contains({0, page})` in rotatePages/deletePages, `anchor >= originalPages`
    // in movePages), while `live.at()` is a position in the list the pending edits have shrunk.
    // Every §69 check passed because each of them asked for the bookmarks on a fresh editor, where
    // the two coincide. The screen decides: an edit here is only *recorded*, never applied, so the
    // page a reader can check a typed number against is the one in the file they opened -- and this
    // block asks for bookmarks *after* a 删页, which is the only order that can tell them apart.
    {
        DocumentEditor after;
        QString error;
        const bool openedAfter = after.open(source, QString(), &error);
        check(openedAfter, QStringLiteral("setup: the editor opens the source: %1").arg(error));
        check(after.deletePages({2}, &error),
              QStringLiteral("setup: an earlier 删页 takes the third page out: %1").arg(error));
        check(after.pageCount() == pagesBefore - 1,
              "setup: and the list a save will cut is one page short");

        // The reader's 「第 6 页」 is index 5 of the file on screen. After that delete it sits at
        // position 4 of what gets written; read as a position it would mean index 6 -- one page
        // further along than the number the reader typed, with no sentence anywhere saying so.
        const bool askedAfter = after.setOutline({outlineRow(QStringLiteral("Sixth"), 1, 5)}, &error);
        check(askedAfter, QStringLiteral("a bookmark naming page 6 is accepted after a delete: %1")
                              .arg(error));
        const QString numberingFile = scratch.filePath(QStringLiteral("outline-numbering.pdf"));
        error.clear();
        const bool afterWrote = after.saveAs(numberingFile, &error);
        check(afterWrote, QStringLiteral("and the save writes it: %1").arg(error));
        const QString numberingRead = bookmarksShownInFile(numberingFile);
        check(numberingRead == QStringLiteral("Sixth@4"),
              QStringLiteral("the bookmark lands on the page the reader counted on screen -- index "
                             "5 of the opened file, which is the 5th page of what was written (%1)")
                  .arg(numberingRead));

        // A number whose page an earlier 删页 took out is not refused: the model's existing answer
        // to a bookmark with no page is a heading, and that is what §69 pinned for a page lost
        // *after* the ask. What it must not do is borrow the neighbour's page. (The dialog owes the
        // reader that sentence before the press, not after -- recorded as §71's.)
        error.clear();
        const bool namedGone = after.setOutline({outlineRow(QStringLiteral("Third"), 1, 2)}, &error);
        check(namedGone, QStringLiteral("naming the page that is already gone is accepted: %1")
                             .arg(error));
        const QString goneFile = scratch.filePath(QStringLiteral("outline-naming-gone.pdf"));
        error.clear();
        const bool goneWrote = after.saveAs(goneFile, &error);
        const QString goneRead = bookmarksShownInFile(goneFile);
        check(goneWrote && goneRead == QStringLiteral("Third@-1"),
              QStringLiteral("and that bookmark comes back with no page rather than taking the one "
                             "next to it (%1)")
                  .arg(goneRead));

        // Validity runs against the file the reader is counting on, not the shortened list: page 40
        // of a 40-page document has to stay typeable after one page is taken out, or the last page
        // on the screen would be unreachable for a reason nobody could see.
        error.clear();
        const bool lastPage = after.setOutline(
            {outlineRow(QStringLiteral("Last page"), 1, pagesBefore - 1)}, &error);
        check(lastPage, QStringLiteral("the opened file's last page is still addressable after a "
                                       "delete: %1")
                            .arg(error));
        const QString lastFile = scratch.filePath(QStringLiteral("outline-last-page.pdf"));
        error.clear();
        const bool lastWrote = after.saveAs(lastFile, &error);
        const QString lastRead = bookmarksShownInFile(lastFile);
        check(lastWrote && lastRead == QStringLiteral("Last page@%1").arg(pagesBefore - 2),
              QStringLiteral("and it lands on the last page of what was written (%1)")
                  .arg(lastRead));

        // And the boundary is that same file: a page the opened document never had is refused, with
        // the count the reader has on their status bar -- not the count of pages still pending.
        error.clear();
        const bool beyond = after.setOutline({outlineRow(QStringLiteral("Beyond"), 1, pagesBefore)},
                                            &error);
        check(!beyond && error.contains(QString::number(pagesBefore + 1))
                            && error.contains(QString::number(pagesBefore)),
              QStringLiteral("a page past the opened file's last is refused, naming that file's "
                             "page count: %1")
                  .arg(error));
        check(after.pendingCount() == 4,
              QStringLiteral("setup: the four accepted requests are the only things in the list (%1)")
                  .arg(after.pendingCount()));
    }

    // A generator, not a test. Since §46 this suite has been saying "no document with a form field
    // exists on this machine", which is true of the fixtures and false as a law: the shapes below
    // can be built, and §54 needed one of them in front of a real window. Setting
    // XYZPDF_FORM_SAMPLE_DIR=<folder> writes them there; the gate never sets it, so a normal run
    // still writes nothing outside its own scratch directory -- and a folder naming `fixtures` is
    // refused outright, because these files are meant to be opened by eye and would otherwise be
    // globbed by whatever test walks that directory next.
    {
        const QByteArray where = qgetenv("XYZPDF_FORM_SAMPLE_DIR");
        if (!where.isEmpty()) {
            const QString dir = QString::fromLocal8Bit(where);
            if (dir.contains(QStringLiteral("fixtures"), Qt::CaseInsensitive)) {
                std::cerr << "FAIL: XYZPDF_FORM_SAMPLE_DIR names the fixture folder; the samples "
                             "are for a window, not for the tests that glob that directory\n";
                ++failures;
            } else {
                QDir().mkpath(dir);
                QString e;
                const QString plain = QDir(dir).filePath(QStringLiteral("plain.pdf"));
                const QString flat = QDir(dir).filePath(QStringLiteral("field-flat.pdf"));
                const QString spread = QDir(dir).filePath(QStringLiteral("field-spread.pdf"));
                const QString dangling = QDir(dir).filePath(QStringLiteral("field-dangling.pdf"));
                const QString xfaSample = QDir(dir).filePath(QStringLiteral("field-xfa.pdf"));
                const QString drawn = QDir(dir).filePath(QStringLiteral("field-appearance.pdf"));
                const QString directive = QDir(dir).filePath(QStringLiteral("field-directive.pdf"));
                QFile::remove(plain);
                QFile::remove(flat);
                QFile::remove(spread);
                QFile::remove(dangling);
                QFile::remove(xfaSample);
                QFile::remove(drawn);
                QFile::remove(directive);
                if (QFile::copy(fixture, plain))
                    std::cout << "SAMPLE plain: " << plain.toStdString() << '\n';
                // §81: the three annotation shapes go out beside them, on the same page `plain.pdf`
                // is -- the screen reading is a difference between two pictures, and it is only
                // evidence if the two pictures differ by one annotation and nothing else. The
                // baseline is therefore plain.pdf itself, not a fourth file.
                for (const QString &kind : {QStringLiteral("highlight"), QStringLiteral("square"),
                                            QStringLiteral("note")}) {
                    const QString out
                        = QDir(dir).filePath(QStringLiteral("markup-%1.pdf").arg(kind));
                    QFile::remove(out);
                    e.clear();
                    check(!writeMarkupSample(fixture, out, kind, &e).isEmpty(),
                          QStringLiteral("sample: markup-%1 is written for the window to open: %2")
                              .arg(kind)
                              .arg(e));
                    std::cout << "SAMPLE markup-" << kind.toStdString() << ": "
                              << out.toStdString() << '\n';
                }
                // §89: the same generator writes the one control the screen route had never had. A
                // widget that paints nothing is a claim about the annotation layer, and it is only
                // worth stating if this window would have noticed a mark on that page at all. The
                // rect goes into the page's own content stream, at the area markup-highlight.pdf
                // claims -- so the screen table has a row that must differ, next to the row that
                // must not, in the same crop geometry (sec.82's rule about a row that reads 0).
                {
                    const QString painted = QDir(dir).filePath(QStringLiteral("painted-rect.pdf"));
                    QFile::remove(painted);
                    e.clear();
                    check(!writePaintedRectSample(fixture, painted, &e).isEmpty(),
                          QStringLiteral("sample: painted-rect is written for the window to open: %1")
                              .arg(e));
                    std::cout << "SAMPLE painted-rect: " << painted.toStdString() << '\n';
                }
                if (!writeFieldCopy(fixture, flat, QStringLiteral("sampleField"),
                                    QStringLiteral("typed on page 1"), &e)
                         .isEmpty()) {
                    std::cout << "SAMPLE flat: " << flat.toStdString() << '\n';
                    // Geometry for the screen pass (sec.54): the box is drawn at 60,600 to 260,630
                    // in points, so without the page's own numbers "nothing was drawn" would be a
                    // statement about a region that might have been below the fold.
                    QPDF probe;
                    probe.processFile(flat.toUtf8().constData());
                    QPDFPageObjectHelper firstPage(
                        QPDFPageDocumentHelper(probe).getAllPages().at(0));
                    const QPDFObjectHandle box = firstPage.getMediaBox();
                    if (box.isArray() && box.getArrayNItems() == 4) {
                        std::cout << "SAMPLE page1 mediabox: "
                                  << box.getArrayItem(0).getNumericValue() << ' '
                                  << box.getArrayItem(1).getNumericValue() << ' '
                                  << box.getArrayItem(2).getNumericValue() << ' '
                                  << box.getArrayItem(3).getNumericValue()
                                  << " ; the field box sits at 60,600 to 260,630 points\n";
                    } else {
                        std::cout << "SAMPLE page1 mediabox: (none readable)\n";
                    }
                } else {
                    std::cerr << "FAIL: sample flat: " << e.toStdString() << '\n';
                }
                if (!writeSpreadFieldCopy(fixture, spread, QStringLiteral("sampleSpread"),
                                          QStringLiteral("typed across two pages"), 0, 20, &e)
                         .isEmpty())
                    std::cout << "SAMPLE spread: " << spread.toStdString() << '\n';
                else
                    std::cerr << "FAIL: sample spread: " << e.toStdString() << '\n';
                // §57's shape: a widget that carries its own /AP appearance stream, which is what a
                // field looks like after anyone has filled it in with a real viewer.
                if (!writeFieldWithAppearanceCopy(fixture, drawn, QStringLiteral("sampleField"),
                                                  QStringLiteral("typed on page 1"), 0, &e)
                         .isEmpty())
                    std::cout << "SAMPLE drawn: " << drawn.toStdString() << " appearances="
                              << appearancesWithPaint(drawn, QStringLiteral("APPEARS-HERE")) << '\n';
                else
                    std::cerr << "FAIL: sample drawn: " << e.toStdString() << '\n';
                // §58's shape: the same field with the document-level instructions a real form
                // carries around it -- /DA says what to draw text with, /DR offers the font, /Q the
                // alignment, /NeedAppearances tells the viewer to build the drawings itself.
                if (!writeDirectiveFieldCopy(fixture, directive, QStringLiteral("sampleField"),
                                             QStringLiteral("typed on page 1"),
                                             QStringLiteral("/Helv"), QStringLiteral("/Helvetica"),
                                             1, true, true, &e)
                             .isEmpty()) {
                    const DirectiveReport sample = readDirectives(directive);
                    std::cout << "SAMPLE directive: " << directive.toStdString()
                              << " docDA=" << sample.formHasDA << " docDR=" << sample.formHasDR
                              << " needAppearances=" << sample.needAppearances
                              << " drawings=" << sample.drawings << " fields=" << sample.terminals
                              << " fonts=[" << sample.formFonts.join(QStringLiteral(", ")).toStdString()
                              << "] handed=["
                              << sample.givenDirectives.join(QStringLiteral(", ")).toStdString()
                              << "]\n";
                } else {
                    std::cerr << "FAIL: sample directive: " << e.toStdString() << '\n';
                }
                // The §55 shape as a file a window and a byte dump can both look at.
                if (!writeXfaFieldCopy(fixture, xfaSample, QStringLiteral("xfaField"),
                                       QStringLiteral("typed on page 1"), &e)
                         .isEmpty()) {
                    const XfaReport checkPacket = readXfa(xfaSample);
                    std::cout << "SAMPLE xfa: " << xfaSample.toStdString() << " present="
                              << checkPacket.present << " pairs=" << checkPacket.pairs << " bytes="
                              << checkPacket.text.toUtf8().size() << '\n';
                } else {
                    std::cerr << "FAIL: sample xfa: " << e.toStdString() << '\n';
                }
                // The §51 shape as something a window can be pointed at: the page is gone, and
                // this document still registers the field that lived on it.
                DocumentEditor out;
                if (!writeFieldCopy(fixture, dangling, QStringLiteral("sampleField"),
                                    QStringLiteral("typed on page 1"), &e).isEmpty()
                    && out.open(dangling, QString(), &e) && out.deletePages({0}, &e)
                    && out.saveAs(dangling + QStringLiteral("-rewritten.pdf"), &e)) {
                    QFile::remove(dangling);
                    QFile::rename(dangling + QStringLiteral("-rewritten.pdf"), dangling);
                    const FieldTreeReport still = readFieldTree(dangling);
                    std::cout << "SAMPLE dangling: " << dangling.toStdString() << " terminals="
                              << still.terminals << '\n';
                } else {
                    std::cerr << "FAIL: sample dangling: " << e.toStdString() << '\n';
                }
            }
        }
    }

    // ---- does this reader draw the annotation layer at all? (§81, the file half) ----
    //
    // Three pages, each carrying exactly one annotation and none of them carrying an /AP appearance
    // stream. PDF 2.0 §12.5.3 is explicit that a viewer has to generate the appearance of an
    // annotation that has none -- a highlight paints its quad points, a square strokes its border,
    // a note draws an icon -- so "no /AP" is not "nothing to draw". This block pins that premise;
    // the pixel answer is read below, off `QtPdfBackend::render`.
    //
    // Not off the screen. The channel §54 and §57 used for the form-field question -- gui-probe's
    // -Mode formpage, which grabs the screen with CopyFromScreen -- was tried first here and gave
    // four runs *identical* band counts and *four different* PNG hashes: a topmost third-party
    // window was sitting inside the crop, and what varied was that window's own clock. §66 recorded
    // the same trap and answered it with PrintWindow; this question does not need the screen at all,
    // and a measurement nobody else's window can cover is the better instrument for it.
    //
    // The three kinds are not redundancy: a renderer could paint markup rectangles and still ignore
    // notes, or the other way round, and the question the roadmap cares about -- "can this program
    // ever show a highlight the reader made elsewhere" -- is answered per kind.
    {
        const QStringList kinds = {QStringLiteral("highlight"), QStringLiteral("square"),
                                   QStringLiteral("note")};
        const QStringList expected = {QStringLiteral("/Highlight"), QStringLiteral("/Square"),
                                      QStringLiteral("/Text")};
        for (int index = 0; index < kinds.size(); ++index) {
            const QString path
                = scratch.filePath(QStringLiteral("markup-%1.pdf").arg(kinds.at(index)));
            QString e;
            check(!writeMarkupSample(fixture, path, kinds.at(index), &e).isEmpty(),
                  QStringLiteral("setup: a page carrying one %1 annotation: %2")
                      .arg(expected.at(index))
                      .arg(e));
            const MarkupReport read = readMarkupAnnotation(path);
            check(read.subtype == expected.at(index),
                  QStringLiteral("%1: the file really carries that one annotation, so a zero pixel "
                                 "answer is about the renderer and not about an empty page (got %2)")
                      .arg(expected.at(index))
                      .arg(read.subtype.isEmpty() ? QStringLiteral("(none)") : read.subtype));
            check(!read.hasAppearance,
                  QStringLiteral("%1: and no /AP to blame -- a viewer that supports annotations has "
                                 "to paint this from the dictionary alone")
                      .arg(expected.at(index)));
            if (kinds.at(index) == QStringLiteral("highlight")) {
                check(read.hasQuadPoints,
                      "the highlight names the area it should paint (quad points, two per line)");
            }
            if (kinds.at(index) == QStringLiteral("note")) {
                check(read.hasContents, "the note carries the text an icon would open onto");
            }
            note(QStringLiteral("sec81 %1: subtype=%2 quadPoints=%3 contents=%4 appearance=%5")
                     .arg(expected.at(index))
                     .arg(read.subtype)
                     .arg(read.hasQuadPoints ? 1 : 0)
                     .arg(read.hasContents ? 1 : 0)
                     .arg(read.hasAppearance ? 1 : 0));
        }

        // The pixel half, measured through the class the app actually renders with.
        //
        // The first version of this called `QPdfDocument::render` directly and reported 0 pixels for
        // all three kinds -- and that was a measurement of the wrong thing. `RenderOptions::annotations`
        // defaults to **on** in this program (document.h:52, with the reason spelled out: a document
        // that shows its highlights everywhere else and not here reads as broken), while Qt's own
        // default render options carry no annotation flag. So the bare call answered for a path the
        // product never takes. §58's lesson again: an instrument that cannot see something produces
        // findings that look exactly like the thing being missing -- here it took a red-free "0" to
        // notice.
        //
        // Each kind is therefore read twice: against the same page with the flag on (what a reader
        // sees), and the same file rendered with the flag off (what the flag is worth). The second
        // column is the one that keeps the first from being read as "annotations are drawn" when the
        // pixels actually come from something else in the page.
        const auto renderFirstPage = [](const QString &path, bool withAnnotations) {
            QtPdfBackend document;
            if (document.load(path, QString()) != Document::Status::Ready)
                return QImage();
            RenderOptions options;
            options.annotations = withAnnotations;
            return document.render(0, QSize(595, 842), options);
        };
        const auto difference = [](const QImage &one, const QImage &other) {
            struct Diff
            {
                int pixels = 0;
                QRect box;
            };
            Diff out;
            if (one.isNull() || other.isNull() || one.size() != other.size())
                return out;
            const QImage a = one.convertToFormat(QImage::Format_RGB32);
            const QImage b = other.convertToFormat(QImage::Format_RGB32);
            for (int y = 0; y < a.height(); ++y) {
                const QRgb *rowA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
                const QRgb *rowB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    if (rowA[x] == rowB[x])
                        continue;
                    ++out.pixels;
                    out.box = out.box.isNull() ? QRect(x, y, 1, 1) : out.box.united(QRect(x, y, 1, 1));
                }
            }
            return out;
        };

        const QImage plainPage = renderFirstPage(source, true);
        check(!plainPage.isNull(),
              "the page without annotations renders, so a difference is measurable");
        // Where each annotation sits, in the same top-left coordinates the rendered image uses: the
        // file's y is measured from the bottom of an 842-point page.
        const QRect highlightBox(70, 842 - 792, 230, 22);
        const QRect squareBox(70, 842 - 760, 190, 60);
        const QRect noteBox(320, 842 - 794, 24, 24);
        const QList<QPair<QString, QRect>> boxes
            = {{QStringLiteral("highlight"), highlightBox}, {QStringLiteral("square"), squareBox},
               {QStringLiteral("note"), noteBox}};
        for (const auto &one : boxes) {
            const QString markedPath
                = scratch.filePath(QStringLiteral("markup-%1.pdf").arg(one.first));
            const QImage markedOn = renderFirstPage(markedPath, true);
            const QImage markedOff = renderFirstPage(markedPath, false);
            check(!markedOn.isNull() && !markedOff.isNull(),
                  QStringLiteral("the page carrying %1 renders both ways").arg(one.first));
            const auto drawn = difference(plainPage, markedOn);
            const auto flag = difference(markedOff, markedOn);
            const auto box = [](const QRect &area) {
                return area.isNull()
                     ? QStringLiteral("(none)")
                     : QStringLiteral("%1,%2 %3x%4")
                           .arg(area.x())
                           .arg(area.y())
                           .arg(area.width())
                           .arg(area.height());
            };
            note(QStringLiteral("sec81 %1: %2 pixels differ from the same page without the "
                                "annotation, inside %3 (its own rect is %4); turning the flag off "
                                "moves %5 pixels")
                     .arg(one.first)
                     .arg(drawn.pixels)
                     .arg(box(drawn.box))
                     .arg(box(one.second))
                     .arg(flag.pixels));
            // The promise this slice exists to test: a reader who highlighted a passage in another
            // program has to see it here. `RenderOptions::annotations` defaults to on for exactly
            // that reason, so a 0 in this column means the promise is not kept for that subtype --
            // and the flag column says whether the flag is what carries it.
            check(drawn.pixels > 0,
                  QStringLiteral("%1: the annotation layer reaches the page image -- %2 pixels "
                                 "differ inside %3")
                      .arg(one.first)
                      .arg(drawn.pixels)
                      .arg(box(drawn.box)));
            check(!drawn.box.isNull() && one.second.adjusted(-4, -4, 4, 4).contains(drawn.box),
                  QStringLiteral("%1: and the difference stays inside the area that annotation "
                                 "names, so it is the annotation and not the page reflowing: %2")
                      .arg(one.first)
                      .arg(box(drawn.box)));
        }
        // The positive control the three zeros above need: the same rectangle, painted by the page's
        // own content stream rather than by an annotation. If this also read 0, the instrument -- not
        // the renderer -- would be the thing not looking.
        const QString paintedPath = scratch.filePath(QStringLiteral("painted-rect.pdf"));
        QString paintedError;
        check(!writePaintedRectSample(source, paintedPath, &paintedError).isEmpty(),
              QStringLiteral("setup: the same page with a box painted into its own contents: %1")
                  .arg(paintedError));
        const auto painted = difference(plainPage, renderFirstPage(paintedPath, true));
        note(QStringLiteral("sec81 painted-rect vs plain: %1 pixels differ, box %2")
                 .arg(painted.pixels)
                 .arg(painted.box.isNull() ? QStringLiteral("(none)")
                                           : QStringLiteral("%1,%2 %3x%4")
                                                 .arg(painted.box.x())
                                                 .arg(painted.box.y())
                                                 .arg(painted.box.width())
                                                 .arg(painted.box.height())));
        // What the painted rectangle adds is not "the instrument can see something" -- the three
        // readings above already do that -- but that a mark at *this* area is reported at *this*
        // area: the difference box comes back exactly the rect that was filled, so the boxes the
        // annotations produced can be read as geometry rather than as unexplained ink.
        check(painted.pixels > 0,
              QStringLiteral("the instrument sees a mark at that spot when the page itself carries "
                             "one: %1 pixels")
                  .arg(painted.pixels));
        check(!painted.box.isNull() && highlightBox.adjusted(-3, -3, 3, 3).contains(painted.box),
              QStringLiteral("and the difference it sees is inside the area that was painted (%1)")
                  .arg(painted.box.isNull()
                           ? QStringLiteral("(none)")
                           : QStringLiteral("%1,%2 %3x%4")
                                 .arg(painted.box.x())
                                 .arg(painted.box.y())
                                 .arg(painted.box.width())
                                 .arg(painted.box.height())));

        // And the claim §54/§57 reached on the screen -- that a form field draws nothing -- asked of
        // this instrument instead. It matters which way it goes: §57's zero was read off a screen
        // crop, and the same crop in §81 turned out to be able to show a third-party window instead
        // of the page. A zero here keeps the earlier round standing (on a channel that has since
        // been shown to be fallible); a non-zero would mean the "表单层不可见" sentence had to be
        // retracted rather than cited.
        {
            const QString fieldPath = scratch.filePath(QStringLiteral("field-flat-copy.pdf"));
            QString fieldError;
            check(!writeFieldCopy(source, fieldPath, QStringLiteral("probeField"),
                                  QStringLiteral("a value in a box"), &fieldError)
                       .isEmpty(),
                  QStringLiteral("setup: a page carrying one text field: %1").arg(fieldError));
            const auto field = difference(plainPage, renderFirstPage(fieldPath, true));
            note(QStringLiteral("sec81 form field vs plain: %1 pixels differ, box %2")
                     .arg(field.pixels)
                     .arg(field.box.isNull()
                              ? QStringLiteral("(none)")
                              : QStringLiteral("%1,%2 %3x%4")
                                    .arg(field.box.x())
                                    .arg(field.box.y())
                                    .arg(field.box.width())
                                    .arg(field.box.height())));
            // This field carries no /AP, which is the shape §54 used (not §57's, which brought its
            // own drawing). So a difference here would be the renderer inventing a box for a widget
            // that brought nothing to draw with.
            check(field.pixels == 0,
                  QStringLiteral("a /Widget with no /AP draws nothing -- the §54 sentence, now "
                                 "measured off the render path rather than off a screen crop: %1 "
                                 "pixels differ")
                      .arg(field.pixels));
        }

        // ---- §82: and a widget that BRINGS its own drawing? ----
        //
        // §57 built exactly this file, looked at it through -Mode formpage, and recorded "0 像素差,
        // 两张 PNG 同一个 SHA". That channel has since been shown (in §81, two lines above) to be able
        // to show a third-party window instead of the page, so the conclusion has been re-read here
        // off the render path the app actually uses. The four variants are not four chances to be
        // right: each isolates one of the explanations the sentence could have, and they are only
        // separable because everything but one key changes between them.
        {
            const QString painted = QStringLiteral("APPEARS-HERE");
            // Each row's expectation is written down next to its shape rather than derived from it:
            // the point of the table is that the subtype name is the only thing separating the two
            // groups, and that claim only exists if the rows say which group they were measured in.
            struct Shape
            {
                QString name;
                AppearanceProbe probe;
                bool draws;
            };
            const QList<Shape> shapes
                = {{QStringLiteral("square-carries-the-painting"),
                    AppearanceProbe{"/Square", false, false, false},
                    true},
                   {QStringLiteral("square-with-field-keys"),
                    AppearanceProbe{"/Square", false, false, true},
                    true},
                   {QStringLiteral("widget-unregistered"),
                    AppearanceProbe{"/Widget", false, false, false},
                    false},
                   {QStringLiteral("widget-in-acroform"),
                    AppearanceProbe{"/Widget", true, false, true},
                    false},
                   {QStringLiteral("widget-need-appearance"),
                    AppearanceProbe{"/Widget", true, true, true},
                    false}};
            const QRect paintBox(60, 842 - 630, 200, 30);
            const auto describe = [](const QRect &area) {
                return area.isNull()
                     ? QStringLiteral("(none)")
                     : QStringLiteral("%1,%2 %3x%4")
                           .arg(area.x())
                           .arg(area.y())
                           .arg(area.width())
                           .arg(area.height());
            };
            for (const Shape &shape : shapes) {
                const QString path
                    = scratch.filePath(QStringLiteral("appearance-%1.pdf").arg(shape.name));
                QString e;
                check(!writeAppearanceProbeCopy(source, path, shape.probe, &e).isEmpty(),
                      QStringLiteral("setup: the page with one %1: %2").arg(shape.name).arg(e));
                // File-level half first: the drawing has to be on this page and still decodable, or a
                // 0 below would be measuring a page that lost the stream, not a renderer that
                // ignored it.
                const int carried = pageAnnotationPaints(path, painted);
                check(carried == 1,
                      QStringLiteral("%1: the file carries the appearance on page one, decoded, so "
                                     "the pixel reading below is about drawing and not about "
                                     "carrying (found %2)")
                          .arg(shape.name)
                          .arg(carried));
                const auto drawn = difference(plainPage, renderFirstPage(path, true));
                const auto flag = difference(renderFirstPage(path, false),
                                             renderFirstPage(path, true));
                note(QStringLiteral("sec82 %1: subtype=%2 registered=%3 fieldKeys=%4 "
                                    "needAppearances=%5 carried=%6 -> %7 pixels differ inside %8 "
                                    "(its rect is %9); the flag alone moves %10")
                         .arg(shape.name)
                         .arg(QString::fromUtf8(shape.probe.subtype))
                         .arg(shape.probe.registeredField ? 1 : 0)
                         .arg(shape.probe.fieldKeys ? 1 : 0)
                         .arg(shape.probe.needAppearances ? 1 : 0)
                         .arg(carried)
                         .arg(drawn.pixels)
                         .arg(describe(drawn.box))
                         .arg(describe(paintBox))
                         .arg(flag.pixels));
                if (shape.draws) {
                    check(drawn.pixels > 0,
                          QStringLiteral("%1: an appearance stream this page carries reaches the "
                                         "image -- %2 pixels -- so the blank rows below are not a "
                                         "renderer that ignores /AP")
                              .arg(shape.name)
                              .arg(drawn.pixels));
                    check(!drawn.box.isNull()
                              && paintBox.adjusted(-3, -3, 3, 3).contains(drawn.box),
                          QStringLiteral("%1: and it lands on the area the annotation names (%2)")
                              .arg(shape.name)
                              .arg(describe(drawn.box)));
                    check(flag.pixels > 0,
                          QStringLiteral("%1: the Annotations flag is the lever that put it there, "
                                         "so this is the layer the reader asks for -- %2 pixels move "
                                         "with it")
                              .arg(shape.name)
                              .arg(flag.pixels));
                } else {
                    check(drawn.pixels == 0,
                          QStringLiteral("%1: the same stream on a /Widget draws nothing -- %2 "
                                         "pixels differ")
                              .arg(shape.name)
                              .arg(drawn.pixels));
                    // Not "and the flag was never on": pulling the one lever this API has moves
                    // nothing here either, which is what turns the row from an observation into the
                    // statement that there is no lever we are failing to pull.
                    check(flag.pixels == 0,
                          QStringLiteral("%1: and the Annotations flag moves nothing for it either "
                                         "-- %2 pixels")
                              .arg(shape.name)
                              .arg(flag.pixels));
                }
            }
            // The fifth reading is not a variant of the probe but §57's own generator, so the number
            // here replaces the one that record printed rather than merely resembling it.
            {
                const QString sec57 = scratch.filePath(QStringLiteral("appearance-sec57.pdf"));
                QString e;
                check(!writeFieldWithAppearanceCopy(source, sec57, QStringLiteral("drawnField"),
                                                    QStringLiteral("typed on page 1"), 0, &e)
                           .isEmpty(),
                      QStringLiteral("setup: §57's own appearance sample: %1").arg(e));
                const int registered = appearancesWithPaint(sec57, painted);
                const int onThePage = pageAnnotationPaints(sec57, painted);
                const auto drawn = difference(plainPage, renderFirstPage(sec57, true));
                note(QStringLiteral("sec82 sec57-generator: appearancesWithPaint=%1 "
                                    "pageAnnotationPaints=%2 -> %3 pixels differ inside %4")
                         .arg(registered)
                         .arg(onThePage)
                         .arg(drawn.pixels)
                         .arg(describe(drawn.box)));
                check(registered == 1 && onThePage == 1,
                      QStringLiteral("§57's file still carries its drawing registered and on the "
                                     "page (%1 / %2), so the number below is the renderer's")
                          .arg(registered)
                          .arg(onThePage));
                // §57's conclusion was reached off a screen crop that §81 later showed can show
                // something else entirely -- and §58 escalated it to "表单层整层不可见". This says the
                // first part was right after all, and the second part was too wide: what is
                // invisible is the widget subtype, not the annotation layer.
                check(drawn.pixels == 0,
                      QStringLiteral("§57's 0 pixels reproduces off the render path, not only off "
                                     "a screen: %1 differ").arg(drawn.pixels));
            }
        }

        // ---- §94: does that 0 reach the four paths that are not the page view? ----
        //
        // §82 closed with an inference -- print, image export, the thumbnail strip and the preview
        // pane all ask for the same drawing, so they must lose the same thing. §58's lesson is that an
        // inference is exactly what reads like a finding, so this measures it instead, at the size and
        // with the option set each call site builds today:
        //   src/ui/thumbnailpanel.cpp:80          RenderOptions options;  (annotations defaults true)
        //   src/windows/thumbnailprovider.cpp:111 RenderOptions{}         shell thumbnail
        //   src/windows/previewhandler.cpp:846    RenderOptions{}         preview pane
        //   src/export/imageexport.cpp:82         annotations=true, optimizedForLcd=false, by DPI
        //   src/print/printjob.cpp:190            annotations=true, optimizedForLcd=false, by sheet
        // What these rows do NOT measure: the printer driver, the file the exporter writes, and the
        // shell's compositing. They measure the one place the /Subtype decision can be made --
        // QtPdfBackend::render() -- with each path's own options and size. If a call site's option set
        // changes later, this table will not notice, which is why every row names the file and line it
        // copies and why the default itself is asserted once below rather than assumed four times.
        {
            struct RenderPath
            {
                const char *name;
                QSize pixels;
                bool lcd;
            };
            const QList<RenderPath> paths
                = {{"page-view-fit-width", QSize(1240, 1753), true},
                   {"thumbnail-strip", QSize(150, 212), false},
                   {"shell-thumbnail", QSize(260, 368), false},
                   {"preview-pane", QSize(400, 566), false},
                   {"image-export-150dpi", QSize(1240, 1754), false},
                   {"print-a4-300dpi", QSize(2480, 3508), false}};

            const auto renderAt = [](const QString &path, const QSize &pixels, bool lcd) {
                QtPdfBackend document;
                if (document.load(path, QString()) != Document::Status::Ready)
                    return QImage();
                RenderOptions options;
                options.optimizedForLcd = lcd;
                return document.render(0, pixels, options);
            };
            // Counting inside the box rather than over the whole page: at 150 px wide a 200x30-point
            // mark is a handful of pixels, and a page-wide difference would let unrelated resampling
            // noise answer the question.
            const auto differingWithin = [](const QImage &one, const QImage &other,
                                            const QRect &area) {
                if (one.isNull() || other.isNull() || one.size() != other.size())
                    return -1;
                const QImage a = one.convertToFormat(QImage::Format_RGB32);
                const QImage b = other.convertToFormat(QImage::Format_RGB32);
                int count = 0;
                for (int y = area.top(); y <= area.bottom() && y < a.height(); ++y) {
                    const QRgb *rowA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
                    const QRgb *rowB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
                    for (int x = area.left(); x <= area.right() && x < a.width(); ++x) {
                        if (rowA[x] != rowB[x])
                            ++count;
                    }
                }
                return count;
            };

            // The two shell paths pass RenderOptions{}, so everything below rests on this default.
            // Said as a check because a flipped default would silently move four rows.
            check(RenderOptions{}.annotations,
                  QStringLiteral("RenderOptions{} asks for the annotation layer by default -- the "
                                 "thumbnail and preview paths pass nothing else, so if this ever "
                                 "reads false their 0s mean something different"));

            const QString widgetPath = scratch.filePath(QStringLiteral("sec94-widget.pdf"));
            const QString squarePath = scratch.filePath(QStringLiteral("sec94-square.pdf"));
            QString e;
            check(!writeAppearanceProbeCopy(source, widgetPath,
                                            AppearanceProbe{"/Widget", true, false, true}, &e)
                       .isEmpty(),
                  QStringLiteral("setup: §94's widget probe: %1").arg(e));
            check(!writeAppearanceProbeCopy(source, squarePath,
                                            AppearanceProbe{"/Square", false, false, false}, &e)
                       .isEmpty(),
                  QStringLiteral("setup: §94's square probe: %1").arg(e));
            // Both files must carry the same stream, or a 0 below could be a lost stream again (§82).
            check(pageAnnotationPaints(widgetPath, QStringLiteral("APPEARS-HERE")) == 1
                      && pageAnnotationPaints(squarePath, QStringLiteral("APPEARS-HERE")) == 1,
                  "both §94 probes carry their appearance on page one, decoded");

            const QRect paintPoints(60, 842 - 630, 200, 30);
            for (const RenderPath &p : paths) {
                const double sx = double(p.pixels.width()) / 595.0;
                const double sy = double(p.pixels.height()) / 842.0;
                const QRect box(int(paintPoints.x() * sx), int(paintPoints.y() * sy),
                                int(std::ceil(paintPoints.width() * sx)),
                                int(std::ceil(paintPoints.height() * sy)));
                const QImage plain = renderAt(source, p.pixels, p.lcd);
                const int widget = differingWithin(plain, renderAt(widgetPath, p.pixels, p.lcd), box);
                const int square = differingWithin(plain, renderAt(squarePath, p.pixels, p.lcd), box);
                note(QStringLiteral("sec94 %1 at %2x%3 (lcd=%4) inside %5,%6 %7x%8: widget=%9 "
                                    "square=%10")
                         .arg(QString::fromLatin1(p.name))
                         .arg(p.pixels.width())
                         .arg(p.pixels.height())
                         .arg(p.lcd ? 1 : 0)
                         .arg(box.x())
                         .arg(box.y())
                         .arg(box.width())
                         .arg(box.height())
                         .arg(widget)
                         .arg(square));
                check(!plain.isNull(), QStringLiteral("%1: the plain page renders at this size")
                                            .arg(QString::fromLatin1(p.name)));
                check(square > 0,
                      QStringLiteral("%1: an annotation-layer mark reaches %2 pixels at this size, "
                                     "so the widget row below is not a channel that cannot see the "
                                     "layer at all")
                          .arg(QString::fromLatin1(p.name))
                          .arg(square));
                check(widget == 0,
                      QStringLiteral("%1: the same mark on a /Widget contributes %2 pixels at this "
                                     "size -- the print/export/thumbnail/preview paths lose the "
                                     "field's answer the same way the page view does")
                          .arg(QString::fromLatin1(p.name))
                          .arg(widget));
            }
        }
    }

    // ---- a name that is readable but not replaceable ----
    //
    // §79. The last step of a save is one MoveFileExW per file with MOVEFILE_REPLACE_EXISTING, and
    // while a program holds a PDF open -- including this one, in another tab or another window --
    // Windows withholds the delete right that step needs. Measured first in
    // tests/documenttab_smoke.cpp, on a name a tab of this program was reading: error 5, arriving
    // *after* the whole document had been rebuilt and written under a staged name, with the reader
    // told a name this program itself was holding might be "locked by another program". So the save
    // now asks the same question before it stages anything.
    //
    // Both halves are pinned because §70's rule is that a contract with two halves needs one
    // assertion each: a block that only exercises the locked name would keep passing if the probe
    // turned into a blanket refusal -- and a save that never lets an existing name through is every
    // overwrite the program used to do correctly.
    {
        const QString victim = scratch.filePath(QStringLiteral("held-by-a-reader.pdf"));
        QString error;
        check(QFile::copy(source, victim),
              "setup: the name a save will aim at already exists, as a copy of the document");
        const QString victimHash = hashOfFile(victim);
        const int victimPages = pagesAsReadByTheReader(victim);

        {
            QPdfDocument reader;
            check(reader.load(victim) == QPdfDocument::Error::None,
                  QStringLiteral("setup: and a reader has it open the way another tab does (%1)")
                      .arg(int(reader.error())));

            DocumentEditor guarded;
            check(guarded.open(source, QString(), &error),
                  QStringLiteral("setup: the document to save: %1").arg(error));
            check(guarded.deletePages({0}, &error),
                  QStringLiteral("setup: one page is taken out: %1").arg(error));
            error.clear();
            check(!guarded.saveAs(victim, &error),
                  QStringLiteral("a save aimed at a name this program is reading refuses instead "
                                 "of pretending it wrote: %1")
                      .arg(error));
            check(error.contains(QStringLiteral("盖不掉")),
                  QStringLiteral("and the reason names a name that cannot be replaced, not a write "
                                 "that failed: %1").arg(error));
            check(hashOfFile(victim) == victimHash,
                  "the file under that name is byte for byte the one the reader was holding");

            // Nothing may be staged either -- that is the half the late failure used to leave
            // behind: a finished new document sitting in the temporary folder, which the reader has
            // no reason to look for and every reason to believe was thrown away.
            const QStringList leftBehind = QDir(scratch.path())
                                               .entryList({QStringLiteral("xyzpdf-save-*.pdf")},
                                                          QDir::Files);
            check(leftBehind.isEmpty(),
                  QStringLiteral("and no staged file was left in the folder: %1 -- %2")
                      .arg(leftBehind.size())
                      .arg(leftBehind.join(QStringLiteral(", "))));
        }

        // The other half: with the name free again, the same save goes through, and the file at
        // that name becomes the edited document rather than staying the old one.
        DocumentEditor freed;
        check(freed.open(source, QString(), &error),
              QStringLiteral("setup: the same document opened again: %1").arg(error));
        check(freed.deletePages({0}, &error),
              QStringLiteral("setup: and the same page taken out: %1").arg(error));
        error.clear();
        check(freed.saveAs(victim, &error),
              QStringLiteral("the same name, no longer read by anyone, saves: %1").arg(error));
        check(pagesAsReadByTheReader(victim) == victimPages - 1,
              QStringLiteral("and what is at that name is the edited document: %1 pages were %2")
                  .arg(pagesAsReadByTheReader(victim))
                  .arg(victimPages));
    }

    if (failures == 0) {
        std::cout << "document editor smoke: all checks passed (" << notes
                  << " measurement(s) printed above)\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
