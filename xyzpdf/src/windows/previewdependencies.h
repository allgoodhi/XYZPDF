// Where this DLL's own libraries are, from inside somebody else's process.
//
// Ported from XYZip's src/shell/previewdependencies.{h,cpp}, comments included.
// The problem is not PDF-specific and neither is the solution; see the .cpp for
// what it cost to find.
#pragma once

#include <string>

namespace PreviewDependencies {

// The directory this DLL was loaded from, with a trailing backslash. Empty when
// it cannot be worked out, which the caller treats as "fall back to the ordinary
// loader" rather than as an error.
std::wstring ownDirectory();

} // namespace PreviewDependencies
