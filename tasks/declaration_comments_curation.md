# Strategy for Adding and Curating Declaration Comments

> **Status:** Proposed process task.

Establish a systematic strategy for adding, updating, and curating
declaration comments (functions, structs, typedefs, globals) across the
codebase by leveraging their actual usage patterns, call graphs, and
structural context within the project.

## Motivation

As a compiler and systems codebase evolves, undocumented internal
helper functions, complex types, and internal APIs accumulate.
Maintaining high-quality declaration comments manually is error-prone
and labour-intensive. Combining codebase usage analysis (static
analysis, call graphs, LSP/codebase-memory tools) with documentation
generators lets developers infer, draft, and curate accurate comments
that reflect how functions and types are actually used.

## Strategy & methodology

### 1. Usage-driven discovery and extraction

- [ ] **Call-graph and reference analysis.** Query codebase memory
      (`search_graph`, `trace_path`) and LSP references to find every
      call site, parameter type combination, and context where a
      symbol is used.
- [ ] **Inference from usage.** Extract expected parameter contracts,
      nullability, error handling, and return-value invariants from
      call-site assertions and usage patterns.
- [ ] **Owner/origin discovery.** For a typedef or macro, record where
      it is defined, which subsystem owns it, and which files include
      it.

### 2. Standardize the documentation format

- [ ] Adopt one doc-comment style. Proposal: a `/** ... */` block
      directly above the declaration, with tags:
      `@brief`, `@param`, `@return`, `@sideeffect`, `@invariant`,
      `@note`.
- [ ] Required sections:
  - [ ] **Summary:** one-line purpose.
  - [ ] **Parameters/invariants:** constraints such as non-null, valid
        ranges, ownership.
  - [ ] **Return value:** meaning of codes/pointers, who owns it.
  - [ ] **Side effects:** global state, allocation, I/O, longjmp.
- [ ] For structs: document field ownership/lifetime.
- [ ] For function pointers: document the callback contract.

### 3. Curating and review workflow

- [ ] **Incremental coverage.** Pilot on `tccgen.c` expression/type
      helpers, then `tccpp.c`, then backends, then `libtcc.c`.
- [ ] **Validation.** A script checks that every documented
      parameter name still exists and that no documented function is
      missing a comment; run it in CI or as a `ninja` target.
- [ ] **Review loop.** Draft comments from usage, then have the
      subsystem owner review; do not merge generated comments
      unreviewed.
- [ ] **Stale-comment policy.** When a signature changes, update the
      comment in the same commit.

## Tooling

- `tools/doc-audit` (shebang, executable): walks a file/translation
  unit, lists undocumented declarations and mismatched `@param` names.
- Optional codebase-memory query recipes stored in
  `docs/doc-analysis.md`.
- No new external dependency unless already present.

## Pilot plan

1. Pick 20 high-fan-in functions in `tccgen.c`.
2. Use `search_graph`/`trace_path` to draft contracts.
3. Review and commit.
4. Measure: count of undocumented symbols before/after.
5. Repeat for the next subsystem.

## Test plan

- `tools/doc-audit` returns non-zero when a documented `@param` no
  longer matches the signature.
- Add a sample fixture with a deliberate mismatch and assert failure.
- Ensure the audit does not flag static functions in generated code.

## Acceptance criteria

- [ ] Documented workflow using codebase memory and static analysis.
- [ ] Guidelines for structuring internal C and D declaration
      comments.
- [ ] `tools/doc-audit` exists and is wired into a check target.
- [ ] Initial pilot pass on key compiler modules.
