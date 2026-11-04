# Performance Notes

> Moved out of `AGENTS.md`.  Dated figures are observations on specific machines, not rules.

## Profiling & PGO Optimization

When evaluating performance or hot spots in the compiler, build `tcc`
using Clang or GCC with maximum preferred optimizations and
Profile-Guided Optimization (PGO) via the automated Makefile target:

```bash
ninja -C build pgo   # honors PGO_CC env var, defaults to cc
```

*(Note: GNU Make assigns a default built-in value to `CC`
 (`cc`/`gcc`), so passing `PGO_CC=...` explicitly is required to
 override the compiler selection).*

This automatically handles instrumented building, training runs
(defaulting to an optimal sweet spot of **25 iterations**), profile
merging, and the final PGO-optimized production build. It then runs
`ninja -C build pgo-bench`, which reports the self-compile throughput
in Mlines/s and the number of executed instructions from Valgrind
cachegrind (`I refs`). `PGO_MARCH` defaults to `native`; on x86,
`-mno-avx512f` is added so that Valgrind can decode the generated
code. Run `ninja -C build pgo-bench` on its own to benchmark an
already-built `tcc`.

*Note (2026-10-24):* On AMD Ryzen AI 7 350, Linux x64, Clang PGO
 (`PGO_CC=clang ninja -C build pgo`, ~5.13M lines/sec) outperforms GCC
 PGO (`PGO_CC=gcc ninja -C build pgo`, ~4.62M lines/sec) by ~11% due
 to LLVM's aggressive profile-guided inlining and optimizer passes.

### Recording benchmark results

Every `pgo-bench` result is recorded, one row per commit and host
compiler (`commit`, `date`, `host_cc`, `Mlines/s`, `instructions`), in
`bench/pgo-history.tsv`:

```bash
# pgo-record is Make-based (worktree-driven); no Ninja equivalent yet:
make pgo-record PGO_HISTORY_RANGE=HEAD^..HEAD
make pgo-record PGO_HISTORY_CC=gcc PGO_HISTORY_RANGE=a1b2d9c9..HEAD
```

`ninja -C build pgo` and `ninja -C build pgo-bench` never write that
file themselves, so scratch builds cannot pollute it. `pgo-record`
builds each commit in a throw-away worktree under
`${TMPDIR:-/tmp}/pgo-history` (the work is done by
`tools/pgo-history`), skips the (commit, compiler) pairs that already
have a row, and always records the base commit of the range as the
pre-change reference point. A `-` in a numeric column means that the
build or the benchmark failed. Numbers are only comparable on the same
machine with the same PGO settings, so keep `PGO_MARCH` and the
compiler versions stable. Until the current performance work settles,
record every commit that touches the compiler sources.

## Key Session Takeaways (2026-10-28)

- **Thread-local compiler state is expensive for Clang:** `bc86ac3c`
  moved the mutable compiler state into TLS (`TCC_TLS`). Clang
  compiles the resulting PIE with the general-dynamic TLS model, so
  almost every access to preprocessor/generator state turns into a
  `__tls_get_addr` call: +11% executed instructions and +6% cycles for
  self-compilation, concentrated in `next_nomacro`, `next`,
  `next_argstream` and `unary`. GCC picks a cheaper TLS model (no
  `__tls_get_addr`) and pays only ~1%. Building with
  `-ftls-model=initial-exec` removes those calls and recovers ~8% of
  the 11%.
- **`pgo-bench` throughput has 1 ms resolution**, which is ~5% of a
  ~20 ms self-compile: single readings cannot resolve smaller changes.
  Use the Valgrind instruction count (exact) or `perf stat -e cycles`
  for finer comparisons, and compare commits on a fixed input tree,
  since `tcc.c` itself keeps growing.

## Code coverage

- Code coverage (optional, diagnostic): run `ninja -C build tcov-test` to
  produce `build/tcc_c.tcov` (gcov-style report).  Not part of the
  required verification; use when adding tests or auditing coverage.
