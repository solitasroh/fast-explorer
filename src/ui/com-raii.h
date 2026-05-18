#pragma once

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

namespace fast_explorer::ui {

template <typename T>
class ComPtr {
 public:
  ComPtr() noexcept = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  ComPtr& operator=(ComPtr&& o) noexcept {
    if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
    return *this;
  }
  ~ComPtr() { reset(); }
  T*  get() const noexcept { return p_; }
  T** put() noexcept { reset(); return &p_; }
  T*  operator->() const noexcept { return p_; }
  explicit operator bool() const noexcept { return p_ != nullptr; }
  void reset() noexcept { if (p_) { p_->Release(); p_ = nullptr; } }

 private:
  T* p_ = nullptr;
};

class PidlOwner {
 public:
  PidlOwner() noexcept = default;
  explicit PidlOwner(LPITEMIDLIST p) noexcept : p_(p) {}
  PidlOwner(const PidlOwner&) = delete;
  PidlOwner& operator=(const PidlOwner&) = delete;
  PidlOwner(PidlOwner&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  PidlOwner& operator=(PidlOwner&& o) noexcept {
    if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
    return *this;
  }
  ~PidlOwner() { reset(); }
  LPITEMIDLIST get() const noexcept { return p_; }
  LPITEMIDLIST release() noexcept {
    LPITEMIDLIST t = p_; p_ = nullptr; return t;
  }
  void reset(LPITEMIDLIST p = nullptr) noexcept {
    if (p_) CoTaskMemFree(p_);
    p_ = p;
  }
  explicit operator bool() const noexcept { return p_ != nullptr; }

 private:
  LPITEMIDLIST p_ = nullptr;
};

class MenuOwner {
 public:
  MenuOwner() noexcept = default;
  explicit MenuOwner(HMENU h) noexcept : h_(h) {}
  MenuOwner(const MenuOwner&) = delete;
  MenuOwner& operator=(const MenuOwner&) = delete;
  MenuOwner(MenuOwner&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  MenuOwner& operator=(MenuOwner&& o) noexcept {
    if (this != &o) { reset(); h_ = o.h_; o.h_ = nullptr; }
    return *this;
  }
  ~MenuOwner() { reset(); }
  HMENU get() const noexcept { return h_; }
  void reset(HMENU h = nullptr) noexcept {
    if (h_) DestroyMenu(h_);
    h_ = h;
  }
  explicit operator bool() const noexcept { return h_ != nullptr; }

 private:
  HMENU h_ = nullptr;
};

}  // namespace fast_explorer::ui
