#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "support/source_location.h"

namespace kestrel {

enum class Severity { Error, Warning, Note };

struct Diagnostic {
  Severity severity;
  SourceLocation location;
  std::string message;
};

class DiagnosticEngine {
 public:
  void report(Severity severity, SourceLocation location, std::string message);
  void error(SourceLocation location, std::string message) {
    report(Severity::Error, location, std::move(message));
  }

  [[nodiscard]] bool has_errors() const noexcept { return error_count_ > 0; }
  [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

  [[nodiscard]] std::string render(std::string_view file_name, std::string_view source) const;

 private:
  std::vector<Diagnostic> diagnostics_;
  std::size_t error_count_ = 0;
};

[[nodiscard]] std::string_view severity_name(Severity severity) noexcept;

}  // namespace kestrel
