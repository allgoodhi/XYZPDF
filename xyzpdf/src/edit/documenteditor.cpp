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

#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
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

// The document as it was opened: every page present, in its own order, all of them
// named by the number the reader is looking at.
QVector<DocumentEditor::PageRef> openedDocumentInOrder(int pageCount)
{
    QVector<DocumentEditor::PageRef> pages;
    pages.reserve(pageCount);
    for (int page = 0; page < pageCount; ++page)
        pages.append({0, page});
    return pages;
}

// Whether a page of a file a merge drew from is one this request names. Repeats
// count once and the result is ordered by page number, so that two requests for the
// same pages produce the same operation -- which is what lets a test compare one
// against the other, and what makes the block arrive in the order the file has it.
QVector<DocumentEditor::PageRef> refsFrom(const QSet<int> &pages, int source)
{
    QVector<DocumentEditor::PageRef> out;
    out.reserve(pages.size());
    for (int page : pages)
        out.append({source, page});
    std::sort(out.begin(), out.end(), [](const DocumentEditor::PageRef &a,
                                        const DocumentEditor::PageRef &b) {
        return a.page < b.page;
    });
    return out;
}

// The pages a request names, as the list an operation holds.
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
QString DocumentEditor::describeOperation(const Operation &op) const
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
    if (op.kind == Operation::Kind::MovePages) {
        // The anchor number in the middle of it is the page as it reads on the
        // screen, which is the only numbering a reader has.
        if (op.moveTo == MoveTarget::Front)
            return tr("移到最前（%1 页）").arg(count);
        if (op.moveTo == MoveTarget::AfterPage)
            return tr("移到第 %2 页之后（%1 页）").arg(count).arg(op.anchor + 1);
        return tr("移到最后（%1 页）").arg(count);
    }
    if (op.kind == Operation::Kind::MergeFile) {
        // Which file, not just how many: a reader who merged three documents and
        // sees "撤销 合入 5 页" has no way to know which 5 pages are about to come
        // back. The name rather than the path, because this is a menu row.
        const Import &import = imports.value(op.importIndex);
        return tr("合入 %2 页（%1）")
            .arg(QFileInfo(import.path).fileName().isEmpty()
                     ? tr("另一份文件")
                     : QFileInfo(import.path).fileName())
            .arg(count);
    }
    if (op.kind == Operation::Kind::SplitBySize) {
        // The size the reader chose rather than the number of files it makes: the file
        // count is whatever is left after the deletes, and a menu row that says 拆成 8 份
        // stops being true the moment a page is taken out, while 每 5 页一份 does not.
        return tr("拆成每 %1 页一份").arg(op.pieceSize);
    }
    return tr("删除 %1 页").arg(count);
}

DocumentEditor::DocumentEditor() = default;

DocumentEditor::~DocumentEditor() = default;

DocumentEditor::DeletePlan DocumentEditor::planDelete(const QVector<PageRef> &present,
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
    // `present` is a page an earlier pending operation removed. Pages that came in
    // from another file cannot be named by these numbers at all, which is what keeps
    // them out of `removable` and into `pagesRemaining`.
    int removable = 0;
    for (int page : std::as_const(named)) {
        if (present.contains({0, page}))
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
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    return named.size();
}

int DocumentEditor::importedPageCount() const
{
    int imported = 0;
    for (const PageRef &ref : live) {
        if (ref.source != 0)
            ++imported;
    }
    return imported;
}

DocumentEditor::Landing DocumentEditor::landingSpot(const QVector<PageRef> &without, MoveTarget to,
                                                   int anchor)
{
    Landing spot;
    spot.at = without.size();
    if (to == MoveTarget::Front) {
        spot.at = 0;
        return spot;
    }
    if (to != MoveTarget::AfterPage)
        return spot;

    const int behind = without.indexOf({0, anchor});
    if (behind >= 0) {
        spot.at = behind + 1;
        spot.anchorUsed = anchor;
        return spot;
    }
    // The page named is gone. Where it was is still a place, and the pages that were
    // behind it stayed behind it, so the gap is in front of the first page whose
    // number is larger -- which is what the reader is standing behind now that the
    // one they named is not there. Pages that came in from another file are skipped in
    // that search: their numbers belong to their own document, and a landing spot
    // reasoned from one of them would be a spot the reader cannot find on the screen.
    for (int index = 0; index < without.size(); ++index) {
        const PageRef &ref = without.at(index);
        if (ref.source == 0 && ref.page > anchor) {
            spot.at = index;
            spot.anchorUsed = ref.page;
            break;
        }
    }
    spot.anchorWasGone = true;
    return spot;
}

DocumentEditor::MovePlan DocumentEditor::planMove(const QVector<PageRef> &present,
                                                 const QVector<int> &pages, MoveTarget to,
                                                 int anchor)
{
    MovePlan plan;
    plan.order = present;

    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // The block in the order it has where it stands, not the order the numbers were
    // typed in: "第 2、4 页移到最后" moves those two pages as a pair keeping 2 before
    // 4, and a reader who wrote "4,2" did not ask for them swapped.
    QVector<PageRef> moved;
    QVector<PageRef> kept;
    kept.reserve(present.size());
    for (const PageRef &ref : present) {
        if (ref.source == 0 && named.contains(ref.page))
            moved.append(ref);
        else
            kept.append(ref);
    }
    plan.pagesMoved = moved.size();
    plan.alreadyGone = named.size() - moved.size();
    if (moved.isEmpty())
        return plan;
    if (to == MoveTarget::AfterPage && moved.contains({0, anchor})) {
        // The anchor is inside the block, so "behind it" is where the block already
        // is. Nothing here can be said about where it should go instead -- a reader
        // who means that types a different page.
        plan.anchorIsMoved = true;
        return plan;
    }

    const Landing spot = landingSpot(kept, to, anchor);

    QVector<PageRef> order;
    order.reserve(present.size());
    for (int index = 0; index < spot.at; ++index)
        order.append(kept.at(index));
    for (const PageRef &ref : moved)
        order.append(ref);
    for (int index = spot.at; index < kept.size(); ++index)
        order.append(kept.at(index));

    plan.order = order;
    plan.landsAt = spot.at;
    plan.anchorUsed = spot.anchorUsed;
    plan.anchorWasGone = spot.anchorWasGone;
    plan.changesOrder = order != present;
    return plan;
}

DocumentEditor::MovePlan DocumentEditor::planMove(const QVector<int> &pages, MoveTarget to,
                                                 int anchor) const
{
    return planMove(live, pages, to, anchor);
}

DocumentEditor::MergePlan DocumentEditor::planMerge(const QVector<PageRef> &present,
                                                    const QVector<int> &pages, int mergeId,
                                                    MoveTarget to, int anchor)
{
    MergePlan plan;
    plan.order = present;

    QSet<int> named;
    for (int page : pages) {
        if (page >= 0)
            named.insert(page);
    }
    // The block as the file it comes from orders it, so that "取第 2、4 页" arrives as
    // 2 then 4 and the sentence below about where they land is true. `mergeId` rather
    // than the file index is what keeps two merges of one page from producing one page
    // twice over and an identity nobody can resolve.
    const QVector<PageRef> block = refsFrom(named, mergeId);
    plan.pagesAdded = block.size();
    if (block.isEmpty())
        return plan;

    // The same function a move uses, run over the whole list rather than over what is
    // left of it: nothing is taken out by a merge, so the landing spot is the one
    // place the two operations genuinely differ, and sharing it is what makes
    // "第 12 页之后" mean one thing in this program.
    const Landing spot = landingSpot(present, to, anchor);

    QVector<PageRef> order;
    order.reserve(present.size() + block.size());
    for (int index = 0; index < spot.at; ++index)
        order.append(present.at(index));
    for (const PageRef &ref : block)
        order.append(ref);
    for (int index = spot.at; index < present.size(); ++index)
        order.append(present.at(index));

    plan.order = order;
    plan.landsAt = spot.at;
    plan.anchorUsed = spot.anchorUsed;
    plan.anchorWasGone = spot.anchorWasGone;
    return plan;
}

QVector<DocumentEditor::PieceRange> DocumentEditor::pieceRanges(int total, int pieceSize)
{
    QVector<PieceRange> pieces;
    if (total <= 0)
        return pieces;
    // A piece of 0 is "nothing was split", and that comes out as one piece holding the
    // whole document rather than an empty list: the save then has one loop for every case,
    // and the case where nothing was split is not a special case that can be forgotten.
    // Anything from 1 up to total-1 is a real split; `splitInto()` refuses the rest.
    const int size = (pieceSize >= 1 && pieceSize < total) ? pieceSize : total;
    for (int first = 0; first < total; first += size) {
        PieceRange piece;
        piece.first = first;
        piece.last = qMin(total, first + size) - 1;
        pieces.append(piece);
    }
    return pieces;
}

QString DocumentEditor::piecePath(const QString &destination, int index, int total,
                                  const PieceRange &range)
{
    const QFileInfo wanted(destination);
    // Wide enough for the last piece, and never narrower than two: the folder sorts by
    // name, and pieces that read 1, 10, 11, 2 in Explorer are a page order nobody can
    // check by eye. The page numbers beside it are 1-based because this name is the one a
    // reader sees in a folder and types into a mail message; the ones inside `range` are
    // the 0-based numbers this class uses everywhere else.
    const int width = qMax(2, QString::number(total).size());
    const QString number = QStringLiteral("%1").arg(index + 1).rightJustified(width, QLatin1Char('0'));
    const QString pages = range.count() == 1
                              ? QString::number(range.first + 1)
                              : QStringLiteral("%1-%2").arg(range.first + 1).arg(range.last + 1);
    const QString stem = QStringLiteral("%1 %2 (%3)")
                             .arg(wanted.completeBaseName(), number, pages);
    const QString suffix = wanted.suffix();
    return QDir(wanted.absolutePath())
        .filePath(suffix.isEmpty() ? stem : stem + QLatin1Char('.') + suffix);
}

int DocumentEditor::splitPieceSize() const
{
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (it->kind == Operation::Kind::SplitBySize)
            return it->pieceSize;
    }
    return 0;
}

QVector<DocumentEditor::PieceRange> DocumentEditor::pieces() const
{
    return pieceRanges(pageCount(), splitPieceSize());
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
    // edits behind, or an error message and a dirty tab arrive together. The imports
    // go with them because pending operations name them by index, and an index left
    // over from another document's list would point at an unrelated file.
    pending.clear();
    undone.clear();
    live.clear();
    imports.clear();
    // And the merge identities go with them for the same reason: a number left over
    // from another document's list would be a page two documents both claim.
    nextMergeId = 1;
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

    live = openedDocumentInOrder(originalPages);
    sourcePath = path;
    sourcePassword = password;
    return true;
}

bool DocumentEditor::inspectImport(const QString &path, int *pageCount, QString *error)
{
    if (pageCount)
        *pageCount = 0;
    QString reason;
    int pages = 0;
    try {
        // Opened with no password on purpose: a file that needs one is refused below
        // rather than asked for, and the reason is in the sentence, not in a box that
        // would have to explain itself.
        QPDF document;
        document.processFile(path.toUtf8().constData());
        pages = int(QPDFPageDocumentHelper(document).getAllPages().size());
    } catch (const QPDFExc &e) {
        // QPDF answers a wrong or missing user password the same way it answers a
        // broken file -- by throwing -- so which of the two this is has to be read off
        // the error code. Getting it wrong would tell a reader their readable file is
        // unreadable, or ask them for a password a file never had.
        const bool needsPassword = e.getErrorCode() == qpdf_e_password;
        reason = needsPassword
            ? tr("%1 要用口令才能打开，不知道口令的文档合不进来。\n\n想合并它，先把"
                 "它本身打开一次，或者去掉它的口令。")
                  .arg(QFileInfo(path).fileName())
            : tr("%1 打不开，所以合不进来。\n\n解析器说：%2")
                  .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        reason = tr("%1 打不开，所以合不进来。\n\n解析器说：%2")
                     .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
    }
    if (!reason.isEmpty()) {
        *error = reason;
        return false;
    }
    if (pages == 0) {
        *error = tr("%1 一页也没有，没什么可合并的。").arg(QFileInfo(path).fileName());
        return false;
    }
    if (pageCount)
        *pageCount = pages;
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
        if (page >= 0 && live.contains({0, page}))
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
        if (page >= 0 && live.contains({0, page}))
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

bool DocumentEditor::movePages(const QVector<int> &pagesToMove, MoveTarget to, int anchor,
                               QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (to == MoveTarget::AfterPage && (anchor < 0 || anchor >= originalPages)) {
        // Checked against the file rather than against `live`: a page an earlier
        // unsaved change deleted is a page the reader can point at, and planMove()
        // knows what to do with it. A number that was never a page is not a
        // position to move behind.
        *error = tr("要说移到第几页之后，而那个页码不在这份文档里。");
        return false;
    }
    const MovePlan plan = planMove(pagesToMove, to, anchor);
    if (plan.pagesMoved == 0) {
        *error = plan.alreadyGone > 0
            ? tr("那些页在还没保存的修改里已经删掉了。")
            : tr("页码范围里没有可移动的页面。");
        return false;
    }
    if (plan.anchorIsMoved) {
        *error = tr("第 %1 页就在要移动的页里，这样移等于没移。").arg(anchor + 1);
        return false;
    }
    if (!plan.changesOrder) {
        // Three ways to end up where you already are, and the one that fits says
        // what the other two would have left the reader to work out.
        *error = to == MoveTarget::Front
            ? tr("那些页已经在最前头了。")
            : (to == MoveTarget::Back ? tr("那些页已经在最后了。")
                                      : tr("那些页已经在第 %1 页后面了。").arg(anchor + 1));
        return false;
    }

    QSet<int> named;
    for (int page : pagesToMove) {
        if (page >= 0 && live.contains({0, page}))
            named.insert(page);
    }
    Operation op;
    op.kind = Operation::Kind::MovePages;
    op.pages = sortedFrom(named);
    op.moveTo = to;
    op.anchor = anchor;
    record(op);
    return true;
}

bool DocumentEditor::mergeFile(const QString &path, const QVector<int> &pages, MoveTarget to,
                               int anchor, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (const int splitting = splitPieceSize(); splitting > 0) {
        // The other end of the rule splitInto() states, and the reason it has to be in
        // both places is that the two requests come in no fixed order: 拆分 then 合并 is
        // as reachable as 合并 then 拆分. What it keeps out of the file is a save with
        // imported pages to place into pieces numbered against a document the reader
        // cannot see -- and with page handles that would have to stay open across every
        // piece's writer at once.
        *error = tr("这份文档已经说了要拆成每 %1 页一份。先撤销那一步，再把文件合并进来。")
                     .arg(splitting);
        return false;
    }
    if (to == MoveTarget::AfterPage && (anchor < 0 || anchor >= originalPages)) {
        // The same rule 移动页面 enforces, for the same reason: the anchor has to be a
        // page of the document on the screen, which is the only numbering a reader can
        // check. A page an earlier change deleted is allowed and lands in the gap.
        *error = tr("要说合并到第几页之后，而那个页码不在这份文档里。");
        return false;
    }

    // Looked over again rather than trusted from the dialog. The file can have been
    // replaced, shrunk or deleted in the time between choosing it and pressing 合并,
    // and a merge that quietly brought in fewer pages than the summary promised is the
    // silent kind of wrong -- the count would look right in the status bar and be wrong
    // in the file.
    int importPages = 0;
    if (!inspectImport(path, &importPages, error))
        return false;

    QSet<int> named;
    for (int page : pages) {
        if (page < 0 || page >= importPages) {
            *error = tr("%1 一共 %2 页，没有第 %3 页。")
                         .arg(QFileInfo(path).fileName())
                         .arg(importPages)
                         .arg(page + 1);
            return false;
        }
        named.insert(page);
    }
    if (named.isEmpty()) {
        *error = tr("还没有说出要合并 %1 的哪几页。").arg(QFileInfo(path).fileName());
        return false;
    }

    // One file, one entry: operations point at files by index, so a second entry for a
    // path already in the list would make the same document look like two of them --
    // and would have the save open it twice.
    int index = -1;
    for (int candidate = 0; candidate < imports.size(); ++candidate) {
        if (sameFile(imports.at(candidate).path, path)) {
            index = candidate;
            break;
        }
    }
    const bool firstTime = index < 0;
    if (firstTime)
        index = imports.size();

    const QVector<int> chosen = sortedFrom(named);
    // This merge's own identity, taken before the plan is worked out because the plan
    // is what stamps it onto the pages. A refusal below burns a number, which costs
    // nothing: the only rule is that no two merges share one.
    const int mergeId = nextMergeId++;
    const MergePlan plan = planMerge(live, chosen, mergeId, to, anchor);
    if (!plan.usable()) {
        *error = tr("没有页可以合并进来。");
        return false;
    }
    if (firstTime)
        imports.append({path, importPages});

    Operation op;
    op.kind = Operation::Kind::MergeFile;
    op.pages = chosen;
    op.importIndex = index;
    op.mergeId = mergeId;
    op.moveTo = to;
    op.anchor = anchor;
    record(op);
    return true;
}

bool DocumentEditor::splitInto(int pagesPerPiece, QString *error)
{
    if (live.isEmpty()) {
        *error = tr("还没有打开任何文档。");
        return false;
    }
    if (const int imported = importedPageCount(); imported > 0) {
        // Not "拆分 is unavailable after a merge" but which pages make it unavailable:
        // the reader has to be able to find the merge and undo it, and after several of
        // them the count is the only thing this sentence can point at.
        *error = tr("这份要写出的文档里有 %1 页是合并进来的，而那些页没有你在屏幕上能看到的"
                    "页号，所以哪几页该进哪一份是说不出来的。先撤销那几次合并再拆。")
                     .arg(imported);
        return false;
    }
    if (pagesPerPiece < 1) {
        // The dialog's spin box cannot send this. Checked because a piece of 0 pages is
        // not a file, and a save that wrote one would write it forever.
        *error = tr("每份至少要有 1 页。");
        return false;
    }
    if (pagesPerPiece >= live.size()) {
        // Refused rather than recorded: a split that comes out as one piece dirties the
        // tab, earns an undo row, and writes exactly the file the reader already had.
        // Saying so is cheaper than letting them find out at the save.
        *error = tr("这份要写出的文档一共 %1 页，每份 %2 页只会写出一份，那就不用拆了。")
                     .arg(live.size())
                     .arg(pagesPerPiece);
        return false;
    }

    Operation op;
    op.kind = Operation::Kind::SplitBySize;
    op.pieceSize = pagesPerPiece;
    // record() and nothing else: no page is taken out, moved or turned, so `live` comes
    // back from rebuildLive() exactly as it went in. That is the whole point of the kind.
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
    live = openedDocumentInOrder(originalPages);
    for (const Operation &op : std::as_const(pending)) {
        if (op.kind == Operation::Kind::DeletePages) {
            for (int page : op.pages)
                live.removeOne({0, page});
        } else if (op.kind == Operation::Kind::MovePages) {
            // The same function the dialog called and the replay below uses, so
            // that the page list in memory and the page tree that gets written can
            // not drift apart by one position -- which, for a reorder, is the
            // difference between the document somebody asked for and one they did
            // not, with the same number of pages in both.
            live = planMove(live, op.pages, op.moveTo, op.anchor).order;
        } else if (op.kind == Operation::Kind::MergeFile) {
            // planMerge() again for the same reason, and note that the pages it adds
            // here are references to another file, not copies: this list is a promise
            // about where pages come from, and the bytes are only pulled in by the
            // save that says so.
            live = planMerge(live, op.pages, op.mergeId, op.moveTo, op.anchor).order;
        }
        // RotatePages needs no branch -- a turned page is still the same page in the same
        // place -- and neither does SplitBySize, which changes how many files a save
        // writes rather than which pages exist. A new kind that does change the list has to
        // appear here, and rebuild-rather-than-patch is what makes forgetting it show up as
        // a count that contradicts the operations instead of a list that quietly drifts.
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
    // tr() for a single comma: this is the line a reader reads in the discard prompt and
    // on the status bar, and joining it with a literal ， put CJK punctuation into the
    // English interface.
    return parts.join(tr("，"));
}

QVector<int> DocumentEditor::positionsOf(const QVector<PageRef> &present,
                                         const QVector<int> &pages)
{
    QVector<int> positions;
    positions.reserve(pages.size());
    for (int page : pages) {
        const int at = present.indexOf({0, page});
        if (at >= 0)
            positions.append(at);
    }
    return positions;
}

void DocumentEditor::apply(const Operation &op, QPDF &document, QVector<PageRef> &present,
                           QPDF *mergeSource)
{
    if (op.kind == Operation::Kind::SplitBySize) {
        // A split says how many files the save writes; it takes nothing out of the page
        // tree, moves nothing in it and turns nothing. The pruning into pieces happens in
        // saveAs(), after this walk has agreed with `live` -- so that the page order every
        // piece is cut from is the one the dialogs described.
        return;
    }

    QPDFPageDocumentHelper helper(document);
    // One snapshot per operation: the handles below are what removePage() matches
    // against, and re-asking between changes would number the pages against a tree
    // that has already changed under them.
    //
    // Not const, and a copy of a handle is still the page itself: these are object
    // references, so rotatePage() on one of them writes into the document.
    std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
    // `present` and the snapshot above are index-aligned -- both run through the pages
    // in the order the tree holds them -- so any page, from either file, can be found
    // by what the reader asked for. No two entries of `present` are the same reference,
    // which is what makes indexOf() a safe way to ask: a page merged in twice carries
    // two identities, so the copy a move leans against is the one that was meant.
    auto handleOf = [&](const PageRef &ref) { return pages.at(size_t(present.indexOf(ref))); };

    if (op.kind == Operation::Kind::RotatePages) {
        for (int original : op.pages) {
            const int at = present.indexOf({0, original});
            if (at >= 0 && at < int(pages.size()))
                pages.at(size_t(at)).rotatePage(op.quarterTurns * 90, /* relative = */ true);
        }
        return;
    }

    if (op.kind == Operation::Kind::MergeFile) {
        const MergePlan plan = planMerge(present, op.pages, op.mergeId, op.moveTo, op.anchor);
        // Pages taken out of another document are not object references here: they are
        // objects in a file that is open beside this one, so each has to be copied
        // across. Handing the foreign page straight to addPage()/addPageAt() is what
        // qpdf does for this -- it copies the page and its resources, after pushing any
        // attributes the other file inherited down into it.
        QPDFPageDocumentHelper fromHelper(*mergeSource);
        const std::vector<QPDFPageObjectHelper> fromPages = fromHelper.getAllPages();
        QPDFAcroFormDocumentHelper toForm(document);
        QPDFAcroFormDocumentHelper fromForm(*mergeSource);

        // Left to right, the same order planMove() re-inserts a block in, and for the
        // same reason: the page each insertion leans on is already in the tree. The
        // tree has changed by then, so the handle of the page before has to be re-asked
        // every time -- getAllPages() is a snapshot, and a stale one numbers the pages
        // against the tree as it was.
        for (int index = plan.landsAt; index < plan.landsAt + plan.pagesAdded; ++index) {
            const PageRef &ref = plan.order.at(index);
            // Both of these are refused loudly rather than skipped: the saveAs() above
            // re-opens each file and checks its page count before anything is staged, so
            // reaching here with a page that cannot be copied means the two lists have
            // stopped agreeing, and writing a document that is one page short of the
            // sentence the reader acted on is the failure this class exists to avoid.
            if (ref.source != op.mergeId)
                throw std::runtime_error(tr("要合并的那一页在这份文件里找不到。")
                                             .toUtf8()
                                             .constData());
            const int source = ref.page;
            if (source < 0 || source >= int(fromPages.size()))
                throw std::runtime_error(tr("要合并的那一页在这份文件里找不到。")
                                             .toUtf8()
                                             .constData());
            const QPDFPageObjectHelper foreign = fromPages.at(size_t(source));
            const QPDFObjectHandle fromHandle = foreign.getObjectHandle();
            if (index == 0)
                helper.addPage(foreign, /*first=*/true);
            else
                helper.addPageAt(foreign, /*before=*/false,
                                 helper.getAllPages().at(size_t(index - 1)));
            // A page copied as an object shares its /Annots array with the page it came
            // from, and its form fields go missing entirely because nothing in this
            // document's AcroForm points at them. qpdf's own repair for that is this
            // call, and it is worth making only of a page that has annotations: the
            // helper this needs is documented as expensive to build, and most pages of
            // most documents have none.
            if (fromHandle.hasKey("/Annots"))
                toForm.fixCopiedAnnotations(helper.getAllPages().at(size_t(index)).getObjectHandle(),
                                            fromHandle, fromForm);
        }
        present = plan.order;
        return;
    }

    if (op.kind == Operation::Kind::MovePages) {
        const MovePlan plan = planMove(present, op.pages, op.moveTo, op.anchor);
        QVector<PageRef> moved;
        for (const PageRef &ref : present) {
            if (op.pages.contains(ref.page) && ref.source == 0)
                moved.append(ref);
        }

        // Out of the tree first, with the handles held on to. This matters more for
        // a move than for anything else here: addPage() shallow-copies a page it
        // finds already in the tree so that it does not add it twice, so inserting
        // before removing would duplicate the page rather than move it -- a document
        // with the block in it twice and nothing to say so.
        for (const PageRef &ref : moved)
            helper.removePage(handleOf(ref));
        // Then walk the order that was asked for and put each moved page behind
        // whatever precedes it. The pages that were not named are still in their
        // relative order, and the moved ones are inserted left to right, so the page
        // before each insertion is already in the tree -- which makes the result
        // exactly `plan.order`, not merely a document with the same pages in it.
        for (int index = 0; index < plan.order.size(); ++index) {
            const PageRef &ref = plan.order.at(index);
            if (!moved.contains(ref))
                continue;
            if (index == 0)
                helper.addPage(handleOf(ref), /*first=*/true);
            else
                helper.addPageAt(handleOf(ref), /*before=*/false,
                                 handleOf(plan.order.at(index - 1)));
        }
        present = plan.order;
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
        present.removeOne({0, original});
}

bool DocumentEditor::saveAs(const QString &path, QString *error, QStringList *written)
{
    if (live.isEmpty() && pending.isEmpty()) {
        *error = tr("还没有打开任何文档。");
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
    const QDir directory = wanted.absoluteDir();
    if (!directory.exists()) {
        *error = tr("文件夹不存在：%1").arg(directory.absolutePath());
        return false;
    }

    // Where the bytes go. Without a split that is the one name the save dialog just asked
    // the reader about; with one it is a name per piece, cut from the same pieceRanges()
    // that fed the dialog's summary -- so the set of files that turns up in the folder is
    // the set that was described, and not a second answer to "拆成几份".
    const QVector<PieceRange> pieces = this->pieces();
    QStringList destinations;
    for (int index = 0; index < pieces.size(); ++index) {
        destinations << (pieces.size() > 1
                             ? QFileInfo(piecePath(path, index, pieces.size(), pieces.at(index)))
                                   .absoluteFilePath()
                             : wanted.absoluteFilePath());
    }

    for (const QString &one : std::as_const(destinations)) {
        if (sameFile(sourcePath, one)) {
            // The rule this enforces is AGENTS.md's 永不写用户的 PDF 文件, and the
            // save dialog is quite capable of handing back the name it was seeded
            // with. The reader asked for a new file; saying so out loud is better than
            // quietly writing a different one. Under a split the same collision is
            // reachable by accident rather than by pressing 用上一次的名字: a document
            // already called 报告 01 (1-3).pdf is exactly one piece's output name.
            *error = destinations.size() > 1
                         ? tr("拆出来的文件里有一个（%1）就是正在打开的这份。请换一个文件名。")
                               .arg(QFileInfo(one).fileName())
                         : tr("另存为不能覆盖正在打开的文件。请换一个文件名。");
            return false;
        }
        for (const Import &import : std::as_const(imports)) {
            if (sameFile(import.path, one)) {
                // The other half of the same rule. A merge keeps the file it draws from
                // open as an input for the length of this save, and the reader may well
                // have that file open in another tab as a document of its own -- so
                // pointing the destination at it would overwrite a file while this program
                // is reading pages out of it.
                *error = tr("另存为不能覆盖合并进来的文件（%1）。请换一个文件名。")
                             .arg(QFileInfo(import.path).fileName());
                return false;
            }
        }
        if (pieces.size() > 1 && QFileInfo::exists(one)) {
            // The dialog asked whether it was all right to overwrite *one* file -- the name
            // the reader typed -- and none of these. So a piece whose name is already taken
            // refuses the whole save instead of quietly replacing it, and refuses everything
            // rather than writing the pieces that were free: a folder holding six of eight
            // pieces is a folder that looks like the job was done.
            *error = tr("%1 那里已经有一个文件了。拆分不会盖掉已经存在的文件，请换一个文件名，"
                        "或者先把那个文件挪走。")
                         .arg(QFileInfo(one).fileName());
            return false;
        }
    }

    if (pieces.size() > 1) {
        for (const Operation &op : std::as_const(pending)) {
            if (op.kind == Operation::Kind::MergeFile) {
                // Both splitInto() and mergeFile() refuse this pairing, so arriving here
                // means one of them stopped checking. It is checked because the pieces are
                // numbered by pages the reader can see and an imported page is not one of
                // those: a save that went ahead would write a set of files whose contents
                // no summary in this program ever described.
                *error = tr("这份文档里既有合并进来的页，又说了要拆分，这两样凑不成一份读者能核对的"
                            "文件。请先撤销其中一步。");
                return false;
            }
        }
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
    const auto stagePathFor = [&](int piece) {
        // The piece number is in the name, and that is not decoration: every piece is
        // staged before any of them is renamed, so a name that two pieces could share
        // would have the second writer truncate the first one's finished file. The counter
        // below steps over what is already on disk and stops at 99, which on its own is
        // exactly the collision it describes -- it is a 每 1 页一份 of a 200-page book,
        // which is a thing readers do ask for.
        QString staged
            = directory.filePath(QStringLiteral("xyzpdf-save-%1-%2.pdf").arg(stamp).arg(piece));
        for (int attempt = 2; QFileInfo::exists(staged) && attempt < 100; ++attempt) {
            staged = directory.filePath(
                QStringLiteral("xyzpdf-save-%1-%2-%3.pdf").arg(stamp).arg(piece).arg(attempt));
        }
        return staged;
    };

    // The files the merges draw from, opened here and held open until the last byte is
    // on disk. They have to outlive the replay: a page copied out of another document
    // keeps pointing at that file for its stream data, and it is the writer, not the
    // copy, that reads it (QPDF.hh says so under copyForeignObject). Opening them all
    // up front rather than one at a time inside the replay also means a file that went
    // away or changed shape is answered before anything is staged, and that two merges
    // of one file copy from one handle -- which is what qpdf's copy cache expects.
    // A std::vector rather than a QVector: this holds unique_ptrs, and Qt's list asks
    // for a copyable element the moment a non-const operator[] or detach touches it.
    std::vector<std::unique_ptr<QPDF>> foreign(size_t(imports.size()));
    for (const Operation &op : std::as_const(pending)) {
        if (op.kind != Operation::Kind::MergeFile || op.importIndex < 0
            || size_t(op.importIndex) >= foreign.size())
            continue;
        if (foreign[size_t(op.importIndex)])
            continue;
        const Import &import = imports.at(op.importIndex);
        auto source = std::make_unique<QPDF>();
        try {
            source->processFile(import.path.toUtf8().constData());
        } catch (const std::exception &e) {
            *error = tr("%1 这时候打不开了，所以这些修改写不出来。原来的文件没有改动。\n\n"
                        "解析器说：%2")
                         .arg(QFileInfo(import.path).fileName(), QString::fromUtf8(e.what()));
            return false;
        }
        const int nowPages = int(QPDFPageDocumentHelper(*source).getAllPages().size());
        if (nowPages != import.pageCount) {
            // Not clamped and not carried on with: the summary the reader acted on
            // named pages of a file that is no longer that file, and writing a
            // document that has fewer of them than the sentence promised is the silent
            // kind of wrong this class keeps refusing.
            *error = tr("%1 在这些修改记下来之后变了：当时是 %2 页，现在只有 %3 页。"
                        "请撤销那次合并，重新选一遍页码。")
                         .arg(QFileInfo(import.path).fileName())
                         .arg(import.pageCount)
                         .arg(nowPages);
            return false;
        }
        foreign[size_t(op.importIndex)] = std::move(source);
    }

    // One piece's bytes, from the source read again off disk. A split runs this once per
    // piece, which is once per parse of the whole file -- docs/store-submission.md §20
    // measures what that costs rather than guessing. It is not done by taking one replayed
    // document and cutting its page tree down N times, because what a piece needs taken out
    // is exactly what removePage() already does for 删除页面, and a page tree cut by hand has
    // to push down the attributes that sit on the intermediate /Pages nodes -- /MediaBox
    // among them -- before any leaf can stand on its own. That is how a hand-cut split
    // opens on blank pages, and it is not a class of wrong this class writes on purpose.
    const auto writeOne = [&](const PieceRange &keep, const QString &stagedPath) {
        try {
            // The replay: the source read again from disk, then every pending operation
            // in the order the reader asked for them. This is where an original page
            // number becomes a position in a document that has already shrunk, which is
            // the whole reason `present` is carried along.
            std::unique_ptr<QPDF> document = std::make_unique<QPDF>();
            document->processFile(sourcePath.toUtf8().constData(),
                                  sourcePassword.isEmpty() ? nullptr
                                                           : sourcePassword.toUtf8().constData());
            QVector<PageRef> present = openedDocumentInOrder(originalPages);
            for (const Operation &op : std::as_const(pending))
                apply(op, *document, present,
                      op.importIndex >= 0 && size_t(op.importIndex) < foreign.size()
                          ? foreign[size_t(op.importIndex)].get()
                          : nullptr);
            if (present != live) {
                // The two ways of working out what the saved file holds -- the in-memory
                // list and the replay over the real page tree -- have to agree before any
                // bytes are promised to the reader. They run the same planMerge()/planMove()
                // functions for that reason, and this is the check that says so: a
                // disagreement means one of them stopped reading the same list, and the
                // file about to be written would not be the document the dialogs described.
                *error = tr("这些修改在内存里是一种页序，写到文件时成了另一种，所以这一份没有写。"
                            "请把这种情况报上来。");
                return false;
            }

            if (keep.count() < present.size()) {
                // Everything outside this piece's run of pages. Highest position first and
                // one snapshot of the tree, for the two reasons 删除页面 already documents:
                // a removal shifts the index of everything after it, and re-asking
                // getAllPages() between removals numbers the pages against a tree that has
                // already changed under the handles.
                QPDFPageDocumentHelper helper(*document);
                const std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
                for (int at = int(pages.size()) - 1; at >= 0; --at) {
                    if (at < keep.first || at > keep.last)
                        helper.removePage(pages.at(size_t(at)));
                }
            }

            // Nothing here sets encryption parameters, so the writer's default
            // (preserveEncryption = true) carries the original's /Encrypt dictionary --
            // including its /P permission bits -- into the copy. That default is off
            // whenever prefiltering, content normalization or QDF mode is selected, and
            // off the moment setR6EncryptionParameters is called, so a future line that
            // adds one of those quietly turns 删页 into a tool that hands out unprotected
            // copies of protected documents. tests/documenteditor_smoke.cpp compares the
            // bits on both files for exactly that reason -- and under a split it compares
            // every piece, since "the copy" is now N of them.
            QPDFWriter writer(*document, stagedPath.toUtf8().constData());
            writer.write();
        } catch (const std::exception &e) {
            // Nothing usable came of the attempt, so nothing of ours is left behind.
            QFile::remove(stagedPath);
            *error = tr("写出文件时出错，原来的文件没有改动。\n\n解析器说：%1")
                         .arg(QString::fromUtf8(e.what()));
            return false;
        }
        return true;
    };

    // Every piece is built before the first one is moved into place: a piece that cannot be
    // written then stops the save with nothing renamed, so the reader is never left with
    // three of eight files in the folder and no sentence to say which three.
    struct Staged
    {
        QString from;
        QString to;
    };
    QVector<Staged> built;
    for (int index = 0; index < pieces.size(); ++index) {
        const QString stagedPath = stagePathFor(index);
        if (!writeOne(pieces.at(index), stagedPath)) {
            for (const Staged &one : std::as_const(built))
                QFile::remove(one.from);
            return false;
        }
        built.append({stagedPath, destinations.at(index)});
    }

    // One call per file, not "remove then rename": removing first would leave a window in
    // which the reader has neither file, and a failure inside that window would
    // lose a document it already had. QFile::rename is not enough here -- it
    // refuses to step on a file that is already there, and the save dialog has just
    // asked the reader whether this name was meant to overwrite that file.
    QStringList leftBehind;
    for (const Staged &one : std::as_const(built)) {
        const std::wstring from = QDir::toNativeSeparators(one.from).toStdWString();
        const std::wstring to = QDir::toNativeSeparators(one.to).toStdWString();
        if (MoveFileExW(from.c_str(), to.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
            == 0) {
            // From here on a complete staged file is never thrown away by this
            // function: the failure that leaves one behind -- the destination locked by
            // another program -- outlasts any dialog this could put up, and the
            // alternative is deleting work that took real time to produce.
            leftBehind << tr("新文件已经写好，但没法放到 %1 那里去——它可能被别的程序占着，"
                             "或者这个名字不允许覆盖（Windows 错误 %2）。写好的文件留在 %3。")
                              .arg(one.to)
                              .arg(GetLastError())
                              .arg(one.from);
        }
    }
    if (leftBehind.isEmpty()) {
        if (written != nullptr) {
            written->clear();
            for (const Staged &one : std::as_const(built))
                *written << one.to;
        }
        return true;
    }
    // Named piece by piece rather than one summary: the reader has to be able to find each
    // orphaned staged file, and a count does not tell them which names are still free.
    *error = leftBehind.size() == 1 && built.size() == 1
                 ? leftBehind.first()
                 : tr("拆出来的 %1 个文件里有 %2 个没能放到位。放到位的那些已经在文件夹里了，"
                      "下面每一条说的是剩下的该去哪儿找。")
                       .arg(built.size())
                       .arg(leftBehind.size())
        + QLatin1Char('\n') + leftBehind.join(QLatin1Char('\n'));
    return false;
}
