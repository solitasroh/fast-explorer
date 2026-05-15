#include "ui/shell-worker.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include <utility>

namespace fast_explorer::ui {

namespace {

// Releases a COM interface pointer and clears it.
template <class T>
void safeRelease(T*& p) noexcept {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

// Runs the recycle-bin delete sequence on the worker's STA thread.
// Returns true if PerformOperations succeeded; otherwise the file
// system is unchanged. The watcher path (kWmFeFsChange + coalesce
// refresh) will pick up the result automatically — no extra
// channel is needed back to the UI for the success case.
bool performShellDelete(const std::wstring& sourcePath) noexcept {
  IFileOperation* op = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&op));
  if (FAILED(hr) || op == nullptr) {
    return false;
  }
  // FOF_ALLOWUNDO permits the recycle-bin path; FOFX_RECYCLEONDELETE
  // forces it even when the shell would otherwise fall back to a
  // permanent delete (e.g. items above the per-drive recycle quota).
  // The two are intentionally combined — losing either one risks a
  // silent permanent delete in edge cases. FOF_NOCONFIRMATION /
  // FOF_NOERRORUI / FOF_SILENT suppress the shell's own dialogs;
  // failures surface only through the HRESULT chain.
  hr = op->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE |
                             FOF_NOCONFIRMATION | FOF_NOERRORUI |
                             FOF_SILENT);
  if (FAILED(hr)) {
    safeRelease(op);
    return false;
  }
  IShellItem* item = nullptr;
  hr = SHCreateItemFromParsingName(sourcePath.c_str(), nullptr,
                                   IID_PPV_ARGS(&item));
  if (FAILED(hr) || item == nullptr) {
    safeRelease(op);
    return false;
  }
  hr = op->DeleteItem(item, nullptr);
  safeRelease(item);
  if (FAILED(hr)) {
    safeRelease(op);
    return false;
  }
  hr = op->PerformOperations();
  safeRelease(op);
  return SUCCEEDED(hr);
}

}  // namespace

ShellWorker::ShellWorker(HWND host)
    : host_(host),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

ShellWorker::~ShellWorker() {
  // Stop and join the worker explicitly so the follow-up sub-step
  // has a clean seam to add result-queue / COM-handle cleanup
  // between the join and any owned shell resources. Matches the
  // IconProvider pattern.
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
}

void ShellWorker::request(ShellCommand command) {
  {
    std::lock_guard lk(mutex_);
    pendingCommands_.push(std::move(command));
  }
  cv_.notify_one();
}

void ShellWorker::waitForProcessedForTest(
    std::size_t expected) const noexcept {
  std::size_t current = processed_.load(std::memory_order_acquire);
  while (current < expected) {
    processed_.wait(current, std::memory_order_acquire);
    current = processed_.load(std::memory_order_acquire);
  }
}

std::optional<ShellCommand> ShellWorker::dequeueOne(std::stop_token tok) {
  std::unique_lock lk(mutex_);
  cv_.wait(lk, tok, [this] { return !pendingCommands_.empty(); });
  if (tok.stop_requested()) {
    // Policy: pending commands are dropped on stop. The worker is
    // only torn down when the host window is closing, so a queued
    // file-system mutation that has not yet started is the user's
    // decision to abandon.
    return std::nullopt;
  }
  ShellCommand command = std::move(pendingCommands_.front());
  pendingCommands_.pop();
  return command;
}

void ShellWorker::processOne(const ShellCommand& command) {
  switch (command.kind) {
    case ShellCommandKind::Delete:
      (void)performShellDelete(command.sourcePath);
      break;
    case ShellCommandKind::Rename:
    case ShellCommandKind::CreateFolder:
      // Wired in the follow-up sub-step.
      break;
  }
  processed_.fetch_add(1, std::memory_order_release);
  processed_.notify_all();
}

void ShellWorker::workerMain(std::stop_token tok) {
  // IFileOperation requires the STA apartment; the next sub-step's
  // CoCreateInstance call will rely on this initialisation.
  const HRESULT coInitResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  while (!tok.stop_requested()) {
    auto cmd = dequeueOne(tok);
    if (!cmd) {
      break;
    }
    processOne(*cmd);
  }

  if (SUCCEEDED(coInitResult)) {
    CoUninitialize();
  }
}

}  // namespace fast_explorer::ui
