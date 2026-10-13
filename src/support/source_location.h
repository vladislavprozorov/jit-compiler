#pragma once

#include <cstdint>

namespace kestrel {

struct SourceLocation {
  std::uint32_t offset = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 1;

  friend constexpr bool operator==(SourceLocation, SourceLocation) = default;
};

}  // namespace kestrel
