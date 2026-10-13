#include "frontend/ast_printer.h"

#include <array>
#include <charconv>
#include <cmath>
#include <string_view>

namespace kestrel {

namespace {

void append_float(std::string& out, double value) {
  if (std::isnan(value)) {
    out += "nan";
    return;
  }
  if (std::isinf(value)) {
    out += value < 0 ? "-inf" : "inf";
    return;
  }
  std::array<char, 32> buf{};
  const auto result = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  const std::string_view text(buf.data(), static_cast<std::size_t>(result.ptr - buf.data()));
  out += text;
  if (text.find_first_of(".e") == std::string_view::npos) {
    out += ".0";
  }
}

class Printer {
 public:
  explicit Printer(std::string& out) noexcept : out_(out) {}

  void expr(const Expr& e) {
    switch (e.kind) {
      case ExprKind::IntLiteral:
        out_ += std::to_string(cast<IntLiteralExpr>(&e)->value);
        return;
      case ExprKind::FloatLiteral:
        append_float(out_, cast<FloatLiteralExpr>(&e)->value);
        return;
      case ExprKind::BoolLiteral:
        out_ += cast<BoolLiteralExpr>(&e)->value ? "true" : "false";
        return;
      case ExprKind::Name:
        out_ += cast<NameExpr>(&e)->name;
        return;
      case ExprKind::Unary: {
        const auto* u = cast<UnaryExpr>(&e);
        out_ += '(';
        out_ += unary_op_spelling(u->op);
        out_ += ' ';
        expr(*u->operand);
        out_ += ')';
        return;
      }
      case ExprKind::Binary: {
        const auto* b = cast<BinaryExpr>(&e);
        out_ += '(';
        out_ += binary_op_spelling(b->op);
        out_ += ' ';
        expr(*b->lhs);
        out_ += ' ';
        expr(*b->rhs);
        out_ += ')';
        return;
      }
      case ExprKind::Call: {
        const auto* c = cast<CallExpr>(&e);
        out_ += "(call ";
        out_ += c->callee;
        for (const Expr* arg : c->args) {
          out_ += ' ';
          expr(*arg);
        }
        out_ += ')';
        return;
      }
    }
  }

  void stmt(const Stmt& s, int indent) {
    switch (s.kind) {
      case StmtKind::Block: {
        const auto* b = cast<BlockStmt>(&s);
        out_ += "(block";
        for (const Stmt* child : b->statements) {
          newline(indent + 1);
          stmt(*child, indent + 1);
        }
        out_ += ')';
        return;
      }
      case StmtKind::Let: {
        const auto* l = cast<LetStmt>(&s);
        out_ += l->is_mutable ? "(var " : "(let ";
        out_ += l->name;
        if (l->type) {
          out_ += ": ";
          out_ += type_kind_name(l->type->kind);
        }
        out_ += ' ';
        expr(*l->init);
        out_ += ')';
        return;
      }
      case StmtKind::Assign: {
        const auto* a = cast<AssignStmt>(&s);
        out_ += '(';
        out_ += assign_op_spelling(a->op);
        out_ += ' ';
        out_ += a->target->name;
        out_ += ' ';
        expr(*a->value);
        out_ += ')';
        return;
      }
      case StmtKind::Expr:
        out_ += "(expr ";
        expr(*cast<ExprStmt>(&s)->expr);
        out_ += ')';
        return;
      case StmtKind::If: {
        const auto* i = cast<IfStmt>(&s);
        out_ += "(if ";
        expr(*i->condition);
        newline(indent + 1);
        stmt(*i->then_branch, indent + 1);
        if (i->else_branch != nullptr) {
          newline(indent + 1);
          stmt(*i->else_branch, indent + 1);
        }
        out_ += ')';
        return;
      }
      case StmtKind::While: {
        const auto* w = cast<WhileStmt>(&s);
        out_ += "(while ";
        expr(*w->condition);
        newline(indent + 1);
        stmt(*w->body, indent + 1);
        out_ += ')';
        return;
      }
      case StmtKind::Break:
        out_ += "(break)";
        return;
      case StmtKind::Continue:
        out_ += "(continue)";
        return;
      case StmtKind::Return: {
        const auto* r = cast<ReturnStmt>(&s);
        out_ += "(return";
        if (r->value != nullptr) {
          out_ += ' ';
          expr(*r->value);
        }
        out_ += ')';
        return;
      }
    }
  }

  void function(const FnDecl& fn) {
    out_ += "(fn ";
    out_ += fn.name;
    out_ += " (";
    bool first = true;
    for (const Param& param : fn.params) {
      if (!first) {
        out_ += ", ";
      }
      first = false;
      out_ += param.name;
      out_ += ": ";
      out_ += type_kind_name(param.type.kind);
    }
    out_ += ") -> ";
    out_ += type_kind_name(fn.return_type.kind);
    newline(1);
    stmt(*fn.body, 1);
    out_ += ")\n";
  }

 private:
  void newline(int indent) {
    out_ += '\n';
    out_.append(static_cast<std::size_t>(indent) * 2, ' ');
  }

  std::string& out_;
};

}  // namespace

std::string print_expr(const Expr& expr) {
  std::string out;
  Printer(out).expr(expr);
  return out;
}

std::string print_module(const Module& module) {
  std::string out;
  Printer printer(out);
  for (const FnDecl* fn : module.functions) {
    printer.function(*fn);
  }
  return out;
}

}  // namespace kestrel
