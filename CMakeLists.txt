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
  src/ui/main-window.cpp
  src/ui/main-window.h
  src/core/perf-tracker.cpp
  src/core/perf-tracker.h
  src/core/ring-logger.cpp
  src/core/ring-logger.h
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
