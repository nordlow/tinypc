# Task: Activate `@safe` memory checks across all language modes

> **Status:** Proposed. Depends on the existing `@safe`/`@system`/
> `@trusted` attribute plumbing and the purity work in
> `pure_attribute_semantics.md`.

Enable `@safe` / `@system` / `@trusted` annotations and the `@safe`
memory checks in every language mode, with `-lang=d` mode activating
`@safe` by default.

## Invariants

- `-lang=d` activates `@safe` by default for all unannotated functions
  unless explicitly marked `@system` or `@trusted`.
- Unannotated functions in C mode (`FUNC_SAFETY_UNKNOWN`) behave as
  `@system`: pointer arithmetic and indexing remain allowed. No
  existing C program may change behaviour.
- Only functions explicitly marked (or defaulted via `-lang=d`) as
  `@safe` are checked.
- Slice indexing/slicing remains legal in `@safe`; only raw pointer
  arithmetic/indexing is forbidden.
- Function attribute inference happens for non-templated functions (as
  this compiler currently has no plans for templates nor type-aware
  macros, though some plan for templates/macros will be added later
  on).

## Current state to inspect

- Attribute parsing: `parse_d_attributes()` and the `AttrDef` table.
- Safety bits already on `Sym`/`CType` (search `FUNC_SAFETY_`).
- Existing D safety tests: `379_d_safety.c`,
  `381_d_safety_pointer_arith.c`, `404_d_safety_inference.c`.
- The D-mode defaulting logic: find where `lang_d` sets function
  defaults and add the safety default there.

## Implementation steps

1. **Defaulting.**
   - In `-lang=d`, set `FUNC_SAFETY_SAFE` for unannotated function
     definitions.
   - Keep `FUNC_SAFETY_UNKNOWN` in C mode.
   - Let `@system` / `@trusted` override the default explicitly.
2. **Inheritance.**
   - Nested blocks inherit the enclosing function's safety unless a
     block attribute overrides it.
   - `@trusted` blocks are checked as `@system` internally but expose
     `@safe` to callers.
3. **Checks.** Inside a `@safe` context reject:
   - raw pointer arithmetic (`p + n`, `p - n`, `p1 - p2`, `++p`,
     `--p`),
   - raw pointer indexing (`p[i]`) where `p` is not a slice/array,
   - pointer/integer casts in both directions,
   - `union` punning and `memcpy`/`memcmp` of pointer bits,
   - calls to `@system` functions unless wrapped in `@trusted`.
4. **Allowances.** Keep legal in `@safe`:
   - slice indexing/slicing (`x[i]`, `x[a..b]`),
   - array indexing,
   - taking addresses of locals for passing to safe functions,
   - `@trusted` escape hatches.
5. **Diagnostics.** Emit a clear error naming the operation and the
   safety context, e.g.
   `"@safe function 'f' may not perform pointer arithmetic"`.
6. **Inference (optional, behind a flag).** Infer `@safe` for simple
   functions in C mode when `-finfer-safe` is given; default off.
7. **Documentation.** Update `README.md` with the mode matrix
   (C unknown/system, D safe-by-default, explicit annotations).

## Edge cases

- Function pointers: a `@safe` function pointer type must not be
  assigned a `@system` target without an explicit cast.
- Variadic functions: the callee body is checked normally.
- Inline functions: check the body once, at definition.
- Templates/macros: out of scope for now; document that inference is
  skipped.
- `extern` functions with no body: treat as `@system` unless
  annotated.
- `main` in D mode is safe by default; document any bootstrap
  exceptions.

## Test plan

| file | asserts |
| --- | --- |
| `NN_safe_default_d.c` | unannotated D function rejects pointer arithmetic |
| `NN_safe_system_override.c` | `@system` allows it |
| `NN_safe_trusted_block.c` | `@trusted` block allows it, callers stay safe |
| `NN_safe_c_unchanged.c` | C mode still allows pointer arithmetic |
| `NN_safe_slice_ok.c` | slice indexing remains legal in `@safe` |
| `NN_safe_fnptr.c` | `@system` target cannot be stored in `@safe` pointer |
| `NN_safe_error_msgs.c` | diagnostics name the operation and context |

Use `-dt` for expected-error tests.

## Acceptance criteria

- [ ] D mode defaults to `@safe` for unannotated functions.
- [ ] C mode behaviour is unchanged.
- [ ] `@system`/`@trusted` overrides work at function and block scope.
- [ ] Slice operations stay legal in `@safe`.
- [ ] Diagnostics are actionable and tested.
