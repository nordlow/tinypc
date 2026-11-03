# Show evaluated operand values when a binary-comparison assertion fails

When `assert($L $BOP $R)` fails, where `$BOP` is a binary comparison
operator (`==`, `!=`, `<`, `<=`, `>`, `>=`), print the **evaluated
values** of `$L` and `$R`, not just the source text of the expression.

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

## Requirements

-  Evaluate `$L` and `$R` exactly once (no double evaluation or
  repeated side effects).
-  Use the type's debug/pretty-printer when available, and fall back to
  a default representation otherwise.
-  Keep the passing path free of any formatting cost.

## Bonus

For long sequences that don't fit the screen, highlight the first
differing element (and index) between `$L` and `$R`.

## Open questions

-  Which operators beyond comparisons (e.g. `&&`, `||`) should get
  diagnostics?
