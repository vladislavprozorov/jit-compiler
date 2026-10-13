#include "support/diagnostics.h"

#include <algorithm>
#include <utility>

namespace kestrel {

namespace {

std::string_view line_containing(std::string_view source, std::uint32_t offset) {
  const std::size_t clamped = std::min<std::size_t>(offset, source.size());
  const std::size_t prev_newline = source.rfind('\n', clamped == 0 ? 0 : clamped - 1);
  const std::size_t begin =
      (prev_newline == std::string_view::npos || clamped == 0) ? 0 : prev_newline + 1;
  std::size_t end = source.find('\n', begin);
  if (end == std::string_view::npos) {
    end = source.size();
  }
  return source.substr(begin, end - begin);
}

}  // namespace

std::string_view severity_name(Severity severity) noexcept {
  switch (severity) {
    case Severity::Error:
      return "error";
    case Severity::Warning:
      return "warning";
    case Severity::Note:
      return "note";
  }
  return "unknown";
}

void DiagnosticEngine::report(Severity severity, SourceLocation location, std::string message) {
  if (severity == Severity::Error) {
    ++error_count_;
  }
  diagnostics_.push_back({severity, location, std::move(message)});
}

std::string DiagnosticEngine::render(std::string_view file_name, std::string_view source) const {
  std::string out;
  for (const Diagnostic& diag : diagnostics_) {
    out += file_name;
    out += ':';
    out += std::to_string(diag.location.line);
    out += ':';
    out += std::to_string(diag.location.column);
    out += ": ";
    out += severity_name(diag.severity);
    out += ": ";
    out += diag.message;
    out += '\n';

    const std::string_view line = line_containing(source, diag.location.offset);
    out += "  ";
    out += line;
    out += "\n  ";
    for (std::uint32_t i = 1; i < diag.location.column && i <= line.size(); ++i) {
      out += line[i - 1] == '\t' ? '\t' : ' ';
    }
    out += "^\n";
  }
  return out;
}

}  // namespace kestrel
