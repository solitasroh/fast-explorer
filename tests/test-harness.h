#pragma once

// Minimal self-contained test harness. No external dependencies; the registry
// lives in a function-static vector so multiple TUs can drop test cases into
// it via FE_TEST_CASE without coordinating link order. Switch to Catch2 or
// GoogleTest later if the suite outgrows this (Design §13.1 plan).

#include <cstdio>
#include <cwchar>
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

// Exceptions only flow up to the runner; tests must not be marked noexcept.
class AssertionFailure {
 public:
  explicit AssertionFailure(std::string msg) : msg_(std::move(msg)) {}
  const std::string& what() const noexcept { return msg_; }
 private:
  std::string msg_;
};

}  // namespace fast_explorer::tests

#define FE_TEST_CASE(name)                                                      \
  static void name();                                                           \
  static ::fast_explorer::tests::Registrar                                      \
      fe_test_registrar_##name(#name, &name);                                   \
  static void name()

#define FE_ASSERT_TRUE(expr)                                                    \
  do {                                                                          \
    if (!(expr)) {                                                              \
      throw ::fast_explorer::tests::AssertionFailure(                           \
          std::string("FE_ASSERT_TRUE(" #expr ") at ") + __FILE__ + ":" +       \
          std::to_string(__LINE__));                                            \
    }                                                                           \
  } while (0)

#define FE_ASSERT_FALSE(expr) FE_ASSERT_TRUE(!(expr))

#define FE_ASSERT_EQ(a, b)                                                      \
  do {                                                                          \
    auto _a = (a);                                                              \
    auto _b = (b);                                                              \
    if (!(_a == _b)) {                                                          \
      throw ::fast_explorer::tests::AssertionFailure(                           \
          std::string("FE_ASSERT_EQ(" #a ", " #b ") at ") + __FILE__ + ":" +    \
          std::to_string(__LINE__));                                            \
    }                                                                           \
  } while (0)

#define FE_ASSERT_NE(a, b)                                                      \
  do {                                                                          \
    auto _a = (a);                                                              \
    auto _b = (b);                                                              \
    if (!(_a != _b)) {                                                          \
      throw ::fast_explorer::tests::AssertionFailure(                           \
          std::string("FE_ASSERT_NE(" #a ", " #b ") at ") + __FILE__ + ":" +    \
          std::to_string(__LINE__));                                            \
    }                                                                           \
  } while (0)

#define FE_ASSERT_WSTREQ(actual, expected)                                      \
  do {                                                                          \
    std::wstring _act = (actual);                                               \
    std::wstring _exp = (expected);                                             \
    if (_act != _exp) {                                                         \
      throw ::fast_explorer::tests::AssertionFailure(                           \
          std::string("FE_ASSERT_WSTREQ at ") + __FILE__ + ":" +                \
          std::to_string(__LINE__));                                            \
    }                                                                           \
  } while (0)
