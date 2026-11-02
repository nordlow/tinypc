# Task: Compile-Time Function Execution (`$run`, Jai-style `#run`)

Give `-lang=d` a compile-time function execution (CTFE) facility:
`$run(expr)` and `$run { ... }` compile their payload with the
existing backend, relocate a private snapshot of the image, and jump
to the generated code *while the compiler is running*, using the
result as a compile-time constant.

The construct is the analogue of Jai's `#run` and of D's
`__ctfe`-guarded code, but it deliberately reuses `libtcc`'s code
generator instead of adding an interpreter to the frontend.

## Goal

- Accept `$run(expr)` in constant expressions and `$run { ... }` as a
  file-scope statement in `-lang=d` mode.
- Compile the payload into a hidden wrapper function with the normal
  backend, so CTFE observes exactly the same ABI, struct layout,
  integer-promotion and overflow behaviour as the eventual program.
- Execute the wrapper by relocating a *private snapshot* of the
  translation unit's sections, calling the wrapper through its symbol
  index, and discarding the snapshot afterwards.
- Push the result back into the parser as an ordinary constant
  (`VT_CONST` value or a reference to materialised data), so it can
  feed array sizes, `enum` values, initialisers, `static assert`, and
  `$assert`.
- Report CTFE crashes and timeouts as ordinary compile errors instead
  of aborting or hanging the compiler.

Non-goals: an interpreter over the token stream, D's full CTFE purity
model, or C++-style `constexpr` evaluation rules.

## Motivation

Compile-time evaluation today stops at constant folding:
`expr_const1()` (`tccgen.c:7951`) folds arithmetic over literals,
`static assert` / `$assert` (`tccgen.c:10388`, `10401`) evaluate a
constant expression, manifest constants (`check_d_manifest_constant`,
`tccgen.c:10326`) alias a folded value, and `unittest` blocks are
only compiled and *run* later via `-unittest -run`.

None of these can compute a table with a loop, call a helper, or build
a struct.  Users fall back to code generators or to `unittest` plus a
second compiler invocation.

The backend already exists and is fast; tcc's own compile step is a
few milliseconds, and `tcc_relocate()` already knows how to turn the
in-memory sections into executable code.  Reusing it means:

- no second semantics to maintain, and no interpreter bugs;
- CTFE results are bit-identical to what the program would compute;
- the feature costs the frontend only a parser hook plus a state
  snapshot, not a new evaluator.

## Surface syntax

Two forms, both gated on `-lang=d` and on `TCC_IS_NATIVE`
(`tcc.h:210`), exactly like `-run` (`libtcc.c:2131`):

```c
/* expression form: usable anywhere a constant expression is */
enum { N = $run(twice(21)) };
static int table[$run(compute_size(7))];

$assert($run(fact(5)) == 120);

/* statement form: run code for its effect on the compilation */
$run {
    /* loops, calls, prints -- evaluated now, not at run time */
}
```

- `$run` is a *contextual* keyword, not a `tcctok.h` keyword.  It is
  matched textually, the way `$assert` is (`tccgen.c:10401`).  It only
  has special meaning in D mode and only when followed by `(` or `{`,
  so existing identifiers named `$run` keep working.
- `$run` is one member of the `$` compile-time family (`$(...)`,
  `$if`, `$foreach`, `$assert`).  The shared lexing rule -- `$` is
  special only before `(` (or `{` for `$run`) -- is specified in
  `tasks/support_tuples.md`, including the rule that `$if` opens no
  scope of its own (only its explicit braces do).
- The statement form is a block, so `$run { ... }` needs no trailing
  `;`, but one is accepted and ignored.
- `#run` as an alias is listed under M4: `#` starts a preprocessor
  directive in `tccpp.c`, so the alias needs a new directive entry
  rather than a parser hook.  `$run` is the primary spelling.
- M1 restricts both forms to file scope; block scope needs the
  `gen_function()` refactor described under M3.
- **Relationship with `$(...)`:** `$(...)` is reserved for lightweight
  compile-time constant folding and literal tuple construction, whereas
  `$run(...)` explicitly triggers full `libtcc` compilation, relocation, and
  execution. While `$(...)` could theoretically attempt interpretation or
  delegate to CTFE when constant folding fails, keeping them separate
  maintains a clear performance and semantic boundary between cheap folding
  and heavy in-memory execution. However, this design remains preliminary and
  may evolve or unify once further investigations and profiling have been
  performed.


## Execution model

Five phases, all inside one call from the parser.

### Phase 1: payload capture

`skip_or_save_block()` (`tccgen.c:9072`) already saves a balanced
token run into a `TokenString` without consuming the terminating
`)`, `}`, `,` or `;`.  `_Generic` (`tccgen.c:6974`-`6991`) and VLA
array types (`tccgen.c:5849`-`5855`) use it exactly this way;
`unittest` (`tccgen.c:11211`) consumes the same live stream without
saving it first.

Consequences to document:

- The saved tokens are already macro-expanded, so replaying them never
  re-expands a macro.
- Replayed tokens carry the line number of the `$run` occurrence, not
  of the original payload.  Diagnostics inside CTFE code therefore
  point at the `$run` site; acceptable, and worth a note in the
  README.
- Token replay goes through `begin_macro(str, 1)` / `end_macro()`
  (`tccpp.c:1093`, `1107`), which `gen_function()`'s trailing
  `next()` needs in order to start reading the body.

### Phase 2: type pass (expression form only)

The result type of `expr` must be known before the wrapper can be
declared.  `expr_type()` (`tccgen.c:6204`) parses an expression with
`nocode_wanted++`, keeps `vtop->type`, and pops it, which is how
`auto` inference already works (`tccgen.c:11316`).  Run it over a
replayed copy of the payload, then apply `convert_expression_type()`
(`tccgen.c:5444`: drop `VT_QUALIFIER`, array to pointer, function to
pointer) so the wrapper's return type is a legal ABI type.

Note that `auto v = "abc"` therefore deduces `char *`, i.e. a pointer
into the snapshot -- which is why pointer results are rejected in M1
(see *Value marshalling*).

### Phase 3: wrapper synthesis

`do_unittest()` (`tccgen.c:10769`) is the template.  It builds a
`static void __unittest_N(void)` type by hand, allocates the symbol
with `external_sym()` (`tccgen.c:1410`) on a `tok_alloc()`ed name,
points `cur_text_section` at `text_section`, and calls
`gen_function()` (`tccgen.c:10204`), which parses the body from the
current token stream.

For `$run` the same steps produce:

| result kind            | wrapper signature                |
| ---------------------- | -------------------------------- |
| integer / `_Bool`      | `static T __tcc_ctfe_N(void)`    |
| `float`/`double`/`long double` | `static T __tcc_ctfe_N(void)` |
| struct / union (M2)    | `static void __tcc_ctfe_N(void *out)` |

Three differences from `do_unittest()` matter:

- `sr->f.func_ctor` must stay 0.  `do_unittest()` sets it to 1 so the
  test lands in `.init_array` (`tccgen.c:10228`); a CTFE wrapper in
  `.init_array` would run at program start, which is exactly wrong.
- The wrapper name is fresh (`TCC_TLS int nb_ctfe`, mirroring
  `nb_unittest` at `tccgen.c:10767`) so repeated `$run`s do not
  collide, and the symbol stays `static` (STB_LOCAL).
- Debug info must be suppressed for the wrapper: `gen_function()`
  calls `tcc_debug_funcstart()` (`tccgen.c:10233`), which would emit
  DWARF for a function that is dead code.  Save `debug_modes` and
  clear it around the synthesis.

Because `gen_function()` ends with

```c
sym_pop(&local_stack, NULL, 0);
label_pop(&global_label_stack, NULL, 0);
sym_pop(&all_cleanups, NULL, 0);
```

(`tccgen.c:10263`-`10265`) it pops *everything*, not just what it
pushed, and leaves `cur_text_section = NULL`, `ind = 0`,
`func_ind = -1`, `nocode_wanted = DATA_ONLY_WANTED`.  At file scope
that is the state `decl()` expects, which is why `do_unittest()` can
get away with it.  Inside an initialiser the surrounding parser state
must be preserved explicitly:

```c
struct ctfe_parse_ctx {
    int ind, func_ind, nocode_wanted, local_scope, global_expr;
    int rsym, nb_temp_local_vars, debug_modes;
    Section *cur_text_section;
    CType func_vt;
    int func_var, func_safety, func_old;
    const char *funcname;
    Scope *cur_scope, *root_scope;
};
```

`ctfe_save_parse()` / `ctfe_restore_parse()` fill and restore it
around every `$run`.  This struct is also 80% of what M3 needs.

### Phase 4: snapshot and relocation

Relocation is destructive and single-shot, so it cannot be applied to
the live state:

- `relocate_section()` writes through `ptr = s->data + rel->r_offset`
  (`tccelf.c:1156`).
- `relocate_sections()` rewrites the relocation entries themselves:
  `rel->r_offset += s->sh_addr` (`tccelf.c:1221`), so a second
  relocation of the same section double-adds.
- `relocate_syms()` rewrites `st_value` in place
  (`tccelf.c:1135`).
- `tcc_relocate_ex()` reaches `cleanup_symbols()` /
  `cleanup_sections()` at `copy == 3` (`tccrun.c:444`-`450`), which
  drops STB_LOCAL symbols and frees every section but the symtab.
- `tcc_relocate()` refuses to run twice (`tccrun.c:158`).

So each `$run` works on a deep clone:

1. **Prepare once per state.**  `tcc_relocate_ex(s1, NULL, 0)`
   normally calls `tcc_add_runtime()` (`tccelf.c:1827`),
   `resolve_common_syms()` (`tccelf.c:1975`) and
   `build_got_entries(s1, 0)` (`tccelf.c:1422`).  Doing that on the
   parent, once, keeps symbol resolution shared.  It is not
   idempotent as written: `tcc_add_support(s1, TCC_LIBTCC1)` would
   re-add `libtcc1.a` objects and duplicate symbols.  Add
   `s1->ctfe_runtime_added` / `s1->got_built` guards (or a
   `tcc_runtime_prepare(s1)` wrapper with its own flag) and assert
   they are set before snapshotting.
2. **Clone the shell and the section graph.**  Copy the `TCCState`
   by value, then replace every owned pointer: `sections`,
   `priv_sections`, `symtab` (with its `link` strtab and `hash`),
   `sym_attrs`, `dynsymtab`, `got`, `plt`.  Each `Section` is
   `tcc_mallocz`ed fresh and its `data`/`data_allocated`/
   `data_offset` copied, `s1` repointed at the clone, and
   `sh_addr` reset to 0.  `loaded_dlls` and the path arrays are
   shared, not copied.  Set `output_type = TCC_OUTPUT_MEMORY` in the
   clone so the `TCC_OUTPUT_DYN` branches in `relocate_section()`
   stay off, and clear `do_backtrace`, `dflag`, `test_coverage`,
   `do_debug`, `dwarf`.
3. **Swap the TLS state.**  `tccelf.c` and `tccrun.c` reach the state
   through bare macros (`text_section`, `symtab_section`, ... at
   `tcc.h:2046`-`2076`) which expand to `tcc_state->...`.  Save
   `tcc_state` and `cur_tcc_state`, set both to the clone for the
   duration of the relocation, and restore them afterwards.  Do not
   use `tcc_enter_state()` / `tcc_exit_state()`
   (`libtcc.c:138`/`146`): the former returns early while
   `error_set_jmp_enabled` is set (it always is, see `tcc_compile()`
   at `libtcc.c:811`), and the latter sets `tcc_state = NULL`.
4. **Relocate the clone.**  A new internal entry point derived from
   `tcc_relocate_ex()` (`tccrun.c:330`) with the prepare step skipped
   and the `cleanup_symbols()` / `cleanup_sections()` calls omitted,
   so the symtab keeps the wrapper's index.  `rt_mem()`
   (`tccrun.c:116`) plus `protect_pages()` (`tccrun.c:471`) already
   provide the executable memory.
5. **Resolve the wrapper by index, not by name.**  `put_elf_sym()`
   only hashes non-local symbols (`tccelf.c:463`-`480`), and
   `cleanup_symbols()` strips STB_LOCAL ones (`tccrun.c:284`), so
   `tcc_get_symbol()` (`tccelf.c:540`) cannot find a `static`
   wrapper.  Keep the `Sym *` returned by `external_sym()` and read
   `elfsym(sym)->st_value`, or index the clone's symtab directly with
   `sym->c`.

### Phase 5: call, marshal, tear down

The call itself reuses the `-run` crash plumbing: `tcc_setjmp()`
(the macro in `libtcc.h:105`-`106`) installs a `jmp_buf` and a
`longjmp` target on the state, `set_exception_handler()`
(`tccrun.c:1366`) turns SIGFPE/SIGILL/SIGSEGV/SIGBUS/SIGABRT into
`sig_error()` (`tccrun.c:1321`), which calls `rt_error()`
(`tccrun.c:1204`) and longjmps out.

- Register the clone with `st_link()` (`tccrun.c:559`) before the
  call so `rt_find_state()` can map a faulting PC back to the clone's
  image, and `st_unlink()` (`tccrun.c:580`) afterwards.  Otherwise
  the backtrace resolver either misses the frame or attributes it to
  the parent state.
- Inject an `exit` interceptor into the clone with
  `tcc_add_symbol(clone, "exit", ctfe_exit_stub)` so a payload that
  calls `exit()` becomes a compile error instead of terminating the
  compiler.  The same hook covers `abort()` (already SIGABRT) and,
  later, `fork()`.
- A `setjmp` return of non-zero means the payload died; report
  `"$run: <reason>"` with `tcc_error_noabort()` so `-dt` can collect
  several failures, and abandon that payload.
- Free the clone: unprotect the pages the way `tcc_run_free()` does
  (`tccrun.c:207`), `free_section()` each cloned section, free
  `sym_attrs`, free the shell.  Never call `tcc_delete()` on it --
  that would `tccelf_delete()` shared pointers and `dlclose()` the
  parent's DLL references.

## Value marshalling

| result type | how the driver reads it | how it is pushed into the parent |
| --- | --- | --- |
| `_Bool`, char, short, int, long, long long (signed/unsigned) | cast the address to the matching `T (*)(void)` and call | `vpushi()` / `vsetc()` as `VT_CONST` |
| `float`, `double`, `long double` | same, with the matching return type | `CValue.f` / `.d` / `.ld` |
| pointer | rejected in M1 | M2: map address to section + offset, find the parent symbol covering it, re-emit with `get_sym_ref()` (`tccgen.c:1168`) and `vpushsym()` (`tccgen.c:1159`) |
| struct / union | M2: `void *out` parameter pointing at driver memory | M2: copy into a hidden `static const` blob in `.rodata` and reference it |
| array / function | rejected (the type pass already decays them) | -- |

Two semantic points the implementation must pin down:

- **Pointers must not escape the snapshot.**  The returned address
  points into memory that is freed at the end of the `$run`.  Until
  provenance mapping exists, a pointer result is a hard error:
  `"$run result may not be a pointer"`.
- **Writes to globals land in the clone.**  `$run { counter = 7; }`
  modifies the clone's `.data`, and the clone is thrown away, so the
  statement form is only useful for `$assert`-style checking and for
  compile-time diagnostics in M1.  Making it useful needs a
  per-symbol write-back (M2): read the symbol's bytes from the
  relocated image, verify that the type has no indirections (or map
  each pointer field's provenance), and emit that value as the
  parent symbol's initialiser.  Copying whole sections back is *not*
  an option, because relocation has rewritten every pointer in
  `.data` with a clone-local address.

## Affected Components & Code Details

### 1. Parser hooks (`tccgen.c`)

- **Declarations**: add `static int check_d_ctfe_run(int l);` next to
  the other D helpers (`tccgen.c:181`-`185`) and call it from
  `decl()` after `check_d_static_assert()` (`tccgen.c:11204`).  It
  must reject `l != VT_CONST` with `"$run is only allowed at file
  scope"`, mirroring the `unittest` check at `tccgen.c:11212`.
- **Expressions**: hook the `tok_identifier:` label in `unary()`
  (`tccgen.c:7011`), after `t = tok; next();` and before
  `sym_find(t)`.  Trigger on `lang_d && t >= TOK_UIDENT &&
  !strcmp(get_tok_str(t, &tokc), "$run") && tok == '('`.  Guard with
  `func_ind >= 0` to reject block scope in M1.
- **Statements** (M3): hook `block()` right after `next()`
  (`tccgen.c:8686`), in the same shape as the `scope(exit)` case at
  `tccgen.c:8826`-`8846`.
- `$run` must *not* be added to `tcctok.h`; keeping it a plain
  identifier is what allows the contextual behaviour and avoids
  breaking `-lang=c` sources that use `$` in identifiers
  (`dollars_in_identifiers` defaults to 1, `libtcc.c:881`).

### 2. Wrapper synthesis (`tccgen.c`)

- `static Sym *do_ctfe_wrapper(CType *ret, TokenString *body, int
  with_out_param)` -- the `do_unittest()` body generalised, returning
  the symbol so the driver can read its `->c` index.
- `struct ctfe_parse_ctx` plus `ctfe_save_parse()` /
  `ctfe_restore_parse()`.
- A `TCC_TLS int nb_ctfe` counter for wrapper names.
- Suppress debug and coverage emission around the synthesis
  (`debug_modes = 0`, save/restore `tcc_state->test_coverage`).
- **Dead-code residue**: the wrapper stays in the parent's `.text`
  and is emitted into every object file.  M1 accepts this (it is a
  dead `static` function, a few dozen bytes).  Trimming options, in
  order of increasing risk, are listed under *Open questions*.

### 3. Snapshot and clone (`tccrun.c`, `tccelf.c`)

- `static TCCState *tcc_snapshot_new(TCCState *s1);`
- `static void tcc_snapshot_free(TCCState *s2);`
- Both live in `tccrun.c` next to `rt_mem()` because they only exist
  for `TCC_IS_NATIVE` builds; declare them in the internal section of
  `tcc.h` (not in `libtcc.h`, which is the public ABI).
- `Section` has a `s1` back-pointer (`tcc.h:596`) that must be
  repointed at the clone; `new_section()` (`tccelf.c:229`) does this
  for fresh sections and is the model for the copy loop.
- `sym_attrs` (`tcc.h:982`) is mutated by `build_got_entries()` and
  `relocate_syms()`, so it must be cloned as an array, not shared.

### 4. Relocation driver (`tccrun.c`)

- `static int tcc_relocate_snapshot(TCCState *s2);` -- a copy of
  `tcc_relocate_ex()` (`tccrun.c:330`) with a `snapshot` flag that
  (a) skips `tcc_add_runtime()` / `resolve_common_syms()` /
  `build_got_entries()`, (b) skips `cleanup_symbols()` /
  `cleanup_sections()` at `copy == 3`, and (c) returns the relocated
  size.
- Replace the TLS error at `tccrun.c:389` with a CTFE-aware message
  when in snapshot mode: `"$run: thread-local storage is not
  supported"`.
- `st_link()` / `st_unlink()` around the call, so crash reporting
  resolves frames in the snapshot image.

### 5. Result materialisation (`tccgen.c`)

- Scalars: `vpushi()` for integers, `vsetc()` with `VT_CONST` and the
  right `CValue` member for floating types.
- Aggregates (M2): allocate a hidden rodata object with
  `section_add()` and `get_sym_ref()`, then `vpushsym()` -- the same
  path string literals and compound literals already use.
- Provenance mapping (M2): given a returned address, find the clone
  section containing it, compute `offset`, look for a parent symbol
  whose section and range cover `offset`, and re-emit
  `parent_sym + (offset - parent_offset)`.  Anything else is an
  error.

### 6. Options, diagnostics, documentation

- `-fno-ctfe` / `-fctfe=off`: make `$run` an error even in
  `-lang=d`, for hermetic builds.  Wire it like the other `TCCState`
  booleans (`tcc.h` flags block, parsed in `tcc_parse_args`).
- `-fctfe-timeout=<seconds>` (M4): `alarm()`/`SIGALRM` or a watchdog
  thread that longjmps out of the payload.
- `-Wctfe` (M4): warn when a `$run` payload is not `@pure`/`@det`
  (see `deterministic_attribute.md`) or when CTFE is used in a build
  that also produces `-c` output, since the result then depends on
  the host.
- `version(CTFE)` and a `__ctfe` macro that is 1 while a payload is
  being compiled, so shared code can branch
  (`version(ctfe) { ... }` in the `check_d_conditional()` style at
  `tccgen.c:11084`).  The wrapper is dead code, so the runtime branch
  never executes.
- README: a new subsection under "D-style language extensions"
  (`README.md:234`), next to `unittest` and `$assert`, plus a
  cross-reference from the compile-time reflection proposal in
  `TODO.md:151`-`186`.

## Data structures

```c
/* tcc.h, defined before struct TCCState and embedded in it */
struct ctfe_ctx {
    TCCState *snapshot;   /* relocated clone, NULL when not running */
    Sym *wrapper;         /* static wrapper symbol in the parent */
    CType ret_type;       /* converted result type */
    void *entry;          /* relocated wrapper address */
    void *image;          /* rt_mem() allocation */
    unsigned image_size;
    int failed;           /* set by the signal/timeout path */
    char reason[80];
};

/* tcc.h, parsed once in tcc_parse_args() */
unsigned char ctfe;             /* 1 = allow $run (default in -lang=d) */
int ctfe_timeout;               /* seconds, 0 = none (M4) */
unsigned char ctfe_runtime_added; /* tcc_add_runtime() ran once */
unsigned char got_built;        /* build_got_entries() ran once */
```

`ctfe` belongs in the D-options block beside `lang_d` (`tcc.h:797`).
`ctfe_runtime_added` and `got_built` are used from `tccelf.c` on every
build, so they go in the unconditional flags area next to
`nocommon`/`static_link`, not in the `TCC_IS_NATIVE` block.

## Function-by-function change list

| file | symbol | change |
| --- | --- | --- |
| `tccgen.c` | `decl()` (11191) | call `check_d_ctfe_run(l)` after `check_d_static_assert()` |
| `tccgen.c` | `unary()` (6459) | intercept `$run(` at `tok_identifier:` (7011) |
| `tccgen.c` | `block()` (8673) | M3: intercept `$run {` after `next()` |
| `tccgen.c` | new | `check_d_ctfe_run()`, `do_ctfe_run()`, `do_ctfe_expr()` |
| `tccgen.c` | new | `do_ctfe_wrapper()`, `ctfe_save_parse()`, `ctfe_restore_parse()` |
| `tccgen.c` | `do_unittest()` (10769) | template only; unchanged |
| `tccgen.c` | `gen_function()` (10204) | M3: pop to saved stack tops instead of NULL |
| `tccgen.c` | `tccgen_compile()` (435) | free any leftover snapshot on error paths |
| `tccrun.c` | new | `tcc_snapshot_new()`, `tcc_snapshot_free()` |
| `tccrun.c` | new | `tcc_relocate_snapshot()`, `ctfe_call()`, `ctfe_exit_stub()` |
| `tccrun.c` | `tcc_relocate_ex()` (330) | extract the prepare/cleanup steps so snapshot mode can skip them |
| `tccelf.c` | `tcc_add_runtime()` (1827) | make repeat calls a no-op via `ctfe_runtime_added` |
| `tccelf.c` | `build_got_entries()` (1422) | honour `got_built`, or prove it is idempotent and document that |
| `tcc.h` | `TCCState` | new flags and the `ctfe_ctx` field |
| `tcc.h` | internal decls | `tcc_snapshot_new/free`, `tcc_relocate_snapshot` |
| `libtcc.c` | `tcc_parse_args()` | `-fno-ctfe`, `-fctfe-timeout=` (M4) |
| `libtcc.c` | `TCC_OPTION_lang` (2083) | set `ctfe = 1` alongside `lang_d = 1` |
| `README.md` | D extensions (234) | document `$run`, its scope limits and its determinism caveats |

## Milestones

**M1 -- file-scope scalar CTFE.**  `$run(expr)` in constant
expressions and `$run { ... }` at file scope; integer, `_Bool` and
floating results; snapshot relocation with crash recovery;
`-fno-ctfe`; diagnostics for pointers, aggregates, block scope, TLS
and cross compilers; README entry.

**M2 -- aggregates, strings and write-back.**  Struct/union results
via an out-parameter and a hidden rodata blob; pointer results via
provenance mapping; per-symbol write-back for the statement form;
`libtcc1` helpers (`memcpy`, `memset`, `__divdi3`, ...) callable
inside payloads; `-b` policy (either support `bcheck_run.o` in the
snapshot or reject `$run` under `-b`).

**M3 -- block scope.**  Allow `$run` inside function bodies, which
requires `gen_function()` to pop only down to saved stack tops
(`local_stack`, `global_label_stack`, `all_cleanups`) and to restore
the enclosing function's codegen state, plus access to enclosing
locals.  The simplest semantics are "payload sees file scope plus
compile-time-constant locals"; a full closure over runtime locals is
out of scope.

**M4 -- ergonomics and safety.**  `#run` alias via a new
preprocessor directive; `-fctfe-timeout=`; `@ctfe` function
attribute; `version(CTFE)` / `__ctfe`; `-Wctfe` determinism warnings;
caching of results keyed on the payload's tokens and argument bytes;
optional trimming of the dead wrapper from the output.

## Test plan

New tests go in `tests/tests2/`, starting at the next free prefix
`398`, following the conventions in that `Makefile`
(`FLAGS += -lang=d`, `GEN = $(GEN-TCC)` for D tests, `-dt` for
multi-snippet error tests):

| file | asserts |
| --- | --- |
| `398_d_ctfe_scalar.c` | `$run` of arithmetic, helper calls, loops, integer and floating results; use in an array bound, an `enum`, a `static assert` and `$assert` |
| `399_d_ctfe_statement.c` | `$run { ... }` at file scope; `$assert` inside the payload; `printf` from the payload reaching the compiler's stdout |
| `400_d_ctfe_error.c` (`-dt`) | pointer result, aggregate result (M1), block scope, cross-compiler/unavailable-host message, payload calling `exit()` |
| `401_d_ctfe_scope.c` (`-dt`) | `$run` rejected inside a function body in M1, accepted in M3 |
| `402_d_ctfe_aggregate.c` | M2: struct/union results, string-literal results, `memcpy` in a payload |
| `403_d_ctfe_crash.c` (`-dt`) | payload divides by zero / dereferences NULL: compile error, not a compiler crash |
| `404_d_ctfe_writeback.c` | M2: `$run { g = 42; }` changes the initialiser emitted for `g` |
| `405_d_ctfe_interaction.c` | `$run` next to `unittest`, `version`, `$assert`, and inside a macro expansion; a second `$run` in the same TU |

Additional coverage:

- A `tests/libtcc_test.c` case that compiles a string containing
  `$run` through the library API and checks the resulting constant,
  to prove the feature does not depend on the `tcc` driver.
- `make -j$(nproc) test` must pass with `-lang=c` sources that use
  `$run` as an ordinary identifier (no regression in
  `-fdollars-in-identifiers` handling).
- The PGO benchmark must not regress: `make pgo PGO_CC=clang` and
  compare Mlines/s and Valgrind `I refs` with `bench/pgo-history.tsv`.

## Risks and limitations

- **`build_got_entries()` re-entrancy.**  It rewrites `r_info` and
  adds GOT/PLT entries; if it is not idempotent, the second call (the
  real link after a `$run`) would corrupt relocations.  Mitigation:
  the `got_built` flag, plus a test that mixes `$run` with `-run` and
  with `-o` in one TU.
- **`tcc_add_runtime()` re-entrancy.**  Adding `libtcc1.a` twice
  duplicates symbols; the flag is mandatory, not an optimisation.
- **Host privileges.**  A payload runs with the compiler's
  privileges: it can open files, read the environment, `fork()`, or
  `exit()`.  `exit`/`abort` are intercepted; the rest is documented
  as "`$run` is a compile-time side-effecting construct", the same
  stance Jai and C++26 `constexpr` callbacks take.  `@pure`/`@det`
  (see `deterministic_attribute.md`) is the recommended way to keep
  payloads reproducible.
- **Determinism.**  A payload that reads the clock, the environment
  or an address makes the build non-reproducible.  `-Wctfe` (M4) is
  the diagnostic; caching (M4) must be off by default for
  non-`@det` payloads.
- **`long double` ABI.**  Reading an x87 80-bit return value from
  generated code into the compiler process assumes host and target
  agree.  Gate on `TCC_IS_NATIVE` and reject `long double` results on
  targets where tcc's representation differs from the host's.
- **Debug info and dumps.**  The wrapper must not appear in DWARF or
  in `-fdump-decls` output; suppress `debug_modes` and skip
  `psyms()` for CTFE symbols.
- **`.text` growth.**  Every `$run` leaves a dead wrapper behind.
  For a compiler that advertises tiny output this is a real cost, and
  M4 should either trim it or compile the wrapper directly into the
  snapshot (which requires the parser to run against the clone, with
  the symbol-transaction rollback described under *Alternatives*).
- **Hangs.**  An infinite loop in a payload hangs the compiler, so
  `-fctfe-timeout` is not a nicety.  Until M4 lands, document the
  risk and keep the `setjmp` path ready for the timeout signal.

## Open questions

1. Should the statement form have a *scope* rule (file scope only) or
   also be allowed inside `unittest` blocks, where running code at
   compile time is natural?
2. Does `$run(expr)` need an explicit type annotation for `auto`
   inference, e.g. `$run(expr : T)`?
3. Should `$run` results participate in `-fconstant-folding`
   diagnostics when the payload is impure, or is a plain warning
   enough?
4. Should the wrapper be trimmed after the run (restore
   `data_offset`, truncate `.rel.text`, mark the `ElfSym` `SHN_UNDEF`)
   or compiled into the snapshot in the first place?
5. Is `#run` worth the preprocessor change, or does `$run` alone
   cover the ergonomics?
6. Should CTFE be able to call `@pure` functions only by default,
   with `@system`/`-fno-ctfe-purity` as the escape hatch?

## Alternatives considered

- **Interpreter over the token stream / AST.**  No ABI drift, but it
  needs a second semantics for every operator, struct layout and
  calling convention, and it cannot reuse `libtcc1` helpers.  This is
  the option the task explicitly rejects.
- **Compile the payload into a fresh `TCCState`.**  Would avoid the
  dead-code residue, but the payload's symbols live in the parent's
  symtab and `table_ident`; compiling against a clone means either
  re-resolving every name or rolling the shared symbol tables back
  after the wrapper is generated.  Worth revisiting in M4.
- **Relocate the parent in place and restore afterwards.**  Cheaper
  than a clone, but every mutated field must be restored by hand:
  section `data`, `sh_addr`, `data_offset`, relocation `r_offset`,
  symtab `st_value`, `sym_attrs`, `qrel`, plus the freed sections at
  `copy == 3`.  One missed field corrupts the output silently, which
  is a bad trade for a compiler whose main selling point is
  correctness on small inputs.
- **Defer all `$run`s to the end of the TU** (the
  `gen_inline_functions()` pattern at `tccgen.c:10283`).  Impossible
  for the expression form, because array bounds and `enum` values
  need the constant during parsing.
- **Shell out to a second `tcc -run` process.**  Simple and fully
  isolated, but it loses the current TU's types and globals, needs a
  temporary directory and a serialisation format, and makes every
  `$run` cost a process spawn.

## Acceptance criteria

- `-lang=d` accepts `$run(expr)` in constant expressions and
  `$run { ... }` at file scope; `-lang=c` does not.
- `$run` of an integer, `_Bool` or floating expression yields a value
  identical to the same code run at program start.
- An array bound, `enum` value, `static assert` and `$assert` can all
  consume a `$run` result.
- Two `$run`s in one TU both work, and a `$run` followed by `-run` or
  `-o` produces correct output.
- A payload that divides by zero, dereferences NULL, calls `exit()`
  or loops past the timeout produces a compile error naming the
  `$run` site, and the compiler exits with a nonzero status instead
  of crashing or hanging.
- Pointer and aggregate results are rejected with an actionable
  message in M1 and supported in M2.
- `-fno-ctfe` turns every `$run` into an error.
- `./configure && make -j$(nproc) -B && make -j$(nproc) test` passes,
  and `make pgo PGO_CC=clang` shows no regression in Mlines/s or
  Valgrind instruction count against `bench/pgo-history.tsv`.
