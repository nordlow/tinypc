# Task: Source and build layout for local LLM reasoning

## Goal

Make the C sources, the build instructions and the build-selected
preprocessor configuration readable by a local LLM without it having
to reconstruct hidden state.  A model should be able to answer "where
is this implemented?", "which code is actually compiled?" and "which
defines are active?" by reading a small number of files and running
one command, using far fewer tokens and tool calls than today.

This is a restructuring task, not a feature task.  Compiler
behaviour, language semantics, tests and performance must not change.

## Why the current layout is hard for a local model

The context cost and the ambiguity come from four independent layers.

**Large translation units.**  Current sizes (lines, as of this task):

| File       | Lines |
|------------|------:|
| `tccgen.c` | 14993 |
| `tccelf.c` |  4248 |
| `tccpp.c`  |  4142 |
| `tccdbg.c` |  2760 |
| `libtcc.c` |  2431 |
| `tcc.h`    |  2372 |
| `tccasm.c` |  1525 |
| `tccrun.c` |  1588 |

A 15k-line file is roughly 150k tokens.  It does not fit in the
context of most local models together with the task, the headers and
the test files.  An agent must therefore guess which region to read,
and a wrong guess costs a large part of the available context.

The compiler is also organized as C fragments that are merged or
compiled separately depending on `ONE_SOURCE`:

- with `ONE_SOURCE=1`, `tcc.c` includes `tccpp.c`, `tccgen.c`,
  `tccdbg.c`, `tccasm.c`, `tccelf.c`, `tccrun.c` and the per-target
  fragments;
- with `ONE_SOURCE=0`, the same implementations compile as separate
  objects, and cross-translation-unit declarations are spread through
  sections of `tcc.h`.

Reading the backend-agnostic core (`tcc.c`, `tccpp.c`, `tccgen.c`,
`tccdbg.c`, `tccasm.c`, `tccelf.c`, `tccrun.c`, `tcc.h`, `elf.h`,
`dwarf.h`) is well over 250k tokens.  That is most of a large agent
context window before task-specific context is added.

**Conditional compilation everywhere.**  Counting only lines that
start with `#if`, `#ifdef`, `#ifndef`, `#elif` or `#else`, the main
files contain roughly: `tccgen.c` 167, `tccelf.c` 123, `libtcc.c` 87,
`tccpp.c` 63.  Many of these select code for other targets
(`TCC_TARGET_*`), build features (`CONFIG_TCC_*`) or developer
tracing (`*_DEBUG`).  A model reading the file cannot tell which
branch is live in the current build, so it reasons about dead code,
or proposes edits inside a branch the compiler never sees.

**Build-derived defines are spread over four places.**  The active
configuration currently comes from:

1. `configure`, which writes `config.mak` and `config.h`;
2. `Makefile`, which adds its own `DEFINES` (`-DCONFIG_*`,
   `-DONE_SOURCE=0`, `-DTCC_PROFILE`, `-DCONFIG_TCC_STATIC`, ...);
3. `tools/build`, which reads `config.mak` but also hardcodes
   `-DONE_SOURCE=0 -DTCC_GITHASH=...`;
4. `tcc.h`, which includes `config.h` and then fills in defaults with
    more `#ifndef` blocks.

Reconstructing "what is defined?" therefore requires reading the
generator, the generated files, the header and the build rules.  That
is exactly the kind of multi-file inference local models do poorly.

**Two build paths.**  Make and Ninja both build the compiler and
both drive tests.  A model that finds one path may miss a target,
a flag or a special case that only exists in the other.  The
Ninja-first plan in `tasks/ninja_only_build.md` addresses this; this
task focuses on the source and configuration layout that remains
after that plan.

## Design principles

1. **The active code path must be visible without a preprocessor.**
   Prefer separate files selected by the build over `#ifdef` blocks
   inside a function.
2. **One concern per file, and files small enough to read whole.**
   The practical budget is a file that fits in the model context with
   room for headers and the task.  For a 32k-token context that means
   well under 2,000 lines; for 128k it means under 8,000.  Aim for
   at most 2,500 lines per hand-written C file.
3. **One canonical configuration.**  One generator produces one header
   and one machine-readable summary.  No layer re-derives defaults
   that another layer already decided.
4. **Code motion before behaviour change.**  Move first, verify, then
   edit.  Pure moves keep the diff reviewable and the benchmark
   comparable.
5. **Optimize for retrieval, not for novelty.**  Stable file names,
   one definition per symbol, distinctive function names, and
   doc comments at declarations.
6. **Keep the performance model intact.**  The single-translation-unit
   amalgamation (`ONE_SOURCE=1`) stays available; separate compilation
   becomes the default for tooling and local reasoning.

## Workstream A: split the C sources

### A1. Split `tccgen.c` along functional seams

`tccgen.c` is a monolithic compiler core: 14,993 lines and roughly
350 KB.  Split it along these seams:

1. value stack and code-emission core (`vstack`, `gv`, `gen_op`,
   compares, returns) — backend-facing;
2. expressions (parse/eval, casts, constant folding, slices, helper
   calls, tuple and slice operators);
3. statements and control flow (blocks, declarations, loops, switch,
   VLA, cleanup, scopes);
4. types and layout (type system, compatibility, qualifiers, storage,
   struct/union/enum/tuple layout);
5. initialisation and output plumbing (`tccgen_init`,
   `tccgen_finish`, symbol and relocation glue, debug output).

Keep the split mechanical: cut at top-level definitions and do not
rename anything in the same commit.  Each new file should be under
3,000 lines; if a seam is larger, split it once more by the same
rule.

The fragments are included by `tccgen.c` rather than compiled as
separate objects.  Sharing the translation unit keeps every static
function and file-scope variable private, holds the token stream
identical, and works in both `ONE_SOURCE` modes.  The trade-off is
that an agent must follow the include chain from `tccgen.c`; the
fragment table in `docs/architecture.md` makes that chain explicit.
`tools/build` tracks the fragments through a GCC depfile, and the
`Makefile` lists them as explicit prerequisites.

### A2. Split `tccelf.c` and `tccpp.c`

Apply the same treatment when `tccgen.c` is done:

- `tccelf.c`: ELF object writing, relocation processing, dynamic
  linking and platform-specific entry/exit code;
- `tccpp.c`: lexer, token strings and macro expansion, directives and
  include handling, predefined macros.

`tccdbg.c` is large but already mostly self-contained; split only if
the file has clear internal seams (DWARF emission vs. line tables vs.
type names).

### A3. Move target code out of the core

`tccgen.c` currently contains target branches such as
`TCC_TARGET_X86_64`, `TCC_TARGET_RISCV64`, `TCC_TARGET_ARM`,
`TCC_TARGET_PE` and `TCC_TARGET_MACHO`.  Move each block to the
matching backend file (`x86_64-gen.c`, `riscv64-gen.c`, ...) or to a
small shared target header when it is ABI data rather than code.
After this, the target-neutral core should contain no
`TCC_TARGET_*` conditionals except size/ABI constants that have a
single obvious home.

### A4. Keep the amalgamation

`ONE_SOURCE=1` remains supported by `tcc.c` including the fragments.
Do not delete it: it is the release build that gives the optimizer a
single translation unit.  Document that graph indexing, LSP and local
reading use the separate-object mode (`ONE_SOURCE=0`), which is
already the default in `tools/build`.

### A5. De-monomize the headers

Split payload out of `tcc.h`, `elf.h`, `dwarf.h` and `coff.h` so that
an agent touching one concern does not load 150 KB or more of
declarations.  Keep one small umbrella header for the public build
interface, and move each subsystem's types and prototypes next to the
fragment that implements them.  After the split, a query about the
ELF writer should not require reading the preprocessor or debug-info
definitions.

### A6. Extract shared backend support

The per-target `*-gen.c` files duplicate register-class,
calling-convention and prologue/epilogue boilerplate.  Pull the
shared parts into one internal header or fragment, and leave only
genuinely target-specific code in each backend.  This reduces the
number of near-identical files a model must compare and makes a
target-specific question answerable from one file plus one shared
header.

### A7. Test-suite and hygiene cleanup

- Keep `tests/tests2/` canonical.  Move the legacy `tests/tcctest.c`
  family into a clearly separated legacy path.
- Ignore generated artifacts inside `tests/tests2/` (`*.exe`, `*.o`,
  `*.so`, `*.tcov`, `*.output`, `*.log`).
- Resolve naming anomalies such as `104+_inline.c`,
  `120+_alias.c` and `425+_tuple_abi.c`.
- Keep one behaviour per test file with a descriptive name.  A model
  searching for coverage should find a file whose name states the
  behaviour, not an unrelated number.

## Workstream B: make conditional compilation explicit

### B1. Classify every macro

Write down each configuration macro and place it in exactly one
category:

- **A. Target/ABI:** `TCC_TARGET_*`, `PTR_SIZE`, `LDOUBLE_SIZE`,
  `TCC_ARM_*`, OS selectors (`TARGETOS_*`).
- **B. Build features:** `CONFIG_TCC_BCHECK`, `CONFIG_TCC_BACKTRACE`,
  `CONFIG_TCC_ASM`, `CONFIG_TCC_STATIC`, `CONFIG_DWARF_VERSION`,
  `CONFIG_TCC_PIE`, `CONFIG_TCC_PREDEFS`.
- **C. Developer tracing:** `INC_DEBUG`, `TAL_DEBUG`, `PP_DEBUG`,
  `BF_DEBUG`, `SYM_DEBUG`, `ASM_DEBUG`, `PARSE_DEBUG`.
- **D. Host build mode:** `ONE_SOURCE`, `MEM_DEBUG`, `TCC_PROFILE`,
  `TCC_GITHASH`, `C2STR`.

The classification is the deliverable: put it in `docs/architecture.md`
and keep it current.  Every macro must have one owner and one
category.

### B2. Target/ABI macros stay, but in one place

Keep `#if` for target selection, but confine it to the existing
target block in `tcc.h` and to the backend files.  Do not spread
target tests through target-neutral code.  Where a branch only
chooses a constant (pointer size, long-double size), expose the
constant once and use it unconditionally.

### B3. Turn build features into selected files or stubs

For each category-B feature:

- move the implementation into a file (`tcc_bcheck.c`,
  `tcc_backtrace.c`, `tcc_asm.c`, ...);
- provide a stub file with the same entry points that does nothing
  (or reports "not built in");
- let the build generator choose the file from the configuration.

Call sites in the core then contain no `#ifdef CONFIG_TCC_*`.  If a
feature is genuinely only a few instructions in a hot path, a single
`static inline` wrapper in a generated header is acceptable, but the
number of such sites should stay small and be listed in the
classification document.

### B4. Remove developer tracing `#if`s from normal builds

Category C is pure debugging.  Either delete it (git history keeps
it), or turn it into a runtime verbosity flag checked by the existing
`-d` infrastructure.  `#if 0` blocks should be deleted: they are dead
code for every reader, human or model.

### B5. One generated configuration header

`configure` already writes `config.h`; make it the only header that
defines configuration macros.  Remove the `#ifndef CONFIG_*` fallback
logic from `tcc.h` by emitting defaults from the generator instead.
The same applies to `-DCONFIG_*` on compiler command lines: move them
into `config.h`.

The only defines that may remain on the command line are those that
select the build mode itself (`ONE_SOURCE`, `TCC_GITHASH`, `C2STR`).

### B6. Emit a machine-readable configuration summary

Have the generator also write `build/config.json` with the effective
target, build mode, feature set and every macro value.  Add a Ninja
target that prints it:

    ninja -C build config

An agent can then answer "is bounds checking enabled?" or "which
target is this build?" with one cheap command instead of reading
`configure`, `config.mak`, `config.h`, `Makefile` and `tools/build`.

## Workstream C: one deterministic build description

### C1. Finish the Ninja-first plan

Phases 1 (self-regenerating `build.ninja`) and 3 (bounded rules) are
implemented in `tools/build`; keep
`tasks/ninja_only_build.md` as the tracker for the remaining phases.
The parts that matter for local reasoning are:

- `tools/build` is the only generator; `build.ninja` is regenerated
  when the generator, `configure`, `config.mak` or `VERSION` change;
- every long-running rule has a `timeout -k 5` wrapper, stdin is
  redirected from `/dev/null`, and a timeout prints `TIMEOUT`
  (already done);
- a failure prints the test name, a short diff and the log path, not
  the whole suite transcript.

### C2. Deterministic output

`tools/build` should emit stable, sorted rules with no timestamps, so
that two runs, or two worktrees, produce byte-identical
`build.ninja`.  This lets an agent diff generated output instead of
reasoning about it.

### C3. One command, plus focused targets

Keep `./configure --ninja && ninja -C build check` as the single
verification entry point (and keep it cheap to rerun).  Add small
targets that an agent can discover and use:

- `ninja -C build config` — effective configuration (see B6);
- `ninja -C build compdb` — write `compile_commands.json`;
- `ninja -C build check` — build and run the suite;
- `ninja -C build sani` — sanitizer suite;
- `ninja -C build test-one` with `TEST=N`;
- `ninja -C build help` — print the documented targets.

### C4. Keep `compile_commands.json` in sync

Generate it from the same flags the build uses, including
`ONE_SOURCE=0` and the generated `config.h` include path.  This makes
clangd, code-graph tools and the compiler agree on which code is
active.  A model that queries a symbol then sees the same definition
the build sees.

### C5. Shrink the build instructions in `AGENTS.md`

Once the generator is self-regenerating and the rules are bounded,
the "Long-running commands" prose can be reduced to the one command,
the sanitizer command, and a pointer to `docs/debugging-hangs.md`.
Every paragraph of build folklore is context an agent must read
before doing real work.

## Workstream D: navigation and diagnostics

### D1. Module map

Add `docs/architecture.md` with one row per source file or fragment:

| Path | Responsibility | Key entry points | Category-B/C macros |
|------|----------------|------------------|---------------------|

Keep it short.  It should be the first file an agent reads after
`AGENTS.md`, and it should be checked against the build's source list
so it cannot silently rot.

### D2. Stable, searchable names

- One definition per symbol; do not alias the same function through
  several macros.
- Prefer distinctive names over generic ones (`gen_slice_index_check`
  rather than `check_index`).
- Keep doc comments at declarations (`tcc.h`, `ST_FUNC`) so symbol
  lookup returns the contract, not just the call sites.
- Avoid frequent file renames; retrieval tools cache paths.

### D3. Function and file size

Aim for functions under roughly 100 lines and no nested `#if`
deeper than two levels.  Use early returns and small helpers instead
of one large dispatch function.  This helps both human review and
local models that reason line by line.

### D4. Actionable errors and logs

Keep the `file:line: message` shape everywhere.  For build and test
failures, print the failing command, the log path and a short diff.
Avoid dumping megabytes of compiler output into the agent's context.

## Workstream E: measure reasoning effectiveness

Structural metrics are necessary but not sufficient.  Build a small
benchmark and run it with the local model(s) actually in use.

### E1. Question set

Use 20-30 questions of the forms:

- "Where is slice index bounds checking emitted?"
- "Which files implement the RISC-V backend?"
- "Is `CONFIG_TCC_BCHECK` enabled in this build, and where is it
  read?"
- "What calls `try_tuple_assign`?"
- "Which tests cover inclusive slice ranges?"
- "If I add a field to `SValue`, which files must change?"

### E2. Recorded metrics

For each question, record:

- success (correct file, correct symbol, correct answer);
- number of tool calls and files read;
- wall time;
- input tokens consumed;
- whether the model read a dead `#ifdef` branch or a generated file.

Also track structural metrics over time: largest file, conditional
line count per file, number of files containing `CONFIG_*`, number of
`-D` flags on compile commands, and whether `build.ninja` is
deterministic.

### E3. Acceptance

The restructuring succeeds when the benchmark improves without a
compiler performance regression.  Do not tune for a single model
version; record model, quantization and context size with the
results.

## Performance guardrails

Restructuring must not change generated code or slow the compiler
down.  Before and after each phase, run:

    ./configure --ninja && ninja -C build check
    ninja -C build sani
    ninja -C build pgo

`pgo` rebuilds the compiler with profile-guided optimisation and runs
`pgo-bench` at the end.  Run it on `HEAD^` and on the change, and
report both Mlines/s and the Valgrind instruction count.  Do not write
to `bench/pgo-history.tsv` unless explicitly asked.

Baseline measured on this host during this task (best of 5):

| Tree | Mlines/s | Valgrind I refs |
|------|---------:|----------------:|
| `HEAD` (`6d90f49d`) | 4.67 | 256,427,605 |
| working tree (leak fixes, slice P2) | 4.68 | 256,941,176 |

The instruction count is the more stable signal; the working tree is
+0.29%, which is noise.  An earlier record of 4.35 Mlines/s and
249,388,455 I refs comes from a different revision and host; treat it
only as a sanity range, not as a baseline for new work.

For changes that only move code, the instruction count should be
unchanged.  A larger shift means the move changed inlining or code
generation and must be investigated before the phase lands.

## Verification performed so far

- `./configure --ninja && ninja -C build check`: `ALL TESTS PASSED`.
- `ninja -C build config` prints the effective configuration, and
  `ninja -C build compdb` writes `compile_commands.json` in the source
  root from the same flags the Ninja rules use.
- `ninja -C build sani`: two compiler leaks were found and fixed
  (`449_tuple_assign_error`, `436_d_tuple_destructure_error`).  The
  remaining failures (`332_d_darray`, `334_d_darray_bcheck`,
  `416_d_stack_address_false_positives`) are pre-existing
  allocations made by the JIT-compiled test programs; the leak sizes
  are identical at `HEAD`.  They are not compiler leaks.

## Suggested rollout

1. **P0 — docs and visibility.**  Add the module map, the macro
   classification, `ninja -C build config`, `compile_commands.json`
   and delete `#if 0` blocks.  No behaviour change.
2. **P1 — split `tccgen.c`.**  Pure code motion, verified by tests
   and an unchanged instruction count.
3. **P2 — de-monomize the headers.**  Split `tcc.h`, `elf.h`,
   `dwarf.h` and `coff.h` by subsystem (A5).
4. **P3 — split `tccelf.c` / `tccpp.c`.**  Same rules as P1.
5. **P4 — feature files.**  Move category-B code behind selected
   files; remove `#ifdef CONFIG_*` from the core.
6. **P5 — shared backend support.**  Extract duplicated
   target-backend boilerplate (A6).
7. **P6 — test-suite hygiene.**  Legacy test separation, ignore
   rules and naming anomalies (A7).
8. **P7 — build unification.**  Finish `tasks/ninja_only_build.md`;
   make the Makefile a forwarding shim.
9. **P8 — measure and iterate.**  Run the benchmark from Workstream E
   after each phase.

Status: P0 is implemented.  `docs/architecture.md` holds the module
map and the macro classification; `tools/build` writes
`build/config.json` and `build/compile_commands.json`; and the
`config` and `compdb` targets expose them.  Deleting `#if 0` blocks
remains open: it is mechanical but touches target files that are not
built locally, so it should land as its own reviewable change.

Status: P1 is implemented.  `tccgen.c` is now 2,081 lines and the
largest fragment is 2,352 lines.  The fragments are include files, so
the change is behavioural no-op apart from line numbers in two
internal `init_assert` messages.  Preprocessed output matches the
old file line for line except for those two numbers.

Status: P2 is implemented for `tcc.h`.  Its declaration sections moved
into `tcc-pp.h`, `tcc-gen.h`, `tcc-elf.h`, `tcc-backend.h`,
`tcc-asm.h` and `tcc-dbg.h`, which `tcc.h` includes in dependency
order; `tcc.h` drops from 2,372 to 1,778 lines.  `dwarf.h` and
`stab.h` are no longer included by the umbrella, only by the files
that use their constants: `tccdbg.c` and `tccrun.c` for DWARF,
`tccgen.c`, `tccdbg.c`, `tccrun.c` and `tcccoff.c` for STABS.
Concatenating the moved text reproduces the old `tcc.h` byte for byte
apart from those two include lines.

`elf.h`, `dwarf.h` and `coff.h` themselves are not split.  They are
vendored, single-concern format payloads with no compiler
declarations, so splitting them by constant group would add files
without changing what an agent reads for a question about that
format.  `elf.h` also has to stay in `tcc.h` because `TCCState` embeds
`ElfW_Rel`, `ElfSym` and `addr_t`.

Status: P3 is implemented.  `tccpp.c` is 659 lines with six fragments
(195–1,016 lines), and `tccelf.c` is 390 lines with four fragments
(568–1,605 lines), so every hand-written source is below the 3,000-line
acceptance limit.  Concatenating the fragments reproduces both original
files byte for byte.  The ELF writer boundary sits after the
`#ifndef ELF_OBJ_ONLY` block, which originally spanned the
relocation/output seam, so that conditional stays inside one fragment.
`tools/build` tracks the fragments through the depfile and the
`Makefile` lists them as prerequisites of `tccpp.o`, `tccelf.o`,
`tcc.o` and `tcc_p`.

## Acceptance criteria

- No hand-written `.c` file exceeds 3,000 lines; generated tables and
  test data are exempt.
- The target-neutral core contains no `CONFIG_*` or `TCC_TARGET_*`
  conditionals; those live in selected files or target backends.
- `ninja -C build config` prints every effective macro and feature.
- Every `CONFIG_*` value originates in the generated `config.h`;
  command lines only carry build-mode defines (`ONE_SOURCE`,
  `TCC_GITHASH`, `C2STR`).
- `compile_commands.json` is generated and matches the Ninja flags.
- `docs/architecture.md` lists every build input and stays in sync.
- The reasoning benchmark improves or, at minimum, does not regress.

## Verification

After every phase:

    ./configure --ninja && ninja -C build check

Before committing, also:

    ninja -C build sani

For changes that only move code, run the benchmark protocol from
`AGENTS.md` on `HEAD^` and on the change.  Pure code motion should
produce an unchanged instruction count; report both Mlines/s values.

## Non-goals

- Changing language features, diagnostics or test contents.
- Rewriting the compiler in another language.
- Adopting CMake, Meson or Bazel.
- Optimising compiler performance as a primary goal; performance is a
  guardrail.
- Adding comments that merely restate the code; the goal is fewer
  hidden decisions, not more prose.

## Risks and open questions

- **Performance.**  Splitting a translation unit can lose inlining and
  cross-function optimisation.  Mitigation: keep `ONE_SOURCE=1` for
  release builds, measure before and after, and only keep the split
  if the benchmark stays within noise.
- **Linkage.**  Moving code between files exposes symbols that were
  previously `static`.  Use `ST_FUNC` and the existing `tcc.h`
  sections; avoid making internal helpers part of the public ABI.
- **Cross-target matrix.**  Removing `#ifdef` can break a target that
  is not built locally.  Keep a CI matrix and move one feature at a
  time.
- **Churn.**  Large mechanical moves conflict with feature work.
  Land them in dedicated commits or short series, and avoid mixing
  renames with moves.
- **What context size should we target?**  The split threshold
  depends on the smallest local model in use.  Decide the target
  (for example 32k tokens) before P1 and record it.
- **Runtime versus link-time feature selection.**  Runtime flags are
  simpler for readers but add branches and configuration surface;
  selected files are cleaner but multiply build variants.  Measure
  both for `CONFIG_TCC_BCHECK` before committing to one.
- **Generated files and graph tools.**  Keep `build/` out of the
  graph index and tell tools to use `ONE_SOURCE=0`, otherwise the
  same symbol appears through both the amalgamation and the fragments.

## Related tasks

- `tasks/ninja_only_build.md` — make Ninja the single build executor
  and retire the Makefile (Workstream C).
- `tasks/refactor_vstack_logic.md` — refactor stack-based value
  operations in `tccgen.c`; a natural companion to A1.
- `tasks/slice_range_checking.md` — an example feature whose
  implementation spans core parsing, code generation and the runtime;
  it benefits directly from the clearer file boundaries in A1-A3.
- `tasks/split_todo_into_separate_entries.md` — keep task tracking
  itself retrievable and small.
