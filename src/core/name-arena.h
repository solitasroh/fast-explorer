#pragma once

#include <cstddef>
#include <string_view>

namespace fast_explorer::core {

class RingLogger;

class NameArena {
 public:
  static constexpr std::size_t kDefaultReserveBytes = 16ull * 1024 * 1024;
  static constexpr std::size_t kCommitChunkBytes    = 64ull * 1024;

  NameArena();
  explicit NameArena(std::size_t reserveBytes);

  ~NameArena();

  NameArena(const NameArena&) = delete;
  NameArena& operator=(const NameArena&) = delete;
  NameArena(NameArena&& other) noexcept;
  NameArena& operator=(NameArena&& other) noexcept;

  void setLogger(RingLogger* logger) noexcept { logger_ = logger; }

  // Returns a view aliasing the arena copy of `name`. The returned view's
  // pointer stays valid until reset() or destruction. An empty input
  // yields an empty view (no allocation). A non-empty input that yields
  // an empty view means the arena is full; the caller should treat this
  // as a fatal-for-this-pane condition.
  std::wstring_view intern(std::wstring_view name);

  // Decommits all pages; the reservation is kept and the arena is
  // immediately reusable. No-op when nothing is committed.
  void reset() noexcept;

  std::size_t committedBytes() const noexcept { return committed_; }
  std::size_t usedBytes() const noexcept { return used_; }
  std::size_t reservedBytes() const noexcept { return reserved_; }

  // False only if the constructor failed to reserve its virtual range.
  bool valid() const noexcept { return base_ != nullptr; }

 private:
  wchar_t* base_;
  std::size_t committed_;
  std::size_t used_;
  std::size_t reserved_;
  RingLogger* logger_ = nullptr;
};

}  // namespace fast_explorer::core
