// The two jobs that put this document's pages out somewhere else, and wait for
// each page: the printer and a folder of images.
//
// Cut out of DocumentTab (docs/review-03.md §1.2) because they are the same job
// twice -- ask for the choices, then render page by page in front of the reader
// with a cancel-able progress box. Both pump the event loop between pages, which
// is exactly what makes them need the guard: see ForegroundJobs, and the two
// `Scope` lines below.
//
// They stay foreground rather than going to a worker like the Word export
// because a QPrinter's paint device is a GUI-thread thing, and because each page
// is a render under the same lock the view uses -- pumping events in the
// callback is what keeps the progress box live and its Cancel button responsive
// between pages.
#pragma once

#include <QObject>
#include <QString>

#include <functional>

class Document;
class ForegroundJobs;
class PageView;
class QWidget;

class DocumentOutput : public QObject
{
    Q_OBJECT

public:
    // `document` answers with the document to draw, or null when the tab has
    // none -- an export with nothing to read says nothing.
    DocumentOutput(QWidget *host, PageView *view, ForegroundJobs *jobs,
                   std::function<Document *()> document);
    ~DocumentOutput() override;

    // Asks for the print choices, then renders and prints.
    void print();

    // Writes every page out as a PNG into a folder the reader chooses.
    void exportImages();

Q_SIGNALS:
    // A page-image export finished: how many files and where, for the window to
    // say so in the status bar.
    void imageExportFinished(const QString &directory, int pages);

private:
    QWidget *host = nullptr;
    PageView *pageView = nullptr;
    ForegroundJobs *jobs = nullptr;
    std::function<Document *()> documentProvider;
};
