# Architecture and build map

This is the short map of the compiler, intended for humans and local
models.  Read it after `AGENTS.md` before opening a large source file.

The map lists the build inputs, the source fragments and the
configuration macros.  It is deliberately shallow: follow the entry
points into the code instead of loading a whole translation unit.

## Build inputs

| Path | Role |
|------|------|
| `configure` | Detect the host, target, libraries and optional features.  Writes `config.mak` and `config.h`. |
| `config.mak` | Make-style key/value view of the configuration. |
| `config.h` | C view of the same configuration.  The only generated header that defines `CONFIG_*`. |
| `Makefile` | Legacy build and test driver.  Ninja rules still delegate some targets to it. |
| `tools/build` | Python generator.  Writes `build/build.ninja`, `build/config.json` and `build/compile_commands.json`. |
| `build/build.ninja` | Generated build description; regenerates itself when the generator or configuration changes. |
| `VERSION` | Version string used by `configure`. |
| `include/tccdefs.h` | Predefined macros for target programs; converted to `tccdefs_.h`. |

## Core sources

| Path | Responsibility | Key entry points |
|------|----------------|------------------|
| `tcc.c` | Front-end entry point.  With `ONE_SOURCE=1` it also includes the other compiler fragments. | `main`, `tcc_compile` |
| `tcctools.c` | Small front-end helpers and `-m32`/`-m64` handling. | tool setup, target selection |
| `libtcc.c` | `TCCState` lifetime, options, allocator, file and library handling. | `tcc_new`, `tcc_delete`, `tcc_set_options`, `tcc_add_file`, `tcc_output_file` |
| `tccpp.c` | Lexer, token table, token strings, macro expansion, directives and includes. | `next`, `next_nomacro`, `tok_alloc`, `begin_macro`, `tcc_preprocess` |
| `tccgen.c` | Parser and target-neutral code generator: types, expressions, statements, initialisation, slices and tuples. | `tccgen_init`, `gen_function`, `block`, `expr`, `gen_op` |
| `tccdbg.c` | Debug information: STABS and DWARF line, type and symbol output. | `tcc_debug_new`, `tcc_debug_end`, `tcc_debug_funcstart` |
| `tccelf.c` | ELF sections, symbols, relocations, dynamic linking and the object/link writer. | `tccelf_new`, `new_section`, `put_elf_sym`, `tcc_output_file` helpers |
| `tccasm.c` | GNU assembler syntax parser and the shared assembler expression code. | `tcc_assemble`, `tcc_assemble_inline` |
| `tccrun.c` | `-run` support: relocation, memory protection, execution, backtraces and unwinding. | `tcc_relocate`, `tcc_run`, `tcc_run_free` |
| `tcc.h` | Core types and cross-fragment declarations.  Split by subsystem sections. | `TCCState`, `SValue`, `CType`, `Sym` |
| `elf.h` | ELF constants and structures. | format definitions |
| `dwarf.h` | DWARF constants. | format definitions |
| `coff.h` | COFF constants used by the PE target. | format definitions |

## Host and target backends

| Path | Responsibility | Key entry points |
|------|----------------|------------------|
| `x86_64-gen.c` | x86-64 code generation. | `gen_le32`, `load`, `store`, `gfunc_call` |
| `x86_64-link.c` | x86-64 relocations. | `relocate` |
| `i386-asm.c` | x86 instruction encoder shared by the x86 targets. | `tcc_assemble_internal` helpers |
| `i386-gen.c`, `i386-link.c` | 32-bit x86 backend. | code generation, relocations |
| `arm-gen.c`, `arm-link.c`, `arm-asm.c` | ARM backend and assembler. | code generation, relocations |
| `arm64-gen.c`, `arm64-link.c`, `arm64-asm.c` | AArch64 backend and assembler. | code generation, relocations |
| `riscv64-gen.c`, `riscv64-link.c`, `riscv64-asm.c` | RISC-V 64 backend and assembler. | code generation, relocations |
| `c67-gen.c`, `c67-link.c` | C67 DSP backend. | code generation, relocations |
| `tccpe.c` | PE/COFF object, DLL and executable output. | PE writer |
| `tccmacho.c` | Mach-O output. | Mach-O writer |

Only `x86_64-gen.c`, `x86_64-link.c` and `i386-asm.c` are in the
default Ninja source list.  The other fragments are selected by
`configure` for cross builds.

## Runtime library and tools

| Path | Responsibility |
|------|----------------|
| `lib/libtcc1.c` | Target runtime helpers used by generated code. |
| `lib/stdatomic.c` | C11 atomics support. |
| `lib/atomic.S` | Atomic primitives for targets that need assembly. |
| `lib/builtin.c` | Compiler builtins that are not inline. |
| `lib/alloca.S`, `lib/alloca-bt.S` | Stack allocation, with and without bounds tracking. |
| `lib/tcov.c` | Test-coverage runtime. |
| `lib/va_list.c` | `va_list` helpers. |
| `lib/dsohandle.c` | Dynamic-loader handle support. |
| `lib/runmain.c` | `-run` startup glue. |
| `tools/build` | Ninja generator and the source lists above. |
| `tools/hangdiag` | Diagnostic helper for hangs. |
| `tools/pgo-history` | PGO history recorder; not run by default. |

## Tests and diagnostics

| Path | Responsibility |
|------|----------------|
| `tests/tests2/` | Canonical regression suite: one `NN_name.c` and one `NN_name.expect` per behaviour. |
| `tests/tests2/Makefile` | Per-test flags, expected-failure handling and generator selection. |
| `tests/` | Legacy integration tests: `tcctest`, `libtcc_test`, ABI and memory tests. |
| `docs/debugging-hangs.md` | What to do when a test or compiler hangs. |
| `docs/perf-notes.md` | PGO benchmark protocol and caveats. |

## Configuration macros

Every macro belongs to exactly one category.  The classification is a
target for the refactoring, not yet an enforced rule; files that still
mix categories are listed in the refactoring task.

### A. Target and ABI

`TCC_TARGET_*`, `PTR_SIZE`, `LDOUBLE_SIZE`, `TCC_ARM_*`,
`TARGETOS_*`.

Owner: `config.h`, the target block in `tcc.h` and the backend files.
Target-neutral code must not test these directly.

### B. Build features

`CONFIG_TCC_BCHECK`, `CONFIG_TCC_BACKTRACE`, `CONFIG_TCC_ASM`,
`CONFIG_TCC_STATIC`, `CONFIG_DWARF_VERSION`, `CONFIG_TCC_PIE`,
`CONFIG_TCC_PREDEFS`.

Owner: `config.h`.  The goal is to select a file or a stub instead of
branching inside the core.

### C. Developer tracing

`INC_DEBUG`, `TAL_DEBUG`, `PP_DEBUG`, `BF_DEBUG`, `SYM_DEBUG`,
`ASM_DEBUG`, `PARSE_DEBUG`.

Owner: the file being traced.  These are debug-only and should not
change normal builds.

### D. Host build mode

`ONE_SOURCE`, `MEM_DEBUG`, `TCC_PROFILE`, `TCC_GITHASH`, `C2STR`.

Owner: the build command line or `tools/build`.  These select how the
compiler is built, not what the compiler supports.

## Querying the configuration

`tools/build` writes a machine-readable summary during generation.
Print it with:

    ninja -C build config

It contains the target, the build directory, the effective compiler
flags and the `CONFIG_*` and `TCC_*` macros that `config.h` defines.

Write a root-level `compile_commands.json` from the same flags:

    ninja -C build compdb

Both files are generated and must not be edited by hand.  The build
directory copies stay in `build/config.json` and
`build/compile_commands.json`.

`ONE_SOURCE=0` is the tooling default, so graph indexers and language
servers see one definition per symbol.  `ONE_SOURCE=1` remains the
release amalgamation and is not used for navigation.

## Keeping this map current

- Add a row whenever a source file or fragment joins the build lists
  in `tools/build` or `Makefile`.
- Keep one responsibility per row.  If a row needs "and" three times,
  the file probably needs splitting.
- Update the macro classification when a new `CONFIG_*` or
  `TCC_TARGET_*` macro appears.
- Prefer this map over guessing which `#ifdef` branch is live; then
  confirm with `ninja -C build config`.
