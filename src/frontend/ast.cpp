#include "frontend/ast.h"

namespace kestrel {

std::string_view type_kind_name(TypeKind kind) noexcept {
  switch (kind) {
    case TypeKind::Void:
      return "void";
    case TypeKind::Int:
      return "int";
    case TypeKind::Float:
      return "float";
    case TypeKind::Bool:
      return "bool";
  }
  return "<invalid type>";
}

std::string_view unary_op_spelling(UnaryOp op) noexcept {
  switch (op) {
    case UnaryOp::Neg:
      return "-";
    case UnaryOp::Not:
      return "!";
    case UnaryOp::BitNot:
      return "~";
  }
  return "<invalid unary op>";
}

std::string_view binary_op_spelling(BinaryOp op) noexcept {
  switch (op) {
    case BinaryOp::Add:
      return "+";
    case BinaryOp::Sub:
      return "-";
    case BinaryOp::Mul:
      return "*";
    case BinaryOp::Div:
      return "/";
    case BinaryOp::Rem:
      return "%";
    case BinaryOp::BitAnd:
      return "&";
    case BinaryOp::BitOr:
      return "|";
    case BinaryOp::BitXor:
      return "^";
    case BinaryOp::Shl:
      return "<<";
    case BinaryOp::Shr:
      return ">>";
    case BinaryOp::Eq:
      return "==";
    case BinaryOp::Ne:
      return "!=";
    case BinaryOp::Lt:
      return "<";
    case BinaryOp::Le:
      return "<=";
    case BinaryOp::Gt:
      return ">";
    case BinaryOp::Ge:
      return ">=";
    case BinaryOp::LogicalAnd:
      return "&&";
    case BinaryOp::LogicalOr:
      return "||";
  }
  return "<invalid binary op>";
}

std::string_view assign_op_spelling(AssignOp op) noexcept {
  switch (op) {
    case AssignOp::Assign:
      return "=";
    case AssignOp::Add:
      return "+=";
    case AssignOp::Sub:
      return "-=";
    case AssignOp::Mul:
      return "*=";
    case AssignOp::Div:
      return "/=";
    case AssignOp::Rem:
      return "%=";
  }
  return "<invalid assign op>";
}

}  // namespace kestrel
