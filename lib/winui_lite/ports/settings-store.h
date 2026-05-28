// settings-store.h — "persist / restore session state" port.
//
// Chrome never inspects the payload — the adapter knows what to read
// and write (window position, pane layout, …) because it was handed
// a reference to the in-memory state at construction time. The port
// surface is intentionally argument-free so different adapters can
// back the same storage door: JSON-on-disk, registry, env vars, an
// in-memory map for the demo example, …

#pragma once

namespace fast_explorer::ui::ports {

class SettingsStore {
 public:
  virtual ~SettingsStore() = default;

  // Reload state from durable storage. Returns false when no prior
  // state exists, the read fails, or the on-disk payload is too
  // corrupt to use. Callers stay with whatever defaults they
  // initialised the state with on a false return.
  virtual bool load() = 0;

  // Persist the current state. Returns false on I/O error.
  virtual bool save() = 0;
};

}  // namespace fast_explorer::ui::ports
