#include "frontend/lexer.h"

#include <gtest/gtest.h>

#include <cctype>
#include <ostream>
#include <string>
#include <vector>

namespace kestrel {
namespace {

std::vector<TokenKind> kinds_of(std::string_view source, DiagnosticEngine& diags) {
  std::vector<TokenKind> kinds;
  for (const Token& token : Lexer::tokenize(source, diags)) {
    kinds.push_back(token.kind);
  }
  return kinds;
}

std::vector<TokenKind> kinds_of(std::string_view source) {
  DiagnosticEngine diags;
  auto kinds = kinds_of(source, diags);
  EXPECT_FALSE(diags.has_errors()) << diags.render("test.ks", source);
  return kinds;
}

Token single(std::string_view source, DiagnosticEngine& diags) {
  const auto tokens = Lexer::tokenize(source, diags);
  EXPECT_EQ(tokens.size(), 2u);
  return tokens.front();
}

using enum TokenKind;

}  // namespace

void PrintTo(TokenKind kind, std::ostream* os) { *os << token_kind_name(kind); }

namespace {

TEST(Lexer, EmptyInputYieldsEof) {
  EXPECT_EQ(kinds_of(""), std::vector{Eof});
  EXPECT_EQ(kinds_of("   \n\t\r\n"), std::vector{Eof});
}

TEST(Lexer, KeywordsAndIdentifiers) {
  EXPECT_EQ(kinds_of("fn let var if else while break continue return true false int float bool"),
            (std::vector{KwFn, KwLet, KwVar, KwIf, KwElse, KwWhile, KwBreak, KwContinue, KwReturn,
                         KwTrue, KwFalse, KwInt, KwFloat, KwBool, Eof}));
  EXPECT_EQ(kinds_of("fnx _let let_ _ x1 Int"),
            (std::vector{Identifier, Identifier, Identifier, Identifier, Identifier, Identifier,
                         Eof}));
}

TEST(Lexer, Punctuation) {
  EXPECT_EQ(kinds_of("( ) { } , ; : ->"),
            (std::vector{LParen, RParen, LBrace, RBrace, Comma, Semicolon, Colon, Arrow, Eof}));
}

TEST(Lexer, OperatorsUseMaximalMunch) {
  EXPECT_EQ(kinds_of("+ - * / % & | ^ ~ << >> ! && || = == != < <= > >= += -= *= /= %="),
            (std::vector{Plus,      Minus,      Star,       Slash,        Percent,
                         Amp,       Pipe,       Caret,      Tilde,        LessLess,
                         GreaterGreater,        Bang,       AmpAmp,       PipePipe,
                         Equal,     EqualEqual, BangEqual,  Less,         LessEqual,
                         Greater,   GreaterEqual,           PlusEqual,    MinusEqual,
                         StarEqual, SlashEqual, PercentEqual, Eof}));
  EXPECT_EQ(kinds_of("a<=b"), (std::vector{Identifier, LessEqual, Identifier, Eof}));
  EXPECT_EQ(kinds_of("x--1"), (std::vector{Identifier, Minus, Minus, IntLiteral, Eof}));
}

TEST(Lexer, IntegerLiterals) {
  EXPECT_EQ(kinds_of("0 42 1_000_000 0x1F 0XdeadBEEF 0xFF_FF"),
            (std::vector{IntLiteral, IntLiteral, IntLiteral, IntLiteral, IntLiteral, IntLiteral,
                         Eof}));
}

TEST(Lexer, FloatLiterals) {
  EXPECT_EQ(kinds_of("1.5 0.0 3e10 2.5E-3 1e+2 1_0.2_5"),
            (std::vector{FloatLiteral, FloatLiteral, FloatLiteral, FloatLiteral, FloatLiteral,
                         FloatLiteral, Eof}));
}

TEST(Lexer, DotWithoutDigitIsNotPartOfNumber) {
  DiagnosticEngine diags;
  EXPECT_EQ(kinds_of("1.", diags), (std::vector{IntLiteral, Error, Eof}));
  EXPECT_EQ(diags.error_count(), 1u);
}

TEST(Lexer, TokenTextAndLocation) {
  DiagnosticEngine diags;
  const auto tokens = Lexer::tokenize("let x =\n  0x10;", diags);
  ASSERT_EQ(tokens.size(), 6u);
  EXPECT_EQ(tokens[0].text, "let");
  EXPECT_EQ(tokens[1].text, "x");
  EXPECT_EQ(tokens[3].text, "0x10");
  EXPECT_EQ(tokens[3].location, (SourceLocation{.offset = 10, .line = 2, .column = 3}));
  EXPECT_EQ(tokens[4].location, (SourceLocation{.offset = 14, .line = 2, .column = 7}));
  EXPECT_EQ(tokens[5].location, (SourceLocation{.offset = 15, .line = 2, .column = 8}));
}

TEST(Lexer, Comments) {
  EXPECT_EQ(kinds_of("a // line comment\nb"), (std::vector{Identifier, Identifier, Eof}));
  EXPECT_EQ(kinds_of("a /* block */ b"), (std::vector{Identifier, Identifier, Eof}));
  EXPECT_EQ(kinds_of("a /* outer /* inner */ still */ b"),
            (std::vector{Identifier, Identifier, Eof}));
  EXPECT_EQ(kinds_of("a / b"), (std::vector{Identifier, Slash, Identifier, Eof}));
  EXPECT_EQ(kinds_of("// only comment"), std::vector{Eof});
}

TEST(Lexer, UnterminatedBlockComment) {
  DiagnosticEngine diags;
  EXPECT_EQ(kinds_of("a /* /* */", diags), (std::vector{Identifier, Eof}));
  ASSERT_EQ(diags.error_count(), 1u);
  EXPECT_EQ(diags.diagnostics()[0].message, "unterminated block comment");
  EXPECT_EQ(diags.diagnostics()[0].location.column, 3u);
}

struct BadLiteral {
  std::string_view source;
  std::string_view message;

  friend void PrintTo(const BadLiteral& lit, std::ostream* os) { *os << '"' << lit.source << '"'; }
};

class LexerBadLiteral : public testing::TestWithParam<BadLiteral> {};

TEST_P(LexerBadLiteral, ReportsSingleErrorToken) {
  DiagnosticEngine diags;
  const Token token = single(GetParam().source, diags);
  EXPECT_EQ(token.kind, Error);
  EXPECT_EQ(token.text, GetParam().source);
  ASSERT_EQ(diags.error_count(), 1u);
  EXPECT_EQ(diags.diagnostics()[0].message, GetParam().message);
}

INSTANTIATE_TEST_SUITE_P(
    Literals, LexerBadLiteral,
    testing::Values(BadLiteral{"123abc", "invalid suffix on numeric literal"},
                    BadLiteral{"1.5f", "invalid suffix on numeric literal"},
                    BadLiteral{"1__0", "misplaced digit separator '_'"},
                    BadLiteral{"1_", "misplaced digit separator '_'"},
                    BadLiteral{"1.5_", "misplaced digit separator '_'"},
                    BadLiteral{"0x", "malformed hexadecimal literal"},
                    BadLiteral{"0x_1", "malformed hexadecimal literal"},
                    BadLiteral{"0xFG", "invalid digit in hexadecimal literal"},
                    BadLiteral{"1e", "expected digits in exponent"},
                    BadLiteral{"1e+", "expected digits in exponent"}),
    [](const testing::TestParamInfo<BadLiteral>& info) {
      std::string name;
      for (const char c : info.param.source) {
        name += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
      }
      return name + "_" + std::to_string(info.index);
    });

TEST(Lexer, UnexpectedCharacters) {
  DiagnosticEngine diags;
  EXPECT_EQ(kinds_of("a @ b", diags), (std::vector{Identifier, Error, Identifier, Eof}));
  ASSERT_EQ(diags.error_count(), 1u);
  EXPECT_EQ(diags.diagnostics()[0].message, "unexpected character '@'");
}

TEST(Lexer, NonAsciiSequenceIsOneErrorToken) {
  DiagnosticEngine diags;
  const auto tokens = Lexer::tokenize("x \xD0\xBF y", diags);
  ASSERT_EQ(tokens.size(), 4u);
  EXPECT_EQ(tokens[1].kind, Error);
  EXPECT_EQ(tokens[1].text, "\xD0\xBF");
  EXPECT_EQ(diags.diagnostics()[0].message, "unexpected byte 0xD0");
}

TEST(Lexer, TokenKindNames) {
  EXPECT_EQ(token_kind_name(Arrow), "'->'");
  EXPECT_EQ(token_kind_name(Identifier), "identifier");
  EXPECT_EQ(token_kind_name(Eof), "end of file");
}

}  // namespace
}  // namespace kestrel
