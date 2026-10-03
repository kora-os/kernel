// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal freestanding C++ runtime for the KoraOS kernel.
//
// The kernel is compiled with -fno-exceptions -fno-rtti -fno-threadsafe-statics,
// so the runtime surface we must supply by hand is small:
//
//   * cxx_init()          -- run C++ global constructors (.init_array).
//   * operator new/delete -- dynamic allocation, backed by the kernel heap
//                            (mm/kmalloc.h): 64-byte aligned, zeroed blocks,
//                            with over-aligned requests honoured exactly.
//   * __cxa_* stubs       -- symbols the compiler references for static objects
//                            and pure-virtual guards. The kernel never exits, so
//                            registered destructors are simply never run.

// Freestanding kernel C++: no standard library headers (-nostdinc++). Use the
// compiler's built-in types rather than <stddef.h>/<stdint.h>, which would drag
// in the hosted libc++ configuration.
using size_t = __SIZE_TYPE__;

// From the kernel heap (mm/kmalloc.h), declared here to avoid pulling a C
// header into C++ translation.
extern "C" void *kmalloc(size_t size);
extern "C" void *kmalloc_aligned(size_t size, size_t align);
extern "C" void kfree(void *ptr);

namespace std {
enum class align_val_t : size_t {};
}

// On exhaustion these return nullptr rather than throwing std::bad_alloc: the
// kernel is built -fno-exceptions, so a null return is the only sensible failure
// mode. Silence the "operator new should not return null" diagnostics.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnew-returns-null"
#pragma clang diagnostic ignored "-Wnonnull"

void *operator new(size_t size) {
    return kmalloc(size);
}

void *operator new[](size_t size) {
    return kmalloc(size);
}

// Over-aligned new (C++17). Circle requests these for some USB structures.
void *operator new(size_t size, std::align_val_t align) {
    return kmalloc_aligned(size, static_cast<size_t>(align));
}

void *operator new[](size_t size, std::align_val_t align) {
    return kmalloc_aligned(size, static_cast<size_t>(align));
}

#pragma clang diagnostic pop

// kfree() finds the block's size and alignment itself, so every delete form,
// sized or aligned, is the same call.
void operator delete(void *ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    kfree(ptr);
}

void operator delete(void *ptr, size_t) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr, size_t) noexcept {
    kfree(ptr);
}

void operator delete(void *ptr, std::align_val_t) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr, std::align_val_t) noexcept {
    kfree(ptr);
}

void operator delete(void *ptr, size_t, std::align_val_t) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr, size_t, std::align_val_t) noexcept {
    kfree(ptr);
}

extern "C" {

// The .init_array bounds emitted by the linker script.
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

void cxx_init(void) {
    for (void (**ctor)(void) = __init_array_start; ctor != __init_array_end;
         ++ctor) {
        (*ctor)();
    }
}

// Referenced when a pure virtual function is called through a partially
// constructed object -- a bug. Halt loudly rather than run off into the weeds.
void __cxa_pure_virtual(void) {
    for (;;) {
        asm volatile("wfi");
    }
}

// Destructor registration for objects with static storage duration. The kernel
// never returns from kernel_main, so we accept the registration and never run
// the destructors.
void *__dso_handle = nullptr;

int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0;
}

}  // extern "C"
