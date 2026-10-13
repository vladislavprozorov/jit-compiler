#include "support/diagnostics.h"

#include <gtest/gtest.h>

namespace kestrel {
namespace {

TEST(DiagnosticEngine, CountsOnlyErrors) {
  DiagnosticEngine diags;
  diags.report(Severity::Warning, {}, "w");
  diags.report(Severity::Note, {}, "n");
  EXPECT_FALSE(diags.has_errors());
  diags.error({}, "e");
  EXPECT_TRUE(diags.has_errors());
  EXPECT_EQ(diags.error_count(), 1u);
  EXPECT_EQ(diags.diagnostics().size(), 3u);
}

TEST(DiagnosticEngine, RendersCaretUnderColumn) {
  const std::string_view source = "let x = 1;\nlet y = @;\n";
  DiagnosticEngine diags;
  diags.error({.offset = 19, .line = 2, .column = 9}, "unexpected character '@'");
  EXPECT_EQ(diags.render("main.ks", source),
            "main.ks:2:9: error: unexpected character '@'\n"
            "  let y = @;\n"
            "          ^\n");
}

TEST(DiagnosticEngine, RendersFirstLine) {
  const std::string_view source = "@";
  DiagnosticEngine diags;
  diags.error({.offset = 0, .line = 1, .column = 1}, "bad");
  EXPECT_EQ(diags.render("a.ks", source), "a.ks:1:1: error: bad\n  @\n  ^\n");
}

}  // namespace
}  // namespace kestrel
