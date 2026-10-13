#pragma once

#include <cstdint>
#include <string_view>

#include "support/source_location.h"

namespace kestrel {

#define KESTREL_TOKEN_KINDS(X)     \
  X(Eof, "end of file")            \
  X(Error, "invalid token")        \
  X(Identifier, "identifier")      \
  X(IntLiteral, "integer literal") \
  X(FloatLiteral, "float literal") \
  X(KwFn, "'fn'")                  \
  X(KwLet, "'let'")                \
  X(KwVar, "'var'")                \
  X(KwIf, "'if'")                  \
  X(KwElse, "'else'")              \
  X(KwWhile, "'while'")            \
  X(KwBreak, "'break'")            \
  X(KwContinue, "'continue'")      \
  X(KwReturn, "'return'")          \
  X(KwTrue, "'true'")              \
  X(KwFalse, "'false'")            \
  X(KwInt, "'int'")                \
  X(KwFloat, "'float'")            \
  X(KwBool, "'bool'")              \
  X(LParen, "'('")                 \
  X(RParen, "')'")                 \
  X(LBrace, "'{'")                 \
  X(RBrace, "'}'")                 \
  X(Comma, "','")                  \
  X(Semicolon, "';'")              \
  X(Colon, "':'")                  \
  X(Arrow, "'->'")                 \
  X(Plus, "'+'")                   \
  X(Minus, "'-'")                  \
  X(Star, "'*'")                   \
  X(Slash, "'/'")                  \
  X(Percent, "'%'")                \
  X(Amp, "'&'")                    \
  X(Pipe, "'|'")                   \
  X(Caret, "'^'")                  \
  X(Tilde, "'~'")                  \
  X(LessLess, "'<<'")              \
  X(GreaterGreater, "'>>'")        \
  X(Bang, "'!'")                   \
  X(AmpAmp, "'&&'")                \
  X(PipePipe, "'||'")              \
  X(Equal, "'='")                  \
  X(EqualEqual, "'=='")            \
  X(BangEqual, "'!='")             \
  X(Less, "'<'")                   \
  X(LessEqual, "'<='")             \
  X(Greater, "'>'")                \
  X(GreaterEqual, "'>='")          \
  X(PlusEqual, "'+='")             \
  X(MinusEqual, "'-='")            \
  X(StarEqual, "'*='")             \
  X(SlashEqual, "'/='")            \
  X(PercentEqual, "'%='")

enum class TokenKind : std::uint8_t {
#define KESTREL_TOKEN_ENUM(name, spelling) name,
  KESTREL_TOKEN_KINDS(KESTREL_TOKEN_ENUM)
#undef KESTREL_TOKEN_ENUM
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;

struct Token {
  TokenKind kind = TokenKind::Eof;
  std::string_view text;
  SourceLocation location;

  [[nodiscard]] bool is(TokenKind k) const noexcept { return kind == k; }
};

}  // namespace kestrel
