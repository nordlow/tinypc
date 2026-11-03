# Task: Automatic Tree-Sitter `grammar.js` Synchronization with C/D Parser

## Goal

Automate the generation, updating, and validation of Tree-sitter grammar
(`grammar.js`) in sync with extensions and additions to TinyPC's C and D
parser (e.g. tuples, slices, `foreach`, destructuring assignment, scope
guards, attributes).

## Scope & Requirements

1. **Parser-Grammar Alignment:**
   -  Map TPC-specific language extensions (tuples `(T1, T2)`, slices `T[]`,
     destructuring assignment `(a, b) = expr`, `foreach` loops, attributes,
     etc.) into Tree-sitter node rules in `grammar.js`.
   -  Maintain bidirectional traceability or generation scripts between
     `tccgen.c` token/parser states and Tree-sitter grammar productions.

2. **Automated Sync / Codegen:**
   -  Add a build target or helper script (e.g., `make treesitter-sync` or
     `tools/sync-grammar`) that inspects parser additions or tests and
     validates `grammar.js` coverage.
   -  Ensure CI / test suite verifies that all syntax constructs parsed by
     `tcc` / `tpc` have corresponding valid Tree-sitter rules.

3. **Acceptance Criteria:**
   - [ ] **P1:** Tree-sitter `grammar.js` fully supports TPC's C and D extensions
     (tuples, slices, destructuring assignment, scope guards).
   - [ ] **P2:** Automated build check / test ensuring grammar is in sync with
     parser changes.
   - [ ] **Verification:** `tree-sitter parse` successfully parses comprehensive
     test files under `tests/tests2/`.
