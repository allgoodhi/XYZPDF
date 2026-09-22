#include "shellverb.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string_view>

namespace {

// Well under the 32767 the API allows. The margin covers the quoting, and it is
// the same number XYZip uses so that neither one is a number to think about
// again: what matters is that it is comfortably below the limit rather than at
// it, because the limit is a hard failure and the cost is one extra process.
constexpr size_t kMaxCommandLine = 30000;

} // namespace

namespace ShellVerb {

bool isPdfPath(const std::wstring &path)
{
    constexpr std::wstring_view suffix = L".pdf";
    if (path.size() < suffix.size())
        return false;
    return std::equal(suffix.begin(), suffix.end(), path.end() - suffix.size(),
                      [](wchar_t expected, wchar_t actual) {
                          return std::towlower(expected) == std::towlower(actual);
                      });
}

std::wstring title()
{
    // Chosen from the user's UI language rather than from the program's
    // translation catalogue, and hardcoded rather than carried in a resource:
    // this DLL has no Qt in it on purpose (it is loaded into a host on somebody's
    // right-click, and pulling Qt along would make every menu slower to open), so
    // tr() does not exist here; and one string does not worth a .rc, a MUI block
    // and the resource indexing that comes with them.
    //
    // The cost is honest to state: a third language needs a line added here,
    // where the rest of the program would need only a .ts entry.
    const LANGID language = LOWORD(GetUserDefaultUILanguage());
    if (PRIMARYLANGID(language) == LANG_CHINESE)
        return L"用 XYZPDF 打开";
    return L"Open with XYZPDF";
}

std::wstring commandLineFor(const std::wstring &executable,
                            const std::vector<std::wstring> &paths,
                            size_t first, size_t *consumed)
{
    std::wstring line = L"\"" + executable + L"\"";
    size_t taken = 0;
    for (size_t index = first; index < paths.size(); ++index) {
        const std::wstring addition = L" \"" + paths[index] + L"\"";
        // Always take one, whatever its length, so that a path long enough to
        // blow the cap on its own is opened rather than skipped forever.
        if (taken > 0 && line.size() + addition.size() > kMaxCommandLine)
            break;
        line += addition;
        ++taken;
    }
    if (consumed)
        *consumed = taken;
    return line;
}

std::vector<std::wstring> commandLinesFor(const std::wstring &executable,
                                          const std::vector<std::wstring> &paths)
{
    std::vector<std::wstring> lines;
    if (executable.empty() || paths.empty())
        return lines;
    for (size_t index = 0; index < paths.size();) {
        size_t taken = 0;
        lines.push_back(commandLineFor(executable, paths, index, &taken));
        // A zero here would loop forever. It cannot happen while one path is
        // always taken, but the cost of the assumption being wrong is a hung
        // shell, so it is checked rather than trusted.
        if (taken == 0)
            break;
        index += taken;
    }
    return lines;
}

} // namespace ShellVerb
