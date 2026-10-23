#include "filechangenotice.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

FileChangeNotice::FileChangeNotice(QWidget *host, std::function<bool(QString *)> reload)
    : reload(std::move(reload))
{
    // ---- the notice that the file changed under the reader ----
    //
    // Above the page and beside the find bar, because it is about the document
    // that is on screen and it has to be seen without opening a menu.
    //
    // This program can now write the open file itself (F8's in-place save), and a
    // watch that reported our own replacement as somebody else's change would put
    // the bar over the page the reader just saved. beginSelfWrite()/endSelfWrite()
    // are the two ends of that write; the events that still arrive for it are
    // recognized in onFileChangeSettled() by the size and time we left behind.
    fileWatcher = new QFileSystemWatcher(this);
    connect(fileWatcher, &QFileSystemWatcher::fileChanged, this,
            &FileChangeNotice::onWatchedFileChanged);

    fileChangeBar = new QWidget(host);
    fileChangeBar->setObjectName(QStringLiteral("fileChangeBar"));
    auto *noticeLayout = new QHBoxLayout(fileChangeBar);
    noticeLayout->setContentsMargins(6, 4, 6, 4);
    noticeLayout->setSpacing(6);
    fileChangeLabel = new QLabel(
        QCoreApplication::translate("DocumentTab", "这个文件在磁盘上改过了。"), fileChangeBar);
    fileChangeLabel->setObjectName(QStringLiteral("fileChangeLabel"));
    noticeLayout->addWidget(fileChangeLabel, 1);

    auto *reloadButton = new QPushButton(
        QCoreApplication::translate("DocumentTab", "重新加载"), fileChangeBar);
    reloadButton->setObjectName(QStringLiteral("fileChangeReload"));
    noticeLayout->addWidget(reloadButton);

    auto *dismissButton = new QToolButton(fileChangeBar);
    dismissButton->setObjectName(QStringLiteral("fileChangeClose"));
    dismissButton->setText(QStringLiteral("✕"));
    dismissButton->setToolTip(QCoreApplication::translate("DocumentTab", "关闭这条提示"));
    noticeLayout->addWidget(dismissButton);

    connect(reloadButton, &QPushButton::clicked, this, &FileChangeNotice::onReloadPressed);
    connect(dismissButton, &QToolButton::clicked, this, &FileChangeNotice::hideNotice);

    // 400 ms: long enough that a word processor's several writes for one save
    // fall inside one wait, short enough that a reader who saved and looked up
    // at the window is not left wondering. Kept in the middle of the range the
    // handoff names (300-500).
    fileChangeTimer = new QTimer(this);
    fileChangeTimer->setSingleShot(true);
    fileChangeTimer->setInterval(400);
    connect(fileChangeTimer, &QTimer::timeout, this, &FileChangeNotice::onFileChangeSettled);

    fileChangeBar->hide();
}

FileChangeNotice::~FileChangeNotice() = default;

bool FileChangeNotice::noticeVisible() const
{
    return fileChangeBar && fileChangeBar->isVisible();
}

QString FileChangeNotice::noticeText() const
{
    return fileChangeLabel ? fileChangeLabel->text() : QString();
}

void FileChangeNotice::watch(const QString &path)
{
    unwatch();
    if (path.isEmpty())
        return;
    watchedPath = path;
    if (!fileWatcher->addPath(path)) {
        // Not a failure worth a sentence: the file the reader opened has just
        // been read, and F5 still works. But without a watch there is nothing
        // to notice, so the bar must not believe it is watching anything --
        // otherwise the next change is compared against a path that was never
        // in the list.
        watchedPath.clear();
    }
}

void FileChangeNotice::unwatch()
{
    if (fileChangeTimer)
        fileChangeTimer->stop();
    fileChangeSize = -1;
    selfWrittenSize = -1;
    selfWrittenModified = -1;
    if (!watchedPath.isEmpty()) {
        fileWatcher->removePath(watchedPath);
        watchedPath.clear();
    }
}

void FileChangeNotice::beginSelfWrite()
{
    selfWriting = true;
    fileChangeSize = -1;
    fileChangeTimer->stop();
    // The path stays in watchedPath: the tab is still about the same file, and
    // endSelfWrite() has to know which one to watch again.
    if (!watchedPath.isEmpty())
        fileWatcher->removePath(watchedPath);
}

void FileChangeNotice::endSelfWrite(qint64 writtenSize, qint64 writtenModified)
{
    selfWriting = false;
    selfWrittenSize = writtenSize;
    selfWrittenModified = writtenModified;
    if (!watchedPath.isEmpty() && QFileInfo::exists(watchedPath)
        && !fileWatcher->files().contains(watchedPath))
        fileWatcher->addPath(watchedPath);
}

void FileChangeNotice::onWatchedFileChanged(const QString &path)
{
    // An event delivered while we are replacing the file is our own write, not a
    // change under the reader. endSelfWrite() reinstates the watch and records what
    // to compare any event still queued after this point against.
    if (selfWriting)
        return;
    if (path != watchedPath)
        return;

    // (a) Windows drops a file from a watcher's list when the file is deleted
    // or renamed. An editor that saves by writing a temporary file and moving
    // it over the original therefore delivers one change and then goes silent
    // -- unless the path is put back. This attempt often fails, because the
    // first signal can be the removal itself and there is no file to add at
    // that instant; onFileChangeSettled() tries again once the writing has
    // stopped, and that is the attempt an atomic save needs.
    if (QFileInfo::exists(path) && !fileWatcher->files().contains(path))
        fileWatcher->addPath(path);

    // (b) One save is not one signal: a writer flushes the file in pieces and
    // every piece can raise fileChanged. Showing the bar on the first would
    // put it on screen while the file was still being written, and a reload
    // pressed then would read half a document. Restarting the timer turns the
    // several signals into one wait.
    fileChangeSize = -1;
    fileChangeTimer->start();
}

void FileChangeNotice::onFileChangeSettled()
{
    if (watchedPath.isEmpty())
        return;
    if (selfWriting)
        return;

    // Our own save: the watcher may have delivered a queued event for the bytes
    // endSelfWrite() just put there. Recognizing them is what keeps the bar off
    // the page after 保存. Any later change by another program has a different
    // size or modified time, does not match, and takes the ordinary path below.
    if (selfWrittenSize >= 0) {
        const QFileInfo now(watchedPath);
        const bool ours = now.exists() && now.size() == selfWrittenSize
            && now.lastModified().toMSecsSinceEpoch() == selfWrittenModified;
        selfWrittenSize = -1;
        selfWrittenModified = -1;
        if (ours) {
            fileChangeSize = -1;
            return;
        }
    }

    if (!QFileInfo::exists(watchedPath)) {
        // Between the two renames of an atomic save, or gone for good. The
        // watcher has nothing more to report; 重新加载 is still the honest
        // answer, and the tab's reload() is where the sentence about a missing
        // file lives.
        showNotice();
        return;
    }

    // The second half of (a). If the path was not there when the signal
    // arrived, this is the first moment it can be watched again; a bar that
    // loses the watch here never hears the next save.
    if (!fileWatcher->files().contains(watchedPath))
        fileWatcher->addPath(watchedPath);

    const qint64 size = QFileInfo(watchedPath).size();
    if (fileChangeSize != size) {
        // Still moving. The first fire records the size; the next one that
        // agrees with it is the file the reader would reload. Two equal reads
        // 400 ms apart is not proof the writer has stopped, but it is the only
        // evidence available without watching the writer instead of the file.
        fileChangeSize = size;
        fileChangeTimer->start();
        return;
    }

    showNotice();
}

void FileChangeNotice::showNotice()
{
    // Set again for every change: the bar may still be carrying the reason a
    // previous 重新加载 refused, and that reason is about a file that has
    // changed once more -- keeping it would describe bytes that are no longer
    // there.
    fileChangeLabel->setText(QCoreApplication::translate("DocumentTab", "这个文件在磁盘上改过了。"));
    fileChangeBar->show();
}

void FileChangeNotice::hideNotice()
{
    fileChangeBar->hide();
    fileChangeLabel->setText(QCoreApplication::translate("DocumentTab", "这个文件在磁盘上改过了。"));
}

void FileChangeNotice::onReloadPressed()
{
    QString error;
    if (reload && reload(&error))
        return; // the tab's open() hid the bar and started a fresh watch

    if (error.isEmpty()) {
        // Empty means the reader said no to losing their pending edits. They
        // have answered the question; the bar stays as it was.
        return;
    }

    // Where the button was pressed, and on screen: the tab's reload() refused
    // before it touched the document (its probe is why a file that is not a
    // PDF gets here with the page still showing), so this sentence is all that
    // changes.
    fileChangeLabel->setText(error);
    fileChangeBar->show();
}
