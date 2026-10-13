#include "frontend/parser.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "frontend/ast_printer.h"

namespace kestrel {
namespace {

std::vector<std::string> messages(const DiagnosticEngine& diags) {
  std::vector<std::string> out;
  for (const Diagnostic& d : diags.diagnostics()) {
    out.push_back(std::to_string(d.location.line) + ":" + std::to_string(d.location.column) + ": " +
                  d.message);
  }
  return out;
}

std::string expr(std::string_view source) {
  Arena arena;
  DiagnosticEngine diags;
  Parser parser(source, arena, diags);
  const Expr* e = parser.parse_standalone_expression();
  EXPECT_FALSE(diags.has_errors()) << diags.render("test.ks", source);
  return e != nullptr ? print_expr(*e) : "<null>";
}

std::string module(std::string_view source) {
  Arena arena;
  DiagnosticEngine diags;
  Parser parser(source, arena, diags);
  const Module m = parser.parse_module();
  EXPECT_FALSE(diags.has_errors()) << diags.render("test.ks", source);
  return print_module(m);
}

std::vector<std::string> expr_errors(std::string_view source) {
  Arena arena;
  DiagnosticEngine diags;
  Parser parser(source, arena, diags);
  const Expr* e = parser.parse_standalone_expression();
  EXPECT_TRUE(diags.has_errors() || e == nullptr);
  return messages(diags);
}

struct ModuleResult {
  std::string ast;
  std::vector<std::string> errors;
};

ModuleResult module_errors(std::string_view source) {
  Arena arena;
  DiagnosticEngine diags;
  Parser parser(source, arena, diags);
  const Module m = parser.parse_module();
  return {print_module(m), messages(diags)};
}

using Errors = std::vector<std::string>;

TEST(ParserExpr, Literals) {
  EXPECT_EQ(expr("42"), "42");
  EXPECT_EQ(expr("1_000"), "1000");
  EXPECT_EQ(expr("0xFF"), "255");
  EXPECT_EQ(expr("2.5"), "2.5");
  EXPECT_EQ(expr("1e3"), "1000.0");
  EXPECT_EQ(expr("true"), "true");
  EXPECT_EQ(expr("false"), "false");
  EXPECT_EQ(expr("x"), "x");
}

TEST(ParserExpr, IntegerLimits) {
  EXPECT_EQ(expr("9223372036854775807"), "9223372036854775807");
  EXPECT_EQ(expr("-9223372036854775808"), "-9223372036854775808");
  EXPECT_EQ(expr("0x7FFF_FFFF_FFFF_FFFF"), "9223372036854775807");
  EXPECT_EQ(expr_errors("9223372036854775808"),
            Errors{"1:1: integer literal '9223372036854775808' is out of range for type 'int'"});
  EXPECT_EQ(expr_errors("-9223372036854775809"),
            Errors{"1:2: integer literal '9223372036854775809' is out of range for type 'int'"});
  EXPECT_EQ(
      expr_errors("99999999999999999999999"),
      Errors{"1:1: integer literal '99999999999999999999999' is out of range for type 'int'"});
}

TEST(ParserExpr, FloatOutOfRange) {
  EXPECT_EQ(expr_errors("1e999"),
            Errors{"1:1: float literal '1e999' is out of range for type 'float'"});
  EXPECT_EQ(expr("1e-999"), "0.0");
}

TEST(ParserExpr, Precedence) {
  EXPECT_EQ(expr("1 + 2 * 3"), "(+ 1 (* 2 3))");
  EXPECT_EQ(expr("(1 + 2) * 3"), "(* (+ 1 2) 3)");
  EXPECT_EQ(expr("a || b && c"), "(|| a (&& b c))");
  EXPECT_EQ(expr("a == b && c < d"), "(&& (== a b) (< c d))");
  EXPECT_EQ(expr("a & b == c"), "(== (& a b) c)");
  EXPECT_EQ(expr("a | b ^ c & d"), "(| a (^ b (& c d)))");
  EXPECT_EQ(expr("1 << 2 + 3"), "(<< 1 (+ 2 3))");
  EXPECT_EQ(expr("a % b * c / d"), "(/ (* (% a b) c) d)");
}

TEST(ParserExpr, LeftAssociativity) {
  EXPECT_EQ(expr("a - b - c"), "(- (- a b) c)");
  EXPECT_EQ(expr("a / b / c"), "(/ (/ a b) c)");
  EXPECT_EQ(expr("a || b || c"), "(|| (|| a b) c)");
}

TEST(ParserExpr, Unary) {
  EXPECT_EQ(expr("-x"), "(- x)");
  EXPECT_EQ(expr("!a && b"), "(&& (! a) b)");
  EXPECT_EQ(expr("~x & y"), "(& (~ x) y)");
  EXPECT_EQ(expr("--x"), "(- (- x))");
  EXPECT_EQ(expr("-5 * 2"), "(* -5 2)");
  EXPECT_EQ(expr("-(5)"), "(- 5)");
  EXPECT_EQ(expr("- -5"), "(- -5)");
  EXPECT_EQ(expr("-2.5"), "(- 2.5)");
}

TEST(ParserExpr, Calls) {
  EXPECT_EQ(expr("f()"), "(call f)");
  EXPECT_EQ(expr("f(1, x + 2)"), "(call f 1 (+ x 2))");
  EXPECT_EQ(expr("f(g(1),)"), "(call f (call g 1))");
  EXPECT_EQ(expr("f(1) + g(2)"), "(+ (call f 1) (call g 2))");
}

TEST(ParserExpr, ComparisonsCannotBeChained) {
  EXPECT_EQ(expr_errors("a < b < c"),
            Errors{"1:7: comparison operators cannot be chained; use '&&' to combine comparisons"});
  EXPECT_EQ(expr_errors("a == b != c"),
            Errors{"1:8: comparison operators cannot be chained; use '&&' to combine comparisons"});
  EXPECT_EQ(expr("(a < b) == c"), "(== (< a b) c)");
}

TEST(ParserExpr, Errors) {
  EXPECT_EQ(expr_errors(""), Errors{"1:1: expected expression, found end of file"});
  EXPECT_EQ(expr_errors("1 +"), Errors{"1:4: expected expression, found end of file"});
  EXPECT_EQ(expr_errors("(1 + 2"),
            Errors{"1:7: expected ')' to close parenthesized expression, found end of file"});
  EXPECT_EQ(expr_errors("f(1 2)"),
            Errors{"1:5: expected ')' after call arguments, found integer literal '2'"});
  EXPECT_EQ(expr_errors("1 2"), Errors{"1:3: expected end of input, found integer literal '2'"});
}

TEST(ParserExpr, LexerErrorDoesNotCascade) {
  EXPECT_EQ(expr_errors("1 + @"), Errors{"1:5: unexpected character '@'"});
}

TEST(ParserExpr, DeepNestingIsRejectedWithoutCrash) {
  const std::string deep = std::string(100000, '(') + "1" + std::string(100000, ')');
  const Errors errors = expr_errors(deep);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0], "1:257: nesting is too deep (limit is 256)");

  const std::string unary(100000, '-');
  EXPECT_EQ(expr_errors(unary + "x").size(), 1u);

  const std::string ok = std::string(200, '(') + "1" + std::string(200, ')');
  EXPECT_EQ(expr(ok), "1");
}

TEST(ParserModule, Fibonacci) {
  EXPECT_EQ(module(R"(
fn fib(n: int) -> int {
  if n < 2 {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}
)"),
            R"((fn fib (n: int) -> int
  (block
    (if (< n 2)
      (block
        (return n)))
    (return (+ (call fib (- n 1)) (call fib (- n 2))))))
)");
}

TEST(ParserModule, FunctionSignatures) {
  EXPECT_EQ(module("fn f() {}"), "(fn f () -> void\n  (block))\n");
  EXPECT_EQ(module("fn g(a: int, b: float, c: bool,) -> bool { return c; }"),
            "(fn g (a: int, b: float, c: bool) -> bool\n  (block\n    (return c)))\n");
}

TEST(ParserModule, Statements) {
  EXPECT_EQ(module(R"(
fn main() {
  let a = 1;
  var b: float = 2.0;
  b += 1.5;
  b = b * 2.0;
  print(a);
  {
    ;
  }
  while true {
    if a > 0 { break; } else if a < 0 { continue; } else { return; }
  }
}
)"),
            R"((fn main () -> void
  (block
    (let a 1)
    (var b: float 2.0)
    (+= b 1.5)
    (= b (* b 2.0))
    (expr (call print a))
    (block)
    (while true
      (block
        (if (> a 0)
          (block
            (break))
          (if (< a 0)
            (block
              (continue))
            (block
              (return))))))))
)");
}

TEST(ParserModule, AllCompoundAssignments) {
  EXPECT_EQ(module("fn f() { x = 1; x += 1; x -= 1; x *= 1; x /= 1; x %= 1; }"),
            "(fn f () -> void\n  (block\n    (= x 1)\n    (+= x 1)\n    (-= x 1)\n    (*= x 1)\n"
            "    (/= x 1)\n    (%= x 1)))\n");
}

TEST(ParserModule, InvalidAssignmentTarget) {
  const auto result = module_errors("fn f() { f() = 1; a + b = 2; }");
  EXPECT_EQ(result.errors,
            (Errors{"1:14: invalid assignment target; only variables can be assigned",
                    "1:25: invalid assignment target; only variables can be assigned"}));
}

TEST(ParserModule, RecoversAtStatementBoundaries) {
  const auto result = module_errors(R"(fn main() {
  let = 1;
  let x = 1 +;
  let y = 2;
  return y
}

fn other() -> int { return 3; }
)");
  EXPECT_EQ(result.errors, (Errors{"2:7: expected variable name after 'let', found '='",
                                   "3:14: expected expression, found ';'",
                                   "6:1: expected ';' after return value, found '}'"}));
  EXPECT_EQ(result.ast,
            "(fn main () -> void\n  (block\n    (let y 2)))\n"
            "(fn other () -> int\n  (block\n    (return 3)))\n");
}

TEST(ParserModule, RecoversAtTopLevel) {
  const auto result = module_errors(R"(let x = 1;
fn (a: int) {}
fn ok() {}
fn bad(a int) {}
fn also_ok() {}
)");
  EXPECT_EQ(result.errors, (Errors{"1:1: expected function declaration, found 'let'",
                                   "2:4: expected function name after 'fn', found '('",
                                   "4:10: expected ':' after parameter name, found 'int'"}));
  EXPECT_EQ(result.ast, "(fn ok () -> void\n  (block))\n(fn also_ok () -> void\n  (block))\n");
}

TEST(ParserModule, MissingClosingBraceBeforeNextFunction) {
  const auto result = module_errors("fn a() {\n  let x = 1;\nfn b() {}\n");
  EXPECT_EQ(result.errors, Errors{"3:1: expected '}' to close block, found 'fn'"});
  EXPECT_EQ(result.ast, "(fn b () -> void\n  (block))\n");
}

TEST(ParserModule, ControlFlowErrors) {
  EXPECT_EQ(module_errors("fn f() { if x return; }").errors,
            Errors{"1:15: expected '{' after 'if' condition, found 'return'"});
  EXPECT_EQ(module_errors("fn f() { if x {} else return; }").errors,
            Errors{"1:23: expected '{' or 'if' after 'else', found 'return'"});
  EXPECT_EQ(module_errors("fn f() { while x break; }").errors,
            Errors{"1:18: expected '{' after 'while' condition, found 'break'"});
  EXPECT_EQ(module_errors("fn f() -> string {}").errors,
            Errors{"1:11: expected type, found identifier 'string'"});
}

TEST(ParserModule, DeeplyNestedBlocksRecover) {
  const std::string source =
      "fn f() " + std::string(1000, '{') + std::string(1000, '}') + "\nfn g() {}";
  const auto result = module_errors(source);
  ASSERT_EQ(result.errors.size(), 1u);
  EXPECT_EQ(result.errors[0], "1:264: nesting is too deep (limit is 256)");
  EXPECT_NE(result.ast.find("(fn g"), std::string::npos);
}

}  // namespace
}  // namespace kestrel
