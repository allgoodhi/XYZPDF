// The part of the context menu entry that is not COM.
//
// Split out for the same reason XYZip splits its verb table from its shell DLL:
// the decisions here are testable by calling them, while the COM object that
// consults them can only be tested by loading a DLL from a host that cannot be
// instrumented. So the smoke test compiles this file directly and drives the DLL
// only for what only a DLL can answer.
//
// It is one verb, not a menu: "用 XYZPDF 打开", which is the entry the registry
// used to write and which the package manifest cannot express any other way. See
// docs/store-submission.md section 3 for what installing the package took away.
#pragma once

#include <string>
#include <vector>

namespace ShellVerb {

// Whether a path is something this verb should offer to open.
//
// By extension rather than by asking the filesystem, and case-insensitively
// because the last component of a Windows path is what the user typed or what a
// scanner produced: `.PDF` is the same document.
bool isPdfPath(const std::wstring &path);

// The menu label. Constant, and deliberately not carrying the file's name: the
// entry sits directly under the name it would repeat, and the registry verb it
// replaces said the same thing.
std::wstring title();

// A command line that opens `paths` in the reader, starting at `first`.
//
// Returns the line for as many paths as fit under the command-line limit and
// reports how many it took through `consumed`, so the caller can keep going with
// the rest. Always takes at least one: a single path long enough to pass the cap
// on its own would otherwise never be opened at all.
std::wstring commandLineFor(const std::wstring &executable,
                            const std::vector<std::wstring> &paths,
                            size_t first, size_t *consumed);

// Every path, in as few command lines as they fit into.
//
// More than one line means more than one process, which is what the single
// instance and its request queue are for: the reader folds them back into one
// window with one tab per document.
std::vector<std::wstring> commandLinesFor(const std::wstring &executable,
                                          const std::vector<std::wstring> &paths);

} // namespace ShellVerb
