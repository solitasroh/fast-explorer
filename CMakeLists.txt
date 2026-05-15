cmake_minimum_required(VERSION 3.24)
project(FastExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/ui/main-window.cpp
  src/ui/main-window.h
  src/ui/messages.h
  src/ui/column-formatter.cpp
  src/ui/column-formatter.h
  src/ui/dpi-scale.h
  src/ui/format-cache.cpp
  src/ui/format-cache.h
  src/ui/pane-controller.cpp
  src/ui/pane-controller.h
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
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

set(FE_RESOURCES
  resources/FastExplorer.rc
)

add_executable(FastExplorer WIN32
  ${FE_APP_SOURCES}
  ${FE_RESOURCES}
)

target_include_directories(FastExplorer PRIVATE src)

target_link_libraries(FastExplorer PRIVATE
  comctl32
  shell32
  ole32
  oleaut32
  user32
  gdi32
  uxtheme
  dbghelp
  propsys
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
  tests/bench-fs-helper.h
  src/ui/messages.h
  src/ui/column-formatter.cpp
  src/ui/column-formatter.h
  src/ui/dpi-scale.h
  src/ui/format-cache.cpp
  src/ui/format-cache.h
  src/ui/pane-controller.cpp
  src/ui/pane-controller.h
  src/ui/status-text.cpp
  src/ui/status-text.h
  src/ui/stall-probe.cpp
  src/ui/stall-probe.h
  src/bench/bench-cli.cpp
  src/bench/bench-cli.h
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
  src/core/memory-fs-backend.cpp
  src/core/memory-fs-backend.h
  src/core/name-arena.cpp
  src/core/name-arena.h
  src/core/path-utils.cpp
  src/core/ring-logger.cpp
  src/core/ring-logger.h
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

target_include_directories(core-tests PRIVATE src tests)

target_link_libraries(core-tests PRIVATE
  shell32
  ole32
)

# ---------------------------------------------------------------------------
# FastExplorerBench: console-mode CLI for synthetic dataset generation and
# enumeration timing measurements. Subcommands are stubs at this scaffold step.
# ---------------------------------------------------------------------------
add_executable(FastExplorerBench
  src/bench/main.cpp
  src/bench/bench-cli.cpp
  src/bench/bench-cli.h
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
  src/core/fs-backend.h
  src/core/fs-watcher.cpp
  src/core/fs-watcher.h
  src/core/name-arena.cpp
  src/core/name-arena.h
  src/core/path-utils.cpp
  src/core/path-utils.h
  src/core/ring-logger.cpp
  src/core/ring-logger.h
  src/core/win32-fs-backend.cpp
  src/core/win32-fs-backend.h
)

target_include_directories(FastExplorerBench PRIVATE src)

target_link_libraries(FastExplorerBench PRIVATE
  shell32
  ole32
)

enable_testing()
add_test(NAME core-tests COMMAND core-tests)
