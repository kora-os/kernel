// SPDX-License-Identifier: GPL-3.0-or-later
//
// Boot-time proof that the freestanding C++ toolchain and runtime work end to
// end: a global object built by a static constructor, virtual dispatch through
// a vtable, and a heap allocation via operator new (frame allocator). This is
// scaffolding for the Circle USB stack landing in a later step; it can be
// removed once real C++ drivers exercise the same paths.

#include "arch/cxx.h"

// The kernel's tinyprintf (src/lib/printf.c) exposes `printf` as a macro for
// `tfp_printf`. Its C prototype takes a non-const `char *`, which C++ will not
// accept for string literals; redeclare it const-correct here (extern "C"
// linkage matches by name) and reinstate the familiar `printf` spelling.
extern "C" void tfp_printf(const char *fmt, ...);
#define printf tfp_printf

namespace {

struct Greeter {
    const char *tag;
    Greeter() : tag("static constructor ran") {}
    virtual ~Greeter() = default;
    virtual const char *who() const { return "Greeter (base)"; }
};

struct Derived : Greeter {
    const char *who() const override { return "Derived (virtual dispatch)"; }
};

// Constructed by the .init_array walk in cxx_init(); proves global ctors run.
Greeter g_greeter;

}  // namespace

extern "C" void cxx_selftest(void) {
    printf("[c++] %s\n", g_greeter.tag);

    Derived d;
    const Greeter &ref = d;
    printf("[c++] vtable -> %s\n", ref.who());

    Greeter *heap = new Derived();
    if (heap != nullptr) {
        printf("[c++] heap new -> %s\n", heap->who());
        delete heap;
    } else {
        printf("[c++] heap new returned null\n");
    }
}
