#include "test-harness.h"

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/name-arena.h"

using fast_explorer::core::NameArena;

namespace {

std::wstring repeatChar(wchar_t ch, std::size_t count) {
  return std::wstring(count, ch);
}

}  // namespace

FE_TEST_CASE(name_arena_default_ctor_reserves_default_size) {
  NameArena arena;
  FE_ASSERT_TRUE(arena.valid());
  FE_ASSERT_EQ(arena.committedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(arena.usedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(arena.reservedBytes(), NameArena::kDefaultReserveBytes);
}

FE_TEST_CASE(name_arena_custom_reserve_rounds_up_to_chunk) {
  NameArena arena(65 * 1024);
  FE_ASSERT_TRUE(arena.valid());
  FE_ASSERT_EQ(arena.reservedBytes(),
               static_cast<std::size_t>(2 * NameArena::kCommitChunkBytes));
}

FE_TEST_CASE(name_arena_zero_reserve_falls_back_to_one_chunk) {
  NameArena arena(0);
  FE_ASSERT_TRUE(arena.valid());
  FE_ASSERT_EQ(arena.reservedBytes(), NameArena::kCommitChunkBytes);
}

FE_TEST_CASE(name_arena_intern_empty_returns_empty_view_no_commit) {
  NameArena arena;
  const std::wstring_view view = arena.intern(std::wstring_view());
  FE_ASSERT_TRUE(view.empty());
  FE_ASSERT_EQ(arena.committedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(arena.usedBytes(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(name_arena_intern_simple_name_returns_aliased_view) {
  NameArena arena;
  const std::wstring_view input = L"readme.txt";
  const std::wstring_view stored = arena.intern(input);
  FE_ASSERT_EQ(stored.size(), input.size());
  FE_ASSERT_EQ(
      std::memcmp(stored.data(), input.data(), input.size() * sizeof(wchar_t)),
      0);
  FE_ASSERT_NE(stored.data(), input.data());
}

FE_TEST_CASE(name_arena_intern_advances_used_by_byte_count) {
  NameArena arena;
  arena.intern(L"abc");
  FE_ASSERT_EQ(arena.usedBytes(), static_cast<std::size_t>(3 * sizeof(wchar_t)));
  arena.intern(L"defgh");
  FE_ASSERT_EQ(arena.usedBytes(),
               static_cast<std::size_t>((3 + 5) * sizeof(wchar_t)));
}

FE_TEST_CASE(name_arena_commits_chunk_aligned_pages) {
  NameArena arena;
  arena.intern(L"x");
  FE_ASSERT_EQ(arena.committedBytes(), NameArena::kCommitChunkBytes);
  FE_ASSERT_EQ(arena.committedBytes() % NameArena::kCommitChunkBytes,
               static_cast<std::size_t>(0));
}

FE_TEST_CASE(name_arena_many_tiny_names_share_single_chunk) {
  NameArena arena;
  for (int i = 0; i < 1000; ++i) {
    arena.intern(L"ab");
  }
  FE_ASSERT_EQ(arena.committedBytes(), NameArena::kCommitChunkBytes);
}

FE_TEST_CASE(name_arena_views_remain_valid_after_subsequent_interns) {
  NameArena arena;
  const std::wstring first = L"persistent";
  const std::wstring_view firstView = arena.intern(first);
  std::vector<std::wstring_view> laterViews;
  for (int i = 0; i < 2000; ++i) {
    laterViews.push_back(
        arena.intern(repeatChar(static_cast<wchar_t>(L'a' + (i % 26)), 10)));
  }
  FE_ASSERT_EQ(firstView.size(), first.size());
  FE_ASSERT_EQ(
      std::memcmp(firstView.data(), first.data(), first.size() * sizeof(wchar_t)),
      0);
  for (const auto& v : laterViews) {
    FE_ASSERT_NE(v.data(), firstView.data());
  }
}

FE_TEST_CASE(name_arena_intern_crosses_chunk_boundary) {
  NameArena arena(256 * 1024);
  const std::size_t firstFill =
      NameArena::kCommitChunkBytes / sizeof(wchar_t) - 50;
  const auto firstView = arena.intern(repeatChar(L'A', firstFill));
  FE_ASSERT_EQ(firstView.size(), firstFill);
  const auto strad = arena.intern(repeatChar(L'B', 200));
  FE_ASSERT_EQ(strad.size(), static_cast<std::size_t>(200));
  for (std::size_t i = 0; i < strad.size(); ++i) {
    FE_ASSERT_EQ(strad[i], L'B');
  }
  FE_ASSERT_TRUE(arena.committedBytes() >= 2 * NameArena::kCommitChunkBytes);
  FE_ASSERT_EQ(arena.committedBytes() % NameArena::kCommitChunkBytes,
               static_cast<std::size_t>(0));
}

FE_TEST_CASE(name_arena_intern_returns_empty_when_exhausted) {
  NameArena arena(NameArena::kCommitChunkBytes);
  bool sawExhaustion = false;
  for (int i = 0; i < 5000; ++i) {
    const auto v = arena.intern(repeatChar(L'X', 100));
    if (v.empty()) {
      sawExhaustion = true;
      break;
    }
  }
  FE_ASSERT_TRUE(sawExhaustion);
  FE_ASSERT_TRUE(arena.valid());
}

FE_TEST_CASE(name_arena_reset_clears_committed_and_used) {
  NameArena arena;
  arena.intern(L"some-name");
  FE_ASSERT_TRUE(arena.committedBytes() > 0);
  FE_ASSERT_TRUE(arena.usedBytes() > 0);
  arena.reset();
  FE_ASSERT_EQ(arena.committedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(arena.usedBytes(), static_cast<std::size_t>(0));
  FE_ASSERT_EQ(arena.reservedBytes(), NameArena::kDefaultReserveBytes);
  FE_ASSERT_TRUE(arena.valid());
}

FE_TEST_CASE(name_arena_intern_after_reset_succeeds) {
  NameArena arena;
  arena.intern(L"first-generation");
  arena.reset();
  const auto v = arena.intern(L"second");
  FE_ASSERT_EQ(v.size(), static_cast<std::size_t>(6));
  FE_ASSERT_EQ(std::memcmp(v.data(), L"second", 6 * sizeof(wchar_t)), 0);
}

FE_TEST_CASE(name_arena_move_ctor_transfers_ownership) {
  NameArena src;
  const auto view = src.intern(L"moved");
  const wchar_t* srcDataPtr = view.data();
  const std::size_t srcUsed = src.usedBytes();
  const std::size_t srcCommitted = src.committedBytes();

  NameArena dst(std::move(src));
  FE_ASSERT_TRUE(dst.valid());
  FE_ASSERT_EQ(dst.usedBytes(), srcUsed);
  FE_ASSERT_EQ(dst.committedBytes(), srcCommitted);
  FE_ASSERT_EQ(std::memcmp(srcDataPtr, L"moved", 5 * sizeof(wchar_t)), 0);
  FE_ASSERT_FALSE(src.valid());
  FE_ASSERT_EQ(src.usedBytes(), static_cast<std::size_t>(0));
}

FE_TEST_CASE(name_arena_move_assign_replaces_existing_reservation) {
  NameArena dst;
  dst.intern(L"original");
  NameArena src;
  src.intern(L"replacement");

  const std::size_t srcUsed = src.usedBytes();
  dst = std::move(src);
  FE_ASSERT_TRUE(dst.valid());
  FE_ASSERT_EQ(dst.usedBytes(), srcUsed);
  FE_ASSERT_FALSE(src.valid());
}

FE_TEST_CASE(name_arena_self_move_assign_is_safe) {
  NameArena arena;
  arena.intern(L"steady");
  const std::size_t usedBefore = arena.usedBytes();
  NameArena& alias = arena;
  arena = std::move(alias);
  FE_ASSERT_TRUE(arena.valid());
  FE_ASSERT_EQ(arena.usedBytes(), usedBefore);
}
