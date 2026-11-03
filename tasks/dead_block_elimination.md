# Task: Dead/Unreachable Block Elimination

> **Status:** Proposed.

Remove code that can never execute so the output is smaller and the
instruction cache is used better. The generator is one-pass, so the
simplest correct design is a *forward reachability filter* on the
emitted byte stream of each function, not a full CFG rebuild.

## Goal

- Drop instructions that follow an unconditional `jmp`/`ret`/`ud2`
  and have no label targeting them.
- Drop an `else` branch whose `then` branch always transfers control.
- Drop a whole function body that is provably unreachable (optional,
  M2).
- Preserve jump targets that are reached by `switch` tables,
  computed labels, or `.init_array` entries.

## Non-goals

- Cross-function dead-code elimination (the linker already does most
  of it for statics; M2 may revisit).
- Loop-invariant or value-based dead-code elimination.
- Rewriting the parser to avoid generating the code in the first
  place.

## Reachability model

Because labels can be emitted out of order, collect a bitmap of
targeted code offsets before filtering:

1. While emitting, record every branch target and every label offset
   in a per-function sorted vector.
2. After the function is complete, walk the byte stream:
   - `reachable = 1` at function entry.
   - At an unconditional terminator (`jmp`, `ret`, `ud2`, `hlt`),
     `reachable = 0` until the next offset in the target set.
   - At a label offset, `reachable = 1`.
   - Copy only reachable instruction bytes to the output.
3. Rewrite relative branch displacements after the shrink. Options:
   - Use the existing relocation/label fixup mechanism so labels are
     resolved after filtering; or
   - Do the filter before label resolution by keeping logical labels
     and their final offsets.

The second option is cheaper if the backend already defers branch
displacements; check `gen_jmp` / `gsym_addr` in the target backend.

## Implementation steps

1. Add a per-function `targets` vector and `reachable` state to the
   backend's function context.
2. Record targets from `gjmp()`, `gsym_addr()`, `gen_jmp_ez()`,
   `jmp_offsets`, jump tables, and any `setjmp`/exception landing
   pads.
3. Add a `dead_block_filter()` that runs at function end, before the
   bytes are appended to `.text`.
4. Handle architecture length decoding:
   - x86-64: decode instruction lengths with the same table the
     disassembler uses, or record `(offset, length)` at emit time
     (preferred; avoids a second decoder).
   - ARM64: fixed 4-byte instructions, trivial to walk.
   - i386/ARM: same idea as x86-64/AArch64 respectively.
5. Recompute branch displacements only for branches that cross a
   removed region.
6. Add `-fno-dce` (or reuse `-fno-optimize`) for A/B testing.

## Interaction with debug info

- If line tables are emitted per instruction, apply the same filter to
  the line entries.
- Never drop an instruction that carries a `DW_LNE_set_address` or
  other marker used by the debugger to anchor a scope.

## Test plan

| file | asserts |
| --- | --- |
| `NN_dce_after_return.c` | code after `return` is not emitted; program still runs |
| `NN_dce_after_goto.c` | unreachable code after `goto` is dropped |
| `NN_dce_if_else.c` | unreachable `else` after an always-returning `then` |
| `NN_dce_labels.c` | a label targeted only from a jump table stays reachable |
| `NN_dce_debug.c` | line info for reachable code is intact |
| `NN_dce_switch.c` | dense/sparse `switch` targets are preserved |

Compare disassembly (`objdump -d`) before/after on the same input and
confirm no `tests/` regression.

## Performance guardrails

- Filtering is linear in emitted bytes; it must not add a second
  instruction decode pass on x86-64 if lengths are recorded at emit
  time.
- Run `ninja -C build pgo-bench` with and without the filter.
- Acceptance: Mlines/s unchanged within noise and Valgrind `I refs`
  no worse than the pre-change value.

## Acceptance criteria

- [ ] Unreachable blocks are removed on x86-64.
- [ ] All jump/switch targets remain correct.
- [ ] Debug line info stays valid.
- [ ] No `pgo-bench` regression.
- [ ] `-fno-dce` disables the filter.
