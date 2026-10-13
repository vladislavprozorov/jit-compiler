# Kestrel

A statically typed toy language with a bytecode interpreter and a tiered JIT compiler
for AArch64, written in modern C++20.

```kestrel
fn fib(n: int) -> int {
  if n < 2 {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

fn main() -> int {
  return fib(35);
}
```

## Architecture

```
source ─► Lexer ─► Parser ─► AST ─► Sema ─► Bytecode ─► Interpreter (tier 0)
                                                 │
                                                 ├──► Baseline JIT (tier 1, AArch64)
                                                 │
                                                 └──► SSA IR ─► Optimizer ─► RegAlloc ─► Optimizing JIT (tier 2)
```

| Module            | Path              | Responsibility                                      |
| ----------------- | ----------------- | --------------------------------------------------- |
| `support`         | `src/support/`    | Source locations, diagnostics rendering             |
| `frontend`        | `src/frontend/`   | Lexer, parser, AST, semantic analysis               |
| `bytecode`        | `src/bytecode/`   | Register-based bytecode, compiler from AST          |
| `vm`              | `src/vm/`         | Interpreter, call frames, profiling counters        |
| `jit`             | `src/jit/`        | Executable memory, AArch64 assembler, JIT tiers     |
| `ir`              | `src/ir/`         | SSA IR, optimization passes, linear-scan allocator  |

## Language

- Types: `int` (64-bit signed), `float` (IEEE-754 double), `bool`
- Bindings: `let` (immutable), `var` (mutable), compound assignment `+= -= *= /= %=`
- Control flow: `if`/`else`, `while`, `break`, `continue`, `return`
- Functions with explicit parameter and return types, recursion
- Operators: arithmetic, bitwise, shifts, comparisons, short-circuit `&&` / `||`
- No implicit conversions: `to_float(int)`, `to_int(float)` convert explicitly
- Builtins: `print` (overloaded for `int`, `float`, `bool`), `sqrt`, `to_float`, `to_int`
- Literals: `1_000_000`, `0xFF`, `2.5e-3`; nested `/* */` comments

## Building

Requirements: CMake ≥ 3.25, Ninja, Clang ≥ 16 or GCC ≥ 13.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer build:

```bash
cmake -S . -B build-asan -G Ninja -DKESTREL_SANITIZE=ON
```

Run:

```bash
./build/kestrel examples/fib.ks
./build/kestrel --dump-tokens examples/fib.ks
./build/kestrel --dump-ast examples/fib.ks
```

```
(fn fib (n: int) -> int
  (block
    (if (< n 2)
      (block
        (return n)))
    (return (+ (call fib (- n 1)) (call fib (- n 2))))))
```

## Frontend design

- **Arena-allocated AST.** Nodes are trivially destructible and live in a bump allocator;
  the whole tree is freed at once. No `unique_ptr` per node, no virtual destructors.
- **LLVM-style RTTI.** Each node carries a kind tag; `isa<T>`, `cast<T>`, `dyn_cast<T>`
  replace `dynamic_cast`.
- **Pratt expression parser** with explicit binding powers. Comparisons are
  non-associative (`a < b < c` is an error), bitwise operators bind tighter than comparisons.
- **Error recovery.** Panic mode with synchronization at statement and declaration
  boundaries; lexer errors never cascade into parser errors.
- **Bounded recursion.** Nesting depth is capped, so adversarial input such as
  `((((...))))` produces a diagnostic instead of a stack overflow.
- **AST invariant.** The tree never contains half-built nodes: a construct with a syntax
  error is dropped as a whole.

## Semantic analysis

The checker annotates the AST in place, so later stages never look anything up by name:
every expression gets its type, every variable reference gets a frame slot, every call
gets its target function or builtin.

- **Slot allocation with reuse.** Variables in sibling scopes share frame slots, so each
  function's `local_count` is the minimum register file size the VM needs.
- **No cascading errors.** An ill-typed expression yields an error type that parents
  silently propagate; a variable whose initializer failed is usable without new errors.
- **Control-flow analysis.** Non-void functions must return on every path; `while true`
  without `break` counts as diverging; `break`/`continue` outside loops are rejected.
- **Warnings.** Unused variables and parameters (silenced with a `_` prefix), unreachable
  code, unused expression results.

```
bad.ks:8:16: error: argument 1 of 'area' must be 'float', found 'int'
    let a = area(r);
                 ^
bad.ks:9:6: error: 'if' condition must be 'bool', found 'float'
    if a {
       ^
bad.ks:6:4: error: function 'main' does not return a value on all paths
  fn main() -> int {
     ^
```

## Operator precedence

| Level | Operators                     | Associativity |
| ----- | ----------------------------- | ------------- |
| 1     | `\|\|`                        | left          |
| 2     | `&&`                          | left          |
| 3     | `== != < <= > >=`             | none          |
| 4     | `\|`                          | left          |
| 5     | `^`                           | left          |
| 6     | `&`                           | left          |
| 7     | `<< >>`                       | left          |
| 8     | `+ -`                         | left          |
| 9     | `* / %`                       | left          |
| 10    | unary `- ! ~`                 | prefix        |

## Roadmap

- [x] Build system, CI (macOS/Linux arm64, ASan/UBSan), diagnostics
- [x] Lexer
- [x] Parser and AST, `--dump-ast`
- [x] Semantic analysis: name resolution, type checking, control-flow checks
- [ ] Register-based bytecode and compiler, `--dump-bytecode`
- [ ] Interpreter (computed-goto dispatch)
- [ ] Executable memory (`MAP_JIT`, W^X) and AArch64 assembler
- [ ] Baseline JIT: template compilation of bytecode
- [ ] Tier-up via call and back-edge counters, on-stack replacement
- [ ] SSA IR, constant folding, DCE, GVN, LICM
- [ ] Linear-scan register allocation
- [ ] Optimizing JIT
- [ ] Differential testing interpreter vs JIT, libFuzzer harnesses
- [ ] Benchmarks against CPython, Lua and LuaJIT
