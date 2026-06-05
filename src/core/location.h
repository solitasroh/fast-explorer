#pragma once

#include <string>
#include <string_view>

namespace fast_explorer::core {

enum class LocationKind {
  FileSystemPath = 0,
  ShellKnownFolder = 1,
  ShellNamespace = 2,
};

enum class KnownShellLocation {
  None = 0,
  ThisPC,
  Network,
  NetworkShortcuts,
};

struct Location {
  LocationKind kind = LocationKind::FileSystemPath;
  KnownShellLocation known = KnownShellLocation::None;
  std::wstring value;
};

[[nodiscard]] Location parseLocation(std::wstring_view text);
[[nodiscard]] std::wstring serializeLocation(const Location& location);
[[nodiscard]] std::wstring displayNameForLocation(const Location& location);
[[nodiscard]] bool isShellLocation(std::wstring_view text) noexcept;

}  // namespace fast_explorer::core
