#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "support/source_location.h"

namespace kestrel {

enum class TypeKind : std::uint8_t { Void, Int, Float, Bool };

[[nodiscard]] std::string_view type_kind_name(TypeKind kind) noexcept;

struct TypeRef {
  TypeKind kind;
  SourceLocation location;
};

enum class UnaryOp : std::uint8_t { Neg, Not, BitNot };

enum class BinaryOp : std::uint8_t {
  Add,
  Sub,
  Mul,
  Div,
  Rem,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
  LogicalAnd,
  LogicalOr,
};

enum class AssignOp : std::uint8_t { Assign, Add, Sub, Mul, Div, Rem };

[[nodiscard]] std::string_view unary_op_spelling(UnaryOp op) noexcept;
[[nodiscard]] std::string_view binary_op_spelling(BinaryOp op) noexcept;
[[nodiscard]] std::string_view assign_op_spelling(AssignOp op) noexcept;

template <typename T, typename Node>
[[nodiscard]] bool isa(const Node* node) noexcept {
  return node->kind == T::kKind;
}

template <typename T, typename Node>
[[nodiscard]] T* cast(Node* node) noexcept {
  assert(isa<T>(node));
  return static_cast<T*>(node);
}

template <typename T, typename Node>
[[nodiscard]] const T* cast(const Node* node) noexcept {
  assert(isa<T>(node));
  return static_cast<const T*>(node);
}

template <typename T, typename Node>
[[nodiscard]] T* dyn_cast(Node* node) noexcept {
  return isa<T>(node) ? static_cast<T*>(node) : nullptr;
}

template <typename T, typename Node>
[[nodiscard]] const T* dyn_cast(const Node* node) noexcept {
  return isa<T>(node) ? static_cast<const T*>(node) : nullptr;
}

enum class ExprKind : std::uint8_t {
  IntLiteral,
  FloatLiteral,
  BoolLiteral,
  Name,
  Unary,
  Binary,
  Call
};

struct Expr {
  const ExprKind kind;
  const SourceLocation location;

 protected:
  Expr(ExprKind k, SourceLocation loc) noexcept : kind(k), location(loc) {}
};

struct IntLiteralExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::IntLiteral;
  IntLiteralExpr(SourceLocation loc, std::int64_t v) noexcept : Expr(kKind, loc), value(v) {}
  std::int64_t value;
};

struct FloatLiteralExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::FloatLiteral;
  FloatLiteralExpr(SourceLocation loc, double v) noexcept : Expr(kKind, loc), value(v) {}
  double value;
};

struct BoolLiteralExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::BoolLiteral;
  BoolLiteralExpr(SourceLocation loc, bool v) noexcept : Expr(kKind, loc), value(v) {}
  bool value;
};

struct NameExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::Name;
  NameExpr(SourceLocation loc, std::string_view n) noexcept : Expr(kKind, loc), name(n) {}
  std::string_view name;
};

struct UnaryExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::Unary;
  UnaryExpr(SourceLocation loc, UnaryOp o, Expr* e) noexcept
      : Expr(kKind, loc), op(o), operand(e) {}
  UnaryOp op;
  Expr* operand;
};

struct BinaryExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::Binary;
  BinaryExpr(SourceLocation loc, BinaryOp o, Expr* l, Expr* r) noexcept
      : Expr(kKind, loc), op(o), lhs(l), rhs(r) {}
  BinaryOp op;
  Expr* lhs;
  Expr* rhs;
};

struct CallExpr : Expr {
  static constexpr ExprKind kKind = ExprKind::Call;
  CallExpr(SourceLocation loc, std::string_view c, std::span<Expr*> a) noexcept
      : Expr(kKind, loc), callee(c), args(a) {}
  std::string_view callee;
  std::span<Expr*> args;
};

enum class StmtKind : std::uint8_t { Block, Let, Assign, Expr, If, While, Break, Continue, Return };

struct Stmt {
  const StmtKind kind;
  const SourceLocation location;

 protected:
  Stmt(StmtKind k, SourceLocation loc) noexcept : kind(k), location(loc) {}
};

struct BlockStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Block;
  BlockStmt(SourceLocation loc, std::span<Stmt*> s) noexcept : Stmt(kKind, loc), statements(s) {}
  std::span<Stmt*> statements;
};

struct LetStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Let;
  LetStmt(SourceLocation loc, bool m, std::string_view n, std::optional<TypeRef> t,
          Expr* i) noexcept
      : Stmt(kKind, loc), is_mutable(m), name(n), type(t), init(i) {}
  bool is_mutable;
  std::string_view name;
  std::optional<TypeRef> type;
  Expr* init;
};

struct AssignStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Assign;
  AssignStmt(SourceLocation loc, AssignOp o, NameExpr* t, Expr* v) noexcept
      : Stmt(kKind, loc), op(o), target(t), value(v) {}
  AssignOp op;
  NameExpr* target;
  Expr* value;
};

struct ExprStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Expr;
  ExprStmt(SourceLocation loc, Expr* e) noexcept : Stmt(kKind, loc), expr(e) {}
  Expr* expr;
};

struct IfStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::If;
  IfStmt(SourceLocation loc, Expr* c, BlockStmt* t, Stmt* e) noexcept
      : Stmt(kKind, loc), condition(c), then_branch(t), else_branch(e) {}
  Expr* condition;
  BlockStmt* then_branch;
  Stmt* else_branch;
};

struct WhileStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::While;
  WhileStmt(SourceLocation loc, Expr* c, BlockStmt* b) noexcept
      : Stmt(kKind, loc), condition(c), body(b) {}
  Expr* condition;
  BlockStmt* body;
};

struct BreakStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Break;
  explicit BreakStmt(SourceLocation loc) noexcept : Stmt(kKind, loc) {}
};

struct ContinueStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Continue;
  explicit ContinueStmt(SourceLocation loc) noexcept : Stmt(kKind, loc) {}
};

struct ReturnStmt : Stmt {
  static constexpr StmtKind kKind = StmtKind::Return;
  ReturnStmt(SourceLocation loc, Expr* v) noexcept : Stmt(kKind, loc), value(v) {}
  Expr* value;
};

struct Param {
  std::string_view name;
  SourceLocation location;
  TypeRef type;
};

struct FnDecl {
  std::string_view name;
  SourceLocation location;
  std::span<Param> params;
  TypeRef return_type;
  BlockStmt* body;
};

struct Module {
  std::span<FnDecl*> functions;
};

}  // namespace kestrel
