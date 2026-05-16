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

namespace {

std::wstring_view leafOf(const std::wstring& path) noexcept {
  if (path.empty()) {
    return {};
  }
  const std::size_t sep = path.find_last_of(L"\\/");
  if (sep == std::wstring::npos) {
    return path;
  }
  return std::wstring_view(path).substr(sep + 1);
}

void appendQuoted(std::wstring& out, std::wstring_view name) {
  out.append(L"'");
  out.append(name);
  out.append(L"'");
}

}  // namespace

std::wstring opResultStatusText(const OperationResult& result) {
  std::wstring out;
  out.reserve(64);
  const std::wstring_view leaf = leafOf(result.sourcePath);
  switch (result.kind) {
    case ShellCommandKind::Delete:
      if (result.success) {
        out.append(L"Moved ");
        appendQuoted(out, leaf);
        out.append(L" to Recycle Bin");
      } else {
        out.append(L"Failed to delete ");
        appendQuoted(out, leaf);
      }
      break;
    case ShellCommandKind::Rename:
      if (result.success) {
        out.append(L"Renamed ");
        appendQuoted(out, leaf);
        out.append(L" to ");
        appendQuoted(out, result.newName);
      } else {
        out.append(L"Failed to rename ");
        appendQuoted(out, leaf);
      }
      break;
    case ShellCommandKind::CreateFolder:
      if (result.success) {
        out.append(L"Created folder ");
        appendQuoted(out, result.newName);
      } else {
        out.append(L"Failed to create folder ");
        appendQuoted(out, result.newName);
      }
      break;
  }
  return out;
}

}  // namespace fast_explorer::ui
