// What the file says about itself: the document properties.
//
// ---- why this exists as its own dialog ----
//
// docs/PLAN.md M2 asks for it, and until this slice it was the one thing in
// M0-M3 that was built everywhere except on screen: DocMetadata was written,
// the backend's metadata() filled it in, document_smoke checked it, and no
// window ever showed it to anybody (docs/review-03.md §3.1).
//
// Read-only, and a dialog rather than a panel: nothing here is decided, it is
// consulted -- usually to answer "what made this file" when a page renders
// oddly, which is why 生成器 and 创建者 are shown separately rather than being
// folded into one line.
//
// ---- why the values are selectable ----
//
// The two interesting fields are long machine strings ("Microsoft® Word for
// Microsoft 365", "GPL Ghostscript 10.02.1"), and the thing a reader does with
// them next is paste them into a search. A label nobody can copy out of turns
// that into transcription.
#pragma once

#include "../document/docmetadata.h"

#include <QDialog>
#include <QSizeF>
#include <QString>

class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    // Everything the dialog shows, gathered by the caller.
    //
    // Passed in rather than read here, because the document lives behind the
    // tab and every call into it takes the same global PDFium lock the renders
    // do: the tab asks its own questions once, on the thread that owns them,
    // and this only lays the answers out.
    struct Info
    {
        QString path;
        qint64 fileBytes = 0;
        int pageCount = 0;
        // Which page the size below belongs to, zero-based. The page rather
        // than the document, because a PDF is free to mix sizes and a single
        // "page size" row would then be picking one of them and not saying so.
        int sizedPage = 0;
        QSizeF pagePoints;
        DocMetadata metadata;
        // How many digital signatures the file carries, read once at open
        // (Document::signatureCount). Zero is the ordinary file, and the row is left
        // out for it rather than reading （无）: "no signatures" is the unremarkable
        // answer and does not deserve a line that makes the empty metadata fields
        // beside it ambiguous.
        int signatureCount = 0;
        // How many of them the checker answered 有效. Negative means the check has not answered
        // at all yet, which is a different thing from zero: the row says "N 个" alone rather than
        // "N 个，0 个有效", because the latter would report two failed signatures on a document
        // whose verification is still running.
        int validSignatureCount = -1;
    };

    PropertiesDialog(QWidget *parent, const Info &info);
};
