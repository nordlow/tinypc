# Task: Leaf Function Prologue/Epilogue Trimming

> **Status:** Proposed.

Omit frame-pointer setup and unneeded save/restore sequences for
functions that never call another function and never spill to the
stack. This is a local, per-function decision, so it costs O(1) in
addition to the code already emitted.

## Goal

- Detect leaf functions reliably.
- Avoid emitting `push %rbp` / `mov %rsp,%rbp` / `pop %rbp` when the
  frame pointer is not needed.
- Skip unused callee-saved register spills.
- Keep `-fomit-frame-pointer` semantics explicit and debuggable.

## Current behaviour to inspect

- x86-64 prologue/epilogue is emitted in `x86_64-gen.c` around
  `gfunc_prolog()` / `gfunc_epilog()`.
- The frame pointer is also used by:
  - `-b` bounds checking,
  - backtraces (`-bt`),
  - variable-length arrays and `alloca`,
  - `setjmp`/`longjmp` users,
  - debug info (`DW_AT_frame_base`).
- Any of these must force a frame pointer even in a leaf function.

## Leaf detection

Track, per function, while emitting:

- `has_call`: set by `gfunc_call()` and by inline asm that may call.
- `has_vla_or_alloca`: set when a dynamic stack allocation appears.
- `has_bcheck`: set when `-b` is active for this function.
- `needs_unwind`: set for functions with cleanups, `setjmp`, or
  exception handling.
- `uses_frame_pointer`: set when a local is addressed through `%rbp`
  rather than `%rsp` (or when the backend chooses `%rbp` addressing).

A function is trimmable when `!has_call && !has_vla_or_alloca &&
!has_bcheck && !needs_unwind && !uses_frame_pointer`.

## Implementation steps

1. Add the flags above to the per-function codegen context.
2. Set them in the existing emit paths:
   - `gfunc_call()` -> `has_call = 1`;
   - VLA/`alloca` path -> `has_vla_or_alloca = 1`;
   - bounds-check emission -> `has_bcheck = 1`;
   - cleanup/`setjmp` paths -> `needs_unwind = 1`.
3. Emit the prologue in two stages:
   - Reserve a placeholder or defer emission until the body is
     generated when the backend supports it.
   - If deferral is too invasive, emit the full prologue and patch it
     out at function end, adjusting any `%rbp`-relative references
     (only safe when `uses_frame_pointer == 0`).
4. Make callee-saved spill decisions use the same flags so unused
   registers are not saved.
5. Keep `-fno-omit-frame-pointer` as a hard override.
6. Emit `CFI` directives only for the frame actually used.

## Safety rules

- Never omit the frame pointer under `-b`, `-bt`, or when a
  `setjmp`/cleanup is present.
- Never trim a function whose address is taken and used with
  `backtrace()`; if this matters, gate trimming on the absence of
  `-g`/`-bt`.
- Keep stack alignment correct after removing `push %rbp`
  (x86-64 SysV requires 16-byte alignment at calls; a leaf function
  with no calls still needs correct alignment for SSE spills).
- Do not trim `main` when the runtime expects a frame.

## Test plan

| file | asserts |
| --- | --- |
| `NN_leaf_no_frame.c` | a leaf function omits `push %rbp` in disassembly and still returns the right value |
| `NN_leaf_calls.c` | a calling function keeps its frame |
| `NN_leaf_vla.c` | VLA/alloca forces a frame |
| `NN_leaf_bcheck.c` | `-b` forces a frame |
| `NN_leaf_setjmp.c` | `setjmp`/cleanup forces a frame |
| `NN_leaf_debug.c` | `-g` line info and unwinding remain valid |

Add a disassembly-based check where the test harness can run
`objdump`; otherwise assert behaviour only and keep the disassembly
check as a manual step.

## Performance guardrails

- The extra bookkeeping is a handful of integer flags per function.
- Run `ninja -C build pgo-bench` with and without the change; both
  Mlines/s and Valgrind `I refs` must stay within noise.

## Acceptance criteria

- [ ] Trimmable leaf functions omit the frame pointer on x86-64.
- [ ] Calling, VLA, `-b`, `setjmp`, and cleanup functions keep it.
- [ ] `-fno-omit-frame-pointer` restores the old prologue.
- [ ] No test or benchmark regression.
