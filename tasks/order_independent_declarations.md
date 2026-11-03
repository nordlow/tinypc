# Task: Order-Independent Declarations

## Goal

Allow declarations and definitions to be used before they appear in source
order (forward references), while preserving compiler speed and tiny
output size.

## Strategy

1. **Two-pass, token-span based (no full AST).**
   -  Pass 1 parses only declarations/types into the symbol table, skipping
     bodies, and saves each function body as a token span
     (`skip_or_save_block`).
   -  Pass 2 generates code in dependency order, replaying saved spans via
     `begin_macro`/`end_macro`.

2. **Lazy/on-demand generation.**
   -  Generate a function only when called or at end-of-TU, mirroring
     `gen_inline_functions()` (`tccgen.c:10283`).
   -  Cache generated status on the symbol to avoid rework.

3. **Dependency tracking.**
   -  Record references during Pass 1; topologically sort in Pass 2.
   -  Cycles fall back to declaration order.

4. **Preserve speed.**
   -  No AST allocation; reuse token strings and symbol table.
   -  Pass 1 does no codegen, relocation, or debug emission.
   -  Pass 2 is incremental and skips unreachable functions.

5. **ABI/safety.**
   -  Resolve type sizes/alignments in Pass 1; patch forward-declared
     struct sizes in Pass 2.

## Acceptance Criteria

- [ ] Functions/types can be referenced before definition at file scope.
- [ ] `-lang=c` and `-lang=d` behavior unchanged for in-order code.
- [ ] No measurable regression in `pgo-bench` Mlines/s or instruction count.
- [ ] New regression tests under `tests/tests2/`.
