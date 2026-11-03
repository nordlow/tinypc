# Task: Compiler Repo Restructuring for Smaller Coding-Agent Context Windows

## Goal

Propose restructurings of this repository that make it easier for coding
agents to address tasks and todos via smaller context windows, without
reducing the performance of the compiler.

## Instructions

### Context-window cost of the current layout

The compiler implementation is organized as C fragments that are merged
or compiled depending on the `ONE_SOURCE` configuration:

- [ ] `tcc.c` includes fragments (`tccpp.c`, `tccgen.c`, `tccdbg.c`,
  `tccasm.c`, `tccelf.c`, `tccrun.c`) plus per-target
  (`*-gen.c`, `*-link.c`, `*-asm.c`) when `ONE_SOURCE=1`; with
  `ONE_SOURCE=0` the same implementations compile as separate objects
  with cross-TU externs declared across `tcc.h` sections.
- [ ] `tccgen.c` is a monolithic compiler core: 11,497 lines / ~347 KB
  (~87k tokens).
- [ ] Reading the full backend-agnostic compiler (`tcc.c`, `tccpp.c`,
  `tccgen.c`, `tccdbg.c`, `tccasm.c`, `tccelf.c`, `tccrun.c`, `tcc.h`,
  `elf.h`, `dwarf.h`) is roughly 250k tokens — most of a large agent
  context window before task-specific context is added.

### Proposal P1 — Split `tccgen.c` into thematic fragments

Split the 11.5k-line file along functional seams:

1. value stack / code-emission core (`vstack`, `gv`, `gen_op`, compares,
   returns) — backend-facing;
2. expressions (parse/eval, casts, constant folding, slices, helper
   calls);
3. statements & control flow (blocks, decls, loops, switch, VLA, cleanup,
   scopes);
4. types & struct/union/enum layout (type system, compatibility,
   qualifiers, storage);
5. debug & output plumbing (init/compile/finish, symbol/reloc handling,
   debug info).

Cross-fragment calls go through the existing `ST_FUNC` mechanism /
`tcc.h` sections; `tcc.c` `ONE_SOURCE` include lists and Makefile
fragment lists updated.

### Proposal P2 — De-monomize the header include chain

Split payload out of `tcc.h` / `elf.h` / `dwarf.h` / `coff.h` so agents
touching one concern don't load 150k+ byte headers.

### Proposal P3 — Extract shared backend support

Pull register-class / calling-convention / prolog boilerplate out of
duplicated `*-gen.c` files into one shared internal header/fragment.

### Proposal P4 — Test-suite and hygiene cleanup

- [ ] Keep `tests/tests2/` canonical; move legacy `tests/tcctest.c` family
  into a clearly separated legacy path.
- [ ] Ignore generated artifacts inside `tests/tests2/` (`*.exe *.o *.so
  *.tcov *.output *.log`).
- [ ] Resolve naming anomalies (`104+_inline.c`, `120+_alias.c`).

### Proposal P5 — Agent navigation aids

- [ ] Add a module map (`docs/architecture.md` or AGENTS.md section) listing
  each compiler file/fragment → responsibility → key entry points.

### Suggested rollout

1. P5 (docs/index, .gitignore) — immediate, zero code risk.
2. P1 (split `tccgen.c`) — big context-window win; verify with required
   protocol.
3. P2, then P3.
4. P4 (test consolidation) — separate task, mechanical.

### Performance guardrails

- [ ] `./configure && make -j$(nproc) -B && make -j$(nproc) test`
- [ ] `make pgo PGO_CC=clang`; `make pgo-bench` → report Mlines/s + valgrind
  I refs.
- [ ] Baseline on this host: 4.35 Mlines/s, 249,388,455 I refs.
- [ ] `make pgo-record PGO_HISTORY_RANGE=HEAD^..HEAD`.

### Verification performed so far

- [ ] `make -j$(nproc) test`: completes but shows pre-existing failures.
- [ ] `make pgo-bench`: 4.35 Mlines/s, 249,388,455 I refs.

## Related task

- [ ] `refactor_vstack_logic.md` — refactor stack-based value operations
  (`vstack`/`vtop`) in `tccgen.c`; related to Proposal P1.
