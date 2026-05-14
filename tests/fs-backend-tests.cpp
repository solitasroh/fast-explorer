#include "test-harness.h"

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/fs-backend.h"

using fast_explorer::core::EnumerationError;
using fast_explorer::core::EnumerationHandle;
using fast_explorer::core::FileEntry;
using fast_explorer::core::IFsBackend;
using fast_explorer::core::Result;

namespace {

class StubHandle : public EnumerationHandle {
 public:
  std::size_t position = 0;
};

// Drives the IFsBackend contract from canned data — used to prove that
// the interface is implementable, that ownership flows through
// unique_ptr cleanly, and that the next() loop terminates on the empty
// optional.
class StubBackend : public IFsBackend {
 public:
  StubBackend() = default;

  void setOpenError(EnumerationError e) { openError_ = e; }
  void addEntry(FileEntry entry) { entries_.push_back(entry); }
  void setNextErrorAt(std::size_t pos, EnumerationError e) {
    nextErrorPos_ = pos;
    nextError_ = e;
  }

  Result<std::unique_ptr<EnumerationHandle>> openEnumeration(
      const std::wstring& /*path*/, std::stop_token /*tok*/) override {
    if (openError_ != EnumerationError::None) {
      return Result<std::unique_ptr<EnumerationHandle>>::failure(openError_);
    }
    return Result<std::unique_ptr<EnumerationHandle>>::success(
        std::make_unique<StubHandle>());
  }

  Result<std::optional<FileEntry>> next(EnumerationHandle& handle,
                                        std::stop_token /*tok*/) override {
    auto* stubPtr = dynamic_cast<StubHandle*>(&handle);
    FE_ASSERT_TRUE(stubPtr != nullptr);
    auto& stub = *stubPtr;
    if (nextError_ != EnumerationError::None && stub.position == nextErrorPos_) {
      return Result<std::optional<FileEntry>>::failure(nextError_);
    }
    if (stub.position >= entries_.size()) {
      return Result<std::optional<FileEntry>>::success(std::nullopt);
    }
    FileEntry e = entries_[stub.position];
    ++stub.position;
    return Result<std::optional<FileEntry>>::success(std::optional<FileEntry>(e));
  }

 private:
  EnumerationError openError_ = EnumerationError::None;
  std::vector<FileEntry> entries_;
  EnumerationError nextError_ = EnumerationError::None;
  std::size_t nextErrorPos_ = 0;
};

}  // namespace

FE_TEST_CASE(fs_backend_result_default_is_ok_with_default_value) {
  Result<int> r;
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_EQ(r.value, 0);
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::None));
}

FE_TEST_CASE(fs_backend_result_success_carries_value) {
  auto r = Result<int>::success(42);
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_EQ(r.value, 42);
}

FE_TEST_CASE(fs_backend_result_failure_carries_error) {
  auto r = Result<int>::failure(EnumerationError::AccessDenied);
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::AccessDenied));
}

FE_TEST_CASE(fs_backend_result_supports_move_only_payloads) {
  auto r = Result<std::unique_ptr<int>>::success(std::make_unique<int>(7));
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value != nullptr);
  FE_ASSERT_EQ(*r.value, 7);
}

FE_TEST_CASE(fs_backend_handle_has_virtual_destructor) {
  FE_ASSERT_TRUE(std::has_virtual_destructor_v<EnumerationHandle>);
}

FE_TEST_CASE(fs_backend_handle_is_non_copyable) {
  FE_ASSERT_FALSE(std::is_copy_constructible_v<EnumerationHandle>);
  FE_ASSERT_FALSE(std::is_copy_assignable_v<EnumerationHandle>);
}

FE_TEST_CASE(fs_backend_interface_is_abstract) {
  FE_ASSERT_TRUE(std::is_abstract_v<IFsBackend>);
  FE_ASSERT_TRUE(std::has_virtual_destructor_v<IFsBackend>);
}

FE_TEST_CASE(fs_backend_interface_is_non_copyable) {
  FE_ASSERT_FALSE(std::is_copy_constructible_v<IFsBackend>);
  FE_ASSERT_FALSE(std::is_copy_assignable_v<IFsBackend>);
}

FE_TEST_CASE(fs_backend_stub_open_failure_propagates) {
  StubBackend backend;
  backend.setOpenError(EnumerationError::PathNotFound);
  auto r = backend.openEnumeration(L"X:\\missing", std::stop_token{});
  FE_ASSERT_FALSE(r.ok());
  FE_ASSERT_EQ(static_cast<int>(r.error),
               static_cast<int>(EnumerationError::PathNotFound));
  FE_ASSERT_TRUE(r.value == nullptr);
}

FE_TEST_CASE(fs_backend_stub_open_success_returns_handle) {
  StubBackend backend;
  auto r = backend.openEnumeration(L"X:\\anything", std::stop_token{});
  FE_ASSERT_TRUE(r.ok());
  FE_ASSERT_TRUE(r.value != nullptr);
}

FE_TEST_CASE(fs_backend_stub_next_streams_entries_then_signals_end) {
  StubBackend backend;
  FileEntry a{};
  a.size = 11;
  FileEntry b{};
  b.size = 22;
  backend.addEntry(a);
  backend.addEntry(b);

  auto open = backend.openEnumeration(L"X:\\", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto& handle = *open.value;

  auto r1 = backend.next(handle, std::stop_token{});
  FE_ASSERT_TRUE(r1.ok());
  FE_ASSERT_TRUE(r1.value.has_value());
  FE_ASSERT_EQ(r1.value->size, static_cast<uint64_t>(11));

  auto r2 = backend.next(handle, std::stop_token{});
  FE_ASSERT_TRUE(r2.ok());
  FE_ASSERT_TRUE(r2.value.has_value());
  FE_ASSERT_EQ(r2.value->size, static_cast<uint64_t>(22));

  auto r3 = backend.next(handle, std::stop_token{});
  FE_ASSERT_TRUE(r3.ok());
  FE_ASSERT_FALSE(r3.value.has_value());
}

FE_TEST_CASE(fs_backend_stub_next_mid_stream_error_aborts) {
  StubBackend backend;
  FileEntry a{};
  a.size = 1;
  FileEntry b{};
  b.size = 2;
  FileEntry c{};
  c.size = 3;
  backend.addEntry(a);
  backend.addEntry(b);
  backend.addEntry(c);
  backend.setNextErrorAt(1, EnumerationError::AccessDenied);

  auto open = backend.openEnumeration(L"X:\\", std::stop_token{});
  FE_ASSERT_TRUE(open.ok());
  auto& handle = *open.value;

  auto r1 = backend.next(handle, std::stop_token{});
  FE_ASSERT_TRUE(r1.ok());
  FE_ASSERT_TRUE(r1.value.has_value());

  auto r2 = backend.next(handle, std::stop_token{});
  FE_ASSERT_FALSE(r2.ok());
  FE_ASSERT_EQ(static_cast<int>(r2.error),
               static_cast<int>(EnumerationError::AccessDenied));
}
