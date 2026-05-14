#include "test-harness.h"

#include <cstdio>

int main() {
  auto& cases = fast_explorer::tests::registry();
  int passed = 0;
  int failed = 0;
  for (const auto& tc : cases) {
    try {
      tc.body();
      printf("[ ok ] %s\n", tc.name);
      ++passed;
    } catch (const fast_explorer::tests::AssertionFailure& f) {
      printf("[FAIL] %s\n       %s\n", tc.name, f.what().c_str());
      ++failed;
    } catch (const std::exception& e) {
      printf("[FAIL] %s\n       std::exception: %s\n", tc.name, e.what());
      ++failed;
    } catch (...) {
      printf("[FAIL] %s\n       unknown exception\n", tc.name);
      ++failed;
    }
  }
  printf("\n%d passed, %d failed (of %zu)\n", passed, failed, cases.size());
  return failed == 0 ? 0 : 1;
}
