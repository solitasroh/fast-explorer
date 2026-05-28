#include "core/settings-store.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "winui_lite/chrome/layout-preset.h"
#include "core/path-utils.h"
#include "core/text-utf.h"
#include "winui_lite/chrome/splitter-ratios.h"

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
constexpr std::string_view kKeyOrientation{"orientation"};
// Schema v4 (v0.2): view toggles. Stored as int 0/1 to reuse the
// existing parseIntInto / appendKeyInt helpers; bool literals are
// not part of the custom JSON dialect this file supports.
constexpr std::string_view kKeyShowHidden    {"view_show_hidden"};
constexpr std::string_view kKeyShowExtensions{"view_show_extensions"};
constexpr std::string_view kLayoutSingle  {"single"};
constexpr std::string_view kLayoutDual    {"dual"};
constexpr std::string_view kOrientVertical  {"vertical"};
constexpr std::string_view kOrientHorizontal{"horizontal"};

// Schema v5 keys (kKeyPanePaths kept for v5->v6 migration reader)
constexpr std::string_view kKeySchemaVersion {"schema_version"};
constexpr std::string_view kKeyPanePaths     {"pane_paths"};
constexpr std::string_view kKeyPaneCount     {"pane_count"};
constexpr std::string_view kKeyActivePane    {"active_pane"};
constexpr std::string_view kKeyPreset        {"preset"};
constexpr std::string_view kKeyRatios        {"ratios"};

// Schema v6 keys
constexpr std::string_view kKeyPanes         {"panes"};
constexpr std::string_view kKeyTabs          {"tabs"};
constexpr std::string_view kKeyActiveTab     {"active_tab"};
constexpr std::string_view kKeyPath          {"path"};

constexpr int kSchemaVersionCurrent = 6;

constexpr std::string_view presetLabel(LayoutPreset p) noexcept {
  switch (p) {
    case LayoutPreset::Single: return "single";
    case LayoutPreset::Dual_V: return "dual_v";
    case LayoutPreset::Dual_H: return "dual_h";
    case LayoutPreset::Tri_A:  return "tri_a";
    case LayoutPreset::Tri_B:  return "tri_b";
    case LayoutPreset::Tri_C:  return "tri_c";
    case LayoutPreset::Quad_A: return "quad_a";
    case LayoutPreset::Quad_B: return "quad_b";
    case LayoutPreset::Quad_C: return "quad_c";
    case LayoutPreset::Quad_D: return "quad_d";
  }
  return "single";
}

constexpr LayoutPreset presetFromLabel(std::string_view s) noexcept {
  if (s == "single") return LayoutPreset::Single;
  if (s == "dual_v") return LayoutPreset::Dual_V;
  if (s == "dual_h") return LayoutPreset::Dual_H;
  if (s == "tri_a")  return LayoutPreset::Tri_A;
  if (s == "tri_b")  return LayoutPreset::Tri_B;
  if (s == "tri_c")  return LayoutPreset::Tri_C;
  if (s == "quad_a") return LayoutPreset::Quad_A;
  if (s == "quad_b") return LayoutPreset::Quad_B;
  if (s == "quad_c") return LayoutPreset::Quad_C;
  if (s == "quad_d") return LayoutPreset::Quad_D;
  return LayoutPreset::Single;
}

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

void appendKeyPanes(std::string& out,
                    const std::array<PaneSessionV6, kMaxPanes>& panes,
                    bool first) {
  appendKeyHeader(out, kKeyPanes, first);
  out.push_back('[');
  for (std::size_t i = 0; i < kMaxPanes; ++i) {
    if (i > 0) out.append(", ");
    out.append("{\"tabs\": [");
    const auto& p = panes[i];
    for (std::size_t t = 0; t < p.tabs.size(); ++t) {
      if (t > 0) out.append(", ");
      out.append("{\"path\": ");
      appendJsonEscapedString(out, p.tabs[t].path);
      out.push_back('}');
    }
    out.append("], \"active_tab\": ");
    char numBuf[24];
    std::snprintf(numBuf, sizeof(numBuf), "%zu", p.activeTab);
    out.append(numBuf);
    out.push_back('}');
  }
  out.push_back(']');
}

void appendKeyRatios(std::string& out,
                     const std::array<fast_explorer::ui::SplitterRatios,
                                       fast_explorer::core::kLayoutPresetCount>& ratios,
                     bool first) {
  appendKeyHeader(out, kKeyRatios, first);
  out.append("{\n");
  bool firstEntry = true;
  for (std::size_t i = 0; i < ratios.size(); ++i) {
    const auto p = static_cast<LayoutPreset>(i);
    const auto& r = ratios[i];
    if (r.ratios[0] == 0.0f && r.ratios[1] == 0.0f && r.ratios[2] == 0.0f) {
      continue;  // skip untouched
    }
    if (!firstEntry) out.append(",\n");
    firstEntry = false;
    out.append("    \"");
    out.append(presetLabel(p));
    out.append("\": [");
    const std::size_t splitterCount = slotCountForPreset(p) > 1
                                          ? slotCountForPreset(p) - 1 : 0;
    const std::size_t n = std::min<std::size_t>(3, splitterCount);
    for (std::size_t j = 0; j < n; ++j) {
      if (j > 0) out.append(", ");
      char buf[32];
      const int len = std::snprintf(buf, sizeof(buf), "%.6f", r.ratios[j]);
      if (len > 0) out.append(buf, static_cast<std::size_t>(len));
    }
    out.push_back(']');
  }
  out.append("\n  }");
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

  int schemaVersion() const noexcept { return schemaVersion_; }

 private:
  const std::string_view text_;
  std::size_t pos_ = 0;
  int schemaVersion_ = 0;

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
  bool parseFloatInto(float& out) {
    skipWs();
    const std::size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) pos_++;
    bool sawDot = false, sawDigit = false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c >= '0' && c <= '9') { sawDigit = true; pos_++; continue; }
      if (c == '.' && !sawDot)  { sawDot = true; pos_++; continue; }
      break;
    }
    if (!sawDigit) return false;
    const std::string tok(text_.substr(start, pos_ - start));
    try { out = std::stof(tok); } catch (...) { return false; }
    return true;
  }
  bool parsePanePathsArrayInto(SessionState& state) {
    // v5 compat: populate legacyPanePaths[] for the migrator.
    skipWs();
    if (!consume('[')) return false;
    skipWs();
    std::size_t idx = 0;
    if (peek() == ']') { pos_++; return true; }
    while (true) {
      skipWs();
      std::string raw;
      if (!parseStringInto(raw)) return false;
      if (idx < state.legacyPanePaths.size()) {
        state.legacyPanePaths[idx] = widenUtf8(raw);
      }
      idx++;
      skipWs();
      if (consume(',')) continue;
      if (consume(']')) return true;
      return false;
    }
  }
  bool parsePanesArrayInto(SessionState& state) {
    // v6: array of {tabs:[{path:"..."},...], active_tab:N}
    skipWs();
    if (!consume('[')) return false;
    skipWs();
    std::size_t paneIdx = 0;
    if (peek() == ']') { pos_++; return true; }
    while (true) {
      skipWs();
      if (!consume('{')) return false;
      skipWs();
      PaneSessionV6 p;
      if (peek() != '}') {
        while (true) {
          skipWs();
          std::string key;
          if (!parseStringInto(key)) return false;
          skipWs();
          if (!consume(':')) return false;
          skipWs();
          if (key == kKeyTabs) {
            // tabs array
            if (!consume('[')) return false;
            skipWs();
            if (peek() == ']') { pos_++; }
            else {
              while (true) {
                skipWs();
                if (!consume('{')) return false;
                skipWs();
                TabRecordV6 t;
                if (peek() != '}') {
                  while (true) {
                    skipWs();
                    std::string k2;
                    if (!parseStringInto(k2)) return false;
                    skipWs();
                    if (!consume(':')) return false;
                    skipWs();
                    if (k2 == kKeyPath) {
                      std::string raw;
                      if (!parseStringInto(raw)) return false;
                      t.path = widenUtf8(raw);
                    } else {
                      if (!skipValue()) return false;
                    }
                    skipWs();
                    if (consume(',')) continue;
                    break;
                  }
                }
                skipWs();
                if (!consume('}')) return false;
                p.tabs.push_back(std::move(t));
                skipWs();
                if (consume(',')) continue;
                if (consume(']')) break;
                return false;
              }
            }
          } else if (key == kKeyActiveTab) {
            int v = 0;
            if (!parseIntInto(v)) return false;
            p.activeTab = static_cast<std::size_t>(v < 0 ? 0 : v);
          } else {
            if (!skipValue()) return false;
          }
          skipWs();
          if (consume(',')) continue;
          break;
        }
      }
      skipWs();
      if (!consume('}')) return false;
      if (paneIdx < kMaxPanes) state.panes[paneIdx] = std::move(p);
      paneIdx++;
      skipWs();
      if (consume(',')) continue;
      if (consume(']')) return true;
      return false;
    }
  }
  bool parseRatiosObjectInto(SessionState& state) {
    skipWs();
    if (!consume('{')) return false;
    skipWs();
    if (peek() == '}') { pos_++; return true; }
    while (true) {
      skipWs();
      std::string key;
      if (!parseStringInto(key)) return false;
      skipWs();
      if (!consume(':')) return false;
      skipWs();
      if (!consume('[')) return false;
      const LayoutPreset p = presetFromLabel(key);
      auto& dst = state.ratiosPerPreset[static_cast<std::size_t>(p)];
      std::size_t idx = 0;
      skipWs();
      if (peek() == ']') { pos_++; }
      else {
        while (true) {
          float v = 0.0f;
          if (!parseFloatInto(v)) return false;
          if (idx < dst.ratios.size()) dst.ratios[idx] = v;
          idx++;
          skipWs();
          if (consume(',')) { skipWs(); continue; }
          if (consume(']')) break;
          return false;
        }
      }
      skipWs();
      if (consume(',')) continue;
      if (consume('}')) return true;
      return false;
    }
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
    if (key == kKeyOrientation) {
      std::string raw;
      if (!parseStringInto(raw)) return false;
      // Same lenient policy as layout_mode — a future "diagonal" or
      // a hand-edit typo falls back to Vertical (the historical
      // default) rather than failing the whole load.
      state.orientation = (raw == kOrientHorizontal)
          ? LayoutOrientation::Horizontal
          : LayoutOrientation::Vertical;
      return true;
    }
    if (key == kKeyShowHidden || key == kKeyShowExtensions) {
      int raw = 0;
      if (!parseIntInto(raw)) return false;
      const bool value = raw != 0;
      if (key == kKeyShowHidden) state.showHidden = value;
      else                       state.showExtensions = value;
      return true;
    }
    if (key == kKeySchemaVersion) {
      int v = 0;
      if (!parseIntInto(v)) return false;
      schemaVersion_ = v;
      state.schemaVersionLoaded = v;
      return true;
    }
    if (key == kKeyPaneCount) {
      int v = 0;
      if (!parseIntInto(v)) return false;
      if (v < 1) v = 1;
      if (v > static_cast<int>(kMaxPanes)) v = static_cast<int>(kMaxPanes);
      state.paneCount = static_cast<std::size_t>(v);
      return true;
    }
    if (key == kKeyActivePane) {
      int v = 0;
      if (!parseIntInto(v)) return false;
      if (v < 0) v = 0;
      state.activePane = static_cast<std::size_t>(v);
      return true;
    }
    if (key == kKeyPreset) {
      std::string raw;
      if (!parseStringInto(raw)) return false;
      state.preset = presetFromLabel(raw);
      return true;
    }
    if (key == kKeyPanePaths) {
      return parsePanePathsArrayInto(state);
    }
    if (key == kKeyPanes) {
      return parsePanesArrayInto(state);
    }
    if (key == kKeyRatios) {
      return parseRatiosObjectInto(state);
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
    if (c == '[') {
      pos_++;
      skipWs();
      if (peek() == ']') { pos_++; return true; }
      while (true) {
        skipWs();
        if (!skipValue()) return false;
        skipWs();
        if (consume(',')) continue;
        if (consume(']')) return true;
        return false;
      }
    }
    if (c == '{') {
      pos_++;
      skipWs();
      if (peek() == '}') { pos_++; return true; }
      while (true) {
        skipWs();
        std::string sink;
        if (!parseStringInto(sink)) return false;
        skipWs();
        if (!consume(':')) return false;
        skipWs();
        if (!skipValue()) return false;
        skipWs();
        if (consume(',')) continue;
        if (consume('}')) return true;
        return false;
      }
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
  for (std::size_t i = 0; i < state.ratiosPerPreset.size(); ++i) {
    auto& r = state.ratiosPerPreset[i];
    if (r.ratios[0] == 0.0f && r.ratios[1] == 0.0f && r.ratios[2] == 0.0f) {
      r = fast_explorer::ui::defaultRatiosFor(static_cast<LayoutPreset>(i));
    }
  }
  if (reader.schemaVersion() < kSchemaVersionCurrent) {
    // v4 -> v5 path/preset migration (populates legacyPanePaths if not
    // already set by pane_paths key).
    if (state.legacyPanePaths[0].empty() && !state.lastPath.empty()) {
      state.legacyPanePaths[0] = state.lastPath;
    }
    if (state.legacyPanePaths[1].empty() && !state.secondPath.empty()) {
      state.legacyPanePaths[1] = state.secondPath;
    }
    if (state.layoutMode == LayoutMode::Dual) {
      state.preset = (state.orientation == LayoutOrientation::Horizontal)
                         ? LayoutPreset::Dual_H : LayoutPreset::Dual_V;
      state.paneCount = 2;
    } else {
      state.preset = LayoutPreset::Single;
      state.paneCount = 1;
    }
    state.activePane = 0;
  }

  // Migrate v5 -> v6: promote legacyPanePaths[] into panes[i].tabs[0].
  if (state.schemaVersionLoaded < 6) {
    for (std::size_t i = 0; i < state.paneCount && i < kMaxPanes; ++i) {
      if (!state.legacyPanePaths[i].empty() && state.panes[i].tabs.empty()) {
        TabRecordV6 t{ state.legacyPanePaths[i] };
        state.panes[i].tabs.push_back(std::move(t));
        state.panes[i].activeTab = 0;
      }
    }
  }

  // Repair v6 invariants (lenient forward-compat: clamp/fill on any path).
  for (std::size_t i = 0; i < kMaxPanes; ++i) {
    auto& p = state.panes[i];
    if (p.tabs.empty()) {
      // Empty tabs -> single Home placeholder (empty path resolved at
      // restore time to %USERPROFILE%).
      p.tabs.push_back(TabRecordV6{});
      p.activeTab = 0;
    } else if (p.activeTab >= p.tabs.size()) {
      p.activeTab = p.tabs.size() - 1;
    }
  }

  return true;
}

bool saveSessionState(const std::wstring& path, const SessionState& state) {
  if (path.empty()) return false;
  if (!ensureParentDir(path)) return false;
  std::string out;
  out.reserve(kSerializedReserveHint);
  appendKeyInt       (out, kKeySchemaVersion, kSchemaVersionCurrent, /*first*/ true);
  appendKeyInt       (out, kKeyWindowX,       state.windowX,         false);
  appendKeyInt       (out, kKeyWindowY,       state.windowY,         false);
  appendKeyInt       (out, kKeyWindowW,       state.windowWidth,     false);
  appendKeyInt       (out, kKeyWindowH,       state.windowHeight,    false);
  appendKeyPanes     (out, state.panes,                              false);
  appendKeyInt       (out, kKeyPaneCount,     static_cast<int>(state.paneCount),  false);
  appendKeyInt       (out, kKeyActivePane,    static_cast<int>(state.activePane), false);
  appendKeyRawString (out, kKeyPreset,        presetLabel(state.preset),          false);
  appendKeyRatios    (out, state.ratiosPerPreset,                                  false);
  appendKeyInt       (out, kKeyShowHidden,    state.showHidden     ? 1 : 0,        false);
  appendKeyInt       (out, kKeyShowExtensions,state.showExtensions ? 1 : 0,        false);
  out.append("\n}\n");

  const std::wstring temp = path + L".tmp";
  if (!writeWholeFile(temp, out)) { DeleteFileW(temp.c_str()); return false; }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

}  // namespace fast_explorer::core
