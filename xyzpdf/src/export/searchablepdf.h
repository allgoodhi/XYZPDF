// A scan turned into a PDF that reads: the page's words recognized and put back on it
// as invisible text, where the ink is.
//
// ---- which pages ----
//
// The pages that have no text of their own. That is the same question the Word export
// asks before it sends a page in as a picture (pageanalysis.cpp): no visible characters,
// or almost none while pictures cover most of the page. A page with a real text layer is
// left exactly as it was -- OCR over printed text would only put a second copy of the
// words on top of the first, with boxes that disagree with the ones already there. A page
// whose text layer is already invisible (an OCR program did this before) is left alone
// too: it has what this export would add.
//
// ---- what it writes ----
//
// Each recognized word becomes one string of invisible text (text render mode 3) whose
// advance is stretched to the width recognition measured, so a selection drags along the
// ink rather than beside it. The text goes into the page's own content stream, appended,
// wrapped in q ... Q: nothing the page drew moves or changes colour, and a rendering
// before and after is the same picture. See src/edit/textlayerwriter.h for the font and
// the save.
//
// ---- the two files ----
//
// The input is read through this program's backend, and then read again by qpdf from the
// same path; the output is a new file the reader names. The file being read is never
// written -- textlayerwriter.cpp refuses a destination that is the source, and the save
// is the staged-write-and-move of docs/handoff-03.md F8.
#pragma once

#include <QAtomicInt>
#include <QSharedPointer>
#include <QString>

#include <functional>

class Document;

struct SearchablePdfReport
{
    bool ok = false;
    // The document changed under the work and it stopped, rather than writing a file
    // that is half of one document and half of another.
    bool cancelled = false;
    // A sentence to show, set whenever ok is false.
    QString error;
    QString path;

    // Pages handed to the recognizer.
    int recognized = 0;
    // Pages left alone because they already had a text layer -- visible or invisible.
    int skipped = 0;
    // Recognized pages that produced no words (a blank page, a picture of something
    // that is not text).
    int empty = 0;
    // Words actually written, and pages that gained a stream. A page can be recognized
    // and still write nothing when every box came back empty.
    int words = 0;
    int writtenPages = 0;
};

struct SearchablePdfOptions
{
    // Set by the caller when the document it is reading is about to be replaced.
    // Checked between pages, because half a file made from one document and half from
    // another is worse than none.
    QSharedPointer<QAtomicInt> cancel;

    // Called after each page, from the worker thread that is doing the reading.
    std::function<void(int done, int total)> progress;
};

// Read `source` through `document`, recognize the pages that have no text, and write
// `source` plus the text layer to `destination`. `password` is what opened the document
// and is handed to qpdf; an encrypted source stays encrypted.
SearchablePdfReport makeSearchablePdf(Document &document, const QString &source,
                                      const QString &password, const QString &destination,
                                      const SearchablePdfOptions &options = SearchablePdfOptions());
