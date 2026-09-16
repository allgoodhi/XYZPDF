#pragma once

#include <QString>
#include <QStringList>

// The documents this person opened recently.
//
// ---- what this stores, and why it can be switched off ----
//
// Every other setting in this program is a preference: a theme, a language.
// This one is a record of what somebody did -- the full paths of files they
// opened, which can say where they work, who they work for, and what they were
// sent. That is not a reason to leave the feature out; it is a reason for it to
// be visible, switchable and clearable, which the others do not need to be.
//
// So: it is kept in the same per-user settings store as everything else and
// never leaves the machine, remembering is a preference the user can turn off,
// turning it off clears what is already there rather than merely hiding it,
// and the menu carries its own way to clear the list without opening settings.
//
// ---- what it does not do ----
//
// It does not check whether the files are still there. Walking a list of paths
// on every menu open would touch removable drives and network shares and stall
// the menu on the ones that are asleep; and a file that is missing today may be
// a drive that is unplugged rather than a file that is gone, so dropping it
// would lose something the user still wants. An entry is checked when it is
// chosen, and only then is it dropped.
namespace RecentDocuments {

// How many are kept. Enough to cover "the one before last" reliably, short
// enough that the menu stays a menu.
inline constexpr int Maximum = 10;

// Newest first.
QStringList all();

// Records one, moving it to the front if it is already there. Does nothing
// when remembering is switched off.
void remember(const QString &documentPath);

// Drops one, for the entry that turned out not to be there any more.
void forget(const QString &documentPath);

// Empties the list.
void clear();

// Whether anything is recorded at all. On by default: a recent list that is
// off by default is a feature nobody finds.
bool isEnabled();

// Turning it off clears the list as well. A switch that stops adding but keeps
// what it already has is a switch that does not do what its label says.
void setEnabled(bool enabled);

} // namespace RecentDocuments
