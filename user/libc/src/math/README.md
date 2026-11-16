# src/math: musl's libm

The `<math.h>` functions for `double` and `float` are copied from
[musl](https://musl.libc.org/) 1.2.5 (git commit `0784374d`), which is under
the MIT licence: see [`COPYRIGHT`](COPYRIGHT) here, musl's own file, and the
notices at the top of the individual sources (Sun Microsystems, FreeBSD
contributors, Arm Limited). musl as a whole was not an option for KoraOS,
because it is bound to the Linux syscalls; its libm makes none.

## What is here

- From musl's `src/math`: the C99 functions for `double` and `float`, their
  internal helpers (`__sin.c`, `__rem_pio2_large.c`, `__math_*.c`, ...) and
  tables (`*_data.c`, `*_data.h`).
- From musl's `src/math/aarch64`: the functions AArch64 does in one
  instruction (`sqrt`, `fabs`, `floor`, `ceil`, `trunc`, `round`, `rint`,
  `nearbyint`, `lrint`, `llrint`, `lround`, `llround`, `fma`, `fmax`, `fmin`).
  musl's portable C versions of these are not copied.
- `libm.h` (musl's `src/internal/libm.h`) and `fp_arch.h` (musl's
  `arch/aarch64/fp_arch.h`).

Not copied: the `long double` functions (binary128 arithmetic needs a
compiler-rt, which KoraOS does not have), the functions outside ISO C (Bessel
functions, `exp10`, `sincos`, `lgamma_r`, ...) and `<complex.h>`.

## Local changes

The copied `.c` files are byte for byte musl's, with one exception:

- `acosh.c` and `acoshf.c` are musl's later versions (commits `3e80328` and
  `26c909b`, August 2026): in 1.2.5, `acosh` of a negative number between -2
  and -2^26 returned a number instead of NaN. The host tests found it.

The headers were adapted, each change marked with a `KoraOS:` comment:

- `libm.h` takes the byte order from the compiler instead of `<endian.h>`,
  includes the private `features.h`, and defines `M_PI_2`, which the ISO-only
  `<math.h>` of KoraOS does not.
- `fp_arch.h` is guarded by `__aarch64__`, so the host unit tests build on
  other machines.
- `features.h` is new: it stands in for musl's internal `<features.h>`
  (`hidden`, and a `weak_alias` that creates no alias: KoraOS exports the ISO
  names only).

Written for KoraOS, under its own licence (GPL-3.0-or-later):

- `long_double.c`: the `long double` functions, computing at double
  precision.
- `nexttoward.c`: `nexttoward` and `nexttowardf`, comparing binary128 values
  by their bits.
- `fpclassifyl.c`: the class of a `long double`, behind `fpclassify()` and
  `isnan()`.

## Build

`user/libc/CMakeLists.txt` builds this directory with `-ffp-contract=off`:
no multiply and add are fused into one instruction, as when musl is built
with GCC in ISO mode, so the results are the same on every machine. The
rounding mode is always to nearest (KoraOS has no `<fenv.h>`), and errors are
reported through the floating-point exception flags, never through `errno`.

## Updating

Copy the same files from a newer musl over these, reapply the changes to
`libm.h` and `fp_arch.h`, and run `tests/run-libc-tests.sh`: `math_test`
compares every function with the host's libm, and `libcprobe` holds the
expected bits of some forty results on the target (recompute them if a
function changed on purpose).
