#pragma once

#include <string>

#include "frontend/ast.h"

namespace kestrel {

[[nodiscard]] std::string print_expr(const Expr& expr);
[[nodiscard]] std::string print_module(const Module& module);

}  // namespace kestrel
