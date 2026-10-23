cmake_minimum_required(VERSION 3.19)

# The one place the version is written down.
#
# It reaches three other places that must agree, and used to be typed into each
# of them in XYZip: the About box, the VERSIONINFO block Windows shows in the
# file's properties, and Identity/@Version in the package manifest, which the
# Store uses to decide whether an upload is newer than what is already
# published. The first two are generated below; the manifest is stamped by
# packaging/build-package.ps1, and tests/version_smoke.cpp checks that it did.
#
# C is in the language list for the v2 dependencies -- QPDF and PDFium are both
# C -- and because CMake silently drops .c sources from a target without it.
project(xyzpdf VERSION 0.1.0 LANGUAGES CXX C)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # A localized cl.exe can make CMake write the /showIncludes prefix to
    # build.ninja as mojibake, silently dropping all header dependencies. Run
    # both detection and compilation under UTF-8, and fail configuration if the
    # exact prefix cannot be established.
    set(XYZPDF_MSVC_UTF8_LAUNCHER
        "${CMAKE_CURRENT_LIST_DIR}/cmake/msvc-utf8-launcher.cmd")
    set(XYZPDF_SHOWINCLUDES_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/XYZPDFShowIncludes")
    file(MAKE_DIRECTORY "${XYZPDF_SHOWINCLUDES_DIR}")
    file(WRITE "${XYZPDF_SHOWINCLUDES_DIR}/probe.h" "\n")
    file(WRITE "${XYZPDF_SHOWINCLUDES_DIR}/probe.cpp"
         "#include \"probe.h\"\nint main() { return 0; }\n")
    execute_process(
        COMMAND "${XYZPDF_MSVC_UTF8_LAUNCHER}" "${CMAKE_CXX_COMPILER}"
                /nologo /showIncludes /c probe.cpp
        WORKING_DIRECTORY "${XYZPDF_SHOWINCLUDES_DIR}"
        OUTPUT_VARIABLE XYZPDF_SHOWINCLUDES_OUTPUT
        ERROR_VARIABLE XYZPDF_SHOWINCLUDES_ERROR
        RESULT_VARIABLE XYZPDF_SHOWINCLUDES_RESULT
        ENCODING UTF-8)
    string(APPEND XYZPDF_SHOWINCLUDES_OUTPUT "${XYZPDF_SHOWINCLUDES_ERROR}")
    if(XYZPDF_SHOWINCLUDES_RESULT EQUAL 0
       AND XYZPDF_SHOWINCLUDES_OUTPUT MATCHES
           "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    else()
        message(FATAL_ERROR
            "Unable to detect the UTF-8 MSVC /showIncludes prefix; Ninja header dependencies would be unsafe.\n"
            "${XYZPDF_SHOWINCLUDES_OUTPUT}")
    endif()
    set(CMAKE_CXX_COMPILER_LAUNCHER "${XYZPDF_MSVC_UTF8_LAUNCHER}")
endif()

# Sources carry Chinese literals, and so do the comments. Without this MSVC
# reads them as the system ANSI codepage and mangles every wide literal and
# every string that reaches tr().
#
# /Brepro is the other half of "the hash of xyzpdf.exe is not a content
# fingerprint" (docs/store-submission.md §82): by default the linker writes the wall clock
# into the COFF header and copies it into the debug directory, so two builds of
# one source differ by four bytes and no comparison of hashes can ever answer
# "is the installed copy these bytes?".
#
# Given to the compiler and the linker because they are two separate claims and
# only one of them was measured here: with the flag on link.exe alone the image
# was byte-identical across runs, while with the flag on cl.exe alone the .obj
# still changed between runs. That is not a reason to drop either one --
# /Brepro replaces the timestamp field with a checksum of the content, so an
# object that embeds its own build path (which these do) carries that path into
# the checksum, and an archive rebuilt somewhere else would differ again.
if(MSVC)
    add_compile_options(/utf-8 /Brepro)
    add_link_options(/Brepro)
endif()

# Network is for QLocalServer/QLocalSocket, which is how a second launch hands
# its work to the first. No sockets are opened on any network.
# LinguistTools provides qt_add_translations, which compiles the English
# catalogue at build time -- 6.7 rather than 6.5 for the TARGETS form of it: on
# 6.5 the keyword is not recognised and lands in the unparsed arguments, which
# is silent and leaves the catalogues attached to nothing.
# Svg is not linked to draw anything: the window icon is an SVG, and the plugin
# that can read one ships with Qt Svg. Without it the icon silently does not
# appear, which is the kind of thing that is noticed a month later.
# Concurrent is for the text-recognition crop: rendering it is the renderer's
# business, but the recognition itself is seconds of work and goes to the
# global pool rather than the thread that draws.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent PrintSupport LinguistTools)
# Qt PDF is deliberately absent. It was PDFium behind a Qt-shaped API, and that
# wrapper is the reason a filled form field painted nothing at all
# (docs/pdfium-plan.md sec.4): it never initialises the form-fill environment and
# gives no way to ask it to. F7 replaced it with the library itself; the payload
# carries pdfium.dll and no Qt6Pdf.dll, which is also why windeployqt is told to
# leave Qt's PDF image-format plugin out below.
# PdfWidgets stays out for the same reason it always was: the plan rules out
# QPdfView, and asking for the module would make it tempting.

# vcpkg installs into ../vcpkg_installed/<triplet> from the repository root, and
# Qt Creator does not receive the vcpkg toolchain file from a normal Desktop
# kit. Prefer that installation when it is there, while still allowing a regular
# vcpkg toolchain configuration to supply packages from elsewhere.
#
# QPDF is the first dependency this program has of its own (docs/PLAN.md §4 M5:
# 结构编辑先上 QPDF，它不碰页面内容流). Everything Qt uses is still Qt. The
# install is `vcpkg install` run from the repository root, which is where
# vcpkg.json is; XYZip's build is laid out the same way, including the
# ../vcpkg_installed/<triplet> location that the block below looks for.
set(XYZPDF_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
if (XYZPDF_VCPKG_TRIPLET STREQUAL "")
    set(XYZPDF_VCPKG_TRIPLET "x64-windows")
endif()
set(XYZPDF_LOCAL_VCPKG_PREFIX
    "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_installed/${XYZPDF_VCPKG_TRIPLET}")
if (EXISTS "${XYZPDF_LOCAL_VCPKG_PREFIX}/share")
    list(PREPEND CMAKE_PREFIX_PATH "${XYZPDF_LOCAL_VCPKG_PREFIX}")
endif()

# Said out loud because the alternative is CMake's own message, which does not
# know that this project keeps its packages in the repository rather than in a
# toolchain file: a fresh clone that never ran vcpkg fails here, and the fix is
# one command in one directory.
find_package(qpdf CONFIG QUIET)
if (NOT qpdf_FOUND)
    message(FATAL_ERROR
        "QPDF was not found. Run `vcpkg install --triplet ${XYZPDF_VCPKG_TRIPLET}` "
        "from the repository root (the directory holding vcpkg.json).")
endif()

# The release DLLs, globbed the way XYZip globs them. Two consumers: the copy into
# <build>/tests further down, which is what lets a test executable start (they are
# run with only Qt's bin on PATH -- tools/build_all_tests.py), and the copy beside
# xyzpdf.exe, which is what lets that one be double-clicked in the build directory.
# The MSIX payload is a third, separate path: packaging/build-package.ps1 stages
# from an install tree and names the files it needs by hand, because a package that
# silently drops an imported DLL fails on somebody else's machine.
file(GLOB XYZPDF_VCPKG_RUNTIME_DLLS "${XYZPDF_LOCAL_VCPKG_PREFIX}/bin/*.dll")

if(MSVC)
    # rc.exe decodes its input with the system ANSI codepage unless told
    # otherwise, so the Chinese in xyzpdf.rc comes out of a UTF-8 file as GBK
    # mojibake -- XYZip's Details tab read "XYZip 鍘嬬缉杞欢". The damage is done
    # at compile time and nothing downstream can undo it.
    string(APPEND CMAKE_RC_FLAGS " /c65001")
endif()

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

# A fixed date for every resource timestamp rcc writes.
#
# rcc records the modification time of each file in a .qrc inside the resource
# tree, and qt_add_translations compiles xyzpdf_en.qm during the build -- so the
# time it records is the time this build ran. Two clean builds of one source
# differed by three bytes in the executable for that one reason, and because
# /Brepro replaces the image's timestamp field with a digest of its content,
# those three bytes came back as forty-four more. tools/check_reproducible.py is
# the check that says whether any of this still holds.
#
# It is not only the generated file: running rcc over one .qrc twice, touching a
# resource file between the runs, gives two different blobs -- which is another
# way of saying that a fresh clone, where every file's time is the checkout's,
# would not build to these bytes. Pinning the date covers those files too, for
# the same price.
#
# There is no command-line option for it: rcc reads QT_RCC_SOURCE_DATE_OVERRIDE
# (or SOURCE_DATE_EPOCH, the same number in the same unit) from its environment
# and gives every entry in the tree that value. Zero is read as "not set", which
# is why the pinned value is an ordinary date rather than 0.
#
# Both rcc paths are covered because they are two different mechanisms: the
# generated translations .qrc is compiled by a custom command that runs the
# imported target Qt6::rcc, while resources.qrc is compiled by CMake's AUTORCC,
# which takes its binary from CMAKE_AUTORCC_EXECUTABLE. The wrapper forwards
# every argument and propagates the exit code -- a resource that failed to
# compile quietly would be a build that passes and lies.
#
# Windows only, because the wrapper is a .cmd and this is the platform the
# program ships on; elsewhere the resources keep their build-time date.
if(WIN32)
    set(XYZPDF_RCC_SOURCE_DATE 1577836800) # 2020-01-01T00:00:00Z
    get_target_property(XYZPDF_RCC_REAL Qt6::rcc IMPORTED_LOCATION)
    if(NOT XYZPDF_RCC_REAL)
        message(FATAL_ERROR
            "Qt6::rcc has no IMPORTED_LOCATION, so the resource timestamps cannot "
            "be pinned; the build would be reproducible only by accident.")
    endif()
    set(XYZPDF_RCC_WRAPPER "${CMAKE_BINARY_DIR}/cmake/rcc-deterministic.cmd")
    configure_file(cmake/rcc-deterministic.cmd.in "${XYZPDF_RCC_WRAPPER}" @ONLY)
    # Every configuration the package declares, not the one this build happens to
    # use: Qt's rcc import carries RELWITHDEBINFO, RELEASE and MINSIZEREL, and a
    # config-specific location wins over the plain one, so overriding only the
    # plain one leaves a RelWithDebInfo build calling the real rcc and writing
    # the clock -- silently, and reproducibility is not something anyone notices
    # until two builds disagree.
    set_property(TARGET Qt6::rcc PROPERTY IMPORTED_LOCATION "${XYZPDF_RCC_WRAPPER}")
    get_target_property(XYZPDF_RCC_CONFIGS Qt6::rcc IMPORTED_CONFIGURATIONS)
    foreach(config IN LISTS XYZPDF_RCC_CONFIGS)
        set_property(TARGET Qt6::rcc PROPERTY IMPORTED_LOCATION_${config}
                                             "${XYZPDF_RCC_WRAPPER}")
    endforeach()
    set(CMAKE_AUTORCC_EXECUTABLE "${XYZPDF_RCC_WRAPPER}")
endif()

# C++20, not the C++17 Qt defaults to. The recognition feature is written
# against C++/WinRT, whose headers decide between <coroutine> and
# <experimental/coroutine> by the language level -- and in C++17 the
# experimental one hard-fails the build on this toolchain (STL1011). Nothing
# else in the project cares; the rest of the code is legal in either.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The version, in the two shapes C++ and the resource compiler want it.
configure_file(src/core/version.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/xyzpdf/version.h @ONLY)

# Which edition this is.
#
# v1 ships one, and nothing is gated on it -- see src/core/edition.h for why the
# option and the header exist anyway. What is deliberately absent is the rest of
# XYZip's arrangement: no second build directory, no second manifest, no second
# set of assets. Those arrive with the second edition or not at all.
set(XYZPDF_EDITION "free" CACHE STRING "Which edition this is (v1: free)")
set_property(CACHE XYZPDF_EDITION PROPERTY STRINGS free pro)
if (NOT XYZPDF_EDITION STREQUAL "free" AND NOT XYZPDF_EDITION STREQUAL "pro")
    message(FATAL_ERROR
        "XYZPDF_EDITION must be free or pro, and v1 only knows free. "
        "Configuring pro produces a build that does not differ from free in any way.")
endif()

# The package identity. The defaults are the development ones, which are what a
# locally installed package has to be signed with; a Store build overrides them
# with what Partner Center assigned, which for this program has not happened yet
# -- so there is nothing reserved to write down here. See
# src/core/packageconfig.h.in.
set(XYZPDF_PACKAGE_IDENTITY_NAME "allgoodhi.XYZPDF" CACHE STRING
    "Package Identity/@Name -- the name reserved in Partner Center for a Store build")
set(XYZPDF_PACKAGE_PUBLISHER "CN=allgoodhi" CACHE STRING
    "Package Identity/@Publisher -- must match the signing certificate's subject")
set(XYZPDF_PACKAGE_PUBLISHER_DISPLAY_NAME "allgoodhi" CACHE STRING
    "The publisher name shown to users")
set(XYZPDF_PACKAGE_DISPLAY_NAME "XYZPDF" CACHE STRING
    "The application name shown to users")
configure_file(src/core/packageconfig.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/xyzpdf/packageconfig.h @ONLY)

# Both generated headers, for every target below.
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

# ---------------------------------------------------------------------------
# The document engine
# ---------------------------------------------------------------------------

# PDFium, called directly. docs/pdfium-plan.md sec.10 F7 replaced Qt PDF with
# this, slice by slice, and F7-E took the development switch and the second
# implementation out at the end: a build has one engine and it is this one,
# because the point of the exercise was that a package carries exactly one thing
# that can read a PDF (sec.8: the alternative was +7.59 MB of both).
#
# Where the release is. tools/fetch_pdfium.py puts it in <repo>/pdfium_installed
# (gitignored); XYZPDF_PDFIUM_DIR points at an extracted release somewhere else.
# Nothing here downloads anything, and a build without one stops here with the
# command to run -- the same treatment vcpkg's absence gets above.
set(XYZPDF_PDFIUM_DIR "" CACHE PATH
    "An extracted pdfium-binaries release (include/, lib/, bin/). Empty means pdfium_installed/ if present.")
set(XYZPDF_PDFIUM_ROOT "")
if(NOT XYZPDF_PDFIUM_DIR STREQUAL "")
    file(TO_CMAKE_PATH "${XYZPDF_PDFIUM_DIR}" XYZPDF_PDFIUM_ROOT)
else()
    set(XYZPDF_PDFIUM_DEFAULT "${CMAKE_CURRENT_LIST_DIR}/../pdfium_installed")
    if(EXISTS "${XYZPDF_PDFIUM_DEFAULT}")
        file(TO_CMAKE_PATH "${XYZPDF_PDFIUM_DEFAULT}" XYZPDF_PDFIUM_ROOT)
    endif()
endif()

if(XYZPDF_PDFIUM_ROOT STREQUAL ""
   OR NOT EXISTS "${XYZPDF_PDFIUM_ROOT}/include/fpdfview.h"
   OR NOT EXISTS "${XYZPDF_PDFIUM_ROOT}/lib/pdfium.dll.lib"
   OR NOT EXISTS "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll")
    message(FATAL_ERROR
        "No PDFium release was found. Run `python tools/fetch_pdfium.py` (it verifies the pinned "
        "sha256) from this directory, or pass -DXYZPDF_PDFIUM_DIR=<extracted release>. "
        "XYZPDF_PDFIUM_DIR is '${XYZPDF_PDFIUM_DIR}'; include/fpdfview.h, lib/pdfium.dll.lib and "
        "bin/pdfium.dll all have to be under it.")
endif()

# One imported target, so that every consumer below links the same bytes and
# nobody has to remember which of the three paths the include directory was.
add_library(xyzpdf_pdfium SHARED IMPORTED)
set_target_properties(xyzpdf_pdfium PROPERTIES
    IMPORTED_IMPLIB "${XYZPDF_PDFIUM_ROOT}/lib/pdfium.dll.lib"
    IMPORTED_LOCATION "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${XYZPDF_PDFIUM_ROOT}/include")

# The release's own version string, compiled in, so the log says which bytes it was measured
# against instead of leaving that to whoever ran it to remember (sec.97's rule about identity).
set(XYZPDF_PDFIUM_VERSION "unknown")
if(EXISTS "${XYZPDF_PDFIUM_ROOT}/VERSION")
    file(STRINGS "${XYZPDF_PDFIUM_ROOT}/VERSION" XYZPDF_PDFIUM_VERSION_LINES)
    set(XYZPDF_PDFIUM_VERSION "")
    foreach(component MAJOR MINOR BUILD PATCH)
        foreach(line ${XYZPDF_PDFIUM_VERSION_LINES})
            if(line MATCHES "^${component}=([0-9]+)$")
                string(APPEND XYZPDF_PDFIUM_VERSION "${CMAKE_MATCH_1}.")
            endif()
        endforeach()
    endforeach()
    # Four components, four dots appended: crop the one past the patch number. Counted rather
    # than regexed, because a regex for "one character at the end" has to survive CMake's own
    # escape rules on the way to the regex engine.
    string(LENGTH "${XYZPDF_PDFIUM_VERSION}" XYZPDF_PDFIUM_VERSION_LENGTH)
    math(EXPR XYZPDF_PDFIUM_VERSION_CROP "${XYZPDF_PDFIUM_VERSION_LENGTH} - 1")
    string(SUBSTRING "${XYZPDF_PDFIUM_VERSION}" 0 ${XYZPDF_PDFIUM_VERSION_CROP}
           XYZPDF_PDFIUM_VERSION)
endif()
message(STATUS "PDFium ${XYZPDF_PDFIUM_VERSION} from ${XYZPDF_PDFIUM_ROOT}")

# Compiles the engine into TARGET. Used by the program, by pdfprobe, by both
# shell handlers and by every test that opens a document -- that is, by every
# target that exists in the package and so has to answer with the engine the
# reader uses.
function(xyzpdf_use_document_backend TARGET)
    target_sources(${TARGET} PRIVATE
        src/document/pdfiumbackend.h
        src/document/pdfiumbackend.cpp)
    target_link_libraries(${TARGET} PRIVATE xyzpdf_pdfium)
endfunction()

# ---------------------------------------------------------------------------
# The program
# ---------------------------------------------------------------------------

set(XYZPDF_SOURCES
    main.cpp
    mainwindow.h
    mainwindow.cpp
    mainwindow.ui
    documenttab.h
    documenttab.cpp
    resources.qrc

    src/cli/commandline.h
    src/cli/commandline.cpp
    src/cli/instancegateway.h
    src/cli/instancegateway.cpp

    src/core/aboutinfo.h
    src/core/appsettings.h
    src/core/edition.h
    src/core/languages.h
    src/core/languages.cpp
    src/core/recentdocuments.h
    src/core/recentdocuments.cpp
    src/core/tempmanager.h
    src/core/tempmanager.cpp
    src/core/thirdpartynotices.h
    src/core/thirdpartynotices.cpp
    src/core/windowdialogs.h
    src/core/windowdialogs.cpp

    src/document/bookmarkposition.h
    src/document/bookmarkposition.cpp
    src/document/document.h
    src/document/documentfactory.h
    src/document/documentfactory.cpp
    src/document/docmetadata.h
    src/document/outline.h
    src/document/outline.cpp
    src/document/pagemetrics.h
    src/document/pagemetrics.cpp
    src/document/textlayer.h
    src/document/textlayer.cpp
    src/document/textsearch.h
    src/document/textsearch.cpp

    src/edit/documenteditor.h
    src/edit/documenteditor.cpp

    src/export/docxwriter.h
    src/export/docxwriter.cpp
    src/export/imageexport.h
    src/export/imageexport.cpp
    src/export/wordexport.h
    src/export/wordexport.cpp
    src/export/zipwriter.h
    src/export/zipwriter.cpp

    src/ocr/ocrengine.h
    src/ocr/ocrengine.cpp

    src/print/printjob.h
    src/print/printjob.cpp

    src/render/pagerenderer.h
    src/render/pagerenderer.cpp
    src/render/rendercache.h
    src/render/rendercache.cpp
    src/render/rendertoken.h

    src/services/documentoutput.h
    src/services/documentoutput.cpp
    src/services/filechangenotice.h
    src/services/filechangenotice.cpp
    src/services/foregroundjobs.h
    src/services/ocrsession.h
    src/services/ocrsession.cpp
    src/services/searchsession.h
    src/services/searchsession.cpp
    src/services/translatesession.h
    src/services/translatesession.cpp
    src/services/wordexportsession.h
    src/services/wordexportsession.cpp

    src/settings/settingsdialog.h
    src/settings/settingsdialog.cpp

    src/theme/theme.h
    src/theme/thememanager.h
    src/theme/thememanager.cpp

    src/translate/translator.h
    src/translate/translator.cpp
    src/translate/keystore.h
    src/translate/keystore.cpp
    src/translate/keystore_windows.cpp

    src/ui/bookmarkdialog.h
    src/ui/bookmarkdialog.cpp
    src/ui/documenttabbar.h
    src/ui/documenttabbar.cpp
    src/ui/deletepagesdialog.h
    src/ui/deletepagesdialog.cpp
    src/ui/findbar.h
    src/ui/findbar.cpp
    src/ui/imageexportdialog.h
    src/ui/imageexportdialog.cpp
    src/ui/languagedialog.h
    src/ui/languagedialog.cpp
    src/ui/mergepagesdialog.h
    src/ui/mergepagesdialog.cpp
    src/ui/movepagesdialog.h
    src/ui/movepagesdialog.cpp
    src/ui/ocrdialog.h
    src/ui/ocrdialog.cpp
    src/ui/outlinepanel.h
    src/ui/outlinepanel.cpp
    src/ui/printdialog.h
    src/ui/printdialog.cpp
    src/ui/propertiesdialog.h
    src/ui/propertiesdialog.cpp
    src/ui/protectpagesdialog.h
    src/ui/protectpagesdialog.cpp
    src/ui/rotatepagesdialog.h
    src/ui/rotatepagesdialog.cpp
    src/ui/searchpanel.h
    src/ui/searchpanel.cpp
    src/ui/splitpagesdialog.h
    src/ui/splitpagesdialog.cpp
    src/ui/thirdpartydialog.h
    src/ui/thirdpartydialog.cpp
    src/ui/thumbnailpanel.h
    src/ui/thumbnailpanel.cpp
    src/ui/translatedialog.h
    src/ui/translatedialog.cpp
    src/ui/watermarkdialog.h
    src/ui/watermarkdialog.cpp
    src/ui/wrappingtoolbararea.h
    src/ui/wrappingtoolbararea.cpp

    src/view/pagelayout.h
    src/view/pagelayout.cpp
    src/view/pageview.h
    src/view/pageview.cpp
    src/view/zoomcontroller.h
    src/view/zoomcontroller.cpp
)

qt_add_executable(xyzpdf WIN32 MACOSX_BUNDLE ${XYZPDF_SOURCES})

# The engine, chosen above. This is the whole of the program's backend decision:
# the factory includes the header this compiles and no other one.
xyzpdf_use_document_backend(xyzpdf)

target_link_libraries(xyzpdf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::Concurrent
    Qt6::PrintSupport
    # From src/edit, which is the only place it is named: the structural editor
    # writes, the reader only reads, and the seam between the two is the point of
    # docs/PLAN.md §7.3.
    qpdf::libqpdf
)

if(WIN32)
    # The resource compiler is run "unscanned": ninja makes xyzpdf.rc.res depend
    # on xyzpdf.rc and on nothing the script names. Regenerating
    # packaging/app.ico therefore relinked the same old icon into the
    # executable, silently -- XYZip's program was rebuilt and still wore the
    # previous artwork. Say the dependency out loud.
    set_property(SOURCE xyzpdf.rc APPEND PROPERTY OBJECT_DEPENDS
                 ${CMAKE_CURRENT_SOURCE_DIR}/packaging/app.ico
                 ${CMAKE_CURRENT_BINARY_DIR}/generated/xyzpdf/version.h)

    # The DPI declaration, merged into the manifest the linker already writes.
    # MANIFESTINPUT merges rather than replaces, which is what keeps the
    # execution level the default manifest carries; adding a second RT_MANIFEST
    # resource through the .rc file instead would be a duplicate, not a merge.
    #
    # The App Certification Kit reported "not a DPI aware application" because it
    # reads the executable's manifest and its imports, and Qt sets the awareness
    # from inside the platform plugin once the process is running, where nothing
    # static can see it. xyzpdf.manifest says what was already true.
    if(MSVC)
        target_link_options(xyzpdf PRIVATE
            /MANIFEST:EMBED
            "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/xyzpdf.manifest")
        set_property(TARGET xyzpdf APPEND PROPERTY LINK_DEPENDS
                     ${CMAKE_CURRENT_SOURCE_DIR}/xyzpdf.manifest)
    endif()

    # The Windows-only half: the registry entries, and the question of whether
    # the package is installed instead.
    target_sources(xyzpdf PRIVATE
        xyzpdf.rc
        src/windows/packageidentity.h
        src/windows/packageidentity.cpp
        src/windows/shellintegration.h
        src/windows/shellintegration.cpp
    )
    target_link_libraries(xyzpdf PRIVATE bcrypt)
    # The Windows OCR service (Windows.Media.Ocr) behind the recognition
    # feature: a system component reached through its WinRT ABI, which is what
    # windowsapp.lib provides. Nothing is redistributed from it, which is why
    # it is not in the third-party notices.
    target_link_libraries(xyzpdf PRIVATE windowsapp)
    # CredWriteW / CredReadW / CredDeleteW: the translation key's storage. See
    # src/translate/keystore_windows.cpp.
    target_link_libraries(xyzpdf PRIVATE advapi32)

    # Deploy the Qt runtime next to the executable, so that it can be run by
    # double-clicking it in the build directory.
    #
    # Without this a build succeeds and the program then refuses to start with
    # "找不到 Qt6Core.dll" -- which reads like a broken build rather than like a
    # program that has not been given its libraries, and it is the first thing
    # anybody sees. XYZip's answer was to run it from Qt Creator, where the
    # development environment already has Qt's bin on PATH; that works until
    # somebody opens the .exe from Explorer, which is how a reader is actually
    # used.
    #
    # The flags leave out what this program does not use and would otherwise
    # carry, about forty megabytes of it:
    #   --no-translations          the catalogues are compiled into the binary
    #                              (resources.qrc and qt_add_translations below),
    #                              so Qt's own .qm files would be a second,
    #                              unused set sitting beside the program
    #   --no-compiler-runtime      the MSVC runtime is on any machine that can
    #                              build this; the package has its own answer
    #   --no-system-d3d-compiler   for Qt Quick, which this program does not use
    #   --no-opengl-sw             a twenty-megabyte software OpenGL fallback,
    #                              for a program that paints with the raster
    #                              engine
    if(TARGET Qt6::windeployqt)
        # The imported target is the path Qt itself knows, which is better than
        # any guess this file could make.
        set(XYZPDF_WINDEPLOYQT "$<TARGET_FILE:Qt6::windeployqt>")
    else()
        find_program(XYZPDF_WINDEPLOYQT NAMES windeployqt
                     HINTS "${QT6_INSTALL_PREFIX}/bin")
    endif()
    # windeployqt's imageformats set includes qpdf.dll -- Qt's plugin for loading
    # a PDF through QImage -- and that plugin links Qt6Pdf.dll. This program never
    # asks QImage for a PDF, and deploying the plugin would put a second PDF
    # engine in the payload the whole F7 exercise is about carrying one of.
    # Excluded by name; measured by listing the directory afterwards, not by
    # trusting the flag (the plugin set is discovered, not declared).
    set(XYZPDF_WINDEPLOYQT_EXCLUDES --exclude-plugins qpdf)
    if(XYZPDF_WINDEPLOYQT)
        add_custom_command(TARGET xyzpdf POST_BUILD
            COMMAND "${XYZPDF_WINDEPLOYQT}"
                    --no-translations
                    --no-compiler-runtime
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    ${XYZPDF_WINDEPLOYQT_EXCLUDES}
                    "$<TARGET_FILE:xyzpdf>"
            COMMENT "Deploying the Qt runtime beside xyzpdf.exe"
            VERBATIM)
    else()
        # Said out loud rather than left to the linker to imply: with no
        # windeployqt the program only runs with Qt's bin on PATH, and finding
        # that out from a missing-DLL dialog is finding it out the hard way.
        message(STATUS
            "windeployqt was not found; xyzpdf.exe will need Qt's bin directory on PATH to start")
    endif()

    # The other runtime, for the same reason and by the same means: xyzpdf.exe
    # imports qpdf30.dll, and the loader looks beside the executable and on PATH,
    # neither of which is where vcpkg put it. Without this the program builds and
    # then will not start, which is exactly the complaint the block above exists to
    # prevent -- Qt just got there first.
    if(XYZPDF_VCPKG_RUNTIME_DLLS)
        add_custom_command(TARGET xyzpdf POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${XYZPDF_VCPKG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:xyzpdf>"
            COMMENT "Deploying the vcpkg runtime beside xyzpdf.exe"
            VERBATIM)
    endif()

    # And the engine's own DLL, by the same argument: it is not Qt's to deploy, so
    # nothing else puts it there.
    add_custom_command(TARGET xyzpdf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll" "$<TARGET_FILE_DIR:xyzpdf>"
        COMMENT "Deploying pdfium.dll beside xyzpdf.exe"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# The tests
# ---------------------------------------------------------------------------

# Every test in the project goes through this.
#
# XYZip writes the same eight lines out for each of its forty-odd tests, and the
# failure mode of that is what tools/build_all_tests.py exists to catch: a target
# that stopped building while its old .exe kept reporting last week's result.
# The less there is to get right per test, the less there is to get wrong.
#
# The tests compile the sources they exercise directly rather than linking the
# program, so that a test of the layout arithmetic does not need a window, a
# document, or a working Qt platform plugin.
#
# RUNTIME_OUTPUT_DIRECTORY is where build_all_tests.py looks, and it is
# <build>/tests rather than beside the program because the tests are run with
# the source directory as the working directory -- the fixtures are opened by
# relative path, and that is what makes a failing test reproducible by hand.
function(xyzpdf_add_test NAME)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES" ${ARGN})
    qt_add_executable(xyzpdf_${NAME} tests/${NAME}.cpp ${TEST_SOURCES})
    target_link_libraries(xyzpdf_${NAME} PRIVATE Qt6::Core Qt6::Gui ${TEST_LIBRARIES})
    set_target_properties(xyzpdf_${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    # A test that links a vcpkg library cannot start without its DLL, and the
    # harness gives it only Qt's bin on PATH. Copying them into <build>/tests is
    # what XYZip does for the same reason.
    if(WIN32 AND XYZPDF_VCPKG_RUNTIME_DLLS)
        add_custom_command(TARGET xyzpdf_${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${XYZPDF_VCPKG_RUNTIME_DLLS} "${CMAKE_BINARY_DIR}/tests"
            VERBATIM)
    endif()
    # The same argument for the engine: a test that opens a document loads
    # pdfium.dll, the loader looks beside the executable and on PATH, and the
    # harness gives a test only Qt's bin on PATH. Copied for every test rather
    # than only for the ones that ask, because which those are changes with the
    # suite and the cost of doing it for all is one file in a directory the
    # package does not read.
    if(WIN32)
        add_custom_command(TARGET xyzpdf_${NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll" "${CMAKE_BINARY_DIR}/tests"
            VERBATIM)
    endif()
endfunction()

# The version, and the manifest it is stamped into.
option(XYZPDF_BUILD_VERSION_SMOKE "Build the version agreement smoke test" OFF)
if(XYZPDF_BUILD_VERSION_SMOKE)
    xyzpdf_add_test(version_smoke)
endif()

# Every switch the shell can hand over, and every way of getting it wrong.
option(XYZPDF_BUILD_COMMANDLINE_SMOKE "Build the command line smoke test" OFF)
if(XYZPDF_BUILD_COMMANDLINE_SMOKE)
    xyzpdf_add_test(commandline_smoke
        SOURCES src/cli/commandline.cpp)
endif()

# The handover between two launches, over a real named pipe.
option(XYZPDF_BUILD_INSTANCEGATEWAY_SMOKE "Build the instance gateway smoke test" OFF)
if(XYZPDF_BUILD_INSTANCEGATEWAY_SMOKE)
    xyzpdf_add_test(instancegateway_smoke
        SOURCES src/cli/commandline.cpp src/cli/instancegateway.cpp
        LIBRARIES Qt6::Network)
endif()

# The catalogues the build claims to have, and the ones it actually carries.
option(XYZPDF_BUILD_I18N_SMOKE "Build the interface language smoke test" OFF)
if(XYZPDF_BUILD_I18N_SMOKE)
    xyzpdf_add_test(i18n_smoke
        SOURCES src/core/languages.cpp
                src/core/thirdpartynotices.cpp
                resources.qrc)
endif()

# Opening a document: pages, sizes, text, outline, and a file that is not a PDF.
option(XYZPDF_BUILD_DOCUMENT_SMOKE "Build the document backend smoke test" OFF)
if(XYZPDF_BUILD_DOCUMENT_SMOKE)
    xyzpdf_add_test(document_smoke
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium)
endif()

# The first native dependency, proven to work before the program uses it. Linked
# against the reader's engine as well as qpdf because the claim being tested is
# the handoff between them, not either one alone -- see tests/qpdf_smoke.cpp.
option(XYZPDF_BUILD_QPDF_SMOKE "Build the QPDF round-trip smoke test" OFF)
if(XYZPDF_BUILD_QPDF_SMOKE)
    xyzpdf_add_test(qpdf_smoke
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium qpdf::libqpdf)
endif()

# The first thing the program writes with that dependency: pages taken out of a
# document, saved as a new file. Two halves -- the arithmetic that decides what a
# range of page numbers means, and the round trip that proves the file which comes
# out is a PDF the reader opens and the original is still byte for byte what it
# was. See tests/documenteditor_smoke.cpp.
option(XYZPDF_BUILD_DOCUMENTEDITOR_SMOKE "Build the page-deletion smoke test" OFF)
if(XYZPDF_BUILD_DOCUMENTEDITOR_SMOKE)
    xyzpdf_add_test(documenteditor_smoke
        SOURCES src/edit/documenteditor.cpp
                src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium qpdf::libqpdf)
endif()

# What QPDF does with an encrypted file when only the user password is in hand. Built
# with everything else and deliberately not run -- the name carries 'probe', which is
# how tools/build_all_tests.py knows a target prints answers instead of passing or
# failing. The questions are in the header comment of tests/encryptprobe.cpp; they are
# what decides whether 解密 can be a feature, so the file stays after the answer is
# written down, because the next person to doubt that answer should measure it again
# rather than take a doc sentence on faith.
option(XYZPDF_BUILD_ENCRYPTPROBE "Build the encrypted-document measurement" OFF)
if(XYZPDF_BUILD_ENCRYPTPROBE)
    xyzpdf_add_test(encryptprobe
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium qpdf::libqpdf)
endif()

# Whether a watermark can be a feature, and which of the two ways of drawing one the
# renderer actually shows. Built with everything else and deliberately not run, for the
# same reason as encryptprobe: every line is another library's answer. The questions are
# in the header comment of tests/watermarkprobe.cpp, and the first of them is why docs
# PLAN.md's "水印 ... 不碰页面内容流" needs correcting before any of this is written.
option(XYZPDF_BUILD_WATERMARKPROBE "Build the watermark-route measurement" OFF)
if(XYZPDF_BUILD_WATERMARKPROBE)
    xyzpdf_add_test(watermarkprobe
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium qpdf::libqpdf)
endif()

# Whether 书签编辑 can be a feature at all, and what it would have to keep true. QPDF's outline
# helpers are read-only -- QPDFOutlineObjectHelper cannot be constructed by a caller and has no
# setter -- so an editor here means writing /Outlines by hand: /Title /Parent /First /Last /Prev
# /Next /Count, over the four destination shapes a real file uses. The probe builds one outline in
# every shape, breaks one consistency at a time in a separate file, and reads all of them back
# through two readers: qpdf, and our own PdfiumBackend::outline() -- what the sidebar draws.
# (Until F7-E a third reader sat between them, Qt's bookmark model; it is not in this build any
# more.) See the header comment of tests/outlineprobe.cpp.
option(XYZPDF_BUILD_OUTLINEPROBE "Build the outline-editing measurement" OFF)
if(XYZPDF_BUILD_OUTLINEPROBE)
    xyzpdf_add_test(outlineprobe
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/edit/documenteditor.cpp
        LIBRARIES xyzpdf_pdfium qpdf::libqpdf)
endif()

# The performance baseline. Built with everything else and deliberately not run:
# tools/build_all_tests.py skips any target whose name contains 'bench', because
# this prints numbers instead of passing or failing, and a threshold weak enough
# to be a test is a threshold somebody will raise the first time it goes red on a
# slower machine. See the header comment in tests/perf_bench.cpp and the record it
# feeds, docs/perf.md.
option(XYZPDF_BUILD_PERF_BENCH "Build the performance baseline measurement" OFF)
if(XYZPDF_BUILD_PERF_BENCH)
    xyzpdf_add_test(perf_bench
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/document/textsearch.cpp
                src/render/rendercache.cpp
        LIBRARIES xyzpdf_pdfium)
    # The table says which release produced it, the same reason the program's own
    # PDFium version is compiled in: a number in docs/perf.md without an engine
    # version beside it cannot be re-measured against.
    target_compile_definitions(xyzpdf_perf_bench PRIVATE
        XYZPDF_PDFIUM_VERSION="${XYZPDF_PDFIUM_VERSION}")
endif()

# The tree the bookmark list is turned into, with no PDF anywhere near it.
option(XYZPDF_BUILD_OUTLINE_SMOKE "Build the bookmark tree smoke test" OFF)
if(XYZPDF_BUILD_OUTLINE_SMOKE)
    xyzpdf_add_test(outline_smoke
        SOURCES src/document/outline.cpp
                src/document/bookmarkposition.cpp
                src/document/pagemetrics.cpp)
endif()

# Folding a selection's two ends into the ranges to draw, with page lengths from
# a table: no PDF, no window, and every case that is easy to get wrong.
option(XYZPDF_BUILD_TEXTLAYER_SMOKE "Build the selection folding smoke test" OFF)
if(XYZPDF_BUILD_TEXTLAYER_SMOKE)
    xyzpdf_add_test(textlayer_smoke
        SOURCES src/document/textlayer.cpp)
endif()

# What counts as a match: case, overlapping occurrences, and the needle that is
# not there. No PDF and no window either -- the rules a reader notices are the
# rules worth pinning down.
option(XYZPDF_BUILD_TEXTSEARCH_SMOKE "Build the text search smoke test" OFF)
if(XYZPDF_BUILD_TEXTSEARCH_SMOKE)
    xyzpdf_add_test(textsearch_smoke
        SOURCES src/document/textsearch.cpp)
endif()

# Rendering a page for real, and the generation rule that drops a stale result.
option(XYZPDF_BUILD_RENDER_SMOKE "Build the page rendering smoke test" OFF)
if(XYZPDF_BUILD_RENDER_SMOKE)
    xyzpdf_add_test(render_smoke
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
        LIBRARIES xyzpdf_pdfium)
endif()

# The memory budget, and which page is evicted when it is reached.
option(XYZPDF_BUILD_CACHE_SMOKE "Build the render cache smoke test" OFF)
if(XYZPDF_BUILD_CACHE_SMOKE)
    xyzpdf_add_test(cache_smoke
        SOURCES src/render/rendercache.cpp)
endif()

# Where pages land in continuous and single-page mode.
option(XYZPDF_BUILD_LAYOUT_SMOKE "Build the page layout smoke test" OFF)
if(XYZPDF_BUILD_LAYOUT_SMOKE)
    xyzpdf_add_test(layout_smoke
        SOURCES src/view/pagelayout.cpp)
endif()

# The zoom arithmetic, including the point under the cursor staying put.
option(XYZPDF_BUILD_ZOOM_SMOKE "Build the zoom smoke test" OFF)
if(XYZPDF_BUILD_ZOOM_SMOKE)
    xyzpdf_add_test(zoom_smoke
        SOURCES src/view/zoomcontroller.cpp
                src/document/pagemetrics.cpp)
endif()

# The view and the tab together, which is where the render generation really has
# to be right.
#
# Every piece of this had a test of its own and the whole of it did not, so a
# zoom or a rotation left every page grey forever without one test noticing:
# each part was correct and one signal came before another. A test that checks
# the parts cannot catch that, which is why this one drives the real widget, the
# real renderer and the real event loop.
option(XYZPDF_BUILD_DOCUMENTTAB_SMOKE "Build the view and tab integration smoke test" OFF)
if(XYZPDF_BUILD_DOCUMENTTAB_SMOKE)
    # The tab owns the recognition flow, so the engine comes with it -- which
    # also means this test runs real Windows OCR once, on the region drag it
    # performs below.
    set(XYZPDF_DOCUMENTTAB_SMOKE_LIBRARIES Qt6::Widgets xyzpdf_pdfium Qt6::Concurrent Qt6::Network Qt6::PrintSupport qpdf::libqpdf)
    if(WIN32)
        list(APPEND XYZPDF_DOCUMENTTAB_SMOKE_LIBRARIES windowsapp advapi32)
    endif()
    xyzpdf_add_test(documenttab_smoke
        SOURCES documenttab.cpp
                src/core/windowdialogs.cpp
                src/edit/documenteditor.cpp
                src/view/pageview.cpp
                src/view/pagelayout.cpp
                src/view/zoomcontroller.cpp
                src/document/bookmarkposition.cpp
                src/document/documentfactory.cpp
                src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/document/textlayer.cpp
                src/document/textsearch.cpp
                src/export/docxwriter.cpp
                src/export/imageexport.cpp
                src/export/wordexport.cpp
                src/export/zipwriter.cpp
                src/ocr/ocrengine.cpp
                src/print/printjob.cpp
                src/translate/keystore.cpp
                src/translate/keystore_windows.cpp
                src/translate/translator.cpp
                src/ui/bookmarkdialog.cpp
                src/ui/deletepagesdialog.cpp
                src/ui/findbar.cpp
                src/ui/imageexportdialog.cpp
                src/ui/mergepagesdialog.cpp
                src/ui/movepagesdialog.cpp
                src/ui/ocrdialog.cpp
                src/ui/outlinepanel.cpp
                src/ui/printdialog.cpp
                src/ui/propertiesdialog.cpp
                src/ui/protectpagesdialog.cpp
                src/ui/rotatepagesdialog.cpp
                src/ui/searchpanel.cpp
                src/ui/splitpagesdialog.cpp
                src/ui/thumbnailpanel.cpp
                src/ui/translatedialog.cpp
                src/ui/watermarkdialog.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
                src/services/documentoutput.cpp
                src/services/filechangenotice.cpp
                src/services/ocrsession.cpp
                src/services/searchsession.cpp
                src/services/translatesession.cpp
                src/services/wordexportsession.cpp
                src/theme/thememanager.cpp
        LIBRARIES ${XYZPDF_DOCUMENTTAB_SMOKE_LIBRARIES})
endif()

# The window as a drop target: a file dragged in from a folder opens, a folder
# or a URL does not, and the action taken is a copy.
#
# This is the one test that builds the window itself, which is why it carries
# most of the program's sources. That is the point of it: whether a drag
# reaches the window at all is decided by Qt walking the widget tree and by one
# call to setAcceptDrops(), and neither is visible in the handlers.
option(XYZPDF_BUILD_MAINWINDOW_SMOKE "Build the window drag-and-drop smoke test" OFF)
if(XYZPDF_BUILD_MAINWINDOW_SMOKE)
    set(XYZPDF_MAINWINDOW_SMOKE_LIBRARIES Qt6::Widgets xyzpdf_pdfium Qt6::Concurrent Qt6::Network Qt6::PrintSupport qpdf::libqpdf)
    if(WIN32)
        list(APPEND XYZPDF_MAINWINDOW_SMOKE_LIBRARIES windowsapp advapi32)
    endif()
    xyzpdf_add_test(mainwindow_smoke
        SOURCES mainwindow.cpp
                mainwindow.ui
                resources.qrc
                documenttab.cpp
                src/cli/commandline.cpp
                src/core/languages.cpp
                src/core/recentdocuments.cpp
                src/core/thirdpartynotices.cpp
                src/core/windowdialogs.cpp
                src/document/bookmarkposition.cpp
                src/document/documentfactory.cpp
                src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/document/textlayer.cpp
                src/document/textsearch.cpp
                src/edit/documenteditor.cpp
                src/export/docxwriter.cpp
                src/export/imageexport.cpp
                src/export/wordexport.cpp
                src/export/zipwriter.cpp
                src/ocr/ocrengine.cpp
                src/print/printjob.cpp
                src/translate/keystore.cpp
                src/translate/keystore_windows.cpp
                src/translate/translator.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
                src/services/documentoutput.cpp
                src/services/filechangenotice.cpp
                src/services/ocrsession.cpp
                src/services/searchsession.cpp
                src/services/translatesession.cpp
                src/services/wordexportsession.cpp
                src/settings/settingsdialog.cpp
                src/theme/thememanager.cpp
                src/ui/documenttabbar.cpp
                src/ui/bookmarkdialog.cpp
                src/ui/deletepagesdialog.cpp
                src/ui/findbar.cpp
                src/ui/imageexportdialog.cpp
                src/ui/languagedialog.cpp
                src/ui/mergepagesdialog.cpp
                src/ui/movepagesdialog.cpp
                src/ui/ocrdialog.cpp
                src/ui/outlinepanel.cpp
                src/ui/printdialog.cpp
                src/ui/propertiesdialog.cpp
                src/ui/protectpagesdialog.cpp
                src/ui/rotatepagesdialog.cpp
                src/ui/searchpanel.cpp
                src/ui/splitpagesdialog.cpp
                src/ui/thumbnailpanel.cpp
                src/ui/translatedialog.cpp
                src/ui/thirdpartydialog.cpp
                src/ui/watermarkdialog.cpp
                src/ui/wrappingtoolbararea.cpp
                src/view/pagelayout.cpp
                src/view/pageview.cpp
                src/view/zoomcontroller.cpp
        LIBRARIES ${XYZPDF_MAINWINDOW_SMOKE_LIBRARIES})
    if(WIN32)
        target_sources(xyzpdf_mainwindow_smoke PRIVATE
            src/windows/packageidentity.cpp
            src/windows/shellintegration.cpp)
    endif()
endif()

# Reading the text in a picture, with the machine's own OCR.
#
# Windows only, because the engine is a Windows service. The test draws text it
# knows onto an image and asks for it back: a pass says the whole chain works
# here, and the failure modes it cannot have -- no language packs, no service --
# are reported as a skip rather than as a pass, because a feature that quietly
# does nothing on some machines is a feature nobody notices is missing.
option(XYZPDF_BUILD_OCR_SMOKE "Build the text recognition smoke test" OFF)
if(XYZPDF_BUILD_OCR_SMOKE AND WIN32)
    xyzpdf_add_test(ocr_smoke
        SOURCES src/ocr/ocrengine.cpp
        LIBRARIES Qt6::Concurrent windowsapp)
endif()

# The measurement behind the accuracy question: one crop, every recognizer this
# machine installs, several preprocessings, and a printed table instead of a
# verdict. See the header comment of tests/ocrprobe.cpp -- it is built and not
# run, because every line it prints is another library's answer.
option(XYZPDF_BUILD_OCR_PROBE "Build the OCR accuracy measurement probe" OFF)
if(XYZPDF_BUILD_OCR_PROBE AND WIN32)
    xyzpdf_add_test(ocrprobe
        SOURCES src/document/documentfactory.cpp
                src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/ocr/ocrengine.cpp
        LIBRARIES xyzpdf_pdfium Qt6::Concurrent windowsapp)
endif()

# A PDF read out as a Word document.
#
# Two halves that need no PDF -- rules about where one paragraph ends and the
# next begins, and a ZIP with XML in it -- plus one run over a fixture, because
# the halves agreeing with each other is exactly what a test of either half
# cannot check. The ZIP is unpacked by the test itself rather than by the writer
# that made it: a reader and a writer from the same hand agree by construction
# and prove nothing about whether Word can open the file.
option(XYZPDF_BUILD_WORDEXPORT_SMOKE "Build the Word export smoke test" OFF)
if(XYZPDF_BUILD_WORDEXPORT_SMOKE)
    xyzpdf_add_test(wordexport_smoke
        SOURCES src/export/docxwriter.cpp
                src/export/wordexport.cpp
                src/export/zipwriter.cpp
                src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES xyzpdf_pdfium)
endif()

# The arithmetic behind "select some words and translate them": the request URL
# and the response parsing, checked on bytes with no service in the loop. Network
# on purpose -- a test that reached the internet would fail on a plane and prove
# nothing about the code.
option(XYZPDF_BUILD_TRANSLATE_SMOKE "Build the translation smoke test" OFF)
if(XYZPDF_BUILD_TRANSLATE_SMOKE)
    xyzpdf_add_test(translate_smoke
        SOURCES src/translate/translator.cpp
                src/translate/keystore.cpp
        LIBRARIES Qt6::Network)
endif()

# The one claim about key storage that no in-process test can make: that
# CredWriteW / CredReadW / CredDeleteW behave the way the translate path assumes they
# do, on a real credential manager. Run by hand once -- the header of
# tests/keystoreprobe.cpp says how, and what it printed is in the commit that added
# it. The gate builds it and never runs it (build_all_tests.py skips any target whose
# name says "probe"), because an automated test that writes to the machine it runs on
# is a test that changes the user's credentials.
option(XYZPDF_BUILD_KEYSTORE_PROBE "Build the Windows credential manager probe" OFF)
if(XYZPDF_BUILD_KEYSTORE_PROBE)
    xyzpdf_add_test(keystoreprobe
        SOURCES src/translate/keystore.cpp
                src/translate/keystore_windows.cpp
        LIBRARIES advapi32)
endif()

# The arithmetic of a print job: which pages a typed range names, and how an
# n-up count becomes a grid. No printer and no document -- the painting is the
# part that needs real hardware and is checked by hand.
option(XYZPDF_BUILD_PRINT_SMOKE "Build the print arithmetic smoke test" OFF)
if(XYZPDF_BUILD_PRINT_SMOKE)
    xyzpdf_add_test(print_smoke
        SOURCES src/print/printjob.cpp
        LIBRARIES Qt6::PrintSupport)
endif()

# The per-document session record: the path-hashing and round-trip that put a
# reader back where they left off. Header-only storage over QSettings, which the
# test runner redirects to a scratch file, so this writes nothing real.
option(XYZPDF_BUILD_APPSETTINGS_SMOKE "Build the settings storage smoke test" OFF)
if(XYZPDF_BUILD_APPSETTINGS_SMOKE)
    xyzpdf_add_test(appsettings_smoke)
endif()

# The file names a page-image export writes: the 1-based page, zero-padded so a
# folder sorts them in page order, and the extension that matches the format.
option(XYZPDF_BUILD_IMAGEEXPORT_SMOKE "Build the image export naming smoke test" OFF)
if(XYZPDF_BUILD_IMAGEEXPORT_SMOKE)
    xyzpdf_add_test(imageexport_smoke
        SOURCES src/export/imageexport.cpp)
endif()

# The registry entries, and the guard on the recursive delete in front of them.
# Windows only, because none of it exists anywhere else.
option(XYZPDF_BUILD_SHELLINTEGRATION_SMOKE "Build the shell integration smoke test" OFF)
if(XYZPDF_BUILD_SHELLINTEGRATION_SMOKE AND WIN32)
    xyzpdf_add_test(shellintegration_smoke
        SOURCES src/windows/shellintegration.cpp
                src/cli/commandline.cpp
        LIBRARIES Qt6::Widgets)
endif()

# The preview probe, driven the way Explorer drives it: load the DLL, ask the
# factory for the class, run the sequence, check a window appeared.
#
# This is the half that can be asserted. Whether the shell routes .pdf previews
# to a server declared by a loosely registered package is the other half, and it
# is not answerable from here -- nor from the registry, where even a package known
# to work has no COM key to look at. So the DLL's own correctness is pinned down
# first, and the routing question is left to the log the handler writes.
#
# The DLL is passed on the command line, which tools/build_all_tests.py knows
# about; a missing argument is reported as a failure rather than skipped.
# The DLL it drives is defined further down, in the Packaging section, and is
# unconditional on Windows -- so no "if this target exists" guard here. Such a
# guard reads as caution and does the opposite: it silently drops the test, and
# the runner reports one fewer test rather than a failure.
option(XYZPDF_BUILD_PREVIEWHANDLER_SMOKE "Build the preview handler smoke test" OFF)
if(XYZPDF_BUILD_PREVIEWHANDLER_SMOKE AND WIN32)
    xyzpdf_add_test(previewhandler_smoke LIBRARIES ole32)
    # The test watches which engine DLL ends up in its process: pdfium.dll, and
    # not the Qt6Pdf.dll that used to be here. A handler wired back to the other
    # backend -- the failure F7-D exists to prevent -- reddens those two lines.
endif()

# The thumbnail provider, driven the same way and for the same reason: it is a DLL
# whose only caller is a shell that cannot be instrumented from here.
#
# What this one pins down that the preview test cannot: the bitmap comes back at a
# size that fits the requested box with the page's aspect kept, and its pixels are
# mostly white with ink in them. A provider that returns a black square, or a
# stretched one, or nothing at all, passes a compile and fails a folder.
option(XYZPDF_BUILD_THUMBNAILPROVIDER_SMOKE "Build the thumbnail provider smoke test" OFF)
if(XYZPDF_BUILD_THUMBNAILPROVIDER_SMOKE AND WIN32)
    xyzpdf_add_test(thumbnailprovider_smoke LIBRARIES ole32 gdi32)
endif()

# The context menu verb, both halves: the command line building is compiled in and
# called, and the COM object is loaded from the DLL and asked the eight questions
# the shell asks.
option(XYZPDF_BUILD_SHELLVERB_SMOKE "Build the shell verb smoke test" OFF)
if(XYZPDF_BUILD_SHELLVERB_SMOKE AND WIN32)
    xyzpdf_add_test(shellverb_smoke
        SOURCES src/windows/shellverb.cpp
        LIBRARIES ole32 shell32)
endif()

# ---------------------------------------------------------------------------
# Measuring
# ---------------------------------------------------------------------------

# tools/pdfprobe.cpp, which prints numbers rather than passing or failing.
#
# It is here as an option like the tests because it has to be built against the
# same engine as the program, and it is built into the build directory rather
# than into tests/ so that the test runner never confuses it for a test --
# though its name would have it skipped anyway.
#
# It exists because "the program is slow with my document" cannot be answered
# from a description of the document. The numbers in docs/PLAN.md §3.3 -- how
# many pixels a page is, how long it takes, how many fit in the budget -- come
# from this.
option(XYZPDF_BUILD_PDFPROBE "Build the document measurement tool" OFF)
if(XYZPDF_BUILD_PDFPROBE)
    qt_add_executable(xyzpdf_pdfprobe
        tools/pdfprobe.cpp
        src/document/documentfactory.cpp
        src/document/outline.cpp
        src/document/pagemetrics.cpp
        src/document/textsearch.cpp
        src/render/rendercache.cpp)
    target_link_libraries(xyzpdf_pdfprobe PRIVATE Qt6::Core Qt6::Gui)
    # It answers "what does the shipped reader see", so it follows the program's
    # engine rather than taking one of its own.
    xyzpdf_use_document_backend(xyzpdf_pdfprobe)
    add_custom_command(TARGET xyzpdf_pdfprobe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll" "$<TARGET_FILE_DIR:xyzpdf_pdfprobe>"
        VERBATIM)
    set_target_properties(xyzpdf_pdfprobe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# The engine's own behaviour, asserted rather than compared: every page of every
# fixture opened, sized and read through PdfiumBackend, the rotation and the band
# geometry the view depends on, the sentences a failed load gets, and the
# file-handle behaviour F7 changed (docs/handoff-03.md F7 item 3 /
# docs/pdfium-plan.md R7) -- a document on screen no longer stops anybody from
# deleting or replacing its file.
#
# This is where the F5 probe's side-by-side comparisons lived, and where the Qt
# column of them went when F7-E retired the second backend. What is left is the
# half that does not need another engine to be read against -- which is the half
# that keeps being true now that there is only one.
option(XYZPDF_BUILD_PDFIUMBACKEND_SMOKE "Build the PDFium backend smoke test" OFF)
if(XYZPDF_BUILD_PDFIUMBACKEND_SMOKE)
    xyzpdf_add_test(pdfiumbackend_smoke
        SOURCES src/document/pdfiumbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/services/filechangenotice.cpp
        LIBRARIES xyzpdf_pdfium Qt6::Widgets)
    target_compile_definitions(xyzpdf_pdfiumbackend_smoke PRIVATE
        XYZPDF_PDFIUM_VERSION="${XYZPDF_PDFIUM_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------

# The preview handler: Explorer's pane, showing page 1 of a PDF.
#
# It is a DLL loaded into DllHost.exe by way of the com:SurrogateServer entry in
# the package manifest, which is why it is here rather than inside xyzpdf.exe:
# the pane has to work when the reader is not running.
#
# It goes through the same Document seam as the reader, so a page cannot look one
# way in the window and another way in the pane. That is three source files of the
# document layer plus whichever backend the build carries -- the cost of the seam
# being the only place an engine is named, and the proof that it is a usable seam,
# being two more consumers that asked for no changes to it.
#
# QT_NO_VERSION_TAGGING: Qt's version tag is an imported *data* symbol, and a data
# symbol cannot be delay loaded -- with it, /DELAYLOAD:Qt6Core.dll is rejected
# outright. The tag exists to produce a clearer message when a binary meets the
# wrong Qt; this DLL loads the Qt that sits beside it, by full path, so there is
# no wrong Qt for it to meet. See previewdependencies.cpp for why the full path.
if(WIN32)
    # The engine DLL the two handlers delay-load. pdfium.dll is self-contained
    # (docs/pdfium-plan.md sec.6.1: it loads with nothing but System32 on the
    # search path, and brings in no other module), and the reason to delay-load it
    # is the same one as for Qt6Core and Qt6Gui: the library lives beside our DLL,
    # not beside DllHost.exe.
    set(XYZPDF_ENGINE_DELAYLOAD /DELAYLOAD:pdfium.dll)

    add_library(xyzpdfpreview SHARED
        src/windows/previewhandler.cpp
        src/windows/previewdependencies.h
        src/windows/previewdependencies.cpp
        src/windows/previewhandler.def
        src/document/documentfactory.cpp
        src/document/outline.cpp
        src/document/pagemetrics.cpp)
    # The engine, and only it: the package carries one backend, and the pane has to
    # show the one the window shows. Before F7-D this list named qtpdfbackend.cpp
    # directly, which meant a PDFium-configured package still put Qt PDF in the
    # pane.
    xyzpdf_use_document_backend(xyzpdfpreview)
    target_link_libraries(xyzpdfpreview PRIVATE
        Qt6::Core Qt6::Gui
        ole32 shell32 user32 gdi32 delayimp)
    target_compile_definitions(xyzpdfpreview PRIVATE QT_NO_VERSION_TAGGING)
    # Every library that is ours rather than Windows', because the loader looks
    # beside the executable and DllHost.exe lives in System32. The list is what
    # "dumpbin /nologo /dependents xyzpdfpreview.dll" reports minus the system
    # DLLs, and previewhandler_smoke loads the DLL with PATH restricted to
    # System32 so that a missed entry fails here rather than in Explorer.
    target_link_options(xyzpdfpreview PRIVATE
        /DELAYLOAD:Qt6Core.dll
        /DELAYLOAD:Qt6Gui.dll
        ${XYZPDF_ENGINE_DELAYLOAD})
    set_target_properties(xyzpdfpreview PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    # The delay-load hook finds the engine beside this DLL. The program's own
    # POST_BUILD copy also puts pdfium.dll here, but a target that can be built on
    # its own should not depend on another target having been built first.
    add_custom_command(TARGET xyzpdfpreview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll" "$<TARGET_FILE_DIR:xyzpdfpreview>"
        VERBATIM)
    install(TARGETS xyzpdfpreview
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # The thumbnail provider: what a PDF looks like in Explorer's icon views.
    #
    # Its own DLL because that is the layout docs/PLAN.md §3.3 prescribes, and
    # because COM gives each surrogate CLSID its own DllHost process -- the two are
    # never in the same process whatever the file layout, so splitting them costs
    # nothing but the three document sources below plus the configured backend,
    # which is the price of the seam being the only place an engine is named.
    #
    # The delay-load options and QT_NO_VERSION_TAGGING are the same argument as the
    # one recorded on the target above; previewdependencies.cpp is compiled into
    # both, and is the only thing in either handler that is shared as source.
    add_library(xyzpdfthumbnail SHARED
        src/windows/thumbnailprovider.cpp
        src/windows/previewdependencies.h
        src/windows/previewdependencies.cpp
        src/windows/thumbnailprovider.def
        src/document/documentfactory.cpp
        src/document/outline.cpp
        src/document/pagemetrics.cpp)
    xyzpdf_use_document_backend(xyzpdfthumbnail)
    target_link_libraries(xyzpdfthumbnail PRIVATE
        Qt6::Core Qt6::Gui
        ole32 gdi32 delayimp)
    target_compile_definitions(xyzpdfthumbnail PRIVATE QT_NO_VERSION_TAGGING)
    target_link_options(xyzpdfthumbnail PRIVATE
        /DELAYLOAD:Qt6Core.dll
        /DELAYLOAD:Qt6Gui.dll
        ${XYZPDF_ENGINE_DELAYLOAD})
    set_target_properties(xyzpdfthumbnail PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_custom_command(TARGET xyzpdfthumbnail POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll" "$<TARGET_FILE_DIR:xyzpdfthumbnail>"
        VERBATIM)
    install(TARGETS xyzpdfthumbnail
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # The context menu verb: the row that says 用 XYZPDF 打开.
    #
    # No Qt, and deliberately so. The other two handlers read a PDF and so carry a
    # renderer; this one reads a file extension and starts a process, and it is
    # activated on somebody's right-click, where loading Qt would be the cost of a
    # menu opening. That is why it is a third DLL rather than a class inside the
    # preview one, and why nothing below mentions delay loading.
    add_library(xyzpdfshell SHARED
        src/windows/shellcommand.cpp
        src/windows/shellverb.h
        src/windows/shellverb.cpp
        src/windows/shellcommand.def)
    target_link_libraries(xyzpdfshell PRIVATE ole32 shell32)
    set_target_properties(xyzpdfshell PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    install(TARGETS xyzpdfshell
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS xyzpdf
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
# The engine, into the same install tree the package is staged from. The
# POST_BUILD copy above is what lets the program be double-clicked in the build
# directory; this is what lets `cmake --install` produce a tree that can run at
# all. PDFium is not Qt's to deploy, so nothing else puts it anywhere.
install(FILES "${XYZPDF_PDFIUM_ROOT}/bin/pdfium.dll"
    DESTINATION ${CMAKE_INSTALL_BINDIR})
# The same exclusion as the POST_BUILD windeployqt line above, and it has to be
# said twice: this is a second, separate run of the deploy tool -- the one the
# package is staged from -- and it knows nothing about the flags of the first.
# Without it the §116 package shipped imageformats\qpdf.dll and, because that
# plugin links it, Qt6Pdf.dll: 4.6 MB of a second PDF engine nothing calls,
# beside the pdfium.dll F7 replaced it with (the payload diff said "gained 1,
# lost 0").
qt_generate_deploy_app_script(
    TARGET xyzpdf
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_TOOL_OPTIONS --exclude-plugins qpdf
)
install(SCRIPT ${deploy_script})

# One qt_add_translations call for the whole project, at the end of the file
# where every target it names already exists.
#
# XYZip had three, each naming the same .ts files -- one for the program and one
# per test that carries the catalogues. That is not harmless duplication: every
# call creates its own lupdate target, and "update_translations" runs them all,
# so several lupdate processes write the same files at once. What came out had
# two </TS> in it and no longer parsed.
#
# mainwindow_smoke joined for the menu mnemonic check: that round builds a window
# while the English catalogue is installed, so it needs the compiled .qm inside the
# test binary and not only inside the program. Nothing here calls tr() or
# translate() with a new literal, so the catalogue gains no entry from the listing.
set(XYZPDF_TRANSLATION_TARGETS xyzpdf)
foreach(translated_test i18n_smoke mainwindow_smoke)
    if (TARGET xyzpdf_${translated_test})
        list(APPEND XYZPDF_TRANSLATION_TARGETS xyzpdf_${translated_test})
    endif()
endforeach()

set(XYZPDF_TS_FILES
    Resources/translations/xyzpdf_en.ts
)

# The document engine's own sentences.
#
# Until F7-E this list named both backend sources, whichever one the build
# compiled and whichever one it did not: there is one catalogue, and -no-obsolete
# would otherwise drop the second backend's entire context on every update, so
# switching back would silently return Chinese sentences to an English window.
# There is one backend now, it is a source of the program target, and lupdate
# reads it with the rest of them.
qt_add_translations(
    TARGETS ${XYZPDF_TRANSLATION_TARGETS}
    TS_FILES ${XYZPDF_TS_FILES}
    RESOURCE_PREFIX "/translations"
    LUPDATE_OPTIONS -no-obsolete
)
