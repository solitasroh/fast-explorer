#include "ui/status-text.h"

#include <cwchar>

namespace fast_explorer::ui {

namespace {

const wchar_t* enumerationErrorName(
    fast_explorer::core::EnumerationError e) {
  using fast_explorer::core::EnumerationError;
  switch (e) {
    case EnumerationError::None: return L"None";
    case EnumerationError::PathNotFound: return L"PathNotFound";
    case EnumerationError::FileNotFound: return L"FileNotFound";
    case EnumerationError::AccessDenied: return L"AccessDenied";
    case EnumerationError::SharingViolation: return L"SharingViolation";
    case EnumerationError::NotReady: return L"NotReady";
    case EnumerationError::DirectoryNotSupported:
      return L"DirectoryNotSupported";
    case EnumerationError::InvalidSyntax: return L"InvalidSyntax";
    case EnumerationError::UncUnsupported: return L"UncUnsupported";
    case EnumerationError::Canceled: return L"Canceled";
    case EnumerationError::Internal: return L"Internal";
  }
  return L"Unknown";
}

}  // namespace

std::wstring loadingStatusText(const std::wstring& path) {
  std::wstring out;
  out.reserve(path.size() + 16);
  out.append(L"Loading: ");
  out.append(path);
  return out;
}

std::wstring loadingProgressStatusText(uint64_t itemsSoFar) {
  wchar_t buf[64];
  swprintf_s(buf, _countof(buf), L"Loading: %llu items",
             static_cast<unsigned long long>(itemsSoFar));
  return buf;
}

std::wstring readyStatusText(size_t itemCount) {
  wchar_t buf[64];
  swprintf_s(buf, _countof(buf), L"%llu items",
             static_cast<unsigned long long>(itemCount));
  return buf;
}

std::wstring errorStatusText(fast_explorer::core::EnumerationError err) {
  std::wstring out;
  out.reserve(32);
  out.append(L"Error: ");
  out.append(enumerationErrorName(err));
  return out;
}

}  // namespace fast_explorer::ui
