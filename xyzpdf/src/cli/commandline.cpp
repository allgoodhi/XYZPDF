#include "commandline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace CommandLine {
namespace {

// The spelling that goes into the registry, and the only one that is written
// anywhere in this program.
const char kPrintSwitch[] = "--print";
const char kPageSwitch[] = "--page";

// The spellings the shell hands over. Explorer's print verb has been "/p" since
// before "--" existed, and Windows accepts "/page" as well as "-page"; both are
// accepted here rather than appointing one of them, because the side that
// writes them is a registry key somebody else may have written.
bool isPrintSwitch(const QString &argument)
{
    return argument == QLatin1String(kPrintSwitch) || argument == QLatin1String("/p")
           || argument == QLatin1String("/print");
}

bool isPageSwitch(const QString &argument)
{
    return argument == QLatin1String(kPageSwitch) || argument == QLatin1String("-page")
           || argument == QLatin1String("/page");
}

QString absolutePath(const QString &argument, const QString &workingDirectory)
{
    // Explorer hands over backslashes, and a trailing one on a folder often
    // enough. QDir::cleanPath already settles both, and leaves a drive root
    // alone -- "C:" on its own would mean "the current directory on C:", which
    // is not what was clicked on.
    const QString cleaned = QDir::cleanPath(argument);
    const QString absolute = QFileInfo(cleaned).isAbsolute()
                                 ? cleaned
                                 : QDir::cleanPath(QDir(workingDirectory).filePath(cleaned));
    return QDir::toNativeSeparators(absolute);
}

} // namespace

bool isKnownAction(int value)
{
    switch (static_cast<Action>(value)) {
    case Action::Open:
    case Action::Print:
        return true;
    }
    return false;
}

QString switchFor(Action action)
{
    switch (action) {
    case Action::Print:
        return QString::fromLatin1(kPrintSwitch);
    case Action::Open:
        break;
    }
    return QString();
}

QList<Request> coalesce(const QList<Request> &requests)
{
    QList<Request> folded;
    for (const Request &request : requests) {
        // Anything that could not be understood stands alone: merging it would
        // attach its complaint to work that is perfectly fine.
        const bool mergeable = request.valid && !folded.isEmpty() && folded.last().valid
                               && folded.last().action == request.action
                               // A page number belongs to the document it was
                               // given for. Merged with another path it would
                               // be applied to the wrong file.
                               && request.page < 0 && folded.last().page < 0;
        if (mergeable)
            folded.last().paths += request.paths;
        else
            folded.append(request);
    }
    return folded;
}

Request parse(const QStringList &arguments, const QString &workingDirectory)
{
    Request request;

    QStringList rest = arguments;
    if (!rest.isEmpty())
        rest.removeFirst(); // the program name

    bool actionSeen = false;
    bool optionsEnded = false;
    bool wantsPage = false;
    QStringList rawPaths;

    for (const QString &argument : std::as_const(rest)) {
        if (wantsPage) {
            wantsPage = false;
            bool understood = false;
            const int number = argument.toInt(&understood);
            if (!understood || number < 1) {
                request.valid = false;
                request.errorMessage =
                    QCoreApplication::translate("CommandLine", "%1 后面要跟一个页码（从 1 开始）。")
                        .arg(QString::fromLatin1(kPageSwitch));
                return request;
            }
            request.page = number - 1;
            continue;
        }
        if (!optionsEnded && argument == QLatin1String("--")) {
            optionsEnded = true;
            continue;
        }
        if (optionsEnded) {
            rawPaths.append(argument);
            continue;
        }

        if (isPrintSwitch(argument)) {
            if (actionSeen) {
                request.valid = false;
                request.errorMessage =
                    QCoreApplication::translate("CommandLine", "一次只能执行一个操作，但命令行里给了多个：%1")
                        .arg(argument);
                return request;
            }
            request.action = Action::Print;
            actionSeen = true;
            continue;
        }
        if (isPageSwitch(argument)) {
            wantsPage = true;
            continue;
        }

        // A path is anything that is not a switch. The check is deliberately
        // narrow: a file really can be called "--notes.pdf", and a path can
        // begin with "/" -- on Windows that is a path from the root of the
        // current drive. Only the exact spellings above are switches, and after
        // "--" nothing is.
        if (!argument.startsWith(QLatin1String("--"))) {
            rawPaths.append(argument);
            continue;
        }

        request.valid = false;
        request.errorMessage =
            QCoreApplication::translate("CommandLine", "无法识别的命令行选项：%1").arg(argument);
        return request;
    }

    if (wantsPage) {
        request.valid = false;
        request.errorMessage =
            QCoreApplication::translate("CommandLine", "%1 后面要跟一个页码（从 1 开始）。")
                .arg(QString::fromLatin1(kPageSwitch));
        return request;
    }

    if (rawPaths.isEmpty()) {
        // No file at all: a double-click on the program, or a shortcut. That is
        // an empty window rather than an error -- there is nothing wrong with
        // starting a reader to then open something from it.
        return request;
    }

    if (request.takesOneDocument() && rawPaths.size() > 1) {
        request.valid = false;
        request.errorMessage =
            QCoreApplication::translate("CommandLine", "%1 只能用在单个文件上，但给了 %2 个。")
                .arg(switchFor(request.action))
                .arg(rawPaths.size());
        return request;
    }

    for (const QString &raw : std::as_const(rawPaths))
        request.paths.append(absolutePath(raw, workingDirectory));
    return request;
}

} // namespace CommandLine
