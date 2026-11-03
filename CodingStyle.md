# Coding Style

In general, use the same coding style as the surrounding code.

However, do not make any unnecessary changes as that complicates
the VCS (git) history and makes it harder to merge patches. So
do not modify code just to make it conform to a coding style.

## Indentation

Turn on a "fill tabs with spaces" option in your editor.

Remove tabs and trailing spaces from any lines that are modified.

Note that some files are indented with 2 spaces (when they
have large indentation) while most are indented with 4 spaces.

## Spacing

Do not put a space between a function-like keyword and its
parenthesized operand.  In particular, write `version(...)` and
`debug(...)`, not `version (...)` or `debug (...)`.

Put one space after control-flow keywords: `if (...)`, `for (...)`,
`while (...)` and `switch (...)`.

## Language

TPC is implemented in C11 (`-std=gnu11`). Do not use any C features
newer than C11 without updating this file and the build documentation.

## Testing

A simple `make test` is sufficient for some simple changes. However,
before committing a change consider performing some of the following
additional tests:

- Build and run `make test` on several architectures.

- Build with `./configure --enable-cross`.

- If the generation of relocations has been changed, try compiling
  with TPC and linking with GCC/Clang. If the linker has been
  modified, try compiling with GCC/Clang and linking with TPC.

- Test with ASan/UBSan to detect memory corruption and undefined behaviour:

  ```sh
  make clean
  ./configure
  make
  make test
  cp libtcc.a libtcc.a.hide

  make clean
  ./configure --extra-cflags="-fsanitize=address,undefined -g"
  make
  cp libtcc.a.hide libtcc.a
  make test
  ```

- Test with Valgrind to detect some uses of uninitialised values:

  ```sh
  make clean
  ./configure
  make
  # On Intel, because Valgrind does floating-point arithmetic differently:
  ( cd tests && gcc -I.. tcctest.c && valgrind -q ./a.out > test.ref )
  make test TCC="valgrind -q --leak-check=full `pwd`/tcc -B`pwd` -I`pwd`"
  ```

  (Because of how VLAs are implemented, invalid reads are expected
  with `79_vla_continue`.)
