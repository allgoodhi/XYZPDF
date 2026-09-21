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
if(MSVC)
    add_compile_options(/utf-8)
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
# Qt PDF is PDFium behind a Qt-shaped API and is LGPLv3, so it stays a DLL of
# its own like every other LGPL dependency here -- linked, never absorbed.
# PdfWidgets is deliberately NOT required: the plan rules out QPdfView, and
# asking for the module would make it tempting.
find_package(Qt6 6.7 REQUIRED COMPONENTS Pdf)

# vcpkg installs into ../vcpkg_installed/<triplet> from the repository root, and
# Qt Creator does not receive the vcpkg toolchain file from a normal Desktop
# kit. Prefer that installation when it is there, while still allowing a regular
# vcpkg toolchain configuration to supply packages from elsewhere.
#
# v1 has no dependencies of its own -- everything it uses is Qt -- so this finds
# nothing today. It is here because the paths and the triplet are the part that
# is forgotten when the first dependency arrives, and because XYZip's build is
# laid out the same way.
set(XYZPDF_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
if (XYZPDF_VCPKG_TRIPLET STREQUAL "")
    set(XYZPDF_VCPKG_TRIPLET "x64-windows")
endif()
set(XYZPDF_LOCAL_VCPKG_PREFIX
    "${CMAKE_CURRENT_LIST_DIR}/../vcpkg_installed/${XYZPDF_VCPKG_TRIPLET}")
if (EXISTS "${XYZPDF_LOCAL_VCPKG_PREFIX}/share")
    list(PREPEND CMAKE_PREFIX_PATH "${XYZPDF_LOCAL_VCPKG_PREFIX}")
endif()

if(MSVC)
    # rc.exe decodes its input with the system ANSI codepage unless told
    # otherwise, so the Chinese in xyzpdf.rc comes out of a UTF-8 file as GBK
    # mojibake -- XYZip's Details tab read "XYZip 鍘嬬缉杞欢". The damage is done
    # at compile time and nothing downstream can undo it.
    string(APPEND CMAKE_RC_FLAGS " /c65001")
endif()

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

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

    src/document/document.h
    src/document/docmetadata.h
    src/document/outline.h
    src/document/outline.cpp
    src/document/pagemetrics.h
    src/document/pagemetrics.cpp
    src/document/qtpdfbackend.h
    src/document/qtpdfbackend.cpp
    src/document/textlayer.h
    src/document/textlayer.cpp
    src/document/textsearch.h
    src/document/textsearch.cpp

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

    src/settings/settingsdialog.h
    src/settings/settingsdialog.cpp

    src/theme/theme.h
    src/theme/thememanager.h
    src/theme/thememanager.cpp

    src/translate/translator.h
    src/translate/translator.cpp

    src/ui/documenttabbar.h
    src/ui/documenttabbar.cpp
    src/ui/findbar.h
    src/ui/findbar.cpp
    src/ui/imageexportdialog.h
    src/ui/imageexportdialog.cpp
    src/ui/languagedialog.h
    src/ui/languagedialog.cpp
    src/ui/ocrdialog.h
    src/ui/ocrdialog.cpp
    src/ui/outlinepanel.h
    src/ui/outlinepanel.cpp
    src/ui/printdialog.h
    src/ui/printdialog.cpp
    src/ui/thirdpartydialog.h
    src/ui/thirdpartydialog.cpp
    src/ui/thumbnailpanel.h
    src/ui/thumbnailpanel.cpp
    src/ui/translatedialog.h
    src/ui/translatedialog.cpp
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

target_link_libraries(xyzpdf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::Pdf
    Qt6::Concurrent
    Qt6::PrintSupport
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
    if(XYZPDF_WINDEPLOYQT)
        add_custom_command(TARGET xyzpdf POST_BUILD
            COMMAND "${XYZPDF_WINDEPLOYQT}"
                    --no-translations
                    --no-compiler-runtime
                    --no-system-d3d-compiler
                    --no-opengl-sw
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
        SOURCES src/core/languages.cpp resources.qrc)
endif()

# Opening a document: pages, sizes, text, outline, and a file that is not a PDF.
option(XYZPDF_BUILD_DOCUMENT_SMOKE "Build the document backend smoke test" OFF)
if(XYZPDF_BUILD_DOCUMENT_SMOKE)
    xyzpdf_add_test(document_smoke
        SOURCES src/document/qtpdfbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES Qt6::Pdf)
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
        SOURCES src/document/qtpdfbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/document/textsearch.cpp
                src/render/rendercache.cpp
        LIBRARIES Qt6::Pdf)
endif()

# The tree the bookmark list is turned into, with no PDF anywhere near it.
option(XYZPDF_BUILD_OUTLINE_SMOKE "Build the bookmark tree smoke test" OFF)
if(XYZPDF_BUILD_OUTLINE_SMOKE)
    xyzpdf_add_test(outline_smoke
        SOURCES src/document/outline.cpp)
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
        SOURCES src/document/qtpdfbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
        LIBRARIES Qt6::Pdf)
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
    set(XYZPDF_DOCUMENTTAB_SMOKE_LIBRARIES Qt6::Widgets Qt6::Pdf Qt6::Concurrent Qt6::Network Qt6::PrintSupport)
    if(WIN32)
        list(APPEND XYZPDF_DOCUMENTTAB_SMOKE_LIBRARIES windowsapp)
    endif()
    xyzpdf_add_test(documenttab_smoke
        SOURCES documenttab.cpp
                src/core/windowdialogs.cpp
                src/view/pageview.cpp
                src/view/pagelayout.cpp
                src/view/zoomcontroller.cpp
                src/document/qtpdfbackend.cpp
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
                src/translate/translator.cpp
                src/ui/findbar.cpp
                src/ui/imageexportdialog.cpp
                src/ui/ocrdialog.cpp
                src/ui/outlinepanel.cpp
                src/ui/printdialog.cpp
                src/ui/thumbnailpanel.cpp
                src/ui/translatedialog.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
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
    set(XYZPDF_MAINWINDOW_SMOKE_LIBRARIES Qt6::Widgets Qt6::Pdf Qt6::Concurrent Qt6::Network Qt6::PrintSupport)
    if(WIN32)
        list(APPEND XYZPDF_MAINWINDOW_SMOKE_LIBRARIES windowsapp)
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
                src/document/qtpdfbackend.cpp
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
                src/translate/translator.cpp
                src/render/pagerenderer.cpp
                src/render/rendercache.cpp
                src/settings/settingsdialog.cpp
                src/theme/thememanager.cpp
                src/ui/documenttabbar.cpp
                src/ui/findbar.cpp
                src/ui/imageexportdialog.cpp
                src/ui/languagedialog.cpp
                src/ui/ocrdialog.cpp
                src/ui/outlinepanel.cpp
                src/ui/printdialog.cpp
                src/ui/thumbnailpanel.cpp
                src/ui/translatedialog.cpp
                src/ui/thirdpartydialog.cpp
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
                src/document/qtpdfbackend.cpp
                src/document/outline.cpp
                src/document/pagemetrics.cpp
        LIBRARIES Qt6::Pdf)
endif()

# The arithmetic behind "select some words and translate them": the request URL
# and the response parsing, checked on bytes with no service in the loop. Network
# on purpose -- a test that reached the internet would fail on a plane and prove
# nothing about the code.
option(XYZPDF_BUILD_TRANSLATE_SMOKE "Build the translation smoke test" OFF)
if(XYZPDF_BUILD_TRANSLATE_SMOKE)
    xyzpdf_add_test(translate_smoke
        SOURCES src/translate/translator.cpp
        LIBRARIES Qt6::Network)
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

# ---------------------------------------------------------------------------
# Measuring
# ---------------------------------------------------------------------------

# tools/pdfprobe.cpp, which prints numbers rather than passing or failing.
#
# It is here as an option like the tests because it has to be built against the
# same Qt PDF as the program, and it is built into the build directory rather
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
        src/document/qtpdfbackend.cpp
        src/document/outline.cpp
        src/document/pagemetrics.cpp
        src/document/textsearch.cpp
        src/render/rendercache.cpp)
    target_link_libraries(xyzpdf_pdfprobe PRIVATE Qt6::Core Qt6::Gui Qt6::Pdf)
    set_target_properties(xyzpdf_pdfprobe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------

install(TARGETS xyzpdf
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
qt_generate_deploy_app_script(
    TARGET xyzpdf
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
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
set(XYZPDF_TRANSLATION_TARGETS xyzpdf)
foreach(translated_test i18n_smoke)
    if (TARGET xyzpdf_${translated_test})
        list(APPEND XYZPDF_TRANSLATION_TARGETS xyzpdf_${translated_test})
    endif()
endforeach()

set(XYZPDF_TS_FILES
    Resources/translations/xyzpdf_en.ts
)

qt_add_translations(
    TARGETS ${XYZPDF_TRANSLATION_TARGETS}
    TS_FILES ${XYZPDF_TS_FILES}
    RESOURCE_PREFIX "/translations"
    LUPDATE_OPTIONS -no-obsolete
)
