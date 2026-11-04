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
| `tcc.c` | Front-end entry point.  With `ONE_SOURCE=1` it also includes the other compiler fragments. | `main`, `set_environment`, `default_outputfile` |
| `tcctools.c` | Small front-end helpers and `-m32`/`-m64` handling. | tool setup, target selection |
| `libtcc.c` | `TCCState` lifetime, options, allocator, file and library handling. | `tcc_new`, `tcc_delete`, `tcc_set_options`, `tcc_add_file`, `tcc_compile` |
| `tccpp.c` | Lexer, token table, token strings, macro expansion, directives and includes. | `next`, `next_nomacro`, `tok_alloc`, `begin_macro`, `preprocess` |
| `tccgen.c` | Parser and target-neutral code generator: types, expressions, statements, initialisation, slices and tuples. | `tccgen_init`, `gen_function`, `block`, `expr`, `gen_op` |
| `tccdbg.c` | Debug information: STABS and DWARF line, type and symbol output. | `tcc_debug_new`, `tcc_debug_end`, `tcc_debug_funcstart` |
| `tccelf.c` | ELF sections, symbols, relocations, dynamic linking and the object/link writer. | `tccelf_new`, `new_section`, `put_elf_sym`, `tcc_output_file` |
| `tccasm.c` | GNU assembler syntax parser and the shared assembler expression code. | `tcc_assemble`, `tcc_assemble_inline` |
| `tccrun.c` | `-run` support: relocation, memory protection, execution, backtraces and unwinding. | `tcc_relocate`, `tcc_run`, `tcc_run_free` |
| `tcc.h` | Umbrella header (1,778 lines): configuration macros, shared types, tokens, allocator and utility declarations, state accessors. | `TCCState`, `SValue`, `CType`, `Sym` |
| `elf.h` | ELF constants and structures. | format definitions |
| `dwarf.h` | DWARF constants.  Included only by `tccdbg.c` and `tccrun.c`. | format definitions |
| `stab.h` | STABS debug codes.  Included only by the debug, runtime and COFF files that emit them. | format definitions |
| `coff.h` | COFF constants used by the PE target. | format definitions |

### Declaration headers

`tcc.h` includes these headers in dependency order.  They are not
standalone: they assume the types and macros from `tcc.h` and from the
headers listed above them, and they are only compiled through the
umbrella.

| Header | Responsibility | Lines |
|--------|----------------|------:|
| `tcc-pp.h` | Lexer and preprocessor state, token buffers, `next`/`preprocess` entry points. | 105 |
| `tcc-gen.h` | Parser and code-generator state, symbol stacks, value-stack helpers, `gen_op`/`expr`. | 165 |
| `tcc-elf.h` | ELF writer, symbol table, relocations, loader and archives. | 74 |
| `tcc-backend.h` | The interface every target backend implements: linker and code-generator hooks. | 140 |
| `tcc-asm.h` | Inline assembler entry points. | 25 |
| `tcc-dbg.h` | Debug information, coverage and unwind-frame declarations. | 113 |

### Code-generator fragments

`tccgen.c` includes these files in order.  They share its translation
unit and its static parser/code-generator state, so they are not
compiled on their own and no internals are exported through `tcc.h`.
The build's depfile tracks them; a change to any fragment rebuilds
`tccgen.o`.

| Fragment | Responsibility | Lines |
|----------|----------------|------:|
| `tccgen.c` | Globals, symbol and label stacks, value-stack primitives, relocation and start/finish. | 2081 |
| `tccgen-gen.c` | Register/stack movement, generic operations, complex and cast code. | 2065 |
| `tccgen-types.c` | Type size and compatibility, attributes, struct/union layout and declarations. | 2116 |
| `tccgen-tuple.c` | Tuple layout and literals, slices, slice bounds checks, type predicates. | 2352 |
| `tccgen-expr.c` | Unary/postfix parsing, operators, conditionals and constant expressions. | 1901 |
| `tccgen-stmt.c` | Returns, switch, cleanups, scopes, blocks, `foreach` and `defer`. | 1254 |
| `tccgen-init.c` | Initialisers, function generation and inline-function emission. | 1286 |
| `tccgen-d.c` | D conditional compilation, destructuring, asserts and `$` forms. | 1395 |
| `tccgen-decl.c` | Declarations and slice assignment. | 549 |

### Preprocessor and lexer fragments

`tccpp.c` includes these files in order.  As with `tccgen.c`, they
share its translation unit and static state, so they are not compiled
on their own.  `tools/build` tracks them through the depfile and the
`Makefile` lists them as prerequisites of `tccpp.o`, `tcc.o` and
`tcc_p`.

| Fragment | Responsibility | Lines |
|----------|----------------|------:|
| `tccpp.c` | Globals, tiny allocator, dynamic strings, token table and `skip`/`expect`. | 659 |
| `tccpp-lex.c` | Character input, comments and stray backslashes, token-string buffers and the macro stack. | 716 |
| `tccpp-dir.c` | `#define`/`#undef`, includes and cached includes, conditional evaluation, pragmas and directive dispatch. | 749 |
| `tccpp-token.c` | Escape sequences, string and numeric literal scanning, and the raw token reader. | 1016 |
| `tccpp-macro.c` | Macro argument substitution, token pasting and the public `next`/`unget_tok` interface. | 589 |
| `tccpp-init.c` | Target predefined macros and preprocessor start-up and teardown. | 224 |
| `tccpp-out.c` | `-E` output, token spacing and `PP_DEBUG` define printing. | 195 |

### ELF and linker fragments

`tccelf.c` includes these files in order, with the same translation-unit
and dependency rules.

| Fragment | Responsibility | Lines |
|----------|----------------|------:|
| `tccelf.c` | Globals, section allocation and lookup, version bookkeeping. | 390 |
| `tccelf-sym.c` | String and symbol tables, ELF and GNU hashes, symbol lookup and listing. | 706 |
| `tccelf-reloc.c` | Symbol and section relocation, GOT/PLT, runtime helpers, dynamic symbol binding, section/segment layout and the dynamic section. | 1605 |
| `tccelf-out.c` | ELF and binary image writers, section ordering, target note/attribute sections and `-c` object output. | 568 |
| `tccelf-load.c` | Object, archive and DLL loading, symbol versioning and linker-script parsing. | 983 |

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
