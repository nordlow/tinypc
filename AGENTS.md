# Agent Instructions

## Verification (required)

Whenever you modify files in this repository, you MUST verify that no
regressions are introduced. Choose the command by what you changed:

- **Changes to the compiler or its sources** (anything that affects
  the built `tcc` binary or runtime libraries): run

      ./configure --ninja && ninja -C build && ninja -C build test

  `./configure --ninja` is cheap (~100 ms) and regenerates `build/build.ninja`,
  so running it every iteration guarantees a fresh, in-sync build.

  The Make-based equivalent remains `make -j$(nproc) && make
  -j$(nproc) test`.

  Running `ninja -C build test` (or `make -j$(nproc) test`) alone is NOT
  sufficient, since it can test a stale binary.

- Since `./configure --ninja` is cheap and Ninja is incremental (unchanged
  targets are skipped), the single command above is appropriate for *all*
  changes, including tests-only ones.  There is no need for a separate
  lighter verification path.

- Before committing, also run the sanitizer pass to catch memory and
  undefined-behavior errors:

      ninja -C build sani

  (equivalently `make sani-test`.)  This builds an ASan/UBSan-instrumented
  `tcc_s` and runs the full test suite under it.  Failures here (e.g.
  leaks) must be investigated even if the normal run passes.

- **Compiler performance benchmark** (whenever you changed compiler
  sources): after the regression tests pass, run `make pgo-bench`
  (or `make pgo`) both *without* the changes (e.g. on
  `HEAD^`) and *with* the changes, and include both printed results
  (throughput in Mlines/s and instruction count) in your final
  summary to demonstrate performance impact.

Run the applicable command after every change while
iterating. Incremental builds are fast, so don't batch up edits before
verifying.

## Debugging Hangs (commands that never finish)

Source from
https://aistudio.google.com/prompts/18siGlB75gCZzoguM1UWnd0MUwkGp1dnf
and https://claude.ai/chat/264fa269-d936-4483-a2bf-0655dea22bfc.

**Rule 0: never run a potentially hanging command unbounded.** Always
use `timeout`, redirect stdin, and capture output:

    timeout -k 5 120 make -j$(nproc) test </dev/null >build.log 2>&1; echo "exit=$?"

Exit code 124 means timeout (hang or just slow); 137 means it needed
SIGKILL. For long-running steps, start them in the background and poll
(`tail -f build.log`) rather than blocking.

### Step 1: Which stage and which command?

    make -n test | head -50            # what would run, without running it
    make --trace -j1 test              # print each recipe and why it runs
    make -j1 -Otarget test SHELL='/bin/sh -x'   # trace every recipe line
    make -d test 2>&1 | grep -E 'Must remake|Reading makefile|Updating goal' | head
    sh -x ./configure 2>&1 | ts -s | tail -30   # configure: find the stuck probe
    tail -50 config.log

Always retry with `-j1` first: if the hang disappears, suspect a
jobserver or ordering race rather than a program bug. Repeated "Must
remake makefile" lines, or `Clock skew detected`, point to a makefile
that rebuilds itself forever (`find . -newermt '+1 hour'`, `touch` the
offending file).

Also try `</dev/null`: if that fixes it, something was waiting on stdin
(tests that read stdin, a configure prompt, `$(shell ...)` without input).

### Step 2: Inspect the stuck process (do this while it hangs)

Run the hang in one shell and inspect from another:

    pstree -ap $(pgrep -o make)                          # who is the leaf?
    ps -eo pid,ppid,stat,pcpu,etime,wchan:24,args --forest | grep -E 'make|tcc|sh'
    cat /proc/$PID/{wchan,syscall,status}; ls -l /proc/$PID/fd
    sudo cat /proc/$PID/stack                            # kernel stack
    top -H -p $PID                                       # which thread burns CPU

Reading the signs:
- `STAT=R`, ~100% CPU -> busy loop, go to Step 4 (gdb/perf).
- `STAT=S`, `wchan=pipe_read`/`do_wait`/`futex_wait` -> blocked; look at the
  fds (`ls -l /proc/$PID/fd`) to see which pipe or file, then check
  who holds the other end.
- `STAT=D` -> stuck in the kernel/filesystem, not a program bug.
- `STAT=T` -> stopped by a signal (job control, SIGTTIN from a tty read).

### Step 3: strace, the first-line tool

    strace -f -tt -T -s 200 -o trace.txt timeout 60 make test
    strace -f -p $PID -s 200                  # attach to a running hang
    strace -f -c -p $PID                      # syscall summary
    strace -f -e trace=process,read,write,openat,wait4 -o t.txt ...

Interpretation of the **last** syscall (`tail trace.txt`):
- `read(0, ...)` -> waiting for stdin.
- `wait4(...)` / `waitid` -> parent waiting for a child; follow the child pid.
- `futex(...)` -> thread/lock deadlock; get thread backtraces (gdb).
- `read(N, ...)` on a pipe -> check jobserver fds (`--jobserver-auth=R,W`).
- `poll/select/nanosleep` with timeouts in a loop -> polling loop or sleep.
- **No syscalls at all, CPU pegged** -> pure user-space loop; use gdb/perf.
- Same few syscalls repeating -> a retry loop, look at the arguments
  and `errno` (e.g. `EAGAIN`, `EINTR`).

`ltrace -f -p $PID` shows library calls if strace is too low level.

### Step 4: Find the exact code with gdb

Build with symbols and no optimization for debugging:

`./configure --extra-cflags="-g -O0" && make -j$(nproc) -B`.

    gdb -p $PID -batch -ex 'thread apply all bt full'   # snapshot of a live hang
    gdb --args ./tcc -B. -I. -Iinclude -c foo.c         # run under gdb, Ctrl-C when stuck
    (gdb) bt                       # where
    (gdb) c ; (gdb) Ctrl-C ; bt    # repeat 5-10x: the frames that recur are the loop
    (gdb) up / info locals / p *s / p tok / p file->line_num
    (gdb) watch var                # does the loop variable ever change?
    (gdb) set follow-fork-mode child ; set detach-on-fork off
    (gdb) catch syscall wait4      # break when a syscall is entered
    (gdb) tbreak file.c:LINE ; commands ; silent ; bt 3 ; cont ; end

Poor man's sampler (no interactive session needed):

    for i in 1 2 3 4 5; do gdb -p $PID -batch -ex bt 2>/dev/null | grep '^#' | head -5; echo ---; sleep 0.5; done

If attach fails with "ptrace: Operation not permitted":

`sudo sysctl kernel.yama.ptrace_scope=0`

or run via `sudo`. Grab a core for offline analysis: `gcore $PID`, or
`kill -ABRT $PID` with `ulimit -c unlimited`, then `gdb ./tcc core`.

### Step 5: Statistical profile of a spinning process

    perf top -p $PID
    perf record -g -p $PID -- sleep 5 && perf report --stdio --no-children | head -40

A single function at ~100% is the loop. With valgrind, an infinite loop
shows up as exploding instruction counts:

    timeout 30 valgrind --tool=callgrind ./tcc -B. -I. -Iinclude -c foo.c -o /dev/null
    callgrind_annotate callgrind.out.* | head -30
    # or sample a live run:
    valgrind --tool=callgrind --vgdb=yes ./tcc ... &   # then:
    callgrind_control -b          # backtrace of the running program
    callgrind_control -d          # dump profile so far

Attach `gdb` to a program running under `valgrind` (`valgrind` is slow
but gives memory-error context at the moment of the hang):

    valgrind --vgdb=yes --vgdb-error=0 ./tcc ...    # in terminal 1
    gdb ./tcc -ex 'target remote | vgdb'            # in terminal 2

### Step 6: Memory errors and UB that cause hangs

Corrupted memory or UB often turns into an infinite loop later. Look
for the first error, not the hang:

    valgrind --tool=memcheck --track-origins=yes --error-exitcode=1 ./tcc ...
    valgrind --tool=helgrind ./prog      # lock-order / data races
    valgrind --tool=drd ./prog           # deadlocks in threaded code

Rebuild tcc itself with sanitizers (they abort with a stack trace
instead of looping):

    ./configure --cc=clang --extra-cflags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
                --extra-ldflags="-fsanitize=address,undefined" && make -j$(nproc) -B
    # threads/deadlocks: -fsanitize=thread (separate build, not combinable with ASan)

A hang that appears only at `-O2` and not at `-O0` is a strong hint of
UB (signed overflow, uninitialized variable, strict
aliasing). Compare: `make CFLAGS='-O0 -g'` vs the default, and try
`-fwrapv -fno-strict-aliasing`.

### Step 7: Narrow to a minimal reproducer

**Which commit:**

    git bisect start HEAD <known-good>
    git bisect run sh -c 'make -j$(nproc) -B >/dev/null 2>&1 && timeout 60 make test </dev/null >/dev/null 2>&1'
    # exit 124 (timeout) is treated as "bad" because it is non-zero

**Which test:** run the test files one by one under a timeout and
print the one that hangs:

    for f in tests/tests2/*.c; do
      timeout 10 ./tcc -B. -I. -Iinclude -run "$f" </dev/null >/dev/null 2>&1
      [ $? -eq 124 ] && echo "HANG: $f"
    done

Then shrink it with `creduce`/`cvise` or by hand, using `timeout 5` as
the interestingness test. If the hang is in the code tcc *generated*
(the `-run` JIT code has no symbols), rerun with `-g` and `-bt`/`-b`
(backtrace/bounds checking), or compile to a real executable
(`./tcc -g foo.c -o foo && gdb ./foo`) so gdb sees the symbols.

**Which environment:** compare with a clean checkout in a fresh
directory (`git worktree add /tmp/clean`), `env -i PATH=$PATH make test`
(env vars), `ulimit -a` (limits), and `make -j1` (races). Hangs that only
appear on a tty usually involve stdin; hangs only without a tty usually
involve `/dev/tty` or `isatty` checks.

### One-shot diagnostic helper

Save as `tools/hangdiag`. It runs a command with a time limit and,
if still alive, dumps everything useful before killing it.

Example: `tools/hangdiag 120 make -j$(nproc) test`

### Reporting

When you find a hang, report: the stage and command, process state
(`R`/`S`/`D`), the last syscall or the repeating backtrace, the
minimal reproducer, and the fix. After fixing, run the full
verification sequence above (including

`./configure && make -j$(nproc) -B && make -j$(nproc) test`),

and add a regression test under `tests/tests2/` where the hang was
caused by the compiler.  Never "fix" a hang by only raising a timeout.
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
####### Ancestor
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

## Final check (before declaring a task done)

Before considering a task complete, do a clean reconfigure and full
rebuild to rule out stale build artifacts and configure-time issues:

    ./configure && make -j$(nproc) -B && make -j$(nproc) test

This currently (on an AMD Ryzen AI 7 350 w/ Radeon 860M) takes only
about 3 seconds (`./configure` ~41 ms, full rebuild ~1.5 s), so always
run it as the last step for any task that changed non-test code, and
whenever incremental results look suspicious.

## Profiling & PGO Optimization

When evaluating performance or hot spots in the compiler, build `tcc`
using Clang or GCC with maximum preferred optimizations and
Profile-Guided Optimization (PGO) via the automated Makefile target:

```bash
make pgo PGO_CC=clang
make pgo PGO_CC=gcc
```

*(Note: GNU Make assigns a default built-in value to `CC`
 (`cc`/`gcc`), so passing `PGO_CC=...` explicitly is required to
 override the compiler selection).*

This automatically handles instrumented building, training runs
(defaulting to an optimal sweet spot of **25 iterations**), profile
merging, and the final PGO-optimized production build. It then runs
`make pgo-bench`, which reports the self-compile throughput in
Mlines/s and the number of executed instructions from Valgrind
cachegrind (`I refs`). `PGO_MARCH` defaults to `native`; on x86,
`-mno-avx512f` is added so that Valgrind can decode the generated
code. Run `make pgo-bench` on its own to benchmark an already-built
`tcc`.

*Note (2026-10-24):* On AMD Ryzen AI 7 350, Linux x64, Clang PGO
 (`CC=clang make pgo`, ~5.13M lines/sec) outperforms GCC PGO (`CC=gcc
 make pgo`, ~4.62M lines/sec) by ~11% due to LLVM's aggressive
 profile-guided inlining and optimizer passes.

### Recording benchmark results

Every `pgo-bench` result is recorded, one row per commit and host
compiler (`commit`, `date`, `host_cc`, `Mlines/s`, `instructions`), in
`bench/pgo-history.tsv`:

```bash
make pgo-record PGO_HISTORY_RANGE=HEAD^..HEAD
make pgo-record PGO_HISTORY_CC=gcc PGO_HISTORY_RANGE=a1b2d9c9..HEAD
```

`make pgo` and `make pgo-bench` never write that file themselves, so
scratch builds cannot pollute it. `pgo-record` builds each commit in a
throw-away worktree under `${TMPDIR:-/tmp}/pgo-history` (the work is
done by `tools/pgo-history`), skips the (commit, compiler) pairs that
already have a row, and always records the base commit of the range as
the pre-change reference point. A `-` in a numeric column means that
the build or the benchmark failed. Numbers are only comparable on the
same machine with the same PGO settings, so keep `PGO_MARCH` and the
compiler versions stable. Until the current performance work settles,
record every commit that touches the compiler sources.

## Key Session Takeaways (2026-10-28)

- **Thread-local compiler state is expensive for Clang:** `bc86ac3c`
  moved the mutable compiler state into TLS (`TCC_TLS`). Clang compiles
  the resulting PIE with the general-dynamic TLS model, so almost every
  access to preprocessor/generator state turns into a `__tls_get_addr`
  call: +11% executed instructions and +6% cycles for
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

