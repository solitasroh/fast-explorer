#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include "bench-fs-helper.h"
#include "core/location.h"
#include "explorer/shell-pidl-location.h"
#include "test-harness.h"

namespace {

class PidlHolder {
 public:
  explicit PidlHolder(PIDLIST_ABSOLUTE pidl) noexcept : pidl_(pidl) {}
  ~PidlHolder() {
    if (pidl_ != nullptr) {
      CoTaskMemFree(pidl_);
    }
  }

  PidlHolder(const PidlHolder&) = delete;
  PidlHolder& operator=(const PidlHolder&) = delete;

  [[nodiscard]] LPCITEMIDLIST get() const noexcept { return pidl_; }

 private:
  PIDLIST_ABSOLUTE pidl_ = nullptr;
};

PidlHolder knownFolder(REFKNOWNFOLDERID id) {
  PIDLIST_ABSOLUTE pidl = nullptr;
  FE_ASSERT_TRUE(SUCCEEDED(SHGetKnownFolderIDList(
      id, KF_FLAG_DEFAULT, nullptr, &pidl)));
  FE_ASSERT_TRUE(pidl != nullptr);
  return PidlHolder(pidl);
}

}  // namespace

FE_TEST_CASE(ShellPidlLocation_ComputerFolder_MapsToThisPcAlias) {
  const auto pidl = knownFolder(FOLDERID_ComputerFolder);
  FE_ASSERT_WSTREQ(fast_explorer::ui::addressLocationForPidl(pidl.get()),
                   L"shell:ThisPC");
}

FE_TEST_CASE(ShellPidlLocation_NetworkFolder_MapsToNetworkAlias) {
  const auto pidl = knownFolder(FOLDERID_NetworkFolder);
  FE_ASSERT_WSTREQ(fast_explorer::ui::addressLocationForPidl(pidl.get()),
                   L"shell:Network");
}

FE_TEST_CASE(ShellPidlLocation_FileSystemFolder_ReturnsPath) {
  fast_explorer::tests::TempDir tmp(L"shell-pidl-location-fs");
  FE_ASSERT_TRUE(CreateDirectoryW(tmp.path().c_str(), nullptr) != 0);

  PidlHolder pidl(ILCreateFromPathW(tmp.path().c_str()));
  FE_ASSERT_TRUE(pidl.get() != nullptr);
  FE_ASSERT_WSTREQ(fast_explorer::ui::addressLocationForPidl(pidl.get()),
                   tmp.path());
}

FE_TEST_CASE(ShellPidlLocation_RecycleBin_MapsToShellNamespace) {
  const auto pidl = knownFolder(FOLDERID_RecycleBinFolder);
  const std::wstring address =
      fast_explorer::ui::addressLocationForPidl(pidl.get());

  FE_ASSERT_FALSE(address.empty());
  const auto loc = fast_explorer::core::parseLocation(address);
  FE_ASSERT_EQ(loc.kind, fast_explorer::core::LocationKind::ShellNamespace);
}
