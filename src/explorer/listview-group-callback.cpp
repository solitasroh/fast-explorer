#include "explorer/listview-group-callback.h"

#include <initguid.h>  // must precede the DEFINE_GUID expansions below

#include "core/file-entry.h"
#include "core/file-grouping.h"
#include "core/file-model-store.h"

#include <unordered_map>

namespace fast_explorer::ui {

// IID_IOwnerDataCallback {44C09D56-8D3B-419D-A462-7B956B105B47}
// Suffix _FE to avoid colliding with any future SDK definition.
extern "C" const GUID IID_IOwnerDataCallback_FE = {
    0x44C09D56,
    0x8D3B,
    0x419D,
    {0xA4, 0x62, 0x7B, 0x95, 0x6B, 0x10, 0x5B, 0x47},
};

// IID_IListView for Windows 7+ (comctl32 6.10+):
// {E5B16AF2-3990-4681-A609-1F060CD14269}.
// The Vista variant {2FFE2979-5928-4386-9CDB-8E1F15B72FB4} stopped being
// recognised once comctl32 6.10 shipped — Win10/11 only accept this GUID.
extern "C" const GUID IID_IListView_FE = {
    0xE5B16AF2,
    0x3990,
    0x4681,
    {0xA6, 0x09, 0x1F, 0x06, 0x0C, 0xD1, 0x42, 0x69},
};

ListViewGroupCallback::ListViewGroupCallback() noexcept = default;

STDMETHODIMP ListViewGroupCallback::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IOwnerDataCallback_FE) {
    *ppv = static_cast<IOwnerDataCallback*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ListViewGroupCallback::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

STDMETHODIMP_(ULONG) ListViewGroupCallback::Release() {
  const LONG c = InterlockedDecrement(&refCount_);
  if (c == 0) {
    delete this;
  }
  return static_cast<ULONG>(c);
}

STDMETHODIMP ListViewGroupCallback::GetItemPosition(int /*itemIndex*/,
                                                    LPPOINT pPosition) {
  // Position is only meaningful for icon-view layouts. We render in
  // details view, so zero out and report success — the listview ignores
  // the result in detail mode but expects a non-failing HRESULT.
  if (pPosition != nullptr) {
    pPosition->x = 0;
    pPosition->y = 0;
  }
  return S_OK;
}

STDMETHODIMP ListViewGroupCallback::SetItemPosition(int /*itemIndex*/,
                                                    POINT /*position*/) {
  // No-op: the user can't drag-arrange virtual-mode items.
  return S_OK;
}

STDMETHODIMP ListViewGroupCallback::GetItemInGroup(int groupIndex,
                                                   int itemIndexInGroup,
                                                   PINT pItemIndex) {
  if (pItemIndex == nullptr) return E_POINTER;
  if (groupIndex < 0 ||
      static_cast<size_t>(groupIndex) >= groups_.size()) {
    return E_INVALIDARG;
  }
  const GroupRange& g = groups_[static_cast<size_t>(groupIndex)];
  if (itemIndexInGroup < 0 || itemIndexInGroup >= g.count) {
    return E_INVALIDARG;
  }
  *pItemIndex = g.firstRow + itemIndexInGroup;
  return S_OK;
}

STDMETHODIMP ListViewGroupCallback::GetItemGroup(int itemIndex,
                                                 int /*occurrenceIndex*/,
                                                 PINT pGroupIndex) {
  if (pGroupIndex == nullptr) return E_POINTER;
  if (itemIndex < 0 ||
      static_cast<size_t>(itemIndex) >= rowToGroupIdx_.size()) {
    return E_INVALIDARG;
  }
  *pGroupIndex = rowToGroupIdx_[static_cast<size_t>(itemIndex)];
  return S_OK;
}

STDMETHODIMP ListViewGroupCallback::GetItemGroupCount(int /*itemIndex*/,
                                                      PINT pOccurenceCount) {
  if (pOccurenceCount == nullptr) return E_POINTER;
  // Every row belongs to exactly one group under our grouping model.
  *pOccurenceCount = 1;
  return S_OK;
}

STDMETHODIMP ListViewGroupCallback::OnCacheHint(LVITEMINDEX /*firstItem*/,
                                                LVITEMINDEX /*lastItem*/) {
  // The cache-hint is advisory; we already populate group state up
  // front in rebuild(). Nothing extra to fetch.
  return S_OK;
}

void ListViewGroupCallback::rebuild(
    const fast_explorer::core::FileModelStore& store,
    fast_explorer::core::GroupKey key,
    uint64_t nowFiletime,
    const std::vector<int32_t>& enumeratedIds) {
  rowToGroupIdx_.clear();
  groups_.clear();
  if (key == fast_explorer::core::GroupKey::None || enumeratedIds.empty()) {
    return;
  }
  // Pre-build the iGroupId → groupIndex map so the per-row walk is O(1)
  // per row instead of O(group_count).
  std::unordered_map<int32_t, int32_t> idToIndex;
  idToIndex.reserve(enumeratedIds.size());
  groups_.resize(enumeratedIds.size());
  for (size_t i = 0; i < enumeratedIds.size(); ++i) {
    const int32_t id = enumeratedIds[i];
    idToIndex.emplace(id, static_cast<int32_t>(i));
    groups_[i].id       = id;
    groups_[i].firstRow = -1;
    groups_[i].count    = 0;
  }
  const std::size_t rows = store.displayedCount();
  rowToGroupIdx_.resize(rows, 0);
  for (std::size_t row = 0; row < rows; ++row) {
    const auto& entry = store.visibleAt(row);
    const int32_t id =
        fast_explorer::core::groupIdForEntry(key, entry, nowFiletime);
    const auto it = idToIndex.find(id);
    if (it == idToIndex.end()) {
      // Defensive: row's id wasn't enumerated (sort+enumerate ran on
      // different snapshots). Pin it into group 0 so the row at least
      // renders somewhere instead of returning E_INVALIDARG to comctl.
      rowToGroupIdx_[row] = 0;
      continue;
    }
    const int32_t gi = it->second;
    rowToGroupIdx_[row] = gi;
    GroupRange& g = groups_[static_cast<size_t>(gi)];
    if (g.firstRow < 0) g.firstRow = static_cast<int32_t>(row);
    ++g.count;
  }
  // Any group that never saw a row (shouldn't happen — enumerateGroups
  // skips empty groups — but guard anyway): fix firstRow to 0 so the
  // GetItemInGroup bounds check rejects the request cleanly.
  for (auto& g : groups_) {
    if (g.firstRow < 0) g.firstRow = 0;
  }
}

void ListViewGroupCallback::clear() noexcept {
  rowToGroupIdx_.clear();
  groups_.clear();
}

}  // namespace fast_explorer::ui
