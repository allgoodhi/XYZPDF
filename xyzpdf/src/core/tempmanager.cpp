#include "tempmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <map>
#include <memory>
#include <utility>

namespace {

QMutex &managerMutex()
{
    static QMutex mutex;
    return mutex;
}

std::map<QString, std::unique_ptr<QLockFile>> &ownedJobs()
{
    static std::map<QString, std::unique_ptr<QLockFile>> jobs;
    return jobs;
}

QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString categoryName(TempManager::Category category)
{
    switch (category) {
    case TempManager::Category::Drag:
        return QStringLiteral("drag");
    case TempManager::Category::Export:
        return QStringLiteral("export");
    case TempManager::Category::Print:
        return QStringLiteral("print");
    case TempManager::Category::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

// Every category, in one place, because both the ownership check and the sweep
// walk all of them and the two lists used to be written out separately.
const std::initializer_list<TempManager::Category> &allCategories()
{
    static const std::initializer_list<TempManager::Category> categories = {
        TempManager::Category::Drag, TempManager::Category::Export,
        TempManager::Category::Print, TempManager::Category::Other};
    return categories;
}

} // namespace

QString TempManager::rootPath()
{
    return cleanAbsolute(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("xyzpdf")));
}

QString TempManager::categoryPath(Category category)
{
    return cleanAbsolute(QDir(rootPath()).filePath(categoryName(category)));
}

QString TempManager::createJobDirectory(Category category)
{
    const QString parent = categoryPath(category);
    if (!QDir().mkpath(parent))
        return {};
    for (int attempt = 0; attempt < 10; ++attempt) {
        // Eight hex characters rather than a full UUID. Staged paths have to
        // fit inside MAX_PATH before the shell will accept them in CF_HDROP,
        // and the 28 characters this saves decide whether a deeply nested
        // document can use the fast drag-out path at all. Collisions are
        // handled by the retry loop and the ownership lock below.
        const QString path = cleanAbsolute(QDir(parent).filePath(
            QString::number(QRandomGenerator::global()->generate(), 16)
                .rightJustified(8, QLatin1Char('0'))));
        if (!QDir().mkpath(path))
            continue;
        auto lock = std::make_unique<QLockFile>(QDir(path).filePath(QStringLiteral(".owner")));
        lock->setStaleLockTime(0);
        if (!lock->tryLock()) {
            QDir(path).removeRecursively();
            continue;
        }
        QMutexLocker locker(&managerMutex());
        ownedJobs().emplace(path, std::move(lock));
        return path;
    }
    return {};
}

bool TempManager::isOwnedJobPath(const QString &path)
{
    const QString absolute = cleanAbsolute(path);
    const QString parent = cleanAbsolute(QFileInfo(absolute).absolutePath());
    const QString root = rootPath();
    for (const Category category : allCategories()) {
        if (parent == categoryPath(category)
            && cleanAbsolute(QFileInfo(parent).absolutePath()) == root) {
            return true;
        }
    }
    return false;
}

bool TempManager::removeJobDirectory(const QString &jobPath)
{
    const QString path = cleanAbsolute(jobPath);
    // Anything not directly under one of our own category directories is
    // refused rather than removed. This is the only guard between a bug in the
    // caller and a recursive delete of a path the user cares about.
    if (!isOwnedJobPath(path))
        return false;
    {
        QMutexLocker locker(&managerMutex());
        const auto iterator = ownedJobs().find(path);
        if (iterator != ownedJobs().end()) {
            iterator->second->unlock();
            ownedJobs().erase(iterator);
        }
    }
    const bool removed = !QFileInfo::exists(path) || QDir(path).removeRecursively();
    if (removed)
        removeEmptyParents(path);
    return removed;
}

void TempManager::cleanupStaleJobs(qint64 minimumAgeSeconds)
{
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-minimumAgeSeconds);
    for (const Category category : allCategories()) {
        const QDir parent(categoryPath(category));
        if (!parent.exists())
            continue;
        const QFileInfoList directories = parent.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo &directory : directories) {
            if (directory.lastModified().toUTC() >= cutoff)
                continue;
            const QString path = cleanAbsolute(directory.absoluteFilePath());
            {
                QMutexLocker locker(&managerMutex());
                if (ownedJobs().find(path) != ownedJobs().end())
                    continue;
            }
            // The lock file is what says whether somebody is still working
            // here. Taking it and immediately giving it back is the question
            // "is this still held", asked without a race.
            QLockFile probe(QDir(path).filePath(QStringLiteral(".owner")));
            probe.setStaleLockTime(0);
            if (!probe.tryLock())
                continue;
            probe.unlock();
            QDir(path).removeRecursively();
        }
    }
}

void TempManager::cleanupOwnedJobs()
{
    QStringList paths;
    {
        QMutexLocker locker(&managerMutex());
        for (const auto &job : ownedJobs())
            paths.append(job.first);
    }
    for (const QString &path : std::as_const(paths))
        removeJobDirectory(path);
}

void TempManager::removeEmptyParents(const QString &jobPath)
{
    QDir category(QFileInfo(cleanAbsolute(jobPath)).absolutePath());
    if (category.exists() && category.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        QDir().rmdir(category.absolutePath());
    QDir root(rootPath());
    if (root.exists() && root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        QDir().rmdir(root.absolutePath());
}
