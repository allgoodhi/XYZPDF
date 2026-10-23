// The box the reader dragged on a page: cut out of the page at a size of its
// own, put on the clipboard or handed to the machine's recognizer, and -- when
// the reader is working in another language -- sent on to a translation.
//
// Cut out of DocumentTab (docs/review-03.md §1.2) because it is one of the jobs
// that "take the document and do something with it". The recognition runs off the
// UI thread and the crop render does not, and that difference is the load-bearing
// part of this class -- see the note in captureRegion().
//
// Nothing in here waits for the document it rendered, because nothing in here is
// still holding it once the call returns: the crop goes into the worker by value.
// That is also why a running recognition is not one of the jobs
// DocumentTab::drainRenders() stops for.
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>

#include <functional>

class Document;
class OcrDialog;
class PageView;
class QWidget;

class OcrSession : public QObject
{
    Q_OBJECT

public:
    // `host` is the tab: the parent for the window a recognition answer opens in.
    // `document` answers with the document to draw the crop from, or null when
    // the tab has none.
    OcrSession(QWidget *host, PageView *view, std::function<Document *()> document);
    ~OcrSession() override;

    // The box the user dragged over a text-free part of a page: rendered at a
    // size of its own, then recognized, then answered in a window. Which of the
    // three tools asked decides what happens to the pixels -- see the definition.
    void captureRegion(int page, QRectF points);

Q_SIGNALS:
    // A box drawn with the snapshot tool has been put on the clipboard, at
    // this size in pixels. The window says so: the tab has no status bar, and
    // §95 measured that it never did.
    void regionCopied(int width, int height);
    // The words that came out of a picture, asked to be put into another
    // language -- either from the translate tool's box or from the 翻译 button in
    // the recognition window. Translation is another service's job.
    void translateRequested(const QString &text);

private:
    // Runs the recognition off the UI thread and fills the window in when it
    // is done. The crop is copied into the worker; the window is watched with
    // a QPointer, because the user may close it first. When `thenTranslate` is
    // set (the translate tool boxing a picture), the recognized words are sent
    // straight to a translation and `dialog` is null.
    void recognize(const QImage &crop, QPointer<OcrDialog> dialog, bool thenTranslate = false);

    QWidget *host = nullptr;
    PageView *pageView = nullptr;
    std::function<Document *()> documentProvider;
};
