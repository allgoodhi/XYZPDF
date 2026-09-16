#include "recentdocuments.h"

#include "appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace RecentDocuments {
namespace {

QString listKey()
{
    return QStringLiteral("recent/documents");
}

QString enabledKey()
{
    return QStringLiteral("recent/remember");
}

// One spelling per file, so that opening the same document by a different route
// -- a drag from Explorer, a double-click, a command line with a relative path
// -- does not leave two entries that look identical in the menu.
QString canonical(const QString &path)
{
    const QFileInfo info(path);
    const QString resolved =
        info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    return QDir::toNativeSeparators(resolved);
}

bool sameFile(const QString &left, const QString &right)
{
    // Case-insensitively: Windows paths are, and two entries differing only in
    // case would be the duplicate this is here to prevent.
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

} // namespace

QStringList all()
{
    if (!isEnabled())
        return {};
    QSettings valueStore = AppSettings::settings();
    QStringList stored = valueStore.value(listKey()).toStringList();
    // Trimmed on read as well as on write. The cap can change between
    // versions, and a list written by a version that kept more should not make
    // this one's menu longer than it means to be.
    while (stored.size() > Maximum)
        stored.removeLast();
    return stored;
}

void remember(const QString &documentPath)
{
    if (!isEnabled() || documentPath.trimmed().isEmpty())
        return;
    const QString entry = canonical(documentPath);
    if (entry.isEmpty())
        return;

    QSettings valueStore = AppSettings::settings();
    QStringList stored = valueStore.value(listKey()).toStringList();
    for (int index = stored.size() - 1; index >= 0; --index) {
        if (sameFile(stored.at(index), entry))
            stored.removeAt(index);
    }
    stored.prepend(entry);
    while (stored.size() > Maximum)
        stored.removeLast();
    valueStore.setValue(listKey(), stored);
}

void forget(const QString &documentPath)
{
    const QString entry = canonical(documentPath);
    QSettings valueStore = AppSettings::settings();
    QStringList stored = valueStore.value(listKey()).toStringList();
    bool changed = false;
    for (int index = stored.size() - 1; index >= 0; --index) {
        // Compared against what was asked for as well as its canonical form:
        // a file that has since been deleted has no canonical path, so the
        // entry being removed may be the only spelling left.
        if (sameFile(stored.at(index), entry)
            || sameFile(stored.at(index), QDir::toNativeSeparators(documentPath))) {
            stored.removeAt(index);
            changed = true;
        }
    }
    if (changed)
        valueStore.setValue(listKey(), stored);
}

void clear()
{
    QSettings valueStore = AppSettings::settings();
    valueStore.remove(listKey());
}

bool isEnabled()
{
    QSettings valueStore = AppSettings::settings();
    return valueStore.value(enabledKey(), true).toBool();
}

void setEnabled(bool enabled)
{
    QSettings valueStore = AppSettings::settings();
    valueStore.setValue(enabledKey(), enabled);
    // Off means gone, not hidden. A switch labelled "remember the documents I
    // open" that leaves a list of them behind is a switch that lies.
    if (!enabled)
        clear();
}

} // namespace RecentDocuments
