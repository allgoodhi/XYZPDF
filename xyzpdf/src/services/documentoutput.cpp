#include "documentoutput.h"

#include "../core/appsettings.h"
#include "../core/windowdialogs.h"
#include "../document/document.h"
#include "../export/imageexport.h"
#include "../print/printjob.h"
#include "../ui/imageexportdialog.h"
#include "../ui/printdialog.h"
#include "../view/pageview.h"
#include "foregroundjobs.h"

#include <QAtomicInt>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPrinter>
#include <QProgressDialog>
#include <QSharedPointer>

DocumentOutput::DocumentOutput(QWidget *host, PageView *view, ForegroundJobs *jobs,
                               std::function<Document *()> document,
                               std::function<bool()> formFields)
    : QObject(host)
    , host(host)
    , pageView(view)
    , jobs(jobs)
    , documentProvider(std::move(document))
    , formFieldsInSource(std::move(formFields))
{
}

DocumentOutput::~DocumentOutput() = default;

void DocumentOutput::print()
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document)
        return;

    // From here to the end of the method the tab must not be taken apart: the
    // settings dialog and the progress box both pump the event loop, and a
    // 关闭标签 landing in either one would delete the object this call is
    // standing in. See ForegroundJobs.
    const ForegroundJobs::Scope scope(jobs);

    QPrinter printer(QPrinter::HighResolution);
    // F7-C: a PDFium-backed build draws field appearances into every page image,
    // print included, so a filled form comes out with its answers and the
    // warning §94 added would be false. The Qt build still prints empty boxes
    // and keeps it until F7-E deletes that backend.
    bool warnAboutBlankFields = false;
#if !defined(XYZPDF_DOCUMENT_BACKEND_PDFIUM)
    warnAboutBlankFields = formFieldsInSource ? formFieldsInSource() : false;
#endif
    PrintDialog dialog(host->window(), &printer, pageView->currentPage(), document->pageCount(),
                       warnAboutBlankFields);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Print::Settings settings = dialog.settings();

    // A cancel-able progress dialog over a foreground job. Print::run calls the
    // callback once per sheet on this thread; pumping events here is what lets
    // the dialog repaint and its Cancel button be noticed between sheets.
    auto cancel = QSharedPointer<QAtomicInt>::create(0);
    QProgressDialog progress(QCoreApplication::translate("DocumentTab", "正在打印…"),
                             QCoreApplication::translate("DocumentTab", "取消"), 0, 1,
                             host->window());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setValue(0);

    const Print::Report report = Print::run(
        *document, printer, settings,
        [&progress, &cancel](int done, int total) {
            progress.setMaximum(total);
            progress.setValue(done);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancel->storeRelaxed(1);
        },
        cancel.data());

    if (!report.ok && !report.cancelled)
        WindowDialogs::warning(host->window(), QCoreApplication::translate("DocumentTab", "打印"),
                               report.error);
}

void DocumentOutput::exportImages()
{
    Document *document = documentProvider ? documentProvider() : nullptr;
    if (!document)
        return;

    // The same reason printing holds one: two dialogs that pump the loop, and
    // a render loop between them that does it once per page.
    const ForegroundJobs::Scope scope(jobs);

    ImageExportDialog dialog(host->window(), pageView->currentPage(), document->pageCount());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString directory = dialog.directory();
    if (directory.isEmpty()) {
        WindowDialogs::warning(host->window(),
                               QCoreApplication::translate("DocumentTab", "导出为图片"),
                               QCoreApplication::translate("DocumentTab", "请先选择保存文件夹。"));
        return;
    }
    const QVector<int> pages = dialog.pages();
    if (pages.isEmpty()) {
        WindowDialogs::warning(host->window(),
                               QCoreApplication::translate("DocumentTab", "导出为图片"),
                               QCoreApplication::translate("DocumentTab",
                                                           "页码范围里没有可导出的页面。"));
        return;
    }
    const QString base = QFileInfo(document->path()).completeBaseName();

    // Foreground, like printing: each page is a render under the same lock the
    // view uses, and pumping events in the callback is what keeps the progress
    // box live and its Cancel button responsive between pages.
    auto cancel = QSharedPointer<QAtomicInt>::create(0);
    QProgressDialog progress(QCoreApplication::translate("DocumentTab", "正在导出为图片…"),
                             QCoreApplication::translate("DocumentTab", "取消"), 0, pages.size(),
                             host->window());
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoReset(false);
    progress.setValue(0);

    const ImageExport::Report report = ImageExport::run(
        *document, pages, directory, base, dialog.format(), dialog.dpi(),
        [&progress, &cancel](int done, int total) {
            progress.setMaximum(total);
            progress.setValue(done);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancel->storeRelaxed(1);
        },
        cancel.data());

    if (report.cancelled)
        return;
    if (!report.ok) {
        WindowDialogs::warning(host->window(),
                               QCoreApplication::translate("DocumentTab", "导出为图片"),
                               report.error);
        return;
    }
    AppSettings::setLastOpenDirectory(directory);
    Q_EMIT imageExportFinished(report.directory, report.pages);
}
