# Task: Peephole Optimizations

> **Status:** Proposed.

Add a cheap, local instruction-pattern replacement pass to shrink
generated code without a measurable compile-time regression. Peepholes
run on the already-emitted byte stream of one function, so they cost
O(bytes) per function and need no AST, CFG, or register model.

## Goal

- Remove redundant moves, no-op arithmetic, and duplicate loads/stores
  that the current one-pass generator emits.
- Keep the pass strictly local: a window of a few instructions inside a
  single basic block, never across a label or a branch target.
- Default-on only when the measured `pgo-bench` instruction count does
  not regress.

## Non-goals

- Global register allocation or instruction scheduling.
- Cross-basic-block or cross-function rewrites.
- Target-independent IR; patterns are per backend.

## Where to hook

- The x86-64 backend's instruction emitters live in `x86_64-gen.c`
  (`g()`, `gen_modrm()`, `load()`, `store()`, and the `gen_op*`
  family). Pick the narrowest point that sees both the opcode and its
  operands.
- A practical design is a small post-emit buffer:
  - `static unsigned char peep_buf[N];` plus a count, flushed on every
    label, branch target, function end, and `section` change.
  - Rewrite the buffer, then `memcpy` it to the output section.
- Do **not** route the whole backend through the buffer in M1; add it
  only around the instruction sequences listed below so other
  emission paths keep their current shape and speed.

## Candidate patterns (M1)

Each pattern is (match, replace). Byte encodings are illustrative;
verify against the actual emitters before implementing.

1. **`mov r, r` self-move** -> delete.
   - Any ModRM form with `mod=3` and `reg == rm`.
2. **`mov r, r'`; `mov r', r`** -> keep one.
   - Only within the same basic block, no intervening flag dependency.
3. **`add r, 0` / `sub r, 0` / `or r, 0`** -> delete.
   - `or r, 0` still sets flags; delete only when the following
     instruction does not read flags (track a one-bit "flags live"
     state locally).
4. **`imul r, 1`** -> delete (flags are undefined after `imul`, so this
   is safe).
5. **`and r, -1`** -> delete.
6. **`shl r, 0` / `shr r, 0` / `sar r, 0`** -> delete.
7. **`lea r, [r + 0]`** -> `mov r, r` then apply pattern 1.
8. **Duplicate `mov [mem], r` with the same operands and no intervening
   write to `mem` or to `r`** -> delete the second.
9. **`mov r, [mem]`; `mov [mem], r`** -> delete the store when the
   value is provably unchanged and `mem` is not volatile.
10. **`xor r, r` used as zeroing a register already known zero** ->
    delete when the local value-tracking proves it.

Patterns 8–10 need a tiny local value-state map; keep it to registers
written in the current window to stay O(1).

## Implementation steps

1. Add a compile-time-strippable build option, e.g.
   `CONFIG_TCC_PEEPHOLE` in `tcc.h` / `config.h`, so the pass can be
   measured on and off.
2. Introduce the buffer + flush API in the selected backend.
   - `peep_begin()`, `peep_emit(byte)`, `peep_flush()`.
   - Flush on labels, jumps, calls, function end, and data emission.
3. Implement pattern 1 and 3 first (no value tracking). Land and
   measure.
4. Add patterns 2, 4, 5, 6, 7.
5. Add the local value-state map for 8–10 behind the same option.
6. Expose `-fno-peephole` for debugging and A/B measurement.
7. Repeat for other backends only after x86-64 numbers are known.

## Correctness rules

- Never rewrite across a label, a jump target, or a call.
- Never rewrite `volatile` loads/stores.
- Treat instructions that read flags conservatively: after `add`,
  `sub`, `or`, `and`, `xor`, `shl`, `shr`, `sar`, `cmp`, `test`,
  assume flags are live until the next flag writer.
- Keep debug line tables consistent: if the backend emits line info
  per instruction, carry the line of the first removed instruction to
  the replacement.
- Do not rewrite instructions whose ModRM uses RIP-relative or
  segment-prefixed forms unless the pattern explicitly allows it.

## Test plan

New tests under `tests/tests2/`, next free prefix:

| file | asserts |
| --- | --- |
| `NN_peephole_mov.c` | self-move and move-pair removal; result still correct |
| `NN_peephole_arith.c` | `+0`, `*1`, `and -1`, shift-by-0 identities |
| `NN_peephole_flags.c` | deleted `or r,0` is kept when flags are consumed |
| `NN_peephole_volatile.c` | volatile loads/stores are never removed |
| `NN_peephole_debug.c` | line info still points at the right source line |

Additional checks:

- Disassemble a few functions with `objdump -d` and compare
  instruction counts before/after.
- Run the full `./configure --ninja && ninja -C build check`.
- Run `ninja -C build pgo-bench` with and without the pass and record
  both Mlines/s and Valgrind `I refs`.

## Acceptance criteria

- [ ] Selected patterns reduce instruction count on the benchmark
      corpus.
- [ ] No miscompiles in `tests/tests2/` and `tests/`.
- [ ] `pgo-bench` shows no regression in Mlines/s or instruction count
      beyond noise.
- [ ] `-fno-peephole` disables the pass.
