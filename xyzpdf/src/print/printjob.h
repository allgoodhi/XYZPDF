// Putting the pages of a document onto paper.
//
// ---- why this is written at all ----
//
// Qt PDF renders a page and reads its text, but it has no print helper -- there
// is nothing behind it that turns a page into marks on a printer. So printing is
// done the long way: ask the printer what its printable area and resolution are,
// render each page into an image at that resolution, and paint the images onto
// the printer's own paint device, one sheet at a time. See docs/PLAN.md §1.1.
//
// ---- what "full options" costs, and where the risk is ----
//
// Page ranges, fit-versus-actual scaling and n-up are each simple on their own
// and interact in the places that matter: a page bigger than its cell, a range
// that names no page, an n-up grid that leaves a slot empty. The two decisions
// that are pure arithmetic -- which pages a range names, and how many columns an
// n-up count wants -- are free functions here and tested without a printer or a
// document; only the painting, which needs a real device, is left to be checked
// by hand.
#pragma once

#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class Document;
class QPrinter;
class QAtomicInt;

namespace Print {

struct Settings
{
    enum class Range
    {
        All,
        Current,
        Custom
    };
    enum class Scale
    {
        Fit,    // shrink or grow the page to fill its cell
        Actual  // one point to one printer pixel; a page larger than its cell clips
    };

    Range range = Range::All;
    // A 1-based spec like "1-3,5,8-10", used only when range is Custom.
    QString custom;
    // Zero-based, used only when range is Current.
    int currentPage = 0;
    Scale scale = Scale::Fit;
    // Pages per sheet: 1, 2, 4, 6, 9 or 16.
    int nUp = 1;
};

struct Report
{
    bool ok = false;
    bool cancelled = false;
    // A sentence to show, set whenever ok is false.
    QString error;
    int sheets = 0;
    int pages = 0;
};

// The pages a range names, zero-based, ascending and without repeats, clamped to
// [0, pageCount). An empty or whitespace spec means every page; a reversed span
// like "5-3" is read as "3-5"; tokens that name no page are skipped. Pure, so
// the whole awkward surface of "what does the user's typing mean" is testable.
QVector<int> parsePageRange(const QString &spec, int pageCount);

// How an n-up count lays out: {columns, rows}. 1->1x1, 2->2x1, 4->2x2, 6->3x2,
// 9->3x3, 16->4x4; anything else falls back to 1x1.
QPair<int, int> nUpGrid(int nUp);

// Renders and prints. Blocking and on the GUI thread (a QPrinter's paint device
// is not something to drive from a worker), which is why `progress` is offered:
// a big document is seconds to minutes of rendering, and a window that gives no
// sign of it reads as one that has stopped. `cancel`, when set and non-zero,
// stops between sheets -- a job already sent to the printer cannot be recalled.
Report run(Document &document, QPrinter &printer, const Settings &settings,
           const std::function<void(int done, int total)> &progress = nullptr,
           const QAtomicInt *cancel = nullptr);

} // namespace Print
