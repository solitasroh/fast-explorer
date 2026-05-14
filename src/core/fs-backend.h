#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

#include "core/file-entry.h"

namespace fast_explorer::core {

enum class EnumerationError : uint8_t {
  None = 0,
  PathNotFound,
  FileNotFound,
  AccessDenied,
  SharingViolation,
  NotReady,
  DirectoryNotSupported,
  InvalidSyntax,
  UncUnsupported,
  Canceled,
  Internal,
};

// Identifies which backend produced an EnumerationHandle. Used by each
// backend's next() to verify the handle is one it produced before doing
// a static_cast to its concrete handle type — avoids the RTTI cost of
// dynamic_cast on the per-entry hot path.
enum class BackendKind : uint8_t {
  Stub,
  Memory,
  Win32,
};

template <typename T>
struct Result {
  T value{};
  EnumerationError error = EnumerationError::None;

  constexpr bool ok() const noexcept {
    return error == EnumerationError::None;
  }

  static Result success(T v) {
    return Result{std::move(v), EnumerationError::None};
  }

  static Result failure(EnumerationError e) {
    Result r;
    r.error = e;
    return r;
  }
};

class EnumerationHandle {
 public:
  virtual ~EnumerationHandle() = default;

  EnumerationHandle(const EnumerationHandle&) = delete;
  EnumerationHandle& operator=(const EnumerationHandle&) = delete;

  BackendKind kind() const noexcept { return kind_; }

 protected:
  explicit EnumerationHandle(BackendKind k) noexcept : kind_(k) {}

 private:
  BackendKind kind_;
};

class IFsBackend {
 public:
  virtual ~IFsBackend() = default;

  // On ok(), the returned unique_ptr is guaranteed non-null. The caller
  // keeps the handle alive while iterating next(). The path must be a
  // valid std::wstring so backends can pass it as a null-terminated
  // LPCWSTR to Win32 APIs without an extra copy. Names returned by
  // prior next() calls on the same handle remain valid until the
  // handle is destroyed.
  virtual Result<std::unique_ptr<EnumerationHandle>> openEnumeration(
      const std::wstring& path, std::stop_token tok) = 0;

  // Empty optional + ok() means end-of-stream. Any non-ok result aborts
  // enumeration. The handle must have been produced by this backend
  // (kind() matches) — passing a foreign handle yields Internal.
  virtual Result<std::optional<FileEntry>> next(
      EnumerationHandle& handle, std::stop_token tok) = 0;

  IFsBackend(const IFsBackend&) = delete;
  IFsBackend& operator=(const IFsBackend&) = delete;

 protected:
  IFsBackend() = default;
};

}  // namespace fast_explorer::core
