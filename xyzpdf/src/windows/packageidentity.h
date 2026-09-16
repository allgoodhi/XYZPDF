#ifndef PACKAGEIDENTITY_H
#define PACKAGEIDENTITY_H

#include <xyzpdf/packageconfig.h>

#include <string>

// Whether xyzpdf's MSIX package is installed for this user.
//
// It matters because the package and the registry verbs are alternatives, not
// additions. The package carries the IExplorerCommand handler; the registry
// entries are the fallback for machines without it. With both in place Explorer
// draws two "XYZPDF" entries side by side, identically named, and nothing about
// that tells the user which is which.
//
// There is no registry key to look for -- packaged COM registrations live in
// the state repository, not under HKCU\Software\Classes -- so the question has
// to be asked of the app model, and that wants a package family name.
namespace PackageIdentity {

// What the manifest says. These two have to match it exactly -- a mismatch does
// not fail to build, it just quietly stops finding the package. They used to be
// literals here and in the manifest both; now CMake stamps this header from the
// XYZPDF_PACKAGE_* cache variables and the packaging scripts stamp the manifest
// from the same header, so there is one place to change and nothing to keep in
// step.
inline const wchar_t *manifestIdentityName() { return XYZPDF_PACKAGE_IDENTITY_NAME_W; }
inline const wchar_t *manifestPublisher() { return XYZPDF_PACKAGE_PUBLISHER_W; }

// "<identity>_<publisherId>", where publisherId is thirteen characters derived
// from the publisher string. Windows computes this the same way, which is why
// the package folder on disk is named xyzpdf_<something> rather than
// xyzpdf_CN=allgoodhi.
//
// Empty when the hash cannot be computed.
std::wstring familyName(const std::wstring &identityName, const std::wstring &publisher);

// Whether a package with that family name is registered for the current user.
bool isInstalled(const std::wstring &packageFamilyName);

// The two together, for xyzpdf's own package.
inline bool isOurPackageInstalled()
{
    return isInstalled(familyName(manifestIdentityName(), manifestPublisher()));
}

} // namespace PackageIdentity

#endif // PACKAGEIDENTITY_H
