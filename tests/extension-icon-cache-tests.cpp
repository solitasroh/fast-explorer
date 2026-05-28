#include "test-harness.h"

#include "explorer/extension-icon-cache.h"

using fast_explorer::ui::ExtensionIconCache;

FE_TEST_CASE(ExtensionIconCache_Default_IsEmpty) {
  ExtensionIconCache cache;
  FE_ASSERT_EQ(cache.size(), static_cast<std::size_t>(0));
  FE_ASSERT_TRUE(cache.empty());
  FE_ASSERT_EQ(cache.capacity(),
               ExtensionIconCache::kDefaultCapacity);
}

FE_TEST_CASE(ExtensionIconCache_Lookup_Miss_ReturnsMissingSentinel) {
  ExtensionIconCache cache;
  FE_ASSERT_EQ(cache.lookup(L".txt"),
               ExtensionIconCache::kMissingIndex);
}

FE_TEST_CASE(ExtensionIconCache_Insert_ThenLookup_HitsSameIndex) {
  ExtensionIconCache cache;
  FE_ASSERT_EQ(cache.insert(L".txt", 42), 42);
  FE_ASSERT_EQ(cache.lookup(L".txt"), 42);
  FE_ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(ExtensionIconCache_Lookup_IsCaseInsensitive) {
  ExtensionIconCache cache;
  cache.insert(L".TXT", 7);
  FE_ASSERT_EQ(cache.lookup(L".txt"), 7);
  FE_ASSERT_EQ(cache.lookup(L".Txt"), 7);
}

FE_TEST_CASE(ExtensionIconCache_Insert_SameKey_UpdatesValueWithoutGrowth) {
  ExtensionIconCache cache;
  cache.insert(L".txt", 1);
  cache.insert(L".txt", 99);
  FE_ASSERT_EQ(cache.size(), static_cast<std::size_t>(1));
  FE_ASSERT_EQ(cache.lookup(L".txt"), 99);
}

FE_TEST_CASE(ExtensionIconCache_CapacityZero_TreatedAsOne) {
  ExtensionIconCache cache(0);
  FE_ASSERT_EQ(cache.capacity(), static_cast<std::size_t>(1));
  cache.insert(L".a", 1);
  int evicted = -999;
  cache.insert(L".b", 2, &evicted);
  // The .a entry must have been evicted with its index reported.
  FE_ASSERT_EQ(evicted, 1);
  FE_ASSERT_EQ(cache.lookup(L".a"),
               ExtensionIconCache::kMissingIndex);
  FE_ASSERT_EQ(cache.lookup(L".b"), 2);
}

FE_TEST_CASE(ExtensionIconCache_Capacity_EvictsLeastRecentlyUsed) {
  ExtensionIconCache cache(2);
  cache.insert(L".a", 10);
  cache.insert(L".b", 20);
  int evicted = -999;
  // .c crosses capacity; LRU is .a (oldest, never re-touched).
  cache.insert(L".c", 30, &evicted);
  FE_ASSERT_EQ(evicted, 10);
  FE_ASSERT_EQ(cache.lookup(L".a"),
               ExtensionIconCache::kMissingIndex);
  FE_ASSERT_EQ(cache.lookup(L".b"), 20);
  FE_ASSERT_EQ(cache.lookup(L".c"), 30);
}

FE_TEST_CASE(ExtensionIconCache_Lookup_PromotesToMostRecentlyUsed) {
  ExtensionIconCache cache(2);
  cache.insert(L".a", 10);
  cache.insert(L".b", 20);
  // Touch .a — it must now be MRU, so the next eviction takes .b.
  cache.lookup(L".a");
  int evicted = -999;
  cache.insert(L".c", 30, &evicted);
  FE_ASSERT_EQ(evicted, 20);
  FE_ASSERT_EQ(cache.lookup(L".a"), 10);
  FE_ASSERT_EQ(cache.lookup(L".b"),
               ExtensionIconCache::kMissingIndex);
  FE_ASSERT_EQ(cache.lookup(L".c"), 30);
}

FE_TEST_CASE(ExtensionIconCache_Insert_NoEviction_LeavesEvictedOutMissing) {
  ExtensionIconCache cache(2);
  int evicted = 123;
  cache.insert(L".a", 1, &evicted);
  FE_ASSERT_EQ(evicted, ExtensionIconCache::kMissingIndex);
}

FE_TEST_CASE(ExtensionIconCache_Clear_RemovesAllEntries) {
  ExtensionIconCache cache;
  cache.insert(L".a", 1);
  cache.insert(L".b", 2);
  cache.clear();
  FE_ASSERT_TRUE(cache.empty());
  FE_ASSERT_EQ(cache.lookup(L".a"),
               ExtensionIconCache::kMissingIndex);
  FE_ASSERT_EQ(cache.lookup(L".b"),
               ExtensionIconCache::kMissingIndex);
}

FE_TEST_CASE(ExtensionIconCache_Insert_Update_DoesNotEmitEviction) {
  ExtensionIconCache cache(1);
  cache.insert(L".a", 1);
  int evicted = -999;
  cache.insert(L".a", 2, &evicted);
  FE_ASSERT_EQ(evicted, ExtensionIconCache::kMissingIndex);
  FE_ASSERT_EQ(cache.lookup(L".a"), 2);
}
