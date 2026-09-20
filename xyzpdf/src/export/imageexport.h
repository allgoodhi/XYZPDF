// Writing pages out as picture files.
//
// The sibling of the Word export, and much smaller: the reader asks for the
// pages as PNG or JPEG -- to paste into a slide, to keep a figure, to open in
// something that is not a PDF reader. There is no structure to preserve here,
// only "render this page at a decent resolution and save the picture", so the
// whole job is the render loop plus one decision worth getting right and testing:
// what each file is called.
//
// It runs off the UI thread the way an export does -- a page at print resolution
// is a real render, and a folder of them is minutes of it.
#pragma once

#include <QString>
#include <QVector>

#include <functional>

class Document;
class QAtomicInt;

namespace ImageExport {

enum class Format
{
    Png,
    Jpeg
};

struct Report
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    // Where the files went, so the window can say so and offer to open it.
    QString directory;
    int pages = 0;
};

// The name one page's file gets: "<base> <page>.<ext>", the page 1-based and
// zero-padded to three digits so the files sort in page order in a folder. Pure,
// because a name that collides or sorts out of order is a quiet bug.
QString fileNameFor(const QString &baseName, int page, Format format);

// Renders each page at `dpi` and writes it into `directory`. Blocking; the caller
// runs it off the UI thread. `progress` is told (done, total) per page and
// `cancel`, when set, stops between pages -- a folder half-written is left as
// it is, which is honest about what got out.
Report run(Document &document, const QVector<int> &pages, const QString &directory,
           const QString &baseName, Format format, int dpi,
           const std::function<void(int done, int total)> &progress = nullptr,
           const QAtomicInt *cancel = nullptr);

} // namespace ImageExport
