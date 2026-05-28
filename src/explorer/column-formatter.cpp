#include "explorer/column-formatter.h"

#include <windows.h>

#include <cwchar>
#include <cwctype>

namespace fast_explorer::ui {

namespace {

constexpr uint64_t kBytesPerKB = 1024ULL;
constexpr uint64_t kBytesPerMB = 1024ULL * 1024;
constexpr uint64_t kBytesPerGB = 1024ULL * 1024 * 1024;
constexpr uint64_t kBytesPerTB = 1024ULL * 1024 * 1024 * 1024;

struct ScaleEntry {
  uint64_t scale;
  const wchar_t* unit;
};

constexpr ScaleEntry kScaleLadder[] = {
    {kBytesPerTB, L"TB"},
    {kBytesPerGB, L"GB"},
    {kBytesPerMB, L"MB"},
    {kBytesPerKB, L"KB"},
};

std::wstring formatScaled(uint64_t bytes, uint64_t scale, const wchar_t* unit) {
  const uint64_t whole = bytes / scale;
  // Compute the fractional digit from the leftover bytes only, so the
  // multiply cannot overflow even at TB scale with byte counts near
  // the top of uint64_t.
  const uint64_t remainder = bytes - whole * scale;
  const uint64_t r10 = (remainder * 10ULL) / scale;
  wchar_t buf[32];
  swprintf_s(buf, _countof(buf), L"%llu.%llu %ls",
             static_cast<unsigned long long>(whole),
             static_cast<unsigned long long>(r10), unit);
  return buf;
}

}  // namespace

std::wstring formatSize(uint64_t bytes) {
  for (const auto& step : kScaleLadder) {
    if (bytes >= step.scale) {
      return formatScaled(bytes, step.scale, step.unit);
    }
  }
  wchar_t buf[32];
  swprintf_s(buf, _countof(buf), L"%llu B",
             static_cast<unsigned long long>(bytes));
  return buf;
}

std::wstring formatSizeForEntry(const fast_explorer::core::FileEntry& e) {
  if (fast_explorer::core::isDirectory(e)) {
    return std::wstring();
  }
  return formatSize(e.size);
}

std::wstring formatType(std::wstring_view extension, bool isDirectory) {
  if (isDirectory) {
    return L"File folder";
  }
  if (extension.empty()) {
    return L"File";
  }
  // Drop the leading dot for display, uppercase the rest.
  std::wstring_view body = extension;
  if (body.front() == L'.') {
    body.remove_prefix(1);
  }
  std::wstring out;
  out.reserve(body.size() + 5);
  for (wchar_t c : body) {
    out.push_back(static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c))));
  }
  out.append(L" File");
  return out;
}

std::wstring formatTypeForEntry(const fast_explorer::core::FileEntry& e) {
  return formatType(fast_explorer::core::extensionView(e),
                    fast_explorer::core::isDirectory(e));
}

std::wstring formatAttributesForEntry(
    const fast_explorer::core::FileEntry& e) {
  std::wstring out;
  out.reserve(6);
  if (fast_explorer::core::isHidden(e)) {
    out.push_back(L'H');
  }
  if (fast_explorer::core::isSystem(e)) {
    out.push_back(L'S');
  }
  if (fast_explorer::core::isReadOnly(e)) {
    out.push_back(L'R');
  }
  if (fast_explorer::core::isReparse(e) &&
      !fast_explorer::core::isSymlink(e)) {
    out.push_back(L'J');
  }
  if (fast_explorer::core::isSymlink(e)) {
    out.push_back(L'L');
  }
  if (fast_explorer::core::isCloudPlaceholder(e)) {
    out.push_back(L'C');
  }
  return out;
}

bool shouldRenderDimmed(const fast_explorer::core::FileEntry& e) noexcept {
  return fast_explorer::core::isHidden(e) || fast_explorer::core::isSystem(e);
}

std::wstring formatModified(uint64_t ft100ns) {
  if (ft100ns == 0) {
    return std::wstring();
  }
  FILETIME utc;
  utc.dwLowDateTime = static_cast<DWORD>(ft100ns & 0xFFFFFFFFu);
  utc.dwHighDateTime = static_cast<DWORD>(ft100ns >> 32);
  FILETIME local{};
  if (!FileTimeToLocalFileTime(&utc, &local)) {
    return std::wstring();
  }
  SYSTEMTIME st{};
  if (!FileTimeToSystemTime(&local, &st)) {
    return std::wstring();
  }
  wchar_t buf[32];
  swprintf_s(buf, _countof(buf), L"%04u-%02u-%02u %02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
  return buf;
}

}  // namespace fast_explorer::ui
