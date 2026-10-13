#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "frontend/lexer.h"
#include "support/diagnostics.h"

namespace {

constexpr std::string_view kUsage =
    "usage: kestrel [--dump-tokens] <file.ks>\n"
    "\n"
    "options:\n"
    "  --dump-tokens   print the token stream and exit\n"
    "  -h, --help      show this message\n";

enum class Mode { DumpTokens };

struct Options {
  Mode mode = Mode::DumpTokens;
  std::string path;
};

std::optional<Options> parse_args(int argc, char** argv, int& exit_code) {
  exit_code = 2;
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::cout << kUsage;
      exit_code = 0;
      return std::nullopt;
    }
    if (arg == "--dump-tokens") {
      options.mode = Mode::DumpTokens;
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "kestrel: unknown option '" << arg << "'\n" << kUsage;
      return std::nullopt;
    } else if (options.path.empty()) {
      options.path = arg;
    } else {
      std::cerr << "kestrel: multiple input files are not supported\n";
      return std::nullopt;
    }
  }
  if (options.path.empty()) {
    std::cerr << kUsage;
    return std::nullopt;
  }
  return options;
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return std::move(buffer).str();
}

void dump_tokens(std::string_view source, kestrel::DiagnosticEngine& diags) {
  kestrel::Lexer lexer(source, diags);
  while (true) {
    const kestrel::Token token = lexer.next();
    std::printf("%4u:%-3u %-18.*s '%.*s'\n", token.location.line, token.location.column,
                static_cast<int>(kestrel::token_kind_name(token.kind).size()),
                kestrel::token_kind_name(token.kind).data(), static_cast<int>(token.text.size()),
                token.text.data());
    if (token.is(kestrel::TokenKind::Eof)) {
      return;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  int exit_code = 0;
  const auto options = parse_args(argc, argv, exit_code);
  if (!options) {
    return exit_code;
  }

  const auto source = read_file(options->path);
  if (!source) {
    std::cerr << "kestrel: cannot read '" << options->path << "'\n";
    return 1;
  }

  kestrel::DiagnosticEngine diags;
  switch (options->mode) {
    case Mode::DumpTokens:
      dump_tokens(*source, diags);
      break;
  }

  if (diags.has_errors()) {
    std::cerr << diags.render(options->path, *source);
    return 1;
  }
  return 0;
}
