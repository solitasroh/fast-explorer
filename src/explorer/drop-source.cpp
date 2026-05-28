#include "explorer/drop-source.h"

namespace fast_explorer::ui {

STDMETHODIMP FileDropSource::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IDropSource) {
    *ppv = static_cast<IDropSource*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = nullptr;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) FileDropSource::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

STDMETHODIMP_(ULONG) FileDropSource::Release() {
  const LONG c = InterlockedDecrement(&refCount_);
  if (c == 0) delete this;
  return static_cast<ULONG>(c);
}

STDMETHODIMP FileDropSource::QueryContinueDrag(BOOL escapePressed,
                                               DWORD keyState) {
  if (escapePressed) return DRAGDROP_S_CANCEL;
  if ((keyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
  return S_OK;
}

STDMETHODIMP FileDropSource::GiveFeedback(DWORD /*effect*/) {
  return DRAGDROP_S_USEDEFAULTCURSORS;
}

}  // namespace fast_explorer::ui
