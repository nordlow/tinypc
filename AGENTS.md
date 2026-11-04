# Agent Instructions

## Where things are

`docs/architecture.md` maps the build inputs, source fragments and
configuration macros.  `ninja -C build config` prints the effective
configuration; `ninja -C build compdb` writes `compile_commands.json`.

## Verification (required)

After ANY change to this repository, run:

    ./configure --ninja && ninja -C build check

`./configure --ninja` is cheap (~100 ms) and regenerates
`build/build.ninja`, so running it every time guarantees a fresh, in-sync
build.  `ninja -C build test` alone is NOT sufficient, since it can test a
stale binary.  Run the command after every change while iterating, tests-only
changes included.

Before committing, also run the sanitizer pass:

    ninja -C build sani

It builds an ASan/UBSan-instrumented `tcc_s` and runs the full test suite
under it.  Failures here (e.g. leaks) must be investigated even if the normal
run passes.

Single-test shortcuts (a specific tests2 entry):

    TEST=N ninja -C build test-one   # non-sanitized
    TEST=N ninja -C build sani-one   # under ASan/UBSan

**Compiler performance benchmark** (whenever you changed compiler sources):
after the regression tests pass, run `ninja -C build pgo-bench` (or
`ninja -C build pgo`) both *without* the changes (e.g. on `HEAD^`) and *with*
them, and include both printed results (Mlines/s and instruction count) in
your final summary.  Details, PGO options and benchmark caveats are in
`docs/perf-notes.md`.  Do not write to `bench/pgo-history.tsv` unless asked.

## Long-running commands

The Ninja rules for tests, benchmarks and sanitizers already wrap their
commands in `timeout -k 5` and redirect stdin from `/dev/null`, so
`ninja -C build check`, `sani`, `test-one` and `pgo-bench` cannot hang
unboundedly.  A timed-out rule prints `TIMEOUT` and fails.

**Rule 0: never run a potentially hanging command unbounded.** For
direct `make`, compiler or test invocations, use `timeout`, redirect
stdin, and capture output:

    timeout -k 5 120 ninja -C build test </dev/null >build.log 2>&1; echo "exit=$?"

Exit code 124 means timeout (hang or just slow); 137 means it needed
SIGKILL. If a command hangs or never finishes, read
`docs/debugging-hangs.md`; never "fix" a hang by only raising a timeout;
if the compiler caused it, add a regression test under `tests/tests2/`.
For long-running steps, start them in the background and poll
(`tail -f build.log`) rather than blocking.

## Dead code removal

When removing dead code like the `#if 0` branch in:

```c
static long double f3_NaN(void)
{
#if 0
    // ARM's default NaN usually has just the top fraction bit set:
    u128_t x = {  0, 0x7fff800000000000 };
#else
    // GCC's library sets all fraction bits:
    u128_t x = { -1, 0x7fffffffffffffff };
#endif
    return x.f;
}
```

list the motive and affected symbols in the commit message.

## Adding tests

- **Comprehensive Coverage:** Write extensive tests that try to cover
  all aspects of features added (including edge cases, error
  conditions, and valid usage).
- When adding a new test, prefer placing it under `tests/tests2/`
  instead of directly under `tests/`. Follow the existing conventions
  there: use the next free numeric prefix, name the source
  `NN_descriptive_name.c`, and add a matching
  `NN_descriptive_name.expect` file with the expected output.
- Only add a test directly under `tests/` if it cannot be expressed in
  the `tests/tests2/` framework.
- **Preserving Tests:** Never remove existing tests unless explicitly
  approved via a query you propose that proves the test is wrong.
