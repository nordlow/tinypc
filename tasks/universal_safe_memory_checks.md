# Activate `@safe` memory checks across all language modes

Enable `@safe` / `@system` / `@trusted` annotations and the `@safe`
memory checks in every language mode, with `-lang=d` mode activating
`@safe` by default.

## Invariants

- [ ] `-lang=d` activates `@safe` by default for all unannotated functions
  unless explicitly marked `@system` or `@trusted`.
- [ ] Unannotated functions in C mode (`FUNC_SAFETY_UNKNOWN`) behave as
  `@system`: pointer arithmetic and indexing remain allowed. No existing C
  program may change behavior.
- [ ] Only functions explicitly marked (or defaulted via `-lang=d`) as `@safe` are
  checked.
- [ ] Slice indexing/slicing remains legal in `@safe`; only raw pointer
  arithmetic/indexing is forbidden.
- [ ] Function attribute inference happens for non-templated functions (as this
  compiler currently has no plans for templates nor type-aware macros, though
  some plan for templates/macros will be added later on).
