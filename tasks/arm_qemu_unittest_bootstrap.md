# Task: ARM Unit Tests for TinyCC via QEMU User-Mode

## Goal

Make it possible to build the TinyCC ARM cross-compiler on an x86_64
Arch Linux host, compile the existing test suite (`tests/tests2/`,
plus `tests/tcctest.c` if feasible) to ARM Linux executables, and run
them under `qemu-arm` with pass/fail comparison against the existing
`.expect` files.

## Feasibility

Feasible, but with a few corrections to the original draft:

- [ ] **There is no runtime `-target arm` flag.** TinyCC cross-compilers
  are separate binaries built at configure time (`./configure
  --enable-cross && make cross-arm`), which produce `arm-tcc` (and
  variants such as `arm-eabi-tcc` / `arm-vfp-tcc`, depending on the
  tree version).
- [ ] **An ARM sysroot is required.** `qemu-user` only emulates the CPU
  and syscalls. The compiled binaries still need ARM crt files, libc,
  and a dynamic loader. Arch has no official ARM cross-glibc, so use
  an AUR/ALARM toolchain package (e.g. `arm-linux-gnueabihf-glibc`) or
  a manually extracted Arch Linux ARM rootfs.
- [ ] **`tcc -run` cannot work cross-arch.** The test harness must compile
  to an executable first, then execute it under QEMU.
- [ ] **Binfmt is optional.** Invoking `qemu-arm -L <sysroot> ./test`
  explicitly is more reproducible than relying on binfmt registration.

## Prerequisites

1. `sudo pacman -S qemu-user-static qemu-user-static-binfmt` (static
   variant needed only if using binfmt or chroots; plain `qemu-user`
   suffices for explicit invocation).
2. ARM sysroot providing `crt1.o`, `crti.o`, `crtn.o`, `libc.so`,
   `libm`, and `ld-linux-armhf.so.3`.
3. Decide on the ABI to test (hard-float EABI vs. softfp) and match it
   to the sysroot. Note that this choice determines which `arm-*-tcc`
   variant is built.

## Steps

1. **Build the cross-compiler.** Run `./configure --enable-cross` and
   `make cross-arm`. Record the exact binary name produced.
2. **Smoke test.** Compile a hello-world with `arm-tcc` against the
   sysroot, using the relevant `--sysroot`, `--crtprefix`,
   `--libpaths`, `--elfinterp` options (or configure
   equivalents). Confirm with `file` that it is an ARM ELF, then run
   `qemu-arm -L $SYSROOT ./hello`.
3. **Add a runner.** In `tests/tests2/Makefile` (or an included
   fragment), add a variable-driven mode, e.g. `make test-arm`, that:
   - [ ] compiles each `*.c` with the ARM cross-compiler to an executable,
   - [ ] runs it with `qemu-arm -L $(ARM_SYSROOT)`,
   - [ ] diffs output against the `.expect` file,
   - [ ] honors existing `TESTS=`/`FILTER=` conventions.
4. **Handle exclusions.** Create an explicit skip list for tests that
   can't work cross or under emulation: `-run`/`-dt` based tests,
   tests that depend on host paths, `dlopen`, or tcc-in-tcc
   behavior. Each skip needs a one-line reason.
5. **Make it configurable.** Expose `ARM_TCC`, `ARM_SYSROOT`,
   `QEMU_ARM` as overridable make variables with sensible defaults,
   and fail with a clear message if any is missing.
6. **Document.** Add a short README section (or `tests/README-arm.md`)
   with the Arch install commands and the sysroot setup.

## Acceptance Criteria

- [ ] `make test-arm ARM_SYSROOT=/path/to/sysroot` runs from a clean
  checkout after the documented setup.
- [ ] The hello-world smoke test passes.
- [ ] Every test in `tests2/` is either passing or on the documented skip
  list. Failures are triaged as either real ARM backend bugs or
  harness issues.
- [ ] Missing tools or sysroot produce a readable error, not a cryptic
  compile failure.
- [ ] No change to the behavior of the existing native `make test`.

## Out of Scope

- [ ] ARM64, RISC-V, and other targets (follow-up, since the same harness
  should generalize).
- [ ] Fixing ARM backend bugs uncovered by the tests (file separately).
- [ ] CI integration.

## Risks and Open Questions

- [ ] Which ARM ABI variant (hard-float vs. soft-float) is the primary target?
- [ ] Does the current tree's ARM backend still pass `tcctest.c`? Failures
  here may predate this task.
- [ ] QEMU user-mode quirks (signals, threads, `/proc`) may cause spurious
  failures; distinguish these from real bugs.

I can also draft the Makefile fragment for step 3 if you'd like.
