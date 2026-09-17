// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// C++ runtime bring-up hooks. KoraOS is mostly C, but the vendored Circle USB
// stack (and any future borrowed drivers) are C++, so the kernel provides the
// minimal freestanding C++ runtime in src/cxx/: global-constructor dispatch,
// operator new/delete backed by the frame allocator, and a few __cxa stubs.

#ifdef __cplusplus
extern "C" {
#endif

// Run all C++ global/static constructors (the .init_array table). Call once,
// early in kernel_main, before any C++ object is used.
void cxx_init(void);

// Exercise the C++ toolchain and runtime at boot: static-constructor init,
// virtual dispatch, and a heap allocation via operator new. Logs its results.
void cxx_selftest(void);

#ifdef __cplusplus
}
#endif
