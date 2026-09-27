# Tests

## Host unit tests (`tests/host/`)

Pure-logic kernel code (code that does not touch hardware registers) is compiled
with the **host** compiler and run directly on your machine, under
AddressSanitizer and UndefinedBehaviorSanitizer. No cross toolchain, QEMU or
Raspberry Pi is needed, and a run takes a few seconds.

```bash
tests/run-host-tests.sh
```

Requires `clang` (the default `CC`; Apple's clang works). GCC is not supported:
it rejects the kernel's own `strlen` declaration under `-Werror`.

### Writing a test

- Add `tests/host/<name>_test.c` and a `run_test <name> <kernel sources...>` line
  in `tests/run-host-tests.sh`.
- Include `test.h` and the kernel header under test. Do **not** include system
  headers: the kernel's `common.h` defines its own fixed-width types, which
  clash with `<stdint.h>`. `test.h` declares the few libc functions tests need.
- Provide stubs for kernel services the code under test calls (for example the
  frame allocator), in the test file itself.
- Use `CHECK(cond, "message %d", ...)` and end the file with
  `TEST_MAIN(test_a, test_b, ...)`.

Current suites:

| Test | Covers |
|------|--------|
| `term_test.c` | `src/video/term.c`: autowrap, scrollback and its view, scroll regions, insert/delete/erase, alternate screen, status replies, colours (16/256/24-bit, bce), UTF-8 and DEC line drawing, tabs, origin mode, cursor style/visibility, OSC, REP |
