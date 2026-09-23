#include "documenteditor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

// A namespace rather than a class member, and named the way QObject's is: these
// are sentences to show the reader, so they go through the catalogue. Going
// through QObject::tr() instead would park every one of them in the "QObject"
// context, which is the thing src/windows/shellintegration.cpp warns about.
QString tr(const char *text)
{
    return QCoreApplication::translate("DocumentEditor", text);
}

// The comparison half of sameFile(): the same path written two ways is still one
// file, and the two ways a save dialog produces are a different drive letter case
// and a slash rather than a backslash.
QString comparablePath(const QString &path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower();
}

// The document as it was opened: every page present, in its own order.
QVector<int> everyPageInOrder(int pageCount)
{
    QVector<int> pages;
    pages.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        pages.append(page);
    return pages;
}

// The pages a request names, as the list an operation holds: deduplicated by the
// set that built it, and in order, so that two requests for the same pages produce
// the same operation -- which is what lets a test compare one against the other.
QVector<int> sortedFrom(const QSet<int> &pages)
{
    QVector<int> out;
    out.reserve(pages.size());
    for (int page : pages)
        out.append(page);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// The words a reader asked for, because these show up in a menu and a menu that
// says "undo operation 2" is not a menu. A member rather than something in the
// namespace above, for the sake of the catalogue -- see the declaration.
QString DocumentEditor::describeOperation(const Operation &op)
{
    const int count = op.pages.size();
    if (op.kind == Operation::Kind::RotatePages) {
        switch (op.quarterTurns) {
        case 2:
            return tr("旋转 180°（%1 页）").arg(count);
        case 3:
            return tr("向左转 90°（%1 页）").arg(count);
        default:
            return tr("向右转 90°（%1 页）").arg(count);
        }
    }
    return tr("删除 %1 页").arg(count);
}

DocumentEditor::DocumentEditor() = default;

DocumentEditor::~DocumentEditor() = default;

DocumentEditor::DeletePlan DocumentEditor::planDelete(const QVector<int> &present,
                                                       const QVector<int> &pages)
{
    DeletePlan plan;
    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // Every number in `named` is an original page number, so the two questions it
    // raises are separate ones: is it still there at all, and how many go if this
    // request is granted. Numbers past the end of the document never reach here --
    // the range parser the dialog uses clamps them -- so anything missing from
    // `present` is a page an earlier pending operation removed.
    int removable = 0;
    for (int page : std::as_const(named)) {
        if (present.contains(page))
            ++removable;
    }
    plan.pagesToRemove = removable;
    plan.alreadyGone = named.size() - removable;
    plan.pagesRemaining = present.size() - removable;
    // Named at least one page and nothing is left: the two ways to have no pages
    // after a delete are different, and "you asked for nothing" is not "you would
    // have destroyed the document".
    plan.leavesNothing = plan.pagesToRemove > 0 && plan.pagesRemaining <= 0;
    return plan;
}

DocumentEditor::DeletePlan DocumentEditor::planDelete(const QVector<int> &pages) const
{
    return planDelete(live, pages);
}

int DocumentEditor::pagesStillThere(const QVector<int> &pages) const
{
    QSet<int> named;
    for (int page : pages) {
        if (page >= 0 && live.contains(page))
            named.insert(page);
    }
    return named.size();
}

bool DocumentEditor::sameFile(const QString &path, const QString &other)
{
    if (path.isEmpty() || other.isEmpty())
        return false;
    if (comparablePath(path) == comparablePath(other))
        return true;

    // The string compare above catches what a save dialog hands back. This catches
    // the rest -- an 8.3 short name typed over the shoulder of a long one, a
    // junction pointed at the same folder -- by asking the file system what each
    // path really is. canonicalFilePath() returns an empty string for a path that
    // does not exist, which is the normal case for a destination being created, so
    // this only says "same" and can never say "different".
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString otherCanonical = QFileInfo(other).canonicalFilePath();
    return !canonical.isEmpty() && !otherCanonical.isEmpty()
        && comparablePath(canonical) == comparablePath(otherCanonical);
}

bool DocumentEditor::open(const QString &path, const QString &password, QString *error)
{
    // Before anything else: a failed open must not leave the previous document's
    // edits behind, or an error message and a dirty tab arrive together.
    pending.clear();
    undone.clear();
    live.clear();
    originalPages = 0;
    sourcePath.clear();
    sourcePassword.clear();

    // Parsed, counted, and let go. Holding on to the QPDF is what the alternative
    // design needed and what this class decided not to do; see the header.
    std::unique_ptr<QPDF> document = std::make_unique<QPDF>();
    try {
        document->processFile(path.toUtf8().constData(),
                              password.isEmpty() ? nullptr : password.toUtf8().constData());
        originalPages = int(QPDFPageDocumentHelper(*document).getAllPages().size());
    } catch (const std::exception &e) {
        document.reset();
        // Worded for the one place this is reached from: the reader is looking at
        // this document right now, so "the file is unreadable" would be a sentence
        // the reader knows is false. What is actually going on is that the
        // structural editor's parser is stricter than the renderer's, and the
        // detail below is the only evidence of which of the two it is.
        *error = tr("这份文档正在阅读中，但结构编辑用的解析器打不开它。\n\n解析器说：%1")
                     .arg(QString::fromUtf8(e.what()));
        return false;
    }
    if (originalPages == 0) {
        // Not a complaint about the parser: a document with no pages is one this
        // class can do nothing to, and saying so here beats failing at the save.
        *error = tr("这份 PDF 一页也没有，没法在上面做任何页面修改。");
        return false;
    }

    live = everyPageInOrder(originalPages);
    sourcePath = path;
    sourcePassword = password;
    return true;
}

bool DocumentEditor::deletePages(const QVector<int> &pagesToDelete, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    const DeletePlan plan = planDelete(pagesToDelete);
    if (plan.pagesToRemove == 0) {
        // Two different reasons for the same zero, and the reader needs the right
        // one: naming nothing, versus naming pages that this same list of
        // unsaved changes has already taken out.
        *error = plan.alreadyGone > 0
            ? tr("那些页在还没保存的修改里已经删掉了。")
            : tr("页码范围里没有可删除的页面。");
        return false;
    }
    if (plan.leavesNothing) {
        *error = tr("不能把每一页都删掉：一个不剩的 PDF 不是 PDF，任何阅读器都打不开它。"
                    "想丢掉整份文档，就把文件本身删掉。");
        return false;
    }

    QSet<int> named;
    for (int page : pagesToDelete) {
        if (page >= 0 && live.contains(page))
            named.insert(page);
    }
    Operation op;
    op.kind = Operation::Kind::DeletePages;
    op.pages = sortedFrom(named);
    record(op);
    return true;
}

bool DocumentEditor::rotatePages(const QVector<int> &pagesToRotate, int quarterTurns,
                                 QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (quarterTurns < 1 || quarterTurns > 3) {
        // PDF records rotation as /Rotate, and only in quarter turns. Refusing an
        // angle that cannot be stored beats storing the nearest one and letting
        // the reader find out from the page.
        *error = tr("旋转只能是 90° 的整倍数。");
        return false;
    }
    QSet<int> named;
    for (int page : pagesToRotate) {
        if (page >= 0 && live.contains(page))
            named.insert(page);
    }
    if (named.isEmpty()) {
        *error = tr("页码范围里没有可旋转的页面。");
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::RotatePages;
    op.pages = sortedFrom(named);
    op.quarterTurns = quarterTurns;
    record(op);
    return true;
}

void DocumentEditor::record(const Operation &op)
{
    pending.append(op);
    // The branch this list took is gone: a new operation after an undo is the
    // reader moving forward again, not a second history waiting somewhere to be
    // picked back up.
    undone.clear();
    // And the page list is retaken from the operations rather than patched, so
    // that the count the dialog showed and the count a save writes cannot be two
    // different answers to the same question.
    rebuildLive();
}

bool DocumentEditor::undo()
{
    if (pending.isEmpty())
        return false;
    undone.append(pending.takeLast());
    rebuildLive();
    return true;
}

bool DocumentEditor::redo()
{
    if (undone.isEmpty())
        return false;
    pending.append(undone.takeLast());
    rebuildLive();
    return true;
}

void DocumentEditor::rebuildLive()
{
    live = everyPageInOrder(originalPages);
    for (const Operation &op : std::as_const(pending)) {
        if (op.kind == Operation::Kind::DeletePages) {
            for (int page : op.pages)
                live.removeOne(page);
        }
    }
}

QString DocumentEditor::undoDescription() const
{
    return pending.isEmpty() ? QString() : describeOperation(pending.last());
}

QString DocumentEditor::redoDescription() const
{
    return undone.isEmpty() ? QString() : describeOperation(undone.last());
}

QString DocumentEditor::pendingSummary() const
{
    QStringList parts;
    for (const Operation &op : pending)
        parts.append(describeOperation(op));
    return parts.join(QStringLiteral("，"));
}

QVector<int> DocumentEditor::positionsOf(const QVector<int> &present, const QVector<int> &pages)
{
    QVector<int> positions;
    positions.reserve(pages.size());
    for (int page : pages) {
        const int at = present.indexOf(page);
        if (at >= 0)
            positions.append(at);
    }
    return positions;
}

void DocumentEditor::apply(const Operation &op, QPDF &document, QVector<int> &present)
{
    QPDFPageDocumentHelper helper(document);
    // One snapshot per operation: the handles below are what removePage() matches
    // against, and re-asking between changes would number the pages against a tree
    // that has already changed under them.
    //
    // Not const, and a copy of a handle is still the page itself: these are object
    // references, so rotatePage() on one of them writes into the document.
    std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();

    if (op.kind == Operation::Kind::RotatePages) {
        for (int original : op.pages) {
            const int at = present.indexOf(original);
            if (at >= 0 && at < int(pages.size()))
                pages.at(size_t(at)).rotatePage(op.quarterTurns * 90, /* relative = */ true);
        }
        return;
    }

    const QVector<int> positions = positionsOf(present, op.pages);
    // Highest first, so that the index of every page still to come is unaffected
    // by the one just taken out.
    QVector<int> ordered = positions;
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    for (int at : std::as_const(ordered)) {
        if (at < int(pages.size()))
            helper.removePage(pages.at(size_t(at)));
    }
    for (int original : op.pages)
        present.removeOne(original);
}

bool DocumentEditor::saveAs(const QString &path, QString *error)
{
    if (live.isEmpty() && pending.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (sameFile(sourcePath, path)) {
        // The rule this enforces is AGENTS.md's 永不写用户的 PDF 文件, and the
        // save dialog is quite capable of handing back the name it was seeded
        // with. The reader asked for a new file; saying so out loud is better than
        // quietly writing a different one.
        *error = tr("另存为不能覆盖正在打开的文件。请换一个文件名。");
        return false;
    }
    if (live.isEmpty()) {
        // Cannot be reached through deletePages(), which refuses the request that
        // would empty the document. It is checked again because a save writes what
        // this list says, and an empty page tree is not a PDF -- an operation added
        // later must not be able to get one written by oversight.
        *error = tr("这些修改会把每一页都删掉，而一个不剩的 PDF 任何阅读器都打不开。");
        return false;
    }

    const QFileInfo wanted(path);
    const QString destination = wanted.absoluteFilePath();
    const QDir directory = wanted.absoluteDir();
    if (!directory.exists()) {
        *error = tr("文件夹不存在：%1").arg(directory.absolutePath());
        return false;
    }

    // A name chosen rather than a file created, and beside the destination rather
    // than in the temp folder: the last step has to be a rename within one volume,
    // since across volumes it becomes a copy -- slower and, more importantly, not
    // atomic. It is also why this is not a QTemporaryFile: that keeps the handle it
    // created the file with until the object is destroyed, and QPDFWriter then
    // answers "Permission denied" when pointed at a file this function made a
    // moment ago (measured, not guessed: it is what the first version of this code
    // did). Nothing opens the path before the writer does, so nothing holds it. The
    // stamp and the counter keep two saves in the same folder apart, and an existing
    // file is stepped over rather than truncated -- one left behind by an earlier
    // refused write stays readable.
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    QString stagedPath = directory.filePath(QStringLiteral("xyzpdf-save-%1.pdf").arg(stamp));
    for (int attempt = 2; QFileInfo::exists(stagedPath) && attempt < 100; ++attempt) {
        stagedPath = directory.filePath(
            QStringLiteral("xyzpdf-save-%1-%2.pdf").arg(stamp).arg(attempt));
    }

    try {
        // The replay: the source read again from disk, then every pending operation
        // in the order the reader asked for them. This is where an original page
        // number becomes a position in a document that has already shrunk, which is
        // the whole reason `present` is carried along.
        std::unique_ptr<QPDF> document = std::make_unique<QPDF>();
        document->processFile(sourcePath.toUtf8().constData(),
                              sourcePassword.isEmpty() ? nullptr
                                                       : sourcePassword.toUtf8().constData());
        QVector<int> present = everyPageInOrder(originalPages);
        for (const Operation &op : std::as_const(pending))
            apply(op, *document, present);

        // Nothing here sets encryption parameters, so the writer's default
        // (preserveEncryption = true) carries the original's /Encrypt dictionary --
        // including its /P permission bits -- into the copy. That default is off
        // whenever prefiltering, content normalization or QDF mode is selected, and
        // off the moment setR6EncryptionParameters is called, so a future line that
        // adds one of those quietly turns 删页 into a tool that hands out unprotected
        // copies of protected documents. tests/documenteditor_smoke.cpp compares the
        // bits on both files for exactly that reason.
        QPDFWriter writer(*document, stagedPath.toUtf8().constData());
        writer.write();
    } catch (const std::exception &e) {
        // Nothing usable came of the attempt, so nothing of ours is left behind.
        QFile::remove(stagedPath);
        *error = tr("写出文件时出错，原来的文件没有改动。\n\n解析器说：%1")
                     .arg(QString::fromUtf8(e.what()));
        return false;
    }

    // One call, not "remove then rename": removing first would leave a window in
    // which the reader has neither file, and a failure inside that window would
    // lose a document it already had. QFile::rename is not enough here -- it
    // refuses to step on a file that is already there, and the save dialog has just
    // asked the reader whether this name was meant to overwrite that file.
    const std::wstring from = QDir::toNativeSeparators(stagedPath).toStdWString();
    const std::wstring to = QDir::toNativeSeparators(destination).toStdWString();
    if (MoveFileExW(from.c_str(), to.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        == 0) {
        // From here on a complete staged file is never thrown away by this
        // function: the failure that leaves one behind -- the destination locked by
        // another program -- outlasts any dialog this could put up, and the
        // alternative is deleting work that took real time to produce.
        *error = tr("新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，或者这个名字不允许"
                    "覆盖（Windows 错误 %2）。写好的文件留在 %3。")
                     .arg(destination)
                     .arg(GetLastError())
                     .arg(stagedPath);
        return false;
    }
    return true;
}
