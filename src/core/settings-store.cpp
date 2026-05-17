#include "core/settings-store.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "core/path-utils.h"
#include "core/text-utf.h"

namespace fast_explorer::core {

namespace {

// Bounded read cap; settings files are expected to be a few hundred
// bytes. A hostile or corrupt file should fail parsing, not exhaust
// memory on read.
constexpr DWORD kMaxSettingsBytes = 64u * 1024u;

// Pre-allocation hint for the writer. Tracks the v2 footprint
// (~250 bytes) with headroom; any growth in the schema or a long
// path triggers std::string's own growth strategy.
constexpr std::size_t kSerializedReserveHint = 384;

constexpr const wchar_t* kFileName = L"settings.json";
// std::string_view (rather than const char*) eliminates the implicit
// strlen on every key comparison in JsonReader::parseValueInto.
constexpr std::string_view kKeyLastPath   {"last_path"};
constexpr std::string_view kKeyWindowX    {"window_x"};
constexpr std::string_view kKeyWindowY    {"window_y"};
constexpr std::string_view kKeyWindowW    {"window_w"};
constexpr std::string_view kKeyWindowH    {"window_h"};
constexpr std::string_view kKeyLayoutMode {"layout_mode"};
constexpr std::string_view kKeySecondPath {"second_path"};
constexpr std::string_view kLayoutSingle  {"single"};
constexpr std::string_view kLayoutDual    {"dual"};

bool readWholeFileBytes(const std::wstring& path, std::vector<char>& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 ||
      static_cast<uint64_t>(size.QuadPart) > kMaxSettingsBytes) {
    CloseHandle(h);
    return false;
  }
  out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()),
                           &read, nullptr);
  CloseHandle(h);
  if (!ok || read != out.size()) {
    return false;
  }
  return true;
}

void appendJsonEscapedString(std::string& out, std::wstring_view value) {
  const std::string utf8 = narrowUtf8(value);
  out.push_back('"');
  for (char c : utf8) {
    switch (c) {
      case '"':  out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n");  break;
      case '\r': out.append("\\r");  break;
      case '\t': out.append("\\t");  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // Unsupported control characters become spaces rather than
          // adding a \uXXXX escape path that the reader would also
          // need; settings values never legitimately carry these.
          out.push_back(' ');
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
}

void appendJsonInt(std::string& out, int value) {
  char buf[32];
  const int n = std::snprintf(buf, sizeof(buf), "%d", value);
  if (n > 0) {
    out.append(buf, static_cast<std::size_t>(n));
  }
}

// Writer-side helpers that own the JSON separator + quoting shape so
// the saveSessionState body stays a flat sequence of one call per
// key. `first=true` emits the opening "{\n  ", subsequent calls emit
// the ",\n  " continuation. The caller is responsible for appending
// the trailing "\n}\n".
void appendKeyHeader(std::string& out, std::string_view key, bool first) {
  out.append(first ? "{\n  \"" : ",\n  \"");
  out.append(key);
  out.append("\": ");
}

void appendKeyInt(std::string& out, std::string_view key, int value,
                  bool first) {
  appendKeyHeader(out, key, first);
  appendJsonInt(out, value);
}

void appendKeyString(std::string& out, std::string_view key,
                     std::wstring_view value, bool first) {
  appendKeyHeader(out, key, first);
  appendJsonEscapedString(out, value);
}

void appendKeyRawString(std::string& out, std::string_view key,
                        std::string_view rawAscii, bool first) {
  appendKeyHeader(out, key, first);
  out.push_back('"');
  out.append(rawAscii);
  out.push_back('"');
}

class JsonReader {
 public:
  explicit JsonReader(std::string_view text) noexcept : text_(text) {}

  bool parseObjectInto(SessionState& state) {
    skipWs();
    if (!consume('{')) return false;
    skipWs();
    if (peek() == '}') {
      pos_++;
      return true;
    }
    while (true) {
      skipWs();
      std::string key;
      if (!parseStringInto(key)) return false;
      skipWs();
      if (!consume(':')) return false;
      skipWs();
      if (!parseValueInto(key, state)) return false;
      skipWs();
      if (consume(',')) continue;
      if (consume('}')) return true;
      return false;
    }
  }

 private:
  std::string_view text_;
  std::size_t pos_ = 0;

  char peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
  }
  bool consume(char c) {
    if (pos_ >= text_.size() || text_[pos_] != c) return false;
    pos_++;
    return true;
  }
  void skipWs() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        pos_++;
      } else {
        return;
      }
    }
  }
  bool parseStringInto(std::string& out) {
    if (!consume('"')) return false;
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c == '\\') {
        if (pos_ >= text_.size()) return false;
        const char esc = text_[pos_++];
        switch (esc) {
          case '"':  out.push_back('"');  break;
          case '\\': out.push_back('\\'); break;
          case '/':  out.push_back('/');  break;
          case 'n':  out.push_back('\n'); break;
          case 'r':  out.push_back('\r'); break;
          case 't':  out.push_back('\t'); break;
          default: return false;
        }
        continue;
      }
      out.push_back(c);
    }
    return false;
  }
  bool parseIntInto(int& out) {
    if (pos_ >= text_.size()) return false;
    bool negative = false;
    if (text_[pos_] == '-') {
      negative = true;
      pos_++;
    }
    const std::size_t digitsStart = pos_;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      pos_++;
    }
    if (pos_ == digitsStart) return false;
    long long acc = 0;
    // The bound (static_cast<long long>(INT_MAX) + 1) accepts INT_MIN
    // exactly: -2147483648 decomposes to digits accumulating to
    // 2147483648 (which passes the guard) and then negates to fit INT_MIN.
    constexpr long long kAccCap = static_cast<long long>(INT_MAX) + 1;
    for (std::size_t i = digitsStart; i < pos_; ++i) {
      acc = acc * 10 + (text_[i] - '0');
      if (acc > kAccCap) return false;
    }
    if (negative) acc = -acc;
    if (acc < INT_MIN || acc > INT_MAX) return false;
    out = static_cast<int>(acc);
    return true;
  }
  bool parseValueInto(std::string_view key, SessionState& state) {
    if (key == kKeyLastPath) {
      std::string raw;
      if (!parseStringInto(raw)) return false;
      state.lastPath = widenUtf8(raw);
      return true;
    }
    if (key == kKeySecondPath) {
      std::string raw;
      if (!parseStringInto(raw)) return false;
      state.secondPath = widenUtf8(raw);
      return true;
    }
    if (key == kKeyLayoutMode) {
      std::string raw;
      if (!parseStringInto(raw)) return false;
      // Lenient: an unrecognized value (corrupt file, future-mode
      // string this build does not know) falls back to Single so the
      // user still gets a window restore instead of a thrown-away load.
      state.layoutMode = (raw == kLayoutDual) ? LayoutMode::Dual
                                              : LayoutMode::Single;
      return true;
    }
    int* slot = nullptr;
    if      (key == kKeyWindowX) slot = &state.windowX;
    else if (key == kKeyWindowY) slot = &state.windowY;
    else if (key == kKeyWindowW) slot = &state.windowWidth;
    else if (key == kKeyWindowH) slot = &state.windowHeight;
    if (slot == nullptr) {
      // Unknown key: skip the value to stay forward-compatible.
      return skipValue();
    }
    return parseIntInto(*slot);
  }
  bool skipValue() {
    skipWs();
    if (pos_ >= text_.size()) return false;
    const char c = text_[pos_];
    if (c == '"') {
      std::string sink;
      return parseStringInto(sink);
    }
    int sink = 0;
    return parseIntInto(sink);
  }
};

bool writeWholeFile(const std::wstring& path, const std::string& bytes) {
  HANDLE h = CreateFileW(path.c_str(),
                         GENERIC_WRITE,
                         0,
                         nullptr,
                         CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(h, bytes.data(),
                            static_cast<DWORD>(bytes.size()),
                            &written, nullptr);
  // FlushFileBuffers forces the data extents to disk before the temp
  // file is renamed; without it MOVEFILE_WRITE_THROUGH only guarantees
  // the rename metadata, leaving the temp's bytes in the OS cache.
  if (ok) {
    FlushFileBuffers(h);
  }
  CloseHandle(h);
  return ok && written == bytes.size();
}

bool ensureParentDir(const std::wstring& path) {
  const std::size_t sep = path.find_last_of(L"\\/");
  if (sep == std::wstring::npos) return true;
  const std::wstring parent = path.substr(0, sep);
  return ensureDirectoryRecursive(parent.c_str());
}

}  // namespace

std::wstring defaultSettingsPath() {
  std::wstring out;
  if (!resolveAppDataSubdir(kFileName, out)) {
    return std::wstring();
  }
  return out;
}

bool loadSessionState(const std::wstring& path, SessionState& state) {
  state = SessionState{};
  std::vector<char> bytes;
  if (!readWholeFileBytes(path, bytes)) {
    return false;
  }
  JsonReader reader(std::string_view(bytes.data(), bytes.size()));
  if (!reader.parseObjectInto(state)) {
    state = SessionState{};
    return false;
  }
  return true;
}

bool saveSessionState(const std::wstring& path, const SessionState& state) {
  if (path.empty()) {
    return false;
  }
  if (!ensureParentDir(path)) {
    return false;
  }
  std::string out;
  out.reserve(kSerializedReserveHint);
  const std::string_view layoutLabel =
      state.layoutMode == LayoutMode::Dual ? kLayoutDual : kLayoutSingle;
  appendKeyString   (out, kKeyLastPath,   state.lastPath,    /*first*/ true);
  appendKeyInt      (out, kKeyWindowX,    state.windowX,     /*first*/ false);
  appendKeyInt      (out, kKeyWindowY,    state.windowY,     /*first*/ false);
  appendKeyInt      (out, kKeyWindowW,    state.windowWidth, /*first*/ false);
  appendKeyInt      (out, kKeyWindowH,    state.windowHeight,/*first*/ false);
  appendKeyRawString(out, kKeyLayoutMode, layoutLabel,       /*first*/ false);
  appendKeyString   (out, kKeySecondPath, state.secondPath,  /*first*/ false);
  out.append("\n}\n");

  const std::wstring temp = path + L".tmp";
  if (!writeWholeFile(temp, out)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

}  // namespace fast_explorer::core
