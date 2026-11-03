# Strategy for Adding and Curating Declaration Comments

Establish a systematic strategy for adding, updating, and curating declaration comments (functions, structs, typedefs, globals) across the codebase by leveraging their actual usage patterns, call graphs, and structural context within the project.

## Motivation

As a compiler and systems codebase evolves, undocumented internal helper functions, complex types, and internal APIs accumulate. Maintaining high-quality declaration comments manually is error-prone and labor-intensive. By combining codebase usage analysis (via static analysis, call graphs, and LSP/codebase-memory tools) with documentation generators, developers can infer, draft, and curate accurate declaration comments that reflect how functions and types are actually used across the project.

## Strategy & Methodology

### 1. Usage-Driven Discovery and Extraction
- [ ] **Call-Graph & Reference Analysis:** Query codebase memory (`search_graph`, `trace_path`) and LSP references (`xd://lsp`) to find every call site, parameter type combination, and context where a declared symbol is used.
- [ ] **Inference from Usage:** Extract expected parameter contracts, nullability, error handling behavior, and return value invariants from call-site assertions and usage patterns.

### 2. Standardization of Documentation Format
- [ ] Adopt a consistent doc-comment standard (e.g. Doxygen / Javadoc style for C internal functions and public APIs) placed directly above function and type declarations.
- [ ] Required sections in declaration comments:
  - [ ] **Summary:** Concise one-line description of purpose.
  - [ ] **Parameters / Invariants:** Document constraints on arguments (e.g., non-null, valid ranges).
  - [ ] **Return Value:** Meaning of return codes or pointers.
  - [ ] **Side Effects:** Global state modifications, memory allocations, or jump/exception behavior.

### 3. Curating and Review Workflow
- [ ] **Incremental Coverage:** Target core subsystems first (e.g., code generator backends, parser (`tccgen.c`), type system), proceeding outward to library and driver components.
- [ ] **Validation:** Ensure documentation comments remain synchronized with function signatures during refactoring, catching signature mismatches or obsolete comments.

## Acceptance Criteria

- [ ] Documented workflow using codebase memory and static analysis for declaration documentation.
- [ ] Guidelines for structuring internal C and D declaration comments.
- [ ] Initial pilot pass on key compiler modules.
