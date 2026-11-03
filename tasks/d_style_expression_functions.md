# Task: D-Style Expression Functions (`=>` Syntax) and Representation Analysis

This task defines the syntax, parsing rules, lowering strategy, and
representation analysis for D-style expression functions (single-expression
functions using `=>`) in TinyPC (`-lang=d` mode).

## Motivation

In D and modern systems languages, expression functions allow concise
definition of simple functions without requiring braces and explicit
`return` statements:

```d
auto f(int x) => x + 1;
int add(int a, int b) => a + b;
```

Adding this syntax to TinyPC improves ergonomics for short utility
functions, math helpers, and template/inlined wrappers.

---

## Grammar & Syntax

1. **Function Definition with Fat Arrow (`=>`)**:
   Instead of a compound statement `{ ... }` as the function body, a
   function definition may use the `=>` operator followed by an expression
   and a semicolon:
   ```c
   [storage] return_type function_name(parameters) => expression;
   ```

2. **Inferred Return Type (`auto`)**:
   When combined with `auto`, the return type is deduced from the type of
   the expression on the right-hand side of `=>`.

---

## Representation Analysis: Does it Deserve a Special Representation?

**Decision: No special representation is needed.**

### Rationale:
1. **Syntactic Sugar (Desugaring):**
   An expression function `auto f(int x) => expr;` is semantically identical
   to a standard function with a compound body containing a single return
   statement:
   ```c
   auto f(int x) { return expr; }
   ```
2. **Implementation Strategy:**
   During function definition parsing in `tccgen.c`, when the parser encounters
   `=>` where a function body (`{`) is expected:
   - [ ] Parse the return expression `expr`.
   - [ ] Desugar it internally into an implicit `return expr;` statement inside
     a synthesized compound block.
   - [ ] Hand off the resulting AST node stream to the existing function compiler,
     type checker, return type deduction engine, and code generator.
3. **Benefits of Zero Special Representation:**
   - [ ] **Zero Backend Impact:** Code generators (`x86_64-gen.c`, etc.), ABIs,
     stack frames, and debugging stabs require zero modifications.
   - [ ] **Zero Symbol Table Bloat:** `Sym` and `FuncAttr` do not need extra flags
     or AST node variants.
   - [ ] **Full Feature Reuse:** Automatically inherits support for local variables,
     temporaries, return type inference (`auto`), cleanup guards, and
     debug info from normal functions.

---

## Implementation Plan

1. **Parser Extension (`tccgen.c`):**
   - [ ] In function definition parsing, check for `TOK_DARROW` (`=>`) in place
     of `{`.
   - [ ] Parse the single expression, build an implicit `return` statement node,
     and compile it as a standard function body.
2. **Return Type Inference (`auto`):**
   - [ ] Leverage existing type deduction rules for `auto` return types by evaluating
     the expression's type during parsing/analysis.
3. **Regression Tests:**
   - [ ] Add test cases under `tests/tests2/` covering basic expression functions,
     `auto` return type deduction, and multi-parameter expressions.
