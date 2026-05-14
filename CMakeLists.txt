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
  src/core/path-utils.cpp
)

target_include_directories(core-tests PRIVATE src tests)

target_link_libraries(core-tests PRIVATE
  shell32
  ole32
)

enable_testing()
add_test(NAME core-tests COMMAND core-tests)
