// Which interface languages exist, and which of them this build actually has.
//
// One table rather than an enumerator. The set grows, and an enumerator would
// have to be edited in the settings, the loader and the picker every time one
// was added -- three places to forget.
//
// A language appears to the user only when its catalogue is compiled into the
// binary. That is deliberate: offering a language and then showing Chinese
// because nobody wrote the catalogue yet is worse than not offering it, and it
// means translations can land one at a time without a separate list to keep in
// step.
//
// Chinese is the exception that needs no catalogue: the source strings in this
// program are Chinese, so it is what tr() returns untranslated.
#pragma once

#include <QList>
#include <QString>

namespace Languages {

struct Entry
{
    // A Qt locale name: "en", "ja", "pt_BR".
    QString code;
    // The language's name in itself, which is what a language picker shows --
    // someone looking for their own language cannot read the current one.
    QString nativeName;
};

// The stored value meaning "whatever Windows is set to".
inline QString systemCode()
{
    return QStringLiteral("system");
}

// Every language this program knows how to be, whether or not this build has
// the catalogue for it.
QList<Entry> known();

// Those a user can actually pick here: the ones whose catalogue is present.
QList<Entry> available();

// The concrete language a stored setting means, following the system when that
// is what was asked for, and falling back to English when the system's language
// is one this build does not have.
QString resolve(const QString &storedCode);

// The name to show for a code, or the code itself when it is not one of ours.
QString nativeName(const QString &code);

// Where a language's own catalogue lives inside the binary.
QString catalogueResource(const QString &code);

// Whether this build carries that catalogue.
bool hasCatalogue(const QString &code);

} // namespace Languages
