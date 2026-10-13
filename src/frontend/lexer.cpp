#include "frontend/lexer.h"

#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace kestrel {

namespace {

constexpr bool is_alpha(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

constexpr bool is_dec_digit(char c) noexcept {
  return c >= '0' && c <= '9';
}

constexpr bool is_hex_digit(char c) noexcept {
  return is_dec_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr bool is_ident_char(char c) noexcept {
  return is_alpha(c) || is_dec_digit(c);
}

struct Keyword {
  std::string_view spelling;
  TokenKind kind;
};

constexpr std::array kKeywords = {
    Keyword{"fn", TokenKind::KwFn},         Keyword{"let", TokenKind::KwLet},
    Keyword{"var", TokenKind::KwVar},       Keyword{"if", TokenKind::KwIf},
    Keyword{"else", TokenKind::KwElse},     Keyword{"while", TokenKind::KwWhile},
    Keyword{"break", TokenKind::KwBreak},   Keyword{"continue", TokenKind::KwContinue},
    Keyword{"return", TokenKind::KwReturn}, Keyword{"true", TokenKind::KwTrue},
    Keyword{"false", TokenKind::KwFalse},   Keyword{"int", TokenKind::KwInt},
    Keyword{"float", TokenKind::KwFloat},   Keyword{"bool", TokenKind::KwBool},
};

TokenKind classify_identifier(std::string_view text) noexcept {
  for (const Keyword& kw : kKeywords) {
    if (kw.spelling == text) {
      return kw.kind;
    }
  }
  return TokenKind::Identifier;
}

std::string describe_char(char c) {
  const auto byte = static_cast<unsigned char>(c);
  if (byte >= 0x20 && byte < 0x7f) {
    return std::string("unexpected character '") + c + "'";
  }
  std::array<char, 8> buf{};
  std::snprintf(buf.data(), buf.size(), "0x%02X", static_cast<unsigned>(byte));
  return std::string("unexpected byte ") + buf.data();
}

}  // namespace

std::string_view token_kind_name(TokenKind kind) noexcept {
  switch (kind) {
#define KESTREL_TOKEN_NAME(name, spelling) \
  case TokenKind::name:                    \
    return spelling;
    KESTREL_TOKEN_KINDS(KESTREL_TOKEN_NAME)
#undef KESTREL_TOKEN_NAME
  }
  return "unknown token";
}

std::vector<Token> Lexer::tokenize(std::string_view source, DiagnosticEngine& diags) {
  Lexer lexer(source, diags);
  std::vector<Token> tokens;
  tokens.reserve(source.size() / 4 + 1);
  while (true) {
    tokens.push_back(lexer.next());
    if (tokens.back().is(TokenKind::Eof)) {
      return tokens;
    }
  }
}

char Lexer::advance() noexcept {
  const char c = source_[pos_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Lexer::match(char expected) noexcept {
  if (at_end() || source_[pos_] != expected) {
    return false;
  }
  advance();
  return true;
}

void Lexer::skip_trivia() {
  while (!at_end()) {
    switch (peek()) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        advance();
        break;
      case '/':
        if (peek(1) == '/') {
          while (!at_end() && peek() != '\n') {
            advance();
          }
        } else if (peek(1) == '*') {
          skip_block_comment();
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

void Lexer::skip_block_comment() {
  const SourceLocation start = location();
  advance();
  advance();
  std::uint32_t depth = 1;
  while (!at_end()) {
    if (peek() == '/' && peek(1) == '*') {
      advance();
      advance();
      ++depth;
    } else if (peek() == '*' && peek(1) == '/') {
      advance();
      advance();
      if (--depth == 0) {
        return;
      }
    } else {
      advance();
    }
  }
  diags_.error(start, "unterminated block comment");
}

Token Lexer::make(TokenKind kind) const noexcept {
  return {kind, source_.substr(token_start_, pos_ - token_start_), token_location_};
}

Token Lexer::error(std::string message) {
  diags_.error(token_location_, std::move(message));
  return make(TokenKind::Error);
}

Token Lexer::next() {
  skip_trivia();
  token_start_ = pos_;
  token_location_ = location();
  if (at_end()) {
    return make(TokenKind::Eof);
  }

  const char c = advance();
  if (is_alpha(c)) {
    return lex_identifier();
  }
  if (is_dec_digit(c)) {
    return lex_number();
  }

  switch (c) {
    case '(':
      return make(TokenKind::LParen);
    case ')':
      return make(TokenKind::RParen);
    case '{':
      return make(TokenKind::LBrace);
    case '}':
      return make(TokenKind::RBrace);
    case ',':
      return make(TokenKind::Comma);
    case ';':
      return make(TokenKind::Semicolon);
    case ':':
      return make(TokenKind::Colon);
    case '~':
      return make(TokenKind::Tilde);
    case '^':
      return make(TokenKind::Caret);
    case '+':
      return make(match('=') ? TokenKind::PlusEqual : TokenKind::Plus);
    case '-':
      if (match('>')) {
        return make(TokenKind::Arrow);
      }
      return make(match('=') ? TokenKind::MinusEqual : TokenKind::Minus);
    case '*':
      return make(match('=') ? TokenKind::StarEqual : TokenKind::Star);
    case '/':
      return make(match('=') ? TokenKind::SlashEqual : TokenKind::Slash);
    case '%':
      return make(match('=') ? TokenKind::PercentEqual : TokenKind::Percent);
    case '&':
      return make(match('&') ? TokenKind::AmpAmp : TokenKind::Amp);
    case '|':
      return make(match('|') ? TokenKind::PipePipe : TokenKind::Pipe);
    case '!':
      return make(match('=') ? TokenKind::BangEqual : TokenKind::Bang);
    case '=':
      return make(match('=') ? TokenKind::EqualEqual : TokenKind::Equal);
    case '<':
      if (match('<')) {
        return make(TokenKind::LessLess);
      }
      return make(match('=') ? TokenKind::LessEqual : TokenKind::Less);
    case '>':
      if (match('>')) {
        return make(TokenKind::GreaterGreater);
      }
      return make(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);
    default:
      break;
  }

  if (static_cast<unsigned char>(c) >= 0x80) {
    while (!at_end() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80) {
      advance();
    }
  }
  return error(describe_char(c));
}

Token Lexer::lex_identifier() {
  while (is_ident_char(peek())) {
    advance();
  }
  return make(classify_identifier(source_.substr(token_start_, pos_ - token_start_)));
}

bool Lexer::consume_digits(bool (*is_digit)(char) noexcept) {
  bool ok = true;
  bool seen_digit = false;
  bool prev_underscore = false;
  while (true) {
    const char c = peek();
    if (is_digit(c)) {
      seen_digit = true;
      prev_underscore = false;
    } else if (c == '_') {
      if (!seen_digit || prev_underscore) {
        ok = false;
      }
      prev_underscore = true;
    } else {
      break;
    }
    advance();
  }
  return ok && seen_digit && !prev_underscore;
}

Token Lexer::lex_number() {
  const char first = source_[token_start_];
  if (first == '0' && (peek() == 'x' || peek() == 'X')) {
    advance();
    const bool ok = consume_digits(is_hex_digit);
    if (is_ident_char(peek())) {
      while (is_ident_char(peek())) {
        advance();
      }
      return error("invalid digit in hexadecimal literal");
    }
    if (!ok) {
      return error("malformed hexadecimal literal");
    }
    return make(TokenKind::IntLiteral);
  }

  pos_ = token_start_;
  column_ = token_location_.column;
  bool ok = consume_digits(is_dec_digit);
  bool is_float = false;

  if (peek() == '.' && is_dec_digit(peek(1))) {
    is_float = true;
    advance();
    ok = consume_digits(is_dec_digit) && ok;
  }
  if (peek() == 'e' || peek() == 'E') {
    const std::size_t sign = (peek(1) == '+' || peek(1) == '-') ? 1 : 0;
    if (is_dec_digit(peek(1 + sign))) {
      is_float = true;
      advance();
      if (sign != 0) {
        advance();
      }
      ok = consume_digits(is_dec_digit) && ok;
    } else {
      advance();
      if (sign != 0) {
        advance();
      }
      return error("expected digits in exponent");
    }
  }

  if (is_ident_char(peek())) {
    while (is_ident_char(peek())) {
      advance();
    }
    return error("invalid suffix on numeric literal");
  }
  if (!ok) {
    return error("misplaced digit separator '_'");
  }
  return make(is_float ? TokenKind::FloatLiteral : TokenKind::IntLiteral);
}

}  // namespace kestrel
