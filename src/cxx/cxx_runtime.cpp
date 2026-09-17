// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal freestanding C++ runtime for the KoraOS kernel.
//
// The kernel is compiled with -fno-exceptions -fno-rtti -fno-threadsafe-statics,
// so the runtime surface we must supply by hand is small:
//
//   * cxx_init()          -- run C++ global constructors (.init_array).
//   * operator new/delete -- dynamic allocation, backed by the page-granular
//                            frame allocator. This is deliberately simple; when
//                            Circle's memory system is vendored it can provide a
//                            real sub-page heap and supersede these.
//   * __cxa_* stubs       -- symbols the compiler references for static objects
//                            and pure-virtual guards. The kernel never exits, so
//                            registered destructors are simply never run.

// Freestanding kernel C++: no standard library headers (-nostdinc++). Use the
// compiler's built-in types rather than <stddef.h>/<stdint.h>, which would drag
// in the hosted libc++ configuration.
using size_t = __SIZE_TYPE__;
using uint8_t = __UINT8_TYPE__;

// From the frame allocator (mm/frame_alloc.h), declared here to avoid pulling a
// C header into C++ translation.
extern "C" void *frame_alloc_pages(size_t count);
extern "C" void frame_free_pages(void *pages, size_t count);

namespace {

constexpr size_t kPageSize = 4096;

// Reserve a header the size of the platform's max alignment so the pointer we
// hand back stays 16-byte aligned (frame pages are page-aligned to start with).
// The header stores the page count so operator delete can free the right run.
constexpr size_t kHeader = 16;

}  // namespace

// On exhaustion these return nullptr rather than throwing std::bad_alloc: the
// kernel is built -fno-exceptions, so a null return is the only sensible failure
// mode. Silence the "operator new should not return null" diagnostics.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnew-returns-null"
#pragma clang diagnostic ignored "-Wnonnull"

void *operator new(size_t size) {
    size_t pages = (size + kHeader + kPageSize - 1) / kPageSize;
    if (pages == 0) {
        pages = 1;
    }
    auto *base = static_cast<uint8_t *>(frame_alloc_pages(pages));
    if (base == nullptr) {
        return nullptr;
    }
    *reinterpret_cast<size_t *>(base) = pages;
    return base + kHeader;
}

void *operator new[](size_t size) {
    return operator new(size);
}

#pragma clang diagnostic pop

void operator delete(void *ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    auto *base = static_cast<uint8_t *>(ptr) - kHeader;
    size_t pages = *reinterpret_cast<size_t *>(base);
    frame_free_pages(base, pages);
}

void operator delete[](void *ptr) noexcept {
    operator delete(ptr);
}

// Sized-deallocation overloads (C++14+). The size is ignored; the page count in
// the header is authoritative.
void operator delete(void *ptr, size_t) noexcept {
    operator delete(ptr);
}

void operator delete[](void *ptr, size_t) noexcept {
    operator delete(ptr);
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
