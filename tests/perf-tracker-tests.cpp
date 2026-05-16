#include "test-harness.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "core/perf-tracker.h"

using fast_explorer::core::PerfTracker;

namespace {

struct CapturedLine {
  std::wstring text;
};

void appendSink(const wchar_t* line, void* userData) {
  auto* out = static_cast<std::vector<CapturedLine>*>(userData);
  CapturedLine cl;
  cl.text = line;
  out->push_back(std::move(cl));
}

bool anyLineContains(const std::vector<CapturedLine>& lines,
                     std::wstring_view needle) {
  for (const auto& l : lines) {
    if (l.text.find(needle) != std::wstring::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

FE_TEST_CASE(PerfTracker_FreshState_RecordsZero) {
  PerfTracker tracker;
  FE_ASSERT_EQ(tracker.recordedCount(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(PerfTracker_Record_BumpsRecordedCount) {
  PerfTracker tracker;
  tracker.record(PerfTracker::EventId::AppLaunchStart);
  FE_ASSERT_EQ(tracker.recordedCount(), static_cast<std::size_t>(1));
  tracker.record(PerfTracker::EventId::AppInteractive);
  FE_ASSERT_EQ(tracker.recordedCount(), static_cast<std::size_t>(2));
}

FE_TEST_CASE(PerfTracker_MemoryProbe_DumpsWithLabel) {
  PerfTracker tracker;
  tracker.record(PerfTracker::EventId::MemoryProbe, 1024 * 1024);
  std::vector<CapturedLine> lines;
  tracker.dumpToCallback(&appendSink, &lines);
  FE_ASSERT_TRUE(anyLineContains(lines, L"memory.probe"));
}

FE_TEST_CASE(PerfTracker_MemoryProbe_AuxiliaryFlowsThroughDump) {
  PerfTracker tracker;
  // Auxiliary == byte count; the dump emits it as `aux=<n>` in the
  // line. Use a sentinel large enough not to collide with the
  // QPC-based ms timestamp.
  constexpr uint64_t kSentinel = 0xC0FFEE'1234ULL;
  tracker.record(PerfTracker::EventId::MemoryProbe, kSentinel);
  std::vector<CapturedLine> lines;
  tracker.dumpToCallback(&appendSink, &lines);
  // The exact text is `aux=<unsigned-decimal>`. Format the sentinel
  // back to wide-decimal for the substring check.
  wchar_t needle[64];
  swprintf_s(needle, _countof(needle), L"aux=%llu",
             static_cast<unsigned long long>(kSentinel));
  FE_ASSERT_TRUE(anyLineContains(lines, needle));
}

FE_TEST_CASE(PerfTracker_MemoryProbe_EnumValueIsSix) {
  // Stable on-disk encoding: the value is also used by the crash
  // dump user-stream attachment, so locking it down prevents an
  // accidental renumbering from invalidating older dumps.
  FE_ASSERT_EQ(static_cast<int>(PerfTracker::EventId::MemoryProbe), 6);
}
