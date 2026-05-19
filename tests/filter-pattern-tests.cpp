#include "test-harness.h"
#include "ui/filter-pattern.h"

using fast_explorer::ui::applyFilter;
using fast_explorer::ui::detectFilterMode;
using fast_explorer::ui::DetectedFilter;
using fast_explorer::ui::FilterMode;
using fast_explorer::ui::FilterPattern;

// --- Plain mode ---------------------------------------------------

FE_TEST_CASE(FilterPattern_DefaultConstructed_MatchesEverything) {
  FilterPattern p;
  FE_ASSERT_TRUE(p.isEmpty());
  FE_ASSERT_TRUE(p.isValid());
  FE_ASSERT_TRUE(p.matches(L"anything.txt"));
  FE_ASSERT_TRUE(p.matches(L""));
}

FE_TEST_CASE(FilterPattern_Plain_EmptyQuery_MatchesEverything) {
  FilterPattern p(L"", FilterMode::Plain);
  FE_ASSERT_TRUE(p.matches(L"report.pdf"));
  FE_ASSERT_TRUE(p.matches(L""));
}

FE_TEST_CASE(FilterPattern_Plain_SubstringMatch_CaseInsensitive) {
  FilterPattern p(L"Report", FilterMode::Plain);
  FE_ASSERT_TRUE(p.matches(L"annual-report-2025.pdf"));
  FE_ASSERT_TRUE(p.matches(L"REPORT.docx"));
  FE_ASSERT_TRUE(p.matches(L"report"));
  FE_ASSERT_FALSE(p.matches(L"summary.pdf"));
  FE_ASSERT_FALSE(p.matches(L""));
}

FE_TEST_CASE(FilterPattern_Plain_KoreanSubstring_Matches) {
  FilterPattern p(L"보고", FilterMode::Plain);
  FE_ASSERT_TRUE(p.matches(L"연간보고서.pdf"));
  FE_ASSERT_FALSE(p.matches(L"요약.pdf"));
}

// --- Wildcard mode ------------------------------------------------

FE_TEST_CASE(FilterPattern_Wildcard_StarExtension) {
  FilterPattern p(L"*.txt", FilterMode::Wildcard);
  FE_ASSERT_TRUE(p.matches(L"notes.txt"));
  FE_ASSERT_TRUE(p.matches(L"readme.txt"));
  FE_ASSERT_FALSE(p.matches(L"image.png"));
  FE_ASSERT_FALSE(p.matches(L"txt"));   // missing dot prefix
}

FE_TEST_CASE(FilterPattern_Wildcard_StarPrefix) {
  FilterPattern p(L"report*", FilterMode::Wildcard);
  FE_ASSERT_TRUE(p.matches(L"report-2024.pdf"));
  FE_ASSERT_TRUE(p.matches(L"REPORT.txt"));
  FE_ASSERT_FALSE(p.matches(L"annual-report.pdf"));
}

FE_TEST_CASE(FilterPattern_Wildcard_QuestionMarkSingleChar) {
  FilterPattern p(L"?.txt", FilterMode::Wildcard);
  FE_ASSERT_TRUE(p.matches(L"a.txt"));
  FE_ASSERT_TRUE(p.matches(L"z.txt"));
  FE_ASSERT_FALSE(p.matches(L"ab.txt"));
  FE_ASSERT_FALSE(p.matches(L".txt"));
}

FE_TEST_CASE(FilterPattern_Wildcard_StarInTheMiddle) {
  FilterPattern p(L"foo*bar*.log", FilterMode::Wildcard);
  FE_ASSERT_TRUE(p.matches(L"foo-2024-bar-final.log"));
  FE_ASSERT_TRUE(p.matches(L"foobar.log"));
  FE_ASSERT_FALSE(p.matches(L"foo-bar.txt"));
}

FE_TEST_CASE(FilterPattern_Wildcard_TrailingStar_MatchesEmptySuffix) {
  FilterPattern p(L"abc*", FilterMode::Wildcard);
  FE_ASSERT_TRUE(p.matches(L"abc"));
  FE_ASSERT_TRUE(p.matches(L"abcdef"));
  FE_ASSERT_FALSE(p.matches(L"ab"));
}

// --- Regex mode ---------------------------------------------------

FE_TEST_CASE(FilterPattern_Regex_BasicCharacterClass) {
  FilterPattern p(L"^[a-z]+\\.txt$", FilterMode::Regex);
  FE_ASSERT_TRUE(p.isValid());
  FE_ASSERT_TRUE(p.matches(L"notes.txt"));
  FE_ASSERT_TRUE(p.matches(L"NOTES.TXT"));        // case-insensitive
  FE_ASSERT_FALSE(p.matches(L"notes2024.txt"));  // digits excluded
  FE_ASSERT_FALSE(p.matches(L"notes.pdf"));
}

FE_TEST_CASE(FilterPattern_Regex_InvalidSyntax_NotValid_MatchesNothing) {
  // Unbalanced bracket — std::regex_error swallowed inside ctor,
  // isValid() falls to false, every match short-circuits to false.
  FilterPattern p(L"[unbalanced", FilterMode::Regex);
  FE_ASSERT_FALSE(p.isValid());
  FE_ASSERT_FALSE(p.matches(L"unbalanced.txt"));
  FE_ASSERT_FALSE(p.matches(L"anything"));
}

FE_TEST_CASE(FilterPattern_Regex_EmptyQuery_AlwaysMatches_RegardlessOfMode) {
  FilterPattern p(L"", FilterMode::Regex);
  FE_ASSERT_TRUE(p.isValid());
  FE_ASSERT_TRUE(p.matches(L"anything.log"));
}

// --- detectFilterMode ---------------------------------------------

FE_TEST_CASE(DetectFilterMode_EmptyString_PlainEmpty) {
  const auto d = detectFilterMode(L"");
  FE_ASSERT_TRUE(d.mode == FilterMode::Plain);
  FE_ASSERT_TRUE(d.query.empty());
}

FE_TEST_CASE(DetectFilterMode_PlainText_StaysPlain) {
  const auto d = detectFilterMode(L"report");
  FE_ASSERT_TRUE(d.mode == FilterMode::Plain);
  FE_ASSERT_WSTREQ(d.query, L"report");
}

FE_TEST_CASE(DetectFilterMode_ContainsStar_RoutesToWildcard) {
  const auto d = detectFilterMode(L"*.txt");
  FE_ASSERT_TRUE(d.mode == FilterMode::Wildcard);
  FE_ASSERT_WSTREQ(d.query, L"*.txt");
}

FE_TEST_CASE(DetectFilterMode_ContainsQuestionMark_RoutesToWildcard) {
  const auto d = detectFilterMode(L"???.log");
  FE_ASSERT_TRUE(d.mode == FilterMode::Wildcard);
  FE_ASSERT_WSTREQ(d.query, L"???.log");
}

FE_TEST_CASE(DetectFilterMode_RegexPrefix_RoutesToRegex_StripsPrefix) {
  const auto d = detectFilterMode(L"r:^[a-z]+$");
  FE_ASSERT_TRUE(d.mode == FilterMode::Regex);
  FE_ASSERT_WSTREQ(d.query, L"^[a-z]+$");
}

FE_TEST_CASE(DetectFilterMode_RegexPrefix_BeatsWildcardWhenBothPresent) {
  // Deliberate regex containing * or ? must still route to Regex
  // because the prefix takes precedence over the wildcard scan.
  const auto d = detectFilterMode(L"r:foo.*\\.txt");
  FE_ASSERT_TRUE(d.mode == FilterMode::Regex);
  FE_ASSERT_WSTREQ(d.query, L"foo.*\\.txt");
}

// --- applyFilter --------------------------------------------------
// applyFilter is integration-light here — it just iterates store
// entries and consults matches(). The store-side wiring is
// covered by file-model-store tests; we exercise the empty-pattern
// short-circuit + a tiny populated store via a lightweight fixture
// in F3 once PaneController wiring lands. For F1 the matches()
// contract is sufficient.
