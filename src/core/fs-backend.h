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

 protected:
  EnumerationHandle() = default;
};

class IFsBackend {
 public:
  virtual ~IFsBackend() = default;

  // On ok(), the returned unique_ptr is guaranteed non-null. The caller
  // keeps the handle alive while iterating next(). The path must be a
  // valid std::wstring so backends can pass it as a null-terminated
  // LPCWSTR to Win32 APIs without an extra copy.
  virtual Result<std::unique_ptr<EnumerationHandle>> openEnumeration(
      const std::wstring& path, std::stop_token tok) = 0;

  // Empty optional + ok() means end-of-stream. Any non-ok result aborts
  // enumeration; the handle remains owned by the caller and the caller
  // releases its unique_ptr to clean up.
  virtual Result<std::optional<FileEntry>> next(
      EnumerationHandle& handle, std::stop_token tok) = 0;

  IFsBackend(const IFsBackend&) = delete;
  IFsBackend& operator=(const IFsBackend&) = delete;

 protected:
  IFsBackend() = default;
};

}  // namespace fast_explorer::core
