#include "frontend/parser.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace kestrel {

namespace {

constexpr int kPrefixBindingPower = 19;

struct InfixOperator {
  BinaryOp op;
  int left_power;
  int right_power;
  bool is_comparison;
};

std::optional<InfixOperator> infix_operator(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::PipePipe:
      return InfixOperator{BinaryOp::LogicalOr, 1, 2, false};
    case TokenKind::AmpAmp:
      return InfixOperator{BinaryOp::LogicalAnd, 3, 4, false};
    case TokenKind::EqualEqual:
      return InfixOperator{BinaryOp::Eq, 5, 6, true};
    case TokenKind::BangEqual:
      return InfixOperator{BinaryOp::Ne, 5, 6, true};
    case TokenKind::Less:
      return InfixOperator{BinaryOp::Lt, 5, 6, true};
    case TokenKind::LessEqual:
      return InfixOperator{BinaryOp::Le, 5, 6, true};
    case TokenKind::Greater:
      return InfixOperator{BinaryOp::Gt, 5, 6, true};
    case TokenKind::GreaterEqual:
      return InfixOperator{BinaryOp::Ge, 5, 6, true};
    case TokenKind::Pipe:
      return InfixOperator{BinaryOp::BitOr, 7, 8, false};
    case TokenKind::Caret:
      return InfixOperator{BinaryOp::BitXor, 9, 10, false};
    case TokenKind::Amp:
      return InfixOperator{BinaryOp::BitAnd, 11, 12, false};
    case TokenKind::LessLess:
      return InfixOperator{BinaryOp::Shl, 13, 14, false};
    case TokenKind::GreaterGreater:
      return InfixOperator{BinaryOp::Shr, 13, 14, false};
    case TokenKind::Plus:
      return InfixOperator{BinaryOp::Add, 15, 16, false};
    case TokenKind::Minus:
      return InfixOperator{BinaryOp::Sub, 15, 16, false};
    case TokenKind::Star:
      return InfixOperator{BinaryOp::Mul, 17, 18, false};
    case TokenKind::Slash:
      return InfixOperator{BinaryOp::Div, 17, 18, false};
    case TokenKind::Percent:
      return InfixOperator{BinaryOp::Rem, 17, 18, false};
    default:
      return std::nullopt;
  }
}

std::optional<AssignOp> assign_operator(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::Equal:
      return AssignOp::Assign;
    case TokenKind::PlusEqual:
      return AssignOp::Add;
    case TokenKind::MinusEqual:
      return AssignOp::Sub;
    case TokenKind::StarEqual:
      return AssignOp::Mul;
    case TokenKind::SlashEqual:
      return AssignOp::Div;
    case TokenKind::PercentEqual:
      return AssignOp::Rem;
    default:
      return std::nullopt;
  }
}

std::string describe(const Token& token) {
  switch (token.kind) {
    case TokenKind::Identifier:
    case TokenKind::IntLiteral:
    case TokenKind::FloatLiteral:
      return std::string(token_kind_name(token.kind)) + " '" + std::string(token.text) + "'";
    default:
      return std::string(token_kind_name(token.kind));
  }
}

std::string strip_separators(std::string_view text) {
  std::string digits;
  digits.reserve(text.size());
  for (const char c : text) {
    if (c != '_') {
      digits += c;
    }
  }
  return digits;
}

}  // namespace

class Parser::DepthGuard {
 public:
  explicit DepthGuard(Parser& parser) : parser_(parser), ok_(++parser.depth_ <= kMaxNestingDepth) {
    if (!ok_) {
      parser_.error_at_current("nesting is too deep (limit is " + std::to_string(kMaxNestingDepth) +
                               ")");
    }
  }
  DepthGuard(const DepthGuard&) = delete;
  DepthGuard& operator=(const DepthGuard&) = delete;
  ~DepthGuard() { --parser_.depth_; }

  explicit operator bool() const noexcept { return ok_; }

 private:
  Parser& parser_;
  bool ok_;
};

Parser::Parser(std::string_view source, Arena& arena, DiagnosticEngine& diags)
    : lexer_(source, diags), arena_(arena), diags_(diags) {
  advance();
}

void Parser::advance() {
  previous_ = current_;
  while (true) {
    current_ = lexer_.next();
    if (!current_.is(TokenKind::Error)) {
      return;
    }
    panic_ = true;
  }
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  advance();
  return true;
}

bool Parser::expect(TokenKind kind, std::string_view context) {
  if (match(kind)) {
    return true;
  }
  error_expected(token_kind_name(kind), context);
  return false;
}

void Parser::error_at(const Token& token, std::string message) {
  if (panic_) {
    return;
  }
  panic_ = true;
  diags_.error(token.location, std::move(message));
}

void Parser::error_expected(std::string_view what, std::string_view context) {
  std::string message = "expected ";
  message += what;
  if (!context.empty()) {
    message += ' ';
    message += context;
  }
  message += ", found ";
  message += describe(current_);
  error_at_current(std::move(message));
}

void Parser::synchronize_statement() {
  std::uint32_t braces = 0;
  while (!check(TokenKind::Eof)) {
    switch (current_.kind) {
      case TokenKind::LBrace:
        ++braces;
        break;
      case TokenKind::RBrace:
        if (braces == 0) {
          panic_ = false;
          return;
        }
        --braces;
        break;
      case TokenKind::Semicolon:
        if (braces == 0) {
          advance();
          panic_ = false;
          return;
        }
        break;
      case TokenKind::KwFn:
        panic_ = false;
        return;
      case TokenKind::KwLet:
      case TokenKind::KwVar:
      case TokenKind::KwIf:
      case TokenKind::KwWhile:
      case TokenKind::KwBreak:
      case TokenKind::KwContinue:
      case TokenKind::KwReturn:
        if (braces == 0) {
          panic_ = false;
          return;
        }
        break;
      default:
        break;
    }
    advance();
  }
  panic_ = false;
}

void Parser::synchronize_top_level() {
  while (!check(TokenKind::Eof) && !check(TokenKind::KwFn)) {
    advance();
  }
  panic_ = false;
}

Module Parser::parse_module() {
  std::vector<FnDecl*> functions;
  while (!check(TokenKind::Eof)) {
    if (check(TokenKind::KwFn)) {
      if (FnDecl* fn = parse_function()) {
        functions.push_back(fn);
      }
    } else {
      error_expected("function declaration", "");
    }
    if (panic_) {
      synchronize_top_level();
    }
  }
  return Module{arena_.copy(functions)};
}

Expr* Parser::parse_standalone_expression() {
  Expr* expr = parse_expression();
  if (expr != nullptr && !check(TokenKind::Eof)) {
    error_expected("end of input", "");
    return nullptr;
  }
  return expr;
}

std::optional<TypeRef> Parser::parse_type() {
  TypeKind kind{};
  switch (current_.kind) {
    case TokenKind::KwInt:
      kind = TypeKind::Int;
      break;
    case TokenKind::KwFloat:
      kind = TypeKind::Float;
      break;
    case TokenKind::KwBool:
      kind = TypeKind::Bool;
      break;
    default:
      error_expected("type", "");
      return std::nullopt;
  }
  const TypeRef type{kind, current_.location};
  advance();
  return type;
}

FnDecl* Parser::parse_function() {
  advance();
  if (!check(TokenKind::Identifier)) {
    error_expected("function name", "after 'fn'");
    return nullptr;
  }
  const Token name = current_;
  advance();
  if (!expect(TokenKind::LParen, "after function name")) {
    return nullptr;
  }

  std::vector<Param> params;
  while (!check(TokenKind::RParen)) {
    if (!check(TokenKind::Identifier)) {
      error_expected("parameter name", "");
      return nullptr;
    }
    const Token param_name = current_;
    advance();
    if (!expect(TokenKind::Colon, "after parameter name")) {
      return nullptr;
    }
    const std::optional<TypeRef> type = parse_type();
    if (!type) {
      return nullptr;
    }
    params.push_back(Param{param_name.text, param_name.location, *type});
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  if (!expect(TokenKind::RParen, "after parameters")) {
    return nullptr;
  }

  TypeRef return_type{TypeKind::Void, previous_.location};
  if (match(TokenKind::Arrow)) {
    const std::optional<TypeRef> type = parse_type();
    if (!type) {
      return nullptr;
    }
    return_type = *type;
  }

  if (!check(TokenKind::LBrace)) {
    error_expected("'{'", "before function body");
    return nullptr;
  }
  BlockStmt* body = parse_block();
  if (body == nullptr) {
    return nullptr;
  }
  return arena_.make<FnDecl>(name.text, name.location, arena_.copy(params), return_type, body);
}

BlockStmt* Parser::parse_block() {
  const DepthGuard guard(*this);
  if (!guard) {
    return nullptr;
  }
  const SourceLocation location = current_.location;
  if (!expect(TokenKind::LBrace, "")) {
    return nullptr;
  }

  std::vector<Stmt*> statements;
  while (!check(TokenKind::RBrace) && !check(TokenKind::Eof) && !check(TokenKind::KwFn)) {
    const std::uint32_t start = current_.location.offset;
    if (Stmt* stmt = parse_statement()) {
      statements.push_back(stmt);
    }
    if (panic_) {
      synchronize_statement();
      if (current_.location.offset == start && !check(TokenKind::RBrace) &&
          !check(TokenKind::Eof) && !check(TokenKind::KwFn)) {
        advance();
      }
    }
  }
  if (!expect(TokenKind::RBrace, "to close block")) {
    return nullptr;
  }
  return arena_.make<BlockStmt>(location, arena_.copy(statements));
}

Stmt* Parser::parse_statement() {
  switch (current_.kind) {
    case TokenKind::LBrace:
      return parse_block();
    case TokenKind::KwLet:
    case TokenKind::KwVar:
      return parse_let();
    case TokenKind::KwIf:
      return parse_if();
    case TokenKind::KwWhile:
      return parse_while();
    case TokenKind::KwReturn:
      return parse_return();
    case TokenKind::KwBreak: {
      const SourceLocation location = current_.location;
      advance();
      if (!expect(TokenKind::Semicolon, "after 'break'")) {
        return nullptr;
      }
      return arena_.make<BreakStmt>(location);
    }
    case TokenKind::KwContinue: {
      const SourceLocation location = current_.location;
      advance();
      if (!expect(TokenKind::Semicolon, "after 'continue'")) {
        return nullptr;
      }
      return arena_.make<ContinueStmt>(location);
    }
    case TokenKind::Semicolon:
      advance();
      return nullptr;
    default:
      return parse_expression_or_assignment();
  }
}

Stmt* Parser::parse_let() {
  const bool is_mutable = check(TokenKind::KwVar);
  advance();
  if (!check(TokenKind::Identifier)) {
    error_expected("variable name", is_mutable ? "after 'var'" : "after 'let'");
    return nullptr;
  }
  const Token name = current_;
  advance();

  std::optional<TypeRef> type;
  if (match(TokenKind::Colon)) {
    type = parse_type();
    if (!type) {
      return nullptr;
    }
  }
  if (!expect(TokenKind::Equal, "in variable declaration")) {
    return nullptr;
  }
  Expr* init = parse_expression();
  if (init == nullptr) {
    return nullptr;
  }
  if (!expect(TokenKind::Semicolon, "after variable declaration")) {
    return nullptr;
  }
  return arena_.make<LetStmt>(name.location, is_mutable, name.text, type, init);
}

Stmt* Parser::parse_if() {
  const DepthGuard guard(*this);
  if (!guard) {
    return nullptr;
  }
  const SourceLocation location = current_.location;
  advance();
  Expr* condition = parse_expression();
  if (condition == nullptr) {
    return nullptr;
  }
  if (!check(TokenKind::LBrace)) {
    error_expected("'{'", "after 'if' condition");
    return nullptr;
  }
  BlockStmt* then_branch = parse_block();
  if (then_branch == nullptr) {
    return nullptr;
  }

  Stmt* else_branch = nullptr;
  if (match(TokenKind::KwElse)) {
    if (check(TokenKind::KwIf)) {
      else_branch = parse_if();
    } else if (check(TokenKind::LBrace)) {
      else_branch = parse_block();
    } else {
      error_expected("'{' or 'if'", "after 'else'");
      return nullptr;
    }
    if (else_branch == nullptr) {
      return nullptr;
    }
  }
  return arena_.make<IfStmt>(location, condition, then_branch, else_branch);
}

Stmt* Parser::parse_while() {
  const SourceLocation location = current_.location;
  advance();
  Expr* condition = parse_expression();
  if (condition == nullptr) {
    return nullptr;
  }
  if (!check(TokenKind::LBrace)) {
    error_expected("'{'", "after 'while' condition");
    return nullptr;
  }
  BlockStmt* body = parse_block();
  if (body == nullptr) {
    return nullptr;
  }
  return arena_.make<WhileStmt>(location, condition, body);
}

Stmt* Parser::parse_return() {
  const SourceLocation location = current_.location;
  advance();
  Expr* value = nullptr;
  if (!check(TokenKind::Semicolon)) {
    value = parse_expression();
    if (value == nullptr) {
      return nullptr;
    }
  }
  if (!expect(TokenKind::Semicolon, value != nullptr ? "after return value" : "after 'return'")) {
    return nullptr;
  }
  return arena_.make<ReturnStmt>(location, value);
}

Stmt* Parser::parse_expression_or_assignment() {
  Expr* expr = parse_expression();
  if (expr == nullptr) {
    return nullptr;
  }

  if (const std::optional<AssignOp> op = assign_operator(current_.kind)) {
    const Token op_token = current_;
    auto* target = dyn_cast<NameExpr>(expr);
    if (target == nullptr) {
      error_at(op_token, "invalid assignment target; only variables can be assigned");
      return nullptr;
    }
    advance();
    Expr* value = parse_expression();
    if (value == nullptr) {
      return nullptr;
    }
    if (!expect(TokenKind::Semicolon, "after assignment")) {
      return nullptr;
    }
    return arena_.make<AssignStmt>(op_token.location, *op, target, value);
  }

  if (!expect(TokenKind::Semicolon, "after expression")) {
    return nullptr;
  }
  return arena_.make<ExprStmt>(expr->location, expr);
}

Expr* Parser::parse_expression(int min_binding_power) {
  const DepthGuard guard(*this);
  if (!guard) {
    return nullptr;
  }
  Expr* lhs = parse_prefix();
  if (lhs == nullptr) {
    return nullptr;
  }

  bool lhs_is_comparison = false;
  while (const std::optional<InfixOperator> infix = infix_operator(current_.kind)) {
    if (infix->left_power < min_binding_power) {
      break;
    }
    if (infix->is_comparison && lhs_is_comparison) {
      error_at_current("comparison operators cannot be chained; use '&&' to combine comparisons");
      return nullptr;
    }
    const Token op_token = current_;
    advance();
    Expr* rhs = parse_expression(infix->right_power);
    if (rhs == nullptr) {
      return nullptr;
    }
    lhs = arena_.make<BinaryExpr>(op_token.location, infix->op, lhs, rhs);
    lhs_is_comparison = infix->is_comparison;
  }
  return lhs;
}

Expr* Parser::parse_prefix() {
  UnaryOp op{};
  switch (current_.kind) {
    case TokenKind::Minus:
      op = UnaryOp::Neg;
      break;
    case TokenKind::Bang:
      op = UnaryOp::Not;
      break;
    case TokenKind::Tilde:
      op = UnaryOp::BitNot;
      break;
    default:
      return parse_primary();
  }

  const Token op_token = current_;
  advance();
  if (op == UnaryOp::Neg && check(TokenKind::IntLiteral)) {
    const Token literal = current_;
    advance();
    return parse_int_literal(literal, true, op_token.location);
  }
  Expr* operand = parse_expression(kPrefixBindingPower);
  if (operand == nullptr) {
    return nullptr;
  }
  return arena_.make<UnaryExpr>(op_token.location, op, operand);
}

Expr* Parser::parse_primary() {
  const Token token = current_;
  switch (token.kind) {
    case TokenKind::IntLiteral:
      advance();
      return parse_int_literal(token, false, token.location);
    case TokenKind::FloatLiteral:
      advance();
      return parse_float_literal(token);
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
      advance();
      return arena_.make<BoolLiteralExpr>(token.location, token.is(TokenKind::KwTrue));
    case TokenKind::Identifier:
      advance();
      if (check(TokenKind::LParen)) {
        return parse_call(token);
      }
      return arena_.make<NameExpr>(token.location, token.text);
    case TokenKind::LParen: {
      advance();
      Expr* inner = parse_expression();
      if (inner == nullptr) {
        return nullptr;
      }
      if (!expect(TokenKind::RParen, "to close parenthesized expression")) {
        return nullptr;
      }
      return inner;
    }
    default:
      error_expected("expression", "");
      return nullptr;
  }
}

Expr* Parser::parse_call(const Token& callee) {
  advance();
  std::vector<Expr*> args;
  while (!check(TokenKind::RParen)) {
    Expr* arg = parse_expression();
    if (arg == nullptr) {
      return nullptr;
    }
    args.push_back(arg);
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  if (!expect(TokenKind::RParen, "after call arguments")) {
    return nullptr;
  }
  return arena_.make<CallExpr>(callee.location, callee.text, arena_.copy(args));
}

Expr* Parser::parse_int_literal(const Token& token, bool negated, SourceLocation location) {
  std::string_view text = token.text;
  int base = 10;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  }
  const std::string digits = strip_separators(text);

  std::uint64_t magnitude = 0;
  const auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), magnitude, base);
  const std::uint64_t limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negated ? 1 : 0);
  if (ec != std::errc{} || ptr != digits.data() + digits.size() || magnitude > limit) {
    diags_.error(token.location, "integer literal '" + std::string(token.text) +
                                     "' is out of range for type 'int'");
    return arena_.make<IntLiteralExpr>(location, std::int64_t{0});
  }
  const std::uint64_t bits = negated ? std::uint64_t{0} - magnitude : magnitude;
  return arena_.make<IntLiteralExpr>(location, static_cast<std::int64_t>(bits));
}

Expr* Parser::parse_float_literal(const Token& token) {
  const std::string digits = strip_separators(token.text);
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(digits.c_str(), &end);
  if (errno == ERANGE && std::isinf(value)) {
    diags_.error(token.location, "float literal '" + std::string(token.text) +
                                     "' is out of range for type 'float'");
    return arena_.make<FloatLiteralExpr>(token.location, 0.0);
  }
  return arena_.make<FloatLiteralExpr>(token.location, value);
}

}  // namespace kestrel
