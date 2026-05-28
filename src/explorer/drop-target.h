#pragma once

#include <windows.h>
#include <ole2.h>

#include <cstddef>
#include <string>

#include "winui_lite/chrome/com-raii.h"

namespace fast_explorer::ui {

class PaneController;
template <class TPane> class PaneManager;

// IDropTarget for a single pane's list-view. Delegates to the shell-
// provided IDropTarget of either the hovered folder row or the pane
// background; the shell handles drop-effect computation (same-drive
// = move, cross-drive = copy) and conflict UI itself.
class PaneDropTarget final : public IDropTarget {
 public:
  PaneDropTarget(HWND lv, PaneManager<PaneController>* paneManager,
                 std::size_t paneIdx) noexcept;

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP DragEnter(IDataObject* data, DWORD keyState, POINTL pt,
                          DWORD* effect) override;
  STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD* effect) override;
  STDMETHODIMP DragLeave() override;
  STDMETHODIMP Drop(IDataObject* data, DWORD keyState, POINTL pt,
                     DWORD* effect) override;

 private:
  // Resolves the shell IDropTarget for the row under `screenPt` if it
  // is a folder; falls back to the pane's currentPath background.
  // Updates currentTarget_ in place and returns whether it changed.
  bool rebindTarget(POINT screenPt);

  void clearCurrentTarget() noexcept;

  HWND lv_;
  PaneManager<PaneController>* paneManager_;
  std::size_t paneIdx_;
  ComPtr<IDataObject> currentData_;
  ComPtr<IDropTarget> currentTarget_;
  std::wstring currentTargetPath_;
  // Row under cursor that produced currentTarget_. -1 = empty area,
  // -2 = uninitialised. Avoids the SHParseDisplayName + SHBindToObject
  // chain on every DragOver when the cursor stays on the same row.
  int lastHitRow_ = -2;
  LONG refCount_ = 1;
};

}  // namespace fast_explorer::ui
