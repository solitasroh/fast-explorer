#include "ui/shell-worker.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include <utility>

#include "ui/messages.h"

namespace fast_explorer::ui {

namespace {

// Minimal RAII wrapper for a COM interface pointer. Releases on
// destruction so each helper can early-return from any HRESULT
// failure without remembering to chain release calls. put() yields
// a T** suitable for COM creation APIs (CoCreateInstance,
// SHCreateItemFromParsingName).
template <class T>
class ComScope {
 public:
  ComScope() = default;
  ~ComScope() { reset(); }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;
  ComScope(ComScope&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
  ComScope& operator=(ComScope&& other) noexcept {
    if (this != &other) {
      reset();
      p_ = other.p_;
      other.p_ = nullptr;
    }
    return *this;
  }

  T* get() const noexcept { return p_; }
  T* operator->() const noexcept { return p_; }
  T** put() noexcept {
    reset();
    return &p_;
  }
  explicit operator bool() const noexcept { return p_ != nullptr; }
  void reset() noexcept {
    if (p_ != nullptr) {
      p_->Release();
      p_ = nullptr;
    }
  }

 private:
  T* p_ = nullptr;
};

// FOF_ALLOWUNDO permits recycle-bin routing; FOFX_RECYCLEONDELETE
// forces it even when the drive's recycle quota is exceeded, so a
// delete cannot silently slip into a permanent removal. The
// remaining flags suppress every shell dialog so failure surfaces
// only through HRESULT.
constexpr DWORD kSilentRecycleFlags = FOF_ALLOWUNDO |
                                      FOFX_RECYCLEONDELETE |
                                      FOF_NOCONFIRMATION |
                                      FOF_NOERRORUI |
                                      FOF_SILENT;

// Creates an IFileOperation already configured for silent-recycle
// behaviour. Returns empty scope on failure.
ComScope<IFileOperation> makeFileOp() noexcept {
  ComScope<IFileOperation> op;
  HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(op.put()));
  if (FAILED(hr) || !op) {
    return {};
  }
  hr = op->SetOperationFlags(kSilentRecycleFlags);
  if (FAILED(hr)) {
    return {};
  }
  return op;
}

// Resolves an IShellItem from an absolute Win32 path. Returns empty
// scope when the shell cannot parse the path (missing parent, etc).
ComScope<IShellItem> shellItem(const std::wstring& path) noexcept {
  ComScope<IShellItem> item;
  HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr,
                                           IID_PPV_ARGS(item.put()));
  if (FAILED(hr)) {
    return {};
  }
  return item;
}

// The watcher path (kWmFeFsChange + coalesce refresh) refreshes
// the pane after each operation; the helpers below report
// success through the bool return, which the worker then
// converts into an OperationResult for the host window.

bool performShellDelete(const std::wstring& sourcePath) noexcept {
  auto op = makeFileOp();
  if (!op) {
    return false;
  }
  auto item = shellItem(sourcePath);
  if (!item) {
    return false;
  }
  HRESULT hr = op->DeleteItem(item.get(), nullptr);
  if (FAILED(hr)) {
    return false;
  }
  return SUCCEEDED(op->PerformOperations());
}

bool performShellRename(const std::wstring& sourcePath,
                        const std::wstring& newName) noexcept {
  if (newName.empty()) {
    return false;
  }
  auto op = makeFileOp();
  if (!op) {
    return false;
  }
  auto item = shellItem(sourcePath);
  if (!item) {
    return false;
  }
  HRESULT hr = op->RenameItem(item.get(), newName.c_str(), nullptr);
  if (FAILED(hr)) {
    return false;
  }
  return SUCCEEDED(op->PerformOperations());
}

bool performShellCreateFolder(const std::wstring& parentPath,
                              const std::wstring& folderName) noexcept {
  if (folderName.empty()) {
    return false;
  }
  auto op = makeFileOp();
  if (!op) {
    return false;
  }
  auto parent = shellItem(parentPath);
  if (!parent) {
    return false;
  }
  HRESULT hr = op->NewItem(parent.get(), FILE_ATTRIBUTE_DIRECTORY,
                           folderName.c_str(), nullptr, nullptr);
  if (FAILED(hr)) {
    return false;
  }
  return SUCCEEDED(op->PerformOperations());
}

}  // namespace

ShellWorker::ShellWorker(HWND host)
    : host_(host),
      worker_([this](std::stop_token tok) { workerMain(tok); }) {}

ShellWorker::~ShellWorker() {
  // Stop and join before any member with worker-visible state goes
  // away. resultsReady_ is destroyed afterwards under the implicit
  // dtor; no extra cleanup needed (no COM-handle ownership in the
  // result payload).
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
  bool success = false;
  switch (command.kind) {
    case ShellCommandKind::Delete:
      success = performShellDelete(command.sourcePath);
      break;
    case ShellCommandKind::Rename:
      success = performShellRename(command.sourcePath, command.newName);
      break;
    case ShellCommandKind::CreateFolder:
      success = performShellCreateFolder(command.sourcePath, command.newName);
      break;
  }
  OperationResult result;
  result.kind = command.kind;
  result.sourcePath = command.sourcePath;
  result.newName = command.newName;
  result.success = success;
  publishResult(std::move(result));
  processed_.fetch_add(1, std::memory_order_release);
  processed_.notify_all();
}

void ShellWorker::publishResult(OperationResult result) {
  {
    std::lock_guard lk(resultMutex_);
    resultsReady_.push_back(std::move(result));
  }
  // Coalesce: drainResults() clears postPending_; one PostMessage
  // per accumulated batch wakes the UI exactly once.
  bool expected = false;
  if (postPending_.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
    if (host_ != nullptr) {
      PostMessageW(host_, kWmFeOperationResult, 0, 0);
    } else {
      // Nothing to deliver to — let the next publish post again.
      postPending_.store(false, std::memory_order_release);
    }
  }
}

std::vector<OperationResult> ShellWorker::drainResults() {
  std::vector<OperationResult> out;
  {
    std::lock_guard lk(resultMutex_);
    out.swap(resultsReady_);
    // Clearing postPending_ inside the lock seals the gap that
    // otherwise lets a worker publish between the swap and the
    // store: under the lock, any push that happens-before the
    // clear is in `out` already, and any push after the clear
    // sees postPending_ == false and posts a fresh message.
    postPending_.store(false, std::memory_order_release);
  }
  return out;
}

void ShellWorker::workerMain(std::stop_token tok) {
  // IFileOperation requires the STA apartment. If CoInitializeEx
  // fails (e.g. RPC_E_CHANGED_MODE from a host that pre-initialised
  // the thread as MTA), the loop keeps draining the queue and every
  // command becomes a silent no-op rather than blocking the queue
  // forever — failure is surfaced through performShell*'s bool
  // return and the watcher refresh path.
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
