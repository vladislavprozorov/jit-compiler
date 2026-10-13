#include "support/arena.h"

#include <bit>
#include <cassert>
#include <cstdint>

namespace kestrel {

void* Arena::allocate(std::size_t size, std::size_t alignment) {
  assert(std::has_single_bit(alignment));
  if (size == 0) {
    size = 1;
  }

  const auto current = reinterpret_cast<std::uintptr_t>(cursor_);
  const std::uintptr_t aligned = (current + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
  if (cursor_ != nullptr && aligned + size <= reinterpret_cast<std::uintptr_t>(end_)) {
    cursor_ = reinterpret_cast<std::byte*>(aligned + size);
    bytes_allocated_ += size;
    return reinterpret_cast<void*>(aligned);
  }

  const std::size_t padded = size + alignment - 1;
  if (padded > block_size_ / 4) {
    auto block = std::make_unique_for_overwrite<std::byte[]>(padded);
    const auto base = reinterpret_cast<std::uintptr_t>(block.get());
    const std::uintptr_t result = (base + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
    blocks_.push_back(std::move(block));
    bytes_allocated_ += size;
    return reinterpret_cast<void*>(result);
  }

  blocks_.push_back(std::make_unique_for_overwrite<std::byte[]>(block_size_));
  cursor_ = blocks_.back().get();
  end_ = cursor_ + block_size_;
  return allocate(size, alignment);
}

}  // namespace kestrel
