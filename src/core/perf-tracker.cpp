#include "core/perf-tracker.h"

#include <cwchar>

namespace fast_explorer::core {

namespace {

const wchar_t* eventName(PerfTracker::EventId id) noexcept {
  switch (id) {
    case PerfTracker::EventId::AppLaunchStart:   return L"app.launch.start";
    case PerfTracker::EventId::AppInteractive:   return L"app.interactive";
    case PerfTracker::EventId::AppShutdownStart: return L"app.shutdown.start";
  }
  return L"unknown";
}

}  // namespace

PerfTracker& PerfTracker::instance() {
  static PerfTracker singleton;
  return singleton;
}

PerfTracker::PerfTracker() {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  qpcFrequency_ = freq.QuadPart;
}

void PerfTracker::record(EventId id, uint64_t auxiliary) noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);

  const uint64_t slot = cursor_.fetch_add(1, std::memory_order_relaxed) % kCapacity;
  Event& e = events_[slot];
  e.qpcTicks = now.QuadPart;
  e.auxiliary = auxiliary;
  e.threadId = GetCurrentThreadId();
  e.id = id;
  e.reserved = 0;
}

double PerfTracker::ticksToMs(int64_t ticks) const noexcept {
  if (qpcFrequency_ == 0) {
    return 0.0;
  }
  return (static_cast<double>(ticks) * 1000.0) / static_cast<double>(qpcFrequency_);
}

size_t PerfTracker::recordedCount() const noexcept {
  const uint64_t c = cursor_.load(std::memory_order_relaxed);
  return c < kCapacity ? static_cast<size_t>(c) : kCapacity;
}

void PerfTracker::dumpToDebugOutput() const {
  const size_t count = recordedCount();
  if (count == 0) {
    OutputDebugStringW(L"[PerfTracker] no events recorded\n");
    return;
  }

  // Baseline = earliest recorded tick among the captured slots.
  int64_t baseline = events_[0].qpcTicks;
  for (size_t i = 1; i < count; ++i) {
    if (events_[i].qpcTicks < baseline) {
      baseline = events_[i].qpcTicks;
    }
  }

  wchar_t line[256];
  swprintf_s(line, L"[PerfTracker] dump %zu events, freq=%lld Hz\n", count, qpcFrequency_);
  OutputDebugStringW(line);

  for (size_t i = 0; i < count; ++i) {
    const Event& e = events_[i];
    const double ms = ticksToMs(e.qpcTicks - baseline);
    swprintf_s(line, L"  [%6.2f ms] tid=%u id=%s aux=%llu\n",
               ms, e.threadId, eventName(e.id), e.auxiliary);
    OutputDebugStringW(line);
  }
}

}  // namespace fast_explorer::core
