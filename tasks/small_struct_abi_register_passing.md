# Task: Small Struct ABI Register Passing

> **Status:** Proposed.

Pass and return small structs/unions in registers according to the
platform ABI instead of always using memory. This changes only the
call ABI, not struct layout, so it is a type-driven, compile-time
decision.

## Goal

- Match the SysV x86-64 and AArch64 AAPCS rules for structs of up to
  16 bytes (x86-64) and the corresponding small-aggregate classes on
  AArch64.
- Keep the change local to argument classification and the call/
  return emitters.
- Never change `sizeof`, alignment, or field layout.

## ABI rules to implement

### x86-64 SysV

Classify each eightbyte of the aggregate into `INTEGER`, `SSE`, or
`MEMORY`:

- Field types `float`, `double`, `_Complex float`,
  `_Complex double` -> `SSE`.
- Integer, pointer, enum, and small aggregates -> `INTEGER`.
- More than two eightbytes, unaligned fields, or a `MEMORY` field ->
  pass in memory.
- `INTEGER` eightbytes go in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`;
  `SSE` eightbytes go in `xmm0`–`xmm7`.
- Return: one `INTEGER` eightbyte -> `rax`; one `SSE` -> `xmm0`;
  two eightbytes -> `rax`+`rdx` or `xmm0`+`xmm1` or the mixed pair.

### AArch64 AAPCS

- Aggregates up to 16 bytes are passed in `x0`–`x7` or `v0`–`v7`
  depending on field classes; larger aggregates are passed by
  reference.
- Homogeneous floating-point aggregates (HFA) of up to four members
  use consecutive SIMD registers.
- Return: `x0`/`x1` or `v0`–`v3` for HFAs; larger aggregates use an
  indirect result pointer in `x8`.

## Where the code lives

- Argument classification and placement:
  - `x86_64-gen.c`: `gfunc_call()`, `gfunc_prolog()`,
    `gfunc_epilog()`, `load()`, `store()`.
  - `arm64-gen.c`: the same functions.
- Type walking helpers: reuse `struct_layout()` / field iteration in
  `tccgen.c` rather than adding a second layout pass.
- The existing `VT_STRUCT` argument path currently forces memory
  passing; locate it in `gfunc_call()` and the parameter loop in
  `gfunc_prolog()`.

## Implementation steps

1. Add a classifier next to the backend:
   `int classify_aggregate(CType *t, int *classes, int max)`.
   - Return the number of eightbytes/registers, or 0 for memory.
2. Use the classifier in `gfunc_prolog()`:
   - Assign incoming registers to parameters in order.
   - For memory-passed aggregates, keep the current stack copy.
3. Use the same classifier in `gfunc_call()`:
   - Load register-class eightbytes with `load()`/`store()` into the
     argument registers.
   - Keep stack arguments 8-byte aligned.
4. Handle returns:
   - Hidden return-pointer parameter for memory-class results.
   - Register result for small aggregates.
5. Keep the C variadic path conservative: aggregates in variadic
   calls stay memory-passed unless the ABI requires otherwise.
6. Update bounds-check (`-b`) and backtrace (`-bt`) paths that assume
   aggregates are on the stack.
7. Repeat on AArch64, then leave i386/ARM as-is (their ABIs differ).

## Edge cases

- Packed structs (`__attribute__((packed))`) and bitfields:
  classify conservatively as `MEMORY`.
- Empty structs and zero-size unions.
- Structs containing arrays or nested aggregates.
- Structs containing `long double` (x87 class) -> memory on x86-64.
- Structs larger than 16 bytes.
- Functions with more arguments than available registers: spill the
  rest to the stack in ABI order.
- Calls through function pointers and old-style/K&R declarations.

## Test plan

Add ABI tests under `tests/tests2/` plus a disassembly check where
possible:

| file | asserts |
| --- | --- |
| `NN_abi_small_struct_arg.c` | 8/16-byte struct passed in registers, callee sees correct fields |
| `NN_abi_small_struct_ret.c` | 8/16-byte struct returned in registers |
| `NN_abi_mixed_class.c` | `{int; double}` uses the mixed integer/SSE pair |
| `NN_abi_hfa_arm64.c` | AArch64 HFA in `v0`–`v3` |
| `NN_abi_large_struct.c` | >16-byte struct still passed by memory |
| `NN_abi_packed.c` | packed struct stays memory-passed |
| `NN_abi_variadic.c` | variadic calls keep the conservative path |
| `NN_abi_function_pointer.c` | indirect calls use the same classification |

Cross-check against GCC/Clang on the same host: compile a small
struct-passing corpus with both compilers and compare returned values.

## Performance guardrails

- Classification runs once per call/parameter at compile time; it
  must not add a second full type walk per expression.
- Run `ninja -C build pgo-bench` before/after; Mlines/s and
  Valgrind `I refs` must stay within noise.

## Acceptance criteria

- [ ] x86-64 small aggregates pass/return in registers.
- [ ] AArch64 AAPCS classification is correct.
- [ ] Large/packed/variadic aggregates keep memory passing.
- [ ] No ABI or test regressions.
- [ ] No `pgo-bench` regression.
