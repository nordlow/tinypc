# Ninja-only build (retire the Makefile)

## Goal

Make Ninja the single build executor and `tools/build` (Python) the single
generator. Remove the GNU Make build once nothing depends on it.

Success looks like this: an agent (or human) needs exactly one command to
build and verify, can never test a stale binary, and gets short, targeted
output when something fails.

## Motivation

- Today there are two build paths. The README calls Make "primary,
  fully-featured" (cross-targets, install, PGO) and Ninja the "fast local
  iteration" path. `ninja -C build test` still delegates to the Make-driven
  test suite.
- `AGENTS.md` carries workarounds for the split: "`test` alone can test a
  stale binary", "`pgo-record` is Make-based, no Ninja equivalent yet", the
  GNU Make `CC` aside. Each one costs agent context and is a place to
  silently verify the wrong thing.
- Every build rule written twice can drift apart.
- Ninja gives incremental builds, `-t commands`, `-t compdb`, `-d explain` and
  `-t targets` for free. A Python generator is easy for an agent to read and
  edit.

## Non-goals

- Switching to CMake, Meson, Bazel or similar (considered and rejected: no
  gain over a Python generator for a project this size, and they fit the
  self-hosting, cross-compiler and PGO structure poorly).
- Changing the compiler sources, language features or test contents.
- Removing any test (see "Preserving Tests" in `AGENTS.md`).

## Plan

Each phase should land as its own commit (or small series) and leave
`./configure --ninja && ninja -C build check` green.

### 0. Audit

- List every Make target and variable that users or CI rely on (`make -qp`,
  `.github/workflows/*`, docs) and every target `ninja -t targets` offers.
- Record the gap in a table at the bottom of this file: Make target, Ninja
  equivalent (or "missing"), phase that closes it.
- Check which parts of `tests/` and `tests/tests2/` still run through Make.

Done when: the gap table exists and is reviewed.

### 1. Self-regenerating `build.ninja`

- Add a Ninja `generator` rule that rebuilds `build.ninja` when `tools/build`,
  `configure`, `config.mak` (or whatever it records) or `VERSION` change.
- Plain `ninja -C build check` must then always be in sync with the sources
  and configuration.
- `./configure` still creates the initial `build/`, but is no longer part of
  the per-iteration command.

Done when: editing `tools/build` and running `ninja -C build` regenerates and
rebuilds without a manual `./configure --ninja`.

Status: done.  The generator edge lives in `tools/build` and Ninja
restarts itself after regenerating `build.ninja`.

### 2. Tests as Ninja edges

- Replace the delegation to the Make-driven suite with a small runner
  (`tools/run-test`, Python) invoked once per `tests/tests2/NN_*.c`.
- One edge per test: inputs are the source, `.expect` file and the `tcc`
  binary; output is a stamp file plus `build/logs/NN.log`.
- Quiet on success; on failure print the test name, a short diff against the
  `.expect` file and the log path, nothing more.
- Keep `TEST=N ninja -C build test-one` and `sani-one` working (or replace with
  `ninja -C build t/NN` and document it in `AGENTS.md`).
- Preserve existing special cases (tests with flags, expected failures,
  self-compile tests that pass `-DTCC_TLS=`).

Done when: `ninja -C build check` runs all existing tests without invoking
`make`, the set of executed tests is identical to before (compare names and
counts), and changing one `.expect` file reruns only that test.

### 3. Hang protection inside the rules

- Wrap test, benchmark and sanitizer rules in `timeout -k 5 <N>` with
  `</dev/null`, with per-rule limits.
- A timed-out test reports "TIMEOUT" and its log path.
- Afterwards, shrink the "Debugging Hangs" prose in `AGENTS.md` (see
  `llm_reasoning_refactoring.md`).

Done when: a deliberately hanging test fails in bounded time without any
manual `timeout` wrapper.

Status: done.  Every long-running rule is wrapped by `guarded()` in
`tools/build`; a timeout prints `TIMEOUT` and fails.  `AGENTS.md` and
`docs/debugging-hangs.md` now point at the built-in wrappers.

### 4. Port the Make-only functionality

Close every "missing" row from the audit, at least:

- cross-compilers and per-target runtime libraries
- `install` with `DESTDIR` support
- documentation (`makeinfo`, `tcc-doc.html`, man page via `texi2pod.pl`)
- `pgo`, `pgo-bench` and `pgo-record` (the latter via `tools/pgo-history`)
- `tcov-test` and `sani` / `sani-one`
- `tests2` variants currently reachable only through Make targets
- Windows (`win32/`) and macOS paths, as far as they are exercised today

Done when: the gap table has no "missing" rows.

### 5. Build profiles in separate directories

- Support `build/` (default), `build-sani/`, `build-pgo/`, `build-cov/`
  (names tentative) so switching profiles never invalidates another.
- Document one-line commands for each in `AGENTS.md`.
- Make sure two worktrees, or two agents, can build concurrently without
  sharing any output path.

### 6. Tooling outputs

- Add a target that writes `compile_commands.json` (`ninja -t compdb`).
- Optionally add a `ninja -C build help` target that prints the documented
  targets, so agents need not guess.

### 7. Self-hosting stages (optional, after the above)

- Model stage1 (host `cc`), stage2 (`tcc` built by stage1) and a stage2-versus-stage3
  comparison as edges.
- Evaluate a fast dev profile that builds the compiler with the previous `tcc`.
  Weigh latency gain against bootstrap risk before enabling it by default.

### 8. Deprecate and remove Make

- Replace the Makefile with a thin shim that forwards common targets to
  `ninja -C build` and prints a deprecation notice.
- Update `README.md` (build instructions, "Make-based equivalents",
  "Performance work" verification sequence), `AGENTS.md`, `CodingStyle.md`,
  `TODO.md`, `.github/workflows/*` and `win32/tcc-win32.txt`.
- After one release cycle (or when CI no longer calls `make`), delete the
  shim and the Make-only helper files.

## Verification

For every phase:

    ./configure --ninja && ninja -C build check
    ninja -C build sani

For phases that touch compiler sources or runtime libraries, also run the
benchmark protocol from `AGENTS.md`. Build rules should not change generated
code, so the instruction count from `pgo-bench` should be unchanged; report
it anyway.

Cross-check each phase against the Make build while it still exists:

- the same `tcc` and `libtcc1.a` bytes (or an explained difference) from
  both build systems
- the same test names and pass/fail results

## Risks and open questions

- Is the Make build the only supported way to produce cross-compilers and
  Windows/macOS builds today? If so, phase 4 is the bulk of the work.
- Distribution packagers may depend on `make install` and `./configure`
  semantics. Decide whether `configure` stays a shell script or becomes
  `configure.py`, and keep `--prefix` and friends compatible.
- Ninja is a new hard build dependency for downstream users. Decide whether
  that is acceptable or whether a `ninja`-less bootstrap path
  (e.g. `tools/build --bootstrap` that compiles with a plain `cc` command line)
  is required.
- Per-test edges depend on the `tcc` binary, so any compiler change reruns
  every test. That is expected; the gain is for tests-only changes.

## Gap table (fill in during phase 0)

| Make target / variable | Ninja equivalent | Phase |
|------------------------|------------------|-------|
|                        |                  |       |
