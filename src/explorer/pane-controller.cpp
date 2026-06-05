#include "explorer/pane-controller.h"

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include <knownfolders.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include "winui_lite/chrome/com-raii.h"
#include "core/directory-enumerator.h"
#include "core/fs-backend.h"
#include "core/fs-watcher.h"
#include "core/location.h"
#include "core/path-utils.h"
#include "explorer/jthread-utils.h"
#include "explorer/messages.h"
#include "explorer/shell-pidl-location.h"

namespace fast_explorer::ui {

PaneController::PaneController(HWND hostWindow, std::size_t paneIndex)
    : hostWindow_(hostWindow),
      paneIndex_(paneIndex),
      store_(L""),
      sortCoord_(store_, hostWindow, paneIndex),
      shellWorker_(hostWindow, paneIndex) {}

PaneController::~PaneController() = default;

void PaneController::joinForTest() noexcept {
  if (worker_.joinable()) {
    worker_.join();
  }
}

uint32_t PaneController::generation() const noexcept {
  return store_.generation();
}

std::wstring PaneController::currentLocation() const {
  const auto location = fast_explorer::core::parseLocation(currentPath_);
  if (location.kind != fast_explorer::core::LocationKind::FileSystemPath) {
    return fast_explorer::core::serializeLocation(location);
  }
  return currentPath_;
}

namespace {

struct SyntheticRow {
  std::wstring name;
  std::wstring targetPath;
};

bool isShellLocation(const fast_explorer::core::Location& location) noexcept {
  return location.kind != fast_explorer::core::LocationKind::FileSystemPath;
}

bool isPathValid(const std::wstring& path) {
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;
  std::wstring internal;
  return toInternal(path, internal) == PathConvertError::None;
}

bool isServerOnlyUncPath(std::wstring_view path) noexcept {
  static constexpr std::wstring_view kDosUncPrefix = L"\\\\?\\UNC\\";
  std::wstring_view body;
  if (path.size() > kDosUncPrefix.size() &&
      path.compare(0, kDosUncPrefix.size(), kDosUncPrefix) == 0) {
    body = path.substr(kDosUncPrefix.size());
  } else if (path.size() >= 2 &&
             (path[0] == L'\\' || path[0] == L'/') &&
             (path[1] == L'\\' || path[1] == L'/')) {
    body = path.substr(2);
  } else {
    return false;
  }
  while (!body.empty() && (body.back() == L'\\' || body.back() == L'/')) {
    body.remove_suffix(1);
  }
  return !body.empty() &&
         body.find_first_of(L"\\/") == std::wstring_view::npos;
}

bool pathCanBeEnumerated(const std::wstring& path) {
  using fast_explorer::core::PathConvertError;
  using fast_explorer::core::toInternal;
  std::wstring internal;
  if (toInternal(path, internal) != PathConvertError::None) {
    return false;
  }
  // Server-only UNC paths ("\\server") are enumerated through
  // WNetEnumResource inside Win32FsBackend rather than by
  // GetFileAttributesW, which reports them as missing.
  if (isServerOnlyUncPath(path) || isServerOnlyUncPath(internal)) {
    return true;
  }
  const DWORD attrs = GetFileAttributesW(internal.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool isSyntheticLocation(const std::wstring& path) {
  return isShellLocation(fast_explorer::core::parseLocation(path));
}

bool samePathCaseInsensitive(const std::wstring& a,
                             const std::wstring& b) noexcept {
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring withoutTrailingSlash(std::wstring text) {
  while (text.size() > 3 &&
         (text.back() == L'\\' || text.back() == L'/')) {
    text.pop_back();
  }
  return text;
}

std::wstring linkDisplayName(std::wstring name) {
  constexpr std::wstring_view kLnk = L".lnk";
  if (name.size() >= kLnk.size()) {
    const std::wstring tail = name.substr(name.size() - kLnk.size());
    if (_wcsicmp(tail.c_str(), L".lnk") == 0) {
      name.resize(name.size() - kLnk.size());
    }
  }
  return name;
}

std::optional<std::wstring> knownFolderPath(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
  if (FAILED(hr) || raw == nullptr) {
    return std::nullopt;
  }
  std::wstring out(raw);
  CoTaskMemFree(raw);
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

class ScopedComInit {
 public:
  ScopedComInit() {
    hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    owns_ = SUCCEEDED(hr_);
  }

  ~ScopedComInit() {
    if (owns_) {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool usable() const noexcept {
    return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT hr_ = E_FAIL;
  bool owns_ = false;
};

std::wstring strRetToString(STRRET& sr, LPCITEMIDLIST relative) {
  wchar_t buf[MAX_PATH]{};
  if (FAILED(StrRetToBufW(&sr, relative, buf,
                          static_cast<UINT>(std::size(buf))))) {
    return {};
  }
  return std::wstring(buf);
}

PidlOwner parseShellNamespacePidl(const std::wstring& address) {
  PIDLIST_ABSOLUTE raw = nullptr;
  SFGAOF attrs = 0;
  if (FAILED(SHParseDisplayName(address.c_str(), nullptr, &raw, 0,
                                &attrs)) ||
      raw == nullptr) {
    return {};
  }
  return PidlOwner(raw);
}

ComPtr<IShellFolder> bindShellFolder(LPCITEMIDLIST absolute) {
  ComPtr<IShellFolder> folder;
  if (absolute == nullptr) {
    return folder;
  }
  if (FAILED(SHBindToObject(nullptr, absolute, nullptr,
                            IID_PPV_ARGS(folder.put())))) {
    folder.reset();
  }
  return folder;
}

std::uint16_t extensionOffsetForSynthetic(std::wstring_view name,
                                          bool isDirectory) noexcept {
  if (isDirectory) {
    return fast_explorer::core::kNoExtension;
  }
  const std::size_t pos = name.rfind(L'.');
  if (pos == std::wstring_view::npos || pos == 0 ||
      pos + 1 >= name.size() || pos > UINT16_MAX) {
    return fast_explorer::core::kNoExtension;
  }
  return static_cast<std::uint16_t>(pos);
}

std::uint32_t fileAttributesForShellItem(SFGAOF attrs,
                                         bool isDirectory) noexcept {
  std::uint32_t out =
      isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  if ((attrs & SFGAO_HIDDEN) != 0) {
    out |= FILE_ATTRIBUTE_HIDDEN;
  }
  if ((attrs & SFGAO_READONLY) != 0) {
    out |= FILE_ATTRIBUTE_READONLY;
  }
  return out;
}

std::uint8_t entryFlagsForAttributes(std::uint32_t attributes) noexcept {
  std::uint8_t flags = 0;
  if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    flags |= fast_explorer::core::file_entry_flags::kIsDirectory;
  }
  if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) {
    flags |= fast_explorer::core::file_entry_flags::kIsHidden;
  }
  if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) {
    flags |= fast_explorer::core::file_entry_flags::kIsSystem;
  }
  return flags;
}

std::optional<std::wstring> resolveShellLinkTarget(
    const std::wstring& linkPath) {
  ScopedComInit com;
  if (!com.usable()) {
    return std::nullopt;
  }

  IShellLinkW* link = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link));
  if (FAILED(hr) || link == nullptr) {
    return std::nullopt;
  }

  IPersistFile* persist = nullptr;
  hr = link->QueryInterface(IID_PPV_ARGS(&persist));
  if (SUCCEEDED(hr) && persist != nullptr) {
    hr = persist->Load(linkPath.c_str(), STGM_READ);
  }

  wchar_t target[MAX_PATH]{};
  if (SUCCEEDED(hr)) {
    hr = link->GetPath(target, static_cast<int>(std::size(target)),
                       nullptr, SLGP_RAWPATH);
  }

  if (persist != nullptr) {
    persist->Release();
  }
  link->Release();

  if (FAILED(hr) || target[0] == L'\0') {
    return std::nullopt;
  }
  return std::wstring(target);
}

void addUniqueSynthetic(std::vector<SyntheticRow>& rows,
                        std::wstring name,
                        std::wstring targetPath) {
  if (name.empty() || targetPath.empty()) {
    return;
  }
  auto alreadyPresent = [&](const SyntheticRow& row) {
    return _wcsicmp(row.targetPath.c_str(), targetPath.c_str()) == 0;
  };
  if (std::any_of(rows.begin(), rows.end(), alreadyPresent)) {
    return;
  }
  rows.push_back({std::move(name), std::move(targetPath)});
}

void appendMappedDriveRows(std::vector<SyntheticRow>& rows) {
  wchar_t drives[512]{};
  const DWORD length =
      GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
  if (length == 0 || length >= std::size(drives)) {
    return;
  }
  for (const wchar_t* p = drives; *p != L'\0'; p += std::wcslen(p) + 1) {
    std::wstring root(p);
    if (GetDriveTypeW(root.c_str()) == DRIVE_REMOTE) {
      addUniqueSynthetic(rows, withoutTrailingSlash(root), root);
    }
  }
}

std::optional<std::wstring> resolveShortcutFolderTarget(
    const std::wstring& folderPath) {
  const std::wstring targetLink =
      fast_explorer::core::joinPath(folderPath, L"target.lnk");
  if (auto target = resolveShellLinkTarget(targetLink);
      target.has_value() && !target->empty()) {
    return target;
  }

  const std::wstring search =
      fast_explorer::core::joinPath(folderPath, L"*.lnk");
  WIN32_FIND_DATAW fd{};
  const HANDLE h = FindFirstFileExW(search.c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr, 0);
  if (h == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  std::optional<std::wstring> out;
  do {
    const std::wstring child =
        fast_explorer::core::joinPath(folderPath, fd.cFileName);
    out = resolveShellLinkTarget(child);
  } while ((!out.has_value() || out->empty()) && FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}

bool isNetworkShortcutFolder(const std::wstring& folderPath) {
  wchar_t clsid[80]{};
  const std::wstring desktopIni =
      fast_explorer::core::joinPath(folderPath, L"desktop.ini");
  GetPrivateProfileStringW(L".ShellClassInfo", L"CLSID2", L"",
                           clsid, static_cast<DWORD>(std::size(clsid)),
                           desktopIni.c_str());
  if (_wcsicmp(clsid, L"{0AFACED1-E828-11D1-9187-B532F1E9575D}") == 0) {
    return true;
  }
  GetPrivateProfileStringW(L".ShellClassInfo", L"CLSID", L"",
                           clsid, static_cast<DWORD>(std::size(clsid)),
                           desktopIni.c_str());
  return _wcsicmp(clsid, L"{0AFACED1-E828-11D1-9187-B532F1E9575D}") == 0;
}

std::optional<std::wstring> resolveNetworkShortcutFolderTarget(
    const std::wstring& folderPath) {
  if (!isNetworkShortcutFolder(folderPath)) {
    return std::nullopt;
  }
  auto target = resolveShortcutFolderTarget(folderPath);
  if (!target.has_value() || target->empty() ||
      samePathCaseInsensitive(*target, folderPath)) {
    return std::nullopt;
  }
  return target;
}

void appendNetworkShortcutRows(std::vector<SyntheticRow>& rows) {
  const auto netHood = knownFolderPath(FOLDERID_NetHood);
  if (!netHood.has_value()) {
    return;
  }

  const std::wstring search = fast_explorer::core::joinPath(*netHood, L"*");
  WIN32_FIND_DATAW fd{};
  const HANDLE h = FindFirstFileExW(search.c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    std::wstring name(fd.cFileName);
    if (name == L"." || name == L"..") {
      continue;
    }
    const std::wstring child =
        fast_explorer::core::joinPath(*netHood, name);
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      const auto target = resolveShortcutFolderTarget(child);
      addUniqueSynthetic(rows, name,
                         target.has_value() && !target->empty()
                             ? *target
                             : child);
      continue;
    }
    if (auto target = resolveShellLinkTarget(child);
        target.has_value() && !target->empty()) {
      addUniqueSynthetic(rows, linkDisplayName(std::move(name)), *target);
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

std::wstring computeParent(const std::wstring& path) {
  if (path.empty()) {
    return std::wstring();
  }
  // Normalize away the \\?\ extended-length prefix so the separator
  // walk below sees a plain "X:\..." or "\\server\share\..." form.
  std::wstring p = fast_explorer::core::toDisplay(path);
  // Trim trailing separators except when we are already at the drive
  // root form "X:\".
  if (p.size() > 3) {
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
      p.pop_back();
    }
  }
  if (p.size() <= 3) {
    return std::wstring();
  }
  // UNC root detection: "\\server\share" has its share-separator at
  // the position of the third backslash counted from the start. If
  // there's no fourth separator, we're at the UNC root and going
  // "up" would land on "\\server" which the OS can't enumerate.
  if (p.size() >= 2 && (p[0] == L'\\' || p[0] == L'/') &&
      (p[1] == L'\\' || p[1] == L'/')) {
    const size_t shareSep = p.find_first_of(L"\\/", 2);
    if (shareSep == std::wstring::npos) {
      // "\\server" only — not a real folder.
      return std::wstring();
    }
    const size_t fourth = p.find_first_of(L"\\/", shareSep + 1);
    if (fourth == std::wstring::npos) {
      // "\\server\share" with no trailing folder — this is the UNC root.
      return std::wstring();
    }
  }
  const size_t lastSep = p.find_last_of(L"\\/");
  if (lastSep == std::wstring::npos) {
    return std::wstring();
  }
  if (lastSep == 2 && p[1] == L':') {
    return p.substr(0, 3);
  }
  return p.substr(0, lastSep);
}

}  // namespace

bool PaneController::openFolder(const std::wstring& path) {
  const auto location = fast_explorer::core::parseLocation(path);
  if (isShellLocation(location)) {
    const std::wstring previous = currentPath_;
    if (!navigateShellLocation(location, FilterResetPolicy::Clear)) {
      return false;
    }
    if (!previous.empty()) {
      backStack_.push_back(previous);
    }
    forwardStack_.clear();
    return true;
  }
  const std::wstring targetPath =
      resolveNetworkShortcutFolderTarget(path).value_or(path);
  if (!isPathValid(targetPath) || !pathCanBeEnumerated(targetPath)) {
    return false;
  }
  if (!currentPath_.empty()) {
    backStack_.push_back(currentPath_);
  }
  forwardStack_.clear();
  return navigateInternal(targetPath, FilterResetPolicy::Clear);
}

bool PaneController::back() {
  if (backStack_.empty()) {
    return false;
  }
  const std::wstring target = backStack_.back();
  const auto location = fast_explorer::core::parseLocation(target);
  const bool shell = isShellLocation(location);
  if (!shell && (!isPathValid(target) || !pathCanBeEnumerated(target))) {
    return false;
  }
  const std::wstring previous = currentPath_;
  const bool navigated =
      shell ? navigateShellLocation(location, FilterResetPolicy::Clear)
            : navigateInternal(target, FilterResetPolicy::Clear);
  if (!navigated) {
    return false;
  }
  backStack_.pop_back();
  if (!previous.empty()) {
    forwardStack_.push_back(previous);
  }
  return true;
}

bool PaneController::forward() {
  if (forwardStack_.empty()) {
    return false;
  }
  const std::wstring target = forwardStack_.back();
  const auto location = fast_explorer::core::parseLocation(target);
  const bool shell = isShellLocation(location);
  if (!shell && (!isPathValid(target) || !pathCanBeEnumerated(target))) {
    return false;
  }
  const std::wstring previous = currentPath_;
  const bool navigated =
      shell ? navigateShellLocation(location, FilterResetPolicy::Clear)
            : navigateInternal(target, FilterResetPolicy::Clear);
  if (!navigated) {
    return false;
  }
  forwardStack_.pop_back();
  if (!previous.empty()) {
    backStack_.push_back(previous);
  }
  return true;
}

bool PaneController::up() {
  const std::wstring parent = computeParent(currentPath_);
  if (parent.empty()) {
    return false;
  }
  return openFolder(parent);
}

bool PaneController::canGoUp() const {
  return !computeParent(currentPath_).empty();
}

bool PaneController::refresh() {
  if (currentPath_.empty()) {
    return false;
  }
  const auto location = fast_explorer::core::parseLocation(currentPath_);
  if (isShellLocation(location)) {
    return navigateShellLocation(location, FilterResetPolicy::Preserve);
  }
  if (!isPathValid(currentPath_) || !pathCanBeEnumerated(currentPath_)) {
    return false;
  }
  return navigateInternal(currentPath_, FilterResetPolicy::Preserve);
}

namespace {

bool shellOpenPath(const std::wstring& path, const std::wstring& cwd,
                   HWND host) noexcept {
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  // No SEE_MASK_FLAG_NO_UI: that flag suppresses not just the shell's
  // own error dialog but also the UAC consent prompt that AppInfo
  // raises when activating a manifest-elevated exe. Double-clicking
  // an admin-required installer was silently no-op'ing as a result.
  // SEE_MASK_NOASYNC keeps the call synchronous so AppInfo can wire
  // the consent.exe handshake back to our process before lpFile goes
  // out of scope when we return.
  info.fMask = SEE_MASK_NOASYNC;
  info.hwnd = host;
  // lpVerb = nullptr lets the shell pick the registered default verb
  // for the file class. For .exe that resolves to "open", same as
  // before; for .msi / .lnk / .url it matches Win Explorer's double-
  // click handling.
  info.lpVerb = nullptr;
  info.lpFile = path.c_str();
  // Parent folder as CWD — matches Win Explorer parity and is what
  // many installers (NSIS, InstallShield) implicitly assume when
  // reading bundled resources via relative paths.
  info.lpDirectory = cwd.empty() ? nullptr : cwd.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != FALSE;
}

}  // namespace

bool PaneController::resolveRowSourcePath(std::uint32_t row,
                                          std::wstring& out) const {
  const auto raw = store_.rawIndexForVisibleRow(row);
  if (!raw.has_value()) {
    return false;
  }
  if (!syntheticTargets_.empty()) {
    if (*raw >= syntheticTargets_.size() || syntheticTargets_[*raw].empty()) {
      return false;
    }
    out = syntheticTargets_[*raw];
    return true;
  }
  const auto& entry = store_.entryAt(*raw);
  out = fast_explorer::core::joinPath(
      currentPath_, fast_explorer::core::nameView(entry));
  return true;
}

bool PaneController::sourcePathForVisibleRow(std::uint32_t row,
                                             std::wstring& out) const {
  return resolveRowSourcePath(row, out);
}

bool PaneController::iconLocationForVisibleRow(std::uint32_t row,
                                               std::wstring& out) const {
  const auto raw = store_.rawIndexForVisibleRow(row);
  if (!raw.has_value() || syntheticIconLocations_.empty() ||
      *raw >= syntheticIconLocations_.size() ||
      syntheticIconLocations_[*raw].empty()) {
    return false;
  }
  out = syntheticIconLocations_[*raw];
  return true;
}

bool PaneController::openItem(std::uint32_t row) {
  std::wstring fullPath;
  if (!resolveRowSourcePath(row, fullPath)) {
    return false;
  }
  const auto raw = store_.rawIndexForVisibleRow(row);
  if (!raw.has_value()) {
    return false;
  }
  if (fast_explorer::core::isDirectory(store_.entryAt(*raw))) {
    const auto location = fast_explorer::core::parseLocation(fullPath);
    if (isShellLocation(location)) {
      const std::wstring previous = currentPath_;
      if (!navigateShellLocation(location, FilterResetPolicy::Preserve)) {
        return false;
      }
      if (!previous.empty()) {
        backStack_.push_back(previous);
      }
      forwardStack_.clear();
      return true;
    }
    if (!isPathValid(fullPath) || !pathCanBeEnumerated(fullPath)) {
      return false;
    }
    if (!currentPath_.empty()) {
      backStack_.push_back(currentPath_);
    }
    forwardStack_.clear();
    return navigateInternal(fullPath, FilterResetPolicy::Preserve);
  }
  const std::wstring cwd = isPathValid(currentPath_) ? currentPath_ : L"";
  return shellOpenPath(fullPath, cwd, hostWindow_);
}

bool PaneController::deleteItem(std::uint32_t row) {
  if (isSyntheticLocation(currentPath_)) {
    return false;
  }
  ShellCommand cmd;
  if (!resolveRowSourcePath(row, cmd.sourcePath)) {
    return false;
  }
  cmd.kind = ShellCommandKind::Delete;
  shellWorker_.request(std::move(cmd));
  return true;
}

bool PaneController::renameItem(std::uint32_t row,
                                const std::wstring& newName) {
  if (newName.empty() || isSyntheticLocation(currentPath_)) {
    return false;
  }
  ShellCommand cmd;
  if (!resolveRowSourcePath(row, cmd.sourcePath)) {
    return false;
  }
  cmd.kind = ShellCommandKind::Rename;
  cmd.newName = newName;
  shellWorker_.request(std::move(cmd));
  return true;
}

bool PaneController::createSubfolder(const std::wstring& name) {
  if (name.empty() || currentPath_.empty() ||
      isSyntheticLocation(currentPath_)) {
    return false;
  }
  ShellCommand cmd;
  cmd.kind = ShellCommandKind::CreateFolder;
  cmd.sourcePath = currentPath_;
  cmd.newName = name;
  shellWorker_.request(std::move(cmd));
  return true;
}

void PaneController::selectRaw(std::uint32_t rawIndex) {
  selectedRaws_.insert(rawIndex);
}

void PaneController::deselectRaw(std::uint32_t rawIndex) noexcept {
  selectedRaws_.erase(rawIndex);
}

void PaneController::clearSelection() noexcept {
  selectedRaws_.clear();
}

bool PaneController::isRawSelected(std::uint32_t rawIndex) const noexcept {
  return selectedRaws_.contains(rawIndex);
}

std::vector<int> PaneController::selectedRowsUnderCurrentOrder() const {
  std::vector<int> rows;
  if (selectedRaws_.empty()) {
    return rows;
  }
  const std::size_t count = store_.displayedCount();
  rows.reserve(selectedRaws_.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto raw = store_.rawIndexForVisibleRow(i);
    if (raw.has_value() && selectedRaws_.contains(*raw)) {
      rows.push_back(static_cast<int>(i));
    }
  }
  return rows;
}

SortDispatch PaneController::setGroupBy(
    fast_explorer::core::GroupKey key) {
  groupBy_ = key;
  // Capture wall-clock once; the same `now` is used by both the sort
  // comparator and any subsequent enumerateGroups call.
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER ui{};
  ui.LowPart  = ft.dwLowDateTime;
  ui.HighPart = ft.dwHighDateTime;
  groupNow_ = ui.QuadPart;
  return requestSort(sortCoord_.currentSortSpec().key);
}

bool PaneController::navigateInternal(const std::wstring& path,
                                      FilterResetPolicy filterPolicy) {
  using fast_explorer::core::DirectoryEnumerator;
  using fast_explorer::core::EnumerationError;

  stopAndJoin(worker_);
  // The enumeration worker is about to reset() the store. Any pending
  // background sort references entries_ via entryAt(); cancel it
  // first so the sort sees a coherent snapshot or exits early.
  sortCoord_.cancel();
  selectedRaws_.clear();
  if (filterPolicy == FilterResetPolicy::Clear) {
    currentFilter_ = FilterPattern{};
  }
  filterAppliedThrough_ = 0;
  fsWatcher_.stop();

  currentPath_ = path;
  store_.reset(path);
  syntheticTargets_.clear();
  syntheticIconLocations_.clear();
  const uint32_t gen = store_.generation();
  const HWND host = hostWindow_;
  const std::size_t paneIdx = paneIndex_;
  std::wstring localPath = path;

  // Order matters: workerActive_ must be true before the thread starts
  // appending, and the thread must clear it on exit (success, error,
  // or cancellation) so requestSort() can re-arm.
  workerActive_.store(true, std::memory_order_release);
  // Snapshot the toggle here so the worker uses the value at navigate
  // start, not whatever the user happens to flip mid-enum.
  const bool includeHiddenSnapshot = includeHidden_;
  worker_ = std::jthread([this, host, gen, paneIdx, includeHiddenSnapshot,
                          localPath = std::move(localPath)](std::stop_token tok) {
    DirectoryEnumerator::Config cfg{};
    cfg.includeHidden = includeHiddenSnapshot;
    DirectoryEnumerator enumerator(cfg);
    auto onBatch = [this, host, gen, paneIdx](std::size_t /*start*/,
                                              std::size_t /*count*/) {
      // publish() before PostMessage so the UI thread that processes
      // kWmFeEnumBatch observes the matching entries on its acquire-
      // load of publishedCount().
      const auto count = static_cast<std::uint32_t>(store_.itemCount());
      store_.publish(count);
      if (host) {
        PostMessageW(host, kWmFeEnumBatch,
                     makePaneWParam(paneIdx, gen),
                     static_cast<LPARAM>(count));
      }
    };
    const EnumerationError err =
        enumerator.run(backend_, localPath, tok, store_, onBatch);
    // Final publish() covers the case where the last batch was flushed
    // but onBatch was already invoked from inside enumerator.run; this
    // is a no-op if publishedCount already matches.
    store_.publish(static_cast<std::uint32_t>(store_.itemCount()));
    if (host) {
      const UINT msg = (err == EnumerationError::None ||
                        err == EnumerationError::Canceled)
                           ? kWmFeEnumComplete
                           : kWmFeEnumError;
      PostMessageW(host, msg, makePaneWParam(paneIdx, gen),
                   static_cast<LPARAM>(static_cast<int>(err)));
    }
    // Release-store after PostMessageW so any future worker-side
    // bookkeeping added between enumerator.run and the completion post
    // remains protected by the same release boundary requestSort()
    // acquires from.
    workerActive_.store(false, std::memory_order_release);
  });

  if (host != nullptr) {
    fsWatcher_.watch(path, host, kWmFeFsChange, paneIndex_);
  }
  return true;
}

bool PaneController::appendSyntheticEntry(std::wstring_view name,
                                          std::wstring targetPath,
                                          bool isDirectory,
                                          std::uint32_t attributes,
                                          std::wstring iconLocation) {
  if (name.empty() || name.size() > UINT16_MAX || targetPath.empty()) {
    return false;
  }
  if (isDirectory) {
    attributes |= FILE_ATTRIBUTE_DIRECTORY;
  } else {
    attributes &= ~FILE_ATTRIBUTE_DIRECTORY;
    if (attributes == 0) {
      attributes = FILE_ATTRIBUTE_NORMAL;
    }
  }

  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER modified{};
  modified.LowPart = ft.dwLowDateTime;
  modified.HighPart = ft.dwHighDateTime;

  fast_explorer::core::FileEntry entry{};
  entry.namePtr = name.data();
  entry.nameLength = static_cast<std::uint16_t>(name.size());
  entry.size = 0;
  entry.modifiedTime100ns = modified.QuadPart;
  entry.attributes = attributes;
  entry.extensionOffset = extensionOffsetForSynthetic(name, isDirectory);
  entry.flags = entryFlagsForAttributes(attributes);

  if (store_.appendEntry(entry) != fast_explorer::core::AppendResult::Stored) {
    return false;
  }
  if (iconLocation.empty()) {
    iconLocation = targetPath;
  }
  syntheticTargets_.push_back(std::move(targetPath));
  syntheticIconLocations_.push_back(std::move(iconLocation));
  return true;
}

void PaneController::populateThisPc() {
  wchar_t drives[512]{};
  const DWORD length =
      GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
  if (length == 0 || length >= std::size(drives)) {
    return;
  }
  for (const wchar_t* p = drives; *p != L'\0'; p += std::wcslen(p) + 1) {
    std::wstring root(p);
    const std::wstring name = withoutTrailingSlash(root);
    appendSyntheticEntry(name, std::move(root));
  }
}

void PaneController::populateNetworkShortcuts(bool includeMappedDrives) {
  std::vector<SyntheticRow> rows;
  if (includeMappedDrives) {
    appendMappedDriveRows(rows);
  }
  appendNetworkShortcutRows(rows);
  if (includeMappedDrives) {
    addUniqueSynthetic(rows, L"Network Shortcuts", L"shell:NetHood");
  }
  for (auto& row : rows) {
    appendSyntheticEntry(row.name, std::move(row.targetPath));
  }
}

bool PaneController::navigateShellNamespace(
    const fast_explorer::core::Location& location,
    FilterResetPolicy filterPolicy) {
  if (location.kind != fast_explorer::core::LocationKind::ShellNamespace ||
      location.value.empty()) {
    return false;
  }

  ScopedComInit com;
  if (!com.usable()) {
    return false;
  }
  PidlOwner root = parseShellNamespacePidl(location.value);
  if (!root) {
    return false;
  }
  ComPtr<IShellFolder> folder = bindShellFolder(root.get());
  if (!folder) {
    return false;
  }

  const SHCONTF enumFlags =
      static_cast<SHCONTF>(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS |
                           (includeHidden_ ? SHCONTF_INCLUDEHIDDEN : 0));
  ComPtr<IEnumIDList> en;
  if (FAILED(folder->EnumObjects(nullptr, enumFlags, en.put())) || !en) {
    return false;
  }

  const FilterPattern preservedFilter = currentFilter_;
  stopAndJoin(worker_);
  sortCoord_.cancel();
  selectedRaws_.clear();
  if (filterPolicy == FilterResetPolicy::Clear) {
    currentFilter_ = FilterPattern{};
  }
  filterAppliedThrough_ = 0;
  workerActive_.store(false, std::memory_order_release);
  fsWatcher_.stop();

  currentPath_ = fast_explorer::core::serializeLocation(location);
  store_.reset(currentPath_);
  syntheticTargets_.clear();
  syntheticIconLocations_.clear();

  LPITEMIDLIST relative = nullptr;
  ULONG fetched = 0;
  while (en->Next(1, &relative, &fetched) == S_OK && fetched == 1) {
    PidlOwner relOwner(relative);
    STRRET sr{};
    if (FAILED(folder->GetDisplayNameOf(relOwner.get(), SHGDN_NORMAL, &sr))) {
      continue;
    }
    const std::wstring name = strRetToString(sr, relOwner.get());
    if (name.empty()) {
      continue;
    }

    LPCITEMIDLIST relRaw = relOwner.get();
    SFGAOF attrs = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_HIDDEN |
                   SFGAO_READONLY;
    if (FAILED(folder->GetAttributesOf(1, &relRaw, &attrs))) {
      attrs = 0;
    }
    const bool isDirectory =
        (attrs & SFGAO_FOLDER) != 0 && (attrs & SFGAO_STREAM) == 0;

    PidlOwner childAbs(ILCombine(root.get(), relOwner.get()));
    if (!childAbs) {
      continue;
    }
    std::wstring target = addressLocationForPidl(childAbs.get());
    if (target.empty()) {
      continue;
    }
    const std::uint32_t fileAttrs =
        fileAttributesForShellItem(attrs, isDirectory);
    if (!appendSyntheticEntry(name, std::move(target), isDirectory,
                              fileAttrs) &&
        store_.itemCount() >=
            fast_explorer::core::FileModelStore::kMaxEntries) {
      break;
    }
  }

  store_.publish(static_cast<std::uint32_t>(store_.itemCount()));
  sortCoord_.reapplyAfterEnumeration();
  if (filterPolicy == FilterResetPolicy::Preserve &&
      !preservedFilter.isEmpty()) {
    setFilter(preservedFilter);
  }

  if (hostWindow_ != nullptr) {
    PostMessageW(hostWindow_, kWmFeEnumComplete,
                 makePaneWParam(paneIndex_, store_.generation()), 0);
  }
  return true;
}

bool PaneController::navigateShellLocation(
    const fast_explorer::core::Location& location,
    FilterResetPolicy filterPolicy) {
  if (location.kind == fast_explorer::core::LocationKind::ShellNamespace) {
    return navigateShellNamespace(location, filterPolicy);
  }
  if (location.kind != fast_explorer::core::LocationKind::ShellKnownFolder ||
      location.known == fast_explorer::core::KnownShellLocation::None) {
    return false;
  }

  const FilterPattern preservedFilter = currentFilter_;
  stopAndJoin(worker_);
  sortCoord_.cancel();
  selectedRaws_.clear();
  if (filterPolicy == FilterResetPolicy::Clear) {
    currentFilter_ = FilterPattern{};
  }
  filterAppliedThrough_ = 0;
  workerActive_.store(false, std::memory_order_release);
  fsWatcher_.stop();

  currentPath_ = fast_explorer::core::serializeLocation(location);
  store_.reset(currentPath_);
  syntheticTargets_.clear();
  syntheticIconLocations_.clear();

  switch (location.known) {
    case fast_explorer::core::KnownShellLocation::ThisPC:
      populateThisPc();
      break;
    case fast_explorer::core::KnownShellLocation::Network:
      populateNetworkShortcuts(true);
      break;
    case fast_explorer::core::KnownShellLocation::NetworkShortcuts:
      populateNetworkShortcuts(false);
      break;
    case fast_explorer::core::KnownShellLocation::None:
      return false;
  }

  store_.publish(static_cast<std::uint32_t>(store_.itemCount()));
  sortCoord_.reapplyAfterEnumeration();
  if (filterPolicy == FilterResetPolicy::Preserve &&
      !preservedFilter.isEmpty()) {
    setFilter(preservedFilter);
  }

  if (hostWindow_ != nullptr) {
    PostMessageW(hostWindow_, kWmFeEnumComplete,
                 makePaneWParam(paneIndex_, store_.generation()), 0);
  }
  return true;
}

}  // namespace fast_explorer::ui
