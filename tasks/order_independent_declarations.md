# Task: Order-Independent Declarations

> **Status:** Proposed.

Allow file-scope functions, types, and variables to be referenced
before their definition, without a full AST and without a measurable
compile-time regression.

## Goal

- Resolve calls and type uses that appear before the definition.
- Keep in-order programs bit-identical and just as fast.
- Keep tiny output: do not duplicate code or add an IR layer.

## Non-goals

- Out-of-order local declarations inside a function.
- Class or namespace member lookup (`namespaces_syntax_c_and_d.md`).
- Whole-program dead-code elimination.

## Design: two-pass, token-span based

### Pass 1 — declarations only

- Parse the translation unit once as today, but:
  - emit no code for function bodies,
  - save each body as a `TokenString` (reuse
    `skip_or_save_block()` / the `unittest` machinery),
  - record the body span, its symbol, the file/line, and the current
    scope on the function symbol.
- Register every function prototype/definition, typedef, struct/union
  tag, enum tag, and global in the symbol table as usual.
- Record references discovered while skipping:
  - callee symbol for each call,
  - type symbol for each `sizeof`, cast, or declaration use,
  - global symbol for each lvalue/rvalue use.
- Do **not** resolve struct sizes that are still incomplete; mark the
  type as `incomplete` and defer layout.

### Pass 2 — generate in dependency order

- Build a graph over function symbols from the recorded references.
- Topologically sort; on a cycle, fall back to source order.
- For each function, replay its saved span with
  `begin_macro(span, 1)` / `end_macro()` and call the normal
  `gen_function()` path.
- Skip functions never referenced unless they are externally visible
  or marked `used`/`unittest`.
- Patch deferred struct sizes/offsets before generating any function
  that uses them.

## Implementation steps

1. **Symbol fields.** Add to `Sym` (or the existing per-function
   fields):
   - `TokenString *body` (or an index into a span table),
   - `int body_line`,
   - `unsigned ordered : 1`,
   - `unsigned incomplete : 1`.
2. **Declaration pass.** Hook `decl()` so a function definition
   saves its body instead of generating it. Keep the existing path
   behind a flag (`order_independent`) so it can be disabled.
3. **Reference recording.** In `unary()`/`post_type()` where symbol
   lookup happens, append `(from_sym, to_sym)` edges when
   `order_independent` is on.
4. **Type completion.** Track incomplete struct/union tags; when a
   definition arrives, update the shared `CType` and clear
   `incomplete`.
5. **Ordering.** Implement a small DFS topological sort in
   `tccgen.c`; reuse `Sym` indices, no new allocation per node.
6. **Generation loop.** Add `gen_ordered_functions()` modelled on
   `gen_inline_functions()`, run before final section cleanup.
7. **Diagnostics.** Report cycles only when they actually prevent a
   valid order; otherwise keep silent.
8. **Flag.** Add `-forder-independent` / `-fno-order-independent`;
   default off until the performance guardrail passes.

## Edge cases

- Recursive and mutually recursive functions: cycle handling.
- Address-taken functions and function pointers.
- `static` functions never called: drop or keep depending on
  `-ffunction-sections`/linker behaviour.
- `inline`/`always_inline` bodies.
- Globals with initialisers that reference later symbols.
- Arrays whose size comes from a later `enum`/`const`.
- `unittest` blocks and `$run` wrappers.
- Debug info: keep line numbers from the original span.
- Macros: spans are already macro-expanded, so replay must not
  re-expand.

## Test plan

| file | asserts |
| --- | --- |
| `NN_oi_call_before_def.c` | call a function defined later |
| `NN_oi_mutual_recursion.c` | two functions calling each other |
| `NN_oi_type_before_def.c` | use a struct tag before its definition |
| `NN_oi_enum_bound.c` | array bound from a later enum constant |
| `NN_oi_global_init.c` | global initialiser references a later global |
| `NN_oi_static_unused.c` | unused static functions are handled |
| `NN_oi_debug.c` | line info points at the original definition |

Verify in-order programs compile to the same bytes before/after the
change (compare `objdump` on the test corpus).

## Performance guardrails

- Pass 1 must not allocate an AST; only token spans and graph edges.
- Graph edges should be deduplicated per (from, to) pair.
- Measure:
  - `ninja -C build pgo-bench` before/after,
  - Valgrind `I refs`,
  - peak memory on a large TU.
- Acceptance: Mlines/s and instruction count within noise for
  in-order code; a small, documented overhead for the new mode.

## Acceptance criteria

- [ ] Functions and types can be referenced before definition at file
      scope.
- [ ] In-order C and D code compiles unchanged.
- [ ] No measurable `pgo-bench` regression.
- [ ] `-fno-order-independent` restores the old single-pass path.
- [ ] New regression tests under `tests/tests2/`.
