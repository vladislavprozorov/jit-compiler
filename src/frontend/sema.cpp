#include "frontend/sema.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel {

namespace {

using MaybeType = std::optional<TypeKind>;

std::string quoted(TypeKind type) {
  return "'" + std::string(type_kind_name(type)) + "'";
}

bool is_numeric(TypeKind type) noexcept {
  return type == TypeKind::Int || type == TypeKind::Float;
}

std::optional<BinaryOp> compound_to_binary(AssignOp op) noexcept {
  switch (op) {
    case AssignOp::Assign:
      return std::nullopt;
    case AssignOp::Add:
      return BinaryOp::Add;
    case AssignOp::Sub:
      return BinaryOp::Sub;
    case AssignOp::Mul:
      return BinaryOp::Mul;
    case AssignOp::Div:
      return BinaryOp::Div;
    case AssignOp::Rem:
      return BinaryOp::Rem;
  }
  return std::nullopt;
}

MaybeType binary_result_type(BinaryOp op, TypeKind lhs, TypeKind rhs) noexcept {
  if (lhs != rhs) {
    return std::nullopt;
  }
  switch (op) {
    case BinaryOp::Add:
    case BinaryOp::Sub:
    case BinaryOp::Mul:
    case BinaryOp::Div:
      return is_numeric(lhs) ? MaybeType(lhs) : std::nullopt;
    case BinaryOp::Rem:
    case BinaryOp::BitAnd:
    case BinaryOp::BitOr:
    case BinaryOp::BitXor:
    case BinaryOp::Shl:
    case BinaryOp::Shr:
      return lhs == TypeKind::Int ? MaybeType(TypeKind::Int) : std::nullopt;
    case BinaryOp::Lt:
    case BinaryOp::Le:
    case BinaryOp::Gt:
    case BinaryOp::Ge:
      return is_numeric(lhs) ? MaybeType(TypeKind::Bool) : std::nullopt;
    case BinaryOp::Eq:
    case BinaryOp::Ne:
      return lhs != TypeKind::Void ? MaybeType(TypeKind::Bool) : std::nullopt;
    case BinaryOp::LogicalAnd:
    case BinaryOp::LogicalOr:
      return lhs == TypeKind::Bool ? MaybeType(TypeKind::Bool) : std::nullopt;
  }
  return std::nullopt;
}

MaybeType unary_result_type(UnaryOp op, TypeKind operand) noexcept {
  switch (op) {
    case UnaryOp::Neg:
      return is_numeric(operand) ? MaybeType(operand) : std::nullopt;
    case UnaryOp::Not:
      return operand == TypeKind::Bool ? MaybeType(TypeKind::Bool) : std::nullopt;
    case UnaryOp::BitNot:
      return operand == TypeKind::Int ? MaybeType(TypeKind::Int) : std::nullopt;
  }
  return std::nullopt;
}

struct BuiltinSignature {
  Builtin builtin;
  TypeKind param;
  TypeKind result;
};

struct BuiltinOverloads {
  std::string_view name;
  std::vector<BuiltinSignature> signatures;
};

const std::vector<BuiltinOverloads>& builtin_table() {
  static const std::vector<BuiltinOverloads> table = {
      {"print",
       {{Builtin::PrintInt, TypeKind::Int, TypeKind::Void},
        {Builtin::PrintFloat, TypeKind::Float, TypeKind::Void},
        {Builtin::PrintBool, TypeKind::Bool, TypeKind::Void}}},
      {"to_float", {{Builtin::ToFloat, TypeKind::Int, TypeKind::Float}}},
      {"to_int", {{Builtin::ToInt, TypeKind::Float, TypeKind::Int}}},
      {"sqrt", {{Builtin::Sqrt, TypeKind::Float, TypeKind::Float}}},
  };
  return table;
}

const BuiltinOverloads* find_builtin(std::string_view name) {
  for (const BuiltinOverloads& overloads : builtin_table()) {
    if (overloads.name == name) {
      return &overloads;
    }
  }
  return nullptr;
}

struct Flow {
  bool returns = false;
  bool terminates = false;

  static constexpr Flow falls_through() noexcept { return {false, false}; }
  static constexpr Flow jumps() noexcept { return {false, true}; }
  static constexpr Flow returning() noexcept { return {true, true}; }
};

struct Local {
  std::string_view name;
  SourceLocation location;
  MaybeType type;
  SlotIndex slot;
  bool is_mutable;
  bool used;
  bool is_param;
};

struct Loop {
  bool has_break = false;
};

class Analyzer {
 public:
  explicit Analyzer(DiagnosticEngine& diags) noexcept : diags_(diags) {}

  void run(Module& module) {
    declare_functions(module);
    for (FnDecl* fn : module.functions) {
      analyze_function(*fn);
    }
  }

 private:
  void declare_functions(Module& module) {
    std::uint32_t index = 0;
    for (FnDecl* fn : module.functions) {
      fn->index = index++;
      if (find_builtin(fn->name) != nullptr) {
        diags_.error(fn->location, "'" + std::string(fn->name) +
                                       "' is a builtin function and cannot be redefined");
        continue;
      }
      const auto [it, inserted] = functions_.try_emplace(fn->name, fn);
      if (!inserted) {
        diags_.error(fn->location, "redefinition of function '" + std::string(fn->name) + "'");
        diags_.report(Severity::Note, it->second->location, "previous definition is here");
      }
    }
    check_main();
  }

  void check_main() {
    const auto it = functions_.find("main");
    if (it == functions_.end()) {
      return;
    }
    const FnDecl& main = *it->second;
    if (!main.params.empty()) {
      diags_.error(main.location, "'main' must not take parameters");
    }
    if (main.return_type.kind != TypeKind::Int && main.return_type.kind != TypeKind::Void) {
      diags_.error(main.return_type.location, "'main' must return 'int' or 'void'");
    }
  }

  void analyze_function(FnDecl& fn) {
    current_fn_ = &fn;
    locals_.clear();
    scope_starts_.clear();
    next_slot_ = 0;
    max_slots_ = 0;

    push_scope();
    for (const Param& param : fn.params) {
      declare_local(param.name, param.location, param.type.kind, false, true);
    }
    const Flow flow = analyze_block_contents(*fn.body);
    pop_scope();

    fn.local_count = max_slots_;
    if (fn.return_type.kind != TypeKind::Void && !flow.returns) {
      diags_.error(fn.location,
                   "function '" + std::string(fn.name) + "' does not return a value on all paths");
    }
    current_fn_ = nullptr;
  }

  void push_scope() { scope_starts_.push_back(locals_.size()); }

  void pop_scope() {
    const std::size_t start = scope_starts_.back();
    scope_starts_.pop_back();
    for (std::size_t i = start; i < locals_.size(); ++i) {
      const Local& local = locals_[i];
      if (!local.used && !local.name.starts_with('_')) {
        diags_.report(Severity::Warning, local.location,
                      std::string(local.is_param ? "unused parameter '" : "unused variable '") +
                          std::string(local.name) + "'");
      }
    }
    locals_.resize(start);
    next_slot_ = locals_.empty() ? 0 : locals_.back().slot + 1;
  }

  SlotIndex declare_local(std::string_view name, SourceLocation location, MaybeType type,
                          bool is_mutable, bool is_param) {
    const std::size_t scope_start = scope_starts_.back();
    for (std::size_t i = scope_start; i < locals_.size(); ++i) {
      if (locals_[i].name == name) {
        diags_.error(location, "redefinition of '" + std::string(name) + "'");
        diags_.report(Severity::Note, locals_[i].location, "previous definition is here");
        break;
      }
    }
    const SlotIndex slot = next_slot_++;
    max_slots_ = std::max(max_slots_, next_slot_);
    locals_.push_back({name, location, type, slot, is_mutable, false, is_param});
    return slot;
  }

  Local* lookup(std::string_view name) noexcept {
    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
      if (it->name == name) {
        return &*it;
      }
    }
    return nullptr;
  }

  Flow analyze_block_contents(BlockStmt& block) {
    Flow flow = Flow::falls_through();
    bool reported_unreachable = false;
    for (Stmt* stmt : block.statements) {
      if (flow.terminates && !reported_unreachable) {
        diags_.report(Severity::Warning, stmt->location, "unreachable code");
        reported_unreachable = true;
      }
      const Flow stmt_flow = analyze_stmt(*stmt);
      if (!flow.terminates) {
        flow = stmt_flow;
      }
    }
    return flow;
  }

  Flow analyze_stmt(Stmt& stmt) {
    switch (stmt.kind) {
      case StmtKind::Block: {
        push_scope();
        const Flow flow = analyze_block_contents(*cast<BlockStmt>(&stmt));
        pop_scope();
        return flow;
      }
      case StmtKind::Let:
        analyze_let(*cast<LetStmt>(&stmt));
        return Flow::falls_through();
      case StmtKind::Assign:
        analyze_assign(*cast<AssignStmt>(&stmt));
        return Flow::falls_through();
      case StmtKind::Expr:
        analyze_expr_stmt(*cast<ExprStmt>(&stmt));
        return Flow::falls_through();
      case StmtKind::If:
        return analyze_if(*cast<IfStmt>(&stmt));
      case StmtKind::While:
        return analyze_while(*cast<WhileStmt>(&stmt));
      case StmtKind::Break:
        if (loops_.empty()) {
          diags_.error(stmt.location, "'break' outside of a loop");
        } else {
          loops_.back().has_break = true;
        }
        return Flow::jumps();
      case StmtKind::Continue:
        if (loops_.empty()) {
          diags_.error(stmt.location, "'continue' outside of a loop");
        }
        return Flow::jumps();
      case StmtKind::Return:
        analyze_return(*cast<ReturnStmt>(&stmt));
        return Flow::returning();
    }
    return Flow::falls_through();
  }

  void analyze_let(LetStmt& let) {
    const MaybeType init = analyze_expr(*let.init);
    MaybeType type = let.type ? MaybeType(let.type->kind) : init;
    if (init) {
      if (*init == TypeKind::Void) {
        diags_.error(let.init->location,
                     "variable '" + std::string(let.name) + "' cannot be initialized with 'void'");
        if (!let.type) {
          type = std::nullopt;
        }
      } else if (let.type && let.type->kind != *init) {
        diags_.error(let.init->location, "cannot initialize variable '" + std::string(let.name) +
                                             "' of type " + quoted(let.type->kind) +
                                             " with a value of type " + quoted(*init));
      }
    }
    let.slot = declare_local(let.name, let.location, type, let.is_mutable, false);
  }

  void analyze_assign(AssignStmt& assign) {
    const MaybeType value = analyze_expr(*assign.value);
    Local* local = lookup(assign.target->name);
    if (local == nullptr) {
      diags_.error(assign.target->location,
                   "use of undeclared variable '" + std::string(assign.target->name) + "'");
      return;
    }
    assign.target->slot = local->slot;
    if (local->type) {
      assign.target->type = *local->type;
    }
    if (!local->is_mutable) {
      diags_.error(assign.location,
                   "cannot assign to immutable " +
                       std::string(local->is_param ? "parameter '" : "variable '") +
                       std::string(local->name) + "'");
      if (!local->is_param) {
        diags_.report(Severity::Note, local->location,
                      "declared here; use 'var' to make it mutable");
      }
    }
    if (assign.op != AssignOp::Assign) {
      local->used = true;
    }
    if (!value || !local->type) {
      return;
    }

    const TypeKind target = *local->type;
    if (const std::optional<BinaryOp> op = compound_to_binary(assign.op)) {
      if (!binary_result_type(*op, target, *value)) {
        diags_.error(assign.location, "invalid operands to compound assignment '" +
                                          std::string(assign_op_spelling(assign.op)) +
                                          "': " + quoted(target) + " and " + quoted(*value));
      }
    } else if (target != *value) {
      diags_.error(assign.value->location, "cannot assign a value of type " + quoted(*value) +
                                               " to variable '" + std::string(local->name) +
                                               "' of type " + quoted(target));
    }
  }

  void analyze_expr_stmt(ExprStmt& stmt) {
    const MaybeType type = analyze_expr(*stmt.expr);
    if (type && !isa<CallExpr>(stmt.expr)) {
      diags_.report(Severity::Warning, stmt.expr->location, "expression result unused");
    }
  }

  void check_condition(Expr& condition, std::string_view construct) {
    const MaybeType type = analyze_expr(condition);
    if (type && *type != TypeKind::Bool) {
      diags_.error(condition.location,
                   std::string(construct) + " condition must be 'bool', found " + quoted(*type));
    }
  }

  Flow analyze_if(IfStmt& stmt) {
    check_condition(*stmt.condition, "'if'");
    const Flow then_flow = analyze_stmt(*stmt.then_branch);
    if (stmt.else_branch == nullptr) {
      return Flow::falls_through();
    }
    const Flow else_flow = analyze_stmt(*stmt.else_branch);
    return {then_flow.returns && else_flow.returns, then_flow.terminates && else_flow.terminates};
  }

  Flow analyze_while(WhileStmt& stmt) {
    check_condition(*stmt.condition, "'while'");
    loops_.push_back({});
    analyze_stmt(*stmt.body);
    const Loop loop = loops_.back();
    loops_.pop_back();

    const auto* literal = dyn_cast<BoolLiteralExpr>(stmt.condition);
    const bool infinite = literal != nullptr && literal->value && !loop.has_break;
    return infinite ? Flow::returning() : Flow::falls_through();
  }

  void analyze_return(ReturnStmt& stmt) {
    const TypeKind expected = current_fn_->return_type.kind;
    const std::string fn_name(current_fn_->name);
    if (stmt.value == nullptr) {
      if (expected != TypeKind::Void) {
        diags_.error(stmt.location, "non-void function '" + fn_name +
                                        "' must return a value of type " + quoted(expected));
      }
      return;
    }
    const MaybeType value = analyze_expr(*stmt.value);
    if (expected == TypeKind::Void) {
      diags_.error(stmt.value->location, "void function '" + fn_name + "' cannot return a value");
      return;
    }
    if (value && *value != expected) {
      diags_.error(stmt.value->location, "function '" + fn_name + "' returns " + quoted(expected) +
                                             ", but the value has type " + quoted(*value));
    }
  }

  MaybeType analyze_expr(Expr& expr) {
    const MaybeType type = analyze_expr_impl(expr);
    if (type) {
      expr.type = *type;
    }
    return type;
  }

  MaybeType analyze_expr_impl(Expr& expr) {
    switch (expr.kind) {
      case ExprKind::IntLiteral:
        return TypeKind::Int;
      case ExprKind::FloatLiteral:
        return TypeKind::Float;
      case ExprKind::BoolLiteral:
        return TypeKind::Bool;
      case ExprKind::Name:
        return analyze_name(*cast<NameExpr>(&expr));
      case ExprKind::Unary:
        return analyze_unary(*cast<UnaryExpr>(&expr));
      case ExprKind::Binary:
        return analyze_binary(*cast<BinaryExpr>(&expr));
      case ExprKind::Call:
        return analyze_call(*cast<CallExpr>(&expr));
    }
    return std::nullopt;
  }

  MaybeType analyze_name(NameExpr& name) {
    Local* local = lookup(name.name);
    if (local == nullptr) {
      diags_.error(name.location, "use of undeclared variable '" + std::string(name.name) + "'");
      return std::nullopt;
    }
    local->used = true;
    name.slot = local->slot;
    return local->type;
  }

  MaybeType analyze_unary(UnaryExpr& unary) {
    const MaybeType operand = analyze_expr(*unary.operand);
    if (!operand) {
      return std::nullopt;
    }
    const MaybeType result = unary_result_type(unary.op, *operand);
    if (!result) {
      diags_.error(unary.location, "invalid operand to unary '" +
                                       std::string(unary_op_spelling(unary.op)) +
                                       "': " + quoted(*operand));
    }
    return result;
  }

  MaybeType analyze_binary(BinaryExpr& binary) {
    const MaybeType lhs = analyze_expr(*binary.lhs);
    const MaybeType rhs = analyze_expr(*binary.rhs);
    if (!lhs || !rhs) {
      return std::nullopt;
    }
    const MaybeType result = binary_result_type(binary.op, *lhs, *rhs);
    if (!result) {
      diags_.error(binary.location, "invalid operands to binary '" +
                                        std::string(binary_op_spelling(binary.op)) +
                                        "': " + quoted(*lhs) + " and " + quoted(*rhs));
    }
    return result;
  }

  MaybeType analyze_call(CallExpr& call) {
    std::vector<MaybeType> args;
    args.reserve(call.args.size());
    bool args_ok = true;
    for (Expr* arg : call.args) {
      args.push_back(analyze_expr(*arg));
      args_ok = args_ok && args.back().has_value();
    }

    if (const BuiltinOverloads* overloads = find_builtin(call.callee)) {
      return resolve_builtin(call, *overloads, args, args_ok);
    }

    const auto it = functions_.find(call.callee);
    if (it == functions_.end()) {
      diags_.error(call.location, "call to undeclared function '" + std::string(call.callee) + "'");
      return std::nullopt;
    }
    const FnDecl& fn = *it->second;
    call.function = &fn;

    if (fn.params.size() != args.size()) {
      diags_.error(call.location, "function '" + std::string(fn.name) + "' expects " +
                                      plural(fn.params.size(), "argument") + ", but " +
                                      std::to_string(args.size()) + " were given");
      return fn.return_type.kind;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
      const TypeKind expected = fn.params[i].type.kind;
      if (args[i] && *args[i] != expected) {
        diags_.error(call.args[i]->location, "argument " + std::to_string(i + 1) + " of '" +
                                                 std::string(fn.name) + "' must be " +
                                                 quoted(expected) + ", found " + quoted(*args[i]));
      }
    }
    return fn.return_type.kind;
  }

  MaybeType resolve_builtin(CallExpr& call, const BuiltinOverloads& overloads,
                            const std::vector<MaybeType>& args, bool args_ok) {
    if (args.size() != 1) {
      diags_.error(call.location, "builtin '" + std::string(overloads.name) +
                                      "' expects 1 argument, but " + std::to_string(args.size()) +
                                      " were given");
      return std::nullopt;
    }
    if (!args_ok) {
      return std::nullopt;
    }
    for (const BuiltinSignature& sig : overloads.signatures) {
      if (sig.param == *args[0]) {
        call.builtin = sig.builtin;
        return sig.result;
      }
    }
    diags_.error(call.args[0]->location, "no overload of builtin '" + std::string(overloads.name) +
                                             "' accepts an argument of type " + quoted(*args[0]));
    return std::nullopt;
  }

  static std::string plural(std::size_t count, std::string_view noun) {
    return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
  }

  DiagnosticEngine& diags_;
  std::unordered_map<std::string_view, FnDecl*> functions_;
  FnDecl* current_fn_ = nullptr;
  std::vector<Local> locals_;
  std::vector<std::size_t> scope_starts_;
  std::vector<Loop> loops_;
  SlotIndex next_slot_ = 0;
  SlotIndex max_slots_ = 0;
};

}  // namespace

bool analyze(Module& module, DiagnosticEngine& diags) {
  const std::size_t errors_before = diags.error_count();
  Analyzer(diags).run(module);
  return diags.error_count() == errors_before;
}

}  // namespace kestrel
