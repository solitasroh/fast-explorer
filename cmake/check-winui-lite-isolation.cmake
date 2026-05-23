# Fails the build if any file under lib/winui_lite/ includes a header
# under src/. winui_lite must depend only on its own headers, the
# standard library, and Win32 / COM system headers. The app (src/) may
# depend on winui_lite, never the other way around.
#
# Invoked via add_custom_target from the top-level CMakeLists.txt with
# LIB_DIR pointing at lib/winui_lite.

if(NOT DEFINED LIB_DIR)
  message(FATAL_ERROR "check-winui-lite-isolation.cmake: LIB_DIR not set")
endif()

file(GLOB_RECURSE LIB_FILES
  "${LIB_DIR}/*.cpp"
  "${LIB_DIR}/*.h"
)

set(VIOLATIONS "")
foreach(f IN LISTS LIB_FILES)
  file(READ "${f}" content)
  # Catches #include "core/...", "ui/...", "app/...", "bench/...",
  # "src/..." in either quoted or angle-bracket form.
  string(REGEX MATCHALL
    "#[ \t]*include[ \t]+[<\"](core|ui|app|bench|src)/"
    hits "${content}")
  if(hits)
    list(APPEND VIOLATIONS "  ${f}")
    foreach(h IN LISTS hits)
      list(APPEND VIOLATIONS "    -> ${h}")
    endforeach()
  endif()
endforeach()

if(VIOLATIONS)
  string(REPLACE ";" "\n" VIOLATION_TEXT "${VIOLATIONS}")
  message(FATAL_ERROR
    "winui_lite isolation violated; lib/ may not include from src/:\n"
    "${VIOLATION_TEXT}\n"
    "Move the shared declaration into lib/winui_lite/ or invert the "
    "dependency (have the app pass it in via a port).")
endif()

message(STATUS "winui_lite isolation: OK (${LIB_FILES})")
