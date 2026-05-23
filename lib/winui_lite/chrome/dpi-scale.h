#pragma once

namespace fast_explorer::ui {

inline constexpr unsigned int kDefaultDpi = 96;

// Scales a length authored at 96 DPI (Windows reference DPI) to the
// equivalent length at the given DPI.  Uses nearest-rounding integer
// math so column widths and similar UI metrics stay sharp.
constexpr int scaleForDpi(int valueAt96Dpi, unsigned int dpi) {
  if (dpi == 0) {
    return valueAt96Dpi;
  }
  const long long scaled =
      static_cast<long long>(valueAt96Dpi) * dpi + kDefaultDpi / 2;
  return static_cast<int>(scaled / kDefaultDpi);
}

}  // namespace fast_explorer::ui
