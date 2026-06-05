#include "core/location.h"

#include <cwctype>

namespace fast_explorer::core {

namespace {

constexpr std::wstring_view kThisPcClsid =
    L"::{20d04fe0-3aea-1069-a2d8-08002b30309d}";
constexpr std::wstring_view kNetworkClsid =
    L"::{f02c1a0d-be21-4350-88b0-7367fc96ef3c}";
constexpr std::wstring_view kRecycleBinClsid =
    L"::{645ff040-5081-101b-9f08-00aa002f954e}";
constexpr std::wstring_view kGalleryClsid =
    L"::{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}";
constexpr std::wstring_view kHomeClsid =
    L"::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}";

std::wstring lowerCopy(std::wstring_view text) {
  std::wstring out(text);
  for (wchar_t& c : out) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return out;
}

bool startsWith(std::wstring_view text, std::wstring_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

std::wstring_view trimView(std::wstring_view text) noexcept {
  while (!text.empty() && std::iswspace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::iswspace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::wstring compactAliasToken(std::wstring_view token) {
  std::wstring out;
  out.reserve(token.size());
  for (wchar_t c : token) {
    if (std::iswspace(c) || c == L'-' || c == L'_') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

Location shellKnown(KnownShellLocation known) {
  Location loc;
  loc.kind = LocationKind::ShellKnownFolder;
  loc.known = known;
  return loc;
}

Location shellNamespace(std::wstring_view value) {
  Location loc;
  loc.kind = LocationKind::ShellNamespace;
  loc.value.assign(value);
  return loc;
}

std::wstring_view stripShellPrefix(std::wstring_view lowered) noexcept {
  constexpr std::wstring_view kShellPrefix = L"shell:";
  if (startsWith(lowered, kShellPrefix)) {
    lowered.remove_prefix(kShellPrefix.size());
  }
  return lowered;
}

}  // namespace

bool isShellLocation(std::wstring_view text) noexcept {
  const std::wstring lowered = lowerCopy(text);
  const std::wstring_view trimmed = trimView(lowered);
  const std::wstring_view token = trimView(stripShellPrefix(trimmed));
  const std::wstring compact = compactAliasToken(token);
  return startsWith(trimmed, L"shell:") ||
         startsWith(trimmed, L"::{") ||
         token == kThisPcClsid ||
         token == kNetworkClsid ||
         compact == L"thispc" ||
         compact == L"mycomputer" ||
         compact == L"computer" ||
         compact == L"내pc" ||
         compact == L"내컴퓨터" ||
         compact == L"network" ||
         compact == L"네트워크" ||
         compact == L"nethood" ||
         compact == L"networkshortcuts" ||
         compact == L"recyclebin" ||
         compact == L"휴지통" ||
         compact == L"gallery" ||
         compact == L"갤러리" ||
         compact == L"home" ||
         compact == L"홈";
}

Location parseLocation(std::wstring_view text) {
  const std::wstring loweredStorage = lowerCopy(text);
  const std::wstring_view trimmed = trimView(loweredStorage);
  const std::wstring_view token = trimView(stripShellPrefix(trimmed));
  const std::wstring compact = compactAliasToken(token);
  const std::wstring_view original = trimView(text);

  if (compact == L"thispc" || compact == L"mycomputer" ||
      compact == L"computer" || compact == L"내pc" ||
      compact == L"내컴퓨터" || token == kThisPcClsid) {
    return shellKnown(KnownShellLocation::ThisPC);
  }
  if (compact == L"network" || compact == L"네트워크" ||
      token == kNetworkClsid) {
    return shellKnown(KnownShellLocation::Network);
  }
  if (compact == L"nethood" || compact == L"networkshortcuts") {
    return shellKnown(KnownShellLocation::NetworkShortcuts);
  }
  if (compact == L"recyclebin" || compact == L"휴지통" ||
      token == kRecycleBinClsid) {
    return shellNamespace(L"shell:RecycleBinFolder");
  }
  if (compact == L"gallery" || compact == L"갤러리" ||
      token == kGalleryClsid) {
    return shellNamespace(kGalleryClsid);
  }
  if (compact == L"home" || compact == L"홈" || token == kHomeClsid) {
    return shellNamespace(kHomeClsid);
  }
  if (startsWith(trimmed, L"shell:") || startsWith(trimmed, L"::{")) {
    return shellNamespace(original);
  }

  Location loc;
  loc.value.assign(text);
  return loc;
}

std::wstring serializeLocation(const Location& location) {
  if (location.kind == LocationKind::ShellKnownFolder) {
    switch (location.known) {
      case KnownShellLocation::ThisPC:
        return L"shell:ThisPC";
      case KnownShellLocation::Network:
        return L"shell:Network";
      case KnownShellLocation::NetworkShortcuts:
        return L"shell:NetHood";
      case KnownShellLocation::None:
        break;
    }
  }
  return location.value;
}

std::wstring displayNameForLocation(const Location& location) {
  if (location.kind == LocationKind::ShellKnownFolder) {
    switch (location.known) {
      case KnownShellLocation::ThisPC:
        return L"This PC";
      case KnownShellLocation::Network:
        return L"Network";
      case KnownShellLocation::NetworkShortcuts:
        return L"Network Shortcuts";
      case KnownShellLocation::None:
        break;
    }
  }
  return location.value;
}

}  // namespace fast_explorer::core
