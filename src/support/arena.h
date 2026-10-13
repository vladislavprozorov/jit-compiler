#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kestrel {

class Arena {
 public:
  explicit Arena(std::size_t block_size = 64 * 1024) noexcept : block_size_(block_size) {}

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) noexcept = default;
  Arena& operator=(Arena&&) noexcept = default;
  ~Arena() = default;

  [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment);

  template <typename T, typename... Args>
    requires std::is_trivially_destructible_v<T>
  [[nodiscard]] T* make(Args&&... args) {
    void* memory = allocate(sizeof(T), alignof(T));
    return ::new (memory) T{std::forward<Args>(args)...};
  }

  template <typename T>
    requires std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>
  [[nodiscard]] std::span<T> copy(std::span<const T> items) {
    if (items.empty()) {
      return {};
    }
    void* memory = allocate(items.size_bytes(), alignof(T));
    std::memcpy(memory, items.data(), items.size_bytes());
    return {static_cast<T*>(memory), items.size()};
  }

  template <typename T>
  [[nodiscard]] std::span<T> copy(const std::vector<T>& items) {
    return copy(std::span<const T>(items));
  }

  [[nodiscard]] std::size_t bytes_allocated() const noexcept { return bytes_allocated_; }
  [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }

 private:
  std::size_t block_size_;
  std::vector<std::unique_ptr<std::byte[]>> blocks_;
  std::byte* cursor_ = nullptr;
  std::byte* end_ = nullptr;
  std::size_t bytes_allocated_ = 0;
};

}  // namespace kestrel
