#include "frontend/sema.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "frontend/parser.h"

namespace kestrel {
namespace {

using Diags = std::vector<std::string>;

struct Analyzed {
  Arena arena;
  DiagnosticEngine diags;
  Module module;
  bool ok = false;

  explicit Analyzed(std::string_view source) {
    Parser parser(source, arena, diags);
    module = parser.parse_module();
    EXPECT_FALSE(diags.has_errors()) << "parse failed:\n" << diags.render("test.ks", source);
    ok = analyze(module, diags);
  }

  [[nodiscard]] Diags messages() const {
    Diags out;
    for (const Diagnostic& d : diags.diagnostics()) {
      out.push_back(std::to_string(d.location.line) + ":" + std::to_string(d.location.column) +
                    ": " + std::string(severity_name(d.severity)) + ": " + d.message);
    }
    return out;
  }

  [[nodiscard]] FnDecl& fn(std::size_t index) const { return *module.functions[index]; }
};

Diags check(std::string_view source) {
  return Analyzed(source).messages();
}

TEST(Sema, AcceptsFibonacciAndResolvesCalls) {
  Analyzed a(R"(fn fib(n: int) -> int {
  if n < 2 {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}
fn main() -> int { return fib(30); }
)");
  EXPECT_TRUE(a.ok);
  EXPECT_EQ(a.messages(), Diags{});
  EXPECT_EQ(a.fn(0).index, 0u);
  EXPECT_EQ(a.fn(1).index, 1u);
  EXPECT_EQ(a.fn(0).local_count, 1u);
  EXPECT_EQ(a.fn(1).local_count, 0u);

  const auto* ret = cast<ReturnStmt>(a.fn(0).body->statements[1]);
  const auto* sum = cast<BinaryExpr>(ret->value);
  EXPECT_EQ(sum->type, TypeKind::Int);
  const auto* call = cast<CallExpr>(sum->lhs);
  EXPECT_EQ(call->function, &a.fn(0));
  EXPECT_EQ(call->builtin, Builtin::None);

  const auto* cond = cast<BinaryExpr>(cast<IfStmt>(a.fn(0).body->statements[0])->condition);
  EXPECT_EQ(cond->type, TypeKind::Bool);
  EXPECT_EQ(cast<NameExpr>(cond->lhs)->slot, 0u);
}

TEST(Sema, ForwardAndMutualRecursion) {
  EXPECT_EQ(check(R"(
fn is_even(n: int) -> bool { if n == 0 { return true; } return is_odd(n - 1); }
fn is_odd(n: int) -> bool { if n == 0 { return false; } return is_even(n - 1); }
)"),
            Diags{});
}

TEST(Sema, SlotsAreReusedAcrossSiblingScopes) {
  Analyzed a(R"(fn f(a: int) -> int {
  {
    let x = 1;
    let y = 2;
    print(x + y);
  }
  {
    let z = 3;
    print(z);
  }
  let w = a;
  return w;
}
)");
  ASSERT_TRUE(a.ok);
  const auto& stmts = a.fn(0).body->statements;
  const auto* first = cast<BlockStmt>(stmts[0]);
  const auto* second = cast<BlockStmt>(stmts[1]);
  EXPECT_EQ(cast<LetStmt>(first->statements[0])->slot, 1u);
  EXPECT_EQ(cast<LetStmt>(first->statements[1])->slot, 2u);
  EXPECT_EQ(cast<LetStmt>(second->statements[0])->slot, 1u);
  EXPECT_EQ(cast<LetStmt>(stmts[2])->slot, 1u);
  EXPECT_EQ(a.fn(0).local_count, 3u);
}

TEST(Sema, ShadowingInInnerScopeAndSelfReferenceInInitializer) {
  Analyzed a(R"(fn f() -> int {
  let x = 1;
  {
    let x = x + 1.5 > 0.0;
    print(x);
  }
  return x;
}
)");
  EXPECT_EQ(a.messages(), Diags{"4:15: error: invalid operands to binary '+': 'int' and 'float'"});
}

TEST(Sema, RedefinitionInSameScope) {
  EXPECT_EQ(check("fn f(a: int) { let a = 1; print(a); }"),
            (Diags{"1:20: error: redefinition of 'a'", "1:6: note: previous definition is here",
                   "1:6: warning: unused parameter 'a'"}));
}

TEST(Sema, BinaryOperatorTypeRules) {
  EXPECT_EQ(check(R"(fn f() {
  print(1 + 2);
  print(1.5 * 2.0);
  print(7 % 2);
  print(1 << 3 | 4 & 5 ^ 6);
  print(1 < 2 && 2.0 >= 1.0 || true == false);
  print(1.0 != 2.0);
}
)"),
            Diags{});

  EXPECT_EQ(check(R"(fn f() {
  print(1 + 1.0);
  print(1.0 % 2.0);
  print(true < false);
  print(1 && 2);
  print(1.5 << 1);
  print(true + true);
}
)"),
            (Diags{"2:11: error: invalid operands to binary '+': 'int' and 'float'",
                   "3:13: error: invalid operands to binary '%': 'float' and 'float'",
                   "4:14: error: invalid operands to binary '<': 'bool' and 'bool'",
                   "5:11: error: invalid operands to binary '&&': 'int' and 'int'",
                   "6:13: error: invalid operands to binary '<<': 'float' and 'int'",
                   "7:14: error: invalid operands to binary '+': 'bool' and 'bool'"}));
}

TEST(Sema, UnaryOperatorTypeRules) {
  EXPECT_EQ(check("fn f() { print(-1); print(-1.5); print(!true); print(~1); }"), Diags{});
  EXPECT_EQ(check("fn f() { print(-true); print(!1); print(~1.5); }"),
            (Diags{"1:16: error: invalid operand to unary '-': 'bool'",
                   "1:30: error: invalid operand to unary '!': 'int'",
                   "1:41: error: invalid operand to unary '~': 'float'"}));
}

TEST(Sema, ErrorsDoNotCascade) {
  EXPECT_EQ(check(R"(fn f() -> float {
  let x = y + 1;
  let z = x * 2.0;
  var w = print(1);
  w = 3;
  return z + w;
}
)"),
            (Diags{"2:11: error: use of undeclared variable 'y'",
                   "4:11: error: variable 'w' cannot be initialized with 'void'"}));
}

TEST(Sema, LetTypeAnnotations) {
  EXPECT_EQ(check("fn f() { let a: float = 1.0; let b: int = 2.0; print(a); print(b); }"),
            Diags{"1:43: error: cannot initialize variable 'b' of type 'int' with a value of "
                  "type 'float'"});
}

TEST(Sema, Assignments) {
  EXPECT_EQ(check(R"(fn f(p: int) {
  let x = 1;
  var y = 2;
  x = 3;
  p = 4;
  y = 1.5;
  y += 1;
  y += 1.5;
  var b = true;
  b += true;
  z = 1;
  print(y);
}
)"),
            (Diags{"4:5: error: cannot assign to immutable variable 'x'",
                   "2:7: note: declared here; use 'var' to make it mutable",
                   "5:5: error: cannot assign to immutable parameter 'p'",
                   "6:7: error: cannot assign a value of type 'float' to variable 'y' of type "
                   "'int'",
                   "8:5: error: invalid operands to compound assignment '+=': 'int' and 'float'",
                   "10:5: error: invalid operands to compound assignment '+=': 'bool' and 'bool'",
                   "11:3: error: use of undeclared variable 'z'",
                   "1:6: warning: unused parameter 'p'", "2:7: warning: unused variable 'x'"}));
}

TEST(Sema, Conditions) {
  EXPECT_EQ(check("fn f() { if 1 { } while 1.0 { } }"),
            (Diags{"1:13: error: 'if' condition must be 'bool', found 'int'",
                   "1:25: error: 'while' condition must be 'bool', found 'float'"}));
}

TEST(Sema, Calls) {
  EXPECT_EQ(check(R"(fn add(a: int, b: int) -> int { return a + b; }
fn f() {
  print(add(1));
  print(add(1, 2.0));
  print(missing(1));
}
)"),
            (Diags{"3:9: error: function 'add' expects 2 arguments, but 1 were given",
                   "4:16: error: argument 2 of 'add' must be 'int', found 'float'",
                   "5:9: error: call to undeclared function 'missing'"}));
}

TEST(Sema, Builtins) {
  Analyzed a(R"(fn f() {
  print(1);
  print(1.5);
  print(true);
  print(sqrt(to_float(2)));
  print(to_int(2.5));
}
)");
  EXPECT_EQ(a.messages(), Diags{});
  const auto& stmts = a.fn(0).body->statements;
  auto builtin_at = [&](std::size_t i) {
    return cast<CallExpr>(cast<ExprStmt>(stmts[i])->expr)->builtin;
  };
  EXPECT_EQ(builtin_at(0), Builtin::PrintInt);
  EXPECT_EQ(builtin_at(1), Builtin::PrintFloat);
  EXPECT_EQ(builtin_at(2), Builtin::PrintBool);
  EXPECT_EQ(builtin_at(3), Builtin::PrintFloat);

  EXPECT_EQ(check("fn f() { print(print(1)); to_float(1.5); sqrt(); }"),
            (Diags{"1:16: error: no overload of builtin 'print' accepts an argument of type 'void'",
                   "1:36: error: no overload of builtin 'to_float' accepts an argument of type "
                   "'float'",
                   "1:42: error: builtin 'sqrt' expects 1 argument, but 0 were given"}));
}

TEST(Sema, CannotRedefineBuiltinsOrFunctions) {
  EXPECT_EQ(check("fn print() {}\nfn g() {}\nfn g() {}"),
            (Diags{"1:4: error: 'print' is a builtin function and cannot be redefined",
                   "3:4: error: redefinition of function 'g'",
                   "2:4: note: previous definition is here"}));
}

TEST(Sema, MainSignature) {
  EXPECT_EQ(check("fn main() {}"), Diags{});
  EXPECT_EQ(check("fn main() -> int { return 0; }"), Diags{});
  EXPECT_EQ(check("fn main(_a: int) -> float { return 0.0; }"),
            (Diags{"1:4: error: 'main' must not take parameters",
                   "1:21: error: 'main' must return 'int' or 'void'"}));
}

TEST(Sema, ReturnStatements) {
  EXPECT_EQ(check(R"(fn a() -> int { return; }
fn b() { return 1; }
fn c() -> int { return 1.5; }
fn d() { return; }
)"),
            (Diags{"1:17: error: non-void function 'a' must return a value of type 'int'",
                   "2:17: error: void function 'b' cannot return a value",
                   "3:24: error: function 'c' returns 'int', but the value has type 'float'"}));
}

TEST(Sema, AllPathsReturn) {
  EXPECT_EQ(check(R"(fn ok_if_else(x: bool) -> int { if x { return 1; } else { return 2; } }
fn ok_else_if(x: int) -> int { if x > 0 { return 1; } else if x < 0 { return -1; } else { return 0; } }
fn ok_loop() -> int { while true { } }
fn ok_loop_return() -> int { var i = 0; while true { i += 1; if i > 10 { return i; } } }
fn ok_nested_block() -> int { { return 1; } }
fn bad_if(x: bool) -> int { if x { return 1; } }
fn bad_loop() -> int { while true { break; } }
fn bad_cond_loop(x: bool) -> int { while x { return 1; } }
fn bad_empty() -> int { }
)"),
            (Diags{"6:4: error: function 'bad_if' does not return a value on all paths",
                   "7:4: error: function 'bad_loop' does not return a value on all paths",
                   "8:4: error: function 'bad_cond_loop' does not return a value on all paths",
                   "9:4: error: function 'bad_empty' does not return a value on all paths"}));
}

TEST(Sema, BreakAndContinueOutsideLoop) {
  EXPECT_EQ(check("fn f() { break; }\nfn g() { if true { continue; } }"),
            (Diags{"1:10: error: 'break' outside of a loop",
                   "2:20: error: 'continue' outside of a loop"}));
  EXPECT_EQ(check("fn f() { while true { if true { break; } continue; } }"), Diags{});
}

TEST(Sema, Warnings) {
  Analyzed a(R"(fn f(unused: int, _ignored: int) -> int {
  let x = 1;
  let _y = 2;
  x + 1;
  return 0;
  print(3);
  print(4);
}
)");
  EXPECT_TRUE(a.ok);
  EXPECT_EQ(a.messages(),
            (Diags{"4:5: warning: expression result unused", "6:3: warning: unreachable code",
                   "1:6: warning: unused parameter 'unused'"}));
}

TEST(Sema, UnreachableAfterBreak) {
  EXPECT_EQ(check("fn f() { while true { break; print(1); } }"),
            Diags{"1:30: warning: unreachable code"});
}

TEST(Sema, AssignmentDoesNotCountAsUse) {
  EXPECT_EQ(check("fn f() { var x = 1; x = 2; }"), Diags{"1:14: warning: unused variable 'x'"});
  EXPECT_EQ(check("fn f() { var x = 1; x += 2; }"), Diags{});
}

}  // namespace
}  // namespace kestrel
