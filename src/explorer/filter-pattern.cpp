#include "explorer/filter-pattern.h"

#include <cassert>
#include <cwctype>
#include <limits>

#include "core/file-entry.h"
#include "core/file-model-store.h"

namespace fast_explorer::ui {

namespace {

// Stack buffer size used by foldToBuffer; folds beyond this fall
// through to heap. Tuned so typical Windows file names (≤260 wchar
// MAX_PATH) stay on the stack with headroom.
constexpr std::size_t kStackBufWchars = 256;

inline wchar_t foldChar(wchar_t c) noexcept {
  return static_cast<wchar_t>(std::towlower(c));
}

std::wstring toLowerCopy(std::wstring_view s) {
  std::wstring out(s);
  for (wchar_t& c : out) c = foldChar(c);
  return out;
}

// Folds `name` into either `stackBuf` (when it fits) or `heap`
// (resized to hold the fold). Returns a string_view over the
// chosen target. On heap allocation failure returns an empty view
// AND sets `oom` so the caller can short-circuit; this keeps
// matches() honestly noexcept by routing OOM through a flag
// rather than a throw.
std::wstring_view foldToBuffer(std::wstring_view name,
                               wchar_t* stackBuf,
                               std::wstring& heap,
                               bool& oom) noexcept {
  oom = false;
  wchar_t* dst;
  if (name.size() <= kStackBufWchars) {
    dst = stackBuf;
  } else {
    try {
      heap.resize(name.size());
      dst = heap.data();
    } catch (const std::bad_alloc&) {
      oom = true;
      return {};
    }
  }
  for (std::size_t i = 0; i < name.size(); ++i) {
    dst[i] = foldChar(name[i]);
  }
  return std::wstring_view(dst, name.size());
}

// Hand-rolled glob matcher with `*` (any run) and `?` (any single
// char). Iterative, no allocation, O(name * pattern) worst case
// but typically O(name + pattern) because most segments resolve
// without backtracking. Both inputs assumed already lowercased.
bool wildcardMatch(std::wstring_view pat, std::wstring_view name) noexcept {
  std::size_t pi = 0, ni = 0;
  std::size_t starPi = std::wstring_view::npos;
  std::size_t starNi = 0;
  while (ni < name.size()) {
    if (pi < pat.size() && (pat[pi] == name[ni] || pat[pi] == L'?')) {
      ++pi; ++ni;
    } else if (pi < pat.size() && pat[pi] == L'*') {
      starPi = pi++;
      starNi = ni;
    } else if (starPi != std::wstring_view::npos) {
      pi = starPi + 1;
      ni = ++starNi;
    } else {
      return false;
    }
  }
  while (pi < pat.size() && pat[pi] == L'*') ++pi;
  return pi == pat.size();
}

// Translate ECMAScript regex syntax errors during construction
// into an empty optional — avoids throwing across the caller's
// message pump. The icase flag is intentionally omitted because
// query_ + the foldToBuffer pass already lowercase both sides,
// so leaving it on would do redundant case folding inside the
// engine and disable some literal-string fast paths.
std::optional<std::wregex> tryCompileRegex(std::wstring_view lowered) noexcept {
  try {
    return std::wregex(lowered.data(), lowered.size(),
                       std::regex_constants::ECMAScript |
                           std::regex_constants::nosubs);
  } catch (const std::regex_error&) {
    return std::nullopt;
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
}

}  // namespace

FilterPattern::FilterPattern() noexcept = default;

FilterPattern::FilterPattern(std::wstring_view query, FilterMode mode)
    : query_(toLowerCopy(query)), mode_(mode), valid_(true) {
  if (mode_ == FilterMode::Regex && !query_.empty()) {
    regex_ = tryCompileRegex(query_);
    valid_ = regex_.has_value();
  }
}

bool FilterPattern::matches(std::wstring_view name) const noexcept {
  if (query_.empty()) return true;
  if (!valid_) return false;
  // Regex ReDoS guard: a hostile user pattern (e.g. (a+)+$) against
  // a very long name can hang the engine. Short-circuit anything
  // beyond the documented cap so the UI thread stays responsive.
  if (mode_ == FilterMode::Regex && name.size() > kMaxRegexInputWchars) {
    return false;
  }
  wchar_t stackBuf[kStackBufWchars];
  std::wstring heapBuf;
  bool oom = false;
  const std::wstring_view loweredName =
      foldToBuffer(name, stackBuf, heapBuf, oom);
  if (oom) return false;
  switch (mode_) {
    case FilterMode::Plain:
      return loweredName.find(query_) != std::wstring_view::npos;
    case FilterMode::Wildcard:
      return wildcardMatch(query_, loweredName);
    case FilterMode::Regex:
      try {
        return std::regex_search(loweredName.begin(), loweredName.end(),
                                 *regex_);
      } catch (const std::exception&) {
        return false;
      }
  }
  return false;
}

DetectedFilter detectFilterMode(std::wstring_view raw) {
  DetectedFilter out;
  // Regex prefix takes precedence so a deliberate regex with * or ?
  // ("r:foo.*\.txt") still routes to the engine.
  constexpr std::wstring_view kRegexPrefix{L"r:"};
  if (raw.size() >= kRegexPrefix.size() &&
      raw.compare(0, kRegexPrefix.size(), kRegexPrefix) == 0) {
    out.mode = FilterMode::Regex;
    out.query = std::wstring(raw.substr(kRegexPrefix.size()));
    return out;
  }
  out.query = std::wstring(raw);
  if (raw.find(L'*') != std::wstring_view::npos ||
      raw.find(L'?') != std::wstring_view::npos) {
    out.mode = FilterMode::Wildcard;
  }
  return out;
}

std::vector<std::uint32_t> applyFilter(
    const fast_explorer::core::FileModelStore& store,
    const FilterPattern& pattern) {
  const std::size_t count = store.publishedCount();
  // The raw-index type is uint32_t everywhere else; a pane store
  // exceeding 4.29B entries is not architecturally supported.
  assert(count <= std::numeric_limits<std::uint32_t>::max());
  std::vector<std::uint32_t> out;
  out.reserve(count);
  if (pattern.isEmpty()) {
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(static_cast<std::uint32_t>(i));
    }
    return out;
  }
  for (std::size_t i = 0; i < count; ++i) {
    const auto& e = store.entryAt(i);
    if (pattern.matches(fast_explorer::core::nameView(e))) {
      out.push_back(static_cast<std::uint32_t>(i));
    }
  }
  return out;
}

}  // namespace fast_explorer::ui
