#pragma once

#include "frontend/ast.h"
#include "support/diagnostics.h"

namespace kestrel {

[[nodiscard]] bool analyze(Module& module, DiagnosticEngine& diags);

}  // namespace kestrel
