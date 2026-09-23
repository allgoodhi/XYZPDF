// Changing what a PDF is, as opposed to reading one.
//
// ---- why this is a second type and not more methods on Document ----
//
// docs/PLAN.md §7.3 settles it: the read interface and the write interface are
// two classes, and the write path must not be reachable through the read-only
// one. src/document/document.h says the same from the other side ("Editing is
// not here at all and must not be"). The reason is the rule AGENTS.md states in
// bold -- 永不写用户的 PDF 文件. A `Document` handle is what the page view, the
// thumbnails, the outline panel, the preview pane and the thumbnail provider all
// hold; none of them should be able to ask it to overwrite a file.
//
// ---- what an edit is, and where it reaches disk ----
//
// Nothing here touches a page's content stream: no drawing, no text changes.
// Every operation is structural -- which pages exist, in what order, turned
// which way. That is the first wave of M5 (docs/PLAN.md §4: 结构编辑先上 QPDF，
// 这些不碰页面内容流，风险最低), and it is why the implementation is QPDF rather
// than something built on the renderer.
//
// Changes are held as a list of operations and reach the disk only through
// saveAs(), which is §7.3's 显式保存. There is no save() that writes back to the
// path this was opened from, and that is not an oversight: it is the one method
// that could break the rule above, so it is not offered.
//
// ---- why the operation list, and not a modified document, is the state ----
//
// The tempting shape is "parse once, keep the QPDF, mutate it, and undo by
// putting back what was taken out". QPDF makes that expensive on purpose:
// `QPDF(const QPDF&)` and its assignment operator are both deleted, and one QPDF
// is tied to one file for its whole life, so there is no way to park a copy of
// the document on a shelf next to the live one. That leaves per-operation inverse
// operations -- and an inverse operation that is subtly wrong (re-inserting a
// page at the wrong position after a second delete, say) fails silently: it
// produces a document that is quietly not what the reader asked for.
//
// So the truth here is `pending`, a list of what was asked for. Undo and redo are
// moves within that list, which is the only reason they can be trusted; the cost
// is that a save re-opens the source and replays the whole list, which docs/
// store-submission.md §10 measures at tens of milliseconds for a 5 MB book. A
// document is parsed when it is written, never held between edits, and 300 MB of
// scanned pages is not sitting in memory because somebody clicked 撤销.
//
// ---- the page numbers a reader gives are the ones on the screen ----
//
// `live` is what makes this class's one real subtlety survivable. The reader is
// looking at the file as it was opened -- the view does not change (docs/
// store-submission.md §12 says why) -- so "delete page 7" means the page 7 of
// *that*, even when an earlier pending operation has already taken pages out from
// in front of it. Operations therefore store original page numbers, and a save
// works out, as it replays, which position each one has in the document at that
// moment. Numbers a reader never has to think about are the ones they cannot get
// wrong.
//
// ---- why saveAs goes through a temporary file ----
//
// QPDFWriter opens its output before it has written anything, so a write that
// throws halfway through -- a page tree it cannot resolve, a disk that fills in
// the middle of the object stream -- leaves a truncated file at the path it was
// given. (Encryption is not one of those reasons: measured, a document marked
// not-to-modify writes without a word of complaint.) The destination comes out
// of a save dialog that will happily point at a file the reader owns and was
// about to replace. So the new bytes are staged in the destination's own
// directory and moved into place only once they are complete, which is the same
// reason a text editor does it.
#pragma once

#include <QString>
#include <QVector>

class QPDF;

// One document being modified. Owned by the tab whose reader is editing it, for
// as long as that document is open, because the operations below pile up across
// dialog boxes: one of these holding a single edit is what made 撤销 impossible.
class DocumentEditor
{
public:
    // One structural change, as it waits in the pending list.
    struct Operation
    {
        enum class Kind
        {
            DeletePages,
            RotatePages
        };
        Kind kind = Kind::DeletePages;
        // Original page numbers -- positions in the document as it was opened,
        // which is what the reader is looking at. See the header note.
        QVector<int> pages;
        // RotatePages only: quarter turns to add clockwise, 1 for 右转 90°, 2 for
        // 转 180°, 3 for 左转 90°. PDF stores rotation as /Rotate, which is
        // clockwise, so the direction the reader chose has to be said this way
        // once, here, rather than in two places.
        int quarterTurns = 1;
    };

    // What a delete request means for the document as it stands, worked out
    // before anything changes. The dialog shows this while the range is being
    // typed and the editor enforces it again at the moment of the change, so
    // that the number a reader acted on and the number the library acts on cannot
    // come from two places.
    struct DeletePlan
    {
        // Pages actually named and still there: numbers outside the document are
        // dropped, repeats count once, and a page an earlier pending operation
        // already took out cannot be taken out again.
        int pagesToRemove = 0;
        int pagesRemaining = 0;
        // True when the request names every page still present. A PDF with no
        // pages is not a PDF -- no reader opens one -- so this is refused rather
        // than written.
        bool leavesNothing = false;
        // How many of the named pages are already gone. It is worth its own
        // number because the sentence it earns is not "you named nothing": the
        // reader did name pages, and an earlier edit in this same list removed
        // them, which is a thing they need to be told rather than contradicted.
        int alreadyGone = 0;
    };
    // `live` is which of the original pages are still present, in order; the
    // member below is the one anyone outside this class calls.
    static DeletePlan planDelete(const QVector<int> &live, const QVector<int> &pages);
    DeletePlan planDelete(const QVector<int> &pages) const;

    // Which of these page numbers still name a page in the document as it
    // stands. The rotate dialog needs it for exactly what the delete dialog
    // needs planDelete() for: the count it shows has to be the count the editor
    // will act on.
    int pagesStillThere(const QVector<int> &pages) const;

    // Whether two paths name the same file, which is the question saveAs() has
    // to answer before it writes anything. Two paths can differ in every
    // character and name one file (a different case, a slash the other way, an
    // 8.3 short name, a junction), and the cost of calling them different is
    // writing over the reader's own document.
    static bool sameFile(const QString &path, const QString &other);

    DocumentEditor();
    DocumentEditor(const DocumentEditor &) = delete;
    DocumentEditor &operator=(const DocumentEditor &) = delete;
    ~DocumentEditor();

    // Looks the document over: parses it far enough to count its pages and to
    // learn whether this reader's parser can handle it at all, then lets it go.
    // `password` is the open password, empty when the document does not need one:
    // a file that needs one and is given none fails here with a sentence to show,
    // not later after the reader has already chosen a destination.
    //
    // Anything pending from a previous document is dropped, which is what makes
    // calling this twice on one editor safe.
    bool open(const QString &path, const QString &password, QString *error);

    QString path() const { return sourcePath; }

    // Pages in the file as it is on disk. The page ranges a reader types are
    // numbered against this, because it is what they can see.
    int sourcePageCount() const { return originalPages; }

    // Pages a save would write: the source, minus whatever the pending
    // operations take out. Re-read from the list rather than cached, so it cannot
    // disagree with the operations.
    int pageCount() const { return live.size(); }

    // ---- the operations ----
    //
    // Zero-based, like every other number that names a page in this program. Each
    // returns false, and changes nothing, when the request names no page still
    // present -- or, for a delete, when it would leave no pages at all.

    bool deletePages(const QVector<int> &pagesToDelete, QString *error);
    // `quarterTurns` is 1, 2 or 3 clockwise; anything else is refused, because a
    // rotation of 90° is the only kind PDF can record and a dialog that sent 45
    // would otherwise silently become something else.
    bool rotatePages(const QVector<int> &pagesToRotate, int quarterTurns, QString *error);

    // ---- the list, and moving through it ----
    //
    // Undo and redo are moves between `pending` and `undone`, nothing more. A
    // redo list that survives its own undo is the usual convention and is what
    // `undo()` clears it by: recording a new operation after a redo was thrown
    // away means the branch is gone, not waiting somewhere.

    bool isDirty() const { return !pending.isEmpty(); }
    int pendingCount() const { return int(pending.size()); }
    bool canUndo() const { return !pending.isEmpty(); }
    bool canRedo() const { return !undone.isEmpty(); }
    bool undo();
    bool redo();

    // What the next undo or redo would take back, as a few words for a menu:
    // "撤销（删除 3 页）". Empty when there is nothing to undo.
    QString undoDescription() const;
    QString redoDescription() const;
    // Every pending operation in one line, for the tab's title and the status
    // bar: "删除 3 页，向右转 90°（5 页）". Empty when nothing is pending.
    QString pendingSummary() const;

    // Writes the document out: the source, with every pending operation replayed
    // onto it in the order it was asked for. Refuses `path` when it names the
    // document this was opened from. Returns false with a sentence when nothing
    // was written.
    //
    // A successful save leaves the list alone. The file this editor is attached to
    // is still the untouched original, so the operations are still pending against
    // it, and the tab keeps saying so -- the honest alternative, marking them
    // saved, would claim that the document on screen had changed.
    bool saveAs(const QString &path, QString *error);

private:
    // Which pages the pending list says are still in the document. Recomputed from
    // scratch rather than patched, because that is the one way `live` cannot drift
    // away from `pending` -- and after an undo, a drift is a page count that
    // disagrees with the operations that produced it.
    void rebuildLive();
    // The one way an operation enters the list: appended, the redo branch dropped,
    // and the page list retaken from what the list now says.
    void record(const Operation &op);
    // One operation, applied to a document being replayed. `present` is the book
    // keeping: the original numbers still in the document, in their current order.
    static void apply(const Operation &op, QPDF &document, QVector<int> &present);
    // Which of `pages` are in `present`, as positions within `present`.
    static QVector<int> positionsOf(const QVector<int> &present, const QVector<int> &pages);
    // A few words for one operation: "删除 3 页". A member rather than a helper in
    // the anonymous namespace, and the reason is the catalogue: lupdate only puts a
    // `tr()` call into a context when it can see which class it sits in, so as a
    // free function these four sentences were compiled out of the translation and
    // the English menu would have shown Chinese undo names straight out of the
    // source. Tests reach them through undoDescription().
    static QString describeOperation(const Operation &op);

    QString sourcePath;
    QString sourcePassword;
    int originalPages = 0;
    // Which original pages are still present, in the order they would be written.
    // An identity list after open(); deletions take entries out of it, and the
    // reorder operation that M5 still owes will move them around inside it.
    QVector<int> live;
    QVector<Operation> pending;
    QVector<Operation> undone;
};
