#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "frontend/token.h"
#include "support/diagnostics.h"

namespace kestrel {

class Lexer {
 public:
  Lexer(std::string_view source, DiagnosticEngine& diags) noexcept
      : source_(source), diags_(diags) {}

  [[nodiscard]] Token next();

  [[nodiscard]] static std::vector<Token> tokenize(std::string_view source,
                                                   DiagnosticEngine& diags);

 private:
  [[nodiscard]] bool at_end() const noexcept { return pos_ >= source_.size(); }
  [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept {
    return pos_ + ahead < source_.size() ? source_[pos_ + ahead] : '\0';
  }
  char advance() noexcept;
  bool match(char expected) noexcept;

  void skip_trivia();
  void skip_block_comment();

  [[nodiscard]] Token make(TokenKind kind) const noexcept;
  [[nodiscard]] Token error(std::string message);
  [[nodiscard]] Token lex_identifier();
  [[nodiscard]] Token lex_number();
  bool consume_digits(bool (*is_digit)(char) noexcept);

  [[nodiscard]] SourceLocation location() const noexcept {
    return {static_cast<std::uint32_t>(pos_), line_, column_};
  }

  std::string_view source_;
  DiagnosticEngine& diags_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t column_ = 1;
  std::size_t token_start_ = 0;
  SourceLocation token_location_;
};

}  // namespace kestrel
