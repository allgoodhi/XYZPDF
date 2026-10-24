// The wiring between the view and the tab.
//
// ---- why this test exists ----
//
// Every part of this had a test and the whole of it did not. render_smoke
// checks PageRenderer on its own, layout_smoke and zoom_smoke check arithmetic,
// and none of them noticed that zooming and rotating left the page grey
// forever: PageView asked for a render *before* telling the tab to make the
// work in flight pointless, so every result came back for a generation that had
// already been superseded and was dropped -- and the page was recorded as "on
// its way" for the size that had just been asked for, so nothing ever asked
// again.
//
// The assertion therefore has to be about what ends up on screen, not about
// signals being emitted. "A request went out" is exactly what was true while
// this was broken.
//
// It runs on the offscreen platform: the point is the plumbing, and a window
// appearing on somebody's desktop while their tests run is not.

#include "../documenttab.h"
#include "../src/core/appsettings.h"
#include "../src/core/stagingcleanup.h"
#include "../src/edit/documenteditor.h"
#include "../src/theme/thememanager.h"
#include "../src/translate/keystore.h"
#include "../src/ui/bookmarkdialog.h"
#include "../src/ui/deletepagesdialog.h"
#include "../src/ui/findbar.h"
#include "../src/ui/mergepagesdialog.h"
#include "../src/ui/movepagesdialog.h"
#include "../src/ui/ocrdialog.h"
#include "../src/ui/printdialog.h"
#include "../src/ui/outlinepanel.h"
#include "../src/ui/protectpagesdialog.h"
#include "../src/ui/propertiesdialog.h"
#include "../src/ui/rotatepagesdialog.h"
#include "../src/ui/searchpanel.h"
#include "../src/ui/splitpagesdialog.h"
#include "../src/ui/thumbnailpanel.h"
#include "../src/ui/translatedialog.h"
#include "../src/ui/watermarkdialog.h"
#include "../src/document/pdfiumbackend.h"
#include "../src/view/pageview.h"
#include "../src/view/zoomcontroller.h"

#include "signaturesample.h"
#include "formsample.h"

#include <QAccessible>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QThreadPool>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

// The overwrite measurement asks the same question the save's last step does, and that is a Win32
// call rather than a Qt one.
//
// The #undef is not tidiness: rpcndr.h (pulled in by windows.h) does `#define small char`, and this
// file already had a local named `small` several hundred lines below the include -- so adding the
// header rewrote a line nobody was working on, and the compiler complained about a QString two
// variables away from the change. undef'ing it here keeps the pre-existing code meaning what it
// said; the alternative was to rename something this slice has no business renaming.
#include <windows.h>
#undef small

// The crash forensics docs/store-submission.md §103 owed: a minidump of the faulting
// thread, written by this process rather than by the machine's error reporting.
// Declared after windows.h for the same reason the #undef above is: this header is
// another layer of the same SDK.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <iostream>
#include <string>
#include <typeinfo>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

int measurements = 0;

// Printed rather than asserted, and counted -- §78's rule: a reading a later slice compares against
// a recorded number has to go through the counter, or the "N measurement(s)" line and the lines the
// run printed stop describing the same run.
void note(const QString &what)
{
    std::cout << "note: " << what.toStdString() << '\n';
    ++measurements;
}

// Pumps the event loop until `ready` says so, or the time runs out. The
// renders are on other threads and come back through queued signals, so there
// is nothing to wait on but the event loop itself.
bool waitFor(const std::function<bool()> &ready, int timeoutMilliseconds = 30000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready()) {
        if (elapsed.elapsed() > timeoutMilliseconds)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

PageView *viewOf(DocumentTab &tab)
{
    return tab.view();
}

// How many rows of the thumbnail strip have a picture in them.
//
// Read off the model rather than off the panel, because what is being asked is
// what a reader would see: a row with no decoration is a page number on an
// empty tile. An icon that exists but is null counts as nothing, which is what
// QIcon::isNull() says.
int thumbnailsRendered(const QAbstractItemView *strip)
{
    const QAbstractItemModel *model = strip->model();
    if (!model)
        return 0;
    int rendered = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QVariant decoration = model->index(row, 0).data(Qt::DecorationRole);
        if (!qvariant_cast<QIcon>(decoration).isNull())
            ++rendered;
    }
    return rendered;
}

// Everything the labels of a dialog say, run together.
//
// The properties dialog is a form of labels and nothing else, so "what does it
// show" is exactly this string -- and asserting on it rather than on row
// indices means a row added above does not move the test.
QString labelsOf(const QWidget *dialog)
{
    QStringList said;
    for (const QLabel *label : dialog->findChildren<QLabel *>())
        said.append(label->text());
    return said.join(QLatin1Char(0x0a));
}

// Answers one of the tab's own dialogs the way a reader would: waits for it to
// open, types `pages` into its page box, presses the button that accepts it.
//
// Armed *before* the menu action is called, because the action ends in exec()
// and nothing after that line runs until the dialog is gone. Bounded both ways:
// a dialog that never appears costs a few seconds rather than the run, and one
// that appears and cannot be filled in is rejected, so the exec() underneath
// always returns.
template <typename Dialog>
void answerNextDialog(const QString &pages)
{
    auto *timer = new QTimer;
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, ticks, pages] {
        Dialog *open = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (auto *candidate = qobject_cast<Dialog *>(top); candidate && candidate->isVisible())
                open = candidate;
            for (Dialog *child : top->findChildren<Dialog *>()) {
                if (child->isVisible())
                    open = child;
            }
        }
        if (open) {
            if (auto *box = open->template findChild<QLineEdit *>())
                box->setText(pages);
            auto *buttons = open->template findChild<QDialogButtonBox *>();
            QPushButton *accept = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
            if (accept && accept->isEnabled())
                accept->click();
            else
                open->reject();
            timer->deleteLater();
            return;
        }
        if (++*ticks > 100)
            timer->deleteLater(); // never appeared; nothing to unblock
    });
    timer->start(50);
}

// Answers the next message box by pressing the button whose text contains
// `buttonText`, recording what the box said.
//
// A repeating timer rather than singleShot(0), because the save paths pump the
// event loop before the box opens (drainRenders() does), and a one-shot timer
// would fire inside that pump, find no box, and leave the real one waiting for a
// human. Bounded: with no box in five seconds it gives up, and the caller's
// "was the box seen" check is what turns that into red rather than a hang.
void answerNextBox(const QString &buttonText, bool *seen, QString *text)
{
    // One waiter at a time. A timer that found no box keeps ticking towards its own
    // five-second limit, and if the next box opens inside that window it is the old
    // timer that answers it -- with the old call's button and the old call's record.
    // Measured here: the second and third refusal boxes were answered by the first
    // save's leftover waiter, which reported "no box" for the calls that owned them.
    static QTimer *waiting = nullptr;
    if (waiting) {
        waiting->stop();
        waiting->deleteLater();
        waiting = nullptr;
    }

    auto *timer = new QTimer;
    waiting = timer;
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, ticks, buttonText, seen, text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box) {
            if (seen)
                *seen = true;
            if (text)
                *text = box->text();
            QAbstractButton *target = nullptr;
            for (QAbstractButton *button : box->buttons()) {
                if (button->text().contains(buttonText))
                    target = button;
            }
            if (target)
                target->click();
            else
                box->reject();
            if (waiting == timer)
                waiting = nullptr;
            timer->deleteLater();
            return;
        }
        if (++*ticks > 250) {
            if (waiting == timer)
                waiting = nullptr;
            timer->deleteLater();
        }
    });
    timer->start(20);
}

// A file read whole, and written whole: the byte-level before/after a save is judged on.
QByteArray readAllBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeAllBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

// One mouse event, straight to the viewport.
//
// Sent to the viewport rather than to the view because that is where Qt
// delivers a mouse to a scroll area from: QAbstractScrollArea filters its
// viewport's events and hands them on with the coordinates already in the
// viewport's system, which is the one pageRectInViewport() reports in.
void sendMouse(QWidget *viewport, QEvent::Type type, const QPointF &position,
               bool buttonHeld = false, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const Qt::MouseButton button = Qt::LeftButton;
    const Qt::MouseButtons buttons =
        type == QEvent::MouseMove
            ? (buttonHeld ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton))
            : Qt::MouseButtons(Qt::LeftButton);
    QMouseEvent event(type, position, viewport->mapToGlobal(position), button, buttons, modifiers);
    QApplication::sendEvent(viewport, &event);
}

// ---- the crash dump §103 asked for ----
//
// §103's control run printed its four FAILs and ended on 0xC0000005. Two product-shaped
// reproductions were run three times each and all six came back clean, so the slice wrote
// "没查出来就不写成解释" and left it open. What was missing was an artifact: nothing in the
// process caught the exception, and the exit code is not a stack.
//
// This filter is armed only when XYZPDF_TEST_CRASH_DUMP is set, so a gate run writes no
// files and no ordinary developer run is slower. It is SetUnhandledExceptionFilter -- this
// process only. The machine's own error-reporting settings are not a test's to change, and
// leaving the search running would hand the crash to something that wants a dialog pressed.
//
// The dump is the artifact; the stack printed beside it is the reading. The gate build
// carries no .pdb (CMake's Release flags are /O2 without /Zi), so dbghelp resolves only
// what the loaded images export: Qt's frames come back by name, ours as module+offset.
// That is enough to say which image the faulting instruction and the return chain live
// in, which is what §103 could not say -- and the same source built into a scratch tree
// with /Zi + /DEBUG gives the names beside those offsets.
bool readableAt(DWORD64 address, size_t bytes)
{
    // Reading the fault site is itself a read, so both ends of the run are asked first:
    // an instruction pointer that is not mapped would turn the report into a second
    // fault, and the filter has no second chance to catch it.
    const auto committed = [](DWORD64 at) {
        MEMORY_BASIC_INFORMATION page{};
        return VirtualQuery(reinterpret_cast<const void *>(at), &page, sizeof(page))
                   == sizeof(page)
               && page.State == MEM_COMMIT
               && (page.Protect == PAGE_EXECUTE_READ || page.Protect == PAGE_READONLY
                   || page.Protect == PAGE_EXECUTE_READWRITE || page.Protect == PAGE_WRITECOPY
                   || page.Protect == PAGE_EXECUTE_WRITECOPY);
    };
    return committed(address) && committed(address + bytes - 1);
}

std::wstring crashDumpDirectory()
{
    QString directory = qEnvironmentVariable("XYZPDF_TEST_CRASH_DUMP_DIR");
    if (directory.isEmpty()) {
        wchar_t buffer[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, buffer) > 0)
            directory = QString::fromWCharArray(buffer);
    }
    return directory.toStdWString();
}

std::wstring crashDumpPath(unsigned exceptionCode)
{
    const std::wstring directory = crashDumpDirectory();
    if (directory.empty())
        return {};
    const QString asText = QString::fromStdWString(directory);
    if (!QDir().mkpath(asText))
        return {};
    const QString name = QStringLiteral("xyzpdf_documenttab_smoke-%1-%2-%3.dmp")
                             .arg(GetCurrentProcessId())
                             .arg(QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyyMMdd-HHmmss")))
                             .arg(exceptionCode, 8, 16, QLatin1Char('0'));
    return QDir(asText).filePath(name).toStdWString();
}

// One address as `name+displacement [image+offset]`, or `image+offset` when nothing names
// the address, or "no mapped image holds this address" when no image does.
QString frameAt(DWORD64 address)
{
    // Which image, asked of the OS rather than of dbghelp. SymGetModuleInfoW64 answers out
    // of the module list SymInitialize built at startup, and the recognition path's DLLs
    // are loaded later -- so an address inside one of them read back as "unknown image",
    // which is a fact about that cache and not about the address (the §96 shape again: a
    // zero describes the search, not the machine).
    QString where = QStringLiteral("no mapped image holds this address");
    HMODULE module = nullptr;
    if (address != 0
        && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(address), &module)
        && module) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(module, path, MAX_PATH);
        where = QString::fromWCharArray(path).section(QLatin1Char('\\'), -1) + QStringLiteral("+0x")
                + QString::number(address - reinterpret_cast<DWORD64>(module), 16);
    }

    // The name, from dbghelp: a real one where the image carries debug information, and the
    // nearest export otherwise. SYMBOL_INFO carries a variable-length name, so the buffer is
    // the struct plus the room and the pointer is cast rather than the struct declared with
    // a size. The older IMAGEHLP_SYMBOL64 is a different shape and SymFromAddr will not take
    // it.
    alignas(8) unsigned char storage[sizeof(SYMBOL_INFO) + 512];
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 512;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
        // The cast is not decoration: SYMBOL_INFO::Name is declared CHAR[1], and handed to
        // fromUtf8 as an array it binds to the QByteArrayView overload, which takes the
        // declared length -- every frame then comes back as its own first letter.
        where = QString::fromUtf8(static_cast<const char *>(symbol->Name)) + QStringLiteral("+0x")
                + QString::number(displacement, 16) + QStringLiteral(" [") + where
                + QStringLiteral("]");
    }
    return where;
}

LONG WINAPI writeCrashDumpAndStack(EXCEPTION_POINTERS *pointers)
{
    const DWORD code =
        pointers && pointers->ExceptionRecord ? pointers->ExceptionRecord->ExceptionCode : 0;
    const DWORD64 faulting =
        pointers && pointers->ExceptionRecord
            ? DWORD64(pointers->ExceptionRecord->ExceptionAddress)
            : 0;

    bool dumpWritten = false;
    const std::wstring path = crashDumpPath(code);
    if (!path.empty()) {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION stream{};
            stream.ThreadId = GetCurrentThreadId();
            stream.ExceptionPointers = pointers;
            stream.ClientPointers = FALSE;
            // IndirectlyReferencedMemory walks what the stack points at, which is what makes
            // the dump answer a question later instead of only recording that a thread died.
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo
                                                         | MiniDumpWithIndirectlyReferencedMemory);
            dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                            &stream, nullptr, nullptr)
                          != FALSE;
            CloseHandle(file);
        }
    }

    std::fprintf(stderr, "CRASH: exception 0x%08lX at %s\n", code,
                 frameAt(faulting).toUtf8().constData());
    // 0xE06D7363 is MSVC's code for a thrown C++ exception. Its record carries three
    // values -- the 0x19930520 signature, the thrown object, and the throw info -- and
    // nothing else: naming the type from here means walking MSVC's private ThrowInfo,
    // so the numbers are printed as they are. The type was read separately (see the
    // report); a guess at somebody's internal layout is not a reading.
    if (code == 0xE06D7363 && pointers && pointers->ExceptionRecord) {
        std::fprintf(stderr, "CRASH:   thrown C++ exception, record");
        for (DWORD index = 0; index < pointers->ExceptionRecord->NumberParameters; ++index)
            std::fprintf(stderr, " 0x%llX",
                         static_cast<unsigned long long>(pointers->ExceptionRecord->ExceptionInformation[index]));
        std::fprintf(stderr, "\n");
    }
    // For an access violation the record carries what was being touched: Information[0]
    // is 0 read / 1 write / 8 DEP-execution and [1] is the address. Without it a fault at
    // an unknown instruction is only a location, not a shape -- and with it the report can
    // say whether the pointer was null, stale, or pointing outside every mapped image.
    if (code == EXCEPTION_ACCESS_VIOLATION && pointers && pointers->ExceptionRecord
        && pointers->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR *information = pointers->ExceptionRecord->ExceptionInformation;
        std::fprintf(stderr, "CRASH:   access %s of 0x%llX -- %s\n",
                     information[0] == 0 ? "read" : (information[0] == 1 ? "write" : "exec"),
                     static_cast<unsigned long long>(information[1]),
                     information[1] == 0
                         ? "no address at all"
                         : frameAt(static_cast<DWORD64>(information[1])).toUtf8().constData());
    }
    // The bytes the faulting instruction is made of, so the same instruction can be
    // found again in a build that does carry symbols.
    if (faulting != 0 && readableAt(faulting, 12)) {
        const auto *at = reinterpret_cast<const unsigned char *>(faulting);
        std::fprintf(stderr, "CRASH:   bytes at fault");
        for (int byte = 0; byte < 12; ++byte)
            std::fprintf(stderr, " %02X", at[byte]);
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "CRASH: dump %s\n",
                 dumpWritten ? QString::fromStdWString(path).toUtf8().constData()
                             : "NOT WRITTEN");
    std::fflush(stderr);

    // The walk is best-effort and last: if it fails, the dump and the lines above it are
    // still on disk and on the console.
    CONTEXT context = pointers ? *pointers->ContextRecord : CONTEXT{};
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    for (int depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame,
                         &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            std::fprintf(stderr, "CRASH: the walk stopped at frame %d\n", depth);
            break;
        }
        if (frame.AddrPC.Offset == 0)
            break;
        const QString text = QStringLiteral("CRASH: #%1 %2").arg(depth).arg(frameAt(frame.AddrPC.Offset));
        std::fprintf(stderr, "%s\n", text.toUtf8().constData());
    }
    std::fflush(stderr);

    // The exit code the crash would have produced anyway, so the harness still reads red.
    TerminateProcess(GetCurrentProcess(), code != 0 ? code : 1);
    return EXCEPTION_CONTINUE_SEARCH; // not reached while the terminate succeeds
}

void installCrashReporting()
{
    // Name the exception before the process goes. An unhandled C++ exception otherwise
    // ends the run with an exit code and no line, which is exactly how §103's crash was
    // recorded: "0xC0000005, and I did not find out what it was".
    //
    // The uncaught_exceptions() guard is not tidiness: `throw;` with no exception in
    // flight re-enters terminate, and a reporter that dies while reporting is worse than
    // the silence it replaces.
    //
    // With XYZPDF_TEST_CRASH_DUMP set, the filter below is reached first and prints the
    // stack beside the dump; this handler is what answers for a C++ exception when no dump
    // was asked for.
    std::set_terminate([] {
        // Two ways in, and the second one is the measured case. `throw;` names the
        // exception when a handler is still on the stack, but terminate for an exception
        // that escaped all the way out of the event loop is reached with
        // uncaught_exceptions() == 0 -- that is exactly the §103 case, and guarding on the
        // count alone silenced the line it was written to print. So: the count first, then
        // the stored exception, and only then the honest "nothing in flight" (terminate is
        // also reached with no exception at all, and `throw;` there re-enters terminate).
        const bool countSaysSo = std::uncaught_exceptions() > 0;
        const std::exception_ptr stored = countSaysSo ? nullptr : std::current_exception();
        if (countSaysSo || stored) {
            try {
                if (countSaysSo)
                    throw;
                std::rethrow_exception(stored);
            } catch (const std::exception &e) {
                // typeid().name() keeps the MSVC spelling, which is the sentence: the
                // answer this slice owed was "class QUnhandledException".
                std::fprintf(stderr, "TERMINATE: uncaught std::exception [%s] what=[%s]\n",
                             typeid(e).name(), e.what());
            } catch (...) {
                std::fprintf(stderr, "TERMINATE: uncaught exception that is not a std::exception\n");
            }
        } else {
            std::fprintf(stderr, "TERMINATE: std::terminate with no exception in flight\n");
        }
        std::fflush(stderr);
        std::_Exit(3);
    });
    if (!qEnvironmentVariableIsSet("XYZPDF_TEST_CRASH_DUMP"))
        return;
    // Symbols have to be enumerated before the crash: doing it inside the filter is a
    // smaller amount of work standing between the fault and the dump.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const bool symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    SetUnhandledExceptionFilter(writeCrashDumpAndStack);
    // Armed, and where the files would land: a run that reports a crash without this line
    // cannot be told apart from a run whose filter was never switched on.
    note(QStringLiteral("crash dump filter armed (symbols: %1, dumps into: %2)")
             .arg(symbols ? QStringLiteral("loaded") : QStringLiteral("NOT loaded"))
             .arg(QString::fromStdWString(crashDumpDirectory())));
}

} // namespace

int main(int argc, char **argv)
{
    // Before QApplication: the platform has to be chosen before it starts.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication application(argc, argv);
    installCrashReporting();
    const QString directory = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("tests/fixtures");
    const QString path = directory + QStringLiteral("/text.pdf");

    DocumentTab tab;
    tab.resize(900, 700);
    tab.show();

    if (tab.open(path) != Document::Status::Ready) {
        std::cerr << "FAIL: could not open the fixture " << path.toStdString() << ": "
                  << tab.lastError().toStdString() << '\n';
        return 1;
    }

    PageView *view = viewOf(tab);

    // ---- the first page appears ----
    {
        check(waitFor([view] { return view->hasImageFor(0); }),
              "the page on screen is rendered and drawn");
        check(tab.pageCount() == 2, "the document's pages are known to the tab");
    }

    // ---- the thumbnails wait for the sidebar to be opened ----
    //
    // docs/review-03.md §2.1: opening a document used to start a walk over
    // every one of its pages, rendering each into the strip, whether or not the
    // sidebar had ever been shown -- and every one of those renders holds the
    // global PDFium lock the page the reader is waiting for needs. On a scanned
    // page a 120 px thumbnail costs 17 ms against the whole page's 30 (measured
    // with tools/pdfprobe.cpp), so a 500-page scan spent about 8.5 seconds of
    // that lock on pictures for a panel nobody had opened.
    //
    // Both halves are asserted, because only one of them is visible in the
    // pictures: nothing is rendered while the sidebar is shut, *and* it is
    // rendered once it is opened. A deferral that never ends would pass the
    // first check on its own.
    {
        auto *strip = tab.findChild<ThumbnailPanel *>();
        check(strip != nullptr, "the tab has a thumbnail strip");
        if (strip) {
            check(!strip->isVisible(), "the sidebar starts shut");
            check(strip->waiting(), "so the strip is holding a job rather than running one");
            // Through the base class: ThumbnailPanel has a member called `model`
            // of its own, which hides QAbstractItemView::model() from anyone
            // holding the derived type.
            const auto *stripView = static_cast<const QAbstractItemView *>(strip);
            check(stripView->model() != nullptr
                      && stripView->model()->rowCount() == tab.pageCount(),
                  "the strip has a row per page even before any of them is rendered");

            // Given every chance to render: the walk is a worker and its results
            // arrive as queued events, so a loop pumped for a while is what the
            // claim "nothing happened" has to survive.
            waitFor([] { return false; }, 400);
            check(thumbnailsRendered(strip) == 0, "and no page has been rendered into it");

            tab.setSidebarVisible(true);
            check(strip->isVisible(),
                  "opening the sidebar puts the strip on screen (text.pdf has no bookmarks, so "
                  "it is the tab in front)");
            check(waitFor([strip, &tab] { return thumbnailsRendered(strip) == tab.pageCount(); }),
                  "and that is when the pages are rendered into it");
            check(!strip->waiting(), "the job it was holding has been taken on");

            // ---- and only the strip that can be seen is rendered ----
            //
            // The other half of the same argument (docs/review-03.md §2.2):
            // the walk used to render every page of the document and keep
            // every picture for as long as it was open -- 120x170 of ARGB
            // each, about 81 KB, so 49 MB on a 600-page book, outside the
            // render cache's budget entirely. A strip shows a handful of rows;
            // holding six hundred is the same mistake review-02 found in
            // PageView::images one layer down.
            {
                const QString book = directory + QStringLiteral("/huge.pdf");
                check(tab.open(book) == Document::Status::Ready, "the 600-page book opens");
                check(tab.pageCount() == 600, "and it really is 600 pages");
                check(waitFor([strip] { return strip->heldCount() > 0; }),
                      "its strip renders the rows at the top");

                // Given every chance to render the rest, the way the old walk
                // would have: if the band were not bounding this, six hundred
                // pages of a text document render in well under a second
                // (0 ms each, measured with pdfprobe).
                waitFor([] { return false; }, 1500);
                const int held = strip->heldCount();
                check(held < 60,
                      QStringLiteral("but it holds only the rows near the viewport, not the "
                                     "document (holding %1 of 600)").arg(held));
                check(held == thumbnailsRendered(strip),
                      QStringLiteral("and what it says it holds is what is in the model "
                                     "(%1 against %2)").arg(held).arg(thumbnailsRendered(strip)));

                // Not bounded by rendering nothing: the far end of the
                // document gets its pictures when it is the far end that is
                // being looked at.
                auto *bar = strip->verticalScrollBar();
                bar->setValue(bar->maximum());
                check(waitFor([strip] {
                          const QAbstractItemModel *rows =
                              static_cast<const QAbstractItemView *>(strip)->model();
                          return !qvariant_cast<QIcon>(
                                      rows->index(rows->rowCount() - 1, 0).data(Qt::DecorationRole))
                                      .isNull();
                      }),
                      "scrolling to the end renders the last page into the strip");
                check(strip->heldCount() < 60,
                      QStringLiteral("and the count is still bounded after the scroll (%1)")
                          .arg(strip->heldCount()));

                check(tab.open(path) == Document::Status::Ready, "and the first fixture is back");
            }

            // Back to where the rest of this file expects to find the tab.
            tab.setSidebarVisible(false);
        }
    }

    // ---- a fit mode fits the document, not the page being read ----
    //
    // The decision that broke a loop which spun at 100% of a core for as long
    // as a mixed-size document was open (docs/store-submission.md §100, found
    // while measuring something else): fit width used to fit the current page,
    // so on this fixture -- 595x842 then 400x500 -- the zoom followed the
    // scroll position. The rest of the loop is the horizontal scrollbar:
    // narrow page fitted -> the wide one overflows -> scrollbar appears -> the
    // viewport loses height -> a different page is current -> fit the wide one
    // -> everything fits -> the scrollbar goes -> round again, forever.
    //
    // What is asserted here is only that the fitted zoom is the widest page's.
    // **The cascade itself is not pinned by this file**: it needs the window,
    // the strip and a geometry where the two states disagree, and when it is
    // reintroduced mainwindow_smoke does not go red -- it stops finishing
    // (measured: 253,000 resize events, killed at 300 s). That is an owed test,
    // not a covered one.
    {
        tab.setZoomMode(ZoomController::Mode::FitWidth);
        // A width of its own, so that a resize really is delivered: the block
        // above showed and hid the sidebar, so the viewport the zoom was last
        // computed against is not the one this block is about.
        tab.resize(880, 700);
        waitFor([] { return false; }, 300);
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "page 1 is drawn at fit width");

        // The widest page of this fixture is page 1 at 595 pt. Fit width is
        // that page's fit whatever else is on screen, so the zoom times 595
        // is the viewport's width less the margin the fit leaves.
        const double zoom = tab.zoom();
        const double fittedWidth = 595.0 * zoom;
        const double viewportWidth = view->viewport()->width();
        check(fittedWidth <= viewportWidth && fittedWidth > viewportWidth - 40,
              QStringLiteral("fit width fits the widest page of the document (595 pt at %1 is "
                             "%2 px in a %3 px viewport)")
                  .arg(zoom).arg(fittedWidth).arg(viewportWidth));

        tab.resize(900, 700);
        waitFor([] { return false; }, 200);
        check(waitFor([view] { return view->isRendered(0); }), "and back");
    }

    // ---- reading the file again (docs/review-03.md §3.4) ----
    //
    // For the document that is being regenerated while it is being read. The
    // three things worth pinning are not "it reopens": they are that the
    // reader is put back where they were, that a file which has gone does not
    // take the document off the screen, and that a job standing on this object
    // says no rather than pulling the floor out (§100's ForegroundJob).
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the reload");
        const QString copy = scratch.filePath(QStringLiteral("reload-me.pdf"));
        check(QFile::copy(path, copy), "setup: a copy of the fixture to read and then delete");

        check(tab.open(copy) == Document::Status::Ready, "the copy opens");
        view->goToPage(1);
        tab.setZoom(1.5);
        tab.rotateBy(90);
        check(waitFor([&tab] { return tab.currentPage() == 1; }), "the reader is on page 2 of it");

        QString error;
        check(tab.reload(&error), QStringLiteral("it reads again (%1)").arg(error));
        check(tab.isOpen(), "and is still open");
        check(tab.currentPage() == 1, "on the page the reader was on");
        check(qFuzzyCompare(tab.zoom(), 1.5),
              QStringLiteral("at the zoom they had set (%1)").arg(tab.zoom()));
        check(tab.rotation() == 90,
              QStringLiteral("and the way up they had turned it (%1)").arg(tab.rotation()));
        check(waitFor([view] { return view->isRendered(1); }), "and the page is drawn again");

        // A job standing on the tab: the same guard the window uses.
        {
            const DocumentTab::ForegroundJob job(&tab);
            QString busy;
            check(!tab.reload(&busy), "a reload is refused while a foreground job is running");
            check(!busy.isEmpty(), "and says why");
        }

        // ---- and the file can go, which is the other half of §3.4 ----
        //
        // §104 pinned the opposite of this line. The delete used to fail: PDFium
        // opened the file without FILE_SHARE_DELETE, so for as long as a document
        // was on screen Windows would not let anyone remove or replace it -- §66
        // measured that for the preview pane and fixed it there by reading a copy,
        // and the main window kept the reader's own file. F7 changed the handle
        // (docs/pdfium-plan.md R7, docs/handoff-03.md F7 item 3) because a save by
        // replacement has to be able to move a new file over the one on screen, and
        // the assertion moved with the behaviour: it is the one old assertion this
        // batch is allowed to change, and the change is reported, not hidden.
        //
        // What that used to make unreachable is now reachable and is tested: a
        // document deleted out from under the reader keeps its page on the screen,
        // says it cannot be reloaded, and does not empty the tab. The branch in
        // reload() that answers "it is not there any more" had no way to run before,
        // which is what the comment on this block used to say.
        check(QFile::remove(copy),
              "the open document can be deleted out from under the reader (F7: our handle "
              "shares DELETE; docs/pdfium-plan.md R7)");
        check(!QFileInfo::exists(copy), "and the name is gone");
        QString gone;
        check(!tab.reload(&gone), "reloading a file that has gone says no");
        check(gone.contains(QStringLiteral("不在了")),
              QStringLiteral("and says there is nothing there to read again: %1").arg(gone));
        check(tab.isOpen() && tab.pageCount() == 2,
              "while the document stays on the screen, whole, on the bytes it opened");

        check(tab.open(path) == Document::Status::Ready, "the fixture is back for what follows");
        tab.setZoomMode(ZoomController::Mode::FitWidth);
        view->setRotation(0);
    }

    // ---- the file changes under the reader ----
    //
    // docs/handoff-01.md B: a document being regenerated by whatever is writing
    // it must not be taken off the screen, and must not be reloaded without
    // being asked either. The bar above the page says what happened; the reader
    // presses 重新加载 when they want the new bytes.
    //
    // Driven by an in-place overwrite, which is one of the two ways a writer can
    // touch a file the reader is looking at -- and the only one that was available
    // when this block was written, because the reader's handle did not share DELETE.
    // It does now, so a delete and a replacement reach the bar as well; those two
    // are pinned on the notice itself in tests/pdfiumbackend_smoke.cpp, which is
    // where they were measured. A rename never reaches this copy at all.
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the file-change notice");
        const QString watched = scratch.filePath(QStringLiteral("watched.pdf"));
        check(QFile::copy(path, watched), "setup: a copy of the fixture to watch");

        auto bytesOf = [](const QString &file) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return in.readAll();
        };
        // r+b, not truncate-and-write: the file has to keep its identity while
        // its bytes change, which is the case both the watcher and the reader's
        // open handle have to survive.
        auto overwrite = [](const QString &file, const QByteArray &bytes) {
            QFile out(file);
            if (!out.open(QIODevice::ReadWrite))
                return false;
            out.seek(0);
            const bool written = out.write(bytes) == bytes.size();
            const bool trimmed = out.resize(bytes.size());
            out.flush();
            return written && trimmed;
        };

        const QString noticeText = QStringLiteral("这个文件在磁盘上改过了。");
        const QByteArray manyBytes = bytesOf(directory + QStringLiteral("/many-pages.pdf"));
        check(!manyBytes.isEmpty(), "setup: the 40-page fixture can be read as bytes");

        check(tab.open(watched) == Document::Status::Ready, "the watched copy opens");
        check(tab.pageCount() == 2, "and is the two-page fixture");
        check(!tab.fileChangeNoticeVisible(), "it starts with no notice");

        // ---- one save, several writes, one bar ----
        //
        // The 40-page document goes in in three pieces, each flushed and
        // followed by a pumped pause shorter than the debounce. While they are
        // arriving the bar must stay down: a bar that appears on the first
        // piece is a bar that says "reload me" about half a file.
        bool burstWrites = true;
        const int piece = (manyBytes.size() + 2) / 3;
        int writtenPieces = 0;
        for (int start = 0; start < manyBytes.size(); start += piece) {
            QFile out(watched);
            if (!out.open(QIODevice::ReadWrite)) {
                burstWrites = false;
                break;
            }
            out.seek(start);
            out.write(manyBytes.mid(start, piece));
            out.flush();
            ++writtenPieces;
            check(out.pos() == qMin(start + piece, manyBytes.size()),
                  "setup: a piece of the new document is written");
            // Closed before the wait: one save's several writes, each arriving
            // as a change of its own -- which is the case the debounce is for.
            out.close();
            // 300 ms: shorter than the 400 ms debounce, so a bar that waits for
            // quiet stays down; long enough for the watcher to deliver the
            // signal, so a bar that does not wait is up by the check below.
            waitFor([] { return false; }, 300);
            check(!tab.fileChangeNoticeVisible(),
                  QStringLiteral("no notice while the file is still being written "
                                 "(after piece %1 of 3)")
                      .arg(writtenPieces));
        }
        {
            QFile out(watched);
            if (burstWrites)
                burstWrites = out.open(QIODevice::ReadWrite)
                              && out.resize(manyBytes.size());
        }
        check(burstWrites, "setup: the overwrite burst could be written");

        QElapsedTimer settleClock;
        settleClock.start();
        check(waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 5000),
              "the bar appears once the writing stops");
        note(QStringLiteral("the notice appeared %1 ms after the last write (400 ms debounce plus "
                            "a 400 ms size-stability wait)")
                 .arg(settleClock.elapsed()));
        check(tab.fileChangeNoticeText() == noticeText,
              "and says the file changed on disk");

        // ---- the button reloads, and the bar goes with the old bytes ----
        tab.setCurrentPage(1);
        check(waitFor([&tab] { return tab.currentPage() == 1; }), "the reader moves to page 2");

        auto *reloadButton = tab.findChild<QPushButton *>(QStringLiteral("fileChangeReload"));
        check(reloadButton != nullptr, "the bar has a 重新加载 button");
        if (reloadButton) {
            reloadButton->click();
            check(!tab.fileChangeNoticeVisible(), "pressing it takes the bar down");
            check(tab.isOpen() && tab.pageCount() == 40,
                  QStringLiteral("and the new file is what is open (%1 pages)")
                      .arg(tab.pageCount()));
            check(tab.currentPage() == 1, "on the page the reader was on");
            check(waitFor([view] { return view->isRendered(1); }),
                  "and that page is drawn from the new bytes");
        }

        // ---- a file that is not a PDF any more ----
        //
        // Half a save, or something else entirely. The probe in reload() is
        // what keeps the page on screen when the bytes cannot be read, and the
        // bar is where the button's refusal is said.
        const QByteArray junk(manyBytes.size(), '\0');
        check(overwrite(watched, junk), "setup: the watched file is overwritten with junk");
        check(waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 5000),
              "the bar comes back for the junk");

        const int pagesBefore = tab.pageCount();
        if (reloadButton) {
            reloadButton->click();
            check(tab.fileChangeNoticeVisible(),
                  "a reload that cannot read the file keeps the bar up");
            check(tab.fileChangeNoticeText() != noticeText
                      && !tab.fileChangeNoticeText().isEmpty(),
                  QStringLiteral("and says why instead (%1)").arg(tab.fileChangeNoticeText()));
            check(tab.fileChangeNoticeText().contains(QStringLiteral("PDF")),
                  "the sentence is the backend's about the file not being a PDF");
            check(tab.isOpen(), "the document is still open");
            check(tab.pageCount() == pagesBefore, "with the pages it had");
            check(tab.currentPage() == 1, "still on the same page");
            check(waitFor([view] { return view->hasImageFor(1); }),
                  "and the page is still drawn on screen");
        }

        // (d) A foreground job standing on the tab: the change still raises the
        // bar -- it is news, not a reload -- and the button hands the question
        // to reload(), which refuses with its own sentence.
        {
            check(overwrite(watched, junk), "setup: a change while a foreground job runs");
            const DocumentTab::ForegroundJob job(&tab);
            check(waitFor([&tab, &noticeText] { return tab.fileChangeNoticeVisible()
                                                      && tab.fileChangeNoticeText() == noticeText; },
                          5000),
                  "the bar appears while a print or export is standing on the tab");
            if (reloadButton) {
                reloadButton->click();
                check(tab.fileChangeNoticeVisible(),
                      "the button leaves the bar up for that refusal too");
                check(tab.fileChangeNoticeText().contains(QStringLiteral("打印或导出")),
                      QStringLiteral("with reload()'s sentence (%1)")
                          .arg(tab.fileChangeNoticeText()));
                check(tab.isOpen(), "and the document is untouched");
            }
        }

        // ---- a new change, then the F5 path ----
        check(overwrite(watched, manyBytes), "setup: the file is put back");
        check(waitFor([&tab, &noticeText] { return tab.fileChangeNoticeVisible()
                                                  && tab.fileChangeNoticeText() == noticeText; },
                      5000),
              "a new change puts the notice back in place of the old reason");

        // The window's F5 action calls exactly this. Its success has to take
        // the bar down as the button's does.
        QString reloadError;
        check(tab.reload(&reloadError),
              QStringLiteral("F5 reads the file again (%1)").arg(reloadError));
        check(!tab.fileChangeNoticeVisible(), "and its success takes the bar down too");
        check(tab.pageCount() == 40, "with the 40 pages");

        // ---- the ✕ is a dismissal, not a promise ----
        check(overwrite(watched, junk), "setup: junk again for the dismissal");
        check(waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 5000),
              "the bar is up once more");
        auto *dismiss = tab.findChild<QToolButton *>(QStringLiteral("fileChangeClose"));
        check(dismiss != nullptr, "the bar has a close button");
        if (dismiss) {
            dismiss->click();
            check(!tab.fileChangeNoticeVisible(), "✕ takes it down");
        }
        check(overwrite(watched, manyBytes), "setup: and the file changes again");
        check(waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 5000),
              "the next change brings it back");

        // ---- a file this tab is no longer reading is not this tab's business ----
        check(tab.open(path) == Document::Status::Ready, "the tab goes back to the fixture");
        check(!tab.fileChangeNoticeVisible(), "a new document starts with no notice");
        check(overwrite(watched, junk), "setup: the file the tab used to read changes");
        // Given every chance to deliver a signal: a watch that was not removed
        // would have the whole debounce to say something.
        waitFor([] { return false; }, 1500);
        check(!tab.fileChangeNoticeVisible(),
              "and no notice: switching documents stopped the watch on the old file");
    }

    // ---- the strip shows what a save would write (docs/review-03.md §1.3) ----
    //
    // Every structural edit used to be invisible: delete a page and the page was
    // still there, turn one and it was not turned, until the edits were written
    // to a new file and that file opened in another tab. He chose the cheapest
    // of the ways out: the main view goes on showing the file, and the thumbnail
    // strip shows the pages a save would write -- in that order, that way up.
    //
    // Driven through the dialogs a reader uses, because the edit list belongs to
    // the tab and the tab only lets it be changed that way.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        check(tab.open(book) == Document::Status::Ready, "the 40-page fixture opens");
        const int pages = tab.pageCount();
        check(pages == 40, QStringLiteral("and it is 40 pages (%1)").arg(pages));

        auto *strip = tab.findChild<ThumbnailPanel *>();
        auto *bar = tab.findChild<QTabWidget *>();
        tab.setSidebarVisible(true);
        if (bar && strip)
            bar->setCurrentWidget(strip);
        const int stripTab = bar && strip ? bar->indexOf(strip) : -1;
        const QString plainTitle = stripTab >= 0 ? bar->tabText(stripTab) : QString();

        if (strip && stripTab >= 0) {
            check(strip->rowCount() == pages, "before any edit, a row per page");
            check(!tab.thumbnailsShowPendingEdits(), "and the strip is the file as it is");

            // Delete page 2.
            answerNextDialog<DeletePagesDialog>(QStringLiteral("2"));
            tab.deletePages();
            check(tab.hasUnsavedEdits(), "the deletion is recorded");
            check(strip->rowCount() == pages - 1,
                  QStringLiteral("and the strip has one row fewer (%1 of %2)")
                      .arg(strip->rowCount())
                      .arg(pages));
            check(strip->rowAt(1).sourcePage == 2,
                  QStringLiteral("the second row is now the third page of the file (row 2 shows "
                                 "page %1)").arg(strip->rowAt(1).sourcePage + 1));
            check(tab.thumbnailsShowPendingEdits(), "the tab knows the strip is a preview");
            check(bar->tabText(stripTab) != plainTitle,
                  QStringLiteral("and the strip's own tab says so (\"%1\")")
                      .arg(bar->tabText(stripTab)));
            check(tab.pageCount() == pages, "while the document on screen still has every page");

            // Turn page 1 right.
            answerNextDialog<RotatePagesDialog>(QStringLiteral("1"));
            tab.rotatePages();
            check(strip->rowAt(0).rotation == 90,
                  QStringLiteral("the first row is shown turned a quarter right (%1)")
                      .arg(strip->rowAt(0).rotation));
            check(waitFor([strip] {
                      const QAbstractItemModel *m =
                          static_cast<const QAbstractItemView *>(strip)->model();
                      const QIcon icon = qvariant_cast<QIcon>(m->index(0, 0).data(Qt::DecorationRole));
                      const QList<QSize> sizes = icon.availableSizes();
                      return !sizes.isEmpty() && sizes.first().width() > sizes.first().height();
                  }),
                  "and its picture is landscape, which is what a portrait page turned is");

            // A row is a page of the file, not a position: clicking the second row
            // goes to page 3 of what is on screen.
            Q_EMIT strip->clicked(static_cast<const QAbstractItemView *>(strip)->model()->index(1, 0));
            check(waitFor([&tab] { return tab.currentPage() == 2; }),
                  QStringLiteral("clicking the second row goes to the page it shows (now on %1)")
                      .arg(tab.currentPage() + 1));

            // And back, all the way.
            tab.undoEdit();
            tab.undoEdit();
            check(!tab.hasUnsavedEdits(), "two undos take both edits back");
            check(strip->rowCount() == pages && strip->rowAt(1).sourcePage == 1
                      && strip->rowAt(0).rotation == 0,
                  "and the strip is the file as it is again");
            check(!tab.thumbnailsShowPendingEdits() && bar->tabText(stripTab) == plainTitle,
                  "with its tab back to its plain name");
        }

        tab.setSidebarVisible(false);
        check(tab.open(path) == Document::Status::Ready, "the first fixture is back");
    }

    // ---- the print box no longer warns about forms ----
    //
    // §109's sentence told the reader a filled form prints as empty boxes, which
    // was true of the engine that never initialised a form layer. F7-C drew the
    // appearances through PDFium's form layer -- formpage measured 24,705 pixels
    // inside a field's rectangle, and 0 with that one call switched off -- and the
    // print path renders through the same call as the view, so the warning became
    // false and it is gone with the other one. What is pinned is the shape the
    // withdrawal leaves behind: the box says nothing about forms, and still says
    // everything it needs to about the job.
    {
        QPrinter printer(QPrinter::HighResolution);
        PrintDialog dialog(nullptr, &printer, 0, 2);
        const QString said = labelsOf(&dialog);
        check(!said.contains(QStringLiteral("表单字段")),
              QStringLiteral("no form warning in the print box any more: %1").arg(said));
        check(dialog.windowTitle() == QStringLiteral("打印"),
              QStringLiteral("and the box is still the one the reader prints from: %1")
                  .arg(dialog.windowTitle()));
    }

    // ---- the strip after a move, and the click that follows it ----
    //
    // §105 pinned delete and rotation through the dialogs a reader uses, but
    // reorder had no assertion of its own: the strip's order is what a save
    // would write, and a moved page is where row number and page number part
    // company for every row after it. Driven through 移动页面, the only door the
    // tab opens to its edit list.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        check(tab.open(book) == Document::Status::Ready, "the 40-page book opens for the move");
        const int pages = tab.pageCount();
        check(pages == 40, QStringLiteral("and it is 40 pages (%1)").arg(pages));

        auto *strip = tab.findChild<ThumbnailPanel *>();
        auto *bar = tab.findChild<QTabWidget *>();
        tab.setSidebarVisible(true);
        if (bar && strip)
            bar->setCurrentWidget(strip);
        const int stripTab = bar && strip ? bar->indexOf(strip) : -1;
        const QString plainTitle = stripTab >= 0 ? bar->tabText(stripTab) : QString();

        if (strip && stripTab >= 0) {
            check(strip->rowCount() == pages, "before the move, a row per page");

            // Page 1 to the back. The box opens on 当前页 + 最后面, and typing into
            // its range box switches it to 指定页码 -- what a reader does.
            answerNextDialog<MovePagesDialog>(QStringLiteral("1"));
            tab.movePages();
            check(tab.hasUnsavedEdits(), "the move is recorded");
            check(strip->rowCount() == pages, "a move keeps every row");
            check(strip->rowAt(0).sourcePage == 1,
                  QStringLiteral("the first row is the file's second page (it shows %1)")
                      .arg(strip->rowAt(0).sourcePage + 1));
            check(strip->rowAt(pages - 1).sourcePage == 0,
                  QStringLiteral("and the last row is the page that moved there (it shows %1)")
                      .arg(strip->rowAt(pages - 1).sourcePage + 1));
            check(bar->tabText(stripTab) != plainTitle,
                  QStringLiteral("the strip says it is a preview (\"%1\")")
                      .arg(bar->tabText(stripTab)));
            check(tab.pageCount() == pages,
                  "while the document on screen still has its own order");

            // Row 40 is page 1 of the file, not page 40: the click follows the
            // row's page, and the reader starts somewhere else so the receipt
            // could not already be true before the click.
            view->goToPage(10);
            check(tab.currentPage() == 10, "setup: the reader is on page 11 before the click");
            Q_EMIT strip->clicked(static_cast<const QAbstractItemView *>(strip)->model()->index(
                pages - 1, 0));
            check(waitFor([&tab] { return tab.currentPage() == 0; }),
                  QStringLiteral("clicking the last row goes to the page it shows (now on %1)")
                      .arg(tab.currentPage() + 1));

            tab.undoEdit();
            check(!tab.hasUnsavedEdits(), "one undo takes the move back");
            check(strip->rowAt(0).sourcePage == 0
                      && strip->rowAt(pages - 1).sourcePage == pages - 1,
                  "and the strip is the file as it is again");
        }

        tab.setSidebarVisible(false);
        check(tab.open(path) == Document::Status::Ready, "the first fixture is back after the move");
    }

    // ---- a merge is a source, not a page number ----
    //
    // The merge box picks files through the shell's own dialog, so this stops at
    // the model the box feeds (docs/handoff-01.md C.2): what a save walks must
    // contain a page from another file, and the source must name that file --
    // the two answers the strip's merged row is built from.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: the editor for the merge opened: %1").arg(error));
        const QString donor = directory + QStringLiteral("/many-pages.pdf");
        const int before = editor.pageCount();
        check(editor.mergeFile(donor, {0, 1}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("two pages of the donor come in at the back: %1").arg(error));

        const QVector<DocumentEditor::PageRef> order = editor.pageOrder();
        check(order.size() == before + 2,
              QStringLiteral("the page order is two pages longer (%1 against %2)")
                  .arg(order.size())
                  .arg(before));
        const DocumentEditor::PageRef merged = order.at(before);
        check(merged.source != 0,
              "a merged page carries the file it came from rather than page 1 of this document");
        check(merged.page == 0, "and its own page number inside that file");
        check(QFileInfo(editor.importPathFor(merged.source)).fileName()
                  == QFileInfo(donor).fileName(),
              QStringLiteral("the source names the file to draw it from (%1)")
                  .arg(editor.importPathFor(merged.source)));
        check(editor.importPathFor(0).isEmpty(),
              "while this document's own pages have no file to import from");
    }

    // ---- the properties dialog (PLAN M2) ----
    //
    // Two halves, because they fail apart: what the tab gathers out of the
    // document, and what the dialog makes of it. docs/review-03.md §3.1 is why
    // this is new -- DocMetadata, the backend's metadata() and its test all
    // existed, and no window had ever shown any of it.
    //
    // The gathering is asserted through propertiesInfo() rather than by opening
    // the dialog: showProperties() ends in exec(), and a test that has to find a
    // modal window and press a button on it hangs for the full timeout on the
    // day the window does not appear.
    {
        view->goToPage(1);
        const PropertiesDialog::Info info = tab.propertiesInfo();
        check(QFileInfo(info.path).fileName() == QStringLiteral("text.pdf"),
              "the properties name the file that is open");
        check(info.pageCount == tab.pageCount(), "and its page count");
        check(info.fileBytes == QFileInfo(info.path).size(), "and its size on disk");
        check(info.sizedPage == 1,
              "the page measured is the one being read, not page 1 -- a PDF may mix sizes");
        view->goToPage(0);
        const PropertiesDialog::Info onFirst = tab.propertiesInfo();
        check(onFirst.sizedPage == 0, "reading page 1 measures page 1");
        // This fixture's two pages are deliberately different sizes, which is
        // the whole reason the row names a page instead of claiming one size
        // for the document.
        check(onFirst.pagePoints != info.pagePoints,
              QStringLiteral("and the two pages of this fixture measure differently "
                             "(%1x%2 against %3x%4)")
                  .arg(onFirst.pagePoints.width())
                  .arg(onFirst.pagePoints.height())
                  .arg(info.pagePoints.width())
                  .arg(info.pagePoints.height()));

        PropertiesDialog::Info made = info;
        // A4 by hand, so that the millimetre arithmetic is checked against a
        // number everyone knows rather than against whatever the fixture is.
        made.pagePoints = QSizeF(595, 842);
        // Every field filled except the author, and both dates valid, so that
        // there is exactly one empty value in the dialog. Counted rather than
        // searched for: with several fields empty, "it says （无） somewhere"
        // passes even when the rule that puts it there has been taken out --
        // measured, that is what the first control of this block did.
        made.metadata.title = QStringLiteral("A Title");
        made.metadata.subject = QStringLiteral("A Subject");
        made.metadata.keywords = QStringLiteral("a, b");
        made.metadata.creator = QStringLiteral("A Creator");
        made.metadata.producer = QStringLiteral("A Producer 9.9");
        made.metadata.creationDate = QDateTime(QDate(2026, 1, 2), QTime(3, 4));
        made.metadata.modificationDate = made.metadata.creationDate;
        made.metadata.author.clear();
        PropertiesDialog filled(nullptr, made);
        const QString shown = labelsOf(&filled);
        check(shown.contains(QStringLiteral("text.pdf")), "the dialog shows the file name");
        check(shown.contains(QStringLiteral("A Producer 9.9")),
              "and the producer string, which is the field people copy out");
        check(shown.contains(QStringLiteral("595")) && shown.contains(QStringLiteral("210")),
              "and the page in points and in millimetres (595 pt is 210 mm)");
        check(shown.count(QStringLiteral("（无）")) == 1,
              QStringLiteral("the one empty field says so rather than being blank (counted %1)")
                  .arg(shown.count(QStringLiteral("（无）"))));
        check(!shown.contains(QStringLiteral("没有填写任何")),
              "and a file with one field filled in is not called empty");

        PropertiesDialog bare(nullptr, info);
        check(labelsOf(&bare).contains(QStringLiteral("没有填写任何")),
              "a file that says nothing about itself is called that, once, at the top");
    }

    // ---- zooming still shows a page ----
    //
    // The regression. Changing the zoom drops the images, so the check is that
    // a page comes back afterwards -- and it is checked from nothing, because
    // the "has an image" answer is what a user sees as "not a grey rectangle".
    {
        const double before = view->zoom();
        tab.setZoom(before * 2.0);

        check(!view->hasImageFor(0),
              "changing the zoom drops the image that no longer fits the page");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again at the new size -- this is what was broken");
        check(std::fabs(view->zoom() - before * 2.0) < 0.001,
              "at the zoom that was asked for");
    }

    // ---- rotating still shows a page ----
    {
        const int before = view->rotation();
        tab.rotateBy(90);
        check(view->rotation() == (before + 90) % 360, "the rotation was taken");

        check(!view->hasImageFor(0), "the old image is dropped, because it is the wrong way up");
        check(waitFor([view] { return view->hasImageFor(0); }),
              "and a page is rendered again the right way up");
    }

    // ---- a factor chosen by hand survives a resize ----
    //
    // The second regression: setZoom() used to leave the controller in "fit
    // width", so the next resize (or one notch of the wheel) replaced the
    // factor the user had typed with whatever fitted.
    {
        view->setZoom(2.0);
        check(view->zoomMode() == ZoomController::Mode::Percent,
              "setting a factor by hand takes the view out of a fit mode");
        check(waitFor([view] { return view->hasImageFor(0); }), "and the page is rendered");

        tab.resize(700, 520);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - 2.0) < 0.001,
              "and a resize does not throw it away");

        // While a fit mode is the one in force, a resize does change the zoom --
        // that is the whole point of a fit mode, and the check above would pass
        // just as well if the fit had stopped working altogether.
        view->setZoomMode(ZoomController::Mode::FitWidth);
        const double fitted = view->zoom();
        check(view->zoomMode() == ZoomController::Mode::FitWidth, "fit width is in force");
        tab.resize(520, 700);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        check(std::fabs(view->zoom() - fitted) > 0.001,
              "and a narrower window fits a narrower page");
    }

    // ---- a jump announces one page, not two ----
    //
    // goToPage() moves the scrollbar, which used to run the scroll handler,
    // which recomputed the current page from the middle of the viewport and
    // announced that as well -- so the page box and the status bar flickered
    // through two numbers, the second of which was often the page after the one
    // that was asked for.
    {
        int announcements = 0;
        int last = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                last = page;
            });

        tab.setCurrentPage(1);
        check(announcements == 1,
              QStringLiteral("jumping announces the page once, not once per scroll event (got %1)")
                  .arg(announcements));
        check(last == 1, "and announces the page that was asked for");

        // Off again before the locals go out of scope: the connection lives as
        // long as the view does, and a lambda that captures locals by reference
        // is a use-after-free the moment anything else scrolls.
        QObject::disconnect(listening);
    }

    // ---- a page larger than the screen is rendered as a band ----
    //
    // At 800% a page is 7140 by 10000 device pixels -- 288 MB for the one page,
    // more than the entire render budget, on a machine where "fit width" is
    // already 33 MB a page. A page bigger than a few viewports is therefore
    // rendered as the band that is on screen, and this is the assertion that it
    // happens rather than being described in a comment.
    {
        QVector<RenderToken> asked;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&asked](const RenderToken &token) { asked.append(token); });

        view->setZoom(4.0);
        check(waitFor([view] { return view->hasImageFor(0); }), "a banded page arrives");

        check(!asked.isEmpty(), "the zoomed page was asked for");
        const RenderToken band = asked.last();
        check(!band.clip.isNull(), "as a band rather than as a whole page");
        check(band.imageSize() != band.pixels,
              "so the image is smaller than the page it came from");

        const qint64 viewportPixels = qint64(view->viewport()->width())
                                      * view->viewport()->height();
        check(qint64(band.imageSize().width()) * band.imageSize().height()
                  <= 4 * viewportPixels,
              QStringLiteral("and no bigger than a few screens (%1 px against a %2 px screen)")
                  .arg(band.imageSize().width() * band.imageSize().height())
                  .arg(viewportPixels));

        // And a small scroll stays inside the band that was already rendered. A
        // band that changed on every pixel of scrolling would be a render per
        // frame, which is worse than the memory banding saves.
        const auto requestsForFrontPage = [&asked] {
            int count = 0;
            for (const RenderToken &token : asked) {
                if (token.page == 0)
                    ++count;
            }
            return count;
        };
        const int before = requestsForFrontPage();
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 40);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        check(requestsForFrontPage() == before, "a small scroll does not ask for it again");

        // See the note above the jump test: the connection outlives this block.
        QObject::disconnect(listening);
    }

    // ---- the images held do not grow with how far somebody has scrolled ----
    //
    // This is the one that mattered: the view used to keep every page it had
    // ever been given, so scrolling grew memory without limit while the target
    // file's pages were 33 to 52 MB each. Forty pages, scrolled to the end, has
    // to leave a handful held rather than forty.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        if (QFileInfo::exists(manyPages)) {
            check(tab.open(manyPages) == Document::Status::Ready, "the long fixture opens");
            check(waitFor([view] { return view->hasImageFor(0); }), "its first page is drawn");

            for (int page = 1; page < tab.pageCount(); ++page) {
                view->goToPage(page);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            check(waitFor([view, &tab] { return view->hasImageFor(tab.pageCount() - 1); }),
                  "and so is the last one");

            // One page, the neighbour above and the neighbour below: three, and
            // at most a couple more if the prefetch allowance is generous. Forty
            // would be the unbounded version.
            check(view->heldImageCount() <= 5,
                  QStringLiteral("only the pages in view and nearby are held (holding %1 of %2)")
                      .arg(view->heldImageCount())
                      .arg(tab.pageCount()));
        } else {
            std::cerr << "FAIL: " << manyPages.toStdString() << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        }
    }

    // ---- a render that produced nothing does not leave a page grey ----
    //
    // An out-of-memory render comes back as a null image, and the view used to
    // treat that as no news: the page stayed recorded as "asked for", so nothing
    // ever asked again and the placeholder was permanent. Every page that failed
    // once stayed grey for the rest of the session.
    {
        const QString path2 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path2) == Document::Status::Ready, "the small fixture reopens");

        RenderToken outstanding;
        int requestsForPage0 = 0;
        const QMetaObject::Connection listening = QObject::connect(
            view, &PageView::pageImageNeeded, view,
            [&outstanding, &requestsForPage0](const RenderToken &token) {
                if (token.page == 0) {
                    ++requestsForPage0;
                    outstanding = token;
                }
            });

        // A fresh request, and then failure for it before the event loop can
        // deliver the real one.
        view->setZoom(2.0);
        check(!outstanding.isValid(), "the token captured from the signal has no generation yet");

        const int before = requestsForPage0;
        view->setPageImage(outstanding, QImage());
        check(waitFor([&requestsForPage0, before] { return requestsForPage0 > before; }),
              "the page is asked for again after a render that produced nothing");
        check(waitFor([view] { return view->hasImageFor(0); }), "and ends up with an image");

        // Off again before `outstanding` and the counter go out of scope: the
        // next block scrolls and renders, and a live connection into locals
        // that no longer exist is a crash rather than a test failure.
        QObject::disconnect(listening);
    }

    // ---- the page itself is white, not whatever is behind it ----
    //
    // A PDF page has no background: the renderer draws the content, and the
    // white paper is supplied by whoever shows the result. Draw the content
    // straight onto the surround and the page comes out the surround's colour
    // -- a grey page under perfectly correct text, which is exactly what the
    // report was. Asserted off the widget rather than out of the code, because
    // the colour is the thing the user is looking at.
    {
        view->goToPage(0);
        // Rendered, not merely held: a band from the other end of the page is
        // an image the view is about to replace, and sampling before it lands
        // is sampling the placeholder.
        check(waitFor([view] { return view->isRendered(0); }),
              "the page on screen is fully rendered");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it has somewhere to be");

        const QImage shot = view->grab().toImage();
        check(!shot.isNull(), "the view can be sampled");

        // Inside the page, near its top-left corner, where an ordinary document
        // has margin: paper, and nothing else. The failure message carries
        // everything that decides what that pixel can be, because "not white"
        // is the symptom of five different things.
        const QPoint inside(int(page.left()) + 8, int(page.top()) + 8);
        const QColor onPage = shot.pixelColor(inside);
        check(onPage == QColor(255, 255, 255),
              QStringLiteral("the page is paper-coloured, not the backdrop (%1,%2,%3 at %4,%5; "
                             "page %6,%7 %8x%9; viewport %10x%11; zoom %12; image %13)")
                  .arg(onPage.red())
                  .arg(onPage.green())
                  .arg(onPage.blue())
                  .arg(inside.x())
                  .arg(inside.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height())
                  .arg(view->viewport()->width())
                  .arg(view->viewport()->height())
                  .arg(view->zoom())
                  .arg(view->hasImageFor(0) ? QStringLiteral("held")
                                            : QStringLiteral("missing")));

        // Beside the page, which is where the surround is supposed to show --
        // the same grey it has always been, now that the page is not drawn on
        // top of it.
        const QColor beside = shot.pixelColor(qMax(0, int(page.left()) - 4),
                                             int(page.top()) + 8);
        const QColor expected = ThemeManager::currentColors().pageBackdrop;
        check(beside == expected,
              QStringLiteral("and beside it is still the backdrop (%1,%2,%3, expected %4,%5,%6)")
                  .arg(beside.red())
                  .arg(beside.green())
                  .arg(beside.blue())
                  .arg(expected.red())
                  .arg(expected.green())
                  .arg(expected.blue()));
    }

    // ---- every stylesheet rule points at the asset it means ----
    //
    // A stylesheet is a string, so a rule can name the wrong picture and only a screenshot would
    // notice. One did: the tab's close button was given the combo box's down chevron (the same token
    // two rules away), which reads as "there is a menu here", closes nothing, and stays plausible
    // looking until someone clicks it.
    for (const AppTheme theme : { AppTheme::Default, AppTheme::DefaultDark }) {
        const QString sheet = ThemeManager::styleSheetFor(theme);
        const int at = sheet.indexOf(QStringLiteral("QTabBar::close-button"));
        const int end = at < 0 ? -1 : sheet.indexOf(QLatin1Char('}'), at);
        const QString rule = (at < 0 || end < 0) ? QString() : sheet.mid(at, end - at).trimmed();
        check(rule.contains(QStringLiteral("tab_close_")) && !rule.contains(QStringLiteral("chevron")),
              QStringLiteral("the tab close button names a close image, not another widget's arrow (%1)")
                  .arg(rule.isEmpty() ? QStringLiteral("no rule found") : rule));
    }

    // ---- dragging over the text selects it ----
    //
    // The end-to-end version of textlayer_smoke: real mouse events, on a real
    // page, turned into a real selection. What it checks is not the exact
    // characters -- those depend on the font's metrics, which are not this
    // program's business -- but that a drag selects part of the document's own
    // text, that the same drag backwards selects the same thing, and that a
    // click selects nothing.
    {
        // The tool this block is about. It is also the default, and saying so
        // here is the point: what a drag does is now a mode, and a test that
        // does not name the mode it is exercising is a test that reads as
        // though the old implicit rule were still in force.
        view->setTool(PageView::Tool::Select);

        const QString path3 = directory + QStringLiteral("/text.pdf");
        check(tab.open(path3) == Document::Status::Ready, "the fixture reopens for selecting");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "the page is drawn");

        // The document's own text, to compare a drag against. Select-all is
        // also the one case where the selection covers every page, so it is
        // worth checking on its own.
        tab.selectAll();
        const QString whole = tab.selectedText();
        check(whole.contains(QStringLiteral("XYZPDF one")), "select all reaches the first page");
        check(whole.contains(QLatin1Char('\n')), "and separates the second page from it");
        check(tab.hasSelection(), "select all leaves a selection");
        tab.clearSelection();
        check(!tab.hasSelection(), "and clearing it leaves none");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "the page is on screen");

        // Across the first line. The fixture draws its text near the top left
        // of the page; these fractions of it are inside that line, so the
        // assertions below do not depend on the font being any set size.
        const QPointF left(page.left() + page.width() * 0.12, page.top() + page.height() * 0.09);
        const QPointF right(page.left() + page.width() * 0.30, page.top() + page.height() * 0.09);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, left, true);
        sendMouse(view->viewport(), QEvent::MouseMove, right, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, right);

        const QString dragged = tab.selectedText();
        check(!dragged.isEmpty(),
              QStringLiteral("dragging across the text selects some of it (got \"%1\")")
                  .arg(dragged));
        check(whole.contains(dragged), "and what it selected is part of the document's text");
        check(dragged != whole, "a drag along one line is not the whole document");

        // Backwards over the same span: the same text. This is the case that
        // gets written wrong, and the one a user hits the moment they overshoot
        // a word and drag back over it.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, right, true);
        sendMouse(view->viewport(), QEvent::MouseMove, left, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(tab.selectedText() == dragged, "and the same text dragged backwards");

        // A click, which is not a selection.
        view->clearSelection();
        sendMouse(view->viewport(), QEvent::MouseButtonPress, left);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, left);
        check(!tab.hasSelection(), "a click selects nothing");

        // ---- the pointer says what is under it ----
        //
        // An I-beam over a line of text and an arrow everywhere else, which is
        // how every other program says "this can be selected". The tracking
        // flag is checked as well: without it the handler is never called in
        // the real program, and this test -- which sends its events by hand --
        // would pass anyway.
        check(view->viewport()->hasMouseTracking(),
              "the view is told about moves with no button held");

        sendMouse(view->viewport(), QEvent::MouseMove, left);
        check(view->viewport()->cursor().shape() == Qt::IBeamCursor,
              QStringLiteral("the pointer is an I-beam over text (shape %1 at %2,%3 of a page "
                             "at %4,%5 %6x%7)")
                  .arg(int(view->viewport()->cursor().shape()))
                  .arg(left.x())
                  .arg(left.y())
                  .arg(page.left())
                  .arg(page.top())
                  .arg(page.width())
                  .arg(page.height()));

        sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
        check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
              "and an arrow away from it");

        // And it is visible. The wash is the only part of this a user can see,
        // and a selection that is correct in the clipboard but invisible on the
        // page is a selection nobody would believe in.
        tab.selectAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const QImage shot = view->grab().toImage();
        const QColor washed = shot.pixelColor(left.toPoint());
        check(washed != QColor(255, 255, 255),
              QStringLiteral("the selection is drawn on the page (%1,%2,%3)")
                  .arg(washed.red())
                  .arg(washed.green())
                  .arg(washed.blue()));
        check(washed.blue() > washed.red(), "in the selection colour rather than the page's");
    }

    // ---- the two drag tools do not answer to each other's gestures ----
    //
    // What this milestone was about. Until the tools existed, a drag meant
    // whichever of the two things the pointer was pointing at, and the two
    // features fought over the same gesture: selecting text on a scan was a
    // box, and a box over a line was a selection. Now the mode decides, and
    // this is the pair of assertions that says so -- the same drag, over the
    // same page, doing one thing and then the other.
    //
    // blank.pdf has no text layer, so a selection drag there has nothing to
    // select: in Select mode it must do nothing at all, and in Recognize mode
    // it must draw a box.
    {
        const QString blankPath = directory + QStringLiteral("/blank.pdf");
        if (!QFileInfo::exists(blankPath)) {
            std::cerr << "FAIL: " << blankPath.toStdString()
                      << " is missing; run tools/make_fixtures.py\n";
            ++failures;
        } else {
            check(tab.open(blankPath) == Document::Status::Ready, "the blank fixture opens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            int captures = 0;
            int capturedPage = -1;
            QRectF captured;
            const QMetaObject::Connection listening = QObject::connect(
                view, &PageView::regionCaptured, view,
                [&](int page, const QRectF &points) {
                    ++captures;
                    capturedPage = page;
                    captured = points;
                });

            const QRectF page = view->pageRectInViewport(0);
            const QPointF from(page.left() + page.width() * 0.3,
                               page.top() + page.height() * 0.3);
            const QPointF to(page.left() + page.width() * 0.6,
                             page.top() + page.height() * 0.6);

            // ---- and first: the selection tool, where the same drag does nothing ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseMove, QPointF(4, 2));
            check(view->viewport()->cursor().shape() == Qt::ArrowCursor,
                  "the selection tool's pointer is not a hand or a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
            check(captures == 0, "a selection drag on a page of scans draws no box");
            check(!tab.hasSelection(), "and selects nothing, there being no text to select");

            // ---- then the recognition tool, where it draws the box ----
            view->setTool(PageView::Tool::Recognize);
            sendMouse(view->viewport(), QEvent::MouseMove, from);
            check(view->viewport()->cursor().shape() == Qt::CrossCursor,
                  "the recognition tool's pointer is a crosshair");

            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

            check(captures == 1, "the drag reports exactly one region");
            check(capturedPage == 0, "from the page it was dragged on");
            // The page's own points, not the screen's: a 595x842 page dragged
            // from 30% to 60% is those fractions of 595 and 842, whatever the
            // zoom and the window did to the pixels in between.
            const QSizeF points(595, 842);
            const QRectF expected(0.3 * points.width(), 0.3 * points.height(),
                                  0.3 * points.width(), 0.3 * points.height());
            check(std::fabs(captured.left() - expected.left()) < 1.5
                      && std::fabs(captured.top() - expected.top()) < 1.5
                      && std::fabs(captured.width() - expected.width()) < 1.5
                      && std::fabs(captured.height() - expected.height()) < 1.5,
                  QStringLiteral("in the page's own coordinates (got %1,%2 %3x%4, wanted %5,%6 "
                                 "%7x%8)")
                      .arg(captured.left())
                      .arg(captured.top())
                      .arg(captured.width())
                      .arg(captured.height())
                      .arg(expected.left())
                      .arg(expected.top())
                      .arg(expected.width())
                      .arg(expected.height()));

            // A click is not a region: it is how a box drag is cancelled, and
            // it must not open a window asking to be recognized.
            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, from);
            check(captures == 1, "a click reports no region");

            // ---- and a box over text, which is the case the tool exists for ----
            //
            // A picture inside a page of text, a table read as a picture, a
            // heading in a scan: the box is drawn over whatever is there and
            // nothing is selected. This is what the mode buys over the old
            // "the pointer decides" rule, where a drag here was a selection.
            check(tab.open(directory + QStringLiteral("/text.pdf"))
                      == Document::Status::Ready,
                  "the text fixture reopens");
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

            const QRectF textPage = view->pageRectInViewport(0);
            const QPointF onLine(textPage.left() + textPage.width() * 0.12,
                                 textPage.top() + textPage.height() * 0.09);
            const QPointF across(textPage.left() + textPage.width() * 0.30,
                                 textPage.top() + textPage.height() * 0.09);

            // A box has two dimensions: these two corners are above and below
            // the line, not just either side of it. A drag along one line is a
            // selection gesture, and in this tool it is a box with no height,
            // which the minimum size throws away.
            const QPointF boxFrom(textPage.left() + textPage.width() * 0.08,
                                  textPage.top() + textPage.height() * 0.05);
            const QPointF boxTo(textPage.left() + textPage.width() * 0.40,
                                textPage.top() + textPage.height() * 0.13);

            sendMouse(view->viewport(), QEvent::MouseButtonPress, boxFrom, true);
            sendMouse(view->viewport(), QEvent::MouseMove, boxTo, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, boxTo);
            check(captures == 2, "a box over a line of text is still a box");
            check(!tab.hasSelection(), "and selects nothing");

            // ---- and the selection tool selects the very same line ----
            view->setTool(PageView::Tool::Select);
            sendMouse(view->viewport(), QEvent::MouseButtonPress, onLine, true);
            sendMouse(view->viewport(), QEvent::MouseMove, across, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, across);
            check(captures == 2, "while the selection tool draws no box");
            check(tab.hasSelection(), "and selects the text instead");

            // Off again before the locals go out of scope: the connection
            // outlives this block, and it captures them by reference.
            QObject::disconnect(listening);
        }
    }

    // ---- the hand drags the page and touches nothing on it ----
    //
    // The third tool, and the one with the least to get wrong -- which is why
    // it is worth asserting rather than assuming: a pan that also selected
    // something, or a pan in the wrong direction, is a tool nobody would use
    // twice.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "a document is open for the hand");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the hand");

        view->setTool(PageView::Tool::View);
        // Scrolled somewhere with room in both directions, so that a pan that
        // ran off the end of the document would still be visible as a value
        // that did not move.
        view->horizontalScrollBar()->setValue(100);
        view->verticalScrollBar()->setValue(200);
        const int beforeX = view->horizontalScrollBar()->value();
        const int beforeY = view->verticalScrollBar()->value();
        const QRectF beforeRect = view->pageRectInViewport(0);

        const QRectF page = view->pageRectInViewport(0);

        const QPointF from(page.left() + page.width() * 0.5, page.top() + page.height() * 0.5);
        const QPointF to(from.x() + 40, from.y() + 30);

        sendMouse(view->viewport(), QEvent::MouseMove, from);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor,
              "the hand tool's pointer is an open hand");

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        check(view->viewport()->cursor().shape() == Qt::ClosedHandCursor,
              "which closes while the page is being dragged");
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);
        check(view->viewport()->cursor().shape() == Qt::OpenHandCursor, "and opens again");

        // The paper follows the pointer: dragging right and down shows what is
        // to the left and above, so the scroll position moves against the drag
        // by exactly its own amount.
        check(view->horizontalScrollBar()->value() == beforeX - 40
                  && view->verticalScrollBar()->value() == beforeY - 30,
              QStringLiteral("the page followed the pointer (%1,%2 from %3,%4)")
                  .arg(view->horizontalScrollBar()->value())
                  .arg(view->verticalScrollBar()->value())
                  .arg(beforeX)
                  .arg(beforeY));
        check(qRound(view->pageRectInViewport(0).left() - beforeRect.left()) == 40
                  && qRound(view->pageRectInViewport(0).top() - beforeRect.top()) == 30,
              "which moved the page on screen by the same amount");
        check(!tab.hasSelection(), "and selected nothing");
    }

    // ---- a drag over the second line of a page selects that line ----
    //
    // The reported bug, end to end. On a document with more than one line per
    // page, dragging over any line but the first used to select nothing a reader
    // could see: the index it anchored at was found by bisecting on a predicate
    // a line break makes non-monotone, and came back from somewhere else in the
    // document. It was reported as "selection works in English and not in
    // Chinese" -- the document was Chinese, and every fixture here had exactly
    // one line per page, so the line break was in none of them.
    //
    // The second line of this fixture is at y between 93 and 110 points of a
    // 595x842 page, which is where the drag below is aimed.
    {
        view->setTool(PageView::Tool::Select);
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        const QRectF page = view->pageRectInViewport(0);
        const QPointF from(page.left() + page.width() * 0.15, page.top() + page.height() * 0.115);
        const QPointF to(page.left() + page.width() * 0.35, page.top() + page.height() * 0.125);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        const QString selected = tab.selectedText();
        check(!selected.isEmpty(),
              QStringLiteral("dragging over the second line selects something (got \"%1\")")
                  .arg(selected));
        check(QStringLiteral("second line here").contains(selected),
              QStringLiteral("and it is that line's own text (got \"%1\")").arg(selected));
    }

    // ---- the same box, with the snapshot tool: pixels, and no recognition ----
    //
    // docs/review-03.md §3.3. Copying a crop was reachable before this tool
    // existed -- through the recognition window's 复制图片 button -- which
    // means a reader who wants a picture has to ask for 识别 and wait for the
    // engine to read something nobody will look at. Both halves are asserted:
    // the picture arrives on the clipboard, and no recognition window opens.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a snapshot");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for it");

        view->setTool(PageView::Tool::Snapshot);
        view->clearSelection();
        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();
        QGuiApplication::clipboard()->clear();

        int copiedWidth = 0;
        int copiedHeight = 0;
        QObject::connect(&tab, &DocumentTab::regionCopied, &tab,
                         [&copiedWidth, &copiedHeight](int width, int height) {
                             copiedWidth = width;
                             copiedHeight = height;
                         });

        const QRectF page = view->pageRectInViewport(0);
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);
        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        check(waitFor([&copiedWidth] { return copiedWidth > 0; }),
              "a box drawn with the snapshot tool reports a picture");
        const QImage onClipboard = QGuiApplication::clipboard()->image();
        check(!onClipboard.isNull(), "and the clipboard has one in it");
        check(onClipboard.width() == copiedWidth && onClipboard.height() == copiedHeight,
              QStringLiteral("of the size the window is told to say (%1x%2 against %3x%4)")
                  .arg(onClipboard.width())
                  .arg(onClipboard.height())
                  .arg(copiedWidth)
                  .arg(copiedHeight));
        // A crop of about two fifths of the width and the better part of a
        // tenth of the height, rendered larger than the screen shows it: the
        // numbers are loose because the scale is the recognizer's, but a crop
        // that came out a pixel wide or the whole page would fail this.
        check(onClipboard.width() > 200 && onClipboard.width() < onClipboard.height() * 6,
              QStringLiteral("and a shape that is the box rather than the page (%1x%2)")
                  .arg(onClipboard.width())
                  .arg(onClipboard.height()));

        // The half that is the point of the tool existing.
        waitFor([] { return false; }, 300);
        check(tab.findChildren<OcrDialog *>().size() == dialogsBefore,
              "and no recognition window was opened for a picture nobody asked to read");
        check(!tab.hasSelection(), "nor was any text selected");

        view->setTool(PageView::Tool::Select);
    }

    // ---- a box over the text: read, not selected ----
    //
    // The case the whole feature is for, and the one its own test did not
    // cover: the drag on the blank fixture proves a window opens and says
    // nothing about what is in it. Here the box is drawn around the fixture's
    // line of text, and what has to come back is the words that are in it.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a box over its text");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for the box");

        view->setTool(PageView::Tool::Recognize);
        view->clearSelection();
        const int dialogsBefore = tab.findChildren<OcrDialog *>().size();

        const QRectF page = view->pageRectInViewport(0);
        // Around the line of text, with room to spare: the fixture draws it
        // near the top left of a 595x842 page, at y between 63 and 80 points.
        const QPointF from(page.left() + page.width() * 0.05,
                           page.top() + page.height() * 0.05);
        const QPointF to(page.left() + page.width() * 0.45, page.top() + page.height() * 0.13);

        sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
        sendMouse(view->viewport(), QEvent::MouseMove, to, true);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

        check(!tab.hasSelection(), "a box over text does not select it");

        OcrDialog *dialog = nullptr;
        check(waitFor([&tab, dialogsBefore, &dialog] {
                  const QList<OcrDialog *> dialogs = tab.findChildren<OcrDialog *>();
                  if (dialogs.size() <= dialogsBefore)
                      return false;
                  dialog = dialogs.last();
                  return true;
              }),
              "the box opens a recognition window");

        QPlainTextEdit *recognized = dialog ? dialog->findChild<QPlainTextEdit *>() : nullptr;
        check(recognized != nullptr, "which has somewhere to show the text");
        if (recognized) {
            check(waitFor([recognized] { return recognized->isEnabled(); }, 30000),
                  "and the recognition finishes");
            const QString text = recognized->toPlainText().simplified();
            // The whole line, spelled as the fixture writes it.
            //
            // This was loosened to "a piece of the line" in F7, because this
            // machine's recognizer read the X of "XYZPDF" as ")(" -- and that
            // misreading turned out to be about WHICH engine got the picture, not
            // about the picture. Measured in tests/ocrprobe.cpp over one crop and
            // fourteen preprocessings: en-US reads this line exactly in every one
            // of them, zh-Hans -- which is what this machine's own language list
            // puts first, and therefore what the engine used to pick -- in four.
            // The engine now asks the other script family when its own answer says
            // that is what the picture was, and the assertion is back to the
            // word-for-word question. The control that shows this row is load-
            // bearing is that one: with the second pass removed, it is red again.
            std::cout << "MEASURE ocr answer: [" << text.toStdString() << "]\n";
            check(text == QStringLiteral("XYZPDF one"),
                  QStringLiteral("the recognition reads the line word for word (got \"%1\")")
                      .arg(text));
        }
    }

    // ---- a second recognition after the pool has been idle ----
    //
    // handoff-03 F10. The recognizer runs on a pool of its own, and those are
    // the process's only MTA threads (the GUI thread is an STA). QThreadPool's
    // default expiry is thirty seconds of idleness: when the last idle worker
    // exits, the apartment goes with it and Windows unloads
    // Windows.Media.Ocr.dll. C++/WinRT's activation-factory cache still hands
    // back the factory it kept, and its vtable is then in unmapped memory --
    // measured from a dump: the second recognition after an idle gap died with
    // 0xC0000005 at `call [rax+0x50]`, with the module absent from the process.
    // The pool now keeps its threads for the life of the process, so this pair
    // of boxes -- the second drawn after the default expiry has passed -- is
    // the whole user-facing shape of the bug.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the text fixture is open for a recognition left idle");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and drawn for it");

        view->setTool(PageView::Tool::Recognize);
        view->clearSelection();

        auto readBox = [&](const QString &when) {
            const int before = tab.findChildren<OcrDialog *>().size();
            const QRectF page = view->pageRectInViewport(0);
            // The same box as the test above: around the fixture's line of text.
            const QPointF from(page.left() + page.width() * 0.05,
                               page.top() + page.height() * 0.05);
            const QPointF to(page.left() + page.width() * 0.45,
                             page.top() + page.height() * 0.13);
            sendMouse(view->viewport(), QEvent::MouseButtonPress, from, true);
            sendMouse(view->viewport(), QEvent::MouseMove, to, true);
            sendMouse(view->viewport(), QEvent::MouseButtonRelease, to);

            OcrDialog *dialog = nullptr;
            check(waitFor([&tab, before, &dialog] {
                      const QList<OcrDialog *> dialogs = tab.findChildren<OcrDialog *>();
                      if (dialogs.size() <= before)
                          return false;
                      dialog = dialogs.last();
                      return true;
                  }),
                  when + QStringLiteral(": the box opens a recognition window"));
            QPlainTextEdit *field = dialog ? dialog->findChild<QPlainTextEdit *>() : nullptr;
            check(field != nullptr, when + QStringLiteral(": which has somewhere to show the text"));
            if (!field)
                return QString();
            check(waitFor([field] { return field->isEnabled(); }, 30000),
                  when + QStringLiteral(": and the recognition finishes"));
            return field->toPlainText().simplified();
        };

        // Both boxes read the whole line, word for word -- the same question the
        // block above asks, asked a second time after the idle gap. What this
        // block is about is the second call surviving; the exact spelling is in
        // it too because a second answer that came back as something else would
        // be the same feature failing quietly.
        const QString first = readBox(QStringLiteral("the first recognition"));
        check(first == QStringLiteral("XYZPDF one"),
              QStringLiteral("the first box reads the line word for word (got \"%1\")").arg(first));

        // Past QThreadPool's thirty-second idle expiry, with the clock room for
        // the expiry itself. A failing waitFor here would report the timeout;
        // the control (fix removed) never gets the chance, because the second
        // recognition takes the process down.
        waitFor([] { return false; }, 32000);

        const QString second = readBox(QStringLiteral("the recognition after the wait"));
        check(second == QStringLiteral("XYZPDF one"),
              QStringLiteral("the box drawn after the idle gap reads the same line, word for word "
                             "(got \"%1\", first was \"%2\")")
                  .arg(second, first));
    }

    // ---- searching the document ----
    //
    // The whole way round: a needle typed into the bar, a scan that runs off the
    // UI thread, the count that comes back, stepping through the hits (wrapping
    // at both ends), the highlight on the page, and the search being forgotten
    // when the document it belonged to is closed.
    {
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "the two-page fixture opens for searching");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn for the search");

        // ---- the bar is shown where the search belongs ----
        tab.showFindBar();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        check(tab.findBarWidget() && tab.findBarWidget()->isVisible(),
              "ask for the find bar and it appears");
        if (tab.findBarWidget()) {
            auto *field = tab.findBarWidget()->findChild<QLineEdit *>(QStringLiteral("findField"));
            // The bar's own focus widget rather than hasFocus(), which is false
            // unless the window is active -- and on the offscreen platform it is
            // not. What is being checked is that the caret has been given to the
            // field, which is what Ctrl+F twice is for.
            check(field && tab.findBarWidget()->focusWidget() == field,
                  "with the caret in the field, ready to be typed in");

            // ---- and it is a bar, not half the window ----
            //
            // Found on screen by the gui-probe branch (handoff-01 D), not by any
            // test here: after Ctrl+F the bar measured 360 px in a 720 px page area,
            // because the tab's vertical layout gave the find bar and the reading
            // area equal shares of the spare height -- neither had a stretch factor.
            // Every count was green; only the picture was wrong. The file-change bar
            // (handoff-01 B) sits in the same layout and would have done the same.
            //
            // Asked as heights against the bar's own size hint, so the assertion is
            // about the bug (a bar that grows) rather than about a pixel count that
            // a font change would move.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int barHeight = tab.findBarWidget()->height();
            const int wanted = tab.findBarWidget()->sizeHint().height();
            check(barHeight <= wanted + 8,
                  QStringLiteral("the find bar is as tall as it asks to be, not a share of the window "
                                 "(%1 px against a hint of %2 in a %3 px tab)")
                      .arg(barHeight)
                      .arg(wanted)
                      .arg(tab.height()));
            check(view->height() >= tab.height() - barHeight - 16,
                  QStringLiteral("and the page keeps the rest of the height (%1 of %2)")
                      .arg(view->height())
                      .arg(tab.height()));
        }

        // ---- a needle in two pages ----
        //
        // The fixture says "XYZPDF one" on page 1 and "XYZPDF two" on page 2, so
        // this is a search that has to cross a page boundary to be right.
        tab.startSearch(QStringLiteral("XYZPDF"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              QStringLiteral("two hits in the document (found %1)").arg(tab.searchMatchCount()));
        check(tab.currentSearchMatch() == 0, "and the first of them is in front");
        check(view->currentPage() == 0, "which is on the page that was being read");

        // ---- and the bar says so ----
        //
        // The label was in the bar from the day it was written and nothing ever
        // filled it in: the count was only reachable through this tab's API,
        // which is not where a reader looks for it. Asserted on the text rather
        // than on the API for that reason, and loosely -- the numbers are the
        // requirement, the wording is the bar's business.
        if (tab.findBarWidget()) {
            auto *count = tab.findBarWidget()->findChild<QLabel *>(QStringLiteral("findCount"));
            check(count != nullptr, "the bar has somewhere to show the count");
            if (count) {
                const QString shown = count->text();
                check(shown.contains(QStringLiteral("1")) && shown.contains(QStringLiteral("2")),
                      QStringLiteral("and it says which of how many (got \"%1\")").arg(shown));
            }
        }

        // ---- it is on the page, and visible ----
        {
            const QRectF page = view->pageRectInViewport(0);
            // The fixture draws its text near the top left of a 595x842 page; the
            // view has just been scrolled to the match, so the box is sampled
            // where the match is rather than where it was.
            const QPointF inside(page.left() + page.width() * 0.12,
                                 page.top() + page.height() * 0.09);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QImage shot = view->grab().toImage();
            const QColor marked = shot.pixelColor(inside.toPoint());
            check(marked != QColor(255, 255, 255),
                  QStringLiteral("the hit is washed on the page (%1,%2,%3 at %4,%5)")
                      .arg(marked.red())
                      .arg(marked.green())
                      .arg(marked.blue())
                      .arg(inside.x())
                      .arg(inside.y()));
            check(marked.red() > marked.blue(), "in the highlight colour, not the page's");
        }

        // ---- stepping, and wrapping at both ends ----
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 1, "the next hit is the one on page 2");
        check(view->currentPage() == 1, "and the view went to page 2 with it");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == 0, "stepping past the last hit wraps to the first");
        tab.stepSearch(-1);
        check(tab.currentSearchMatch() == 1, "and stepping back from the first wraps to the last");

        // ---- case, both ways ----
        tab.startSearch(QStringLiteral("xyzpdf"), true);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a case-sensitive search finishes");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and finds nothing in a document that has it in capitals (found %1)")
                  .arg(tab.searchMatchCount()));
        tab.startSearch(QStringLiteral("xyzpdf"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
              "while the same needle without case finds both");

        // ---- nothing to find ----
        tab.startSearch(QStringLiteral("nothing like this is in the document"), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "a needle that is not there finishes");
        check(tab.searchMatchCount() == 0, "with no hits");
        check(tab.currentSearchMatch() == -1, "and nothing in front");
        tab.stepSearch(1);
        check(tab.currentSearchMatch() == -1, "and stepping does nothing rather than crashing");

        tab.startSearch(QString(), false);
        check(waitFor([&tab] { return !tab.isSearching(); }), "an empty needle finishes");
        check(tab.searchMatchCount() == 0, "and matches nothing rather than everything");

        // ---- the hits, as a list in the reading sidebar ----
        //
        // The find bar says how many there are; the sidebar's third tab says
        // where they are, one row per hit, and a row pressed is that hit. Driven
        // through the tab's own search, so the list is fed by the scan that
        // found the hits rather than by a test's copy of them.
        {
            SearchPanel *panel = tab.searchPanelWidget();
            auto *sidebar = tab.findChild<QTabWidget *>(QStringLiteral("readingSidebar"));
            check(panel != nullptr && sidebar != nullptr, "the tab has a search list and a sidebar");
            if (!panel || !sidebar)
                return 1;
            // Four tabs since handoff-07 I1, and the count is of the tab widget rather than of the
            // visible ones: 签名 is *hidden* for an unsigned document by QTabWidget::setTabVisible,
            // which leaves the index the search panel has always been at.
            check(sidebar->count() == 4 && sidebar->tabText(2) == QStringLiteral("搜索")
                      && sidebar->tabText(3) == QStringLiteral("签名"),
                  QStringLiteral("the sidebar's third tab is 搜索 and its fourth is 签名 (%1 tabs)")
                      .arg(sidebar->count()));
            check(!tab.sidebarVisible(), "the sidebar is shut before any of this");
            check(panel->count() == 0, "and the list is empty");

            // A search does not open a closed sidebar. The list is filled all
            // the same, so 列出全部 has something to show when it is pressed.
            tab.startSearch(QStringLiteral("XYZPDF"), false);
            check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
                  "the two hits are back");
            check(panel->count() == 2,
                  QStringLiteral("and both are listed (%1 rows)").arg(panel->count()));
            check(!tab.sidebarVisible() && !sidebar->isVisible(),
                  "a search typed with the sidebar shut leaves it shut");

            check(panel->rowText(0).startsWith(QStringLiteral("第 1 页"))
                      && panel->rowText(1).startsWith(QStringLiteral("第 2 页")),
                  QStringLiteral("each row leads with its page (\"%1\" / \"%2\")")
                      .arg(panel->rowText(0), panel->rowText(1)));
            check(panel->rowText(0).contains(QStringLiteral("XYZPDF one"))
                      && panel->rowText(1).contains(QStringLiteral("XYZPDF two")),
                  QStringLiteral("and carries the page's own words around the hit"));
            for (int row = 0; row < 2; ++row) {
                check(panel->rowText(row).mid(panel->boldFromOfRow(row),
                                              panel->boldToOfRow(row)
                                                  - panel->boldFromOfRow(row))
                          == QStringLiteral("XYZPDF"),
                      QStringLiteral("with the hit in bold (row %1: \"%2\")")
                          .arg(row)
                          .arg(panel->rowText(row).mid(panel->boldFromOfRow(row),
                                                       panel->boldToOfRow(row)
                                                           - panel->boldFromOfRow(row))));
            }

            // ---- the button that asks for the list ----
            QToolButton *listAll = tab.findBarWidget()
                                       ? tab.findBarWidget()->findChild<QToolButton *>(
                                             QStringLiteral("findListAll"))
                                       : nullptr;
            check(listAll != nullptr, "the find bar has a 列出全部 button");
            if (listAll) {
                listAll->click();
                check(tab.sidebarVisible() && sidebar->isVisible(),
                      "pressing it opens the sidebar");
                check(sidebar->currentWidget() == panel,
                      "on the 搜索 tab, because the list is what was asked for");
            }

            // ---- an open sidebar follows a new search ----
            sidebar->setCurrentIndex(1); // the thumbnails, as if the reader were there
            tab.startSearch(QStringLiteral("XYZPDF"), false);
            check(sidebar->currentWidget() == panel,
                  "a search with the sidebar already open switches to its list");
            check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 2; }),
                  "and the scan finishes");

            // ---- a row pressed is that hit ----
            //
            // The view is on page 2 here, so the scan has already put the second
            // hit in front. The row pressed is therefore the *first* one: pressing
            // the row that is already current would pass with the connection
            // broken, which is exactly what the first version of this block did
            // (the control run stayed green and found it).
            check(tab.currentSearchMatch() == 1,
                  "before the press, the hit on the page being read is in front");
            Q_EMIT panel->clicked(panel->model()->index(0, 0));
            check(tab.currentSearchMatch() == 0,
                  QStringLiteral("pressing the first row puts the first hit in front (%1)")
                      .arg(tab.currentSearchMatch()));
            check(panel->currentRow() == 0, "and that row is the selected one");
            check(view->currentPage() == 0, "the view went to the page the row names");
            if (tab.findBarWidget()) {
                auto *count = tab.findBarWidget()->findChild<QLabel *>(QStringLiteral("findCount"));
                check(count && count->text().contains(QStringLiteral("1 / 2")),
                      QStringLiteral("and the bar counts it as the first of two (got \"%1\")")
                          .arg(count ? count->text() : QString()));
            }
            // And the other way, so what is shown is a row followed rather than a
            // page the press happened to land on.
            Q_EMIT panel->clicked(panel->model()->index(1, 0));
            check(tab.currentSearchMatch() == 1 && panel->currentRow() == 1
                      && view->currentPage() == 1,
                  "pressing the second row moves the index, the row and the page to the second hit");

            // ---- stepping moves the highlight in the list too ----
            tab.stepSearch(-1);
            check(tab.currentSearchMatch() == 0 && panel->currentRow() == 0,
                  "the previous hit is marked in the list");
            tab.stepSearch(-1);
            check(tab.currentSearchMatch() == 1 && panel->currentRow() == 1,
                  "and wrapping back marks the row it wrapped to");

            // ---- emptied the two ways it can be ----
            tab.startSearch(QString(), false);
            check(panel->count() == 0 && panel->omittedCount() == 0,
                  "clearing the needle clears the list");

            tab.startSearch(QStringLiteral("XYZPDF"), false);
            check(waitFor([&tab, panel] { return !tab.isSearching() && panel->count() == 2; }),
                  "another search fills it again");
            check(tab.open(path) == Document::Status::Ready,
                  "opening a document again is asked for");
            check(panel->count() == 0,
                  "and a new document starts with no list, whatever was searched before");

            // ---- the thousandth-and-one hit ----
            //
            // A common word in a long document can have more hits than rows worth
            // building. The panel's own contract is what is checked here: the
            // first thousand get rows, and one last row counts the rest -- a
            // number, so "there are more" is not left to be guessed.
            QVector<TextSearch::Match> many;
            many.reserve(SearchPanel::MaximumRows + 5);
            for (int at = 0; at < SearchPanel::MaximumRows + 5; ++at) {
                TextSearch::Match match;
                match.page = at % 40;
                match.from = 0;
                match.to = 6;
                match.snippet.text = QStringLiteral("XYZPDF and more");
                match.snippet.from = 0;
                match.snippet.to = 6;
                many.append(match);
            }
            panel->setMatches(many, 0);
            check(panel->count() == SearchPanel::MaximumRows + 1,
                  QStringLiteral("a thousand hits get rows, and the count goes on one more (%1)")
                      .arg(panel->count()));
            check(panel->omittedCount() == 5,
                  QStringLiteral("the last row counts the five not listed (%1)")
                      .arg(panel->omittedCount()));
            check(panel->matchIndexOfRow(panel->count() - 1) == -1,
                  "and it is not itself a hit");
            check(panel->rowText(panel->count() - 1).contains(QStringLiteral("5")),
                  QStringLiteral("its words say how many (got \"%1\")")
                      .arg(panel->rowText(panel->count() - 1)));

            panel->clearMatches();
            tab.setSidebarVisible(false);
        }

        // ---- the same thing on a document with more than one line per page ----
        //
        // Which is where selection used to break, and a search that highlights
        // the wrong box is the same bug wearing a hat.
        check(tab.open(directory + QStringLiteral("/lines.pdf")) == Document::Status::Ready,
              "the multi-line fixture opens");
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }), "and is drawn");

        tab.startSearch(QStringLiteral("second"), false);
        check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 1; }),
              QStringLiteral("one hit for the word on the second line (found %1)")
                  .arg(tab.searchMatchCount()));

        // ---- whole words, through the box a reader ticks ----
        //
        // The rule itself is textsearch_smoke's; what is checked here is that
        // the box reaches the scan at all. docs/review-03.md §3.2: PLAN M2 asks
        // for 大小写与全词选项 and only the first of the two existed.
        {
            tab.startSearch(QStringLiteral("secon"), false, false);
            check(waitFor([&tab] { return !tab.isSearching() && tab.searchMatchCount() == 1; }),
                  QStringLiteral("part of a word is found when whole words are not asked for "
                                 "(found %1)").arg(tab.searchMatchCount()));
            tab.startSearch(QStringLiteral("secon"), false, true);
            check(waitFor([&tab] { return !tab.isSearching(); }), "and the same with the option on");
            check(tab.searchMatchCount() == 0,
                  QStringLiteral("finds nothing, because it is part of \"second\" (found %1)")
                      .arg(tab.searchMatchCount()));
            check(tab.searchWholeWord(), "and the tab remembers which it was asked for");

            // The box itself, since that is the thing that is new on screen: it
            // starts clear, and ticking it is what asks again.
            auto *whole = tab.findBarWidget()
                              ? tab.findBarWidget()->findChild<QCheckBox *>(
                                    QStringLiteral("findWholeBox"))
                              : nullptr;
            check(whole != nullptr, "the find bar has a whole-word box");
            if (whole) {
                check(!whole->isChecked(), "which starts clear, like every other reader's");
                whole->setChecked(true);
                check(waitFor([&tab] { return tab.searchWholeWord(); }),
                      "and ticking it is what asks the tab again");
                whole->setChecked(false);
                check(waitFor([&tab] { return !tab.searchWholeWord(); }), "and clearing it, back");
            }
            tab.startSearch(QStringLiteral("second"), false, false);
            check(waitFor([&tab] { return !tab.isSearching(); }), "and the state is put back");
        }

        // ---- and a search does not outlive its document ----
        check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
              "another document opens");
        check(tab.searchMatchCount() == 0,
              QStringLiteral("and the search of the previous one is gone (found %1)")
                  .arg(tab.searchMatchCount()));
    }

    // ---- a document is replaced while its pages are still rendering ----
    //
    // The race, made reproducible rather than left to luck: open a document,
    // ask for pages, and replace the document before those renders come back --
    // five times over. A render that is still running is inside the document
    // that load() is about to replace, and the backend's global PDFium lock does
    // not cover the reload itself: this used to end in a fail-fast inside
    // open(), which the test saw as "a page is drawn" timing out and then the
    // process dying.
    //
    // A reader hits it by opening a second document while the first is still
    // painting -- rare on a two-page file, ordinary on a five-hundred-page scan.
    // DocumentTab::open() now waits for the renders in flight before it reloads.
    {
        const QString manyPages = directory + QStringLiteral("/many-pages.pdf");
        const QString small = directory + QStringLiteral("/text.pdf");
        if (QFileInfo::exists(manyPages)) {
            for (int attempt = 0; attempt < 5; ++attempt) {
                check(tab.open(manyPages) == Document::Status::Ready,
                      QStringLiteral("round %1: the long fixture opens").arg(attempt + 1));
                // Deliberately not waited for: the renders are what the next
                // open has to survive.
                view->goToPage(attempt + 3);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

                check(tab.open(small) == Document::Status::Ready,
                      QStringLiteral("round %1: and the next document opens over the renders "
                                     "still in flight")
                          .arg(attempt + 1));
            }
            // Back to the first page before waiting: the loops above left the
            // view scrolled wherever the last document's page 4 was, and a page
            // that is not on screen is never rendered -- which is not the
            // question being asked here.
            view->goToPage(0);
            check(waitFor([view] { return view->isRendered(0); }),
                  "and a page is drawn afterwards, so nothing was left wedged");
        }
    }

    // The recognition a box drag kicks off runs on the global pool. Whatever
    // it decided to do, the test does not leave work behind at exit: a process
    // teardown under a running WinRT call is a hang, not a failure.
    QThreadPool::globalInstance()->waitForDone(30000);

    // ---- 导出为 Word：the tab's own half of it ----
    //
    // The conversion itself is wordexport_smoke's business. What is this test's
    // is the wiring around it: that the work runs off the UI thread, that the
    // answer comes back as a signal, and that the tab knows it is busy -- the
    // last of which is what keeps a document from being reloaded under it.
    {
        const QString path = directory + QStringLiteral("/text.pdf");
        check(tab.open(path) == Document::Status::Ready, "the fixture opens for export");

        QTemporaryDir output;
        check(output.isValid(), "there is somewhere to export to");
        const QString file = output.filePath(QStringLiteral("tab.docx"));

        bool answered = false;
        WordExportReport answer;
        int progressReports = 0;
        int lastReported = 0;
        const QMetaObject::Connection listening = QObject::connect(
            &tab, &DocumentTab::wordExportFinished, &tab, [&](const WordExportReport &report) {
                answered = true;
                answer = report;
            });
        // The progress the window puts in the status bar, which is the only sign
        // of life a long export has -- and the only part of this that has to
        // cross from the worker thread to this one.
        const QMetaObject::Connection progress = QObject::connect(
            &tab, &DocumentTab::wordExportProgress, &tab, [&](int done, int total) {
                ++progressReports;
                lastReported = total > 0 ? done : lastReported;
            });

        tab.exportToWord(file);
        check(waitFor([&answered] { return answered; }, 30000), "the export answers");
        check(progressReports > 0, QStringLiteral("having reported its progress (%1 reports)")
                                       .arg(progressReports));
        check(progressReports == 0 || lastReported == 2,
              QStringLiteral("up to the last page (%1)").arg(lastReported));
        QObject::disconnect(progress);

        check(answer.ok, QStringLiteral("with a file written (%1)").arg(answer.error));
        check(answer.path == file, "at the path it was given");
        check(answer.pages == 2, QStringLiteral("of both pages (%1)").arg(answer.pages));
        check(QFileInfo::exists(file) && QFileInfo(file).size() > 0, "and it is on disk");
        check(!tab.isExporting(), "and the tab is no longer busy");

        QObject::disconnect(listening);
    }

    // ---- 划线翻译：nothing leaves this machine before the reader says so ----
    //
    // The two things this feature must never do are ask a service nobody
    // configured, and send the selected words to a service the reader has not
    // agreed to. Both are asserted on the real path -- DocumentTab::translateText,
    // the window's own -- because the rule lives in that function, and a test of
    // the arithmetic alone (translate_smoke) would still pass if the window forgot
    // to ask. What "did not send" is read off is the absence of the translation
    // window: it is created only after both gates open.
    //
    // The key store is the in-memory one installed below. An automated test must
    // not use the Windows credential manager -- it would leave an entry on the
    // machine running the suite, and could read back a real key and send a real
    // request -- so the real store is verified by hand once instead, and the
    // accepted-consent branch, which necessarily starts a request, is left out of
    // this file on purpose rather than run against a service.
    {
        struct BoxReading
        {
            bool seen = false;
            QString text;
            QString informative;
            QString acceptText;
        };

        // The boxes are window-modal and run their own event loop, so the answer
        // has to be pressed from a timer that fires inside it. A timer that finds
        // no box leaves `seen` false, which is what makes "the box did not appear"
        // a red assertion rather than a hang.
        auto pressWhenBoxOpens = [](BoxReading *reading, bool accept) {
            QTimer::singleShot(0, [reading, accept] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                reading->seen = true;
                reading->text = box->text();
                reading->informative = box->informativeText();
                QAbstractButton *target = box->button(QMessageBox::Cancel);
                for (QAbstractButton *button : box->buttons()) {
                    if (box->buttonRole(button) != QMessageBox::AcceptRole)
                        continue;
                    reading->acceptText = button->text();
                    if (accept)
                        target = button;
                }
                if (target)
                    target->click();
                else
                    box->reject();
            });
        };

        std::unique_ptr<Translate::KeyStore> store = Translate::makeMemoryKeyStore();
        Translate::KeyStore *keys = store.get();
        tab.setTranslateKeyStoreForTesting(std::move(store));

        int setupAsks = 0;
        const QMetaObject::Connection setup =
            QObject::connect(&tab, &DocumentTab::translationSetupRequested, &tab,
                             [&setupAsks] { ++setupAsks; });

        // ---- no service chosen ----
        {
            AppSettings::setTranslateProvider(QString());
            BoxReading reading;
            pressWhenBoxOpens(&reading, true);
            tab.translateText(QStringLiteral("hello"));
            check(reading.seen, "an unconfigured translate asks rather than sending");
            check(reading.text.contains(QStringLiteral("API 密钥")),
                  QStringLiteral("and says what is missing (%1)").arg(reading.text));
            check(!reading.acceptText.isEmpty(), "the box offers a way to the settings");
            check(reading.acceptText == QStringLiteral("去设置"),
                  QStringLiteral("named as the thing it does (%1)").arg(reading.acceptText));
            check(setupAsks == 1, QStringLiteral("「去设置」 asks the window for the settings (%1)")
                                       .arg(setupAsks));
            check(tab.findChildren<TranslateDialog *>().isEmpty(),
                  "and no translation window was opened, so nothing was asked of a service");
        }

        // ---- a service chosen, no key for it ----
        {
            AppSettings::setTranslateProvider(QStringLiteral("deepl"));
            keys->remove(QStringLiteral("deepl"), nullptr);
            BoxReading reading;
            pressWhenBoxOpens(&reading, false);
            tab.translateText(QStringLiteral("hello"));
            check(reading.seen, "a service without a key is still unconfigured, not a request");
            check(reading.text.contains(QStringLiteral("API 密钥")),
                  QStringLiteral("and the same sentence says so (%1)").arg(reading.text));
            check(setupAsks == 1, "choosing 取消 does not ask for the settings");
            check(tab.findChildren<TranslateDialog *>().isEmpty(),
                  "and nothing was sent");
        }

        // ---- a key, but no agreement to talk to that service ----
        {
            QString ignored;
            keys->write(QStringLiteral("deepl"), QStringLiteral("probe-key-not-a-real-key"), &ignored);
            AppSettings::setTranslateConsent(QStringLiteral("deepl"), false);
            BoxReading reading;
            pressWhenBoxOpens(&reading, false);
            tab.translateText(QStringLiteral("hello"));
            check(reading.seen, "a configured service is asked about before its first request");
            check(reading.text.contains(QStringLiteral("DeepL")),
                  QStringLiteral("naming the service the words would go to (%1)").arg(reading.text));
            check(reading.text.contains(QStringLiteral("api.deepl.com")),
                  QStringLiteral("and the host it would go to (%1)").arg(reading.text));
            check(!reading.informative.isEmpty(), "and what it says will not be sent");
            check(!reading.acceptText.isEmpty(), "with a button that means yes");
            check(!AppSettings::translateConsentGiven(QStringLiteral("deepl")),
                  "declining records no agreement");
            check(tab.findChildren<TranslateDialog *>().isEmpty(), "and sends nothing");
        }

        // ---- agreement belongs to one service ----
        {
            keys->write(QStringLiteral("google"), QStringLiteral("probe-google-key"), nullptr);
            AppSettings::setTranslateConsent(QStringLiteral("deepl"), true);
            AppSettings::setTranslateProvider(QStringLiteral("google"));
            BoxReading reading;
            pressWhenBoxOpens(&reading, false);
            tab.translateText(QStringLiteral("hello"));
            check(reading.seen,
                  "agreeing to DeepL is not agreement to send the same words to Google");
            check(reading.text.contains(QStringLiteral("Google Cloud Translation")),
                  QStringLiteral("and the new service is named (%1)").arg(reading.text));
            check(reading.text.contains(QStringLiteral("translation.googleapis.com")),
                  QStringLiteral("with its own host (%1)").arg(reading.text));
            check(!AppSettings::translateConsentGiven(QStringLiteral("google")),
                  "declining leaves Google un-agreed, while DeepL keeps its agreement");
            check(AppSettings::translateConsentGiven(QStringLiteral("deepl")),
                  "which is what per-service consent means");

            // Back to the state the rest of the file (and the next run) expects: no
            // agreement on file for either service, and no service chosen.
            AppSettings::setTranslateConsent(QStringLiteral("deepl"), false);
            keys->remove(QStringLiteral("deepl"), nullptr);
            keys->remove(QStringLiteral("google"), nullptr);
            AppSettings::setTranslateProvider(QString());
        }

        QObject::disconnect(setup);
    }

    // ---- the form the reader fills: one path from a click to bytes on disk ----
    //
    // handoff-04 G1's test list asks for the whole route in this file: click a field,
    // type, see the star, save, read /V back with qpdf (a second reader, not the engine
    // that wrote it), and re-render to see the new value drawn. Each layer is pinned on
    // its own in pdfiumbackend_smoke; this block exists for the seams, because the view's
    // routing, the tab's dirty answer, the two-stage save and the reopen are four places a
    // mistake can hide while every layer still says something correct by itself.
    //
    // The file is written into a scratch directory. Nothing here opens or touches
    // tests/fixtures, and the one thing this slice must never do is write the reader's own
    // PDF -- AGENTS.md's rule -- so a test that wrote a fixture would be a test breaking
    // the rule it means to check.
    {
        QTemporaryDir formDir;
        check(formDir.isValid(), "setup: a scratch folder for the form path");
        const QString formPath = formDir.filePath(QStringLiteral("fillable.pdf"));
        QString why;
        check(FormSampleFile::write(formPath, 0, false, &why),
              QStringLiteral("fill: the fillable sample is written: %1").arg(why));
        const FormSampleFile::Geometry fields;

        DocumentTab tab;
        tab.resize(900, 700);
        tab.show();
        check(tab.open(formPath) == Document::Status::Ready,
              QStringLiteral("fill: the sample opens in a tab (%1)").arg(tab.lastError()));
        PageView *view = viewOf(tab);
        check(waitFor([view] { return view->isRendered(0); }),
              "fill: and the page with the fields on it is drawn");

        // A field's centre as the view draws it: user space flipped to a top-left origin
        // and scaled into the rectangle the page occupies. That is pagePointFor() run
        // backwards, which is the mapping the click itself uses -- so a test with its own
        // arithmetic would be measuring its arithmetic.
        const auto fieldPoint = [view](const QRectF &rect) {
            const QPointF centre = FormSampleFile::userCentre(rect);
            const QRectF page = view->pageRectInViewport(0);
            return QPointF(page.left() + centre.x() / 595.0 * page.width(),
                           page.top() + (842.0 - centre.y()) / 842.0 * page.height());
        };
        const QPointF textFieldOnScreen = fieldPoint(fields.textField);
        QWidget *canvas = view->viewport();

        check(!tab.hasUnsavedEdits(), "fill: opening a document is not a change to it");
        check(tab.title() == QStringLiteral("fillable.pdf"),
              QStringLiteral("fill: and the title carries no star (%1)").arg(tab.title()));

        // ---- the click, through the view ----
        sendMouse(canvas, QEvent::MouseButtonPress, textFieldOnScreen);
        sendMouse(canvas, QEvent::MouseButtonRelease, textFieldOnScreen);
        check(waitFor([&tab] { return tab.view()->fieldHasFocus(); }),
              "fill: a click on the text field gives it the keyboard");
        check(view->viewport()->cursor().shape() == Qt::IBeamCursor,
              QStringLiteral("fill: and the pointer over the field is an I-beam (got %1)")
                  .arg(int(view->viewport()->cursor().shape())));

        // ---- the keystrokes, and what the tab says about them ----
        view->setFocus();
        const auto type = [view](const QString &text) {
            for (const QChar character : text) {
                QKeyEvent press(QEvent::KeyPress,
                                 int(Qt::Key_A) + character.toLatin1() - 'A', Qt::NoModifier,
                                 QString(character));
                QApplication::sendEvent(view, &press);
            }
        };
        type(QStringLiteral("ABC"));
        check(waitFor([&tab] { return tab.hasUnsavedEdits(); }),
              "fill: three characters are an unsaved change");
        check(tab.hasFieldEdits(), "and the tab knows it came from a field");
        check(tab.title() == QStringLiteral("fillable.pdf *"),
              QStringLiteral("fill: the title carries the star (%1)").arg(tab.title()));
        note(QStringLiteral("fill summary: [%1] undoName=[%2] canUndo=%3")
                 .arg(tab.pendingEditsSummary(), tab.undoEditName())
                 .arg(tab.canUndoEdits() ? 1 : 0));
        check(tab.pendingEditsSummary().contains(QStringLiteral("字段")),
              "fill: and the summary names the fields the reader changed");
        check(tab.undoEditName().isEmpty(),
              "fill: with the keyboard in a field, 撤销 names no page operation");

        // ---- the picture on the screen follows the keystroke ----
        //
        // This is the row RenderCache::dropPage() earns. A keystroke changes what a page
        // looks like without changing what an image of it is an answer to -- same page, same
        // band, same size, same rotation -- and token equality ignores the generation
        // (rendertoken.h says so), so the entry the tab's cache holds for that page is
        // exactly the page as it was before the character. Everything downstream still looks
        // right: the view asks again, the tab answers, and the answer is the old picture. Only
        // a pixel count taken off the screen sees it.
        //
        // Counted rather than hashed, because the focused field also draws a caret and that
        // blinks on a timer: an exact comparison would be a coin flip about the blink phase.
        // Two characters of text are tens of pixels of ink, so a caret cannot hide behind the
        // margin -- and the margin is printed with the reading, so the row can be audited.
        const auto fieldOnScreen = [view](const QRectF &rect) {
            const QRectF page = view->pageRectInViewport(0);
            const double scaleX = page.width() / 595.0;
            const double scaleY = page.height() / 842.0;
            // `rect` is in user space (origin bottom-left), so the displayed top edge is the
            // page height minus the box's upper y.
            return QRect(QPoint(int(page.left() + rect.left() * scaleX),
                                int(page.top() + (842.0 - rect.bottom()) * scaleY)),
                         QSize(qMax(1, int(rect.width() * scaleX)),
                               qMax(1, int(rect.height() * scaleY))));
        };
        const auto inkOnScreen = [view, fieldOnScreen](const QRectF &rect) {
            const QImage whole = view->grab().toImage();
            const QRect box = fieldOnScreen(rect).intersected(whole.rect());
            if (box.isEmpty())
                return -1;
            const QImage crop = whole.copy(box);
            int ink = 0;
            for (int y = 0; y < crop.height(); ++y) {
                for (int x = 0; x < crop.width(); ++x) {
                    if (qGray(crop.pixel(x, y)) < 250)
                        ++ink;
                }
            }
            return ink;
        };
        const int inkAfterClick = inkOnScreen(fields.textField);
        type(QStringLiteral("DD"));
        check(waitFor([&tab] { return tab.hasFieldEdits(); }),
              "fill: a second pair of characters is another change");
        check(waitFor([view] { return view->isRendered(0); }),
              "fill: and the page is drawn again after the keystroke");
        const int inkAfterTyping = inkOnScreen(fields.textField);
        note(QStringLiteral("fill screen ink in the field box: after the click=%1 after two "
                            "characters=%2 (the crop is %3x%4)")
                 .arg(inkAfterClick)
                 .arg(inkAfterTyping)
                 .arg(fieldOnScreen(fields.textField).width())
                 .arg(fieldOnScreen(fields.textField).height()));
        check(inkAfterTyping > inkAfterClick + 20,
              "fill: the field on the screen shows what the reader just typed");
        // Back to where this row started, so the block below finds the field focused with
        // the characters it expects -- and the picture has to come back to its own ink count.
        // Through the view, not the surface: the view is what drains PDFium's repaint
        // queue, and a change that is never drained is a change that is never drawn --
        // which is the other half of what these two rows are checking.
        view->undoInField();
        view->undoInField();
        check(waitFor([view] { return view->isRendered(0); }), "fill: the undo is drawn too");
        const int inkAfterUndo = inkOnScreen(fields.textField);
        note(QStringLiteral("fill screen ink after undoing both characters: %1").arg(inkAfterUndo));
        // Compared with the picture that had the characters in it, not with the one that
        // had none: what an undone keystroke leaves behind is the reader's own selection
        // over the text that came back, and a washed band is hundreds of pixels of ink no
        // value can be read off. That the ink falls at all is the point -- it says the undo
        // reached the screen, which is the thing this row is about.
        check(inkAfterUndo < inkAfterTyping,
              QStringLiteral("fill: the undo is drawn on the screen too (%1 against %2)")
                  .arg(inkAfterUndo)
                  .arg(inkAfterTyping));
        // The next rows need the keyboard in the field again, which is what a click gives.
        sendMouse(canvas, QEvent::MouseButtonPress, textFieldOnScreen);
        sendMouse(canvas, QEvent::MouseButtonRelease, textFieldOnScreen);
        check(waitFor([&tab] { return tab.view()->fieldHasFocus(); }),
              "fill: refocused for the undo rows below");

        // 撤销 while the field has the keyboard is the field's own, and it is per
        // keystroke: measured in pdfiumbackend_smoke, one undo after three characters takes
        // one character back. So the star stays until the last one is gone -- which is the
        // reading a reader expects from a field, and the one that would be wrong if the
        // keystroke had quietly gone to the page list instead.
        tab.undoEdit();
        check(tab.hasUnsavedEdits(), "fill: one 撤销 in a field takes one keystroke back");
        tab.undoEdit();
        tab.undoEdit();
        check(waitFor([&tab] { return !tab.hasUnsavedEdits(); }),
              "fill: undoing all three puts the file's value back, and the star with it");

        // A click on paper commits and lets go; the page is the reader's again.
        const QPointF paper = fieldPoint(QRectF(300, 300, 40, 40));
        sendMouse(canvas, QEvent::MouseButtonPress, paper);
        sendMouse(canvas, QEvent::MouseButtonRelease, paper);
        check(waitFor([&tab] { return !tab.view()->fieldHasFocus(); }),
              "fill: a click on blank paper takes the keyboard away from the field");

        // ---- the input method, in one commit ----
        sendMouse(canvas, QEvent::MouseButtonPress, textFieldOnScreen);
        sendMouse(canvas, QEvent::MouseButtonRelease, textFieldOnScreen);
        check(waitFor([&tab] { return tab.view()->fieldHasFocus(); }),
              "fill: refocused for the input-method commit");
        {
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("中文"));
            QApplication::sendEvent(view, &commit);
        }
        check(waitFor([&tab] { return tab.hasFieldEdits(); }),
              "fill: a committed string is one edit to one field");

        // ---- the save: the answers reach the file, and only the save writes it ----
        const QByteArray beforeSave = readAllBytes(formPath);
        check(tab.saveEdits(), "fill: 保存 writes the filled field back in place");
        note(QStringLiteral("fill: the reader's file went from %1 to %2 bytes")
                 .arg(beforeSave.size())
                 .arg(readAllBytes(formPath).size()));
        check(readAllBytes(formPath) != beforeSave,
              "fill: and the bytes really moved -- this is the one path allowed to write");
        check(!tab.hasUnsavedEdits(), "fill: after a save nothing is unsaved");
        check(tab.title() == QStringLiteral("fillable.pdf"),
              "fill: and the star is off again");

        // qpdf is the second reader: the value has to be in the file's own /V, not only in
        // whatever PDFium drew.
        {
            QPDF read;
            read.processFile(formPath.toUtf8().constData());
            QString value;
            std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(read).getAllPages();
            for (QPDFPageObjectHelper &page : pages) {
                for (const QPDFObjectHandle &annot : page.getAnnotations()) {
                    if (annot.getKey("/T").getStringValue() == "name") {
                        // getUTF8Value, not getStringValue: PDFium writes a value that is
                        // not plain ASCII as a UTF-16BE string with a byte order mark, and
                        // the raw bytes of that are what getStringValue hands back.
                        value = QString::fromUtf8(annot.getKey("/V").getUTF8Value().c_str());
                    }
                }
            }
            note(QStringLiteral("fill: qpdf reads the field name=[%1]").arg(value));
            check(value.contains(QStringLiteral("中文")),
                  "fill: the committed string is in the file the reader opened");
            check(!value.contains(QStringLiteral("ABC")),
                  "fill: and the three characters that were undone are not in it");
        }

        // ---- and the reopened page draws it ----
        //
        // The pixel half the handoff asks for. Measured as a difference between two
        // renders of one rectangle, the file before the save and the file after: the
        // instrument sec.89 uses, because "there are pixels in the field" would still be
        // true with the old value painted in it.
        {
            const QString keptBefore = formDir.filePath(QStringLiteral("before.pdf"));
            check(writeAllBytes(keptBefore, beforeSave),
                  "fill: setup: the file as it was before the save is kept to compare");
            const QRectF box = fields.textField;
            const auto inkOf = [&box](const QString &path) {
                PdfiumBackend reader;
                if (reader.load(path, QString()) != Document::Status::Ready)
                    return QStringLiteral("(no picture)");
                RenderOptions options;
                const QImage image = reader.render(0, QSize(595, 842), options);
                if (image.isNull())
                    return QStringLiteral("(no picture)");
                // The field's device rectangle at this size: the page is drawn 1:1 and
                // unrotated, so the flip is the page height and nothing else.
                const int left = int(box.left());
                const int right = int(box.right());
                // box is in user space (origin bottom-left), so the displayed top edge
                // is the page height minus the box's *upper* y -- flipped the other way
                // round the rectangle collapses to nothing and every count is a zero
                // about an empty crop rather than about the page.
                const int top = int(842.0 - box.bottom());
                const int bottom = int(842.0 - box.top());
                quint64 hash = 1469598103934665603ULL;
                int ink = 0;
                for (int y = top; y < bottom; ++y) {
                    for (int x = left; x < right; ++x) {
                        const QRgb pixel = image.pixel(x, y);
                        if (qGray(pixel) < 250)
                            ++ink;
                        hash = (hash ^ quint64(pixel)) * 1099511628211ULL;
                    }
                }
                return QStringLiteral("ink=%1 hash=%2").arg(ink).arg(hash, 0, 16);
            };
            const QString before = inkOf(keptBefore);
            const QString after = inkOf(formPath);
            note(QStringLiteral("fill render field pixels: before [%1] after [%2]")
                     .arg(before, after));
            check(before != after,
                  "fill: the field is drawn with the value the reader put in it");
        }

        // ---- another answer, and 另存为 with it ----
        sendMouse(canvas, QEvent::MouseButtonPress, textFieldOnScreen);
        sendMouse(canvas, QEvent::MouseButtonRelease, textFieldOnScreen);
        check(waitFor([&tab] { return tab.view()->fieldHasFocus(); }),
              "fill: refocused for the save-as");
        type(QStringLiteral("XYZ"));
        check(waitFor([&tab] { return tab.hasFieldEdits(); }),
              "fill: and the tab is dirty again");
        const QString saveAsPath = formDir.filePath(QStringLiteral("copy-with-answers.pdf"));
        QString saveError;
        check(tab.saveEditsTo(saveAsPath, &saveError),
              QStringLiteral("fill: 另存为 writes the filled document out (%1)")
                  .arg(saveError));
        check(QFileInfo::exists(saveAsPath), "fill: and the new file is there");
        {
            const QString copyValue
                = FormSampleFile::fieldValue(saveAsPath, QStringLiteral("name"), &why);
            note(QStringLiteral("fill: the 另存为 copy reads name=[%1]").arg(copyValue));
            check(copyValue.contains(QStringLiteral("XYZ")),
                  "fill: the new file carries the answer typed after the save");
        }

        // ---- what one character costs on a page that is slow to draw ----
        //
        // handoff-04 G1 3 asks for this number, and asks for it because the UI thread and
        // the renderer share one lock: a keystroke that has to be drawn again waits behind
        // whatever render is running, and on a scanned page that is tens of milliseconds
        // rather than less than one. The row is the reading; the bound is a hair trigger
        // for the day a keystroke starts costing a second, which would be a different bug
        // than being slow on a scan.
        {
            const QString heavy = formDir.filePath(QStringLiteral("heavy-form.pdf"));
            {
                QPDF qpdf;
                qpdf.processFile((directory + QStringLiteral("/scan.pdf")).toUtf8().constData());
                std::vector<QPDFPageObjectHelper> pages =
                    QPDFPageDocumentHelper(qpdf).getAllPages();
                QPDFObjectHandle page = pages.front().getObjectHandle();
                const QPDFObjectHandle box = pages.front().getMediaBox();
                const double width = box.getArrayItem(2).getNumericValue();
                const double height = box.getArrayItem(3).getNumericValue();
                QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
                field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
                field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
                field.replaceKey("/T", QPDFObjectHandle::newString("name"));
                field.replaceKey("/V", QPDFObjectHandle::newString("typed"));
                field.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 10 Tf 0 g "));
                QPDFObjectHandle rect = QPDFObjectHandle::newArray();
                rect.appendItem(QPDFObjectHandle::newReal(width * 0.1));
                rect.appendItem(QPDFObjectHandle::newReal(height * 0.1));
                rect.appendItem(QPDFObjectHandle::newReal(width * 0.5));
                rect.appendItem(QPDFObjectHandle::newReal(height * 0.16));
                field.replaceKey("/Rect", rect);
                QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
                font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
                font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
                font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
                font = qpdf.makeIndirectObject(font);
                QPDFObjectHandle resources = page.getKey("/Resources");
                if (!resources.isDictionary()) {
                    resources = QPDFObjectHandle::newDictionary();
                    page.replaceKey("/Resources", resources);
                }
                QPDFObjectHandle fonts = resources.getKey("/Font");
                if (!fonts.isDictionary()) {
                    fonts = QPDFObjectHandle::newDictionary();
                    resources.replaceKey("/Font", fonts);
                }
                fonts.replaceKey("/Helv", font);
                field = qpdf.makeIndirectObject(field);
                field.replaceKey("/P", page);
                QPDFObjectHandle annots = QPDFObjectHandle::newArray();
                annots.appendItem(field);
                page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
                QPDFObjectHandle formFields = QPDFObjectHandle::newArray();
                formFields.appendItem(field);
                QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
                form.replaceKey("/Fields", formFields);
                form.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 10 Tf 0 g "));
                qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
                QPDFWriter writer(qpdf, heavy.toUtf8().constData());
                writer.write();
            }

            DocumentTab heavyTab;
            heavyTab.resize(900, 700);
            heavyTab.show();
            check(heavyTab.open(heavy) == Document::Status::Ready,
                  QStringLiteral("fill: the scanned page with a field on it opens (%1)")
                      .arg(heavyTab.lastError()));
            PageView *heavyView = viewOf(heavyTab);
            check(waitFor([heavyView] { return heavyView->isRendered(0); }),
                  "fill: and the heavy page is drawn once before anything is timed");
            PdfiumBackend measured;
            check(measured.load(heavy, QString()) == Document::Status::Ready,
                  "fill: setup: the file is opened again to be asked how big its page is");
            const QSizeF heavyPoints = measured.pageSize(0);
            note(QStringLiteral("fill heavy: page 1 is %1x%2 pt and the view draws it %3x%4 px")
                     .arg(heavyPoints.width())
                     .arg(heavyPoints.height())
                     .arg(heavyView->pageRectInViewport(0).width())
                     .arg(heavyView->pageRectInViewport(0).height()));
            const QRectF heavyField(heavyPoints.width() * 0.1, heavyPoints.height() * 0.1,
                                    heavyPoints.width() * 0.4, heavyPoints.height() * 0.06);
            const QPointF centre = FormSampleFile::userCentre(heavyField);
            const QRectF drawn = heavyView->pageRectInViewport(0);
            const QPointF onScreen(drawn.left() + centre.x() / heavyPoints.width() * drawn.width(),
                                   drawn.top()
                                       + (heavyPoints.height() - centre.y()) / heavyPoints.height()
                                             * drawn.height());
            QWidget *heavyCanvas = heavyView->viewport();
            sendMouse(heavyCanvas, QEvent::MouseButtonPress, onScreen);
            sendMouse(heavyCanvas, QEvent::MouseButtonRelease, onScreen);
            check(waitFor([&heavyTab] { return heavyTab.view()->fieldHasFocus(); }),
                  "fill: the field on the scanned page takes the keyboard");
            heavyView->setFocus();

            qint64 totalMilliseconds = 0;
            const int rounds = 5;
            for (int round = 0; round < rounds; ++round) {
                QElapsedTimer clock;
                clock.start();
                const int ascii = 0x41 + round; // 'A'
                QKeyEvent key(QEvent::KeyPress, ascii, Qt::NoModifier,
                              QString(QChar(static_cast<ushort>(ascii))));
                QApplication::sendEvent(heavyView, &key);
                // Until the page is drawn again, not until the call returns: what the
                // reader pays is the wait for their own character to appear, and that is
                // the render behind the shared lock.
                waitFor([heavyView] { return heavyView->isRendered(0); });
                totalMilliseconds += clock.elapsed();
            }
            const qint64 perKeystroke = rounds > 0 ? totalMilliseconds / rounds : 0;
            note(QStringLiteral("fill heavy: %1 characters on a scanned page, %2 ms in all, "
                                "%3 ms per keystroke including its redraw")
                     .arg(rounds)
                     .arg(totalMilliseconds)
                     .arg(perKeystroke));
            check(perKeystroke < 750,
                  QStringLiteral("fill: a keystroke on the heaviest page this program is "
                                 "tested on stays under three quarters of a second (%1 ms)")
                      .arg(perKeystroke));
        }

        // ---- the encrypted document: measured, and then saved like any other ----
        //
        // handoff-04 G1 6 asks for a measurement before a decision: does FPDF_SaveAsCopy
        // keep the encryption and the permissions of the file it was given, or does writing
        // a filled form also take the protection off? The rows that print the answer are
        // quoted in the report, and the checks under them pin it, so that a later PDFium
        // which stopped carrying /Encrypt over reddens a line here rather than quietly
        // putting an unprotected copy of somebody's protected document on their disk.
        {
            const QString lockedPath = formDir.filePath(QStringLiteral("locked.pdf"));
            const QString lockedAgain = formDir.filePath(QStringLiteral("locked-copy.pdf"));
            {
                QPDF lock;
                lock.processFile(formPath.toUtf8().constData());
                QPDFWriter out(lock, lockedPath.toUtf8().constData());
                // A real user password, so that opening the file at all needs the key --
                // the case worth asking about. R3/RC4-128 rather than the R6 the 加密 box
                // writes, because what is asked here is what happens to /Encrypt on the way
                // through PDFium's writer, and the file has to open for the question to be
                // asked at all.
                out.setR3EncryptionParametersInsecure("let-me-in", "owner-key", true, true,
                                                     true, true, true, true, qpdf_r3p_full);
                out.write();
            }
            check(QFileInfo::exists(lockedPath), "fill: setup: an encrypted copy was written");
            const QByteArray lockedBefore = readAllBytes(lockedPath);

            // What the source's own dictionary says, to be compared with what comes back.
            QString sourcePermissions;
            {
                QPDF probe;
                probe.processFile(lockedPath.toUtf8().constData(), "let-me-in");
                int R = 0;
                int P = 0;
                probe.isEncrypted(R, P);
                sourcePermissions = QStringLiteral("R=%1 P=%2 print=%3 extract=%4 assemble=%5 "
                                                  "annotate=%6 form=%7 other=%8")
                                        .arg(R)
                                        .arg(P)
                                        .arg(probe.allowPrintHighRes() ? 1 : 0)
                                        .arg(probe.allowExtractAll() ? 1 : 0)
                                        .arg(probe.allowModifyAssembly() ? 1 : 0)
                                        .arg(probe.allowModifyAnnotation() ? 1 : 0)
                                        .arg(probe.allowModifyForm() ? 1 : 0)
                                        .arg(probe.allowModifyOther() ? 1 : 0);
            }

            DocumentTab locked;
            locked.resize(900, 700);
            locked.show();
            check(locked.open(lockedPath, QStringLiteral("let-me-in"))
                      == Document::Status::Ready,
                  QStringLiteral("fill: the encrypted form opens with its key (%1)")
                      .arg(locked.lastError()));
            PageView *lockedView = viewOf(locked);
            check(waitFor([&lockedView] { return lockedView->isRendered(0); }),
                  "fill: and its page is drawn");
            const auto lockedFieldPoint = [lockedView](const QRectF &rect) {
                const QPointF centre = FormSampleFile::userCentre(rect);
                const QRectF page = lockedView->pageRectInViewport(0);
                return QPointF(page.left() + centre.x() / 595.0 * page.width(),
                               page.top() + (842.0 - centre.y()) / 842.0 * page.height());
            };
            const QPointF lockedField = lockedFieldPoint(fields.textField);
            sendMouse(lockedView->viewport(), QEvent::MouseButtonPress, lockedField);
            sendMouse(lockedView->viewport(), QEvent::MouseButtonRelease, lockedField);
            check(waitFor([&locked] { return locked.view()->fieldHasFocus(); }),
                  "fill: a field on a protected document takes the keyboard");
            {
                QInputMethodEvent commit;
                commit.setCommitString(QStringLiteral("SECRET"));
                QApplication::sendEvent(lockedView, &commit);
            }
            check(waitFor([&locked] { return locked.hasFieldEdits(); }),
                  "fill: and the answer is held in memory, unsaved");

            // The measurement, off the backend's own first stage rather than off the tab:
            // what would the bytes look like if a protected document were saved?
            {
                QString staged;
                QString stageError;
                const bool stagedOk = locked.formSurface()->stageFieldValues(
                    formDir.path(), &staged, &stageError);
                check(stagedOk && !staged.isEmpty(),
                      QStringLiteral("fill: the backend stages its copy of a protected "
                                     "document (%1)").arg(stageError));
                if (stagedOk && !staged.isEmpty()) {
                    // Whether the file can be read with no key. The flag has to start at
                    // false and be set only by a read that worked: a file qpdf cannot open
                    // without a key is a file that still has one, and treating the throw as
                    // "no answer" printed "opens with no key=1" for a file that was
                    // encrypted the whole time.
                    bool opensWithoutKey = false;
                    bool readWithNoKey = false;
                    QString stagedPermissions;
                    try {
                        QPDF none;
                        none.processFile(staged.toUtf8().constData());
                        readWithNoKey = true;
                        opensWithoutKey = !none.isEncrypted();
                    } catch (const std::exception &e) {
                        note(QStringLiteral("fill encrypt: reading the staged file with no "
                                            "key is refused: %1")
                             .arg(QString::fromUtf8(e.what())));
                    }
                    if (!opensWithoutKey) {
                        QPDF probe;
                        probe.processFile(staged.toUtf8().constData(), "let-me-in");
                        int R = 0;
                        int P = 0;
                        probe.isEncrypted(R, P);
                        stagedPermissions =
                            QStringLiteral("R=%1 P=%2 print=%3 extract=%4 assemble=%5 "
                                           "annotate=%6 form=%7 other=%8")
                                .arg(R)
                                .arg(P)
                                .arg(probe.allowPrintHighRes() ? 1 : 0)
                                .arg(probe.allowExtractAll() ? 1 : 0)
                                .arg(probe.allowModifyAssembly() ? 1 : 0)
                                .arg(probe.allowModifyAnnotation() ? 1 : 0)
                                .arg(probe.allowModifyForm() ? 1 : 0)
                                .arg(probe.allowModifyOther() ? 1 : 0);
                    }
                    note(QStringLiteral("fill encrypt: source  %1").arg(sourcePermissions));
                    note(QStringLiteral("fill encrypt: staged   %1").arg(stagedPermissions));
                    note(QStringLiteral("fill encrypt: staged opens with no key=%1, bytes=%2")
                             .arg(readWithNoKey && opensWithoutKey ? 1 : 0)
                             .arg(QFileInfo(staged).size()));
                    QFile::remove(staged);
                    check(!readWithNoKey || !opensWithoutKey,
                          "fill: measured -- PDFium's save keeps the encryption");
                    check(stagedPermissions == sourcePermissions,
                          "fill: and it keeps the permission bits beside it");
                }
            }

            // So the save is allowed, and what it writes has to still be the protected
            // document it was -- with the reader's answer inside it.
            check(locked.saveEdits(),
                  "fill: a protected document with filled fields saves like any other");
            check(readAllBytes(lockedPath) != lockedBefore,
                  "fill: and the bytes moved");
            check(!locked.hasUnsavedEdits(), "fill: and nothing is unsaved any more");
            {
                bool stillNeedsKey = false;
                try {
                    QPDF none;
                    none.processFile(lockedPath.toUtf8().constData());
                } catch (const std::exception &e) {
                    stillNeedsKey = QString::fromUtf8(e.what()).contains(
                        QStringLiteral("password"));
                }
                check(stillNeedsKey,
                      "fill: the file that came back still asks for its key");
                QPDF withKey;
                withKey.processFile(lockedPath.toUtf8().constData(), "let-me-in");
                QString value;
                QPDFPageDocumentHelper withKeyPages(withKey);
                std::vector<QPDFPageObjectHelper> pages = withKeyPages.getAllPages();
                for (QPDFPageObjectHelper &page : pages) {
                    for (QPDFAnnotationObjectHelper &entry : page.getAnnotations()) {
                        const QPDFObjectHandle annot = entry.getObjectHandle();
                        if (annot.getKey("/T").getStringValue() == "name")
                            value = QString::fromUtf8(annot.getKey("/V").getUTF8Value().c_str());
                    }
                }
                note(QStringLiteral("fill: the saved protected file reads name=[%1]").arg(value));
                check(value.contains(QStringLiteral("SECRET")),
                      "fill: and the answer the reader typed is in it");
            }

            // 另存为 takes the same route with a new name, and the copy it writes is
            // encrypted the same way -- which is the only way "save a copy" can be offered
            // on a protected document at all.
            // A fresh answer first: with nothing unsaved 另存为 refuses, which is the same
            // rule the unprotected tab follows. And the reason is taken into a variable on
            // its own line, because check() may evaluate its second argument before its
            // first -- so an inline .arg(why) printed the sentence before the call had
            // filled it, and a real refusal looked like an empty one.
            // Click it again first: the save reloaded the document, so the field that had
            // the keyboard belonged to the pages before it.
            const QPointF refocused = lockedFieldPoint(fields.textField);
            sendMouse(lockedView->viewport(), QEvent::MouseButtonPress, refocused);
            sendMouse(lockedView->viewport(), QEvent::MouseButtonRelease, refocused);
            check(waitFor([&locked] { return locked.view()->fieldHasFocus(); }),
                  "fill: the field takes the keyboard again after the save and reload");
            {
                QInputMethodEvent more;
                more.setCommitString(QStringLiteral("!!"));
                QApplication::sendEvent(lockedView, &more);
            }
            check(waitFor([&locked] { return locked.hasFieldEdits(); }),
                  "fill: and a protected document can be edited again after a save");
            QString asWhy;
            const bool savedAs = locked.saveEditsTo(lockedAgain, &asWhy);
            note(QStringLiteral("fill: 另存为 of the protected document returned %1 (%2)")
                     .arg(savedAs ? 1 : 0)
                     .arg(asWhy));
            check(savedAs, "fill: 另存为 of a protected document works");
            {
                QPDF probe;
                probe.processFile(lockedAgain.toUtf8().constData(), "let-me-in");
                note(QStringLiteral("fill: the 另存为 copy is encrypted=%1, pages=%2")
                         .arg(probe.isEncrypted() ? 1 : 0)
                         .arg(int(QPDFPageDocumentHelper(probe).getAllPages().size())));
                check(probe.isEncrypted(), "fill: and the copy is still encrypted");
            }
        }

        // ---- the choice fields: a pick, the star, the file, and the picture ----
        //
        // H1 1/2 put the dropdown, the editable box and the multi-select list into the
        // backend, and pdfiumbackend_smoke pins each of them there. This is the seam the
        // backend cannot see: the view's press routing (a dropdown row is not on the page
        // as far as its hit test is concerned), the tab's dirty answer, the save, and a
        // second reader opening the bytes. The multi-select array is asserted here for
        // the reason the backend test says out loud: the ordinary field read answers only
        // the first of what was picked, so only qpdf can say the second one survived.
        {
            const QString choicePath = formDir.filePath(QStringLiteral("choices.pdf"));
            QString choiceWhy;
            check(FormSampleFile::write(choicePath, 0, false, &choiceWhy),
                  QStringLiteral("choice: the sample is written (%1)").arg(choiceWhy));
            const QByteArray choiceBefore = readAllBytes(choicePath);
            const QString choiceKept = formDir.filePath(QStringLiteral("choices-before.pdf"));
            check(writeAllBytes(choiceKept, choiceBefore),
                  "choice: setup: the file as it is before any pick is kept");

            const FormSampleFile::Geometry choiceFields;
            DocumentTab choiceTab;
            choiceTab.resize(900, 700);
            choiceTab.show();
            check(choiceTab.open(choicePath) == Document::Status::Ready,
                  QStringLiteral("choice: the sample opens in a tab (%1)")
                      .arg(choiceTab.lastError()));
            PageView *choiceView = viewOf(choiceTab);
            check(waitFor([choiceView] { return choiceView->isRendered(0); }),
                  "choice: and the page with the choice fields is drawn");
            const auto at = [choiceView](const QPointF &userSpace) {
                const QRectF page = choiceView->pageRectInViewport(0);
                return QPointF(page.left() + userSpace.x() / 595.0 * page.width(),
                               page.top() + (842.0 - userSpace.y()) / 842.0 * page.height());
            };
            QWidget *choiceCanvas = choiceView->viewport();

            // The dropdown button, then a row of the list the engine draws under the
            // field: neither point is one the page's hit test can name, and the backend's
            // tests are the place that measured the routing. Here the presses go through
            // the view, which is what a reader actually does.
            const QPointF button = at(FormSampleFile::comboBoxButton(choiceFields.comboBox));
            sendMouse(choiceCanvas, QEvent::MouseButtonPress, button);
            sendMouse(choiceCanvas, QEvent::MouseButtonRelease, button);
            check(waitFor([&choiceTab] { return choiceTab.view()->fieldHasFocus(); }),
                  "choice: the dropdown button gives the combo box the keyboard");
            const QPointF firstRow
                = at(QPointF(choiceFields.comboBox.center().x(),
                             FormSampleFile::popupRowY(choiceFields.comboBox, 0)));
            sendMouse(choiceCanvas, QEvent::MouseButtonPress, firstRow);
            sendMouse(choiceCanvas, QEvent::MouseButtonRelease, firstRow);
            check(waitFor([&choiceTab] { return choiceTab.hasFieldEdits(); }),
                  "choice: picking a row is an unsaved change");
            check(choiceTab.title() == QStringLiteral("choices.pdf *"),
                  QStringLiteral("choice: and the title carries the star (%1)")
                      .arg(choiceTab.title()));

            check(choiceTab.saveEdits(), "choice: 保存 writes the pick back in place");
            check(!choiceTab.hasUnsavedEdits(), "choice: after the save nothing is unsaved");
            check(choiceTab.title() == QStringLiteral("choices.pdf"),
                  "choice: and the star is off again");
            {
                QPDF read;
                read.processFile(choicePath.toUtf8().constData());
                QString value;
                std::vector<QPDFPageObjectHelper> pages =
                    QPDFPageDocumentHelper(read).getAllPages();
                for (QPDFPageObjectHelper &page : pages) {
                    for (const QPDFObjectHandle &annot : page.getAnnotations()) {
                        if (annot.getKey("/T").getStringValue() == "pick")
                            value = QString::fromUtf8(annot.getKey("/V").getUTF8Value().c_str());
                    }
                }
                note(QStringLiteral("choice: qpdf reads the combo box as [%1]").arg(value));
                check(value == QStringLiteral("a"),
                      "choice: the row the reader picked is the answer the file carries");
            }
            check(waitFor([choiceView] { return choiceView->isRendered(0); }),
                  "choice: the saved page is drawn again before the next pick");

            // The list box: a plain click on the first row, then a Ctrl+click on the row
            // the file already had -- the shape pdfiumbackend_smoke measured, where the
            // live text (the label picked last) ends up exactly equal to the file's own
            // answer while the selection is two rows. The pair's /V is an array, and the
            // ordinary reads cannot tell that anything changed; the star has to.
            const QPointF listRowFirst
                = at(QPointF(choiceFields.multiListBox.center().x(),
                             FormSampleFile::listRowY(choiceFields.multiListBox, 0)));
            const QPointF listRowSecond
                = at(QPointF(choiceFields.multiListBox.center().x(),
                             FormSampleFile::listRowY(choiceFields.multiListBox, 1)));
            sendMouse(choiceCanvas, QEvent::MouseButtonPress, listRowFirst);
            sendMouse(choiceCanvas, QEvent::MouseButtonRelease, listRowFirst);
            sendMouse(choiceCanvas, QEvent::MouseButtonPress, listRowSecond, false,
                      Qt::ControlModifier);
            sendMouse(choiceCanvas, QEvent::MouseButtonRelease, listRowSecond, false,
                      Qt::ControlModifier);
            check(waitFor([&choiceTab] { return choiceTab.hasFieldEdits(); }),
                  "choice: a click and a Ctrl+click in the list box are an unsaved change");
            check(choiceTab.title() == QStringLiteral("choices.pdf *"),
                  QStringLiteral("choice: and the star is back (%1)").arg(choiceTab.title()));
            check(choiceTab.saveEdits(), "choice: 保存 writes both selected rows");
            {
                QPDF read;
                read.processFile(choicePath.toUtf8().constData());
                QStringList answers;
                std::vector<QPDFPageObjectHelper> pages =
                    QPDFPageDocumentHelper(read).getAllPages();
                for (QPDFPageObjectHelper &page : pages) {
                    for (const QPDFObjectHandle &annot : page.getAnnotations()) {
                        if (annot.getKey("/T").getStringValue() != "many")
                            continue;
                        const QPDFObjectHandle value = annot.getKey("/V");
                        if (value.isArray()) {
                            for (int index = 0; index < value.getArrayNItems(); ++index)
                                answers.append(QString::fromUtf8(
                                    value.getArrayItem(index).getUTF8Value().c_str()));
                        } else {
                            answers.append(
                                QString::fromUtf8(value.getUTF8Value().c_str()));
                        }
                    }
                }
                answers.sort();
                note(QStringLiteral("choice: qpdf reads the multi-select /V as [%1]")
                         .arg(answers.join(QStringLiteral(", "))));
                check(answers == QStringList{QStringLiteral("one"), QStringLiteral("two")},
                      "choice: /V is an array holding both selected rows and nothing else");
            }

            // The picture, read the way sec.89 reads one: two renders of one rectangle,
            // the file before the picks and the file after them. "There are pixels in the
            // box" would still be true with the old answer painted in it.
            const auto signature = [](const QString &path, const QRectF &box) {
                PdfiumBackend reader;
                if (reader.load(path, QString()) != Document::Status::Ready)
                    return QStringLiteral("(no picture)");
                RenderOptions options;
                const QImage image = reader.render(0, QSize(595, 842), options);
                if (image.isNull())
                    return QStringLiteral("(no picture)");
                const int left = int(box.left());
                const int right = int(box.right());
                const int top = int(842.0 - box.bottom());
                const int bottom = int(842.0 - box.top());
                quint64 hash = 1469598103934665603ULL;
                int ink = 0;
                for (int y = top; y < bottom; ++y) {
                    for (int x = left; x < right; ++x) {
                        const QRgb pixel = image.pixel(x, y);
                        if (qGray(pixel) < 250)
                            ++ink;
                        hash = (hash ^ quint64(pixel)) * 1099511628211ULL;
                    }
                }
                return QStringLiteral("ink=%1 hash=%2").arg(ink).arg(hash, 0, 16);
            };
            const QString comboBefore = signature(choiceKept, choiceFields.comboBox);
            const QString comboAfter = signature(choicePath, choiceFields.comboBox);
            const QString listBefore = signature(choiceKept, choiceFields.multiListBox);
            const QString listAfter = signature(choicePath, choiceFields.multiListBox);
            note(QStringLiteral("choice render: combo before [%1] after [%2]; "
                                "list before [%3] after [%4]")
                     .arg(comboBefore, comboAfter, listBefore, listAfter));
            check(comboBefore != comboAfter,
                  "choice: a reopened page draws the answer the reader picked");
            check(listBefore != listAfter,
                  "choice: and the list box draws its two selected rows");
        }
    }

    // ---- Tab walks the form, through the widget ----
    //
    // Found on screen by handoff-05 H2: with a field holding the keyboard, Tab left the
    // page for the document tab strip. QWidget::event() offers Tab to focusNextPrevChild()
    // before keyPressEvent(), so a key the view meant for the field was spent as a focus
    // move. G1's Tab test asked the backend directly and could not see it; this one sends
    // the key to the widget, which is the door a real Tab comes through. Shift+Tab arrives
    // as Qt::Key_Backtab, a name the key table did not have.
    {
        QTemporaryDir tabDir;
        check(tabDir.isValid(), "setup: a scratch folder for the Tab walk");
        const QString tabPath = tabDir.filePath(QStringLiteral("tab-walk.pdf"));
        QString why;
        check(FormSampleFile::write(tabPath, 0, false, &why),
              QStringLiteral("setup: the fillable sample is written: %1").arg(why));
        const FormSampleFile::Geometry fields;

        // Beside a second widget that takes focus, the way the real window has its tab
        // strip: a lone DocumentTab has nothing for focusNextPrevChild() to move to, so
        // the base answer is already "not a focus move" and the block passed with the fix
        // taken out (measured; the window's tab strip is where H2 saw the keyboard land).
        QWidget host;
        auto *hostLayout = new QVBoxLayout(&host);
        auto *elsewhere = new QLineEdit(&host);
        elsewhere->setObjectName(QStringLiteral("somewhereElseToTabTo"));
        hostLayout->addWidget(elsewhere);
        auto *walkPointer = new DocumentTab(&host);
        hostLayout->addWidget(walkPointer, 1);
        DocumentTab &walk = *walkPointer;
        host.resize(900, 760);
        host.show();
        check(walk.open(tabPath) == Document::Status::Ready,
              QStringLiteral("setup: the sample opens (%1)").arg(walk.lastError()));
        PageView *view = viewOf(walk);
        check(waitFor([view] { return view->isRendered(0); }), "setup: and is drawn");
        const QPointF centre = FormSampleFile::userCentre(fields.textField);
        const QRectF page = view->pageRectInViewport(0);
        const QPointF onScreen(page.left() + centre.x() / 595.0 * page.width(),
                               page.top() + (842.0 - centre.y()) / 842.0 * page.height());
        sendMouse(view->viewport(), QEvent::MouseButtonPress, onScreen);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, onScreen);
        check(waitFor([view] { return view->fieldHasFocus(); }),
              "setup: a click gives the text field the keyboard");
        // The window has to be the active one with the view as Qt's focus widget: with no
        // focus widget, QWidget::focusNextPrevChild() finds nothing to move to and answers
        // false by itself, and the Tab is delivered as a key whether or not the view
        // declines -- the first version of this block passed with the fix taken out.
        host.activateWindow();
        view->setFocus(Qt::TabFocusReason);
        check(waitFor([view] { return QApplication::focusWidget() == view; }),
              QStringLiteral("setup: the page view is the window's focus widget (it is %1)")
                  .arg(QApplication::focusWidget()
                           ? QString::fromLatin1(QApplication::focusWidget()->metaObject()->className())
                           : QStringLiteral("nothing")));

        FormFill *surface = view->formSurface();
        int firstPage = -1;
        QRectF firstRect;
        check(surface && surface->focusedFieldRect(&firstPage, &firstRect),
              "setup: and the form can say which field that is");

        QKeyEvent tabKey(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(view, &tabKey);
        int nextPage = -1;
        QRectF nextRect;
        const bool stillInForm = surface && surface->focusedFieldRect(&nextPage, &nextRect);
        check(stillInForm && view->fieldHasFocus(),
              "Tab keeps the keyboard in the form rather than leaving the page");
        check(stillInForm && nextRect != firstRect,
              QStringLiteral("and moves it to another field (before %1,%2 %3x%4, after %5,%6 %7x%8)")
                  .arg(firstRect.x()).arg(firstRect.y()).arg(firstRect.width()).arg(firstRect.height())
                  .arg(nextRect.x()).arg(nextRect.y()).arg(nextRect.width()).arg(nextRect.height()));

        QKeyEvent backKey(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(view, &backKey);
        int backPage = -1;
        QRectF backRect;
        check(surface && surface->focusedFieldRect(&backPage, &backRect) && backRect == firstRect,
              QStringLiteral("and Shift+Tab (Qt::Key_Backtab) brings it back to the first one "
                             "(%1,%2 %3x%4)")
                  .arg(backRect.x()).arg(backRect.y()).arg(backRect.width()).arg(backRect.height()));
    }

    // ---- night reading ----
    //
    // Asserted off the pixels because the pixels are the feature. The setting
    // existed for milestones with nothing reading it -- a stored preference that
    // changed nothing on screen -- and that is precisely the defect that reading
    // the code cannot catch and sampling the widget can.
    {
        tab.setNightInverted(false);
        view->goToPage(0);
        check(waitFor([view] { return view->isRendered(0); }),
              "the first page is drawn for the day");

        const QRectF page = view->pageRectInViewport(0);
        check(!page.isNull(), "and it is somewhere that can be sampled");

        // The viewport's own centre, not a corner of the page: this fixture is
        // laid out far larger than the test viewport, so the page's top-left
        // corner sits at a negative x and a point taken from it falls outside the
        // grabbed image -- where pixelColor answers with an invalid colour whose
        // channels all read 0, which looks exactly like a black page.
        const QPoint paper = view->viewport()->rect().center();
        check(page.contains(paper),
              "the point sampled is on the page, not the surround");
        const QImage dayShot = view->grab().toImage();
        const QColor day = dayShot.pixelColor(paper);
        check(day.isValid(), QStringLiteral("and the sample is a real pixel (%1)")
                                 .arg(day.isValid() ? day.name() : QStringLiteral("invalid")));

        tab.setNightInverted(true);
        check(view->nightInverted(), "the view reports itself inverted");
        check(waitFor([view] { return view->isRendered(0); }),
              "the page is re-rendered rather than left as a placeholder");
        const QColor nightColour = view->grab().toImage().pixelColor(paper);

        // Asserted as a relationship between the two samples rather than against
        // a colour, because by this point in the file the tab holds whichever
        // fixture the last block opened, and what that pixel is -- paper, ink, or
        // a black bar in a graphic -- is that fixture's business. What must hold
        // for any pixel is that inversion takes each channel to its complement.
        check(day != nightColour,
              QStringLiteral("the toggle changed what is on screen (%1,%2,%3 became %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));
        check(day.red() + nightColour.red() == 255
                  && day.green() + nightColour.green() == 255
                  && day.blue() + nightColour.blue() == 255,
              QStringLiteral("and every channel went to its complement (%1,%2,%3 / %4,%5,%6)")
                  .arg(day.red()).arg(day.green()).arg(day.blue())
                  .arg(nightColour.red()).arg(nightColour.green()).arg(nightColour.blue()));

        // Back the way it was, so that anything added after this sees a day page.
        tab.setNightInverted(false);
        check(waitFor([view] { return view->isRendered(0); }), "and it comes back");
    }

    // ---- the region snapshot ----
    //
    // The first time this suite touches the clipboard at all, so it is worth
    // stating plainly: if the offscreen platform has no clipboard, the readback
    // below comes back empty and this fails for a reason that is not the
    // feature's fault. It stays because "a button that silently copies nothing"
    // is exactly what the row exists to catch, and the only way to know which of
    // the two happened is to have looked.
    {
        OcrDialog dialog;
        auto *copyImage = dialog.findChild<QPushButton *>(QStringLiteral("copyImageButton"));
        check(copyImage != nullptr, "the recognition window offers a picture to copy");
        if (copyImage) {
            check(!copyImage->isEnabled(),
                  "and it is off before there are any pixels to copy");

            // A crop as the renderer actually returns it: ink over transparency,
            // because the white of a page is put down by whoever shows the image.
            QImage crop(120, 60, QImage::Format_ARGB32_Premultiplied);
            crop.fill(Qt::transparent);
            {
                QPainter ink(&crop);
                ink.fillRect(10, 10, 20, 20, Qt::black);
            }

            // By day: the paper has to be in the picture, or pasting gives a
            // cut-out that borrows whatever background it lands on.
            dialog.setCropImage(crop, false);
            check(copyImage->isEnabled(), "handing it the crop enables it");
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage pasted = QGuiApplication::clipboard()->image();
            check(!pasted.isNull(), "clicking it puts an image on the clipboard");
            check(pasted.size() == crop.size(),
                  QStringLiteral("at the size that was cropped (%1x%2 against %3x%4)")
                      .arg(pasted.size().width())
                      .arg(pasted.size().height())
                      .arg(crop.size().width())
                      .arg(crop.size().height()));
            check(pasted.pixelColor(0, 0) == QColor(Qt::white),
                  QStringLiteral("with paper under the ink, not transparency (%1)")
                      .arg(pasted.pixelColor(0, 0).name()));
            check(pasted.pixelColor(20, 20) == QColor(Qt::black),
                  QStringLiteral("and the ink still where it was (%1)")
                      .arg(pasted.pixelColor(20, 20).name()));

            // At night: what the reader is looking at is the inverted page, and a
            // snapshot that comes back white-on-black is the complement of the
            // thing they chose to copy.
            dialog.setCropImage(crop, true);
            copyImage->click();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const QImage nightPasted = QGuiApplication::clipboard()->image();
            check(nightPasted.pixelColor(0, 0) == QColor(Qt::black),
                  QStringLiteral("at night the paper pastes black (%1)")
                      .arg(nightPasted.pixelColor(0, 0).name()));
            check(nightPasted.pixelColor(20, 20) == QColor(Qt::white),
                  QStringLiteral("and the ink white (%1)")
                      .arg(nightPasted.pixelColor(20, 20).name()));
        }
    }

    // ---- the merge dialog's order promise ----
    //
    // 合并文件进来 is the first dialog here whose answer is a sequence rather than a
    // number, and the sequence is worked out inside the dialog: a merge lands beside
    // the target the reader named, not beside the block the row before it added, so
    // for 最前面 and 指定页之后 the rows have to be recorded in reverse for the list to
    // be the order the file gets (MergePagesDialog::mergesToRecord). The editor cannot
    // catch that from the other side -- it is handed one file at a time -- so it is
    // checked here, with the file picker replaced by the half of 添加文件… that is not
    // a window.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: the editor opened %1: %2").arg(path, error));
        const int onScreen = editor.pageCount();

        MergePagesDialog dialog(&tab, &editor);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QPushButton *okay = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        auto *table = dialog.findChild<QTableWidget *>();
        check(okay && table, "the merge window has its list and its button");

        auto pickTarget = [&dialog](const QString &words) {
            const auto radios = dialog.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        auto summary = [&dialog] {
            const auto labels = dialog.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };

        check(okay && !okay->isEnabled(),
              "with an empty list the button is off, and says why");
        check(summary().contains(QStringLiteral("还没有加上要合并进来的文件")),
              QStringLiteral("the empty list is described, not just greyed: %1").arg(summary()));

        const QStringList donors = {directory + QStringLiteral("/many-pages.pdf"),
                                    directory + QStringLiteral("/dense.pdf"),
                                    directory + QStringLiteral("/mixed.pdf")};
        const QStringList refused = dialog.addPaths(donors);
        check(refused.isEmpty(),
              QStringLiteral("three ordinary files are taken: %1").arg(refused.join(QStringLiteral("; "))));
        const QVector<MergePagesDialog::Entry> listed = dialog.mergesToRecord();
        check(listed.size() == donors.size(), "and one row per file");
        for (int row = 0; row < listed.size(); ++row) {
            check(baseName(listed.at(row).path) == baseName(donors.at(row)),
                  QStringLiteral("row %1 is the file picked at that place: got %2")
                      .arg(row + 1)
                      .arg(baseName(listed.at(row).path)));
            check(listed.at(row).pageCount > 0
                      && listed.at(row).pages.size() == listed.at(row).pageCount,
                  QStringLiteral("and a new row says the whole of its own file (%1 of %2)")
                      .arg(listed.at(row).pages.size())
                      .arg(listed.at(row).pageCount));
        }

        check(pickTarget(QStringLiteral("最前面")) && pickTarget(QStringLiteral("指定页之后"))
                  && pickTarget(QStringLiteral("最后面")),
              "the three places the reader can aim at are on the dialog");

        // The reversal itself, and what it is for: recorded in list order at 最前面 the
        // three land 3,2,1, because each one goes in front of the two already there.
        pickTarget(QStringLiteral("最前面"));
        QStringList recordOrder;
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord())
            recordOrder << baseName(entry.path);
        check(recordOrder == QStringList({baseName(donors.at(2)), baseName(donors.at(1)),
                                          baseName(donors.at(0))}),
              QStringLiteral("pointed at 最前面 the rows are recorded so the file gets them "
                             "in list order: got %1")
                  .arg(recordOrder.join(QStringLiteral(","))));

        DocumentEditor landing;
        check(landing.open(path, QString(), &error), "setup: a second editor on the same file");
        for (const MergePagesDialog::Entry &entry : dialog.mergesToRecord()) {
            check(landing.mergeFile(entry.path, entry.pages, DocumentEditor::MoveTarget::Front,
                                    -1, &error),
                  QStringLiteral("%1 comes in at the front: %2").arg(baseName(entry.path), error));
        }
        const QVector<DocumentEditor::PageRef> front = landing.pageOrder();
        check(front.size() == onScreen + listed.at(0).pages.size() + listed.at(1).pages.size()
                    + listed.at(2).pages.size(),
              "and the document is every one of those pages longer");
        // Each merge carries its own identity, and the last one recorded is the first
        // of them -- so the front of the file is row 1, then row 2, then row 3.
        int walking = 0;
        for (int row = 0; row < listed.size(); ++row) {
            const int identity = listed.size() - row;
            for (int page = 0; page < listed.at(row).pages.size(); ++page, ++walking) {
                check(front.at(walking).source == identity
                          && front.at(walking).page == page,
                      QStringLiteral("page %1 of the file is merge %2's page %3, which is row %4 "
                                     "of the list")
                          .arg(walking + 1)
                          .arg(identity)
                          .arg(page + 1)
                          .arg(row + 1));
            }
        }

        pickTarget(QStringLiteral("最后面"));
        table->item(0, 2)->setText(QStringLiteral("2"));
        check(dialog.mergesToRecord().at(0).pages == QVector<int>({1}),
              "a row's range cell is read as that file's own page numbers");
        table->item(1, 2)->setText(QStringLiteral(" "));
        check(okay && !okay->isEnabled(),
              "and a row left blank stops the button -- blank is nothing said, not everything");
        check(summary().contains(baseName(donors.at(1))),
              QStringLiteral("naming which row: %1").arg(summary()));
        table->item(1, 2)->setText(QStringLiteral("1"));

        pickTarget(QStringLiteral("指定页之后"));
        check(okay && !okay->isEnabled(),
              "an 指定页之后 with no number in the box is not an answer yet");
        check(summary().contains(QStringLiteral("要说合并到第几页之后")),
              QStringLiteral("and it says that: %1").arg(summary()));
        auto *anchorBox = dialog.findChild<QLineEdit *>();
        check(anchorBox != nullptr, "the box beside 指定页之后");
        if (anchorBox) {
            anchorBox->setText(QString::number(onScreen + 5));
            check(okay && !okay->isEnabled(), "a number past the last page of the screen is refused");
            check(summary().contains(QStringLiteral("没有第 %1 页").arg(onScreen + 5)),
                  QStringLiteral("and named as the number that was typed: %1").arg(summary()));
            anchorBox->setText(QStringLiteral("2"));
            check(okay && okay->isEnabled(), "a page that is there lets the button go back");
        }

        // The sentence 删除页面 needs too, said where the pages come from: an imported
        // page has no number on the screen, so a reader has to be told before merging.
        check(editor.mergeFile(donors.at(0), {0}, DocumentEditor::MoveTarget::Back, -1, &error),
              QStringLiteral("setup: one page recorded into the editor the dialog reads: %1")
                  .arg(error));
        pickTarget(QStringLiteral("最后面"));
        check(summary().contains(QStringLiteral("已经有 %1 页是合并进来的").arg(1)),
              QStringLiteral("the summary counts what the screen never showed: %1").arg(summary()));
        check(summary().contains(QStringLiteral("只能撤销那次合并")),
              QStringLiteral("and says the only way back out: %1").arg(summary()));
    }

    // ---- the merge box refuses a pending split before the click ----
    //
    // §20 caught this dialog lighting 合并这些文件 and printing an arithmetic line while
    // DocumentEditor::mergeFile() was quietly going to refuse whatever it promised, with
    // 拆分文件 having done it the other way round (greyed button, reason in the summary)
    // since §16. So both ends of that mutual exclusion now answer the same two questions
    // of the box itself: does the button stay off, and is the reason said in the model's
    // own words rather than a paraphrase that can drift. A fresh box per click, the way
    // the menu builds one, so what it says is read off the list as it stands now -- which
    // is also what makes the request pass again once the split has been undone.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto saidByMergeBox = [](MergePagesDialog &box, const QString &donor) {
            check(box.addPaths({donor}).isEmpty(), "setup: the donor file is taken");
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOfMergeBox = [](MergePagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const QString donor = directory + QStringLiteral("/dense.pdf");

        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("setup: with no split pending, this same request is one the "
                                 "button lights for: %1")
                      .arg(said));
        }

        check(editor.splitInto(15, &error),
              QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && !okay->isEnabled(),
                  "with a split pending the merge box's own button is off before the click");
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("and the summary names the pending split, in the model's "
                                 "sentence: %1")
                      .arg(said));
            // The number the refusal replaces: where a batch would land is an answer
            // about a file that will not be written, which is the same kind of blank as
            // §20's "一个没有指称的数".
            check(!said.contains(QStringLiteral("连着排在第")),
                  QStringLiteral("and no page numbers are promised for a merge that cannot "
                                 "be recorded: %1")
                      .arg(said));
            // The box and the editor refuse the same request -- the rule §21 states,
            // asked from this end.
            check(!editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error)
                      && error.contains(QStringLiteral("先撤销那一步")),
                  QStringLiteral("and the editor refuses it too: %1").arg(error));
        }

        check(editor.undo(), "setup: the split is taken back out of the list");
        {
            MergePagesDialog box(&tab, &editor);
            const QString said = saidByMergeBox(box, donor);
            QPushButton *okay = okayOfMergeBox(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("undo that split and the very same request is lit again: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成")),
                  QStringLiteral("and the refusal is gone with it: %1").arg(said));
        }
    }

    // ---- one page is not a range: the two landing sentences ----
    //
    // §20 fixed this for 拆分's file names and pinned it there ("a one-page piece says 21,
    // not 21-21"), while 移动页面 and 合并文件进来 kept printing 5-5 and 40-40 -- and kept
    // saying 它们连着 / "they end up together" about a single page, which is the same thing:
    // a number deciding the words around it. Both dialogs now read the number from
    // DocumentEditor::pageSpan() and pick the wording by the count, so this asks each box the
    // one-page question and the several-page question, and looks for the dash on the right
    // side of that line only.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document to move pages around in: %1").arg(error));
        const int onScreen = editor.pageCount();

        const auto saidBy = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto pickTarget = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并文件进来 first: a one-page donor needs no range typed, because a fresh row takes
        // the whole of its own file, and rotated.pdf is exactly one page.
        const auto rangeCell = [](QDialog &box) {
            auto *cells = box.findChild<QTableWidget *>();
            return cells && cells->item(0, 2) ? cells->item(0, 2)->text() : QString();
        };
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/rotated.pdf")}).isEmpty(),
                  "setup: a one-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("一共取 1 页")),
                  QStringLiteral("the count says one page: %1").arg(said));
            check(said.contains(QStringLiteral("排在第 %1 页").arg(onScreen + 1)),
                  QStringLiteral("and it lands on one number, not a range: %1").arg(said));
            check(!said.contains(QStringLiteral("连着")),
                  QStringLiteral("one page is not 连着 either, which is the half the English "
                                 "reader sees as \"they end up together\": %1").arg(said));
            // The fifth exit, and the only one that was an *input*: the cell a fresh row seeds
            // with its own file's range held "1-1" for a one-page file. It parses the same, so
            // nothing downstream noticed -- it is only ever a reader reading the number back.
            check(rangeCell(box) == QStringLiteral("1"),
                  QStringLiteral("a one-page donor seeds the range cell with 1, not 1-1: [%1]")
                      .arg(rangeCell(box)));
        }
        {
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({directory + QStringLiteral("/dense.pdf")}).isEmpty(),
                  "setup: a two-page donor is taken");
            check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("连着排在第 %1-%2 页")
                                     .arg(onScreen + 1)
                                     .arg(onScreen + 2)),
                  QStringLiteral("two pages keep the run and the wording that goes with it: %1")
                      .arg(said));
            check(rangeCell(box) == QStringLiteral("1-2"),
                  QStringLiteral("and a two-page donor does seed a run, so the cell is not just "
                                 "always the first number: [%1]").arg(rangeCell(box)));
        }

        // 移动页面: the same two questions, typed into its own range box -- picked by the
        // placeholder, because this dialog has a second line edit beside it.
        {
            MovePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = nullptr;
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    range = edit;
            }
            check(range != nullptr, "setup: the box that names which pages move");
            if (range) {
                check(pickTarget(box, QStringLiteral("最后面")), "setup: aimed at the end");
                range->setText(QStringLiteral("7"));
                const QString one = saidBy(box);
                check(one.contains(QStringLiteral("把 1 页移到")),
                      QStringLiteral("one page moved is not a block of them: %1").arg(one));
                check(one.contains(QStringLiteral("移完它就排在第 40 页")),
                      QStringLiteral("page 7 alone takes the last place, which is 40: %1")
                          .arg(one));
                check(!one.contains(QStringLiteral("它们")),
                      QStringLiteral("and the plural pronoun went with the dash: %1").arg(one));

                range->setText(QStringLiteral("7,9"));
                const QString two = saidBy(box);
                check(two.contains(QStringLiteral("连着排在第 39-40 页")),
                      QStringLiteral("two pages come over together, at 39 and 40: %1").arg(two));
            }
        }

        // 拆分文件: the fourth exit, and the one whose rule had a private copy of its own
        // until this slice. Its list is cut short after six pieces, so 每 1 页一份 of this
        // 40-page document is the shape where every number in it stands alone -- a dash
        // anywhere in that list would be the 40-40 bug wearing a different hat. Nothing in
        // any test read this box's summary before now, which is how two correct private
        // copies of one rule stayed unasked while two missing ones reached the screen.
        {
            SplitPagesDialog box(&tab, &editor);
            auto *size = box.findChild<QSpinBox *>();
            check(size != nullptr, "setup: the box that says how many pages go in a piece");
            if (size) {
                size->setValue(1);
                const QString said = saidBy(box);
                check(said.contains(QStringLiteral("会写成 40 份，每份最多 1 页")),
                      QStringLiteral("forty pieces of one page: %1").arg(said));
                const QString list
                    = said.section(QStringLiteral("页："), 1).section(QStringLiteral("。"), 0, 0);
                // Six pieces are listed, then the cut, then the last one -- and the cut is
                // joined to the numbers with `、` too, so the list splits into nine.
                // Split on U+3001 by its code point: QLatin1Char cannot hold it (the source
                // is UTF-8, so it would silently take one byte of a three-byte character).
                check(list.split(QChar(0x3001)).size() == 8,
                      QStringLiteral("six of them listed, then the cut, then the last one: [%1]")
                          .arg(list));
                check(list.startsWith(QStringLiteral("1、2")) && list.endsWith(QStringLiteral("40")),
                      QStringLiteral("bare numbers at both ends of the list: [%1]").arg(list));
                check(!list.contains(QLatin1Char('-')),
                      QStringLiteral("and no piece of it is a range, because none of them holds "
                                     "more than one page: [%1]").arg(list));
            }
        }
    }

    // ---- XFA and the threads between annotations: said before the press ----
    //
    // Two features of the same shape, so they are tested in one place. Both are a sentence a
    // reader has to be given *while the boxes are still editable* -- F3's rule, restated from §77
    // and §80 -- and both count off the request in front of the reader rather than off the last
    // thing the pending list accepted. The three controls of this block are therefore all of the
    // same kind: change the request, and the sentence has to change with it, because a line that
    // is simply "this document has X" would pass every assertion that only checks the words.
    //
    // The samples are written here rather than carried in tests/fixtures: §17's rule (the fixture
    // folder is globbed by other tests), and none of the real files has an XFA packet or a reply
    // annotation in it.
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the XFA and threaded copies");
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        QString error;

        // A book whose form is said twice: an AcroForm field on page 1 and a packet describing it.
        const QString xfaBook = scratch.filePath(QStringLiteral("with-xfa.pdf"));
        // The same, with /NeedsRendering set and no /Fields registered: the dynamic case.
        const QString dynamicBook = scratch.filePath(QStringLiteral("dynamic-xfa.pdf"));
        const auto buildPacket = [](const QString &source, const QString &file, bool withFields,
                                   bool needsRendering, QString *why) -> bool {
            try {
                QPDF qpdf;
                qpdf.processFile(source.toUtf8().constData());
                QPDFObjectHandle page =
                    QPDFPageDocumentHelper(qpdf).getAllPages().front().getObjectHandle();
                QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
                field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
                field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
                field.replaceKey("/T", QPDFObjectHandle::newString("onOne"));
                field.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
                QPDFObjectHandle rect = QPDFObjectHandle::newArray();
                rect.appendItem(QPDFObjectHandle::newReal(60));
                rect.appendItem(QPDFObjectHandle::newReal(600));
                rect.appendItem(QPDFObjectHandle::newReal(260));
                rect.appendItem(QPDFObjectHandle::newReal(630));
                field.replaceKey("/Rect", rect);
                field = qpdf.makeIndirectObject(field);
                field.replaceKey("/P", page);
                QPDFObjectHandle annots = QPDFObjectHandle::newArray();
                annots.appendItem(field);
                page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));

                QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
                if (withFields) {
                    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
                    fields.appendItem(field);
                    form.replaceKey("/Fields", fields);
                }
                if (needsRendering) // the catalog's key, not the form's (PDF 32000-1, table 28)
                    qpdf.getRoot().replaceKey("/NeedsRendering", QPDFObjectHandle::newBool(true));

                const QString templateXml = QStringLiteral(
                    "<?xml version=\"1.0\"?><xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xfdf/\">"
                    "<template><pageSet><page name=\"P1\"/></pageSet></template></xdp:xdp>");
                QPDFObjectHandle packet =
                    QPDFObjectHandle::newStream(&qpdf, templateXml.toUtf8().constData());
                packet.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
                QPDFObjectHandle xfa = QPDFObjectHandle::newArray();
                xfa.appendItem(QPDFObjectHandle::newString(""));
                xfa.appendItem(packet);
                form.replaceKey("/XFA", xfa);
                qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
                QPDFWriter writer(qpdf, file.toUtf8().constData());
                writer.write();
            } catch (const std::exception &e) {
                *why = QString::fromUtf8(e.what());
                return false;
            }
            return true;
        };
        // A thread across two pages of one document: the note on page 1, the reply that answers it
        // on page 2. Legal where it is, and the shape 删除页面 and 拆分文件 can break.
        const auto buildThread = [](const QString &source, const QString &file,
                                   QString *why) -> bool {
            const auto rect = [](QPDF &qpdf, double left, double top, double right,
                                 double bottom) {
                QPDFObjectHandle value = QPDFObjectHandle::newArray();
                value.appendItem(QPDFObjectHandle::newReal(left));
                value.appendItem(QPDFObjectHandle::newReal(top));
                value.appendItem(QPDFObjectHandle::newReal(right));
                value.appendItem(QPDFObjectHandle::newReal(bottom));
                return value;
            };
            try {
                QPDF qpdf;
                qpdf.processFile(source.toUtf8().constData());
                const std::vector<QPDFPageObjectHelper> pages
                    = QPDFPageDocumentHelper(qpdf).getAllPages();
                QPDFObjectHandle first = pages.at(0).getObjectHandle();
                QPDFObjectHandle second = pages.at(1).getObjectHandle();

                QPDFObjectHandle note = QPDFObjectHandle::newDictionary();
                note.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                note.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
                note.replaceKey("/Rect", rect(qpdf, 60, 700, 90, 730));
                note.replaceKey("/Contents", QPDFObjectHandle::newString("the note on page 1"));
                note = qpdf.makeIndirectObject(note);
                note.replaceKey("/P", first);
                QPDFObjectHandle firstAnnots = QPDFObjectHandle::newArray();
                firstAnnots.appendItem(note);
                first.replaceKey("/Annots", qpdf.makeIndirectObject(firstAnnots));

                QPDFObjectHandle reply = QPDFObjectHandle::newDictionary();
                reply.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                reply.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
                reply.replaceKey("/Rect", rect(qpdf, 60, 600, 90, 630));
                reply.replaceKey("/Contents", QPDFObjectHandle::newString("a reply on page 2"));
                reply.replaceKey("/IRT", note);
                reply.replaceKey("/RT", QPDFObjectHandle::newName("/R"));
                reply = qpdf.makeIndirectObject(reply);
                reply.replaceKey("/P", second);
                QPDFObjectHandle secondAnnots = QPDFObjectHandle::newArray();
                secondAnnots.appendItem(reply);
                second.replaceKey("/Annots", qpdf.makeIndirectObject(secondAnnots));

                QPDFWriter writer(qpdf, file.toUtf8().constData());
                writer.write();
            } catch (const std::exception &e) {
                *why = QString::fromUtf8(e.what());
                return false;
            }
            return true;
        };

        QString made;
        check(buildPacket(book, xfaBook, true, false, &made),
              QStringLiteral("setup: a copy of the book carrying an XFA packet: %1").arg(made));
        check(buildPacket(book, dynamicBook, false, true, &made),
              QStringLiteral("setup: and a dynamic one: %1").arg(made));
        const QString threaded = scratch.filePath(QStringLiteral("threaded.pdf"));
        check(buildThread(book, threaded, &made),
              QStringLiteral("setup: and a copy with a reply across two pages: %1").arg(made));

        // ---- what the tab knows on the way in ----
        //
        // The status line is MainWindow's, but the answers it says are read here, off the one
        // qpdf probe open() already makes -- and that probe is the thing a regression could quietly
        // stop filling in.
        {
            check(tab.open(xfaBook) == Document::Status::Ready,
                  "the packet copy opens in the tab");
            check(tab.hasXfa(), "and the tab knows there is an XFA packet in it");
            check(!tab.hasDynamicXfa(),
                  "but does not call a packet that has fields under it a dynamic one");

            check(tab.open(dynamicBook) == Document::Status::Ready,
                  "the dynamic copy opens");
            check(tab.hasXfa() && tab.hasDynamicXfa(),
                  "and both answers come back yes: no /Fields registered under the packet");

            check(tab.open(book) == Document::Status::Ready, "the plain book opens again");
            check(!tab.hasXfa() && !tab.hasDynamicXfa(),
                  "and neither answer is claimed for a document with no packet at all");
            check(tab.open(threaded) == Document::Status::Ready, "the threaded copy opens");
            check(!tab.hasXfa(),
                  "while a document full of annotations is not mistaken for one full of XFA");

            // The cost of what these three answers add, measured rather than asserted -- open() is
            // on the path a reader feels, and both readings ride the qpdf pass the tab was already
            // making (§77 measured that pass at 22 ms for a 600-page file). A 0 here means the two
            // added lookups are free next to it; a number is printed whatever it is, so the next
            // person to add a third reading has a before-picture.
            {
                const QString big = directory + QStringLiteral("/huge.pdf");
                QElapsedTimer clock;
                DocumentEditor small;
                clock.start();
                const bool openedSmall = small.open(threaded, QString(), &error);
                const qint64 smallMs = clock.elapsed();
                DocumentEditor large;
                clock.restart();
                const bool openedLarge = large.open(big, QString(), &error);
                const qint64 largeMs = clock.elapsed();
                std::cout << "MEASURE open cost: 40-page threaded=" << smallMs
                          << "ms pages=" << (openedSmall ? small.pageCount() : -1) << " | 600-page="
                          << largeMs << "ms pages="
                          << (openedLarge ? large.pageCount() : 0) << '\n';
                check(openedSmall && openedLarge,
                      "setup: both files opened for the cost reading");
                check(smallMs < 100 && largeMs < 400,
                      QStringLiteral("and neither reading is slow enough to be a sentence worth "
                                     "writing about on its own: %1 ms for 40 pages, %2 ms for the "
                                     "big one")
                          .arg(smallMs)
                          .arg(largeMs));
            }
        }

        const auto saidOf = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto rangeOf = [](QDialog &box) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };

        // ---- the XFA sentence, in the four boxes whose operations drop the packet ----
        {
            DocumentEditor packeted;
            check(packeted.open(xfaBook, QString(), &error),
                  QStringLiteral("setup: the editor opens the packet copy: %1").arg(error));
            DocumentEditor plainBook;
            check(plainBook.open(book, QString(), &error),
                  QStringLiteral("setup: and the plain one: %1").arg(error));

            DeletePagesDialog del(&tab, 0, &packeted);
            check(saidOf(del).contains(QStringLiteral("XFA 表单")),
                  QStringLiteral("删除页面 says what happens to the packet: %1").arg(saidOf(del)));
            DeletePagesDialog delPlain(&tab, 0, &plainBook);
            check(!saidOf(delPlain).contains(QStringLiteral("XFA")),
                  QStringLiteral("and stays out of a box that has no packet to lose: %1")
                      .arg(saidOf(delPlain)));

            MovePagesDialog mov(&tab, 0, &packeted);
            check(saidOf(mov).contains(QStringLiteral("XFA 表单")),
                  QStringLiteral("移动页面 says it too, because the packet names pages by number: %1")
                      .arg(saidOf(mov)));
            MergePagesDialog mer(&tab, &packeted);
            // A row first, because this box answers an empty list with "还没有加上要合并进来的
            // 文件" in place of any arithmetic -- and the packet sentence belongs to a request, so
            // it belongs in that branch. §47's count for the merge is taken with a row below.
            check(mer.addPaths({book}).isEmpty(), "setup: one donor row is on the list");
            check(saidOf(mer).contains(QStringLiteral("XFA 表单")),
                  QStringLiteral("合并文件进来 as well: %1").arg(saidOf(mer)));
            SplitPagesDialog spl(&tab, &packeted);
            check(saidOf(spl).contains(QStringLiteral("XFA 表单")),
                  QStringLiteral("and 拆分文件, where every piece would have carried the whole form: %1")
                      .arg(saidOf(spl)));

            // The two boxes whose operations keep the packet keep their silence about it -- the
            // sentence is about a cost, not about a property of the file.
            RotatePagesDialog rot(&tab, 0, &packeted);
            check(!saidOf(rot).contains(QStringLiteral("XFA")),
                  QStringLiteral("旋转页面 has nothing to say, because it changes no page: %1")
                      .arg(saidOf(rot)));
            ProtectPagesDialog pro(&tab, &packeted);
            check(!saidOf(pro).contains(QStringLiteral("XFA")),
                  QStringLiteral("and neither has 加密 / 去掉口令: %1").arg(saidOf(pro)));
        }

        // ---- §47: the reply count moves with the request, before any press ----
        {
            DocumentEditor threaded1;
            check(threaded1.open(threaded, QString(), &error),
                  QStringLiteral("setup: the editor opens the threaded copy: %1").arg(error));

            DeletePagesDialog del(&tab, 0, &threaded1);
            QLineEdit *range = rangeOf(del);
            check(range != nullptr, "setup: the range box of 删除页面");
            if (range) {
                range->setText(QStringLiteral("1"));
                const QString said = saidOf(del);
                check(said.contains(QStringLiteral("这 1 条回答会失去")),
                      QStringLiteral("taking the note's page is said while it is still a request: %1")
                          .arg(said));
                range->setText(QStringLiteral("2"));
                const QString second = saidOf(del);
                check(!second.contains(QStringLiteral("条回答会失去")),
                      QStringLiteral("taking the reply's own page costs no thread, and the box "
                                     "stops saying it: %1")
                          .arg(second));
                range->setText(QStringLiteral("1-2"));
                check(!saidOf(del).contains(QStringLiteral("条回答会失去")),
                      QStringLiteral("and taking both leaves no reply behind to be standalone: %1")
                          .arg(saidOf(del)));
                range->setText(QStringLiteral("1"));
                check(saidOf(del).contains(QStringLiteral("这 1 条回答会失去")),
                      QStringLiteral("asking again says it again -- the number is this request's, "
                                     "not the last one's: %1")
                          .arg(saidOf(del)));
                check(!threaded1.isDirty(),
                      "and none of that was ever pressed: the pending list is still empty");
            }

            SplitPagesDialog spl(&tab, &threaded1);
            auto *size = spl.findChild<QSpinBox *>();
            check(size != nullptr, "setup: the spin box of 拆分文件");
            if (size) {
                size->setValue(1);
                check(saidOf(spl).contains(QStringLiteral("会被拆到两个文件里去")),
                      QStringLiteral("每 1 页一份 sends the note and its reply to separate files: %1")
                          .arg(saidOf(spl)));
                size->setValue(2);
                check(!saidOf(spl).contains(QStringLiteral("拆到两个文件")),
                      QStringLiteral("while 每 2 页一份 leaves the pair together, and says nothing "
                                     "about them: %1")
                          .arg(saidOf(spl)));
            }
        }

        // ---- §47 in the merge box: the donor's replies, counted off the rows as they stand ----
        {
            DocumentEditor editor;
            check(editor.open(book, QString(), &error),
                  QStringLiteral("setup: a plain document to merge into: %1").arg(error));
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({threaded}).isEmpty(),
                  "setup: the threaded document is on the list");
            QString donorRange;
            const auto rows = box.findChildren<QTableWidget *>();
            check(!rows.isEmpty(), "setup: the list has a row for it");
            if (!rows.isEmpty()) {
                donorRange = rows.at(0)->item(0, 2) ? rows.at(0)->item(0, 2)->text() : QString();
                check(!donorRange.isEmpty(),
                      QStringLiteral("and the row spells its own whole range: %1").arg(donorRange));
            }
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("条是对另一条批注的回答")),
                  QStringLiteral("the merge box says the thread comes in broken: %1").arg(said));

            // The control: the same row, a range that names neither of the two pages carrying
            // annotations. Nothing else about the box has changed, so a count that came from the
            // file rather than the request would still print the sentence.
            if (!rows.isEmpty() && rows.at(0)->item(0, 2)) {
                rows.at(0)->item(0, 2)->setText(QStringLiteral("10-12"));
                check(!saidOf(box).contains(QStringLiteral("条是对另一条批注的回答")),
                      QStringLiteral("and it goes when the pages named hold no reply: %1")
                          .arg(saidOf(box)));
                rows.at(0)->item(0, 2)->setText(donorRange);
                check(saidOf(box).contains(QStringLiteral("条是对另一条批注的回答")),
                      QStringLiteral("back on when the range names them again: %1").arg(saidOf(box)));
            }
        }
    }

    // ---- F8: 保存 writes back into the file the reader has open ----
    //
    // The order in DocumentTab::saveEdits() is the whole of this slice: a hidden copy
    // beside the original, the copy read back with qpdf, every reader of the original
    // let go, ReplaceFileW, reopen. Each half is asserted on a copy in a scratch folder,
    // never on a fixture: the tests may write here, and the fixtures are shared with
    // every other block in this file.
    //
    // What the checks are shaped against: a save that reports success without the bytes
    // changing, a replace that fails but loses the original, a staged file left behind,
    // and the tab's own write coming back as "this file changed on disk" -- the last one
    // is why FileChangeNotice grew beginSelfWrite()/endSelfWrite(), and the control for
    // it is a change made by somebody else afterwards, which still has to raise the bar.
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for F8's save");
        const QString source = scratch.filePath(QStringLiteral("many-pages.pdf"));
        check(QFile::copy(directory + QStringLiteral("/many-pages.pdf"), source),
              "setup: a copy of the 40-page fixture to save into");

        auto sha = [](const QString &file) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha256);
        };
        auto bytesOf = [](const QString &file) {
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly))
                return QByteArray();
            return in.readAll();
        };
        auto overwrite = [](const QString &file, const QByteArray &bytes) {
            QFile out(file);
            if (!out.open(QIODevice::ReadWrite))
                return false;
            out.seek(0);
            const bool written = out.write(bytes) == bytes.size();
            const bool trimmed = out.resize(bytes.size());
            out.flush();
            return written && trimmed;
        };
        auto pagesIn = [](const QString &file, const QString &password = QString()) {
            try {
                QPDF document;
                const QByteArray key = password.toUtf8();
                document.processFile(file.toUtf8().constData(),
                                     key.isEmpty() ? nullptr : key.constData());
                return int(QPDFPageDocumentHelper(document).getAllPages().size());
            } catch (const std::exception &) {
                return -1;
            }
        };
        auto stagedBeside = [](const QString &folder) {
            return QDir(folder)
                .entryList({QStringLiteral("xyzpdf-save-*.pdf")}, QDir::Files | QDir::Hidden)
                .size();
        };

        const QByteArray originalBytes = bytesOf(source);
        const QByteArray beforeSave = sha(source);
        check(!originalBytes.isEmpty() && pagesIn(source) == 40,
              "setup: the copy is a 40-page PDF");

        check(tab.open(source) == Document::Status::Ready, "the copy opens");
        // Zoom and turn before standing on page 10: the page has to be in the
        // viewport at the size it is asked for, or the render that the save below is
        // supposed to wait for is never requested at all.
        tab.setZoom(1.75);
        tab.rotateBy(90);
        tab.setCurrentPage(9);
        check(waitFor([view] { return view->hasImageFor(9); }),
              "the reader is on page 10 and it is drawn");
        const int pageBefore = tab.currentPage();
        const double zoomBefore = tab.zoom();
        const auto modeBefore = tab.zoomMode();
        const int rotationBefore = tab.rotation();
        check(pageBefore == 9 && zoomBefore > 1.5 && rotationBefore == 90,
              QStringLiteral("setup: page 10 at %1 times, the page turned %2 degrees")
                  .arg(zoomBefore)
                  .arg(rotationBefore));

        // The last page, so the page the reader stands on keeps its own number: this is
        // about the view coming back, not about renumbering after a delete above it.
        answerNextDialog<DeletePagesDialog>(QStringLiteral("40"));
        tab.deletePages();
        check(tab.hasUnsavedEdits(), "the last page is on the pending list");
        check(tab.title().endsWith(QStringLiteral(" *")),
              QStringLiteral("and the tab says so before the save (%1)").arg(tab.title()));

        // A save that goes through says nothing: a box here would mean the path
        // refused, and the waiter also keeps a broken save from hanging the run.
        bool firstBoxSeen = false;
        QString firstBoxText;
        answerNextBox(QStringLiteral("取消"), &firstBoxSeen, &firstBoxText);
        check(tab.saveEdits(), "保存 writes the change back into the file the tab is reading");
        check(!firstBoxSeen,
              QStringLiteral("with no box to explain a failure (%1)").arg(firstBoxText));
        check(!tab.fileChangeNoticeVisible(),
              "and its own write is not reported as a change from outside");
        check(!waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 2000),
              "not even when the watcher has had two seconds to say something");
        check(tab.documentPath() == source, "the tab is still on that file");
        check(sha(source) != beforeSave,
              "the bytes on disk are not the ones that were there before the save");
        check(pagesIn(source) == 39,
              QStringLiteral("a parser opening the file finds 39 pages (%1)").arg(pagesIn(source)));
        check(tab.pageCount() == 39 && tab.isOpen(),
              QStringLiteral("the tab reopened it with 39 pages (%1)").arg(tab.pageCount()));
        check(tab.currentPage() == 9, "on the page the reader was standing on");
        check(tab.zoom() == zoomBefore && tab.zoomMode() == modeBefore
                  && tab.rotation() == rotationBefore,
              QStringLiteral("and the zoom, its mode and the page rotation came back with it "
                             "(%1 / %2)")
                  .arg(tab.zoom())
                  .arg(tab.rotation()));
        check(!tab.hasUnsavedEdits(), "nothing is pending any more");
        check(!tab.title().endsWith(QStringLiteral(" *")), "and the star is gone");

        // The control for the suppression: it is the settled size and time of *our* bytes
        // that are ignored, not every change. An outside write still raises the bar.
        check(overwrite(source, originalBytes), "setup: another program writes the file");
        check(waitFor([&tab] { return tab.fileChangeNoticeVisible(); }, 5000),
              "a later change made by somebody else still raises the bar");
        if (auto *close = tab.findChild<QToolButton *>(QStringLiteral("fileChangeClose")))
            close->click();

        // ---- the replace that Windows refuses ----
        //
        // An open handle without FILE_SHARE_DELETE: qpdf and Qt PDF can still read the
        // file (they ask for read sharing), staging succeeds, and ReplaceFileW is the
        // first step that cannot proceed. The guarantee to pin is that the original is
        // byte-identical and the staged copy does not survive the refusal.
        const QString locked = scratch.filePath(QStringLiteral("locked.pdf"));
        check(QFile::copy(directory + QStringLiteral("/text.pdf"), locked),
              "setup: a two-page copy for the locked-file case");
        check(tab.open(locked) == Document::Status::Ready, "the locked copy opens");
        answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
        tab.deletePages();
        check(tab.hasUnsavedEdits(), "and has a deletion pending");

        const QByteArray lockedBefore = sha(locked);
        const std::wstring lockedPath = QDir::toNativeSeparators(locked).toStdWString();
        HANDLE holder = CreateFileW(lockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(holder != INVALID_HANDLE_VALUE,
              "setup: another program holds the file without sharing delete");

        bool failureBoxSeen = false;
        QString failureBoxText;
        answerNextBox(QStringLiteral("取消"), &failureBoxSeen, &failureBoxText);
        const bool replaced = tab.saveEdits();
        if (holder != INVALID_HANDLE_VALUE)
            CloseHandle(holder);

        check(!replaced, "an in-place save that Windows will not let replace reports failure");
        check(failureBoxSeen, "and says so in a box rather than silently");
        check(failureBoxText.contains(QStringLiteral("Windows")),
              QStringLiteral("naming the Windows error (%1)").arg(failureBoxText));
        check(sha(locked) == lockedBefore,
              "the file that could not be replaced is byte-identical");
        check(pagesIn(locked) == 2, "still with its two pages");
        check(stagedBeside(scratch.path()) == 0,
              "and the staged copy written beside it was taken away");
        check(tab.hasUnsavedEdits(), "the pending list is still there");
        check(tab.isOpen() && tab.pageCount() == 2, "the original is back on screen");

        // The control: the same save, the same tab, the same list -- with the handle gone
        // it goes through, which is what makes the refusal above a refusal and not a save
        // path that never writes anything.
        bool controlBoxSeen = false;
        QString controlBoxText;
        answerNextBox(QStringLiteral("取消"), &controlBoxSeen, &controlBoxText);
        check(tab.saveEdits(), "the same save goes through once nothing holds the file");
        check(!controlBoxSeen,
              QStringLiteral("with nothing to report (%1)").arg(controlBoxText));
        check(sha(locked) != lockedBefore && pagesIn(locked) == 1,
              "and this time the file really changed, to the one-page document");
        check(!tab.hasUnsavedEdits(), "with nothing left pending");

        // ---- 另存为 ----
        //
        // The same pending list, written to a name the reader chooses: the tab moves to
        // the new file and the one it came from keeps every byte it had.
        const QString asSource = scratch.filePath(QStringLiteral("as-source.pdf"));
        const QString asTarget = scratch.filePath(QStringLiteral("as-target.pdf"));
        check(QFile::copy(directory + QStringLiteral("/text.pdf"), asSource),
              "setup: a two-page copy for 另存为");
        check(tab.open(asSource) == Document::Status::Ready, "it opens");
        tab.setCurrentPage(1);
        check(waitFor([view] { return view->hasImageFor(1); }), "on its second page");
        answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
        tab.deletePages();
        const QByteArray asBefore = sha(asSource);
        QString asError;
        check(tab.saveEditsTo(asTarget, &asError),
              QStringLiteral("另存为 writes the name it was given (%1)").arg(asError));
        check(tab.documentPath() == asTarget, "and the tab switches to the new file");
        check(sha(asSource) == asBefore && pagesIn(asSource) == 2,
              "the file it came from is byte-identical, still with its two pages");
        check(pagesIn(asTarget) == 1,
              QStringLiteral("while the new file holds the deletion (%1)").arg(pagesIn(asTarget)));
        check(tab.pageCount() == 1, "the tab shows the new file");
        check(tab.currentPage() == 0,
              "on the nearest page it has, since page 2 is not in the copy");
        check(!tab.hasUnsavedEdits(), "and nothing is pending");
        check(!tab.title().endsWith(QStringLiteral(" *")), "with no star");

        // ---- a file that cannot be written where it is ----
        //
        // The read-only attribute is the reachable member of the family the spec names
        // (WindowsApps needs an ACL this test cannot create, and a dropped network share
        // cannot be arranged while PDFium holds the file open). The refusal happens
        // before anything is staged, and the box offers 另存为 -- here answered 取消.
        const QString readOnly = scratch.filePath(QStringLiteral("read-only.pdf"));
        check(QFile::copy(directory + QStringLiteral("/text.pdf"), readOnly),
              "setup: a copy to make read-only");
        SetFileAttributesW(QDir::toNativeSeparators(readOnly).toStdWString().c_str(),
                           FILE_ATTRIBUTE_READONLY);
        check(tab.open(readOnly) == Document::Status::Ready, "a read-only file still opens");
        answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
        tab.deletePages();
        const QByteArray readOnlyBefore = sha(readOnly);
        bool readOnlyBoxSeen = false;
        QString readOnlyBoxText;
        answerNextBox(QStringLiteral("取消"), &readOnlyBoxSeen, &readOnlyBoxText);
        check(!tab.saveEdits(), "保存 refuses a read-only file");
        check(readOnlyBoxSeen, "with a box");
        check(readOnlyBoxText.contains(QStringLiteral("只读")),
              QStringLiteral("that says it is read-only (%1)").arg(readOnlyBoxText));
        check(sha(readOnly) == readOnlyBefore && stagedBeside(scratch.path()) == 0,
              "nothing was written and nothing staged was left behind");
        check(tab.hasUnsavedEdits(), "and the edits are still pending");
        // The control: clear the attribute and the same save goes through, which is what
        // makes the refusal above the attribute's and not the save path's.
        SetFileAttributesW(QDir::toNativeSeparators(readOnly).toStdWString().c_str(),
                           FILE_ATTRIBUTE_NORMAL);
        bool attributeGoneBoxSeen = false;
        QString attributeGoneBoxText;
        answerNextBox(QStringLiteral("取消"), &attributeGoneBoxSeen, &attributeGoneBoxText);
        check(tab.saveEdits(), "with the attribute gone the same save goes through");
        check(!attributeGoneBoxSeen,
              QStringLiteral("with nothing to report (%1)").arg(attributeGoneBoxText));
        check(pagesIn(readOnly) == 1 && !tab.hasUnsavedEdits(),
              "and writes the one-page document");

        // ---- a save cannot step on a print ----
        //
        // F8 §5 leaves the foreground-job rules alone, and 保存 is a new caller of them:
        // the job has this tab's document on the stack, and pulling the file out from
        // under it is the failure §100 is about. Both halves are asked, because a refusal
        // that never lifted would pass the first alone.
        const QString busy = scratch.filePath(QStringLiteral("busy.pdf"));
        check(QFile::copy(directory + QStringLiteral("/text.pdf"), busy),
              "setup: a copy for the busy-tab case");
        check(tab.open(busy) == Document::Status::Ready, "it opens");
        {
            const DocumentTab::ForegroundJob job(&tab);
            answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
            tab.deletePages();
            check(tab.hasUnsavedEdits(), "with a deletion pending and a job standing in");
            bool busyBoxSeen = false;
            QString busyBoxText;
            answerNextBox(QStringLiteral("确定"), &busyBoxSeen, &busyBoxText);
            check(!tab.saveEdits(), "保存 refuses a tab that a print or an export is standing in");
            check(busyBoxSeen, "and says so in a box");
            check(busyBoxText.contains(QStringLiteral("打印或导出")),
                  QStringLiteral("naming the job that is in the way (%1)").arg(busyBoxText));
            check(tab.hasUnsavedEdits(), "and leaves the list where it was");
        }
        {
            bool afterJobBoxSeen = false;
            QString afterJobBoxText;
            answerNextBox(QStringLiteral("取消"), &afterJobBoxSeen, &afterJobBoxText);
            check(tab.saveEdits(), "the same save goes through the moment the job is over");
            check(!afterJobBoxSeen,
                  QStringLiteral("with nothing to report (%1)").arg(afterJobBoxText));
            check(pagesIn(busy) == 1 && !tab.hasUnsavedEdits(), "and writes the one-page document");
        }

        // ---- a document opened with a password stays one ----
        //
        // No 加密 on the list means Carry: the writer's default preserves the source's
        // /Encrypt dictionary, and the tab reopens with the key it opened with. Both
        // halves are asserted -- the file still refuses a reader without the key, and the
        // tab is open on it -- because either alone would pass with the other broken.
        const QString sealed = scratch.filePath(QStringLiteral("sealed.pdf"));
        const QString key = QStringLiteral("hunter2");
        {
            DocumentEditor maker;
            QString error;
            check(maker.open(directory + QStringLiteral("/text.pdf"), QString(), &error),
                  QStringLiteral("setup: a plain source for the protected copy: %1").arg(error));
            DocumentEditor::Protection guard;
            guard.what = DocumentEditor::Protection::What::Encrypt;
            guard.password = key;
            check(maker.protectWith(guard, &error),
                  QStringLiteral("setup: a password is recorded: %1").arg(error));
            check(maker.saveAs(sealed, &error),
                  QStringLiteral("setup: and written out: %1").arg(error));
        }
        check(pagesIn(sealed) == -1, "setup: the copy needs a password to be read");
        check(pagesIn(sealed, key) == 2,
              QStringLiteral("setup: and qpdf opens it with the key (%1)").arg(pagesIn(sealed, key)));
        const Document::Status sealedOpen = tab.open(sealed, key);
        check(sealedOpen == Document::Status::Ready,
              QStringLiteral("it opens with the key (%1)").arg(int(sealedOpen)));
        answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
        tab.deletePages();
        bool sealedBoxSeen = false;
        QString sealedBoxText;
        answerNextBox(QStringLiteral("取消"), &sealedBoxSeen, &sealedBoxText);
        check(tab.saveEdits(), "保存 writes a protected document in place");
        check(!sealedBoxSeen,
              QStringLiteral("with nothing to report (%1)").arg(sealedBoxText));
        check(pagesIn(sealed) == -1, "the file still refuses a reader without the key");
        check(pagesIn(sealed, key) == 1,
              QStringLiteral("and with it opens as the one-page document (%1)").arg(pagesIn(sealed, key)));
        check(tab.isOpen() && tab.pageCount() == 1,
              "the tab itself reopened with the key carried over");

        check(tab.open(path) == Document::Status::Ready,
              "the fixture is back for the rest of the run");
    }

    // ---- H3: 暂存文件不留在读者的文件夹里 ----
    //
    // What a save leaves behind when the process is killed between writing the hidden copy
    // beside a reader's document and swapping it in. The rule is in src/core/stagingcleanup.h
    // and it is narrower than tidying a folder: only a path this program wrote down itself,
    // only while the file at it is still hidden and still the one that was staged, and only
    // once the process that wrote it has gone.
    //
    // The two ends are asserted apart because they look identical from the folder: a sweep
    // that deletes nothing and a save that records nothing both leave the same file there. So
    // the first half asks the two functions that stage a file whether they wrote down where
    // they put it -- PDFium's first stage and qpdf's, plus a whole 保存 through the tab, which
    // is what takes the record off again. The second half plants records over real files and
    // asks the sweep for each of its four answers: recorded and orphaned -> deleted; never
    // recorded -> not even looked at; the writer still running -> left alone; the delete
    // refused -> kept on the list for the next launch.
    //
    // Nothing here asks the reader a question, so nothing here expects a box: a dialog in the
    // sweep would not be a red assertion, it would be this run sitting still until the
    // watchdog lifted it -- which is why the four cases are driven through the class and not
    // through a window.
    {
        QTemporaryDir scratch;
        check(scratch.isValid(), "setup: a scratch folder for the staging records");
        // One spelling for every comparison below: the list is keyed by the same cleaned
        // absolute path the product records, and a drive letter or a separator that disagrees
        // with itself reads as "no records here" -- a green that measured nothing.
        const QString folder = StagingCleanup::normalized(scratch.path());
        const QString thisProgram
            = QFileInfo(QCoreApplication::applicationFilePath()).fileName();

        auto clearRecords = [] {
            const QVector<AppSettings::StagedRecord> existing = AppSettings::stagedRecords();
            for (const AppSettings::StagedRecord &entry : existing)
                AppSettings::forgetStagedRecord(entry.path);
        };
        auto recordsHere = [folder] {
            QVector<AppSettings::StagedRecord> found;
            const QVector<AppSettings::StagedRecord> all = AppSettings::stagedRecords();
            for (const AppSettings::StagedRecord &entry : all) {
                if (entry.path.startsWith(folder, Qt::CaseInsensitive))
                    found.append(entry);
            }
            return found;
        };
        // A file shaped exactly like a staged one: our own bytes, a name from the same pattern
        // the writers use, and the hidden attribute the save sets.
        auto stageFile = [folder](const QString &name) {
            const QString path = QDir(folder).filePath(name);
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QString();
            out.write("%PDF-1.7\n% a staged copy whose save never came back\n");
            out.close();
            SetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str(),
                               FILE_ATTRIBUTE_HIDDEN);
            return path;
        };
        // Write down a record the way the sweep reads it. `createdMs` of -1 takes the file's
        // own creation time, which is what a save that got as far as hiding it puts there.
        auto plant = [thisProgram](const QString &path, qint64 pid, qint64 createdMs) {
            AppSettings::StagedRecord entry;
            entry.path = StagingCleanup::normalized(path);
            entry.pid = pid;
            entry.program = thisProgram;
            entry.createdMs
                = createdMs >= 0
                    ? createdMs
                    : QFileInfo(path).birthTime().toMSecsSinceEpoch();
            AppSettings::putStagedRecord(entry);
        };
        auto hiddenStagedHere = [folder] {
            return QDir(folder)
                .entryList({QStringLiteral("xyzpdf-save-*.pdf")}, QDir::Files | QDir::Hidden)
                .size();
        };
        // A process that came and went, for the one fact the sweep cannot make up: the writer
        // is gone. QProcess closes its handle on destruction, so by the time the number comes
        // back nothing holds the process object either.
        auto deadPid = [] {
            qint64 pid = 0;
            {
                QProcess doomed;
                doomed.start(QStringLiteral("cmd.exe"),
                             {QStringLiteral("/c"), QStringLiteral("exit")});
                if (doomed.waitForStarted(5000)) {
                    // Read the number while the process is still there: it is the process's
                    // own, and an ended QProcess is not obliged to remember it.
                    pid = qint64(doomed.processId());
                    doomed.waitForFinished(5000);
                }
            }
            return pid;
        };

        const qint64 dead = deadPid();
        check(dead > 0, "setup: a process that started and ended, to be a dead pid");
        note(QStringLiteral("h3: dead pid=%1 this pid=%2 program=%3")
                 .arg(dead)
                 .arg(QCoreApplication::applicationPid())
                 .arg(thisProgram));

        // ---- the two write sites record what they wrote ----
        //
        // PDFium's first stage of a form save (src/document/pdfiumbackend.cpp) and qpdf's
        // stage of an in-place one (src/edit/documenteditor.cpp). Both have to put the path,
        // this process and the file's own creation time in the list, because a record missing
        // any of the three is a leftover nobody may touch.
        {
            QString why;
            const QString formCopy = QDir(folder).filePath(QStringLiteral("fillable.pdf"));
            check(FormSampleFile::write(formCopy, 0, false, &why),
                  QStringLiteral("setup: a form sample to stage: %1").arg(why));
            check(tab.open(formCopy) == Document::Status::Ready,
                  QStringLiteral("h3: the form copy opens (%1)").arg(tab.lastError()));
            clearRecords();
            QString fieldStage;
            QString stageError;
            auto *surface = tab.formSurface();
            check(surface != nullptr, "h3: the opened document has a form surface");
            const bool staged = surface && surface->stageFieldValues(folder, &fieldStage,
                                                                     &stageError);
            check(staged, QStringLiteral("h3: the form stage wrote its copy (%1)")
                              .arg(stageError));
            const QVector<AppSettings::StagedRecord> afterForm = recordsHere();
            check(afterForm.size() == 1,
                  QStringLiteral("h3: and put exactly one record down for it (%1)")
                      .arg(afterForm.size()));
            if (afterForm.size() == 1) {
                const AppSettings::StagedRecord &entry = afterForm.at(0);
                check(entry.path == StagingCleanup::normalized(fieldStage),
                      QStringLiteral("h3: the recorded path is the file (%1 vs %2)")
                          .arg(entry.path, fieldStage));
                check(entry.pid == qint64(QCoreApplication::applicationPid()),
                      QStringLiteral("h3: and the pid is the process that wrote it (%1)")
                          .arg(entry.pid));
                check(entry.program == thisProgram, "h3: and so is the program beside it");
                check(entry.createdMs > 0
                          && entry.createdMs
                              == QFileInfo(fieldStage).birthTime().toMSecsSinceEpoch(),
                      QStringLiteral("h3: and the creation time is read off the file, not a "
                                     "clock (%1)")
                          .arg(entry.createdMs));
            }
            // The record is what a later launch deletes by, so taking the file away has to take
            // the record with it -- otherwise the next sweep is reading a list of names that
            // are no longer anybody's.
            check(StagingCleanup::removeStaged(fieldStage),
                  "h3: deleting a staged copy reports success");
            check(recordsHere().isEmpty() && !QFileInfo::exists(fieldStage),
                  "h3: and its record went with it");

            // qpdf's stage, the second of the two in a save that has both filled fields and
            // page edits -- and the whole of one that only has page edits.
            const QString structCopy = QDir(folder).filePath(QStringLiteral("struct-copy.pdf"));
            check(QFile::copy(directory + QStringLiteral("/many-pages.pdf"), structCopy),
                  "setup: a copy of the 40-page fixture to stage a delete into");
            DocumentEditor editor;
            check(editor.open(structCopy, QString(), &why),
                  QStringLiteral("h3: the editor opens the copy: %1").arg(why));
            check(editor.deletePages(QVector<int> {39}, &why),
                  QStringLiteral("h3: and one page is on the pending list: %1").arg(why));
            clearRecords();
            QString stagedInPlace;
            check(editor.stageInPlace(&stagedInPlace, &why),
                  QStringLiteral("h3: the in-place stage wrote its copy: %1").arg(why));
            const QVector<AppSettings::StagedRecord> afterStage = recordsHere();
            check(afterStage.size() == 1,
                  QStringLiteral("h3: with one record beside it (%1)")
                      .arg(afterStage.size()));
            if (afterStage.size() == 1) {
                const AppSettings::StagedRecord &entry = afterStage.at(0);
                check(entry.path == StagingCleanup::normalized(stagedInPlace)
                          && entry.pid == qint64(QCoreApplication::applicationPid())
                          && entry.createdMs > 0,
                      QStringLiteral("h3: and it names the file, this process and the time "
                                     "(%1)").arg(entry.createdMs));
                check(QFileInfo(stagedInPlace).exists(),
                      "h3: the staged file is on disk until the swap takes it");
            }
            StagingCleanup::removeStaged(stagedInPlace);
            clearRecords();
        }

        // ---- a whole 保存 takes its own record off ----
        //
        // The tab is the one that calls ReplaceFileW, so it is the one that has to stop
        // recording the file that swap consumed. Asserted as "nothing left in either place":
        // the folder holds no hidden staged copy and the list holds nothing pointing at this
        // folder -- a record that outlived its own swap would be the sweep deleting a file
        // somebody else later put at that name.
        {
            const QString source = QDir(folder).filePath(QStringLiteral("save-through.pdf"));
            check(QFile::copy(directory + QStringLiteral("/text.pdf"), source),
                  "setup: a copy for the save that goes through");
            check(tab.open(source) == Document::Status::Ready, "h3: it opens");
            clearRecords();
            answerNextDialog<DeletePagesDialog>(QStringLiteral("1"));
            tab.deletePages();
            check(tab.hasUnsavedEdits(), "h3: with a deletion pending");
            bool boxSeen = false;
            QString boxText;
            answerNextBox(QStringLiteral("取消"), &boxSeen, &boxText);
            check(tab.saveEdits(),
                  QStringLiteral("h3: 保存 writes it back in place (%1)").arg(boxText));
            check(!boxSeen, "h3: with nothing to report");
            check(recordsHere().isEmpty(),
                  "h3: and no record is left for the file that was swapped in");
            check(hiddenStagedHere() == 0, "h3: and no hidden staged copy in the folder");
        }

        // ---- the sweep's four answers ----
        //
        // One recorded file and one that only looks the same, in the same folder, in a single
        // run: that pairing is the point. A sweep that went looking by name pattern would take
        // both, and every count in this block would still be the number it wanted -- the
        // unrecorded neighbour is what makes "只删自己记过的" a measured thing rather than a
        // sentence in a comment.
        {
            clearRecords();
            const QString orphan = stageFile(QStringLiteral("xyzpdf-save-orphan.pdf"));
            const QString sibling = stageFile(QStringLiteral("xyzpdf-save-sibling.pdf"));
            check(!orphan.isEmpty() && !sibling.isEmpty(), "setup: two hidden staged-shaped files");
            plant(orphan, dead, -1);
            check(recordsHere().size() == 1, "setup: only one of them is recorded");

            const StagingCleanup::Sweep swept = StagingCleanup::sweepStaleRecords();
            note(QStringLiteral("h3 sweep orphan: removed=%1 kept=%2 dropped=%3 [%4]")
                     .arg(swept.removed)
                     .arg(swept.keptForLater)
                     .arg(swept.dropped)
                     .arg(swept.reasons.join(QStringLiteral(" | "))));
            check(swept.removed == 1,
                  QStringLiteral("h3: the recorded one was taken away (%1)").arg(swept.removed));
            check(!QFileInfo::exists(orphan), "h3: and it is gone from the folder");
            check(QFileInfo::exists(sibling),
                  "h3: the file nobody recorded is still exactly where it was");
            check(recordsHere().isEmpty(), "h3: and its record went with the file");
            StagingCleanup::forget(sibling);
            QFile::remove(sibling);
        }

        // The writer still holds the name: another window, or another copy of this program, is
        // mid-save. Left alone, and kept on the list. Then the same file with a dead number
        // beside it goes -- which is what says the pid is what held it, not the folder, not the
        // name, not the attributes.
        {
            clearRecords();
            const QString live = stageFile(QStringLiteral("xyzpdf-save-live.pdf"));
            plant(live, qint64(QCoreApplication::applicationPid()), -1);
            const StagingCleanup::Sweep held = StagingCleanup::sweepStaleRecords();
            note(QStringLiteral("h3 sweep live: removed=%1 kept=%2 dropped=%3 [%4]")
                     .arg(held.removed)
                     .arg(held.keptForLater)
                     .arg(held.dropped)
                     .arg(held.reasons.join(QStringLiteral(" | "))));
            check(held.removed == 0 && held.keptForLater == 1,
                  QStringLiteral("h3: a save still running is not cleaned up (removed=%1 kept=%2)")
                      .arg(held.removed)
                      .arg(held.keptForLater));
            check(QFileInfo::exists(live), "h3: the file is where it was");
            check(recordsHere().size() == 1, "h3: and it is still on the list");

            plant(live, dead, -1);
            const StagingCleanup::Sweep after = StagingCleanup::sweepStaleRecords();
            check(after.removed == 1 && !QFileInfo::exists(live),
                  QStringLiteral("h3: the same record with the writer gone does take it away "
                                 "(removed=%1)").arg(after.removed));
        }

        // A delete that Windows refuses -- another program has the file open with no sharing --
        // keeps the record so the next launch can try again, and says nothing. Closing the
        // handle and sweeping once more is the control inside the case: it shows the first
        // answer was the handle and not a broken delete path.
        {
            clearRecords();
            const QString held = stageFile(QStringLiteral("xyzpdf-save-held.pdf"));
            plant(held, dead, -1);
            const std::wstring native = QDir::toNativeSeparators(held).toStdWString();
            HANDLE blocker = CreateFileW(native.c_str(), GENERIC_READ, 0, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_HIDDEN, nullptr);
            check(blocker != INVALID_HANDLE_VALUE,
                  "setup: the staged file is held open with no sharing at all");
            const StagingCleanup::Sweep refused = StagingCleanup::sweepStaleRecords();
            note(QStringLiteral("h3 sweep held: removed=%1 kept=%2 dropped=%3 [%4]")
                     .arg(refused.removed)
                     .arg(refused.keptForLater)
                     .arg(refused.dropped)
                     .arg(refused.reasons.join(QStringLiteral(" | "))));
            check(refused.removed == 0 && refused.keptForLater == 1,
                  QStringLiteral("h3: a file that will not delete stays on the list (kept=%1)")
                      .arg(refused.keptForLater));
            check(QFileInfo::exists(held) && recordsHere().size() == 1,
                  "h3: both the file and its record are still there");
            CloseHandle(blocker);
            const StagingCleanup::Sweep retried = StagingCleanup::sweepStaleRecords();
            check(retried.removed == 1 && !QFileInfo::exists(held) && recordsHere().isEmpty(),
                  QStringLiteral("h3: and the next sweep finishes it (removed=%1)")
                      .arg(retried.removed));
        }

        // The other two refusals, which both end with the file kept and the record dropped:
        // a reader who unhid it has claimed it, and a path whose creation time does not match
        // is a path that now holds a different file. Neither is ours to delete, and a record
        // that can never be acted on is not a plan.
        {
            clearRecords();
            const QString claimed = stageFile(QStringLiteral("xyzpdf-save-claimed.pdf"));
            plant(claimed, dead, -1);
            SetFileAttributesW(QDir::toNativeSeparators(claimed).toStdWString().c_str(),
                               FILE_ATTRIBUTE_NORMAL);
            const StagingCleanup::Sweep unhidden = StagingCleanup::sweepStaleRecords();
            check(unhidden.removed == 0 && unhidden.dropped == 1
                      && QFileInfo::exists(claimed) && recordsHere().isEmpty(),
                  QStringLiteral("h3: a file that is no longer hidden is left, and unrecorded "
                                 "(removed=%1 dropped=%2)")
                      .arg(unhidden.removed)
                      .arg(unhidden.dropped));
            QFile::remove(claimed);

            const QString swapped = stageFile(QStringLiteral("xyzpdf-save-swapped.pdf"));
            const qint64 birth = QFileInfo(swapped).birthTime().toMSecsSinceEpoch();
            plant(swapped, dead, birth + 60000); // a time two minutes off its own file
            const StagingCleanup::Sweep mismatch = StagingCleanup::sweepStaleRecords();
            check(mismatch.removed == 0 && mismatch.dropped == 1
                      && QFileInfo::exists(swapped) && recordsHere().isEmpty(),
                  QStringLiteral("h3: a creation time that does not match leaves the file alone "
                                 "(removed=%1 dropped=%2)")
                      .arg(mismatch.removed)
                      .arg(mismatch.dropped));
            QFile::remove(swapped);
        }

        // The two shapes a record can have before it is ever useful: a path that no longer has
        // a file behind it (the save took it away in a run this list never caught up with), and
        // a record made before the bytes existed -- createdMs 0, which is exactly what a kill
        // inside the writer leaves. The first drops the line; the second still deletes, because
        // the hidden file at a path we recorded is the case this whole class exists for.
        {
            clearRecords();
            const QString absent = QDir(folder).filePath(QStringLiteral("xyzpdf-save-absent.pdf"));
            plant(absent, dead, 0);
            const StagingCleanup::Sweep gone = StagingCleanup::sweepStaleRecords();
            check(gone.dropped == 1 && gone.removed == 0 && recordsHere().isEmpty(),
                  QStringLiteral("h3: a record whose file is already gone is only bookkeeping "
                                 "(dropped=%1 removed=%2)")
                      .arg(gone.dropped)
                      .arg(gone.removed));

            const QString halfway = stageFile(QStringLiteral("xyzpdf-save-halfway.pdf"));
            plant(halfway, dead, 0);
            const StagingCleanup::Sweep stamped
                = StagingCleanup::sweepStaleRecords();
            note(QStringLiteral("h3 sweep halfway: removed=%1 kept=%2 dropped=%3 [%4]")
                     .arg(stamped.removed)
                     .arg(stamped.keptForLater)
                     .arg(stamped.dropped)
                     .arg(stamped.reasons.join(QStringLiteral(" | "))));
            check(stamped.removed == 1 && !QFileInfo::exists(halfway),
                  QStringLiteral("h3: a half-recorded hidden file still goes (removed=%1)")
                      .arg(stamped.removed));
        }

        // A PID that is not this program's -- the number was recycled by something else
        // entirely. Recorded with a live pid under a program name that no longer fits, the
        // sweep has to read it as "the writer is gone" rather than "somebody is saving".
        {
            clearRecords();
            const QString recycled = stageFile(QStringLiteral("xyzpdf-save-recycled.pdf"));
            AppSettings::StagedRecord entry;
            entry.path = recycled;
            entry.pid = qint64(QCoreApplication::applicationPid());
            entry.program = QStringLiteral("not-this-program.exe");
            entry.createdMs = QFileInfo(recycled).birthTime().toMSecsSinceEpoch();
            AppSettings::putStagedRecord(entry);
            const StagingCleanup::Sweep reuse = StagingCleanup::sweepStaleRecords();
            check(reuse.removed == 1 && !QFileInfo::exists(recycled),
                  QStringLiteral("h3: a recycled pid is not the writer any more (removed=%1)")
                      .arg(reuse.removed));
        }

        clearRecords();
        check(AppSettings::stagedRecords().isEmpty(), "h3: the list is left empty for the next block");
        check(tab.open(path) == Document::Status::Ready,
              "the fixture is back for the rest of the run");
    }

    // ---- 已签名的文档：能填的一半，不能动的一半 ----
    //
    // handoff-06 H4 from the tab's side. Three seams, and each is a place the layers on
    // either side of it pass their own tests: a fields-only save has to reach disk through
    // PDFium's incremental writer (so the spans a signature names still hold their bytes),
    // a structural save has to go through the qpdf rewrite (so they do not), and the
    // certification signature's /P plus a signature's /Lock decide which gestures arrive at
    // the form layer at all. The per-layer readings are in pdfiumbackend_smoke, the writer's
    // own copy of the two answers in documenteditor_smoke.
    //
    // Every file here is written into a scratch folder. The one save in this block that goes
    // "in place" is on a copy, because 永不写用户的 PDF -- a test that wrote a fixture would
    // be the rule's first counterexample.
    {
        QTemporaryDir signedDir;
        check(signedDir.isValid(), "signed: a scratch folder for the signed samples");
        const QString signedFolder = signedDir.path();
        const auto signedPathOf = [&signedFolder](const char *name) {
            return signedFolder + QLatin1Char('/') + QLatin1String(name);
        };
        QString signedWhy;
        check(FormSampleFile::writeSigned(signedPathOf("signed-one.pdf"), 0, 1, false, &signedWhy),
              QStringLiteral("signed: the one-signature sample is written (%1)").arg(signedWhy));
        check(FormSampleFile::writeSigned(signedPathOf("signed-p1.pdf"), 1, 1, false, &signedWhy),
              QStringLiteral("signed: the /P 1 sample is written (%1)").arg(signedWhy));
        check(FormSampleFile::writeSigned(signedPathOf("signed-p2.pdf"), 2, 1, false, &signedWhy),
              QStringLiteral("signed: the /P 2 sample is written (%1)").arg(signedWhy));
        check(FormSampleFile::writeSigned(signedPathOf("signed-locked.pdf"), 0, 1, true,
                                          &signedWhy),
              QStringLiteral("signed: the /Lock sample is written (%1)").arg(signedWhy));

        const FormSampleFile::SignedGeometry signedFields;
        const auto bytesOf = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return QByteArray();
            return file.readAll();
        };
        const auto digestOf = [](const QByteArray &bytes) {
            return QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        // A field's centre as the view draws it, the same arithmetic the fill block uses.
        const auto fieldPointOn = [&signedFields](PageView *view, const QRectF &rect) {
            const QPointF centre = FormSampleFile::userCentre(rect);
            const QRectF page = view->pageRectInViewport(0);
            return QPointF(page.left() + centre.x() / 595.0 * page.width(),
                           page.top() + (842.0 - centre.y()) / 842.0 * page.height());
        };

        // ---- a fields-only save is an append, and the signature's bytes stay put ----
        {
            const QString opened = signedPathOf("signed-one.pdf");
            const QString live = signedFolder + QStringLiteral("/signed-fill.pdf");
            check(QFile::copy(opened, live), "signed: the fill test works on its own copy");
            const QByteArray before = bytesOf(live);
            QVector<int> range;
            {
                PdfiumBackend holder;
                check(holder.load(live, QString()) == Document::Status::Ready,
                      "signed: the copy opens in the reader before it is touched");
                range = FormSampleFile::signatureByteRange(live, 0);
            }
            check(range.size() == 4, "signed: and states a two-span /ByteRange");

            DocumentTab signedTab;
            signedTab.resize(900, 700);
            signedTab.show();
            check(signedTab.open(live) == Document::Status::Ready,
                  QStringLiteral("signed: the copy opens in a tab (%1)")
                      .arg(signedTab.lastError()));
            PageView *signedView = viewOf(signedTab);
            check(waitFor([signedView] { return signedView->isRendered(0); }),
                  "signed: and its page is drawn");
            QWidget *canvas = signedView->viewport();
            const QPointF fieldPoint = fieldPointOn(signedView, signedFields.textField);
            sendMouse(canvas, QEvent::MouseButtonPress, fieldPoint);
            sendMouse(canvas, QEvent::MouseButtonRelease, fieldPoint);
            check(waitFor([&signedTab] { return signedTab.view()->fieldHasFocus(); }),
                  "signed: the text field takes the keyboard");
            signedView->setFocus();
            for (const QChar character : QStringLiteral("XYZ")) {
                QKeyEvent press(QEvent::KeyPress,
                                int(Qt::Key_A) + character.toLatin1() - 'A', Qt::NoModifier,
                                QString(character));
                QApplication::sendEvent(signedView, &press);
            }
            check(waitFor([&signedTab] { return signedTab.hasFieldEdits(); }),
                  "signed: three characters are a pending field change");
            check(signedTab.saveEdits(), "signed: 保存 writes the answers back into the copy");

            const QByteArray after = bytesOf(live);
            const QByteArray coveredBefore = FormSampleFile::signatureCoveredBytes(before, range);
            const QByteArray coveredAfter = FormSampleFile::signatureCoveredBytes(after, range);
            note(QStringLiteral("signed fill: %1 bytes before, %2 after; covered %3 bytes, "
                                "sha256 before=%4 after=%5")
                     .arg(before.size())
                     .arg(after.size())
                     .arg(coveredBefore.size())
                     .arg(digestOf(coveredBefore), digestOf(coveredAfter)));
            check(after.size() > before.size(),
                  "signed: the save appended a revision instead of rewriting the file");
            check(!before.isEmpty() && after.left(before.size()) == before,
                  "signed: its first N bytes are the file that was opened, byte for byte");
            check(!coveredBefore.isEmpty() && coveredBefore == coveredAfter,
                  QStringLiteral("signed: so the two spans the signature names still hold the "
                                 "same bytes (%1)")
                      .arg(range.at(1) + range.at(3)));
            {
                PdfiumBackend reader;
                check(reader.load(live, QString()) == Document::Status::Ready,
                      "signed: the saved copy still opens");
                QString valueWhy;
                const QString nameValue
                    = FormSampleFile::fieldValue(live, QStringLiteral("name"), &valueWhy);
                note(QStringLiteral("signed fill: the saved file reads name=[%1]").arg(nameValue));
                check(nameValue.contains(QStringLiteral("XYZ")),
                      QStringLiteral("signed: and the answer is in the appended revision (%1)")
                          .arg(valueWhy));
            }
        }

        // ---- an encrypted signed document: the append keeps the prefix, and the key ----
        //
        // H4's measurement: what FPDF_INCREMENTAL does on a file that is both signed and
        // encrypted. The source is qpdf-encrypted (R3/RC4-128, a real user password),
        // because that is the case where 保存 has two things to keep at once. The bytes are
        // compared with the encrypted file the tab opened -- qpdf re-serialised the signed
        // sample on the way in, so the sample it came from is not the file under test.
        {
            const QString sealed = signedFolder + QStringLiteral("/signed-encrypted.pdf");
            {
                QPDF qpdf;
                qpdf.processFile(signedPathOf("signed-one.pdf").toUtf8().constData());
                QPDFWriter out(qpdf, sealed.toUtf8().constData());
                out.setR3EncryptionParametersInsecure("let-me-in", "owner-key", true, true,
                                                      true, true, true, true, qpdf_r3p_full);
                out.write();
            }
            check(QFileInfo::exists(sealed),
                  "signed encrypt: setup: an encrypted signed copy was written");
            const QByteArray before = bytesOf(sealed);
            QVector<int> range;
            {
                PdfiumBackend holder;
                check(holder.load(sealed, QStringLiteral("let-me-in"))
                          == Document::Status::Ready,
                      "signed encrypt: the encrypted copy opens with its key");
                check(holder.signatureCount() == 1,
                      "signed encrypt: and still carries the signature");
                range = FormSampleFile::signatureByteRange(sealed, 0,
                                                           QStringLiteral("let-me-in"));
                // qpdf re-serialised the sample on the way in, so these offsets are the
                // numbers its signature dictionary carries, not a claim that the encrypted
                // copy verifies. What the rows below measure is that the append moves none
                // of those bytes.
                note(QStringLiteral("signed encrypt: /ByteRange [%1,%2,%3,%4]")
                         .arg(range.value(0))
                         .arg(range.value(1))
                         .arg(range.value(2))
                         .arg(range.value(3)));
            }

            DocumentTab sealedTab;
            sealedTab.resize(900, 700);
            sealedTab.show();
            check(sealedTab.open(sealed, QStringLiteral("let-me-in"))
                      == Document::Status::Ready,
                  QStringLiteral("signed encrypt: a tab opens it (%1)")
                      .arg(sealedTab.lastError()));
            PageView *sealedView = viewOf(sealedTab);
            check(waitFor([sealedView] { return sealedView->isRendered(0); }),
                  "signed encrypt: and its page is drawn");
            const QPointF fieldPoint = fieldPointOn(sealedView, signedFields.textField);
            sendMouse(sealedView->viewport(), QEvent::MouseButtonPress, fieldPoint);
            sendMouse(sealedView->viewport(), QEvent::MouseButtonRelease, fieldPoint);
            check(waitFor([&sealedTab] { return sealedTab.view()->fieldHasFocus(); }),
                  "signed encrypt: the field takes the keyboard through the encryption");
            sealedView->setFocus();
            for (const QChar character : QStringLiteral("KEY")) {
                QKeyEvent press(QEvent::KeyPress,
                                int(Qt::Key_A) + character.toLatin1() - 'A', Qt::NoModifier,
                                QString(character));
                QApplication::sendEvent(sealedView, &press);
            }
            check(waitFor([&sealedTab] { return sealedTab.hasFieldEdits(); }),
                  "signed encrypt: and the answer is a pending change");
            check(sealedTab.saveEdits(), "signed encrypt: 保存 writes it back");

            const QByteArray after = bytesOf(sealed);
            const QByteArray coveredBefore = FormSampleFile::signatureCoveredBytes(before, range);
            const QByteArray coveredAfter = FormSampleFile::signatureCoveredBytes(after, range);
            note(QStringLiteral("signed encrypt: %1 bytes before, %2 after; covered %3 bytes; "
                                "sha256 before=%4 after=%5")
                     .arg(before.size())
                     .arg(after.size())
                     .arg(coveredBefore.size())
                     .arg(digestOf(coveredBefore), digestOf(coveredAfter)));
            check(after.size() > before.size(),
                  "signed encrypt: the save appended a revision");
            check(!before.isEmpty() && after.left(before.size()) == before,
                  "signed encrypt: its first N bytes are the encrypted file that was opened, "
                  "byte for byte");
            check(coveredBefore.isEmpty() || coveredBefore == coveredAfter,
                  "signed encrypt: and every byte the signature names is still there");
            {
                PdfiumBackend reread;
                check(reread.load(sealed, QStringLiteral("let-me-in"))
                          == Document::Status::Ready,
                      "signed encrypt: the saved file still opens with the same key");
                check(reread.signatureCount() == 1,
                      "signed encrypt: with its signature object intact");
                PdfiumBackend noKey;
                check(noKey.load(sealed, QString()) == Document::Status::NeedsPassword,
                      "signed encrypt: and still asks for the key from a reader who has none");
            }
        }

        // ---- a structural save is a rewrite, and the spans do not survive it ----
        {
            const QString opened = signedPathOf("signed-one.pdf");
            const QString turned = signedFolder + QStringLiteral("/signed-turned.pdf");
            DocumentTab signedTab;
            signedTab.resize(900, 700);
            signedTab.show();
            check(signedTab.open(opened) == Document::Status::Ready,
                  QStringLiteral("signed: the sample opens for the structural save (%1)")
                      .arg(signedTab.lastError()));
            // Through the tab's own door, which is the box a reader meets: page 1 is named
            // and 旋转这些页 is pressed by the timer helper above.
            answerNextDialog<RotatePagesDialog>(QStringLiteral("1"));
            signedTab.rotatePages();
            check(signedTab.hasUnsavedEdits(), "signed: the turn is a pending structural edit");
            check(signedTab.saveEditsTo(turned, &signedWhy),
                  QStringLiteral("signed: 另存为 writes the turned copy (%1)").arg(signedWhy));
            check(QFileInfo::exists(turned), "signed: and the new file is there");

            QVector<int> range;
            {
                PdfiumBackend holder;
                check(holder.load(opened, QString()) == Document::Status::Ready,
                      "signed: the untouched source still opens for its /ByteRange");
                range = FormSampleFile::signatureByteRange(opened, 0);
            }
            check(range.size() == 4, "signed: which is still a two-span /ByteRange");
            const QByteArray before = bytesOf(opened);
            const QByteArray after = bytesOf(turned);
            const QByteArray coveredBefore
                = FormSampleFile::signatureCoveredBytes(before, range);
            const qint64 coveredEnd = range.size() == 4 ? qint64(range.at(2)) + range.at(3) : 0;
            const bool spansStillThere = range.size() == 4 && after.size() >= coveredEnd;
            const QByteArray coveredAfter = spansStillThere
                ? FormSampleFile::signatureCoveredBytes(after, range)
                : QByteArray();
            note(QStringLiteral("signed turn: source %1 bytes, rewritten %2 bytes, covered "
                                "ends at %3; sha256 before=%4 after=%5")
                     .arg(before.size())
                     .arg(after.size())
                     .arg(coveredEnd)
                     .arg(digestOf(coveredBefore),
                          spansStillThere ? digestOf(coveredAfter)
                                          : QStringLiteral("(span past the end)")));
            check(!coveredBefore.isEmpty(), "signed: the source's spans name real bytes");
            // Either the rewrite kept the spans and put different bytes in them, or it
            // made the file shorter than the second span. Both are the signature's claim
            // failing, and pinning which one would be pinning qpdf's serialization.
            check(!(spansStillThere && coveredAfter == coveredBefore),
                  "signed: the rewritten file no longer holds the bytes the signature covered");

            PdfiumBackend reader;
            check(reader.load(turned, QString()) == Document::Status::Ready,
                  "signed: the turned file opens in the reader");
            const QSizeF size = reader.pageSize(0);
            note(QStringLiteral("signed turn: page size %1x%2").arg(size.width()).arg(size.height()));
            check(size.width() > size.height(),
                  "signed: and its page is turned the way the dialog asked");
        }

        // ---- all eight boxes say what a save costs, before anything is pressed ----
        //
        // Constructed, not shown: every one of them builds its sentence in the constructor,
        // which is also when the reader first sees it. The unsigned-document control for the
        // "this sentence is not always there" half is the XFA block above -- the same phrase
        // appears in no box there.
        {
            DocumentTab signedTab;
            signedTab.resize(900, 700);
            signedTab.show();
            check(signedTab.open(signedPathOf("signed-one.pdf")) == Document::Status::Ready,
                  "signed: a signed document opens for the eight boxes");
            // A writer opened beside the tab, the way the XFA block does it: the boxes take
            // the writer's copy of the file's facts, and this is the same file and the same
            // class the tab itself hands them.
            DocumentEditor signedEditor;
            check(signedEditor.open(signedPathOf("signed-one.pdf"), QString(), &signedWhy),
                  QStringLiteral("signed: with a writer opened on it (%1)").arg(signedWhy));
            {
                const auto sentenceOf = [](QDialog &box) {
                    for (QLabel *label : box.findChildren<QLabel *>()) {
                        if (label->text().contains(QStringLiteral("原来那份文件")))
                            return label->text();
                    }
                    return QString();
                };
                const auto saysTheCost = [&sentenceOf](QDialog &box, const char *name) {
                    const QString sentence = sentenceOf(box);
                    check(sentence.contains(QStringLiteral("保存之后这 1 个数字签名都会失效")),
                          QStringLiteral("signed: %1 gives the cost before the press: %2")
                              .arg(QLatin1String(name), sentence));
                };
                DeletePagesDialog del(&signedTab, 0, &signedEditor);
                saysTheCost(del, "删除页面");
                MovePagesDialog mov(&signedTab, 0, &signedEditor);
                saysTheCost(mov, "移动页面");
                MergePagesDialog mer(&signedTab, &signedEditor);
                saysTheCost(mer, "合并文件进来");
                SplitPagesDialog spl(&signedTab, &signedEditor);
                saysTheCost(spl, "拆分文件");
                RotatePagesDialog rot(&signedTab, 0, &signedEditor);
                saysTheCost(rot, "旋转页面");
                ProtectPagesDialog pro(&signedTab, &signedEditor);
                saysTheCost(pro, "加密 / 去掉口令");
                WatermarkDialog wat(&signedTab, &signedEditor);
                saysTheCost(wat, "水印");
                BookmarkDialog book(&signedTab, &signedEditor, Outline{});
                saysTheCost(book, "编辑书签");
            }
        }

        // ---- /P: which half a reader gets ----
        //
        // The one-page sample makes the rotation box a clean carrier for the question: turning
        // page 1 is a request the same document accepts without a certification signature, so
        // the only variable between these three rows is /P. That unsigned row is the control,
        // in the same run and with the same request.
        {
            const auto rotateOnePage = [](QDialog &box) {
                for (QLineEdit *edit : box.findChildren<QLineEdit *>()) {
                    if (edit->placeholderText().contains(QStringLiteral("1-3")))
                        edit->setText(QStringLiteral("1"));
                }
                auto *buttons = box.findChild<QDialogButtonBox *>();
                return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
            };

            DocumentTab signedTab;
            signedTab.resize(900, 700);
            signedTab.show();
            check(signedTab.open(signedPathOf("signed-one.pdf")) == Document::Status::Ready,
                  "signed: the signed sample opens for the /P readings");
            DocumentEditor signedEditor;
            check(signedEditor.open(signedPathOf("signed-one.pdf"), QString(), &signedWhy),
                  QStringLiteral("signed: with a writer opened on it (%1)").arg(signedWhy));
            {
                RotatePagesDialog signedBox(&signedTab, 0, &signedEditor);
                QPushButton *signedOkay = rotateOnePage(signedBox);
                check(signedOkay && signedOkay->isEnabled(),
                      "signed: a signature with no certification still allows the turn");
            }
            DocumentEditor plain;
            check(plain.open(QStringLiteral("tests/fixtures/text.pdf"), QString(), &signedWhy),
                  QStringLiteral("signed: setup: an unsigned document (%1)").arg(signedWhy));
            RotatePagesDialog plainBox(&signedTab, 0, &plain);
            QPushButton *plainOkay = rotateOnePage(plainBox);
            check(plainOkay && plainOkay->isEnabled(),
                  "signed: control: the same request on an unsigned file can confirm");
            signedTab.deleteLater();
        }

        // ---- /P 1: fields read-only, structural boxes grey ----
        {
            DocumentTab p1Tab;
            p1Tab.resize(900, 700);
            p1Tab.show();
            check(p1Tab.open(signedPathOf("signed-p1.pdf")) == Document::Status::Ready,
                  QStringLiteral("signed: the /P 1 sample opens in a tab (%1)").arg(p1Tab.lastError()));
            PageView *p1View = viewOf(p1Tab);
            check(waitFor([p1View] { return p1View->isRendered(0); }),
                  "signed: and its page is drawn");
            QWidget *canvas = p1View->viewport();
            const QPointF fieldPoint = fieldPointOn(p1View, signedFields.textField);
            sendMouse(canvas, QEvent::MouseButtonPress, fieldPoint);
            sendMouse(canvas, QEvent::MouseButtonRelease, fieldPoint);
            // Anything PDFium would do with the click has happened by here; the wait is only
            // so that "no focus" is not "not yet".
            waitFor([] { return false; }, 300);
            check(!p1Tab.view()->fieldHasFocus(),
                  "signed: on a /P 1 file the field does not take the keyboard");
            check(!p1Tab.hasFieldEdits(), "signed: and the click did not become an edit");

            // The properties row is fed by the same count, and it is the only place the
            // number is shown without a sentence around it.
            // The row the properties box gets depends on whether the check has answered, so the
            // reading waits for the answer rather than racing it: before it, the row says only the
            // count, after it the count and how many came out valid -- and for this sample, whose
            // /Contents is a placeholder rather than a PKCS#7, that is none of them.
            waitFor([&p1Tab] { return p1Tab.signatureRowCount() == 1; });
            const PropertiesDialog::Info p1Info = p1Tab.propertiesInfo();
            check(p1Info.signatureCount == 1,
                  "signed: the properties gather the signature count");
            {
                PropertiesDialog box(nullptr, p1Info);
                QStringList texts;
                for (QLabel *label : box.findChildren<QLabel *>())
                    texts.append(label->text());
                check(texts.contains(QStringLiteral("数字签名"))
                        && texts.contains(QStringLiteral("1 个，其中 0 个有效")),
                      QStringLiteral("signed: and the dialog shows one (%1)")
                          .arg(texts.join(QStringLiteral(" | "))));
            }

            DocumentEditor p1Editor;
            check(p1Editor.open(signedPathOf("signed-p1.pdf"), QString(), &signedWhy)
                    && p1Editor.docMdpPermission() == 1,
                  QStringLiteral("signed: a writer opened beside it reads the same /P 1 (%1)")
                      .arg(signedWhy));
            {
                const auto sentenceOf = [](QDialog &box) {
                    for (QLabel *label : box.findChildren<QLabel *>()) {
                        if (label->text().contains(QStringLiteral("原来那份文件")))
                            return label->text();
                    }
                    return QString();
                };
                const auto rotateOnePage = [](QDialog &box) {
                    for (QLineEdit *edit : box.findChildren<QLineEdit *>()) {
                        if (edit->placeholderText().contains(QStringLiteral("1-3")))
                            edit->setText(QStringLiteral("1"));
                    }
                    auto *buttons = box.findChild<QDialogButtonBox *>();
                    return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
                };
                RotatePagesDialog rot(&p1Tab, 0, &p1Editor);
                QPushButton *okay = rotateOnePage(rot);
                check(okay && !okay->isEnabled(),
                      "signed: 旋转页面 cannot confirm under /P 1, with one page named");
                const QString sentence = sentenceOf(rot);
                note(QStringLiteral("signed p1: the box says [%1]")
                         .arg(QString(sentence).replace(QLatin1Char('\n'), QLatin1Char(' '))));
                check(sentence.contains(QStringLiteral("不允许任何改动")),
                      "signed: and says the certification signature forbids every change");
                check(sentence.contains(QStringLiteral("保存之后这 1 个数字签名都会失效")),
                      "signed: with the cost beside the reason");
            }
        }

        // ---- /P 2: fields fillable, structural boxes still grey ----
        {
            DocumentTab p2Tab;
            p2Tab.resize(900, 700);
            p2Tab.show();
            check(p2Tab.open(signedPathOf("signed-p2.pdf")) == Document::Status::Ready,
                  QStringLiteral("signed: the /P 2 sample opens in a tab (%1)").arg(p2Tab.lastError()));
            PageView *p2View = viewOf(p2Tab);
            check(waitFor([p2View] { return p2View->isRendered(0); }),
                  "signed: and its page is drawn");
            QWidget *canvas = p2View->viewport();
            const QPointF fieldPoint = fieldPointOn(p2View, signedFields.textField);
            sendMouse(canvas, QEvent::MouseButtonPress, fieldPoint);
            sendMouse(canvas, QEvent::MouseButtonRelease, fieldPoint);
            check(waitFor([&p2Tab] { return p2Tab.view()->fieldHasFocus(); }),
                  "signed: /P 2 lets the field take the keyboard");
            p2View->setFocus();
            for (const QChar character : QStringLiteral("AB")) {
                QKeyEvent press(QEvent::KeyPress,
                                int(Qt::Key_A) + character.toLatin1() - 'A', Qt::NoModifier,
                                QString(character));
                QApplication::sendEvent(p2View, &press);
            }
            check(waitFor([&p2Tab] { return p2Tab.hasFieldEdits(); }),
                  "signed: and what was typed is a pending change");

            DocumentEditor p2Editor;
            check(p2Editor.open(signedPathOf("signed-p2.pdf"), QString(), &signedWhy)
                    && p2Editor.docMdpPermission() == 2,
                  QStringLiteral("signed: a writer opened beside it reads the same /P 2 (%1)")
                      .arg(signedWhy));
            {
                RotatePagesDialog rot(&p2Tab, 0, &p2Editor);
                for (QLineEdit *edit : rot.findChildren<QLineEdit *>()) {
                    if (edit->placeholderText().contains(QStringLiteral("1-3")))
                        edit->setText(QStringLiteral("1"));
                }
                auto *buttons = rot.findChild<QDialogButtonBox *>();
                QPushButton *okay = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
                check(okay && !okay->isEnabled(),
                      "signed: /P 2 permits filling and nothing else, so the turn is grey");
                QString sentence;
                for (QLabel *label : rot.findChildren<QLabel *>()) {
                    if (label->text().contains(QStringLiteral("原来那份文件")))
                        sentence = label->text();
                }
                check(sentence.contains(QStringLiteral("只允许填写表单")),
                      QStringLiteral("signed: and the reason says form filling is all it "
                                     "permits: %1")
                          .arg(sentence));
            }
        }

        // ---- a /Lock seals one field and leaves the one beside it free ----
        //
        // PDFium has no API for /Lock and does not enforce one (measured on chromium/8111
        // for the guard, see pdfiumformfill.cpp), so this is our guard arriving through the
        // tab: the list is read by qpdf at open and handed to the form layer before the first
        // gesture. The unsealed field is the companion that keeps a blanket "no clicks" bug
        // from reading as a pass.
        {
            DocumentTab lockedTab;
            lockedTab.resize(900, 700);
            lockedTab.show();
            check(lockedTab.open(signedPathOf("signed-locked.pdf")) == Document::Status::Ready,
                  QStringLiteral("signed: the /Lock sample opens in a tab (%1)")
                      .arg(lockedTab.lastError()));
            PageView *lockedView = viewOf(lockedTab);
            check(waitFor([lockedView] { return lockedView->isRendered(0); }),
                  "signed: and its page is drawn");
            QWidget *canvas = lockedView->viewport();
            const QPointF sealedPoint = fieldPointOn(lockedView, signedFields.lockedField);
            sendMouse(canvas, QEvent::MouseButtonPress, sealedPoint);
            sendMouse(canvas, QEvent::MouseButtonRelease, sealedPoint);
            waitFor([] { return false; }, 300);
            check(!lockedTab.view()->fieldHasFocus(),
                  "signed: the field the signature seals does not take the keyboard");
            const QPointF openPoint = fieldPointOn(lockedView, signedFields.textField);
            sendMouse(canvas, QEvent::MouseButtonPress, openPoint);
            sendMouse(canvas, QEvent::MouseButtonRelease, openPoint);
            check(waitFor([&lockedTab] { return lockedTab.view()->fieldHasFocus(); }),
                  "signed: while the field beside it does");
        }
    }

    // ---- 签名的答案上侧栏 ----
    //
    // handoff-07 I1 from the tab's side: the fourth sidebar page appears for a document that
    // carries signatures and not for one that does not, a row names the verdict and the page the
    // field is on, pressing it moves the page, and the verification is waited for before the
    // document it read is replaced.
    //
    // The signature is made during this run by a key in a temporary container and a certificate
    // that never reaches a store (tests/signaturesample.h). No root is injected here, so the
    // honest verdict on this machine is 证书不受信任或已过期 -- and that is the reading worth
    // having in the tab test: it can only come from a check that ran.
    {
        QTemporaryDir verifyDir;
        check(verifyDir.isValid(), "verify: a scratch folder for the checked samples");
        const QString verifyFolder = verifyDir.path();
        SignedSample::Identity signer;
        QString signerWhy;
        const QDateTime verifyNow = QDateTime::currentDateTime();
        const bool verifyBuilt
            = SignedSample::createIdentity(&signer, QStringLiteral("XYZPDF tab signer"),
                                           verifyNow.addDays(-2), verifyNow.addDays(200),
                                           &signerWhy);
        check(verifyBuilt, QStringLiteral("verify: an in-memory certificate (%1)")
                               .arg(signerWhy));
        if (verifyBuilt) {
            const QString pageTwoPath = verifyFolder + QStringLiteral("/sig-page2.pdf");
            SignedSample::Shape onSecond;
            onSecond.signSecondPage = true;
            QByteArray secondBytes;
            check(SignedSample::write(pageTwoPath, signer, onSecond, &secondBytes, &signerWhy),
                  QStringLiteral("verify: a sample signed on its second page (%1)")
                      .arg(signerWhy));

            DocumentTab verifyTab;
            verifyTab.resize(900, 700);
            verifyTab.show();
            verifyTab.setSidebarVisible(true);
            check(verifyTab.open(pageTwoPath) == Document::Status::Ready,
                  QStringLiteral("verify: the tab opens it (%1)").arg(verifyTab.lastError()));
            // The check runs on a worker, so the row arrives. Waiting for it is the test: a panel
            // that is filled synchronously would pass the same assertions with no thread at all,
            // and the drainRenders() wait underneath is what makes the answer safe.
            check(waitFor([&verifyTab] { return verifyTab.signatureRowCount() == 1; }),
                  QStringLiteral("verify: one signature row arrives from the worker (%1 rows)")
                      .arg(verifyTab.signatureRowCount()));
            check(verifyTab.signaturePanelVisible(),
                  "verify: and the 签名 page is showing for a document that carries one");
            const QString row = verifyTab.signatureRowText(0);
            std::cout << "MEASURE verify row: [" << row.toStdString() << "]\n";
            check(row.contains(QStringLiteral("证书不受信任或已过期")),
                  QStringLiteral("the row says which of the four it is: %1").arg(row));
            check(row.contains(QStringLiteral("XYZPDF tab signer")),
                  QStringLiteral("and names the signer the certificate names: %1").arg(row));
            // The join that handoff-07 asked for by value rather than by order: the page the row
            // carries is the page the *field* is on, which only the qpdf half can answer.
            check(verifyTab.signatureRowPage(0) == 1,
                  QStringLiteral("and the row points at the second page, where the widget is (%1)")
                      .arg(verifyTab.signatureRowPage(0)));

            // Pressing it moves the page. Started from the first page so that the move is the
            // reading, not the absence of one.
            verifyTab.setCurrentPage(0);
            check(verifyTab.currentPage() == 0, "verify: the tab starts on the first page");
            verifyTab.selectSignatureRow(0);
            check(waitFor([&verifyTab] { return verifyTab.currentPage() == 1; }),
                  QStringLiteral("verify: pressing the row goes to the signature's page (%1)")
                      .arg(verifyTab.currentPage()));

            // A second document replaces the first: the panel must not still be showing the
            // answer about a file the tab no longer reads. This is drainRenders() and the
            // identity guard working together -- a stale list is the shape that would otherwise
            // say "1 signature" about an unsigned document.
            const QString plainCopy = verifyFolder + QStringLiteral("/plain.pdf");
            check(QFile::copy(directory + QStringLiteral("/text.pdf"), plainCopy),
                  "verify: an unsigned copy for the replacement reading");
            check(verifyTab.open(plainCopy) == Document::Status::Ready,
                  QStringLiteral("verify: the same tab opens the unsigned copy (%1)")
                      .arg(verifyTab.lastError()));
            check(!verifyTab.signaturePanelVisible(),
                  "verify: and the 签名 page is taken away for a document with none");
            check(waitFor([&verifyTab] { return verifyTab.signatureRowCount() == 0; }, 5000),
                  QStringLiteral("verify: no row from the previous document is left (%1)")
                      .arg(verifyTab.signatureRowCount()));
            check(verifyTab.signatureSummary().isEmpty(),
                  "verify: and the unsigned document gets no summary sentence");

            // The properties row: the count, and the second number only once the check has
            // answered. Re-opened so the answer has arrived before the box is read.
            check(verifyTab.open(pageTwoPath) == Document::Status::Ready,
                  "verify: back to the signed file for the properties row");
            check(waitFor([&verifyTab] { return verifyTab.signatureRowCount() == 1; }),
                  "verify: its answer arrives again");
            const PropertiesDialog::Info verifyInfo = verifyTab.propertiesInfo();
            check(verifyInfo.signatureCount == 1,
                  "verify: the properties count is the file's own");
            check(verifyInfo.validSignatureCount == 0,
                  "verify: and zero of them came back valid on this machine");

            // The appended-revision fact, on the tab's own answer: one update after the signing
            // is a line in the detail, and the covered bytes still match.
            const QString appendedPath = verifyFolder + QStringLiteral("/sig-appended.pdf");
            SignedSample::Shape appendedShape;
            appendedShape.revisionsAfter = 1;
            check(SignedSample::write(appendedPath, signer, appendedShape, nullptr, &signerWhy),
                  QStringLiteral("verify: a sample with one update appended (%1)")
                      .arg(signerWhy));
            DocumentTab appendedTab;
            appendedTab.resize(900, 700);
            appendedTab.show();
            check(appendedTab.open(appendedPath) == Document::Status::Ready,
                  "verify: the appended sample opens");
            check(waitFor([&appendedTab] { return appendedTab.signatureRowCount() == 1; }),
                  "verify: and is answered");
        }
        SignedSample::releaseIdentity(&signer);
    }

    // ---- 加密 / 去掉口令: the box and the editor refuse the same requests ----
    //
    // The dialog greys 确定 for exactly the requests DocumentEditor::protectWith() and
    // ::removeProtection() turn down, and it does that by reading the same two answers the
    // save reads. That sharing is the arrangement, and it is the kind that rots quietly: let
    // one of the refusals through and the reader meets a warning box after pressing 确定;
    // grey one the editor would have taken and the feature is unreachable. So both sides of
    // each refusal are asked here. The box is also rebuilt between them the way the menu
    // rebuilds it on every click -- which is why a dialog can never be seen agreeing with an
    // older version of the list.
    {
        DocumentEditor editor;
        QString error;
        check(editor.open(path, QString(), &error),
              QStringLiteral("setup: an unprotected document opens: %1").arg(error));

        const auto okayOf = [](ProtectPagesDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](ProtectPagesDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto choose = [](ProtectPagesDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };
        const auto groupStartingWith = [](ProtectPagesDialog &box, const QString &words) {
            const auto groups = box.findChildren<QGroupBox *>();
            for (QGroupBox *group : groups) {
                if (group->title().startsWith(words))
                    return group;
            }
            return static_cast<QGroupBox *>(nullptr);
        };

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            QGroupBox *keys = groupStartingWith(box, QStringLiteral("打开口令"));
            QGroupBox *limits = groupStartingWith(box, QStringLiteral("允许的用途"));
            check(keys && limits, "the box asks for a口令 and offers the six permissions");
            check(okay && okay->isEnabled() && keys->isEnabled() && limits->isEnabled(),
                  "nothing is pending and a file that has never been locked can still be locked, "
                  "so it opens on 加密 with every question live");

            check(choose(box, QStringLiteral("去掉口令和权限")), "the other direction is on the box");
            check(okay && !okay->isEnabled(),
                  "over a file that was never locked, 去掉口令 greys the button");
            check(saidBy(box).contains(QStringLiteral("没有东西可去掉")),
                  QStringLiteral("and says that rather than only greying: %1").arg(saidBy(box)));
            check(!editor.removeProtection(&error),
                  "the editor refuses the same request the box greyed");
            check(!keys->isEnabled() && !limits->isEnabled(),
                  "a口令 and six permissions that no answer will read are put away, not left on show");

            check(choose(box, QStringLiteral("加上口令和权限")), "back to 加密");
            check(keys->isEnabled() && limits->isEnabled(), "and the two questions are back");
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            check(edits.size() == 2,
                  "the口令 is asked twice, because nothing here can report it back later");
            // The name a field answers to when nothing points at it is its group's title, so
            // a reader that cannot see the screen asked both fields 打开口令 -- measured in a
            // real window through UI Automation, which is also the only way anyone would ever
            // notice. The labels are buddied to the fields so the two answer apart.
            const auto nameOf = [](QLineEdit *edit) {
                QAccessibleInterface *face = QAccessible::queryAccessibleInterface(edit);
                return face ? face->text(QAccessible::Name) : QString();
            };
            check(nameOf(edits.at(0)) == QStringLiteral("口令") &&
                      nameOf(edits.at(1)) == QStringLiteral("再输一遍"),
                  QStringLiteral("the two fields are named apart by whatever only reads names: "
                                 "[%1] [%2]")
                      .arg(nameOf(edits.at(0)), nameOf(edits.at(1))));
            edits.at(0)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "one box filled and the other empty is two different口令");
            check(saidBy(box).contains(QStringLiteral("两次输入的口令不一样")),
                  QStringLiteral("and it names that as the reason: %1").arg(saidBy(box)));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && okay->isEnabled(), "agreeing lets it through");
            const DocumentEditor::Protection asked = box.request();
            check(asked.what == DocumentEditor::Protection::What::Encrypt
                      && asked.password == QStringLiteral("hunter2"),
                  "and what the box hands over is the direction plus the key as typed");
            check(editor.protectWith(asked, &error),
                  QStringLiteral("the editor takes it: %1").arg(error));
        }

        {
            // A reader who opens the box a second time finds the six boxes as they were
            // recorded and the口令 fields empty -- showing the key back is the thing the key
            // is for. So typing it again is asking for the request that is already in the
            // list, which is the second refusal, and changing one box is not.
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            const auto edits = box.findChildren<QLineEdit *>();
            edits.at(0)->setText(QStringLiteral("hunter2"));
            edits.at(1)->setText(QStringLiteral("hunter2"));
            check(okay && !okay->isEnabled(), "the same request asked again is greyed");
            check(saidBy(box).contains(QStringLiteral("不会多改什么")),
                  QStringLiteral("with the reason: %1").arg(saidBy(box)));
            check(!editor.protectWith(box.request(), &error),
                  "and the editor turns the same request down");

            const auto permissionBoxes = box.findChildren<QCheckBox *>();
            check(permissionBoxes.size() == 6,
                  "six permissions, one per thing PDF keeps a bit for");
            permissionBoxes.first()->click();
            check(okay && okay->isEnabled(), "clearing one of them is a change, so the button goes back");
            check(editor.protectWith(box.request(), &error),
                  QStringLiteral("and the editor records it: %1").arg(error));
        }

        {
            ProtectPagesDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            check(choose(box, QStringLiteral("去掉口令和权限")),
                  "the way back out of a 加密 that was just recorded is this same box");
            check(okay && okay->isEnabled(), "with a 加密 pending there is something to take off");
            check(saidBy(box).contains(QStringLiteral("会被这一步顶掉")),
                  QStringLiteral("and it says so: %1").arg(saidBy(box)));
            check(box.request().what == DocumentEditor::Protection::What::Decrypt,
                  "the box says which way it went");
            check(editor.removeProtection(&error),
                  QStringLiteral("the editor records that too: %1").arg(error));
            check(editor.passwordOfWrittenFile().isEmpty(),
                  "and the copy a save would write from here asks for no key at all");
        }
    }

    // ---- 加水印: the box offers what the model means, and greys what it refuses ----
    //
    // The third inert recorder. Two things a model test cannot see are checked here. One is what
    // the box shows before anybody has touched it: the first version opened offering 1% 不透明度,
    // because a QSpinBox whose range starts at 1 sits at 1 until someone asks for the default --
    // found in a real window, and a watermark at 1% is a watermark nobody can see, so the number
    // is now read from the same struct the writer reads. The other is §22's rule from the 加密
    // box: the button is off for exactly the requests addWatermark() turns down, so the reader
    // learns 再要一次不会多改什么 from a grey button and one sentence, not from a warning box
    // after a press. A fresh box per click, the way the menu builds one.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        const auto okayOf = [](WatermarkDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](WatermarkDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        // The two boxes are picked by the hint they carry, not by counting: a QSpinBox owns a
        // QLineEdit of its own, so the second line edit in creation order is the opacity field
        // and writing a page range into it changes nothing anyone can see.
        const auto editHintedWith = [](WatermarkDialog &box, const QString &hint) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText() == hint)
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto wordEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 机密、作废、内部资料"));
        };
        const auto rangeEdit = [&editHintedWith](WatermarkDialog &box) {
            return editHintedWith(box, QStringLiteral("例如 1-3,5,8-10"));
        };
        const auto radio = [](WatermarkDialog &box, const QString &starts) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *choice : radios) {
                if (choice->text().startsWith(starts))
                    return choice;
            }
            return static_cast<QRadioButton *>(nullptr);
        };

        {
            WatermarkDialog box(&tab, &editor);
            QPushButton *okay = okayOf(box);
            auto *spin = box.findChild<QSpinBox *>();
            check(spin != nullptr, "the box asks how solid the marks should be");
            check(spin && spin->value() == DocumentEditor::Watermark {}.opacityPercent,
                  QStringLiteral("and it opens on the model's own default, not on its minimum: %1%")
                      .arg(spin ? spin->value() : -1));
            check(okay && !okay->isEnabled(),
                  "a box nobody has typed a word into is off, because an empty word is a smudge");
            check(saidBy(box).contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("and says which half is missing: %1").arg(saidBy(box)));
            QRadioButton *all = radio(box, QStringLiteral("全部"));
            check(all && all->isChecked(),
                  "it opens on 全部, because that is what a reader who came to stamp every page means");
            const DocumentEditor::Watermark asked = box.request();
            check(asked.text.isEmpty() && asked.pages.size() == 40,
                  "and what it would hand over is every page and no word");
        }

        {
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled() && okay->text() == QStringLiteral("盖这 40 页"),
                  QStringLiteral("one word and the button counts the pages it lands on: %1")
                      .arg(okay ? okay->text() : QStringLiteral("(none)")));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("「机密」")) && said.contains(QStringLiteral("20%"))
                      && said.contains(QStringLiteral("盖在 40 页上")),
                  QStringLiteral("the summary repeats all three answers back: %1").arg(said));
            check(said.contains(QStringLiteral("画上去的一张图")),
                  "and warns that the marks are a drawing, which is the one fact here a reader "
                  "cannot discover by trying");
        }

        {
            // 空 is not 全部. The print parser reads an empty range as the whole document, and
            // this box has a radio for saying that on purpose -- a second way to say it would let
            // Enter stamp four hundred pages because a box was left blank.
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            radio(box, QStringLiteral("指定"))->click();
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "choosing 指定页码 and naming nothing is off");
            check(saidBy(box).contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and is named as the missing half: %1").arg(saidBy(box)));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 3 页"),
                  "three pages named, three pages counted");
            const DocumentEditor::Watermark asked = box.request();
            QStringList spelled;
            for (int page : asked.pages)
                spelled << QString::number(page + 1);
            check(asked.pages == QVector<int>({1, 2, 6}),
                  QStringLiteral("and the numbers handed over are the ones the save reads: %1")
                      .arg(spelled.join(QLatin1Char(','))));
        }

        {
            // A page an earlier 删页 took out is not inked, and the box says the number the save
            // will draw rather than the number of boxes ticked -- pagesStillThere() is the one
            // function both ends ask.
            check(editor.deletePages({1}, &error),
                  QStringLiteral("setup: the second page goes: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            wordEdit(box)->setText(QStringLiteral("机密"));
            rangeEdit(box)->setText(QStringLiteral("2-3,7"));
            check(radio(box, QStringLiteral("指定"))->isChecked(),
                  "a range typed in moves the choice to it without being clicked");
            check(saidBy(box).contains(QStringLiteral("说好的 3 页里有 2 页还在")),
                  QStringLiteral("and the box says which two: %1").arg(saidBy(box)));
            check(okayOf(box)->text() == QStringLiteral("盖这 2 页"),
                  "the button counts what is still there");
            check(editor.pagesStillThere(box.request().pages) == 2,
                  "read off the same one function the save will read");
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("setup: the word is recorded: %1").arg(error));
        }

        {
            // Reopening is what the menu does on every click. The recorded answer is offered back
            // rather than a blank box, and asking for exactly that again is the request the editor
            // refuses -- so the button is off before the press, in the same words.
            WatermarkDialog box(&tab, &editor);
            check(wordEdit(box)->text() == QStringLiteral("机密"),
                  "the box comes back saying what is already on the list");
            check(rangeEdit(box)->text() == QStringLiteral("3,7"),
                  QStringLiteral("with the pages a save would actually ink spelled back: %1")
                      .arg(rangeEdit(box)->text()));
            // 3,7 and not 2,3,7: addWatermark() dropped the page the earlier 删页 had taken out
            // before it recorded anything, so the list holds only pages that exist to be drawn on
            // and a box opened against it cannot offer a second way to ask for a ghost.
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the very same ask again is off");
            check(saidBy(box).contains(QStringLiteral("一模一样")),
                  QStringLiteral("said, not just greyed: %1").arg(saidBy(box)));
            check(!editor.addWatermark(box.request(), &error)
                      && error.contains(QStringLiteral("再要一次不会多改什么")),
                  QStringLiteral("and the editor refuses the identical request: %1").arg(error));

            wordEdit(box)->setText(QStringLiteral("作废"));
            check(okay->isEnabled() && okay->text() == QStringLiteral("盖这 2 页"),
                  "a different word is a change, so the button lights again");
            check(saidBy(box).contains(QStringLiteral("会把前面记下的「机密」换掉")),
                  QStringLiteral("and says it replaces rather than adds: %1").arg(saidBy(box)));
            check(editor.addWatermark(box.request(), &error),
                  QStringLiteral("the editor takes that one: %1").arg(error));
            check(editor.watermarkToWrite().text == QStringLiteral("作废"),
                  "one ask at a time: the list carries the newest word, not two layers");

            check(editor.undo(), "setup: the replacement is taken back");
            check(editor.watermarkToWrite().text == QStringLiteral("机密"),
                  "and the earlier answer is what a save would write now");
            WatermarkDialog again(&tab, &editor);
            QPushButton *againOkay = okayOf(again);
            check(againOkay && !againOkay->isEnabled(),
                  "a box built after the undo offers the undone-away answer back and greys it too, "
                  "because it reads the list as it stands now");
        }
    }

    // ---- who speaks first: the sentence a dialog picks when two of its guards hold ----
    //
    // Every edit dialog answers with ONE sentence out of a chain, and each link in that chain
    // has a test somewhere -- but a chain is a sequence, and nothing asked what the reader
    // gets when two guards are true at once. That is not a corner: 拆分挂着 and 列表是空的 are
    // both facts about the same box, and reordering the two links changes what a person reads
    // while every single-branch check stays green. §29 wrote this exact case down from a real
    // window and left it unpinned, so each pin below is built by making both guards true.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");
        const QString donor = directory + QStringLiteral("/dense.pdf");
        auto baseName = [](const QString &file) { return QFileInfo(file).fileName(); };
        QString error;
        // DocumentEditor is not copyable (its QPdfDocument is), so each case opens its own:
        // this fills one rather than returning one.
        const auto openBook = [&book, &error](DocumentEditor &editor) {
            check(editor.open(book, QString(), &error),
                  QStringLiteral("setup: a 40-page document to refuse in: %1").arg(error));
        };

        const auto saidOf = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto okayOf = [](QDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        // The range box, picked by its placeholder: 移动页面 has a second line edit beside it
        // (the anchor number), and taking the first child would read out the wrong one.
        const auto rangeOf = [](QDialog &box) {
            const auto edits = box.findChildren<QLineEdit *>();
            for (QLineEdit *edit : edits) {
                if (edit->placeholderText().contains(QStringLiteral("1-3")))
                    return edit;
            }
            return static_cast<QLineEdit *>(nullptr);
        };
        const auto pick = [](QDialog &box, const QString &words) {
            const auto radios = box.findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                if (radio->text() == words) {
                    radio->click();
                    return true;
                }
            }
            return false;
        };

        // 合并 with nothing in the list AND a split pending: the list is the more basic
        // nothing-to-do, and §29 read exactly this pair on a real screen (with an empty list
        // the pending split is never mentioned) without any test saying so.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error),
                  QStringLiteral("setup: a split of every 15 pages is pending: %1").arg(error));
            MergePagesDialog box(&tab, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有加上要合并进来的文件")),
                  QStringLiteral("an empty list answers first, even with a split pending: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("已经说了要拆成每")),
                  QStringLiteral("and the pending split is not mentioned while there is nothing "
                                 "to merge in: %1")
                      .arg(said));
        }

        // 合并 with a row that has not said its pages AND a split pending: here the split wins,
        // because it invalidates the whole request rather than one row of it. The order is the
        // decision, so it needs both sentences in the same run to be worth anything.
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.splitInto(15, &error), "setup: a split is pending");
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            check(table != nullptr && table->item(0, 2) != nullptr, "setup: the range cell");
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("已经说了要拆成每 15 页一份")),
                  QStringLiteral("a silent row does not outrank a request the model will refuse: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要合入哪几页")),
                  QStringLiteral("and the row is not named while the split is what stops it: %1")
                      .arg(said));
        }

        // 合并 with a silent row and no split: now the row is the only thing wrong, and the
        // sentence names which row-- so the same box moves from the split's answer to the
        // row's answer by undoing one list entry.
        {
            DocumentEditor editor;
            openBook(editor);
            MergePagesDialog box(&tab, &editor);
            check(box.addPaths({donor}).isEmpty(), "setup: the donor row is taken");
            auto *table = box.findChild<QTableWidget *>();
            table->item(0, 2)->setText(QStringLiteral(" "));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要合入哪几页"))
                      && said.contains(baseName(donor)),
                  QStringLiteral("without a pending split the silent row is what gets said, "
                                 "with its file named: %1")
                      .arg(said));
            // and the anchor question is further down the chain than the row question
            pick(box, QStringLiteral("指定页之后"));
            check(saidOf(box).contains(baseName(donor))
                      && !saidOf(box).contains(QStringLiteral("要说合并到第几页之后")),
                  QStringLiteral("a blank anchor does not cut in front of a silent row: %1")
                      .arg(saidOf(box)));
        }

        // 删除 with nothing typed: blank is nothing said, and the button waits (§13's finding,
        // fixed by making a blank box mean nothing rather than everything).
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("a box nobody typed into says that and does not count: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and the button is off, so nothing can be pressed");
        }

        // 删除 with pages typed that an earlier pending 删页 already took: those numbers are
        // original page numbers (§15), so typing 3 after deleting original page 3 names a page
        // that is not there to delete -- and the answer must not be "you said nothing".
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.deletePages({2}, &error),
                  QStringLiteral("setup: original page 3 is already taken out: %1").arg(error));
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这些页在前面的修改里已经删掉了")),
                  QStringLiteral("contradicting the reader is the thing to avoid, so this names "
                                 "the earlier change: %1")
                      .arg(said));
            check(!said.contains(QStringLiteral("还没有说出要删哪几页")),
                  QStringLiteral("and not the sentence for a blank box, which this is not: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and nothing is left to press");
        }

        // 删除 naming everything that is left: the floor is its own answer, and it is said
        // before the arithmetic that would otherwise report 剩下 0 页.
        {
            DocumentEditor editor;
            openBook(editor);
            DeletePagesDialog box(&tab, 0, &editor);
            QLineEdit *range = rangeOf(box);
            range->setText(QStringLiteral("1-40"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("这样一页也不剩了")),
                  QStringLiteral("the floor answers before the arithmetic does: %1").arg(said));
            check(!said.contains(QStringLiteral("剩下 0 页")),
                  QStringLiteral("and no line of numbers is offered for a document that cannot "
                                 "exist: %1")
                      .arg(said));
        }

        // §77: 删除页面 says, before the press, what taking a page out does to a form answer -- and
        // only when there is a form to say it about. The book above has no /AcroForm at all, which is
        // the half that must stay silent; this makes a copy of it with one field on the first page.
        // Written into a scratch directory, never beside the fixtures: that directory is globbed by
        // other tests (§17's rule, restated in AGENTS.md).
        {
            QTemporaryDir forms;
            check(forms.isValid(), "setup: a scratch folder for the form-bearing copy");
            const QString withForm = forms.filePath(QStringLiteral("one-field.pdf"));
            QString made;
            bool built = false;
            try {
                QPDF qpdf;
                qpdf.processFile(book.toUtf8().constData());
                QPDFObjectHandle page =
                    QPDFPageDocumentHelper(qpdf).getAllPages().front().getObjectHandle();
                QPDFObjectHandle field = QPDFObjectHandle::newDictionary();
                field.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                field.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
                field.replaceKey("/FT", QPDFObjectHandle::newName("/Tx"));
                field.replaceKey("/T", QPDFObjectHandle::newString("onOne"));
                field.replaceKey("/V", QPDFObjectHandle::newString("typed on page 1"));
                QPDFObjectHandle rect = QPDFObjectHandle::newArray();
                rect.appendItem(QPDFObjectHandle::newReal(60));
                rect.appendItem(QPDFObjectHandle::newReal(600));
                rect.appendItem(QPDFObjectHandle::newReal(260));
                rect.appendItem(QPDFObjectHandle::newReal(630));
                field.replaceKey("/Rect", rect);
                field = qpdf.makeIndirectObject(field);
                field.replaceKey("/P", page);
                QPDFObjectHandle annots = QPDFObjectHandle::newArray();
                annots.appendItem(field);
                page.replaceKey("/Annots", qpdf.makeIndirectObject(annots));
                QPDFObjectHandle fields = QPDFObjectHandle::newArray();
                fields.appendItem(field);
                QPDFObjectHandle form = QPDFObjectHandle::newDictionary();
                form.replaceKey("/Fields", fields);
                qpdf.getRoot().replaceKey("/AcroForm", qpdf.makeIndirectObject(form));
                QPDFWriter writer(qpdf, withForm.toUtf8().constData());
                writer.write();
                built = true;
            } catch (const std::exception &e) {
                made = QString::fromUtf8(e.what());
            }
            check(built, QStringLiteral("setup: a copy of the book carrying one form field: %1")
                             .arg(made));

            DocumentEditor formBook;
            check(formBook.open(withForm, QString(), &error),
                  QStringLiteral("setup: the editor opens it: %1").arg(error));
            check(formBook.hasFormFields(), "and the model answers that there is a form here");
            DeletePagesDialog inForm(&tab, 0, &formBook);
            check(saidOf(inForm).contains(QStringLiteral("字段填的答案属于它画在哪一页上")),
                  QStringLiteral("so the box says what a deletion does to an answer before the "
                                 "press: %1").arg(saidOf(inForm)));

            // The same question asked of the tab. What the tab knows is still worth
            // knowing -- the delete and split boxes read it to decide what to say
            // about a field's answer -- even though the open no longer warns about
            // it: the warning (§94's) said this program draws no field contents
            // anywhere, and F7-C drew them, so the sentence is gone and only the
            // fact stays.
            {
                // Started from a document this test knows is on disk. It used to
                // read `tab.documentPath()`, and that path was a scratch copy an
                // earlier block had left the tab on -- a copy that really is gone
                // by now, because since F7 the reader's handle shares DELETE
                // (docs/pdfium-plan.md R7) and the scratch folder's delete succeeds
                // where it used to fail. Under the old handle the dead path stayed
                // openable by accident, and a test that leans on that is one that
                // reports the wrong thing the moment the file lock changes.
                const QString live = directory + QStringLiteral("/many-pages.pdf");
                check(tab.open(live) == Document::Status::Ready,
                      "setup: a book with no form is open in the tab");
                check(tab.open(withForm) == Document::Status::Ready,
                      "the form-bearing copy opens in the tab");
                check(tab.hasFormFields(),
                      "and the tab knows there are form fields in it, which is what the status "
                      "line says on the way past");
                check(tab.open(live) == Document::Status::Ready,
                      "the book without a form opens again");
                check(!tab.hasFormFields(), "and the tab does not claim a form that is not there");
            }

            DocumentEditor plain;
            openBook(plain);
            check(!plain.hasFormFields(), "the same book with no form answers no");
            DeletePagesDialog inPlain(&tab, 0, &plain);
            check(!saidOf(inPlain).contains(QStringLiteral("表单字段")),
                  QStringLiteral("and the sentence keeps out of a box that has nothing to say about "
                                 "forms: %1").arg(saidOf(inPlain)));
        }

        // 移动 with nothing named AND no anchor number: the pages are the more basic question.
        // Getting there is itself a reading: the box opens on 当前页, which always names one
        // page, so the "no pages named" branch only exists behind the 指定页码 choice -- a box
        // left as it opened asks the anchor question first, and that is the right order.
        {
            DocumentEditor editor;
            openBook(editor);
            MovePagesDialog box(&tab, 0, &editor);
            pick(box, QStringLiteral("指定页码"));
            pick(box, QStringLiteral("指定页之后"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说出要移动哪几页")),
                  QStringLiteral("no pages named is said before no anchor: %1").arg(said));
            check(!said.contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("and the anchor question waits its turn: %1").arg(said));
            pick(box, QStringLiteral("当前页（第 1 页）"));
            check(saidOf(box).contains(QStringLiteral("要说移到第几页之后")),
                  QStringLiteral("name the page another way and the anchor is the only thing "
                                 "left missing: %1")
                      .arg(saidOf(box)));
        }

        // 水印 with neither words nor pages: the text is the first question the box asks, and
        // the page question is not raised while the marks themselves are still unsaid.
        {
            DocumentEditor editor;
            openBook(editor);
            WatermarkDialog box(&tab, &editor);
            pick(box, QStringLiteral("指定页码"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("words first: %1").arg(said));
            check(!said.contains(QStringLiteral("还没有说出要盖哪几页")),
                  QStringLiteral("and the page question is not asked yet: %1").arg(said));
        }

        // 水印 with no words but a page range typed, on a document that carries imported pages:
        // the imported-pages aside is about pages that will not be inked, so it stays out of a
        // refusal that already says nothing was named. (It used to be appended here, and the
        // reason this is a pin rather than a comment is that the aside is gated on the pages,
        // not on the answer the box just gave.)
        {
            DocumentEditor editor;
            openBook(editor);
            check(editor.mergeFile(donor, {0}, DocumentEditor::MoveTarget::Back, -1, &error),
                  QStringLiteral("setup: one imported page: %1").arg(error));
            WatermarkDialog box(&tab, &editor);
            QLineEdit *range = rangeOf(box);
            check(range != nullptr, "setup: the box that takes the numbers");
            range->setText(QStringLiteral("3"));
            const QString said = saidOf(box);
            check(said.contains(QStringLiteral("还没有说要盖什么字")),
                  QStringLiteral("a typed range with no words is still no words: %1").arg(said));
            check(!said.contains(QStringLiteral("前面合并进来的那")),
                  QStringLiteral("and nothing is explained about pages that were never asked to "
                                 "be inked: %1")
                      .arg(said));
        }
    }

    // ---- 编辑书签: the box that shows a tree as a list, and greys what would change nothing ----
    //
    // The eighth recorder, and the only one whose subject is a structure rather than a number or a
    // range. Three things are checked here that no model test can see. One is what the box opens on:
    // the sidebar's own tree, so the reader edits the list they can see rather than an empty grid.
    // Two is the rule the model does *not* have: pressing 确定 on a box nobody touched would record a
    // request, dirty the tab and hand out an undo row that puts the file back exactly as it was --
    // setOutline() only refuses an identical *pending* tree (§69), and nothing but this box can see
    // the document's own. Three is §70's owed sentence: a bookmark naming a page an earlier 删页 took
    // out is said out loud *before* the press and still let through, because the model's answer to it
    // is a heading, which is the same answer it gives for a page lost after the ask.
    {
        const QString book = directory + QStringLiteral("/many-pages.pdf");

        // A tree the shape of a real chapter list: five bookmarks, three depths, one of them a
        // heading that names no page.
        const QVector<OutlineRow> sample = {
            {QStringLiteral("第一章"), 1, 0, QPointF()},
            {QStringLiteral("1.1"), 2, 2, QPointF()},
            {QStringLiteral("1.1.1"), 3, 4, QPointF()},
            {QStringLiteral("1.2"), 2, 6, QPointF()},
            {QStringLiteral("第二章"), 1, -1, QPointF()},
        };
        const Outline tree = outlineFromRows(sample);

        const auto gridOf = [](BookmarkDialog &box) { return box.findChild<QTableWidget *>(); };
        const auto okayOf = [](BookmarkDialog &box) {
            auto *buttons = box.findChild<QDialogButtonBox *>();
            return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        };
        const auto saidBy = [](BookmarkDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };
        const auto button = [](BookmarkDialog &box, const QString &name) {
            const auto buttons = box.findChildren<QPushButton *>();
            for (QPushButton *one : buttons) {
                if (one->text() == name)
                    return one;
            }
            return static_cast<QPushButton *>(nullptr);
        };
        const auto orderOf = [](BookmarkDialog &box) {
            QStringList titles;
            const QVector<OutlineRow> rows = box.rows();
            for (const OutlineRow &row : rows)
                titles << QStringLiteral("%1/%2/%3").arg(row.title).arg(row.level).arg(row.page);
            return titles.join(QStringLiteral(", "));
        };
        DocumentEditor editor;
        QString error;
        check(editor.open(book, QString(), &error),
              QStringLiteral("setup: a 40-page document opens: %1").arg(error));

        {
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            check(grid != nullptr, "the box shows the bookmarks as a grid");
            check(grid && grid->rowCount() == 5,
                  QStringLiteral("it opens on the tree the sidebar is showing, not on an empty "
                                 "list (%1 rows)")
                      .arg(grid ? grid->rowCount() : -1));
            // 1-based in the cell: this is the number the reader counts on screen (§70), so a page
            // index of 0 has to read as 第 1 页 or every number in the box is one off from the bar.
            check(grid && grid->item(0, 2) && grid->item(0, 2)->text() == QStringLiteral("1"),
                  QStringLiteral("and the page column is the numbering on the screen: %1")
                      .arg(grid && grid->item(0, 2) ? grid->item(0, 2)->text() : QStringLiteral("(none)")));
            check(grid && grid->item(4, 2) && grid->item(4, 2)->text().isEmpty(),
                  "a bookmark with no page opens with an empty cell rather than a 0 or a 1");
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(),
                  "an untouched box is off: pressing it would record a change that is not one");
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("一模一样")) && said.contains(QStringLiteral("不会多改什么")),
                  QStringLiteral("and it says that in those words: %1").arg(said));
            check(said.contains(QStringLiteral("其中 1 条不指向任何一页")) == false,
                  QStringLiteral("the untouched box says nothing about headings, because the "
                                 "sentence about a change belongs to a change: %1")
                      .arg(said));
        }

        {
            // One keystroke and the box becomes a request: the count, the heading aside, and the
            // family's ending all have to arrive together, or the reader agrees to a sentence that
            // stopped being true when they typed.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            check(grid && grid->item(0, 0), "setup: the first row has a name cell");
            grid->item(0, 0)->setText(QStringLiteral("第一章 改名"));
            QPushButton *okay = okayOf(box);
            const QString renamed = saidBy(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("renaming one row is a change, and the button knows: %1")
                      .arg(renamed));
            check(okay && okay->text() == QStringLiteral("写这 5 条书签"),
                  QStringLiteral("and the button counts them: %1")
                      .arg(okay ? okay->text() : QStringLiteral("(none)")));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("会有 5 条书签")) && said.contains(QStringLiteral("原来那份文件")),
                  QStringLiteral("the summary repeats the count and keeps the save promise: %1")
                      .arg(said));
            check(said.contains(QStringLiteral("其中 1 条不指向任何一页")),
                  QStringLiteral("and says which of the five is a heading: %1").arg(said));
        }

        {
            // §70's owed sentence, on the only path that can produce it: the page was taken out by an
            // edit that has not been saved, so the reader's number still names a page on screen and
            // nothing at all in the file a save would write.
            DocumentEditor deleted;
            QString e;
            check(deleted.open(book, QString(), &e), QStringLiteral("setup: opens again: %1").arg(e));
            check(deleted.deletePages({2}, &e), QStringLiteral("setup: the third page is pending-deleted: %1").arg(e));
            const QVector<OutlineRow> naming = {
                {QStringLiteral("还在一起"), 1, 0, QPointF()},
                {QStringLiteral("被删那页"), 1, 2, QPointF()},
            };
            BookmarkDialog box(&tab, &deleted, outlineFromRows(naming));
            QTableWidget *grid = gridOf(box);
            // A change first, because an untouched box is not a request at all (that is the rule two
            // blocks above pin): what is being asked here is whether the box tells the reader about
            // the page that is already gone *while* they are making an edit that will write it.
            grid->item(0, 0)->setText(QStringLiteral("还在一起 改名"));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled(),
                  QStringLiteral("renaming is a change, so the press is allowed through: %1")
                      .arg(saidBy(box)));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("在前面的修改里已经删掉了")),
                  QStringLiteral("and the box says which kind of bookmark they are about to write: %1")
                      .arg(said));
            check(said.contains(QStringLiteral("有 1 条")),
                  QStringLiteral("counted, not vaguely mentioned: %1").arg(said));
            check(box.rows().at(1).page == 2,
                  "the row keeps the number the reader typed rather than being renumbered here");
        }

        {
            // Sharing a page is not the same as losing one, and this is the case that was missed
            // until a real book was opened in the box: 184 bookmarks over fewer pages, with several
            // rows on the same page. Asking "how many of my rows are still on a page" of a function
            // that counts *distinct pages* subtracts the duplicates and reports them as deleted --
            // which is how the box told a reader who had deleted nothing that 77 bookmarks pointed at
            // pages that were gone (docs/store-submission.md §72).
            DocumentEditor shared;
            QString e;
            check(shared.open(book, QString(), &e), QStringLiteral("setup: opens for the shared page: %1").arg(e));
            const QVector<OutlineRow> threeOnTwo = {
                {QStringLiteral("One"), 1, 5, QPointF()},
                {QStringLiteral("Two"), 1, 5, QPointF()},
                {QStringLiteral("Three"), 1, 6, QPointF()},
            };
            BookmarkDialog box(&tab, &shared, outlineFromRows(threeOnTwo));
            box.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("One 改名"));
            const QString said = saidBy(box);
            check(!said.contains(QStringLiteral("在前面的修改里已经删掉了")),
                  QStringLiteral("three bookmarks on two pages, nothing deleted, so nothing is "
                                 "reported as taken out: %1")
                      .arg(said));
            QPushButton *okay = okayOf(box);
            check(okay && okay->isEnabled(), "and the change itself is still a change");

            // The positive half: the same box, with a page genuinely pending-deleted behind it, says
            // the sentence about the one row that lost its page -- so the check above is not green
            // because the sentence can never appear.
            check(shared.deletePages({6}, &e), QStringLiteral("setup: page 7 is now pending-deleted: %1").arg(e));
            box.findChild<QTableWidget *>()->item(1, 0)->setText(QStringLiteral("Two 改名"));
            const QString gone = saidBy(box);
            check(gone.contains(QStringLiteral("在前面的修改里已经删掉了")) && gone.contains(QStringLiteral("有 1 条")),
                  QStringLiteral("counted as rows, not as pages: %1").arg(gone));
        }

        {
            // The two ways a page cell can be wrong, and that both stop the press.
            DocumentEditor strict;
            QString e;
            check(strict.open(book, QString(), &e), QStringLiteral("setup: opens for the bounds: %1").arg(e));
            const QVector<OutlineRow> one = {
                {QStringLiteral("Only"), 1, 5, QPointF()},
            };
            BookmarkDialog box(&tab, &strict, outlineFromRows(one));
            QTableWidget *grid = gridOf(box);
            grid->item(0, 2)->setText(QStringLiteral("999"));
            QPushButton *okay = okayOf(box);
            const QString said = saidBy(box);
            check(okay && !okay->isEnabled(), "a page the file never had is refused before the press");
            check(said.contains(QStringLiteral("第 1 行")) && said.contains(QStringLiteral("40 页")),
                  QStringLiteral("naming the row and the file's own page count: %1").arg(said));
            grid->item(0, 2)->setText(QStringLiteral("第三" /* not a number */));
            const QString again = saidBy(box);
            check(again.contains(QStringLiteral("不是数字")) && again.contains(QStringLiteral("留空")),
                  QStringLiteral("and a cell that is not a number is told apart from a blank one, "
                                 "with the way to mean no page: %1")
                      .arg(again));
        }

        {
            // The 层级 column is a text cell, and that swap is the whole of §74: the first version put a
            // spin box *inside* the cell, and §72 read this box off the real window and found that cell
            // empty in the UIA tree -- a number on the screen and nothing for a screen reader. A text
            // cell is reachable, but it can now hold "0", "深" or nothing at all, so the bound the spin
            // enforced silently is said out loud before the press instead. Both halves are pinned here:
            // the shape the accessibility tree can reach, and the refusal that replaced the clamp.
            DocumentEditor levels;
            QString e;
            check(levels.open(book, QString(), &e),
                  QStringLiteral("setup: opens for the level column: %1").arg(e));
            const QVector<OutlineRow> nested = {
                {QStringLiteral("章"), 1, 0, QPointF()},
                {QStringLiteral("节"), 2, 2, QPointF()},
            };
            BookmarkDialog box(&tab, &levels, outlineFromRows(nested));
            QTableWidget *grid = gridOf(box);
            check(grid->item(1, 1) && grid->item(1, 1)->text() == QStringLiteral("2"),
                  QStringLiteral("the level is the cell's own text, which is what the tree reads: %1")
                      .arg(grid->item(1, 1) ? grid->item(1, 1)->text() : QStringLiteral("(none)")));
            check(grid->cellWidget(0, 1) == nullptr && grid->cellWidget(1, 1) == nullptr,
                  "and no widget sits in those cells, because a cell widget is absent from UIA (§72)");

            QPushButton *okay = okayOf(box);
            // A real request first. Without this the button would be grey for the *other* reason -- an
            // untouched box (§71) -- and a refusal asserted as "grey" would be green whether or not the
            // level check exists at all.
            grid->item(0, 0)->setText(QStringLiteral("章 改名"));
            check(okay && okay->isEnabled(), "setup: renaming made it a request");

            grid->item(0, 1)->setText(QStringLiteral("0"));
            QString said = saidBy(box);
            check(okay && !okay->isEnabled(),
                  QStringLiteral("a typed 0 is refused before the press, not clamped to 1: %1").arg(said));
            check(said.contains(QStringLiteral("第 1 行的层级")) && said.contains(QStringLiteral("1 或更大")),
                  QStringLiteral("naming the row, the column and what a level looks like: %1").arg(said));
            check(box.rows().at(0).level == 0,
                  "and the list keeps the number that was typed, so nothing is quietly re-nested");

            grid->item(0, 1)->setText(QString());
            said = saidBy(box);
            check(!okay->isEnabled() && said.contains(QStringLiteral("第 1 行的层级")),
                  QStringLiteral("a blank level is the same refusal, because a blank is not a level: %1")
                      .arg(said));

            grid->item(0, 1)->setText(QStringLiteral("深"));
            said = saidBy(box);
            check(!said.contains(QStringLiteral("不是数字")),
                  QStringLiteral("and it does not borrow the page column's non-number sentence, whose "
                                 "second half tells the reader to leave the cell empty -- here empty is "
                                 "not an answer: %1").arg(said));

            grid->item(0, 1)->setText(QStringLiteral("3"));
            said = saidBy(box);
            check(okay->isEnabled(), QStringLiteral("a level of 3 is a request again: %1").arg(said));
            check(box.rows().at(0).level == 3, "and that is the number the model gets");

            grid->item(0, 1)->setText(QStringLiteral("0"));
            grid->item(0, 2)->setText(QStringLiteral("999"));
            said = saidBy(box);
            check(said.contains(QStringLiteral("层级")) && !said.contains(QStringLiteral("不在这份文件里")),
                  QStringLiteral("a row broken in two columns is asked about left first: %1").arg(said));

            {
                // Writing back the digits a row already had is not an edit: §71's "untouched box" rule
                // compares the parsed number, so a cell re-typed as "1" has to land back on the level
                // it opened with rather than reading as a change.
                grid->item(0, 0)->setText(QStringLiteral("章"));
                grid->item(0, 1)->setText(QStringLiteral("1"));
                grid->item(0, 2)->setText(QStringLiteral("1"));
                grid->item(1, 1)->setText(QStringLiteral("2"));
                const QString back = saidBy(box);
                check(!okay->isEnabled(),
                      QStringLiteral("back to exactly what it opened with, so the press is off again: %1")
                          .arg(back));
                check(back.contains(QStringLiteral("一模一样")),
                      QStringLiteral("and it says the untouched sentence, not a count of what would be "
                                     "written: %1").arg(back));
            }
        }

        {
            // Which message wins when two rows are broken: the topmost row. A box that answered by
            // *kind* would name row 5's page while the reader is looking at row 2's empty name.
            const QVector<OutlineRow> two = {
                {QStringLiteral("First"), 1, 0, QPointF()},
                {QString(), 1, 3, QPointF()},
                {QStringLiteral("Last"), 1, 999, QPointF()},
            };
            BookmarkDialog box(&tab, &editor, outlineFromRows(two));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("第 2 行还没有名字")),
                  QStringLiteral("the earlier row is the one it asks about: %1").arg(said));
            check(!said.contains(QStringLiteral("第 3 行")),
                  QStringLiteral("and it does not talk about the third row yet: %1").arg(said));
        }

        {
            // 加一条 puts the new row beside the selected one -- after its whole subtree, which is
            // the one question outlineSubtreeEnd() answers. A row inserted directly under a chapter
            // would become its child and rearrange a tree the reader did not touch.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(0, 0);
            QPushButton *add = button(box, QStringLiteral("加一条"));
            check(add && add->isEnabled(), "a box with rows in it can take one more");
            add->click();
            const QVector<OutlineRow> after = box.rows();
            check(after.size() == 6, QStringLiteral("one row was added, not one child: %1")
                                          .arg(after.size()));
            check(after.at(4).title == QStringLiteral("新书签") && after.at(4).level == 1,
                  QStringLiteral("and it landed after 第一章's whole block, at the top level (%1)")
                      .arg(orderOf(box)));
            check(after.at(5).title == QStringLiteral("第二章") && after.at(5).page == -1,
                  QStringLiteral("with the file's own last bookmark still last and still without a "
                                 "page (%1)")
                      .arg(orderOf(box)));
        }

        {
            // 删一条 takes the subtree with it. The children have nowhere else to go: left behind,
            // outlineFromRows would hang them on whatever row precedes them, which is a different
            // document than the one the reader was looking at.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(0, 0);
            QPushButton *remove = button(box, QStringLiteral("删一条"));
            check(remove && remove->isEnabled(), "a selected row can be taken out");
            remove->click();
            check(box.rows().size() == 1,
                  QStringLiteral("taking the chapter out took its three descendants with it: %1")
                      .arg(orderOf(box)));
        }

        {
            // 上移/下移 move a block past a sibling and are off where there is no sibling to pass.
            BookmarkDialog box(&tab, &editor, tree);
            QTableWidget *grid = gridOf(box);
            grid->setCurrentCell(1, 0); // 1.1, a level-2 row with a child and one sibling (1.2)
            QPushButton *up = button(box, QStringLiteral("上移"));
            QPushButton *down = button(box, QStringLiteral("下移"));
            check(up && !up->isEnabled(),
                  "a first child cannot move above its parent, and the button says so");
            check(down && down->isEnabled(), "but it has a sibling below to swap with");
            down->click();
            check(orderOf(box) == QStringLiteral(
                      "第一章/1/0, 1.2/2/6, 1.1/2/2, 1.1.1/3/4, 第二章/1/-1"),
                  QStringLiteral("and the swap carried the child along with its parent: %1")
                      .arg(orderOf(box)));
        }

        {
            // Opening on what is already pending, and the difference between "same as the file" and
            // "same as the last request": both grey the button, and they have to say different things,
            // because the first is nothing to undo and the second is an edit that is already recorded.
            DocumentEditor pending;
            QString e;
            check(pending.open(book, QString(), &e), QStringLiteral("setup: opens for the pending tree: %1").arg(e));
            check(pending.setOutline(sample, &e), QStringLiteral("setup: a tree is recorded: %1").arg(e));
            // The file's own tree, deliberately a different size, so that "it opened on the recorded
            // list" and "it opened on the bookmarks in the file" cannot both be true of one reading.
            const QVector<OutlineRow> asInFile = {
                {QStringLiteral("旧封面"), 1, 0, QPointF()},
                {QStringLiteral("旧版权"), 1, 1, QPointF()},
            };
            BookmarkDialog box(&tab, &pending, outlineFromRows(asInFile));
            QTableWidget *grid = gridOf(box);
            check(grid && grid->rowCount() == 5,
                  QStringLiteral("the box opens on the recorded tree, not on the file's own bookmarks "
                                 "(%1 rows)")
                      .arg(grid ? grid->rowCount() : -1));
            QPushButton *okay = okayOf(box);
            check(okay && !okay->isEnabled(), "and untouched, it is still not a change");
            grid->item(0, 0)->setText(QStringLiteral("第一章 又改"));
            const QString said = saidBy(box);
            check(said.contains(QStringLiteral("这会把前面记下的那一条")),
                  QStringLiteral("now it says which list is being replaced: %1").arg(said));
        }
    }

    // ---- the box says what a save will cost the file's own bookmarks (§80) ----
    //
    // §75 measured that every bookmark this program writes comes out as /Fit, so a document whose
    // bookmarks name a place inside a page loses that layer on a save -- and nothing on this screen
    // can show it, because the tree being edited holds a title, a level and a page number and no
    // position at all. §75's third route is the one taken: say it before the press. Both halves are
    // pinned -- it is said for a file that carries positions, and *not* said for one that does not,
    // for §77's reason that a warning about a loss which cannot happen here trains the reader to
    // ignore the warnings that can.
    //
    // The sample is hand-built for the same reason §75's was: our own writer cannot produce a
    // bookmark with a position in it, so no file written by this program ever reaches this branch.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "setup: a temporary folder for the bookmark sample");
        const QString sample = dir.filePath(QStringLiteral("positional-bookmarks.pdf"));
        bool built = false;
        QString why;
        try {
            QPDF qpdf;
            qpdf.processFile((directory + QStringLiteral("/many-pages.pdf")).toUtf8().constData());
            const std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(qpdf).getAllPages();
            auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };
            QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
            root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
            root = qpdf.makeIndirectObject(root);
            auto item = [&qpdf](const std::string &title, QPDFObjectHandle parent) {
                QPDFObjectHandle handle = QPDFObjectHandle::newDictionary();
                handle.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
                handle.replaceKey("/Parent", parent);
                return qpdf.makeIndirectObject(handle);
            };
            auto addDest = [](QPDFObjectHandle handle, std::vector<QPDFObjectHandle> parts) {
                QPDFObjectHandle dest = QPDFObjectHandle::newArray();
                for (QPDFObjectHandle &part : parts)
                    dest.appendItem(part);
                handle.replaceKey("/Dest", dest);
            };
            const QPDFObjectHandle xyz = item("Xyz spot", root);
            addDest(xyz, { pageAt(1), QPDFObjectHandle::newName("/XYZ"),
                           QPDFObjectHandle::newReal(0), QPDFObjectHandle::newReal(700),
                           QPDFObjectHandle::newReal(0) });
            const QPDFObjectHandle fitHorizontal = item("FitH spot", root);
            addDest(fitHorizontal, { pageAt(4), QPDFObjectHandle::newName("/FitH"),
                                     QPDFObjectHandle::newReal(650) });
            const QPDFObjectHandle fit = item("Fit page", root);
            addDest(fit, { pageAt(7), QPDFObjectHandle::newName("/Fit") });
            const QPDFObjectHandle heading = item("Heading", root);
            std::vector<QPDFObjectHandle> row = { xyz, fitHorizontal, fit, heading };
            for (size_t i = 0; i < row.size(); ++i) {
                if (i > 0)
                    row[i].replaceKey("/Prev", row[i - 1]);
                if (i + 1 < row.size())
                    row[i].replaceKey("/Next", row[i + 1]);
            }
            root.replaceKey("/First", row.front());
            root.replaceKey("/Last", row.back());
            root.replaceKey("/Count", QPDFObjectHandle::newInteger(int(row.size())));
            qpdf.getRoot().replaceKey("/Outlines", root);
            QPDFWriter writer(qpdf, sample.toUtf8().constData());
            writer.write();
            built = true;
        } catch (const std::exception &e) {
            why = QString::fromUtf8(e.what());
        }
        check(built,
              QStringLiteral("setup: a file whose bookmarks name a spot inside a page: %1").arg(why));

        const auto saidByBox = [](QDialog &box) {
            const auto labels = box.findChildren<QLabel *>();
            for (QLabel *label : labels) {
                if (label->text().contains(QStringLiteral("原来那份文件")))
                    return label->text();
            }
            return QString();
        };

        DocumentEditor carrying;
        QString e;
        check(carrying.open(sample, QString(), &e),
              QStringLiteral("setup: the positional file opens for the editor: %1").arg(e));
        check(carrying.sourcePositionalBookmarks() == 2,
              QStringLiteral("and the editor counts two of its four bookmarks as carrying a "
                             "position: %1").arg(carrying.sourcePositionalBookmarks()));

        const QVector<OutlineRow> one = { { QStringLiteral("改一下"), 1, 0, QPointF() } };
        BookmarkDialog box(&tab, &carrying, outlineFromRows(one));
        // A real request first: the clause sits with the other "what pressing will do" sentences, and
        // an untouched box answers 一模一样 instead (§71) -- without this the check below could be
        // reading a summary that never reached the branch it claims to test.
        box.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("改一下名字"));
        const QString said = saidByBox(box);
        // §99 changed what this number counts, and the test has to follow it. The file carries two
        // positional bookmarks; a request that replaces the whole tree with one row of our own
        // matches none of them, so only the row standing in slot 0 -- where the file had /XYZ -- is
        // reported. Saying 2 here would be §80's old claim ("这一版一条都留不住"), which stopped
        // being true the moment the destinations started riding along with the rows.
        QVector<OutlineRow> justRenamed = one;
        justRenamed[0].title = QStringLiteral("改一下名字");
        check(carrying.positionalAtRiskFor(justRenamed) == 2,
              QStringLiteral("setup: replacing the whole tree with one new row leaves both of the "
                             "file's positions behind: %1")
                  .arg(carrying.positionalAtRiskFor(justRenamed)));
        check(said.contains(QStringLiteral("其中 2 条书签指着页面内的某个位置")),
              QStringLiteral("the box says, before the press, how many positions THIS request "
                             "will leave behind: %1")
                  .arg(said));
        check(said.contains(QStringLiteral("整页适配")) && said.contains(QStringLiteral("对不上")),
              QStringLiteral("and says what those become, and why this time it is the request and "
                             "not the version: %1")
                  .arg(said));

        // The half §99 bought, and the reason the warning is not decoration: ask for the file's own
        // tree, change something that carries no position, and nothing is at risk -- so the sentence
        // is not said about a loss that is not happening (§77's rule, now with a live case where it
        // applies). One row's title is edited, so this is still a real request and not the untouched
        // 一模一样 answer §71 pins.
        const QVector<OutlineRow> asTheFileHas = {
            { QStringLiteral("Xyz spot"), 1, 1, QPointF() },
            { QStringLiteral("FitH spot"), 1, 4, QPointF() },
            { QStringLiteral("Fit page"), 1, 7, QPointF() },
            { QStringLiteral("Heading"), 1, -1, QPointF() },
        };
        BookmarkDialog carried(&tab, &carrying, outlineFromRows(asTheFileHas));
        carried.findChild<QTableWidget *>()->item(3, 0)->setText(QStringLiteral("Heading, renamed"));
        const QString carriedSaid = saidByBox(carried);
        QVector<OutlineRow> renamedHeading = asTheFileHas;
        renamedHeading[3].title = QStringLiteral("Heading, renamed");
        check(carrying.positionalAtRiskFor(renamedHeading) == 0,
              QStringLiteral("setup: renaming a bookmark that had no position puts none at risk: %1")
                  .arg(carrying.positionalAtRiskFor(renamedHeading)));
        check(!carriedSaid.contains(QStringLiteral("页面内的某个位置")),
              QStringLiteral("and nothing is said about positions, though this file has two of "
                             "them: %1")
                  .arg(carriedSaid));

        // The negative half, same box, a document with nothing at risk.
        DocumentEditor plain;
        check(plain.open(path, QString(), &e),
              QStringLiteral("setup: a document with no bookmarks at all opens: %1").arg(e));
        check(plain.sourcePositionalBookmarks() == 0,
              QStringLiteral("and the count there is zero: %1").arg(plain.sourcePositionalBookmarks()));
        BookmarkDialog quiet(&tab, &plain, outlineFromRows(one));
        quiet.findChild<QTableWidget *>()->item(0, 0)->setText(QStringLiteral("改一下名字"));
        const QString kept = saidByBox(quiet);
        check(!kept.contains(QStringLiteral("页面内的某个位置")),
              QStringLiteral("nothing is said about positions for a file that has none: %1").arg(kept));
    }

    //
    // §13 wrote this down from a real window: after a zoom-mode change the pages in
    // front were 5 and 6 while the bar still read "2 / 40", and pressing a paging key
    // once made the two agree. The reason is that only scrolling ever works out which
    // page is current -- see scrollContentsBy -- and the changes that re-lay the pages
    // out can move a page boundary across the middle of the viewport without moving the
    // scrollbar by a pixel. No scroll event, so nothing re-derived the number, and what
    // the reader was told named a page that was not in front of them. The same number
    // feeds the page box and the six 文档 dialogs' "当前页（第 %1 页）", so it is not
    // only the corner of the window that was wrong.
    //
    // The arithmetic below is worked out by hand the way layout_smoke's is: the
    // fixture's pages are 595x842 points and the gap is 12 pixels (kSpacing), so at 100%
    // page k starts at 12 + k * 854 content pixels, and the middle of the window is the
    // scroll position plus half a viewport.
    {
        check(tab.open(directory + QStringLiteral("/many-pages.pdf")) == Document::Status::Ready,
              "the forty-page fixture opens");
        check(tab.pageCount() == 40,
              QStringLiteral("forty pages, the document the note was read on (got %1)")
                  .arg(tab.pageCount()));

        // Which page is in front, worked out from the rectangles the view hands out
        // rather than from the answer it reports: a check that asked the view both
        // questions could be answered by a view that was wrong about both. The rule is
        // the one a reader uses without noticing -- the page the middle of the window is
        // on -- and the hand-worked numbers below are what pin it, since this lambda and
        // PageLayout::pageAtViewportCenter could both be quietly wrong.
        const auto pageOnScreen = [view] {
            const double middle = view->viewport()->height() / 2.0;
            const int count = view->pageCount();
            for (int page = 0; page < count; ++page) {
                const QRectF rect = view->pageRectInViewport(page);
                if (!rect.isNull() && middle < rect.bottom())
                    return page;
            }
            return count - 1;
        };

        // The status bar is not polled; it moves when the view says so, through
        // DocumentTab's pageChanged. A fix that settled the internal page and stayed
        // quiet would leave the number where it was, so the announcement is what is
        // checked, not just the field.
        int announcements = 0;
        int lastAnnounced = -1;
        const QMetaObject::Connection listening =
            QObject::connect(view, &PageView::currentPageChanged, view, [&](int page) {
                ++announcements;
                lastAnnounced = page;
            });

        // Back to one place every case starts from: 100%, one page per row, page 11's top
        // edge at the top of the window. A case that began anywhere else would have to
        // argue about where it began as well as about what changed.
        const auto startAtPageEleven = [&](const QString &forCase) {
            view->setRotation(0);
            view->setLayoutMode(PageLayout::Mode::Continuous);
            view->setZoomMode(ZoomController::Mode::Percent);
            view->setZoom(1.0);
            view->goToPage(10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(view->verticalScrollBar()->value() == 8552,
                  QStringLiteral("%1 starts with page 11's top at the top of the window "
                                 "(scroll is %2)")
                      .arg(forCase)
                      .arg(view->verticalScrollBar()->value()));
            check(view->currentPage() == 10 && pageOnScreen() == 10,
                  QStringLiteral("%1 begins in agreement: bar %2, screen %3")
                      .arg(forCase)
                      .arg(view->currentPage() + 1)
                      .arg(pageOnScreen() + 1));
            announcements = 0;
            lastAnnounced = -1;
        };

        // The promise, said once and then asked after every one of the changes that can
        // break it.
        const auto agreesWithTheScreen = [&](const QString &after) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int shown = pageOnScreen();
            check(shown != 10,
                  QStringLiteral("%1 moved a page boundary across the middle of the window, "
                                 "so there was something to get wrong (still page %2)")
                      .arg(after)
                      .arg(shown + 1));
            check(view->currentPage() == shown,
                  QStringLiteral("%1 names the page that is in front (bar says page %2, the "
                                 "screen shows page %3)")
                      .arg(after)
                      .arg(view->currentPage() + 1)
                      .arg(shown + 1));
            check(announcements > 0 && lastAnnounced == shown,
                  QStringLiteral("%1 says it out loud, which is the only way the bar and the "
                                 "page box can hear (announcements %2, last %3)")
                      .arg(after)
                      .arg(announcements)
                      .arg(lastAnnounced + 1));
        };

        // ---- scrolling, which is the path that already worked ----
        //
        // Here so that the one place that decides the answer has a check over every way of
        // getting there: the older test of this asks how many times the view said a page
        // changed, which a view that said the wrong one would pass just as well.
        {
            startAtPageEleven(QStringLiteral("scrolling to page 2"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            check(pageOnScreen() == 1,
                  QStringLiteral("page 2 is what the middle of the window is on now (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("scrolling to page 2"));
        }

        // ---- a factor, chosen by hand or typed into the box ----
        //
        // At 400% a page occupies 3368 pixels and starts at 12 + k * 3380. The middle of
        // the window is between 8552 and 9394 for any window under 1684 px tall, and that
        // whole range is inside page 3 (6772 .. 10140).
        {
            startAtPageEleven(QStringLiteral("zooming to 400%"));
            const int scrolledTo = view->verticalScrollBar()->value();
            view->setZoom(4.0);
            check(view->verticalScrollBar()->value() == scrolledTo,
                  "and the zoom did not scroll anywhere -- which is the whole reason: there "
                  "was no scroll event to re-derive the page from");
            check(pageOnScreen() == 2,
                  QStringLiteral("page 3 is what is in front at four times the size (says %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("zooming to 400%"));
        }

        // ---- a fit mode, which is what the note was about ----
        //
        // 适应整页 shrinks the factor until a whole page fits the window's height, so the
        // pages get shorter and one of them ends up higher than the middle of the window
        // than it was. No hand number here: the factor depends on the window's height,
        // which the test reads rather than assumes.
        {
            startAtPageEleven(QStringLiteral("适应整页"));
            view->setZoomMode(ZoomController::Mode::FitPage);
            agreesWithTheScreen(QStringLiteral("适应整页"));
        }

        // ---- the same fit mode in a window wide enough for a page ----
        //
        // This case exists because the one above does not, on its own, cover the mode
        // change. Fitting a page makes the pages narrower as well as shorter, and in a
        // window that was too narrow for a page that takes the sideways scrollbar away --
        // which resizes the viewport a moment later, and that resize recomputes the fit and
        // re-takes the number. Cut the call site in setZoomMode() and the case above stays
        // green: the number is settled by the second pass, not the first.
        //
        // Wide enough for a page to hang nothing sideways, there is no second pass: the mode
        // change is the only event, so this is the case that asks it.
        {
            const QSize sizeBefore = tab.size();
            tab.resize(1400, 620);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            startAtPageEleven(QStringLiteral("适应整页 in a wide window"));
            check(view->horizontalScrollBar()->maximum() == 0,
                  QStringLiteral("the window is wide enough that nothing hangs sideways at "
                                 "100% either (scrollbar maximum %1)")
                      .arg(view->horizontalScrollBar()->maximum()));

            const int heightBefore = view->viewport()->height();
            view->setZoomMode(ZoomController::Mode::FitPage);
            check(view->viewport()->height() == heightBefore,
                  QStringLiteral("and the fitted pages are narrower still, so no scrollbar "
                                 "came or went and no resize arrives afterwards to do it "
                                 "instead (viewport height %1 -> %2)")
                      .arg(heightBefore)
                      .arg(view->viewport()->height()));
            agreesWithTheScreen(QStringLiteral("适应整页 in a wide window"));
            tab.resize(sizeBefore);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }

        // ---- two pages per row ----
        //
        // Twenty spreads of 854 pixels each, so the middle of the window (8552 .. 9394)
        // lands in spread 11, which is pages 21 and 22 -- and the layout's rule names the
        // left one of a spread first.
        {
            startAtPageEleven(QStringLiteral("turning on two-page mode"));
            view->setLayoutMode(PageLayout::Mode::TwoPage);
            check(pageOnScreen() == 20,
                  QStringLiteral("the spread in front is pages 21 and 22 (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("turning on two-page mode"));
        }

        // ---- a quarter turn ----
        //
        // The pages become 842 wide by 595 tall, so every page above the middle of the
        // window gets 247 pixels shorter and the middle reaches further into the
        // document. Which page exactly depends on the window's height, so this one is the
        // invariant alone -- with the check inside agreesWithTheScreen that a boundary
        // really did cross, which is what keeps it from passing by standing still.
        {
            startAtPageEleven(QStringLiteral("rotating"));
            view->setRotation(90);
            agreesWithTheScreen(QStringLiteral("rotating"));
        }

        // ---- a shorter window ----
        //
        // The clearest case of all, because nothing about the document changed at all: the
        // middle of the window moved up by half the difference in height. A taller window
        // makes the same point, but this screen is only so tall -- growing the window by
        // two page periods grew the viewport by one, which is why the measurement goes the
        // other way -- and a shorter window starts by parking page 11's top edge a known
        // ten pixels above the middle, so the distance to the boundary it has to cross is
        // fixed rather than a function of how tall the window happens to be.
        {
            startAtPageEleven(QStringLiteral("making the window shorter"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(8552 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 10,
                  QStringLiteral("the middle of the window is ten pixels inside page 11 "
                                 "(says page %1)")
                      .arg(pageOnScreen() + 1));

            const int heightBefore = view->viewport()->height();
            tab.resize(tab.width(), tab.height() - 200);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
            const int lost = heightBefore - view->viewport()->height();
            // The middle rose by half of that, so anything between 21 and 1752 pixels
            // leaves it inside page 10 -- the ask is 200, and however much of it the
            // window's own minimum size gave back, the answer is a page number rather
            // than a measurement of the platform.
            check(lost > 20 && lost < 1752,
                  QStringLiteral("the window lost %1 pixels of page, which is more than twice "
                                 "the ten pixels to spare and less than a whole page")
                      .arg(lost));
            check(pageOnScreen() == 9,
                  QStringLiteral("a shorter window reaches back over the edge the middle was "
                                 "inside (says page %1)")
                      .arg(pageOnScreen() + 1));
            agreesWithTheScreen(QStringLiteral("making the window shorter"));
        }

        // ---- opening a second document in the same tab ----
        //
        // The same fact, with the document changing instead of the window: which page the
        // middle of the window sits on is a property of the geometry, and the geometry
        // includes what is laid out. setPageSizes() answered it by writing page 1 down,
        // which is where a reader expects to start rather than where they end up -- the
        // scroll position is not reset for a new document, so a two-page file opened in a
        // tab that was showing page 2 of a forty-page one is showing page 2, and the bar
        // read "1 / 2" while it did.
        //
        // text.pdf's pages are the same 595x842 as the forty-page fixture's, which is what
        // puts the position below inside the new document rather than past its end -- a
        // position past the end would be clamped, the clamp would scroll, and the scrolling
        // path would have quietly fixed it. That is why the number is asked for before and
        // compared after: this case is the half of the situation that no scroll event ever
        // covered. Where a new document *should* start is a different question, and is
        // recorded as still open in docs/store-submission.md §25.
        {
            startAtPageEleven(QStringLiteral("opening a two-page document"));
            const int middle = view->viewport()->height() / 2;
            view->verticalScrollBar()->setValue(866 - middle + 10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            check(pageOnScreen() == 1,
                  QStringLiteral("the middle of the window is ten pixels inside page 2 of the "
                                 "forty-page fixture (says page %1)")
                      .arg(pageOnScreen() + 1));
            const int kept = view->verticalScrollBar()->value();

            check(tab.open(directory + QStringLiteral("/text.pdf")) == Document::Status::Ready,
                  "the two-page fixture opens in the same tab");
            check(tab.pageCount() == 2,
                  QStringLiteral("of two pages (got %1)").arg(tab.pageCount()));
            check(view->verticalScrollBar()->value() == kept,
                  QStringLiteral("and the new document is still showing the same content "
                                 "pixels as the old one (scroll %1 -> %2), so no scroll event "
                                 "happened -- which is the shape of this bug")
                      .arg(kept)
                      .arg(view->verticalScrollBar()->value()));
            agreesWithTheScreen(QStringLiteral("opening a two-page document"));
        }

        QObject::disconnect(listening);
    }

    // ---- what a save does to a name another tab of this program is already reading ----
    //
    // The last step of DocumentEditor::saveAs is one MoveFileExW per file with
    // MOVEFILE_REPLACE_EXISTING, and the name the save dialog hands back can perfectly well be a PDF
    // this very window has open in a different tab -- the editor refuses its own source file and any
    // page it merged in from (documenteditor.cpp:2201, :2215), and nothing refuses a tab. Two bad
    // endings are possible and which one happens has never been asked of a file: the move fails and
    // the reader is told the name is "locked by another program" while the program is us, or the move
    // succeeds and the other tab goes on showing bytes that no longer exist on disk. Printing both
    // answers here, before choosing a fix, is §58's rule -- a suspicion written as a run rather than
    // as a sentence.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "setup: a temporary folder for the overwrite measurement");
        const QString beingRead = dir.filePath(QStringLiteral("being-read.pdf"));
        const QString stagedFile = dir.filePath(QStringLiteral("staged-overwrite.pdf"));
        check(QFile::copy(path, beingRead),
              "setup: the name another tab will read is a copy in the scratch folder");
        check(QFile::copy(directory + QStringLiteral("/many-pages.pdf"), stagedFile),
              "setup: the file that will be moved onto it is the many-page one");

        DocumentTab other;
        other.resize(700, 500);
        other.show();
        check(other.open(beingRead) == Document::Status::Ready,
              QStringLiteral("setup: another tab has that name open: %1").arg(other.lastError()));
        PageView *otherView = viewOf(other);
        check(waitFor([otherView] { return otherView->hasImageFor(0); }),
              "setup: and its first page is drawn, so the tab is idle the way a reader's would be");
        const int pagesWhileOpen = other.pageCount();

        const std::wstring from = QDir::toNativeSeparators(stagedFile).toStdWString();
        const std::wstring to = QDir::toNativeSeparators(beingRead).toStdWString();
        const BOOL moved = MoveFileExW(from.c_str(), to.c_str(),
                                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        const DWORD lastError = moved ? 0 : GetLastError();

        int pagesOnDisk = -1;
        try {
            QPDF probe;
            probe.processFile(beingRead.toUtf8().constData());
            pagesOnDisk = int(QPDFPageDocumentHelper(probe).getAllPages().size());
        } catch (const std::exception &e) {
            std::cerr << "FAIL: could not read the name the move left at: " << e.what() << '\n';
            ++failures;
        }
        note(QStringLiteral("sec79 move onto a name another tab reads: moved=%1 win32=%2, the tab "
                            "says %3 pages, the file at that name now says %4, the staged file is "
                            "still there: %5")
                 .arg(moved ? 1 : 0)
                 .arg(lastError)
                 .arg(pagesWhileOpen)
                 .arg(pagesOnDisk)
                 .arg(QFile::exists(stagedFile) ? 1 : 0));

        // The premise §79's early refusal rests on, pinned as checks rather than left as printed
        // numbers: a name one tab is reading cannot be replaced, and the failed attempt leaves both
        // files whole. If a future Qt starts sharing the delete, these redden -- and the refusal
        // that cites this measurement should be relaxed in the same change, not left telling a
        // reader a name is unavailable when it has become available.
        check(!moved,
              QStringLiteral("a name another tab of this program is reading cannot be replaced "
                             "(win32 %1)").arg(lastError));
        check(QFile::exists(stagedFile),
              "and the file that was to be moved into its place is still where it was");
        check(pagesOnDisk == pagesWhileOpen,
              QStringLiteral("and the name still holds the document the reader was looking at "
                             "(disk says %1, the tab said %2)").arg(pagesOnDisk).arg(pagesWhileOpen));
        check(other.pageCount() == pagesWhileOpen,
              "and the reading tab is left showing that same document, not a half-applied one");

        // The second half, which only matters if the move went through: does the tab that is still
        // looking at that name notice the change? Asked twice -- what it says now, and what it says
        // after being told to open the same path again.
        const int pagesStillSaid = other.pageCount();
        const Document::Status reopened = other.open(beingRead);
        note(QStringLiteral("sec79 that tab says %1 pages after the move; opening the same path "
                            "again returns status=%2 and then says %3 pages")
                 .arg(pagesStillSaid)
                 .arg(static_cast<int>(reopened))
                 .arg(other.pageCount()));
    }

    // ---- a bookmark click lands where the file said (§75's leftovers) ----
    //
    // §75 measured that Qt's bookmark model reports 0,0 for /FitH -- the shape a real book uses
    // 184 times out of 184 -- so a sidebar built from it can only jump to a page's top. §99 carried
    // the destinations into the file's own writing and left the click on the page number. This is
    // the click: the panel's Qt-read tree is joined by title and page to the file's own rows, the
    // destination is turned into a distance from the top of the page *as drawn* (so the reader's
    // rotation is part of the answer), and the view scrolls there.
    //
    // The sample is hand-built because our own writer cannot make a bookmark with a position; the
    // page is the 595x842 one from many-pages.pdf, so the unrotated answers are 842 - 700 = 142
    // for /XYZ 100 700 and 842 - 650 = 192 for /FitH 650. At a quarter turn the /XYZ's x becomes
    // the vertical coordinate (100), and the /FitH line becomes vertical, which has no height to
    // jump to and therefore answers the page's top.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "setup: a temporary folder for the bookmark-jump sample");
        const QString sample = dir.filePath(QStringLiteral("jump-bookmarks.pdf"));
        bool built = false;
        QString why;
        try {
            QPDF qpdf;
            qpdf.processFile((directory + QStringLiteral("/many-pages.pdf")).toUtf8().constData());
            const std::vector<QPDFPageObjectHelper> pages =
                QPDFPageDocumentHelper(qpdf).getAllPages();
            auto pageAt = [&](int index) { return pages.at(size_t(index)).getObjectHandle(); };
            auto item = [&qpdf](const std::string &title, QPDFObjectHandle parent) {
                QPDFObjectHandle handle = QPDFObjectHandle::newDictionary();
                handle.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
                handle.replaceKey("/Parent", parent);
                return qpdf.makeIndirectObject(handle);
            };
            auto addDest = [](QPDFObjectHandle handle, std::vector<QPDFObjectHandle> parts) {
                QPDFObjectHandle dest = QPDFObjectHandle::newArray();
                for (QPDFObjectHandle &part : parts)
                    dest.appendItem(part);
                handle.replaceKey("/Dest", dest);
            };
            QPDFObjectHandle root = QPDFObjectHandle::newDictionary();
            root.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
            root = qpdf.makeIndirectObject(root);

            const QPDFObjectHandle spot = item("Spot", root);
            addDest(spot, { pageAt(0), QPDFObjectHandle::newName("/XYZ"),
                            QPDFObjectHandle::newReal(100), QPDFObjectHandle::newReal(700),
                            QPDFObjectHandle::newNull() });
            const QPDFObjectHandle line = item("Line", root);
            addDest(line, { pageAt(0), QPDFObjectHandle::newName("/FitH"),
                            QPDFObjectHandle::newReal(650) });
            const QPDFObjectHandle whole = item("Whole", root);
            addDest(whole, { pageAt(1), QPDFObjectHandle::newName("/Fit") });

            std::vector<QPDFObjectHandle> row = { spot, line, whole };
            for (size_t i = 0; i < row.size(); ++i) {
                if (i > 0)
                    row[i].replaceKey("/Prev", row[i - 1]);
                if (i + 1 < row.size())
                    row[i].replaceKey("/Next", row[i + 1]);
            }
            root.replaceKey("/First", row.front());
            root.replaceKey("/Last", row.back());
            root.replaceKey("/Count", QPDFObjectHandle::newInteger(int(row.size())));
            qpdf.getRoot().replaceKey("/Outlines", root);
            QPDFWriter writer(qpdf, sample.toUtf8().constData());
            writer.write();
            built = true;
        } catch (const std::exception &e) {
            why = QString::fromUtf8(e.what());
        }
        check(built, QStringLiteral("setup: a file whose bookmarks name places inside pages: %1")
                         .arg(why));

        DocumentTab reading;
        reading.resize(900, 700);
        reading.show();
        check(reading.open(sample) == Document::Status::Ready,
              QStringLiteral("setup: the bookmark-jump sample opens: %1").arg(reading.lastError()));
        reading.setSidebarVisible(true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        auto *panel = reading.findChild<OutlinePanel *>();
        check(panel != nullptr, "the tab has a bookmark panel");
        if (panel) {
            panel->expandAll();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            // Through the base class: OutlinePanel has a member called `model` of its own, which
            // hides QAbstractItemView::model() from anyone holding the derived type -- the same
            // shape as the thumbnail strip's member below (§2.1's test).
            const auto *panelView = static_cast<const QAbstractItemView *>(panel);
            check(panelView->model() && panelView->model()->rowCount() == 3,
                  QStringLiteral("the panel shows the sample's three bookmarks (%1 rows)")
                      .arg(panelView->model() ? panelView->model()->rowCount() : -1));

            const auto indexOf = [panelView](const QString &title) {
                const QAbstractItemModel *model = panelView->model();
                for (int row = 0; model && row < model->rowCount(); ++row) {
                    const QModelIndex index = model->index(row, 0);
                    if (index.data(Qt::DisplayRole).toString() == title)
                        return index;
                }
                return QModelIndex();
            };
            // A real mouse click on the row, not the signal behind it: what is being checked is
            // that the click a reader makes carries the file's destination all the way to the
            // scrollbar, and a direct Q_EMIT would skip the panel's own role lookup.
            const auto clickTitle = [&](const QString &title) {
                const QModelIndex index = indexOf(title);
                if (!index.isValid())
                    return false;
                panel->scrollTo(index);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QRect rect = panel->visualRect(index);
                if (!rect.isValid() || !panel->viewport()->rect().contains(rect.center()))
                    return false;
                sendMouse(panel->viewport(), QEvent::MouseButtonPress, rect.center());
                sendMouse(panel->viewport(), QEvent::MouseButtonRelease, rect.center());
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                return true;
            };

            PageView *view = viewOf(reading);
            view->setLayoutMode(PageLayout::Mode::Continuous);
            view->setZoomMode(ZoomController::Mode::Percent);
            view->setZoom(1.0);
            view->setRotation(0);
            view->goToPage(0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            const int firstTop = view->verticalScrollBar()->value();

            check(clickTitle(QStringLiteral("Spot")), "the panel has a clickable Spot row");
            check(view->verticalScrollBar()->value() == firstTop + 142,
                  QStringLiteral("/XYZ 100 700 puts the viewport 142 pt down page 1 (scroll is "
                                 "%1, page top %2)")
                      .arg(view->verticalScrollBar()->value())
                      .arg(firstTop));
            check(view->currentPage() == 0, "and the page it landed on is the one it names");

            check(clickTitle(QStringLiteral("Line")), "the panel has a clickable Line row");
            check(view->verticalScrollBar()->value() == firstTop + 192,
                  QStringLiteral("/FitH 650 puts it 192 pt down (scroll is %1, page top %2)")
                      .arg(view->verticalScrollBar()->value())
                      .arg(firstTop));

            view->goToPage(1);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            const int secondTop = view->verticalScrollBar()->value();
            check(clickTitle(QStringLiteral("Whole")), "the panel has a clickable Whole row");
            check(view->verticalScrollBar()->value() == secondTop,
                  QStringLiteral("/Fit moves to page 2's top (scroll is %1, that top is %2)")
                      .arg(view->verticalScrollBar()->value())
                      .arg(secondTop));
            check(view->currentPage() == 1, "and it did change page for the /Fit bookmark");

            // The same destination under a quarter turn: the page's x axis is now vertical, so
            // /XYZ's 100 is the height to land at, and the /FitH line is vertical -- the top is
            // the only height it can honestly mean.
            view->goToPage(0);
            view->setRotation(90);
            view->goToPage(0);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            check(view->rotation() == 90, "setup: the page is turned a quarter turn");
            const int turnedTop = view->verticalScrollBar()->value();
            check(clickTitle(QStringLiteral("Spot")),
                  "the turned page still has a clickable Spot row");
            check(view->verticalScrollBar()->value() == turnedTop + 100,
                  QStringLiteral("after the turn /XYZ 100 700 lands 100 pt down (scroll is %1, "
                                 "page top %2)")
                      .arg(view->verticalScrollBar()->value())
                      .arg(turnedTop));
            check(clickTitle(QStringLiteral("Line")), "and a clickable Line row");
            check(view->verticalScrollBar()->value() == turnedTop,
                  QStringLiteral("a vertical /FitH line has no height, so it lands on the page top "
                                 "(scroll is %1)")
                      .arg(view->verticalScrollBar()->value()));
        }
    }

    if (failures == 0) {
        std::cout << "the view and the tab hold together (" << measurements
                  << " measurement(s) printed above)\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
