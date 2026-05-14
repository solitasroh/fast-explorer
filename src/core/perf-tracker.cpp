#include "core/perf-tracker.h"

#include <stdio.h>
#include <wchar.h>

namespace fast_explorer::core {

namespace {

constexpr size_t kDumpLineCapacity = 256;

const wchar_t* eventName(PerfTracker::EventId id) noexcept {
  switch (id) {
    case PerfTracker::EventId::AppLaunchStart:   return L"app.launch.start";
    case PerfTracker::EventId::AppInteractive:   return L"app.interactive";
    case PerfTracker::EventId::AppShutdownStart: return L"app.shutdown.start";
    case PerfTracker::EventId::PaneOpenStart:    return L"pane.open.start";
    case PerfTracker::EventId::PaneFirstBatch:   return L"pane.first_batch";
  }
  return L"unknown";
}

void debugOutputSink(const wchar_t* line, void* /*userData*/) {
  OutputDebugStringW(line);
  OutputDebugStringW(L"\n");
}

}  // namespace

PerfTracker::PerfTracker() noexcept {
  LARGE_INTEGER freq{};
  if (QueryPerformanceFrequency(&freq)) {
    qpcFrequency_ = freq.QuadPart;
  }
}

void PerfTracker::record(EventId id, uint64_t auxiliary) noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);

  const uint64_t ticket = cursor_.fetch_add(1, std::memory_order_acq_rel);
  const size_t slot = static_cast<size_t>(ticket % kCapacity);
  PublishedSlot& s = slots_[slot];

  // seq = 2*generation + 1 marks "in progress". The release on inProgress
  // ensures the slot's previous published payload (if any, from a wrap) is
  // visible to readers before we start writing. The second release publishes
  // the new payload.
  const uint64_t generation = ticket / kCapacity;
  const uint64_t inProgress = (generation * 2) + 1;
  const uint64_t published = (generation * 2) + 2;
  s.seq.store(inProgress, std::memory_order_release);

  s.event.qpcTicks = now.QuadPart;
  s.event.auxiliary = auxiliary;
  s.event.threadId = GetCurrentThreadId();
  s.event.id = id;
  s.event.reserved = 0;

  s.seq.store(published, std::memory_order_release);
}

double PerfTracker::ticksToMs(int64_t ticks) const noexcept {
  if (qpcFrequency_ == 0) {
    return 0.0;
  }
  return (static_cast<double>(ticks) * 1000.0) / static_cast<double>(qpcFrequency_);
}

size_t PerfTracker::recordedCount() const noexcept {
  const uint64_t c = cursor_.load(std::memory_order_acquire);
  return c < kCapacity ? static_cast<size_t>(c) : kCapacity;
}

void PerfTracker::dumpToDebugOutput() const {
  dumpToCallback(&debugOutputSink, nullptr);
}

void PerfTracker::dumpToCallback(LineSink sink, void* userData) const {
  if (sink == nullptr) {
    return;
  }
  const uint64_t totalTickets = cursor_.load(std::memory_order_acquire);
  if (totalTickets == 0) {
    sink(L"[PerfTracker] no events recorded", userData);
    return;
  }

  const size_t count = totalTickets < kCapacity ? static_cast<size_t>(totalTickets) : kCapacity;
  const uint64_t startTicket = totalTickets < kCapacity ? 0 : totalTickets - kCapacity;

  // First pass: find baseline among published slots only.
  int64_t baseline = 0;
  bool baselineSet = false;
  for (size_t i = 0; i < count; ++i) {
    const uint64_t ticket = startTicket + i;
    const PublishedSlot& s = slots_[ticket % kCapacity];
    const uint64_t seq = s.seq.load(std::memory_order_acquire);
    if ((seq & 1u) != 0u) {
      continue;  // still in progress (should not happen post-shutdown)
    }
    if (!baselineSet || s.event.qpcTicks < baseline) {
      baseline = s.event.qpcTicks;
      baselineSet = true;
    }
  }

  wchar_t header[kDumpLineCapacity];
  _snwprintf_s(header, kDumpLineCapacity, _TRUNCATE,
               L"[PerfTracker] dump %zu events, freq=%lld Hz",
               count, qpcFrequency_);
  sink(header, userData);

  for (size_t i = 0; i < count; ++i) {
    const uint64_t ticket = startTicket + i;
    const PublishedSlot& s = slots_[ticket % kCapacity];
    const uint64_t seq = s.seq.load(std::memory_order_acquire);
    wchar_t line[kDumpLineCapacity];
    if ((seq & 1u) != 0u) {
      _snwprintf_s(line, kDumpLineCapacity, _TRUNCATE,
                   L"  [   skip] ticket=%llu (in progress)", ticket);
      sink(line, userData);
      continue;
    }
    const Event e = s.event;
    const double ms = baselineSet ? ticksToMs(e.qpcTicks - baseline) : 0.0;
    _snwprintf_s(line, kDumpLineCapacity, _TRUNCATE,
                 L"  [%6.2f ms] tid=%u id=%s aux=%llu",
                 ms, e.threadId, eventName(e.id), e.auxiliary);
    sink(line, userData);
  }
}

}  // namespace fast_explorer::core
