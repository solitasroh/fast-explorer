#pragma once

#include <windows.h>
#include <ole2.h>

namespace fast_explorer::ui {

class FileDropSource final : public IDropSource {
 public:
  FileDropSource() noexcept = default;

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override;
  STDMETHODIMP GiveFeedback(DWORD effect) override;

 private:
  LONG refCount_ = 1;
};

}  // namespace fast_explorer::ui
