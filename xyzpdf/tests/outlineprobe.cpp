// 书签（大纲）编辑动笔之前的量，和 tests/watermarkprobe.cpp 同一个理由：这一族的问题不是
// "我们想怎么设计"，而是"两个库各自把大纲当成什么"。写的一侧只有 QPDF 可用，而它的大纲 helper
// 是**只读**的（QPDFOutlineObjectHelper 的构造函数全是 private，头文件写着 "You can only create
// one of these using QPDFOutlineDocumentHelper"，没有一个 setter）—— 所以"编辑书签"实际上是
// 手工改 /Outlines 那串字典：/Title /Parent /First /Last /Prev /Next /Count，外加 /Dest 的几种
// 形状。哪一条不改就会让整棵树读不出来，而"读不出来"只有 PDFium 那边（也就是我们侧栏真正读的
// 那一侧）说话。
//
// Hand-run, never in the gate: like the other two probes every line it prints is another
// library's answer. The questions, in the order below:
//
//   A. 一棵七种形状的大纲，三个读者各读一遍 —— qpdf、Qt 的 bookmark model、我们自己的
//      QtPdfBackend::outline()。第三个是重点：侧栏画的是那棵树，不是 Qt 的 model。
//   B. 四处故意写坏（父项 /Count 的符号、断掉的 /Next、指错处的 /Parent、根上缺 /First /Last），
//      每处单独一个文件：哪个读者还看得见，决定哪些一致性是承重的。
//   C. 页出去的那一遍：删掉书签指着的那一页、把那一页挪到末尾，再三个读者各读一遍。§51 在表单层
//      量到的"页没了、注册还在"在大纲层是不是同一回事。
//   D. 一份真书而不是手造样本：184 条书签里，侧栏今天看得见几条（它的整条读路就是第 A 段那三个
//      读者）。Location/Zoom 那一问不在这一段里，它在 A 的每一行上 —— 两条读路都把 loc 与 zoom
//      打出来，所以"点书签跳到页顶"到底是设计还是丢数据，在 A 就能读出答案。
//
// 输出走 stdout；GBK 控制台下中文会花，所以按项目老规矩写文件再看：
//   build-local-release\tests\xyzpdf_outlineprobe.exe > H:\tmp\outline.txt 2>&1

#include "../src/document/outline.h"
#include "../src/document/qtpdfbackend.h"
#include "../src/edit/documenteditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFOutlineObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <QtPdf/QPdfDocument>
#include <QtPdf/QPdfBookmarkModel>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cout << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

void say(const QString &line)
{
    std::cout << line.toStdString() << '\n';
}

// The destination shapes an outline item can take. PDF 2.0 §12.3.3.3 allows a /Dest array, a
// name referring to a destination in the document's name tree or the older root /Dests, and the
// /A GoTo action instead of /Dest; an item with neither is legal too (a section heading).
enum Shape {
    FitArray = 0,     // /Dest [page /Fit]
    XyzArray,         // /Dest [page /XYZ 0 700 0] -- carries a position
    GoToAction,       // /A << /S /GoTo /D [page /FitH 650] >>
    NamedFound,       // /Dest (chap1), resolvable through root /Names /Dests
    NamedMissing,     // /Dest (nowhere), not in any tree
    DestinationDict,  // /Dest << ... >> -- the form the spec does not define for items
    NoDestination     // nothing at all
};

QPDFObjectHandle makeItem(QPDF &qpdf, const std::string &title, QPDFObjectHandle parent,
                          int shape, QPDFObjectHandle page)
{
    QPDFObjectHandle item = QPDFObjectHandle::newDictionary();
    item.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
    if (!parent.isNull())
        item.replaceKey("/Parent", parent);

    switch (shape) {
    case FitArray: {
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(page);
        dest.appendItem(QPDFObjectHandle::newName("/Fit"));
        item.replaceKey("/Dest", dest);
        break;
    }
    case XyzArray: {
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(page);
        dest.appendItem(QPDFObjectHandle::newName("/XYZ"));
        dest.appendItem(QPDFObjectHandle::newReal(0));
        dest.appendItem(QPDFObjectHandle::newReal(700));
        dest.appendItem(QPDFObjectHandle::newReal(0));
        item.replaceKey("/Dest", dest);
        break;
    }
    case GoToAction: {
        QPDFObjectHandle target = QPDFObjectHandle::newArray();
        target.appendItem(page);
        target.appendItem(QPDFObjectHandle::newName("/FitH"));
        target.appendItem(QPDFObjectHandle::newReal(650));
        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
        action.replaceKey("/S", QPDFObjectHandle::newName("/GoTo"));
        action.replaceKey("/D", target);
        item.replaceKey("/A", action);
        break;
    }
    case NamedFound:
        item.replaceKey("/Dest", QPDFObjectHandle::newString("chap1"));
        break;
    case NamedMissing:
        item.replaceKey("/Dest", QPDFObjectHandle::newString("nowhere"));
        break;
    case DestinationDict: {
        // A dictionary where the spec allows an array, a name or (via /A) nothing at all. The
        // point is not that a generator does this -- it is that an editor must not turn a tree
        // into a pile because one item's /Dest is unreadable.
        QPDFObjectHandle array = QPDFObjectHandle::newArray();
        array.appendItem(page);
        array.appendItem(QPDFObjectHandle::newName("/Fit"));
        QPDFObjectHandle dest = QPDFObjectHandle::newDictionary();
        dest.replaceKey("/D", array);
        item.replaceKey("/Dest", dest);
        break;
    }
    default:
        break;
    }
    return qpdf.makeIndirectObject(item);
}

// Build the same nesting without any of the awkward shapes: four items, one of them a parent of
// two children, every destination an explicit array. This existed to separate two explanations for
// the children not showing up under Qt's model in section A -- a reader that refuses a whole
// subtree because one item is malformed, versus my own read asking the model only for the root's
// rows. The second won and the first is dead: counted through the hierarchy this file shows 4 rows
// for its 2 root rows, and outline-shapes.pdf shows 9 for 6, malformed /Dest and all.
QString buildPlainNestedSample(const QString &source, const QString &destination, QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };

        QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
        root = qpdf.makeIndirectObject(root);

        QPDFObjectHandle first = makeItem(qpdf, "Chapter 1", root, FitArray, pageAt(0));
        QPDFObjectHandle second = makeItem(qpdf, "Chapter 2", root, FitArray, pageAt(10));
        first.replaceKey("/Next", second);
        second.replaceKey("/Prev", first);

        QPDFObjectHandle sub = makeItem(qpdf, "Section 1.1", first, FitArray, pageAt(3));
        QPDFObjectHandle sub2 = makeItem(qpdf, "Section 1.2", first, XyzArray, pageAt(6));
        sub.replaceKey("/Next", sub2);
        sub2.replaceKey("/Prev", sub);
        first.replaceKey("/First", sub);
        first.replaceKey("/Last", sub2);
        first.replaceKey("/Count", QPDFObjectHandle::newInteger(2));

        root.replaceKey("/First", first);
        root.replaceKey("/Last", second);
        root.replaceKey("/Count", QPDFObjectHandle::newInteger(4));
        qpdf.getRoot().replaceKey("/Outlines", root);

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// Which structure to break in the written file. Zero means "build it correctly", which is the
// premise every control needs: with the good file the readings say what the shapes mean, and
// only the difference to it says what a broken one costs.
enum Break {
    NothingBroken = 0,
    ChildCountSignFlipped,   // the parent's /Count says -2 where the tree says 2
    FirstNextRemoved,        // the first top-level item no longer points at its sibling
    ChildParentMisaimed,     // a child claims the root as its parent
    RootWithoutFirstLast     // the root lists a /Count and no children to walk
};

QString buildOutlineSample(const QString &source, const QString &destination, int broken,
                           QString *error)
{
    try {
        QPDF qpdf;
        qpdf.processFile(source.toUtf8().constData());
        const std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };

        QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
        root = qpdf.makeIndirectObject(root);

        // Five top-level items; the fourth owns two children. Sibling links are written in both
        // directions on purpose: that is what an editor has to keep true after any edit.
        std::vector<QPDFObjectHandle> tops;
        tops.push_back(makeItem(qpdf, "Top fit", root, FitArray, pageAt(0)));
        tops.push_back(makeItem(qpdf, "Top xyz", root, XyzArray, pageAt(1)));
        tops.push_back(makeItem(qpdf, "Top goto", root, GoToAction, pageAt(2)));
        QPDFObjectHandle parentItem = makeItem(qpdf, "Parent", root, FitArray, pageAt(3));
        tops.push_back(parentItem);
        tops.push_back(makeItem(qpdf, "Top named missing", root, NamedMissing, pageAt(4)));
        for (size_t index = 1; index < tops.size(); ++index) {
            tops.at(index).replaceKey("/Prev", tops.at(index - 1));
            tops.at(index - 1).replaceKey("/Next", tops.at(index));
        }

        QPDFObjectHandle firstChild = makeItem(qpdf, "Child one", parentItem, XyzArray, pageAt(5));
        QPDFObjectHandle secondChild = makeItem(qpdf, "Child two", parentItem, NamedFound,
                                                pageAt(6));
        firstChild.replaceKey("/Next", secondChild);
        secondChild.replaceKey("/Prev", firstChild);
        parentItem.replaceKey("/First", firstChild);
        parentItem.replaceKey("/Last", secondChild);
        parentItem.replaceKey("/Count", QPDFObjectHandle::newInteger(2));

        // Two more shapes, one of them not a form the spec defines for an outline item: the
        // reading that matters is whether a viewer drops the tree or just that entry.
        QPDFObjectHandle dictChild = makeItem(qpdf, "Child dict", parentItem, DestinationDict,
                                              pageAt(7));
        secondChild.replaceKey("/Next", dictChild);
        dictChild.replaceKey("/Prev", secondChild);
        parentItem.replaceKey("/Last", dictChild);
        parentItem.replaceKey("/Count", QPDFObjectHandle::newInteger(3));
        QPDFObjectHandle noDest = makeItem(qpdf, "Top no destination", root, NoDestination,
                                           QPDFObjectHandle::newNull());
        tops.back().replaceKey("/Next", noDest);
        noDest.replaceKey("/Prev", tops.back());
        tops.push_back(noDest);

        root.replaceKey("/First", tops.front());
        root.replaceKey("/Last", tops.back());
        // /Count counts **every** descendant of the root, not the top level: six items on top of
        // three children. Getting this wrong is the single easiest mistake an editor can make, so
        // the number is computed here from the tree it has to describe rather than typed.
        int descendants = 0;
        for (const QPDFObjectHandle &top : tops) {
            ++descendants;
            if (top.hasKey("/First")) {
                QPDFObjectHandle child = top.getKey("/First");
                while (!child.isNull()) {
                    ++descendants;
                    child = child.getKey("/Next");
                }
            }
        }
        root.replaceKey("/Count", QPDFObjectHandle::newInteger(descendants));

        // The name the NamedFound item points at, as a single-leaf name tree: legal, and the
        // shape Qt's own resolver has to walk.
        QPDFObjectHandle names = QPDFObjectHandle::newArray();
        names.appendItem(QPDFObjectHandle::newString("chap1"));
        QPDFObjectHandle chap = QPDFObjectHandle::newArray();
        chap.appendItem(pageAt(8));
        chap.appendItem(QPDFObjectHandle::newName("/Fit"));
        names.appendItem(chap);
        QPDFObjectHandle dests = QPDFObjectHandle::newDictionary();
        dests.replaceKey("/Names", names);
        QPDFObjectHandle rootNames = QPDFObjectHandle::newDictionary();
        rootNames.replaceKey("/Dests", qpdf.makeIndirectObject(dests));
        qpdf.getRoot().replaceKey("/Names", rootNames);

        qpdf.getRoot().replaceKey("/Outlines", root);

        switch (broken) {
        case ChildCountSignFlipped:
            parentItem.replaceKey("/Count", QPDFObjectHandle::newInteger(-3));
            break;
        case FirstNextRemoved:
            tops.front().replaceKey("/Next", QPDFObjectHandle::newNull());
            break;
        case ChildParentMisaimed:
            firstChild.replaceKey("/Parent", root);
            break;
        case RootWithoutFirstLast:
            root.replaceKey("/First", QPDFObjectHandle::newNull());
            root.replaceKey("/Last", QPDFObjectHandle::newNull());
            break;
        default:
            break;
        }

        QPDFWriter writer(qpdf, destination.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
    return destination;
}

// ---- reader 1: qpdf. It resolves named destinations for us, which is one of the answers. ----

// The destination in its own words: which kind, and with what numbers. An array's *length* cannot
// tell /XYZ 0 700 0 from /FitH 650 from /Fit, and §75's whole question is which of those a save
// leaves behind -- so the line has to say the name, not the count of slots.
QString describeDest(const QPDFObjectHandle &dest)
{
    if (dest.isNull())
        return QStringLiteral("none");
    if (dest.isName())
        return QStringLiteral("named:%1").arg(QString::fromUtf8(dest.getName().c_str()));
    if (dest.isString())
        return QStringLiteral("named:%1").arg(QString::fromUtf8(dest.getStringValue().c_str()));
    if (dest.isDictionary())
        return QStringLiteral("dict");
    if (!dest.isArray())
        return QStringLiteral("unreadable");
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

// How many destinations of each kind a file has. The before/after of a save is one line each way;
// reading two printed trees against each other is how a measurement like this gets away from you.
void tallyDestinations(const QString &label, const QString &path)
{
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        QPDFOutlineDocumentHelper helper(qpdf);
        QMap<QString, int> seen;
        std::function<void(QPDFOutlineObjectHelper &)> count = [&](QPDFOutlineObjectHelper &item) {
            seen[describeDest(item.getDest())]++;
            for (QPDFOutlineObjectHelper &child : item.getKids())
                count(child);
        };
        for (QPDFOutlineObjectHelper &item : helper.getTopLevelOutlines())
            count(item);
        QStringList parts;
        for (auto it = seen.constBegin(); it != seen.constEnd(); ++it)
            parts << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
        say(QStringLiteral("  %1: %2").arg(label, parts.isEmpty() ? QStringLiteral("(no outlines)")
                                                                  : parts.join(QStringLiteral(", "))));
    } catch (const std::exception &e) {
        say(QStringLiteral("  %1: qpdf threw %2").arg(label, QString::fromUtf8(e.what())));
    }
}

void walkQpdf(QPDFOutlineObjectHelper &item, int depth)
{
    const QPDFObjectHandle dest = item.getDest();
    const QString kind = describeDest(dest);
    const QPDFObjectHandle page = item.getDestPage();
    say(QStringLiteral("    %1%2 | dest=%3 | count=%4 | page=%5")
            .arg(QString(depth * 2, QChar(' ')), QString::fromUtf8(item.getTitle().c_str()), kind)
            .arg(item.getCount())
            .arg(page.isNull() ? QStringLiteral("-") : QStringLiteral("ok")));
    for (QPDFOutlineObjectHelper &child : item.getKids())
        walkQpdf(child, depth + 1);
}

void readWithQpdf(const QString &path)
{
    try {
        QPDF qpdf;
        qpdf.processFile(path.toUtf8().constData());
        QPDFOutlineDocumentHelper helper(qpdf);
        if (!helper.hasOutlines()) {
            say("  qpdf: no /Outlines at all");
            return;
        }
        std::vector<QPDFOutlineObjectHelper> tops = helper.getTopLevelOutlines();
        say(QStringLiteral("  qpdf: %1 top-level item(s)").arg(tops.size()));
        for (QPDFOutlineObjectHelper &item : tops)
            walkQpdf(item, 1);
    } catch (const std::exception &e) {
        say(QStringLiteral("  qpdf: threw %1").arg(QString::fromUtf8(e.what())));
    }
}

// ---- reader 2: Qt's bookmark model, i.e. what PDFium builds from the same bytes. ----

void readWithQt(const QString &path)
{
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None) {
        say("  qt: the document did not open");
        return;
    }
    QPdfBookmarkModel model;
    model.setDocument(&document);
    // Read it BOTH ways. Our backend walks `rowCount(QModelIndex())` and trusts a Level column,
    // on the assumption recorded in outline.h that the model is flat. Whether that assumption is
    // what the model does is the whole question behind the sidebar's indentation, so the walk
    // here also asks each row how many children the model itself reports -- and prints the
    // /Outlines row count with and without descending. If those two disagree, a flat read is
    // dropping bookmarks, and the drop is silent.
    const int rootRows = model.rowCount(QModelIndex());
    say(QStringLiteral("  qt: %1 row(s) under the root; Level values seen below").arg(rootRows));
    int visited = 0;
    std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
        const int rows = model.rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex index = model.index(row, 0, parent);
            if (!index.isValid())
                continue;
            ++visited;
            const QString title =
                model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Title)).toString();
            const int level =
                model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Level)).toInt();
            const int page =
                model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Page)).toInt();
            const QPointF location =
                model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Location)).toPointF();
            const double zoom =
                model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Zoom)).toDouble();
            const int kids = model.rowCount(index);
            say(QStringLiteral("    %1%2 | level=%3 page=%4 loc=%5,%6 zoom=%7 children=%8")
                    .arg(QString(depth * 2 - 2, QChar(' ')), title)
                    .arg(level).arg(page).arg(location.x()).arg(location.y()).arg(zoom).arg(kids));
            if (depth < 4)
                walk(index, depth + 1);
        }
    };
    walk(QModelIndex(), 1);
    say(QStringLiteral("  qt: %1 row(s) counted through the hierarchy (%2 under the root)")
            .arg(visited).arg(rootRows));
    if (visited > rootRows)
        say("  qt: THE MODEL IS NOT FLAT -- a root-only read loses rows");
}

// ---- reader 3: our own backend, which is the tree the sidebar actually draws. ----

void readWithOurBackend(const QString &path)
{
    QtPdfBackend backend;
    if (backend.load(path, QString()) != Document::Status::Ready) {
        say("  our backend: would not open it");
        return;
    }
    const Outline tree = backend.outline();
    int total = 0;
    std::function<void(const QVector<OutlineNode> &, int)> walk =
        [&](const QVector<OutlineNode> &nodes, int depth) {
            for (const OutlineNode &node : nodes) {
                ++total;
                say(QStringLiteral("    %1%2 | page=%3 | location=%4,%5")
                        .arg(QString(depth * 2, QChar(' ')), node.title)
                        .arg(node.page)
                        .arg(node.location.x())
                        .arg(node.location.y()));
                walk(node.children, depth + 1);
            }
        };
    walk(tree.nodes, 1);
    say(QStringLiteral("  our backend: %1 node(s), %2 top level")
            .arg(total)
            .arg(tree.nodes.size()));
}

void readAllThree(const QString &label, const QString &path)
{
    say(QStringLiteral("--- %1 (%2) ---").arg(label, QFileInfo(path).fileName()));
    readWithQpdf(path);
    readWithQt(path);
    readWithOurBackend(path);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::cout << "no scratch directory\n";
        return 1;
    }

    QString source = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (source.isEmpty())
        source = QDir(QCoreApplication::applicationDirPath())
                     .filePath(QStringLiteral("../../tests/fixtures/many-pages.pdf"));
    if (!QFile::exists(source)) {
        std::cout << "fixture not found: " << source.toStdString() << '\n';
        return 1;
    }
    say(QStringLiteral("source: %1").arg(source));

    QString error;
    const QString whole = scratch.filePath(QStringLiteral("outline-shapes.pdf"));
    if (buildOutlineSample(source, whole, NothingBroken, &error).isEmpty()) {
        std::cout << "could not build the sample: " << error.toStdString() << '\n';
        return 1;
    }

    say("");
    say("=== A. one hand-built outline, three readers ===");
    readAllThree("nothing broken", whole);

    const QString plain = scratch.filePath(QStringLiteral("outline-plain-nested.pdf"));
    error.clear();
    check(!buildPlainNestedSample(source, plain, &error).isEmpty(),
          QStringLiteral("setup: the same nesting without any awkward shape: %1").arg(error));
    readAllThree("nesting only: nothing malformed", plain);

    say("");
    say("=== B. which consistencies are load-bearing ===");
    const QStringList labels = {
        QStringLiteral("/Count of an item flipped to negative"),
        QStringLiteral("the first top-level item's /Next removed"),
        QStringLiteral("a child's /Parent aimed at the root instead of the item"),
        QStringLiteral("root keeps /Count but lists no /First /Last")
    };
    for (int broken = ChildCountSignFlipped; broken <= RootWithoutFirstLast; ++broken) {
        const QString path = scratch.filePath(QStringLiteral("outline-broken%1.pdf").arg(broken));
        error.clear();
        if (buildOutlineSample(source, path, broken, &error).isEmpty()) {
            say(QStringLiteral("  control %1 could not be built: %2").arg(broken).arg(error));
            continue;
        }
        readAllThree(labels.at(broken - 1), path);
    }

    say("");
    say("=== C. a page going out from under a bookmark ===");
    {
        DocumentEditor gone;
        error.clear();
        check(gone.open(whole, QString(), &error) && gone.deletePages({5}, &error),
              QStringLiteral("setup: delete the page a bookmark names: %1").arg(error));
        const QString goneOut = scratch.filePath(QStringLiteral("outline-page-deleted.pdf"));
        error.clear();
        check(gone.saveAs(goneOut, &error), QStringLiteral("and that deletion is written: %1").arg(error));
        readAllThree("page 6 of 40 deleted", goneOut);

        DocumentEditor moved;
        error.clear();
        check(moved.open(whole, QString(), &error)
                  && moved.movePages({0}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("setup: move the first page to the end: %1").arg(error));
        const QString movedOut = scratch.filePath(QStringLiteral("outline-page-moved.pdf"));
        error.clear();
        check(moved.saveAs(movedOut, &error), QStringLiteral("and that move is written: %1").arg(error));
        readAllThree("first page moved to the end", movedOut);
    }

    say("");
    say("=== D. a real document with a real outline ===");
    {
        const QString real = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
        if (real.isEmpty() || !QFile::exists(real))
            say("  (no real outline-bearing file given; pass it as the second argument)");
        else {
            say(QStringLiteral("  file: %1").arg(QFileInfo(real).fileName()));
            tallyDestinations("real book, as it came", real);
            readAllThree("real document, nothing done to it", real);
        }
    }

    say("");
    say("=== E. what a save through our own writer leaves on a destination ===");
    {
        // §71 wrote this down as an inferred consequence and never tested it: the read path asks Qt's
        // model for Title and Page and nothing else (qtpdfbackend.cpp:527-534 -- no Location role, no
        // Zoom role, although both exist), and the writer emits /Fit for every row
        // (documenteditor.cpp:713-719). If that is right, a bookmark naming a spot halfway down a page
        // loses that spot on the way through the sidebar and one save. The sample's "Top xyz" row is
        // the case: /XYZ 0 700 0 on an 842-point-tall page, which Qt itself reads back as loc=0,142
        // (top-left origin -- the number a writer would have to convert back).
        tallyDestinations("before, as built", whole);

        QtPdfBackend backend;
        check(backend.load(whole, QString()) == Document::Status::Ready,
              "setup: the sample opens through our own backend");
        const QVector<OutlineRow> asSeen = outlineRowsOf(backend.outline());
        const int carrying = static_cast<int>(std::count_if(
            asSeen.begin(), asSeen.end(),
            [](const OutlineRow &row) { return !row.location.isNull(); }));
        // isNull() is "both coordinates are 0", so a bookmark that genuinely aims at the page's top
        // left corner and a bookmark whose position was never read look identical in this type. That
        // is part of what a fix would have to settle, not a quirk of this line.
        say(QStringLiteral("  our reader's %1 row(s), of which %2 carry any location")
                .arg(asSeen.size()).arg(carrying));

        DocumentEditor editor;
        error.clear();
        check(editor.open(whole, QString(), &error) && editor.setOutline(asSeen, &error),
              QStringLiteral("setup: asking for the tree our reader saw is accepted: %1").arg(error));
        const QString after = scratch.filePath(QStringLiteral("outline-roundtrip.pdf"));
        error.clear();
        check(editor.saveAs(after, &error),
              QStringLiteral("setup: and that request is written: %1").arg(error));
        tallyDestinations("after setOutline + saveAs", after);
        readWithQpdf(after);
    }

    say("");
    if (failures == 0)
        say("outline probe finished: the lines above are the measurements, not verdicts");
    else
        say(QStringLiteral("outline probe: %1 setup line(s) said no").arg(failures));
    return failures == 0 ? 0 : 1;
}
