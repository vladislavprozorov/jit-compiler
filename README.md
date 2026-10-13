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
./build/kestrel --dump-tokens examples/fib.ks
```

## Roadmap

- [x] Build system, CI (macOS/Linux arm64, ASan/UBSan), diagnostics
- [x] Lexer
- [ ] Parser and AST, `--dump-ast`
- [ ] Semantic analysis: name resolution, type checking
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
