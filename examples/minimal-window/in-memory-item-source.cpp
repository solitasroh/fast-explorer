#include "in-memory-item-source.h"

namespace winui_lite_demo {

namespace ports = fast_explorer::ui::ports;

namespace {

constexpr std::size_t kFakeItemCount = 10;

// Static metadata table for the ten fake items. Index = id - 1.
struct FakeItem {
  const wchar_t* name;
  const wchar_t* sizeText;
  const wchar_t* modifiedText;
  const wchar_t* typeText;
};

constexpr FakeItem kItems[kFakeItemCount] = {
    {L"alpha.txt",       L"42 B",     L"2026-05-28 09:00", L"Text"},
    {L"beta.cpp",        L"3.1 KB",   L"2026-05-27 14:32", L"Source"},
    {L"gamma.png",       L"82.4 KB",  L"2026-05-25 11:18", L"Image"},
    {L"delta.md",        L"1.2 KB",   L"2026-05-22 22:05", L"Markdown"},
    {L"epsilon.log",     L"512 KB",   L"2026-05-20 08:11", L"Log"},
    {L"zeta.json",       L"6.3 KB",   L"2026-05-18 19:44", L"JSON"},
    {L"eta.yaml",        L"812 B",    L"2026-05-15 12:00", L"YAML"},
    {L"theta.bin",       L"1.5 MB",   L"2026-05-10 03:27", L"Binary"},
    {L"iota.docx",       L"24.1 KB",  L"2026-05-05 16:00", L"Document"},
    {L"kappa.xlsx",      L"38.7 KB",  L"2026-05-01 09:30", L"Spreadsheet"},
};

}  // namespace

InMemoryItemSource::InMemoryItemSource()
    : location_(L"demo:/fake-folder") {}

bool InMemoryItemSource::navigateTo(const std::wstring& location) {
  // The demo source has only one synthetic location; accept any
  // non-empty input and record it for currentLocation() readouts.
  if (location.empty()) return false;
  location_ = location;
  return true;
}

const std::wstring& InMemoryItemSource::currentLocation() const {
  return location_;
}

std::size_t InMemoryItemSource::count() const {
  return kFakeItemCount;
}

ports::ItemId InMemoryItemSource::idAt(std::size_t index) const {
  if (index >= kFakeItemCount) return ports::kInvalidItemId;
  return static_cast<ports::ItemId>(index + 1);
}

std::wstring InMemoryItemSource::textFor(ports::ItemId id,
                                          ports::ItemField field) const {
  if (id == ports::kInvalidItemId) return {};
  const std::size_t idx = static_cast<std::size_t>(id - 1);
  if (idx >= kFakeItemCount) return {};
  const FakeItem& it = kItems[idx];
  switch (field) {
    case ports::ItemField::Name:         return it.name;
    case ports::ItemField::SizeText:     return it.sizeText;
    case ports::ItemField::ModifiedText: return it.modifiedText;
    case ports::ItemField::TypeText:     return it.typeText;
  }
  return {};
}

int InMemoryItemSource::iconIndexFor(ports::ItemId) const {
  // No imagelist is wired in the demo — -1 means "no icon" per the
  // port contract.
  return -1;
}

}  // namespace winui_lite_demo
