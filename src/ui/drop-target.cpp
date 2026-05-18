#include "ui/drop-target.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "core/file-entry.h"
#include "core/file-model-store.h"
#include "ui/pane-controller.h"
#include "ui/pane-manager.h"

namespace fast_explorer::ui {

namespace {

ComPtr<IDropTarget> queryFolderDropTarget(const std::wstring& path,
                                          HWND ownerHwnd) {
  ComPtr<IDropTarget> target;
  if (path.empty()) return target;
  LPITEMIDLIST pidl = nullptr;
  SFGAOF attrs = 0;
  if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &attrs))) {
    return target;
  }
  PidlOwner pidlOwner(pidl);
  ComPtr<IShellFolder> folder;
  if (FAILED(SHBindToObject(nullptr, pidlOwner.get(), nullptr,
                            IID_PPV_ARGS(folder.put())))) {
    return target;
  }
  folder->CreateViewObject(ownerHwnd, IID_IDropTarget,
                           reinterpret_cast<void**>(target.put()));
  return target;
}

std::wstring joinPath(const std::wstring& base, std::wstring_view leaf) {
  std::wstring out = base;
  if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
  out.append(leaf);
  return out;
}

}  // namespace

PaneDropTarget::PaneDropTarget(HWND lv, PaneManager* paneManager,
                                std::size_t paneIdx) noexcept
    : lv_(lv), paneManager_(paneManager), paneIdx_(paneIdx) {}

STDMETHODIMP PaneDropTarget::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IDropTarget) {
    *ppv = static_cast<IDropTarget*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) PaneDropTarget::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

STDMETHODIMP_(ULONG) PaneDropTarget::Release() {
  const LONG c = InterlockedDecrement(&refCount_);
  if (c == 0) delete this;
  return static_cast<ULONG>(c);
}

void PaneDropTarget::clearCurrentTarget() noexcept {
  if (currentTarget_) {
    currentTarget_->DragLeave();
  }
  currentTarget_.reset();
  currentTargetPath_.clear();
}

bool PaneDropTarget::rebindTarget(POINT screenPt) {
  if (!paneManager_ || paneIdx_ >= paneManager_->count()) {
    return false;
  }
  PaneController& pane = paneManager_->at(paneIdx_);
  std::wstring targetPath = pane.currentPath();
  POINT clientPt = screenPt;
  ScreenToClient(lv_, &clientPt);
  LVHITTESTINFO ht{};
  ht.pt = clientPt;
  const int hit = ListView_HitTest(lv_, &ht);
  if (hit >= 0) {
    const auto& store = pane.store();
    const auto row = static_cast<std::size_t>(hit);
    if (row < store.publishedCount()) {
      const auto& entry = store.visibleAt(row);
      if (fast_explorer::core::isDirectory(entry)) {
        targetPath = joinPath(pane.currentPath(),
                              fast_explorer::core::nameView(entry));
      }
    }
  }
  if (targetPath == currentTargetPath_ && currentTarget_) {
    return false;
  }
  clearCurrentTarget();
  currentTarget_ = queryFolderDropTarget(targetPath, lv_);
  if (currentTarget_) {
    currentTargetPath_ = std::move(targetPath);
    return true;
  }
  return false;
}

STDMETHODIMP PaneDropTarget::DragEnter(IDataObject* data, DWORD keyState,
                                       POINTL pt, DWORD* effect) {
  if (effect == nullptr) return E_POINTER;
  currentData_.reset();
  if (data) {
    data->AddRef();
    *currentData_.put() = data;
  }
  POINT screenPt{pt.x, pt.y};
  rebindTarget(screenPt);
  if (currentTarget_) {
    return currentTarget_->DragEnter(data, keyState, pt, effect);
  }
  *effect = DROPEFFECT_NONE;
  return S_OK;
}

STDMETHODIMP PaneDropTarget::DragOver(DWORD keyState, POINTL pt,
                                      DWORD* effect) {
  if (effect == nullptr) return E_POINTER;
  POINT screenPt{pt.x, pt.y};
  const bool rebound = rebindTarget(screenPt);
  if (rebound && currentTarget_ && currentData_) {
    return currentTarget_->DragEnter(currentData_.get(), keyState, pt, effect);
  }
  if (currentTarget_) {
    return currentTarget_->DragOver(keyState, pt, effect);
  }
  *effect = DROPEFFECT_NONE;
  return S_OK;
}

STDMETHODIMP PaneDropTarget::DragLeave() {
  clearCurrentTarget();
  currentData_.reset();
  return S_OK;
}

STDMETHODIMP PaneDropTarget::Drop(IDataObject* data, DWORD keyState,
                                  POINTL pt, DWORD* effect) {
  if (effect == nullptr) return E_POINTER;
  HRESULT hr = S_OK;
  if (currentTarget_) {
    hr = currentTarget_->Drop(data, keyState, pt, effect);
  } else {
    *effect = DROPEFFECT_NONE;
  }
  clearCurrentTarget();
  currentData_.reset();
  return hr;
}

}  // namespace fast_explorer::ui
