#include "core/name-arena.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

#include "core/ring-logger.h"

namespace fast_explorer::core {

namespace {

constexpr std::size_t roundUpTo(std::size_t value,
                                std::size_t multiple) noexcept {
  return ((value + multiple - 1) / multiple) * multiple;
}

}  // namespace

NameArena::NameArena() : NameArena(kDefaultReserveBytes) {}

NameArena::NameArena(std::size_t reserveBytes)
    : base_(nullptr),
      committed_(0),
      used_(0),
      reserved_(roundUpTo(reserveBytes == 0 ? kCommitChunkBytes : reserveBytes,
                          kCommitChunkBytes)) {
  void* p = VirtualAlloc(nullptr, reserved_, MEM_RESERVE, PAGE_NOACCESS);
  base_ = static_cast<wchar_t*>(p);
}

NameArena::~NameArena() {
  if (base_ != nullptr) {
    // MEM_RELEASE requires the size argument to be 0.
    VirtualFree(base_, 0, MEM_RELEASE);
  }
}

NameArena::NameArena(NameArena&& other) noexcept
    : base_(other.base_),
      committed_(other.committed_),
      used_(other.used_),
      reserved_(other.reserved_),
      logger_(other.logger_) {
  other.base_ = nullptr;
  other.committed_ = 0;
  other.used_ = 0;
  other.reserved_ = 0;
  other.logger_ = nullptr;
}

NameArena& NameArena::operator=(NameArena&& other) noexcept {
  if (this != &other) {
    if (base_ != nullptr) {
      VirtualFree(base_, 0, MEM_RELEASE);
    }
    base_ = other.base_;
    committed_ = other.committed_;
    used_ = other.used_;
    reserved_ = other.reserved_;
    logger_ = other.logger_;
    other.base_ = nullptr;
    other.committed_ = 0;
    other.used_ = 0;
    other.reserved_ = 0;
    other.logger_ = nullptr;
  }
  return *this;
}

std::wstring_view NameArena::intern(std::wstring_view name) {
  if (name.empty() || base_ == nullptr) {
    return std::wstring_view();
  }
  const std::size_t needBytes = name.size() * sizeof(wchar_t);
  // Multiplicative overflow guard on size_t.
  if (needBytes / sizeof(wchar_t) != name.size()) {
    if (logger_ != nullptr) {
      logger_->error(L"NameArena: size_t overflow on name (len=%zu)",
                     name.size());
    }
    return std::wstring_view();
  }
  if (needBytes > reserved_ - used_) {
    if (logger_ != nullptr) {
      logger_->error(
          L"NameArena: out of space (need=%zu used=%zu reserved=%zu)",
          needBytes, used_, reserved_);
    }
    return std::wstring_view();
  }
  while (committed_ < used_ + needBytes) {
    const std::size_t remaining = reserved_ - committed_;
    const std::size_t addBytes = std::min(kCommitChunkBytes, remaining);
    void* commitAddr = reinterpret_cast<char*>(base_) + committed_;
    void* p = VirtualAlloc(commitAddr, addBytes, MEM_COMMIT, PAGE_READWRITE);
    if (p == nullptr) {
      if (logger_ != nullptr) {
        logger_->error(
            L"NameArena: MEM_COMMIT failed (committed=%zu add=%zu err=%lu)",
            committed_, addBytes, ::GetLastError());
      }
      return std::wstring_view();
    }
    committed_ += addBytes;
  }
  wchar_t* dst = reinterpret_cast<wchar_t*>(
      reinterpret_cast<char*>(base_) + used_);
  std::memcpy(dst, name.data(), needBytes);
  used_ += needBytes;
  return std::wstring_view(dst, name.size());
}

void NameArena::reset() noexcept {
  if (base_ == nullptr || committed_ == 0) {
    committed_ = 0;
    used_ = 0;
    return;
  }
  if (VirtualFree(base_, committed_, MEM_DECOMMIT)) {
    committed_ = 0;
    used_ = 0;
    return;
  }
  // On VirtualFree failure (very rare) the arena keeps its current
  // committed_/used_ so the next intern continues on already-committed
  // pages rather than racing against partially-decommitted state.
  if (logger_ != nullptr) {
    logger_->error(L"NameArena: MEM_DECOMMIT failed (committed=%zu err=%lu)",
                   committed_, ::GetLastError());
  }
}

}  // namespace fast_explorer::core
