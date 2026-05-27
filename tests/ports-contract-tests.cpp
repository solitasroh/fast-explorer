// Contract tests for the lib/winui_lite/ports/ interfaces. The ports
// themselves are pure-virtual, so the tests fabricate the smallest
// possible adapters in an anonymous namespace and exercise them both
// directly and through a base-class pointer. They verify the
// interface SHAPE (override signatures resolve, polymorphic dispatch
// works) rather than any behaviour — real adapters are introduced
// in step 8 with their own behavioural tests.

#include "test-harness.h"
#include "winui_lite/ports/item-activator.h"
#include "winui_lite/ports/item-dispatcher.h"
#include "winui_lite/ports/item-source.h"

using fast_explorer::ui::ports::ActivationResult;
using fast_explorer::ui::ports::ItemActivator;
using fast_explorer::ui::ports::ItemDispatcher;
using fast_explorer::ui::ports::ItemField;
using fast_explorer::ui::ports::ItemId;
using fast_explorer::ui::ports::ItemSource;
using fast_explorer::ui::ports::kInvalidItemId;

namespace {

// Three rows, ids = index + 1 so kInvalidItemId (0) really is invalid.
class FakeSource : public ItemSource {
 public:
  void navigateTo(const std::wstring& loc) override { loc_ = loc; }
  const std::wstring& currentLocation() const override { return loc_; }
  std::size_t count() const override { return 3; }
  ItemId idAt(std::size_t index) const override {
    if (index >= 3) return kInvalidItemId;
    return static_cast<ItemId>(index + 1);
  }

 private:
  std::wstring loc_;
};

class FakeDispatcher : public ItemDispatcher {
 public:
  std::wstring textFor(ItemId id, ItemField field) const override {
    if (id == kInvalidItemId) return L"";
    switch (field) {
      case ItemField::Name:         return L"name-" + std::to_wstring(id);
      case ItemField::SizeText:     return L"size-" + std::to_wstring(id);
      case ItemField::ModifiedText: return L"mod-"  + std::to_wstring(id);
      case ItemField::TypeText:     return L"type-" + std::to_wstring(id);
    }
    return L"";
  }
  int iconIndexFor(ItemId id) const override {
    return id == kInvalidItemId ? -1 : static_cast<int>(id);
  }
};

class FakeActivator : public ItemActivator {
 public:
  ActivationResult activate(ItemId id) override {
    if (id == kInvalidItemId) return {};
    return ActivationResult{true, L"sub/" + std::to_wstring(id)};
  }
};

}  // namespace

FE_TEST_CASE(Ports_FakeSource_NavigateRoundTrip) {
  FakeSource s;
  FE_ASSERT_TRUE(s.currentLocation().empty());
  s.navigateTo(L"C:\\dev");
  FE_ASSERT_TRUE(s.currentLocation() == L"C:\\dev");
}

FE_TEST_CASE(Ports_FakeSource_CountAndIdMapping) {
  FakeSource s;
  FE_ASSERT_EQ(s.count(), static_cast<std::size_t>(3));
  FE_ASSERT_EQ(s.idAt(0), static_cast<ItemId>(1));
  FE_ASSERT_EQ(s.idAt(2), static_cast<ItemId>(3));
}

FE_TEST_CASE(Ports_FakeSource_OutOfRangeReturnsInvalidId) {
  FakeSource s;
  FE_ASSERT_EQ(s.idAt(99), kInvalidItemId);
}

FE_TEST_CASE(Ports_FakeDispatcher_TextPerField) {
  FakeDispatcher d;
  FE_ASSERT_TRUE(d.textFor(7, ItemField::Name)         == L"name-7");
  FE_ASSERT_TRUE(d.textFor(7, ItemField::SizeText)     == L"size-7");
  FE_ASSERT_TRUE(d.textFor(7, ItemField::ModifiedText) == L"mod-7");
  FE_ASSERT_TRUE(d.textFor(7, ItemField::TypeText)     == L"type-7");
}

FE_TEST_CASE(Ports_FakeDispatcher_InvalidIdYieldsEmptyAndMinusOne) {
  FakeDispatcher d;
  FE_ASSERT_TRUE(d.textFor(kInvalidItemId, ItemField::Name).empty());
  FE_ASSERT_EQ(d.iconIndexFor(kInvalidItemId), -1);
}

FE_TEST_CASE(Ports_FakeActivator_HandledWithLocation) {
  FakeActivator a;
  const auto r = a.activate(5);
  FE_ASSERT_TRUE(r.handled);
  FE_ASSERT_TRUE(r.nextLocation == L"sub/5");
}

FE_TEST_CASE(Ports_FakeActivator_InvalidIdNotHandled) {
  FakeActivator a;
  const auto r = a.activate(kInvalidItemId);
  FE_ASSERT_TRUE(!r.handled);
  FE_ASSERT_TRUE(r.nextLocation.empty());
}

FE_TEST_CASE(Ports_PolymorphicDispatch_ThroughBasePointers) {
  FakeSource source;
  FakeDispatcher dispatcher;
  FakeActivator activator;

  ItemSource* sBase = &source;
  ItemDispatcher* dBase = &dispatcher;
  ItemActivator* aBase = &activator;

  sBase->navigateTo(L"X:");
  FE_ASSERT_EQ(sBase->count(), static_cast<std::size_t>(3));
  const ItemId id = sBase->idAt(1);
  FE_ASSERT_TRUE(dBase->textFor(id, ItemField::Name) == L"name-2");
  FE_ASSERT_TRUE(aBase->activate(id).handled);
}
