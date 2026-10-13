#include "support/arena.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace kestrel {
namespace {

struct Pair {
  std::int64_t a;
  double b;
};

TEST(Arena, MakeConstructsAlignedObjects) {
  Arena arena(256);
  auto* c = arena.make<char>('x');
  auto* p = arena.make<Pair>(7, 2.5);
  EXPECT_EQ(*c, 'x');
  EXPECT_EQ(p->a, 7);
  EXPECT_EQ(p->b, 2.5);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(Pair), 0u);
}

TEST(Arena, AllocationsDoNotOverlapAcrossBlocks) {
  Arena arena(128);
  std::vector<std::int64_t*> values;
  for (std::int64_t i = 0; i < 1000; ++i) {
    values.push_back(arena.make<std::int64_t>(i));
  }
  for (std::int64_t i = 0; i < 1000; ++i) {
    EXPECT_EQ(*values[static_cast<std::size_t>(i)], i);
  }
  EXPECT_GT(arena.block_count(), 1u);
  EXPECT_EQ(arena.bytes_allocated(), 1000 * sizeof(std::int64_t));
}

TEST(Arena, LargeAllocationKeepsCurrentBlock) {
  Arena arena(1024);
  auto* first = arena.make<std::int32_t>(1);
  const std::size_t blocks_before = arena.block_count();
  void* big = arena.allocate(4096, 64);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(big) % 64, 0u);
  EXPECT_EQ(arena.block_count(), blocks_before + 1);
  auto* second = arena.make<std::int32_t>(2);
  EXPECT_EQ(reinterpret_cast<std::byte*>(second) - reinterpret_cast<std::byte*>(first),
            static_cast<std::ptrdiff_t>(sizeof(std::int32_t)));
}

TEST(Arena, CopySpan) {
  Arena arena;
  const std::vector<int> source{1, 2, 3, 4};
  std::span<int> copy = arena.copy(source);
  ASSERT_EQ(copy.size(), 4u);
  EXPECT_NE(copy.data(), source.data());
  EXPECT_TRUE(std::equal(copy.begin(), copy.end(), source.begin()));
  EXPECT_TRUE(arena.copy(std::vector<int>{}).empty());
}

}  // namespace
}  // namespace kestrel
