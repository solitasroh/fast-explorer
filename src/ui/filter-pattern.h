#pragma once

#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace fast_explorer::core {
class FileModelStore;
}

namespace fast_explorer::ui {

enum class FilterMode : std::uint8_t {
  Plain = 0,     // substring, ASCII case-insensitive
  Wildcard = 1,  // glob with * and ? (ASCII case-insensitive)
  Regex = 2,     // std::wregex ECMAScript syntax (ASCII case-insensitive)
};

// Compiled match predicate built from a user-typed query string +
// mode. Construction is failable for Regex mode: an invalid pattern
// leaves isValid() == false and matches() returns false for every
// input, so callers can paint a syntax-error indicator without
// throwing across the message-pump boundary. Plain / Wildcard
// patterns are always isValid().
//
// Empty query string: isEmpty() returns true, matches() returns
// true for every input — callers can short-circuit by checking
// isEmpty() before iterating, but the matches() contract still
// holds so a naive loop stays correct.
//
// Case folding: all three modes are ASCII case-insensitive via
// towlower under the C locale. Non-ASCII (accented Latin, Turkish
// dotted I, German ß, …) is left as-is. Korean / CJK ideographs
// have no case so they round-trip unchanged.
//
// Regex ReDoS guard: when the input name exceeds
// kMaxRegexInputWchars, the regex path returns false rather than
// invoking std::regex_search on it. This protects the UI thread
// against catastrophic backtracking on adversarial inputs.
class FilterPattern {
 public:
  // Length cap fed into std::regex_search to bound the worst-case
  // search time on the UI thread. Names beyond this are skipped
  // (returns false) rather than risking a pump-blocking backtrack.
  static constexpr std::size_t kMaxRegexInputWchars = 4096;

  // The default-constructed pattern matches everything (empty
  // Plain query). Cheap to construct and copy.
  FilterPattern() noexcept;

  // Compiles `query` under `mode`. May throw std::bad_alloc on
  // string ops; std::regex_error from a bad Regex query is
  // caught internally and surfaces as isValid() == false.
  FilterPattern(std::wstring_view query, FilterMode mode);

  // Copy semantics: std::wregex itself is copy-constructible, so
  // FilterPattern can be value-typed despite the optional<wregex>
  // member. Copies do allocate (one wregex deep-clone) but that
  // is bounded and only happens when the caller asks for it.
  FilterPattern(const FilterPattern&) = default;
  FilterPattern(FilterPattern&&) noexcept = default;
  FilterPattern& operator=(const FilterPattern&) = default;
  FilterPattern& operator=(FilterPattern&&) noexcept = default;

  [[nodiscard]] bool isEmpty() const noexcept { return query_.empty(); }
  [[nodiscard]] bool isValid() const noexcept { return valid_; }
  [[nodiscard]] FilterMode mode() const noexcept { return mode_; }
  [[nodiscard]] const std::wstring& query() const noexcept { return query_; }

  // Returns true when `name` matches the compiled pattern. For an
  // invalid pattern (Regex with syntax error) always returns false.
  // For an empty query always returns true regardless of mode.
  [[nodiscard]] bool matches(std::wstring_view name) const noexcept;

 private:
  std::wstring query_;            // lowercased, original case folded away
  FilterMode mode_ = FilterMode::Plain;
  bool valid_ = true;
  std::optional<std::wregex> regex_;
};

// Heuristic mode picker for the Spotlight popup: the user types a
// single line, and the popup needs to dispatch it to one of the
// three matchers without a separate toggle UI.
//   - prefix "r:"             -> Regex (with the prefix stripped)
//   - contains '*' or '?'     -> Wildcard
//   - otherwise               -> Plain
// The "r:" prefix is checked before the wildcard scan so a regex
// containing '?' or '*' (e.g. "r:foo.*\.txt") is honored as regex.
struct DetectedFilter {
  std::wstring query;
  FilterMode mode = FilterMode::Plain;
};
[[nodiscard]] DetectedFilter detectFilterMode(std::wstring_view raw);

// Returns the raw indices of every entry in `store` (in raw, not
// visible, order) whose nameView() satisfies `pattern.matches`.
// When `pattern.isEmpty()` returns true, the returned vector
// contains every published raw index in ascending order (callers
// can then map back to the existing visibleOrder permutation).
//
// O(N) where N = store.publishedCount(). Allocation is bounded by
// the number of matches; callers running on a hot path should
// .reserve() the output via the upper-bound publishedCount.
[[nodiscard]] std::vector<std::uint32_t> applyFilter(
    const fast_explorer::core::FileModelStore& store,
    const FilterPattern& pattern);

}  // namespace fast_explorer::ui
