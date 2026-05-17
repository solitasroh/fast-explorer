#include "core/text-utf.h"

#include <windows.h>

namespace fast_explorer::core {

std::wstring widenUtf8(std::string_view bytes) {
  if (bytes.empty()) {
    return std::wstring();
  }
  const int wide = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                                       static_cast<int>(bytes.size()),
                                       nullptr, 0);
  if (wide <= 0) {
    return std::wstring();
  }
  std::wstring out(static_cast<std::size_t>(wide), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                      static_cast<int>(bytes.size()), out.data(), wide);
  return out;
}

std::string narrowUtf8(std::wstring_view text) {
  if (text.empty()) {
    return std::string();
  }
  const int narrow = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (narrow <= 0) {
    return std::string();
  }
  std::string out(static_cast<std::size_t>(narrow), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(),
                      static_cast<int>(text.size()), out.data(), narrow,
                      nullptr, nullptr);
  return out;
}

}  // namespace fast_explorer::core
