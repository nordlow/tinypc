# Task: Advanced Type-Aware General Formatting Engine

## Goal

Design and specify an advanced, type-aware general-purpose formatting engine (supporting `printf`, `fprintf`, debugging logs, and compiler diagnostics) incorporating custom type inspection, sequence bounds, symbol metadata vs. value distinction, and modern interpolation syntax.

## Motivation

Standard C `printf` and formatting utilities lack native awareness of compiler internal data structures (`CType`, `Sym`, registers, AST nodes, D-style ranges). A robust, general-purpose type-aware formatting engine enables clean, unified formatting across debugging output, logging, pretty-printing, and compiler diagnostics (`tcc_error`, `tcc_warning`).

---

## Proposed Formatting Extensions & Syntax

### 1. Domain-Specific Type Specifiers
- **`%T` (`CType *`)**: Human-readable type representation (e.g., `const int *`, `(int, double)`).
- **`%S` (`Sym *`)**: Fully qualified symbol name and scope metadata (Symbol Description).
- **`%V` (`Sym *` or evaluated expression)**: Evaluated runtime value or contents of a symbol (Symbol Value).
- **`%K` (`int tok`)**: Token string mnemonic (e.g., `TOK_DARROW` -> `=>`).
- **`%R` (`int reg`)**: Target register name (e.g., `rax`, `x0`).
- **`%A` (`asm_operand_t *`)**: Assembler operand constraints and addressing mode.
- **`%F` (`Sym *fn_sym`)**: Function safety, purity, and attribute qualifiers (`@pure`, `@safe`).

### 2. Modern Interpolation & Escaping (`%%`, `{}`, `%?`, `%{}`)
- **`%%`**: Literal escape for `%`.
- **`{}`**: Type-inferred interpolation via C11 `_Generic`, automatically invoking the correct type formatter without explicit format verbs.
- **`%?`**: Conditional / optional formatting (renders argument only if non-null/truthy, preventing null pointer formatting errors).
- **`%{}`**: Named/modifier interpolation (supports formatting modifiers and field access, e.g., `%{hex}val` or `%{sym.name}`).

### 3. Bounded Sequence Formatting (Arrays, Ranges, Iterators)
- **`%[N]s` / `%.Ns`**: Formats up to $N$ elements of an array, slice, range, or iterator, appending `...` if truncated to prevent log spam.

---

## Precedents & Design Inspiration

- **GNU MP (`gmp_printf`)**: Type-prefix classes (e.g., `Zd`, `Zu`) for arbitrary-precision types.
- **Glibc (`register_printf_specifier`)**: Custom parser and renderer callbacks for domain-specific format verbs.
- **C++20 / Python / Rust**: Colon-separated format modifiers and type-inferred braces.

---

## Implementation Plan

1. **Core Format Engine Extension**:
   - [ ] Implement a general-purpose type-aware formatting parser and renderer API (usable by `printf`, `fprintf`, debugging logs, and diagnostics) supporting extended specifiers (`T`, `S`, `V`, `K`, `R`, `A`, `F`, etc.), bounds (`[N]`), and interpolation braces (`{}`, `%{}`).
2. **Type & Symbol Renderers**:
   - [ ] Implement robust rendering functions for `CType`, `Sym` (description vs. value), registers, and sequences with length limits.
3. **Integration**:
   - [ ] Integrate the general formatting engine across debugging output, pretty-printers, and compiler diagnostics.
