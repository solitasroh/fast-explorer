cmake_minimum_required(VERSION 3.24)
project(FastExplorer
  VERSION 0.6.1
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32)
  message(FATAL_ERROR "FastExplorer targets Windows only.")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "FastExplorer requires the MSVC toolchain.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

add_compile_definitions(
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
)

add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)

set(FE_APP_SOURCES
  src/app/main.cpp
  src/app/app-services.cpp
  src/app/app-services.h
  src/ui/address-bar-popup.cpp
  src/ui/address-bar-popup.h
  src/ui/main-window.cpp
  src/ui/main-window.h
  src/ui/messages.h
  src/ui/column-formatter.cpp
  src/ui/column-formatter.h
  src/ui/extension-icon-cache.cpp
  src/ui/extension-icon-cache.h
  src/ui/adapters/local-settings-store.cpp
  src/ui/adapters/local-settings-store.h
  src/ui/adapters/shell-change-notifier.cpp
  src/ui/adapters/shell-change-notifier.h
  src/ui/adapters/shell-clipboard.cpp
  src/ui/adapters/shell-clipboard.h
  src/ui/adapters/shell-context-menu-adapter.cpp
  src/ui/adapters/shell-context-menu-adapter.h
  src/ui/adapters/shell-drag-drop.cpp
  src/ui/adapters/shell-drag-drop.h
  src/ui/adapters/shell-item-dispatcher.cpp
  src/ui/adapters/shell-item-dispatcher.h
  src/ui/adapters/shell-item-source.cpp
  src/ui/adapters/shell-item-source.h
  src/ui/folder-name.cpp
  src/ui/folder-name.h
  src/ui/format-cache.cpp
  src/ui/format-cache.h
  src/ui/icon-cache.cpp
  src/ui/icon-cache.h
  src/ui/icon-cache-coordinator.cpp
  src/ui/icon-cache-coordinator.h
  src/ui/icon-provider.cpp
  src/ui/icon-provider.h
  src/ui/jthread-utils.h
  src/ui/label-edit-controller.cpp
  src/ui/label-edit-controller.h
  src/ui/listview-group-callback.cpp
  src/ui/listview-group-callback.h
  src/ui/shell-worker.cpp
  src/ui/shell-worker.h
  src/ui/filter-pattern.cpp
  src/ui/filter-pattern.h
  src/ui/search-popup.cpp
  src/ui/search-popup.h
  src/ui/pane-controller.cpp
  src/ui/pane-controller.h
  src/ui/pane-sort-coordinator.cpp
  src/ui/pane-sort-coordinator.h
  src/ui/result-channel.h
  src/ui/selection-sync.cpp
  src/ui/selection-sync.h
  src/ui/shell-context-menu.cpp
  src/ui/shell-context-menu.h
  src/ui/clipboard-ops.cpp
  src/ui/clipboard-ops.h
  src/ui/cut-state-tracker.cpp
  src/ui/cut-state-tracker.h
  src/ui/drop-source.cpp
  src/ui/drop-source.h
  src/ui/drop-target.cpp
  src/ui/drop-target.h
  src/ui/shell-bind.cpp
  src/ui/shell-bind.h
  src/ui/status-text.cpp
  src/ui/status-text.h
  src/ui/stall-probe.cpp
  src/ui/stall-probe.h
  src/core/directory-enumerator.cpp
  src/core/directory-enumerator.h
  src/core/file-entry.h
  src/core/file-model-store.cpp
  src/core/file-model-store.h
  src/core/file-sort.cpp
  src/core/file-sort.h
  src/core/file-grouping.cpp
  src/core/file-grouping.h
  src/core/fs-backend.h
  src/core/fs-watcher.cpp
  src/core/fs-watcher.h
  src/core/memory-fs-backend.cpp
  src/core/memory-fs-backend.h
  src/core/name-arena.cpp
  src/core/name-arena.h
  src/core/path-utils.cpp
  src/core/path-utils.h
  src/core/perf-tracker.cpp
  src/core/perf-tracker.h
  src/core/ring-logger.cpp
  src/core/ring-logger.h
  src/core/crash-handler.cpp
  src/core/crash-handler.h
  src/core/process-memory.cpp
  src/core/process-memory.h
  src/core/settings-store.cpp
  src/core/settings-store.h
  src/core/text-utf.cpp
  src/core/text-utf.h
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

# ---------------------------------------------------------------------------
# WinSparkle — auto-update via appcast.xml + Ed25519 signed releases.
# Fetched as a prebuilt binary from the official GitHub release; pinned by
# SHA256 so build is reproducible and tamper-evident. Bump WINSPARKLE_VERSION
# and WINSPARKLE_URL_HASH together when upgrading.
# ---------------------------------------------------------------------------
include(FetchContent)
set(WINSPARKLE_VERSION "0.9.3")
set(WINSPARKLE_URL_HASH "SHA256=745985F41D2AB26B2D5A1CF87D76E4ED851039DB19038E50610EB25EA0B73772")
FetchContent_Declare(winsparkle
  URL      "https://github.com/vslavik/winsparkle/releases/download/v${WINSPARKLE_VERSION}/WinSparkle-${WINSPARKLE_VERSION}.zip"
  URL_HASH ${WINSPARKLE_URL_HASH}
)
FetchContent_MakeAvailable(winsparkle)

set(WINSPARKLE_INCLUDE_DIR "${winsparkle_SOURCE_DIR}/include")
set(WINSPARKLE_LIB         "${winsparkle_SOURCE_DIR}/x64/Release/WinSparkle.lib")
set(WINSPARKLE_DLL         "${winsparkle_SOURCE_DIR}/x64/Release/WinSparkle.dll")

add_library(WinSparkle::WinSparkle SHARED IMPORTED)
set_target_properties(WinSparkle::WinSparkle PROPERTIES
  IMPORTED_LOCATION             "${WINSPARKLE_DLL}"
  IMPORTED_IMPLIB               "${WINSPARKLE_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${WINSPARKLE_INCLUDE_DIR}"
)

# Appcast feed and EdDSA public key are injected as preprocessor macros so the
# repository contains no environment-specific URLs and no embedded key (key
# stays as a release-time/runtime parameter, never committed).
set(FE_APPCAST_URL "https://solitasroh.github.io/fast-explorer/appcast.xml"
    CACHE STRING "URL of the WinSparkle appcast.xml feed")
set(FE_EDDSA_PUBLIC_KEY "IwpcrVIahO+HGUSIdhAHDi7xKN6dydtALIR2hHVONx8="
    CACHE STRING
    "Base64-encoded EdDSA (Ed25519) public key used by WinSparkle to verify the appcast — must match the ED25519_PRIVATE_KEY GitHub Secret used at release time. Override locally for forks.")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/FastExplorer.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/resources/FastExplorer.rc
  @ONLY
)

set(FE_RESOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/resources/FastExplorer.rc
)

# ---------------------------------------------------------------------------
# winui_lite: reusable Win32 UI building blocks. Knows nothing about file
# explorers, the shell, or this app's domain. Consumed by FastExplorer and
# (later) by examples/minimal-window. See docs/refactor-plan.html for the
# port/adapter layout being built up step by step.
# ---------------------------------------------------------------------------
add_library(winui_lite STATIC
  lib/winui_lite/chrome/cmd-packing.h
  lib/winui_lite/chrome/com-raii.h
  lib/winui_lite/chrome/command-router.cpp
  lib/winui_lite/chrome/command-router.h
  lib/winui_lite/chrome/dark-scrollbar-hook.cpp
  lib/winui_lite/chrome/dark-scrollbar-hook.h
  lib/winui_lite/chrome/dispinfo-histogram.cpp
  lib/winui_lite/chrome/dispinfo-histogram.h
  lib/winui_lite/chrome/dpi-scale.h
  lib/winui_lite/chrome/layout-orientation.h
  lib/winui_lite/chrome/layout-preset.h
  lib/winui_lite/chrome/pane-layout.cpp
  lib/winui_lite/chrome/pane-layout.h
  lib/winui_lite/chrome/pane-manager.h
  lib/winui_lite/chrome/pane-splitter.cpp
  lib/winui_lite/chrome/pane-splitter.h
  lib/winui_lite/chrome/pane-toolbar-row.cpp
  lib/winui_lite/chrome/pane-toolbar-row.h
  lib/winui_lite/chrome/splitter-ratios.h
  lib/winui_lite/chrome/splitter-resize.h
  lib/winui_lite/chrome/status-bar.cpp
  lib/winui_lite/chrome/status-bar.h
  lib/winui_lite/chrome/theme-watcher.cpp
  lib/winui_lite/chrome/theme-watcher.h
  lib/winui_lite/chrome/window-base.cpp
  lib/winui_lite/chrome/window-base.h
  lib/winui_lite/ports/change-notifier.h
  lib/winui_lite/ports/clipboard-backend.h
  lib/winui_lite/ports/context-menu.h
  lib/winui_lite/ports/drag-drop-backend.h
  lib/winui_lite/ports/item-activator.h
  lib/winui_lite/ports/item-dispatcher.h
  lib/winui_lite/ports/item-source.h
  lib/winui_lite/ports/settings-store.h
  lib/winui_lite/widgets/address-input.cpp
  lib/winui_lite/widgets/address-input.h
)

target_include_directories(winui_lite PUBLIC lib)

target_compile_features(winui_lite PUBLIC cxx_std_20)

# Enforces rule #1 of the refactor plan: lib/winui_lite/ may not include
# any header under src/. Greps the lib tree on every build of winui_lite
# and fails the build if a backwards edge appears.
add_custom_target(check_winui_lite_isolation
  COMMAND ${CMAKE_COMMAND}
    -DLIB_DIR=${CMAKE_CURRENT_SOURCE_DIR}/lib/winui_lite
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-winui-lite-isolation.cmake
  VERBATIM
)
add_dependencies(winui_lite check_winui_lite_isolation)

add_executable(FastExplorer WIN32
  ${FE_APP_SOURCES}
  ${FE_RESOURCES}
)

target_include_directories(FastExplorer PRIVATE src)

target_compile_definitions(FastExplorer PRIVATE
  FE_APPCAST_URL="${FE_APPCAST_URL}"
  FE_EDDSA_PUBLIC_KEY="${FE_EDDSA_PUBLIC_KEY}"
)

target_link_libraries(FastExplorer PRIVATE
  winui_lite
  comctl32
  shell32
  shlwapi
  ole32
  oleaut32
  user32
  gdi32
  uxtheme
  dbghelp
  propsys
  mpr       # WNetEnumResource for \\server share listing (matches `net view`)
  WinSparkle::WinSparkle
)

# Copy WinSparkle.dll next to FastExplorer.exe so the app starts without
# the user adjusting PATH — also covered by install(FILES ...) below for
# the packaged ZIP.
add_custom_command(TARGET FastExplorer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WINSPARKLE_DLL}" "$<TARGET_FILE_DIR:FastExplorer>/WinSparkle.dll"
  COMMENT "Staging WinSparkle.dll next to FastExplorer.exe"
)

set_target_properties(FastExplorer PROPERTIES
  VS_GLOBAL_EnableDpiAwareness "PerMonitor"
)

# Manifest is embedded through the .rc file; disable the default generated manifest.
target_link_options(FastExplorer PRIVATE "/MANIFEST:NO")

# ---------------------------------------------------------------------------
# core-tests: self-contained unit test binary (see tests/test-harness.h).
# Linked directly against the source files it exercises so test runs do not
# depend on a separate object library yet. Once the test suite grows we will
# extract a CMake OBJECT library shared between FastExplorer and core-tests.
# ---------------------------------------------------------------------------
add_executable(core-tests
  tests/test-main.cpp
  tests/path-utils-tests.cpp
  tests/file-entry-tests.cpp
  tests/name-arena-tests.cpp
  tests/fs-backend-tests.cpp
  tests/memory-fs-backend-tests.cpp
  tests/win32-fs-backend-tests.cpp
  tests/file-model-store-tests.cpp
  tests/directory-enumerator-tests.cpp
  tests/bench-cli-tests.cpp
  tests/dataset-generator-tests.cpp
  tests/enumeration-bench-tests.cpp
  tests/head-to-head-bench-tests.cpp
  tests/ui-messages-tests.cpp
  tests/pane-controller-tests.cpp
  tests/column-formatter-tests.cpp
  tests/format-cache-tests.cpp
  tests/dpi-scale-tests.cpp
  tests/status-text-tests.cpp
  tests/stall-probe-tests.cpp
  tests/file-sort-tests.cpp
  tests/file-grouping-tests.cpp
  tests/extension-icon-cache-tests.cpp
  tests/folder-name-tests.cpp
  tests/icon-cache-tests.cpp
  tests/icon-provider-tests.cpp
  tests/pane-sort-coordinator-tests.cpp
  tests/shell-worker-tests.cpp
  tests/result-channel-tests.cpp
  tests/perf-tracker-tests.cpp
  tests/bench-json-tests.cpp
  tests/dispinfo-histogram-tests.cpp
  tests/process-memory-tests.cpp
  tests/selection-sync-tests.cpp
  tests/label-edit-controller-tests.cpp
  tests/settings-store-tests.cpp
  tests/splitter-ratios-tests.cpp
  tests/command-router-tests.cpp
  tests/ports-contract-tests.cpp
  tests/splitter-resize-tests.cpp
  tests/pane-manager-tests.cpp
  tests/pane-layout-tests.cpp
  tests/layout-preset-tests.cpp
  tests/filter-pattern-tests.cpp
  tests/bench-fs-helper.h
  src/ui/messages.h
  src/ui/column-formatter.cpp
  src/ui/column-formatter.h
  src/ui/extension-icon-cache.cpp
  src/ui/extension-icon-cache.h
  src/ui/folder-name.cpp
  src/ui/folder-name.h
  src/ui/format-cache.cpp
  src/ui/format-cache.h
  src/ui/icon-cache.cpp
  src/ui/icon-cache.h
  src/ui/icon-cache-coordinator.cpp
  src/ui/icon-cache-coordinator.h
  src/ui/icon-provider.cpp
  src/ui/icon-provider.h
  src/ui/jthread-utils.h
  src/ui/label-edit-controller.cpp
  src/ui/label-edit-controller.h
  src/ui/shell-worker.cpp
  src/ui/shell-worker.h
  src/ui/filter-pattern.cpp
  src/ui/filter-pattern.h
  src/ui/search-popup.cpp
  src/ui/search-popup.h
  src/ui/pane-controller.cpp
  src/ui/pane-controller.h
  src/ui/pane-sort-coordinator.cpp
  src/ui/pane-sort-coordinator.h
  src/ui/result-channel.h
  src/ui/selection-sync.cpp
  src/ui/selection-sync.h
  src/ui/status-text.cpp
  src/ui/status-text.h
  src/ui/stall-probe.cpp
  src/ui/stall-probe.h
  src/bench/bench-cli.cpp
  src/bench/bench-cli.h
  src/bench/bench-json.cpp
  src/bench/bench-json.h
  src/bench/dataset-generator.cpp
  src/bench/dataset-generator.h
  src/bench/enumeration-bench.cpp
  src/bench/enumeration-bench.h
  src/bench/head-to-head-bench.cpp
  src/bench/head-to-head-bench.h
  src/core/directory-enumerator.cpp
  src/core/directory-enumerator.h
  src/core/file-entry.h
  src/core/file-model-store.cpp
  src/core/file-model-store.h
  src/core/file-sort.cpp
  src/core/file-sort.h
  src/core/file-grouping.cpp
  src/core/file-grouping.h
  src/core/fs-backend.h
  src/core/fs-watcher.cpp
  src/core/fs-watcher.h
  src/core/memory-fs-backend.cpp
  src/core/memory-fs-backend.h
  src/core/name-arena.cpp
  src/core/name-arena.h
  src/core/path-utils.cpp
  src/core/perf-tracker.cpp
  src/core/perf-tracker.h
  src/core/process-memory.cpp
  src/core/process-memory.h
  src/core/ring-logger.cpp
  src/core/ring-logger.h
  src/core/settings-store.cpp
  src/core/settings-store.h
  src/core/text-utf.cpp
  src/core/text-utf.h
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

target_include_directories(core-tests PRIVATE src tests)

target_link_libraries(core-tests PRIVATE
  winui_lite
  comctl32
  shell32
  ole32
  user32
  psapi
  mpr       # WNetEnumResource for share-enum tests
)

# ---------------------------------------------------------------------------
# winui_lite_demo: minimal shell-free executable that proves the chrome +
# ports surface compiles and runs without any FastExplorer / shell code.
# Renders ten in-memory items into an LVS_OWNERDATA list-view driven by
# the ItemDispatcher port, plus a chrome StatusBar at the bottom.
# ---------------------------------------------------------------------------
add_executable(winui_lite_demo WIN32
  examples/minimal-window/main.cpp
  examples/minimal-window/in-memory-item-source.cpp
  examples/minimal-window/in-memory-item-source.h
  examples/minimal-window/noop-adapters.h
)

target_include_directories(winui_lite_demo PRIVATE
  examples/minimal-window
)

target_compile_features(winui_lite_demo PRIVATE cxx_std_20)

target_link_libraries(winui_lite_demo PRIVATE
  winui_lite
  comctl32
  user32
  gdi32
  uxtheme
)

# ---------------------------------------------------------------------------
# FastExplorerBench: console-mode CLI for synthetic dataset generation and
# enumeration timing measurements. Subcommands are stubs at this scaffold step.
# ---------------------------------------------------------------------------
add_executable(FastExplorerBench
  src/bench/main.cpp
  src/bench/bench-cli.cpp
  src/bench/bench-cli.h
  src/bench/bench-json.cpp
  src/bench/bench-json.h
  src/bench/dataset-generator.cpp
  src/bench/dataset-generator.h
  src/bench/enumeration-bench.cpp
  src/bench/enumeration-bench.h
  src/bench/head-to-head-bench.cpp
  src/bench/head-to-head-bench.h
  src/core/directory-enumerator.cpp
  src/core/directory-enumerator.h
  src/core/file-entry.h
  src/core/file-model-store.cpp
  src/core/file-model-store.h
  src/core/file-sort.cpp
  src/core/file-sort.h
  src/core/fs-backend.h
  src/core/fs-watcher.cpp
  src/core/fs-watcher.h
  src/core/name-arena.cpp
  src/core/name-arena.h
  src/core/path-utils.cpp
  src/core/path-utils.h
  src/core/perf-tracker.cpp
  src/core/perf-tracker.h
  src/core/process-memory.cpp
  src/core/process-memory.h
  src/core/ring-logger.cpp
  src/core/ring-logger.h
  src/core/text-utf.cpp
  src/core/text-utf.h
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

target_include_directories(FastExplorerBench PRIVATE src)

target_link_libraries(FastExplorerBench PRIVATE
  shell32
  ole32
  psapi
  mpr  # win32-fs-backend share-enum path
)

enable_testing()
add_test(NAME core-tests COMMAND core-tests)

# ---------------------------------------------------------------------------
# Packaging — install rules + CPack ZIP generator.
# Build the release ZIP with: cmake --build --preset release --target package
# Produces: build/FastExplorer-<version>-win64.zip
# ---------------------------------------------------------------------------
# Install into bin/ to match the CPack convention: the
# CPACK_PACKAGE_EXECUTABLES shortcut, the Add/Remove Programs
# DisplayIcon, and CPACK_NSIS_MUI_FINISHPAGE_RUN all expect the
# exe to live at `$INSTDIR\bin\<name>.exe`. Previously the install
# used DESTINATION "." (root of install dir), so the auto-generated
# shortcut at $INSTDIR\bin\FastExplorer.exe was broken — clicking
# it pointed at a nonexistent path.
install(TARGETS FastExplorer
  RUNTIME DESTINATION bin
)
install(FILES "${WINSPARKLE_DLL}" DESTINATION bin)

set(CPACK_PACKAGE_NAME "FastExplorer")
set(CPACK_PACKAGE_VENDOR "Fast Explorer Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast dual-pane file explorer for Windows")
set(CPACK_PACKAGE_VERSION_MAJOR "${FastExplorer_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${FastExplorer_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${FastExplorer_VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME "FastExplorer-${FastExplorer_VERSION}-win64")
# ZIP for power users who prefer portable extraction, NSIS .exe for
# WinSparkle auto-update (ShellExecute on a .zip just opens it in
# the system archive viewer — only an installer actually installs).
# Both files are uploaded to the GitHub Release; appcast.xml points
# at the .exe so the auto-update click triggers a real install.
set(CPACK_GENERATOR "ZIP;NSIS")
# Toplevel directory kept ON (the CPack default). Setting it OFF
# globally broke NSIS — staging tree shape no longer matched the
# generated `File` commands and /S installs landed empty (observed
# v0.2.1 + v0.2.2). The generator-scoped override
# CPACK_NSIS_INCLUDE_TOPLEVEL_DIRECTORY is not a real CPack
# variable, so the only safe path is to leave the default on and
# accept that ZIP extracts produce one nested folder
# (FastExplorer-<ver>-win64/). Portable users just `cd` into it.

# NSIS-specific config. Installs under %LOCALAPPDATA%\Programs\
# FastExplorer so no admin elevation is needed for first install
# or for the auto-update flow. Start-menu shortcut "Fast Explorer"
# points at the exe; the post-install Finish page offers a checkbox
# to launch the app, ticked by default.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FastExplorer")
# Bracket-quote with a literal `\\` so the value stored is two
# backslashes. CMake's CPackConfig.cmake writer preserves the
# value as-is into `"..."`; the inner `\\` is then a valid
# escape that the CPackConfig re-parser collapses back to a
# single `\` before handing the string to the NSIS generator.
# A single `\` here would write `"\P..."` to CPackConfig and
# trip the "Invalid character escape '\P'" parser error.
set(CPACK_NSIS_INSTALL_ROOT [[$LOCALAPPDATA\\Programs]])
set(CPACK_NSIS_DISPLAY_NAME "Fast Explorer")
set(CPACK_NSIS_PACKAGE_NAME "Fast Explorer")
set(CPACK_NSIS_HELP_LINK "https://github.com/solitasroh/fast-explorer")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/solitasroh/fast-explorer")
set(CPACK_NSIS_CONTACT "rsj0811@rootech.com")
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
# CPack's NSIS template emits `MUI_FINISHPAGE_RUN "$INSTDIR\bin\<this>"`
# — it prepends `$INSTDIR\bin\` itself, so passing `bin\FastExplorer.exe`
# yields `$INSTDIR\bin\bin\FastExplorer.exe`, which doesn't exist and
# turns the Finish-page Run checkbox into a silent no-op. Pass the leaf
# name only.
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "FastExplorer.exe")
set(CPACK_PACKAGE_EXECUTABLES "FastExplorer" "Fast Explorer")
# CPack's NSIS template hard-codes `RequestExecutionLevel admin` at
# header level — that demands UAC consent on every launch, and when
# the user denies (or the parent process is non-elevated) NSIS
# silently exits 0, leaving an empty install dir. Since our install
# target is %LOCALAPPDATA%\Programs (user-writable, no UAC needed),
# we want `user`. NSIS allows multiple RequestExecutionLevel
# directives — the last wins — so injecting one via
# @CPACK_NSIS_DEFINES@ overrides the hard-coded `admin` without
# having to vendor the template file. The .onInit logic also probes
# UserInfo::GetAccountType; for non-admin (the common case) it
# leaves $INSTDIR at the InstallDir directive value.
set(CPACK_NSIS_DEFINES "RequestExecutionLevel user")
include(CPack)
