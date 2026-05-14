#pragma once

// Minimal self-contained test harness. No external dependencies; the registry
// lives in a function-static vector so multiple TUs can drop test cases into
// it via FE_TEST_CASE without coordinating link order. Switch to Catch2 or
// GoogleTest later if the suite outgrows this (Design §13.1 plan).

#include <cstdio>
#include <cwchar>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace fast_explorer::tests {

struct TestCase {
  const char* name;
  std::function<void()> body;
};

// Returns the singleton registry. Function-static so initialization order is
// deterministic across translation units.
inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    registry().push_back({name, std::move(body)});
  }
};

// Inherits std::exception so generic catch handlers (and the runner) see it
// uniformly. what() owns its storage to avoid pointer lifetime issues when
// the throwing scope unwinds.
class AssertionFailure : public std::exception {
 public:
  explicit AssertionFailure(std::string msg) : msg_(std::move(msg)) {}
  const char* what() const noexcept override { return msg_.c_str(); }

 private:
  std::string msg_;
};

// Single helper that builds the location string and throws. Centralizing the
// throw site keeps the FE_ASSERT_* macros tiny and avoids the prior shotgun
// surgery where each macro carried its own copy of the throw block.
[[noreturn]] inline void failAssertion(const char* expression,
                                       const char* file,
                                       int line) {
  std::string msg(expression);
  msg.append(" at ");
  msg.append(file);
  msg.append(":");
  msg.append(std::to_string(line));
  throw AssertionFailure(std::move(msg));
}

}  // namespace fast_explorer::tests

// Argument names are intentionally namespaced (fe_lhs__ / fe_rhs__) so that
// user expressions cannot collide with the macro's locals.
#define FE_TEST_CASE(name)                                                      \
  static void name();                                                           \
  static ::fast_explorer::tests::Registrar                                      \
      fe_test_registrar_##name(#name, &name);                                   \
  static void name()

#define FE_ASSERT_TRUE(expr)                                                    \
  do {                                                                          \
    if (!(expr)) {                                                              \
      ::fast_explorer::tests::failAssertion("FE_ASSERT_TRUE(" #expr ")",         \
                                            __FILE__, __LINE__);                \
    }                                                                           \
  } while (0)

#define FE_ASSERT_FALSE(expr) FE_ASSERT_TRUE(!(expr))

#define FE_ASSERT_EQ(a, b)                                                      \
  do {                                                                          \
    const auto fe_lhs__ = (a);                                                  \
    const auto fe_rhs__ = (b);                                                  \
    if (!(fe_lhs__ == fe_rhs__)) {                                              \
      ::fast_explorer::tests::failAssertion(                                    \
          "FE_ASSERT_EQ(" #a ", " #b ")", __FILE__, __LINE__);                  \
    }                                                                           \
  } while (0)

#define FE_ASSERT_NE(a, b)                                                      \
  do {                                                                          \
    const auto fe_lhs__ = (a);                                                  \
    const auto fe_rhs__ = (b);                                                  \
    if (!(fe_lhs__ != fe_rhs__)) {                                              \
      ::fast_explorer::tests::failAssertion(                                    \
          "FE_ASSERT_NE(" #a ", " #b ")", __FILE__, __LINE__);                  \
    }                                                                           \
  } while (0)

// Use this instead of FE_ASSERT_EQ for wide-character string comparisons.
// Comparing wide-string literals through FE_ASSERT_EQ would compare pointers
// (array-to-pointer decay), not contents.
#define FE_ASSERT_WSTREQ(actual, expected)                                      \
  do {                                                                          \
    const std::wstring fe_actual__ = (actual);                                  \
    const std::wstring fe_expected__ = (expected);                              \
    if (fe_actual__ != fe_expected__) {                                         \
      ::fast_explorer::tests::failAssertion(                                    \
          "FE_ASSERT_WSTREQ", __FILE__, __LINE__);                              \
    }                                                                           \
  } while (0)
