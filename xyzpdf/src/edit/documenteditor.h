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
// Every operation is structural -- which pages exist, where they came from, in what
// order, turned which way. That is the first wave of M5 (docs/PLAN.md §4: 结构编辑先上
// QPDF，这些不碰页面内容流，风险最低), and it is why the implementation is QPDF rather
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
// ---- why a page is a pair of numbers, not one ----
//
// 合并文件 is what forced `PageRef`. Until it, one number identified a page: the
// operations were delete, rotate and move, all of which only ever rearrange pages
// that are already there, and the reader is looking at the file as it was opened, so
// "第 7 页" meant one thing in every sentence and in every operation.
//
// Merging brings in pages that were never in that file, and after that the two
// questions a reader could mean are different answers -- the 7th page on the screen,
// or the 7th page of the file a save would write. This program keeps numbering
// everything by the screen: every range typed into every dialog addresses the opened
// document, which is the only document a reader can see. So pages that came from
// another file carry a number from *their own* file, and `source` says which merge
// put them here.
//
// The consequence worth stating out loud, because it is a limit rather than a
// detail: an imported page has no number a reader can type. It can be moved to the
// front or the back and it can be taken away by undoing the merge that brought it,
// but it cannot be named in a 删除页面 or 旋转页面 box -- those pages are not on the
// screen. A save writes them all the same.
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
// ---- why a split is in this list, and what it changes ----
//
// Every other operation here changes which pages exist or where they sit. 拆分文件
// changes neither: the document stays one document on screen, with the same page count
// and the same order, and `live` is not rearranged by it at all. What it changes is how
// many files a save writes and what they are called.
//
// That is a different kind of thing to have in an undo stack, so it is worth saying why
// it still belongs here rather than in a "split now" button: a split that wrote its
// pieces straight to disk could not be undone, and `undo()` here is only ever a move
// between two in-memory lists -- it has no power to take back a file that already
// exists. Deferred to the save, the reader can change their mind about the piece size as
// many times as they like, and each 撤销 genuinely undoes the whole of what it promised,
// because nothing had been written yet.
//
// The consequence is that a split is *inert* until 保存修改为: the tab says 拆成每 5 页
// 一份 and that is the whole of its visible effect.
//
// ---- the second inert operation, and the one thing measurement took off the table ----
//
// 加密 / 去掉口令 is inert the same way, for the same reward: it changes no page, so
// `live` is not rearranged by it, `rebuildLive()` has no branch for it, and undoing it is
// taking one entry out of a list -- no file is rewritten backwards, because no file was
// written at all. What it changes is the set of parameters the one writer at the end of a
// save is handed.
//
// Measurement is what shapes this one, and one number in particular took a feature off
// the table (tests/encryptprobe.cpp, run three times): QPDF does not know which password
// unlocked a file. Open the same document by its user password and by its owner password
// and `isEncrypted(R, P)` plus all six permission flags read back identically, so nothing
// downstream can ask "did this session come from the owner?". The consequence for the
// interface is that it must not promise what only an author could do -- there is no
// "enter the owner password to lift these restrictions", because there is no such
// question. Anything that can open the file can also have the restrictions taken off on
// the way out, and can have different ones put on. That is why the dialog says the
// permission bits are advisory, and why a sentence in the docs has to carry it too.
//
// What is *not* a measurement artefact: the writes themselves. Stripping the protection,
// putting a new password on, and putting new bits on all succeed through the same writer
// call a 删页 already uses, they work while a page edit is in the same save (so 解密 and
// 编辑 share one file), and a file written with R6/AES-256 opens in this program's own
// reader with the password and not without it. Stripping is also *cheaper* than what a
// save does today, which carries the source's encryption through and re-encrypts every
// stream on the way out: 600 pages measured 72-80 ms carried, 45-50 ms stripped.
#pragma once

#include <QString>
#include <QVector>

class QPDF;
class QDir;

// One document being modified. Owned by the tab whose reader is editing it, for
// as long as that document is open, because the operations below pile up across
// dialog boxes: one of these holding a single edit is what made 撤销 impossible.
class DocumentEditor
{
public:
    // Where a block of pages lands. Said as a target rather than as an index
    // because an index is a position in a document the reader cannot see: after a
    // delete, "position 7" is not the page they are pointing at, while "after page
    // 7" is exactly what is on the screen in front of them.
    enum class MoveTarget
    {
        Front,
        Back,
        AfterPage
    };

    // Which document a page came from, and where inside it. See the header note:
    // this is a pair rather than one number because 合并文件 puts pages into the saved
    // file that the screen never showed.
    //
    // `source` names the *merge that brought the page in*, not the file it came from
    // (Operation::mergeId), and 0 is the document that was opened. A file index would
    // be one shorter, and would be wrong: merging the same page twice has to produce
    // two pages with two identities, or the second copy cannot be told apart from the
    // first when a later move asks where it is.
    struct PageRef
    {
        // Which merge brought this page in, or 0 for a page of the document this editor
        // was opened from. Every page number a reader types names a page of that file,
        // so a non-zero source is always one a merge put here -- and no two pages in one
        // document share a source and a number.
        int source = 0;
        int page = -1;

        // Same page of same merge. QVector's indexOf()/removeOne() go through this, and
        // they are how a pending operation finds the page it was asked about.
        bool operator==(const PageRef &other) const
        {
            return source == other.source && page == other.page;
        }
    };

    // A file some operation took pages out of. Entries are never removed, even when
    // the merge that added one is undone: operations point at these by index, and an
    // index that shifted under them would silently re-point an undo at a different
    // file.
    struct Import
    {
        QString path;
        int pageCount = 0;
    };

    // What a save should do about protection. Plain data on purpose: the dialog fills one
    // of these from its boxes, the pending list holds it, and the save is the only place a
    // number in it reaches a QPDFWriter. The six flags are the ones PDF writes into /P --
    // the mapping to qpdf's seven arguments (its `accessibility` is always allowed and is
    // not offered here, and printing is a level rather than a box) lives in saveAs(), once.
    //
    // The password is kept exactly as it was typed, and it is never put into a summary, a
    // menu row or a status line -- see describeOperation().
    struct Protection
    {
        enum class What
        {
            // Nothing was asked for: write the source's own /Encrypt through, which is
            // what every save has done since §11 and what the tests pin the bits of.
            Carry,
            // Write the file with no protection at all, whatever the source said.
            Decrypt,
            // Write it with this password and these bits.
            Encrypt
        };
        What what = What::Carry;

        // One password, not the PDF's pair. PDF keeps a user password (needed to open) and
        // an owner password (nominally needed to lift the restrictions), and the interface
        // could ask for both -- but measurement says the distinction is not one this program
        // can offer: QPDF never records which of the two unlocked a file, so a session that
        // opened a document by its user password can write it out unrestricted, and can write
        // out *different* restrictions, with no further key (tests/encryptprobe.cpp). Asking
        // the reader for a second password would sell a door that does not exist, and the
        // file this writes uses the same string for both, which is what most tools that ask
        // for "a password" do.
        //
        // Empty means "no password to open" -- a file anybody can read and which writes down
        // that editing is not allowed. That is a real request, and it is the shape most
        // restricted PDFs in the wild have.
        QString password;

        bool allowPrint = true;
        bool allowExtract = true;
        bool allowAssembly = false;
        bool allowAnnotate = true;
        bool allowFormFilling = true;
        bool allowModifyOther = false;

        // Asked for the same thing again? It is how a second 加密 becomes "change the
        // number" rather than a second operation to undo, the same way a second split is.
        bool operator==(const Protection &other) const
        {
            return what == other.what && password == other.password
                && allowPrint == other.allowPrint && allowExtract == other.allowExtract
                && allowAssembly == other.allowAssembly
                && allowAnnotate == other.allowAnnotate
                && allowFormFilling == other.allowFormFilling
                && allowModifyOther == other.allowModifyOther;
        }
        bool operator!=(const Protection &other) const { return !(*this == other); }
    };

    // One structural change, as it waits in the pending list.
    struct Operation
    {
        enum class Kind
        {
            DeletePages,
            RotatePages,
            MovePages,
            MergeFile,
            // 拆分文件: how many pages go in each written file. It is the one kind that
            // changes no page -- see the header note.
            SplitBySize,
            // 加密 / 去掉口令: the second kind that changes no page. Like a split it only
            // reaches what a save writes -- a split changes how many files come out, this
            // changes what each of them requires to open.
            Protect
        };
        Kind kind = Kind::DeletePages;
        // Delete/Rotate/Move: original page numbers -- positions in the document as
        // it was opened, which is what the reader is looking at. See the header note.
        // MergeFile: page numbers within `imports[importIndex]`, which is the file
        // the reader was looking at while choosing them.
        QVector<int> pages;
        // RotatePages only: quarter turns to add clockwise, 1 for 右转 90°, 2 for
        // 转 180°, 3 for 左转 90°. PDF stores rotation as /Rotate, which is
        // clockwise, so the direction the reader chose has to be said this way
        // once, here, rather than in two places.
        int quarterTurns = 1;
        // MovePages and MergeFile only: where the block goes.
        MoveTarget moveTo = MoveTarget::Back;
        // MovePages and MergeFile with MoveTarget::AfterPage only: the original
        // number of the page the block lands behind -- a page of the opened document
        // in both cases, so that a merge can be pointed at a page the reader can see.
        // Kept as a number rather than resolved to a position at the time it was
        // asked for, because the page it names may be one a later undo brings back --
        // see planMove().
        int anchor = -1;
        // MergeFile only: which entry of `imports` names the file, which is what a
        // save opens to copy out of and what the undo menu says.
        int importIndex = -1;
        // MergeFile only: this merge's own number, and the `source` every page it
        // brings in carries. It is not the file index: a reader can merge the same page
        // of the same file twice, and the two copies have to be tellable apart by
        // anything that looks for one of them later. Assigned once, never reused, so
        // undo and redo move an operation around with its identity intact.
        int mergeId = -1;
        // SplitBySize only: how many pages each output file gets. Nothing is pruned or
        // reordered by it -- see the header note -- so this is the only thing a save has
        // to know to turn one destination into several.
        int pieceSize = 0;
        // Protect only: what a save should hand the writer about protection. Nothing
        // changes about the pages themselves -- see the header note.
        Protection protection;
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
        // What the document would have left, and note that once a merge is pending
        // that number counts pages the screen has never shown. The dialog has to say
        // so out loud -- see importedPageCount() -- because "剩下 40 页" next to a
        // 40-page document that is losing three is otherwise a sentence the reader
        // can check and find wrong.
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
    // `live` is which pages the saved document would have, in order, as `PageRef`s;
    // the member below is the one anyone outside this class calls.
    static DeletePlan planDelete(const QVector<PageRef> &present, const QVector<int> &pages);
    DeletePlan planDelete(const QVector<int> &pages) const;

    // Which of these page numbers still name a page in the document as it
    // stands. The rotate dialog needs it for exactly what the delete dialog
    // needs planDelete() for: the count it shows has to be the count the editor
    // will act on.
    int pagesStillThere(const QVector<int> &pages) const;

    // What a move request means for the document as it stands: the order a save
    // would write, and the three ways the request can be one that moves nothing.
    //
    // This is also the only place the reordering is worked out, which is why
    // rebuildLive() and the replay in saveAs() both go through it: the sequence a
    // reader was promised in the dialog and the sequence that reaches the file
    // cannot then be two different answers, and a reorder is exactly the operation
    // where being off by one is silent -- it writes a document with every page in
    // it, just not the one anybody asked for.
    struct MovePlan
    {
        // Which pages the saved document would have, in the order it would have
        // them.
        QVector<PageRef> order;
        // Pages named and still there, and pages named that an earlier unsaved
        // change already took out. Same split planDelete() makes, for the same
        // reason: "you named nothing" is the wrong sentence for "you named pages
        // this list has already moved out of the way".
        int pagesMoved = 0;
        int alreadyGone = 0;
        // Where the block starts in `order`, 0-based. Its own number because the
        // dialog's whole point is to say where the pages end up, and the block is
        // always contiguous -- a move takes the named pages along in the order
        // they are in now, so "第 38-40 页" is a true statement about the result.
        int landsAt = 0;
        // The anchor the request resolves to: -1 for 最前/最后 and for "the named
        // page is gone and nothing follows it", which is a move to the end.
        int anchorUsed = -1;
        // The page named as the anchor is one of the pages being moved, so the
        // request is "put this block behind itself".
        bool anchorIsMoved = false;
        // The page named as the anchor is one an earlier unsaved change deleted.
        // The move still goes ahead -- into the gap that page left -- but the
        // reader has to be told which page it actually lined up against.
        bool anchorWasGone = false;
        // Whether the order below differs from the order the document has now.
        bool changesOrder = false;
        // Whether OK is pressable and the request is worth recording.
        bool usable() const { return pagesMoved > 0 && !anchorIsMoved && changesOrder; }
    };
    static MovePlan planMove(const QVector<PageRef> &present, const QVector<int> &pages,
                             MoveTarget to, int anchor = -1);
    MovePlan planMove(const QVector<int> &pages, MoveTarget to, int anchor = -1) const;

    // What a merge request means: the pages that would be added, and where they
    // would land.
    //
    // The landing spot is worked out by the same function planMove() uses, which is
    // the whole point of this struct's existence: a merge asked for "behind page 12"
    // has to mean the same place the reorder means by that, and two answers to "where
    // does this block go" is how a file ends up with a page order nobody was shown.
    struct MergePlan
    {
        QVector<PageRef> order;
        // Pages this would put into the document. Nothing here can make a merge
        // request name zero pages -- the dialog bounds its range box by the file's
        // own page count -- so this has no alreadyGone counterpart, and a file whose
        // page count changed underneath the dialog is refused rather than clamped.
        int pagesAdded = 0;
        int landsAt = 0;
        int anchorUsed = -1;
        bool anchorWasGone = false;
        bool usable() const { return pagesAdded > 0; }
    };
    static MergePlan planMerge(const QVector<PageRef> &present, const QVector<int> &pages,
                               int mergeId, MoveTarget to, int anchor = -1);

    // What a split would write: the pieces, in order, each one a run of pages of the
    // document as a save would write it.
    //
    // This is the single answer to "which pages go in which file", and both ends call it
    // -- the dialog's summary and the save that lays the files down -- for the same
    // reason planMove() is one function: a reader told "8 份，最后一份 3 页" who gets
    // seven files was lied to by a second implementation of the same arithmetic.
    //
    // Note what the numbers are numbering: positions in the document a *save writes*, not
    // in the file on the screen. Those are the same thing until an edit is pending, and
    // 删除页面 leaves the view alone (docs/store-submission.md §12), so a dialog that
    // shows these ranges has to say which numbering it is using.
    struct PieceRange
    {
        // 0-based, inclusive at both ends, like every page number inside this class.
        int first = 0;
        int last = -1;
        int count() const { return last - first + 1; }
    };
    // `pieceSize` of 0 means no split is pending and comes back as one piece holding
    // everything, which is what lets the save loop over pieces without a special case for
    // "and also the case where nothing was split".
    static QVector<PieceRange> pieceRanges(int total, int pieceSize);

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
    // calling this twice on one editor safe. That includes the list of files earlier
    // merges took pages out of: those indices are what the operations point at, so
    // they belong to one document's list and to no other.
    bool open(const QString &path, const QString &password, QString *error);

    QString path() const { return sourcePath; }

    // Pages in the file as it is on disk. The page ranges a reader types are
    // numbered against this, because it is what they can see.
    int sourcePageCount() const { return originalPages; }

    // Pages a save would write: the source, minus whatever the pending operations
    // take out, plus whatever a merge has put in. Re-read from the list rather than
    // cached, so it cannot disagree with the operations.
    int pageCount() const { return live.size(); }

    // How many of those came from another file. The dialogs need this because their
    // own page counts are about the document a save would write, which stops being
    // the same number as the one on the screen the moment a merge is pending -- and a
    // reader who is told "剩下 40 页" while looking at 40 pages that are losing three
    // has to be told where the difference came from.
    int importedPageCount() const;

    // The pages a save would write, in the order it would write them. Read-only, and
    // here because one dialog needs more than the counts above: 合并文件进来 can be
    // asked for several files at once, and the summary it shows has to say where the
    // whole batch lands -- which is planMerge() run over this list, row after row, the
    // same way a save replays them. Anything else would be two answers to "第几页".
    QVector<PageRef> pageOrder() const { return live; }

    // Whether a file can be merged into this document, and how many pages it has.
    // The dialog calls this the moment a name is chosen, because the range box has to
    // be bounded by the file's own page count rather than by whatever the reader
    // guesses -- and because a document that needs a password to open is refused here,
    // in front of the reader who still has time to go and type that password
    // somewhere else, rather than after they have chosen a destination for the merge.
    static bool inspectImport(const QString &path, int *pageCount, QString *error);

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
    // Moving a block of pages to the front, to the back, or behind `anchor` (an
    // original page number, used only with MoveTarget::AfterPage). The named pages
    // travel together and keep the order they have among themselves, which is what
    // makes "第 2-4 页移到最后" one action rather than three.
    bool movePages(const QVector<int> &pagesToMove, MoveTarget to, int anchor, QString *error);
    // Copying pages out of another file into this document, to the front, to the
    // back, or behind `anchor` -- where `anchor` is a page of *this* document, the
    // same numbering every other dialog here uses, because it is the only one a reader
    // can check. `pages` are page numbers within `path`, which is the file the reader
    // was looking at while choosing them.
    //
    // The file is looked over again here rather than trusted from the dialog: it may
    // have gone away, been replaced, or shrunk since the range was typed, and a merge
    // that quietly takes fewer pages than the summary promised is the silent kind of
    // wrong.
    bool mergeFile(const QString &path, const QVector<int> &pages, MoveTarget to, int anchor,
                   QString *error);
    // Recording that a save should write this document in pieces of `pagesPerPiece`
    // pages. Nothing is written and no page moves: see the header note on why a split is
    // in this list at all.
    //
    // Refused while a merge is pending. A merged-in page carries a number from the file it
    // came from, not one the reader can see on the screen (docs/store-submission.md §17),
    // so "第 6-10 页" would name pages of one file and pages of another, and which piece
    // such a page belongs to has no answer anybody could check. Refusing is the honest
    // answer, and it is the same limit every other dialog here already has.
    bool splitInto(int pagesPerPiece, QString *error);

    // The piece size a save would honour: the last split in the pending list, since a
    // second one is the reader changing their mind rather than a second split to carry
    // out. 0 when no split is pending.
    int splitPieceSize() const;
    // The pieces a save would write, which is one piece over the whole document when
    // nothing is split. `pieceCount()` is that list's length.
    QVector<PieceRange> pieces() const;
    int pieceCount() const { return int(pieces().size()); }

    // The name one piece gets beside the destination a reader chose: `report.pdf` split
    // into three comes out as `report 01 (1-5).pdf` and so on, in the same folder. Static
    // and public for the reason pieceRanges() is -- a name worked out in two places is a
    // file nobody was told about. The tests read the folder back through this too, so what
    // is checked is the naming and not a copy of the pattern.
    //
    // The index is zero-padded because the folder gets sorted by name, and a piece list
    // that reads 1, 10, 11, 2 in Explorer is a piece list whose pages arrive shuffled.
    static QString piecePath(const QString &destination, int index, int total,
                             const PieceRange &range);

    // A name a save stages one piece under, beside the destination and before any of the
    // set is moved into place: `<directory>/xyzpdf-save-<stamp>-<piece>.pdf`, with a
    // counter stepped past whatever is already on disk. Not a name the reader typed or ever
    // sees, except in the sentence that names a file left behind after a refused write.
    //
    // Static and public for the reason pieceRanges() is: "the path this hands back never
    // has a file behind it" is the one promise that keeps a second writer from truncating
    // the first piece's finished document, and a test can only hold a function to it if the
    // function is reachable.
    static QString stagedPath(const QDir &directory, qint64 stamp, int piece);

    // Recording what a save should do about protection. Nothing is written, no page
    // changes, and the source file keeps whatever it has: see the header note on why this
    // kind belongs in a list that undo walks through.
    //
    // Refused when the request is the answer a save would already give -- that is not a
    // change, and dirtying the tab for one would put an undo row on the menu for nothing.
    bool protectWith(const Protection &protection, QString *error);
    // Taking the protection off, which is the same operation with the other sign on it: it
    // is refused when there is nothing to take off, and a pending 加密 can be cancelled
    // this way too (that is what makes the pair symmetric enough to be one dialog).
    bool removeProtection(QString *error);

    // The protection a save would write: the last Protect in the pending list, since a
    // second one is the reader changing their mind rather than a second thing to carry out
    // -- the same rule splitPieceSize() follows. What::Carry when nothing is pending.
    //
    // One function for the same reason pieceRanges() is one: the dialog's summary and the
    // writer's parameters must not be two answers to "what will this file require".
    Protection protectionToWrite() const;

    // The password the file a save just wrote will ask for, or empty when it will ask for
    // none. Read by the tab that opens the result: a document written with a new key is not
    // openable with the old one, and a Carry save re-encrypts with the key the source was
    // opened by -- so this is not the same question as protectionToWrite().password, which
    // is empty for a Carry that still needs a key.
    //
    // Nothing here stores a password anywhere it is not needed: it lives in this object for
    // as long as the tab holds the document (a reload has to be able to open it again) and
    // is written to no setting, no log and no summary.
    QString passwordOfWrittenFile() const;

    // Whether the file on disk has a /Encrypt dictionary of its own, and what its bits
    // say. The dialog needs both: 去掉口令 has nothing to act on when the first is false,
    // and the six boxes have to open on what the document actually says rather than on
    // whatever this program defaults to. Computed once, in open().
    bool sourceIsProtected() const { return sourceEncrypted; }
    Protection sourceProtection() const { return sourceProtect; }

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
    // onto it in the order it was asked for -- including the merges, which means
    // opening each file a merge drew from and copying pages out of it here, at the
    // moment there is somewhere to put them. Refuses `path` when it names the
    // document this was opened from or any of the files being merged in: both are
    // documents somebody has open, and one of them is being read while this writes.
    // Returns false with a sentence when nothing was written.
    //
    // When a split is pending, `path` is the name the pieces are cut from rather than the
    // file that gets written, and this expands it into one name per piece (piecePath()).
    // The pre-flight differs from the single-file case in one direction on purpose: the
    // save dialog has asked the reader about overwriting the *one* name they typed, and
    // about nothing else, so a piece whose name is already taken refuses the whole save.
    // The files are also all staged before any of them is moved into place, so a piece
    // that cannot be built leaves no half-set of outputs behind.
    //
    // A successful save leaves the list alone. The file this editor is attached to
    // is still the untouched original, so the operations are still pending against
    // it, and the tab keeps saying so -- the honest alternative, marking them
    // saved, would claim that the document on screen had changed.
    // The files that came out, in the order they were written, which is the piece order.
    // `written` is optional because it is only interesting for one thing: under a split the
    // caller has to know that the name it handed over is not a file that now exists. On a
    // success this holds one path when nothing was split, so there is no case where the
    // answer is "several files, and here is a name that is none of them".
    bool saveAs(const QString &path, QString *error, QStringList *written = nullptr);

private:
    // Which pages the pending list says are still in the document. Recomputed from
    // scratch rather than patched, because that is the one way `live` cannot drift
    // away from `pending` -- and after an undo, a drift is a page count that
    // disagrees with the operations that produced it.
    void rebuildLive();
    // The one way an operation enters the list: appended, the redo branch dropped,
    // and the page list retaken from what the list now says.
    void record(const Operation &op);
    // Where a block lands among the pages that are already here: the same arithmetic
    // planMove() and planMerge() both need, and the reason it is one function is the
    // reason planMove() itself is -- "第 12 页之后" has to mean one place.
    //
    // `without` is the list to land in, with the block that is going to be inserted
    // taken out of it. The anchor names a page of the opened document, so the search
    // for "the page it was behind" ignores imported pages entirely: they have no
    // number the reader can see, so a landing spot guessed from them would be a spot
    // nobody could check. When the named page is gone, the block goes in front of the
    // first page whose number is larger, which is what planMove() already said about
    // the gap a delete leaves.
    struct Landing
    {
        int at = 0;
        int anchorUsed = -1;
        bool anchorWasGone = false;
    };
    static Landing landingSpot(const QVector<PageRef> &without, MoveTarget to, int anchor);
    // One operation, applied to a document being replayed. `present` is the book
    // keeping: the pages still in the document, in their current order.
    //
    // Not static, because a merge copies pages out of another file: `source` is that
    // file, already open and kept open for the whole save. The pages this copies from
    // it are not usable before the writer has run, which is why the save -- not the
    // operation -- owns the lifetime; see the note in saveAs().
    void apply(const Operation &op, QPDF &document, QVector<PageRef> &present,
               QPDF *mergeSource);
    // Which of `pages` are in `present`, as positions within `present`.
    static QVector<int> positionsOf(const QVector<PageRef> &present, const QVector<int> &pages);
    // A few words for one operation: "删除 3 页". A member rather than a helper in
    // the anonymous namespace, and the reason is the catalogue: lupdate only puts a
    // `tr()` call into a context when it can see which class it sits in, so as a
    // free function these four sentences were compiled out of the translation and
    // the English menu would have shown Chinese undo names straight out of the
    // source. Tests reach them through undoDescription().
    QString describeOperation(const Operation &op) const;

    QString sourcePath;
    QString sourcePassword;
    // What the file on disk says about protection, read once in open() beside the page
    // count. Not re-derived at save time: by then the reader may have asked for a change,
    // and "what it was" has to stay answerable to show them what they are about to undo.
    bool sourceEncrypted = false;
    Protection sourceProtect;
    int originalPages = 0;
    // Which pages are still present, in the order they would be written. An identity
    // list of the opened document's own pages after open(); deletions take entries
    // out of it, moves rearrange entries inside it, and merges splice in pages from
    // another file -- all three of them by replaying the whole list rather than
    // patching this one.
    QVector<PageRef> live;
    // Files that merges took pages out of, in the order they were first merged. An
    // operation names one by index, and the indices never shift, so undoing a merge
    // leaves the entry in place for whichever operation points at it next.
    QVector<Import> imports;
    // The number the next MergeFile operation gets, and with it the `source` of the
    // pages it brings in. Rising, never reused -- see Operation::mergeId. 0 is left
    // out because it already means "the document that was opened".
    int nextMergeId = 1;
    QVector<Operation> pending;
    QVector<Operation> undone;
};
