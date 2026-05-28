#pragma once

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <unknwn.h>

#include <cstdint>
#include <vector>

#include "core/file-grouping.h"

namespace fast_explorer::core {
class FileModelStore;
}  // namespace fast_explorer::core

namespace fast_explorer::ui {

// LVM_QUERYINTERFACE = LVM_FIRST + 189 (0x1000 + 189).
// Sends a QueryInterface to the comctl32 list-view; wParam = IID*, lParam = void**.
// Documented only via reverse-engineering — see Geoff Chappell + CodeProject
// "Undocumented List View Features" (Stephan Keil).
constexpr UINT kLvmQueryInterface = LVM_FIRST + 189;

// IID_IOwnerDataCallback {44C09D56-8D3B-419D-A462-7B956B105B47}.
// Used by Windows Explorer to make LVS_OWNERDATA list-views support groups.
extern "C" const GUID IID_IOwnerDataCallback_FE;

// IID_IListView for Win7+ comctl32 6.10:
// {E5B16AF2-3990-4681-A609-1F060CD14269}.
// Obtained by sending kLvmQueryInterface to the list-view HWND.
// (The Vista-era {2FFE2979-...} GUID is no longer accepted on Win10/11.)
extern "C" const GUID IID_IListView_FE;

// Undocumented owner-data callback. Implemented by us; consumed by
// comctl32 once we hand it to IListView::SetOwnerDataCallback.
// Vtable layout reverse-engineered from Geoff Chappell's notes.
struct IOwnerDataCallback : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetItemPosition(int itemIndex,
                                                    LPPOINT pPosition) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetItemPosition(int itemIndex,
                                                    POINT position) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetItemInGroup(int groupIndex,
                                                   int itemIndexInGroup,
                                                   PINT pItemIndex) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetItemGroup(int itemIndex,
                                                 int occurrenceIndex,
                                                 PINT pGroupIndex) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetItemGroupCount(int itemIndex,
                                                      PINT pOccurenceCount) = 0;
  virtual HRESULT STDMETHODCALLTYPE OnCacheHint(LVITEMINDEX firstItem,
                                                LVITEMINDEX lastItem) = 0;
};

// Minimal subset of IListView (vtable layout per Geoff Chappell).
// We only need SetOwnerDataCallback; the rest are stubs that preserve
// vtable alignment so the call lands at the correct slot. The interface
// derives from IOleWindow (which derives from IUnknown), so the layout
// is: QueryInterface/AddRef/Release/GetWindow/ContextSensitiveHelp +
// 112 IListView methods, with SetOwnerDataCallback as the 113th.
struct IListView_FE {
  // IUnknown
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) = 0;
  virtual ULONG   STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG   STDMETHODCALLTYPE Release() = 0;
  // IOleWindow
  virtual HRESULT STDMETHODCALLTYPE GetWindow(HWND*) = 0;
  virtual HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) = 0;
  // IListView — 112 methods, all opaque except SetOwnerDataCallback.
  // We expand them as virtual void slots so the vtable spacing matches
  // exactly. The actual signatures don't matter because we never call
  // anything except SetOwnerDataCallback through this declaration.
  virtual HRESULT STDMETHODCALLTYPE Slot_05() = 0;  // GetImageList
  virtual HRESULT STDMETHODCALLTYPE Slot_06() = 0;  // SetImageList
  virtual HRESULT STDMETHODCALLTYPE Slot_07() = 0;  // GetBackgroundColor
  virtual HRESULT STDMETHODCALLTYPE Slot_08() = 0;  // SetBackgroundColor
  virtual HRESULT STDMETHODCALLTYPE Slot_09() = 0;  // GetTextColor
  virtual HRESULT STDMETHODCALLTYPE Slot_10() = 0;  // SetTextColor
  virtual HRESULT STDMETHODCALLTYPE Slot_11() = 0;  // GetTextBackgroundColor
  virtual HRESULT STDMETHODCALLTYPE Slot_12() = 0;  // SetTextBackgroundColor
  virtual HRESULT STDMETHODCALLTYPE Slot_13() = 0;  // GetHotLightColor
  virtual HRESULT STDMETHODCALLTYPE Slot_14() = 0;  // SetHotLightColor
  virtual HRESULT STDMETHODCALLTYPE Slot_15() = 0;  // GetItemCount
  virtual HRESULT STDMETHODCALLTYPE Slot_16() = 0;  // SetItemCount
  virtual HRESULT STDMETHODCALLTYPE Slot_17() = 0;  // GetItem
  virtual HRESULT STDMETHODCALLTYPE Slot_18() = 0;  // SetItem
  virtual HRESULT STDMETHODCALLTYPE Slot_19() = 0;  // GetItemState
  virtual HRESULT STDMETHODCALLTYPE Slot_20() = 0;  // SetItemState
  virtual HRESULT STDMETHODCALLTYPE Slot_21() = 0;  // GetItemText
  virtual HRESULT STDMETHODCALLTYPE Slot_22() = 0;  // SetItemText
  virtual HRESULT STDMETHODCALLTYPE Slot_23() = 0;  // GetBackgroundImage
  virtual HRESULT STDMETHODCALLTYPE Slot_24() = 0;  // SetBackgroundImage
  virtual HRESULT STDMETHODCALLTYPE Slot_25() = 0;  // GetFocusedColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_26() = 0;  // SetSelectionFlags
  virtual HRESULT STDMETHODCALLTYPE Slot_27() = 0;  // GetSelectedColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_28() = 0;  // SetSelectedColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_29() = 0;  // GetView
  virtual HRESULT STDMETHODCALLTYPE Slot_30() = 0;  // SetView
  virtual HRESULT STDMETHODCALLTYPE Slot_31() = 0;  // InsertItem
  virtual HRESULT STDMETHODCALLTYPE Slot_32() = 0;  // DeleteItem
  virtual HRESULT STDMETHODCALLTYPE Slot_33() = 0;  // DeleteAllItems
  virtual HRESULT STDMETHODCALLTYPE Slot_34() = 0;  // UpdateItem
  virtual HRESULT STDMETHODCALLTYPE Slot_35() = 0;  // GetItemRect
  virtual HRESULT STDMETHODCALLTYPE Slot_36() = 0;  // GetSubItemRect
  virtual HRESULT STDMETHODCALLTYPE Slot_37() = 0;  // HitTestSubItem
  virtual HRESULT STDMETHODCALLTYPE Slot_38() = 0;  // GetIncrSearchString
  virtual HRESULT STDMETHODCALLTYPE Slot_39() = 0;  // GetItemSpacing
  virtual HRESULT STDMETHODCALLTYPE Slot_40() = 0;  // SetIconSpacing
  virtual HRESULT STDMETHODCALLTYPE Slot_41() = 0;  // GetNextItem
  virtual HRESULT STDMETHODCALLTYPE Slot_42() = 0;  // FindItem
  virtual HRESULT STDMETHODCALLTYPE Slot_43() = 0;  // GetSelectionMark
  virtual HRESULT STDMETHODCALLTYPE Slot_44() = 0;  // SetSelectionMark
  virtual HRESULT STDMETHODCALLTYPE Slot_45() = 0;  // GetItemPosition
  virtual HRESULT STDMETHODCALLTYPE Slot_46() = 0;  // SetItemPosition
  virtual HRESULT STDMETHODCALLTYPE Slot_47() = 0;  // ScrollView
  virtual HRESULT STDMETHODCALLTYPE Slot_48() = 0;  // EnsureItemVisible
  virtual HRESULT STDMETHODCALLTYPE Slot_49() = 0;  // EnsureSubItemVisible
  virtual HRESULT STDMETHODCALLTYPE Slot_50() = 0;  // EditSubItem
  virtual HRESULT STDMETHODCALLTYPE Slot_51() = 0;  // RedrawItems
  virtual HRESULT STDMETHODCALLTYPE Slot_52() = 0;  // ArrangeItems
  virtual HRESULT STDMETHODCALLTYPE Slot_53() = 0;  // RecomputeItems
  virtual HRESULT STDMETHODCALLTYPE Slot_54() = 0;  // GetEditControl
  virtual HRESULT STDMETHODCALLTYPE Slot_55() = 0;  // EditLabel
  virtual HRESULT STDMETHODCALLTYPE Slot_56() = 0;  // EditGroupLabel
  virtual HRESULT STDMETHODCALLTYPE Slot_57() = 0;  // CancelEditLabel
  virtual HRESULT STDMETHODCALLTYPE Slot_58() = 0;  // GetEditItem
  virtual HRESULT STDMETHODCALLTYPE Slot_59() = 0;  // HitTest
  virtual HRESULT STDMETHODCALLTYPE Slot_60() = 0;  // GetStringWidth
  virtual HRESULT STDMETHODCALLTYPE Slot_61() = 0;  // GetColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_62() = 0;  // SetColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_63() = 0;  // GetColumnOrderArray
  virtual HRESULT STDMETHODCALLTYPE Slot_64() = 0;  // SetColumnOrderArray
  virtual HRESULT STDMETHODCALLTYPE Slot_65() = 0;  // GetHeaderControl
  virtual HRESULT STDMETHODCALLTYPE Slot_66() = 0;  // InsertColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_67() = 0;  // DeleteColumn
  virtual HRESULT STDMETHODCALLTYPE Slot_68() = 0;  // CreateDragImage
  virtual HRESULT STDMETHODCALLTYPE Slot_69() = 0;  // GetViewRect
  virtual HRESULT STDMETHODCALLTYPE Slot_70() = 0;  // GetClientRect
  virtual HRESULT STDMETHODCALLTYPE Slot_71() = 0;  // GetColumnWidth
  virtual HRESULT STDMETHODCALLTYPE Slot_72() = 0;  // SetColumnWidth
  virtual HRESULT STDMETHODCALLTYPE Slot_73() = 0;  // GetCallbackMask
  virtual HRESULT STDMETHODCALLTYPE Slot_74() = 0;  // SetCallbackMask
  virtual HRESULT STDMETHODCALLTYPE Slot_75() = 0;  // GetTopIndex
  virtual HRESULT STDMETHODCALLTYPE Slot_76() = 0;  // GetCountPerPage
  virtual HRESULT STDMETHODCALLTYPE Slot_77() = 0;  // GetOrigin
  virtual HRESULT STDMETHODCALLTYPE Slot_78() = 0;  // GetSelectedCount
  virtual HRESULT STDMETHODCALLTYPE Slot_79() = 0;  // SortItems
  virtual HRESULT STDMETHODCALLTYPE Slot_80() = 0;  // GetExtendedStyle
  virtual HRESULT STDMETHODCALLTYPE Slot_81() = 0;  // SetExtendedStyle
  virtual HRESULT STDMETHODCALLTYPE Slot_82() = 0;  // GetHoverTime
  virtual HRESULT STDMETHODCALLTYPE Slot_83() = 0;  // SetHoverTime
  virtual HRESULT STDMETHODCALLTYPE Slot_84() = 0;  // GetToolTip
  virtual HRESULT STDMETHODCALLTYPE Slot_85() = 0;  // SetToolTip
  virtual HRESULT STDMETHODCALLTYPE Slot_86() = 0;  // GetHotItem
  virtual HRESULT STDMETHODCALLTYPE Slot_87() = 0;  // SetHotItem
  virtual HRESULT STDMETHODCALLTYPE Slot_88() = 0;  // GetHotCursor
  virtual HRESULT STDMETHODCALLTYPE Slot_89() = 0;  // SetHotCursor
  virtual HRESULT STDMETHODCALLTYPE Slot_90() = 0;  // ApproximateViewRect
  virtual HRESULT STDMETHODCALLTYPE Slot_91() = 0;  // SetRangeObject
  virtual HRESULT STDMETHODCALLTYPE Slot_92() = 0;  // GetWorkAreas
  virtual HRESULT STDMETHODCALLTYPE Slot_93() = 0;  // SetWorkAreas
  virtual HRESULT STDMETHODCALLTYPE Slot_94() = 0;  // GetWorkAreaCount
  virtual HRESULT STDMETHODCALLTYPE Slot_95() = 0;  // ResetEmptyText
  virtual HRESULT STDMETHODCALLTYPE Slot_96() = 0;  // EnableGroupView
  virtual HRESULT STDMETHODCALLTYPE Slot_97() = 0;  // IsGroupViewEnabled
  virtual HRESULT STDMETHODCALLTYPE Slot_98() = 0;  // SortGroups
  virtual HRESULT STDMETHODCALLTYPE Slot_99() = 0;  // GetGroupInfo
  virtual HRESULT STDMETHODCALLTYPE Slot_100() = 0;  // SetGroupInfo
  virtual HRESULT STDMETHODCALLTYPE Slot_101() = 0;  // GetGroupRect
  virtual HRESULT STDMETHODCALLTYPE Slot_102() = 0;  // GetGroupState
  virtual HRESULT STDMETHODCALLTYPE Slot_103() = 0;  // HasGroup
  virtual HRESULT STDMETHODCALLTYPE Slot_104() = 0;  // InsertGroup
  virtual HRESULT STDMETHODCALLTYPE Slot_105() = 0;  // RemoveGroup
  virtual HRESULT STDMETHODCALLTYPE Slot_106() = 0;  // InsertGroupSorted
  virtual HRESULT STDMETHODCALLTYPE Slot_107() = 0;  // GetGroupMetrics
  virtual HRESULT STDMETHODCALLTYPE Slot_108() = 0;  // SetGroupMetrics
  virtual HRESULT STDMETHODCALLTYPE Slot_109() = 0;  // RemoveAllGroups
  virtual HRESULT STDMETHODCALLTYPE Slot_110() = 0;  // GetFocusedGroup
  virtual HRESULT STDMETHODCALLTYPE Slot_111() = 0;  // GetGroupCount
  // The one slot we actually call. Vtable index 112 (0-based) /
  // offset 0x1C0 on 32-bit or 0x380 on 64-bit.
  virtual HRESULT STDMETHODCALLTYPE SetOwnerDataCallback(IUnknown* pCallback) = 0;
};

// LVS_OWNERDATA + groups bridge. Owns a per-pane row→groupIndex mapping
// that comctl32 queries through IOwnerDataCallback. Lifetime is tied to
// the owning MainWindow pane slot; the list-view holds a ref via
// SetOwnerDataCallback. Must be created on the UI thread and only
// accessed from there (no internal locking).
class ListViewGroupCallback final : public IOwnerDataCallback {
 public:
  ListViewGroupCallback() noexcept;
  ~ListViewGroupCallback() = default;

  ListViewGroupCallback(const ListViewGroupCallback&) = delete;
  ListViewGroupCallback& operator=(const ListViewGroupCallback&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // IOwnerDataCallback — see Geoff Chappell for semantics.
  STDMETHODIMP GetItemPosition(int itemIndex, LPPOINT pPosition) override;
  STDMETHODIMP SetItemPosition(int itemIndex, POINT position) override;
  STDMETHODIMP GetItemInGroup(int groupIndex, int itemIndexInGroup,
                              PINT pItemIndex) override;
  STDMETHODIMP GetItemGroup(int itemIndex, int occurrenceIndex,
                            PINT pGroupIndex) override;
  STDMETHODIMP GetItemGroupCount(int itemIndex, PINT pOccurenceCount) override;
  STDMETHODIMP OnCacheHint(LVITEMINDEX firstItem,
                           LVITEMINDEX lastItem) override;

  // Walks `store`'s visible-order rows (already group-clustered by the
  // sort that ran before us) and populates internal row→groupIndex /
  // groupIndex→firstRow tables. `enumeratedIds` is the iGroupId list in
  // the order LVM_INSERTGROUP was called; index into it IS the
  // groupIndex comctl32 sees through this callback.
  void rebuild(const fast_explorer::core::FileModelStore& store,
               fast_explorer::core::GroupKey key,
               uint64_t nowFiletime,
               const std::vector<int32_t>& enumeratedIds);

  // Drops all per-row mapping state. Called by the navigation reset path
  // so a stale row→group answer can't survive a folder change while the
  // listview is still drained.
  void clear() noexcept;

  // Returns the number of visible rows that belong to the k-th group
  // (k = groupIndex, same indexing the callback uses internally).
  // Used by applyListViewGroups to fill LVGROUP::cItems when inserting
  // groups under LVS_OWNERDATA — without cItems set, comctl32 lays out
  // empty group containers and the row count appears as 0.
  [[nodiscard]] int countInGroup(size_t groupIndex) const noexcept {
    return groupIndex < groups_.size() ? groups_[groupIndex].count : 0;
  }

 private:
  struct GroupRange {
    int32_t id;        // iGroupId (the value handed to LVM_INSERTGROUP)
    int32_t firstRow;  // first visible row that belongs to this group
    int32_t count;     // number of visible rows in this group
  };

  LONG refCount_ = 1;
  std::vector<int32_t> rowToGroupIdx_;  // size == visible row count
  std::vector<GroupRange> groups_;       // size == enumeratedIds.size()
};

}  // namespace fast_explorer::ui
