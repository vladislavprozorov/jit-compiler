#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "support/arena.h"
#include "support/diagnostics.h"

namespace kestrel {

class Parser {
 public:
  static constexpr std::uint32_t kMaxNestingDepth = 256;

  Parser(std::string_view source, Arena& arena, DiagnosticEngine& diags);

  [[nodiscard]] Module parse_module();
  [[nodiscard]] Expr* parse_standalone_expression();

 private:
  class DepthGuard;

  void advance();
  [[nodiscard]] bool check(TokenKind kind) const noexcept { return current_.kind == kind; }
  bool match(TokenKind kind);
  bool expect(TokenKind kind, std::string_view context);

  void error_at(const Token& token, std::string message);
  void error_at_current(std::string message) { error_at(current_, std::move(message)); }
  void error_expected(std::string_view what, std::string_view context);

  void synchronize_statement();
  void synchronize_top_level();

  FnDecl* parse_function();
  std::optional<TypeRef> parse_type();

  BlockStmt* parse_block();
  Stmt* parse_statement();
  Stmt* parse_let();
  Stmt* parse_if();
  Stmt* parse_while();
  Stmt* parse_return();
  Stmt* parse_expression_or_assignment();

  Expr* parse_expression(int min_binding_power = 0);
  Expr* parse_prefix();
  Expr* parse_primary();
  Expr* parse_call(const Token& callee);
  Expr* parse_int_literal(const Token& token, bool negated, SourceLocation location);
  Expr* parse_float_literal(const Token& token);

  Lexer lexer_;
  Arena& arena_;
  DiagnosticEngine& diags_;
  Token current_;
  Token previous_;
  bool panic_ = false;
  std::uint32_t depth_ = 0;
};

}  // namespace kestrel
