#include "core/perf-tracker.h"

#include <stdio.h>
#include <wchar.h>

#include <cstdarg>

namespace fast_explorer::core {

namespace {

constexpr size_t kDumpLineCapacity = 256;

const wchar_t* eventName(PerfTracker::EventId id) noexcept {
  switch (id) {
    case PerfTracker::EventId::AppLaunchStart:   return L"app.launch.start";
    case PerfTracker::EventId::AppInteractive:   return L"app.interactive";
    case PerfTracker::EventId::AppShutdownStart: return L"app.shutdown.start";
  }
  return L"unknown";
}

void emitLine(const wchar_t* prefix, const wchar_t* fmt, ...) noexcept {
  wchar_t line[kDumpLineCapacity];
  if (prefix) {
    OutputDebugStringW(prefix);
  }
  va_list args;
  va_start(args, fmt);
  // _snwprintf_s with _TRUNCATE writes a terminator on overflow and returns
  // -1 without invoking the invalid-parameter handler. Truncation is OK for
  // diagnostic output; we just drop the tail.
  const int rc = _vsnwprintf_s(line, kDumpLineCapacity, _TRUNCATE, fmt, args);
  va_end(args);
  if (rc < 0) {
    line[kDumpLineCapacity - 1] = L'\0';
  }
  OutputDebugStringW(line);
}

}  // namespace

PerfTracker& PerfTracker::instance() noexcept {
  static PerfTracker singleton;
  return singleton;
}

PerfTracker::PerfTracker() {
  LARGE_INTEGER freq{};
  if (QueryPerformanceFrequency(&freq)) {
    qpcFrequency_ = freq.QuadPart;
  }
}

void PerfTracker::record(EventId id, uint64_t auxiliary) noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);

  const uint64_t ticket = cursor_.fetch_add(1, std::memory_order_relaxed);
  const size_t slot = static_cast<size_t>(ticket % kCapacity);
  PublishedSlot& s = slots_[slot];

  // seq = 2*generation + 1 marks "in progress" so the consumer can skip a
  // half-written slot. The store is relaxed; the publishing release store
  // below establishes the happens-before edge for the payload.
  const uint64_t generation = ticket / kCapacity;
  const uint64_t inProgress = (generation * 2) + 1;
  const uint64_t published = (generation * 2) + 2;
  s.seq.store(inProgress, std::memory_order_relaxed);

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
  const uint64_t totalTickets = cursor_.load(std::memory_order_acquire);
  if (totalTickets == 0) {
    OutputDebugStringW(L"[PerfTracker] no events recorded\n");
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

  emitLine(nullptr,
           L"[PerfTracker] dump %zu events, freq=%lld Hz\n",
           count, qpcFrequency_);

  // Second pass: emit in ticket order so the timeline reads chronologically
  // even after the ring has wrapped.
  for (size_t i = 0; i < count; ++i) {
    const uint64_t ticket = startTicket + i;
    const PublishedSlot& s = slots_[ticket % kCapacity];
    const uint64_t seq = s.seq.load(std::memory_order_acquire);
    if ((seq & 1u) != 0u) {
      emitLine(nullptr, L"  [   skip] ticket=%llu (in progress)\n", ticket);
      continue;
    }
    const Event e = s.event;  // safe copy after acquire load of seq.
    const double ms = baselineSet ? ticksToMs(e.qpcTicks - baseline) : 0.0;
    emitLine(nullptr,
             L"  [%6.2f ms] tid=%u id=%s aux=%llu\n",
             ms, e.threadId, eventName(e.id), e.auxiliary);
  }
}

}  // namespace fast_explorer::core
