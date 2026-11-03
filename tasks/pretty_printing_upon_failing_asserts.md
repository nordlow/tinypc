# Task: Show evaluated operand values when a binary-comparison assertion fails

> **Status:** Proposed. Related: existing `$assert` comparison support
> (`tests/tests2/384`–`397`).

When `assert($L $BOP $R)` fails, where `$BOP` is a binary comparison
operator (`==`, `!=`, `<`, `<=`, `>`, `>=`), print the **evaluated
values** of `$L` and `$R`, not just the source text of the expression.

## Goal

- Produce a useful runtime diagnostic without evaluating either
  operand twice.
- Keep the passing path allocation- and formatting-free.
- Reuse the existing assert lowering and type information where
  possible.

## Current behaviour to inspect

- `do_d_assert()` in `tccgen.c` (search for the `$assert` lowering)
  already decomposes a comparison into operands and a token.
- The existing comparison tests (`384`–`397`) show the expected
  syntax and which types are supported.
- Check whether operand values are already kept in temporaries; if so,
  the formatter only needs to read them.

## Output formats

**Inline (default).** Use when both rendered values fit on one line
(suggested limit: 80 columns total, including the message prefix):

```
assert(a == b) failed: 3 != 4
```

Render the *negation* of `$BOP` between the values (`!=` for `==`,
`>=` for `<`, and so on) so the output reads as what actually
happened.

**Stacked.** Use when either value is multi-line, or the inline form
would exceed the width limit (e.g. long arrays, nested structures):

```
assert(xs == ys) failed for

xs: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
ys: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13]
```

Align the values vertically so differences are easy to spot. Label
each line with its source expression (`$L` / `$R`).

## Implementation steps

1. **Preserve operand values.** In the comparison lowering, store both
   operands in hidden temporaries when the assert is active. Reuse the
   temporaries already created for the comparison if they exist;
   otherwise add two `get_temp_local_var()` slots.
2. **Carry type info.** Record the type of each operand so the
   formatter can choose the right printer (integer, float, pointer,
   bool, char, string, slice, struct, tuple).
3. **Formatting helpers.** Add a small set of runtime-safe printers:
   - `assert_fmt_int`, `assert_fmt_uint`, `assert_fmt_float`,
     `assert_fmt_ptr`, `assert_fmt_str`, `assert_fmt_slice`.
   - Each appends to a bounded buffer and returns the new length.
   - No `malloc` on the failure path beyond a fixed stack buffer; if a
     value does not fit, truncate with `...`.
4. **Choose inline vs stacked.** After formatting both values, compare
   the total width and the presence of `\n`; pick the layout.
5. **Emit the call.** On the failure branch, call the formatter with
   the two temporaries and the operator token, then `abort()`/exit as
   today.
6. **Passing path.** Ensure the temporaries and formatter call are
   only emitted on the failure branch, or are dead-code-eliminated.
   Verify with `objdump` that the passing path has no format setup.
7. **Operator negation.** Add a mapping table from `$BOP` to its
   printed negation.
8. **Optional diff highlight (Bonus).** For slices and arrays with the
   same length, find the first differing element and print its index;
   for different lengths, print both lengths.

## Edge cases

- Operands with side effects (function calls, `i++`) must evaluate
  exactly once.
- `float`/`double` formatting: use a stable representation
  (`%g`-like), including `NaN`/`inf`.
- Pointers: print the address; do not dereference.
- Strings: print with quotes and escape control characters.
- Slices/arrays: print length and elements up to a cap.
- Structs/tuples: print fields in declaration order up to a depth
  cap.
- `char` vs `signed char` vs `unsigned char`.
- `_Bool`: print `true`/`false`.
- Very long values: truncate deterministically.
- Nested asserts: no global buffer reuse across frames.

## Test plan

| file | asserts |
| --- | --- |
| `NN_assert_fmt_int.c` | inline integer output, correct negation symbol |
| `NN_assert_fmt_float.c` | float/double and `NaN`/`inf` |
| `NN_assert_fmt_ptr.c` | pointer address, no dereference |
| `NN_assert_fmt_str.c` | quoted, escaped strings |
| `NN_assert_fmt_slice.c` | stacked output and first-diff index |
| `NN_assert_fmt_side_effect.c` | operand evaluated exactly once |
| `NN_assert_fmt_pass.c` | passing assertion emits no formatting |

Use the `$assert` test harness and capture stderr. Add an `objdump`
check for the passing path where practical.

## Open questions

- Which operators beyond comparisons (e.g. `&&`, `||`) should get
  diagnostics?
- Should the formatter be shared with `unittest` failure output?
- Is a user-overridable printer hook (`@print`) worth the complexity?

## Acceptance criteria

- [ ] Failing comparison asserts print evaluated values.
- [ ] Operands are evaluated exactly once.
- [ ] Inline vs stacked selection follows the width/newline rule.
- [ ] Passing assertions pay no formatting cost.
- [ ] Regression tests under `tests/tests2/`.
