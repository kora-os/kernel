// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the program loader (src/user/elf.c): the stack size a
// program declares (PT_GNU_STACK's p_memsz, what lld's -z stack-size writes)
// comes back in stack_size, 0 when there is none; the image covers its
// PT_LOAD segments with the BSS zeroed; and malformed images are refused. The
// ELF images are built in memory, and the frame allocator and the cache
// maintenance are stubbed.

#include "test.h"
#include "mm.h"
#include "proc/task.h"
#include "user/elf.h"

int posix_memalign(void **out, size_t align, size_t size);
void free(void *ptr);
void *memset(void *dest, int c, size_t n);

static int live_runs;

void *frame_alloc_pages(size_t count) {
    void *p = NULL;
    if (count == 0 || posix_memalign(&p, PAGE_SIZE, count * PAGE_SIZE) != 0) {
        return NULL;
    }
    memset(p, 0, count * PAGE_SIZE);  // the real allocator hands out zeroed pages
    live_runs++;
    return p;
}

void frame_free_pages(void *pages, size_t count) {
    (void)count;
    if (pages != NULL) {
        live_runs--;
        free(pages);
    }
}

void icache_sync_range(const void *addr, size_t size) {
    (void)addr;
    (void)size;
}

void tfp_printf(char *fmt, ...) {
    (void)fmt;  // the loader's messages are not under test
}

// An image: the ELF header, up to four program headers, then the segment
// contents at FILE_DATA.
#define FILE_DATA 0x200
#define LINK_BASE 0x10000

static union {
    unsigned char bytes[0x400];
    Elf64_Ehdr ehdr;
} image;

static Elf64_Phdr *phdr(int i) {
    return (Elf64_Phdr *)(image.bytes + sizeof(Elf64_Ehdr) + (size_t)i * sizeof(Elf64_Phdr));
}

// A valid AArch64 PIE: one PT_LOAD of 16 bytes of data and 0x2000 in memory
// (the rest BSS), then `extra` more program headers the test fills in.
static void build(int extra) {
    memset(&image, 0, sizeof image);
    Elf64_Ehdr *eh = &image.ehdr;
    eh->e_ident[0] = 0x7f;
    eh->e_ident[1] = 'E';
    eh->e_ident[2] = 'L';
    eh->e_ident[3] = 'F';
    eh->e_ident[EI_CLASS] = ELFCLASS64;
    eh->e_ident[EI_DATA] = ELFDATA2LSB;
    eh->e_type = ET_DYN;
    eh->e_machine = EM_AARCH64;
    eh->e_entry = LINK_BASE + 4;
    eh->e_phoff = sizeof(Elf64_Ehdr);
    eh->e_phentsize = sizeof(Elf64_Phdr);
    eh->e_phnum = (uint16_t)(1 + extra);
    Elf64_Phdr *load = phdr(0);
    load->p_type = PT_LOAD;
    load->p_offset = FILE_DATA;
    load->p_vaddr = LINK_BASE;
    load->p_filesz = 16;
    load->p_memsz = 0x2000;
    for (int i = 0; i < 16; i++) {
        image.bytes[FILE_DATA + i] = (unsigned char)(0xa0 + i);
    }
}

static void gnu_stack(int index, uint64_t size) {
    Elf64_Phdr *ph = phdr(index);
    ph->p_type = PT_GNU_STACK;
    ph->p_memsz = size;  // lld leaves every other field 0 but the flags
    ph->p_flags = 6;     // RW
}

static int load(struct loaded_prog *lp) {
    memset(lp, 0x55, sizeof *lp);
    return elf_load(&image, sizeof image, lp);
}

static void test_no_stack_header(void) {
    build(0);
    struct loaded_prog lp;
    CHECK(load(&lp) == 0, "a plain image loads");
    CHECK(lp.stack_size == 0, "no PT_GNU_STACK: no stack asked for (%lu)",
          (unsigned long)lp.stack_size);
    CHECK(lp.image_pages == 2, "two pages for 0x2000 bytes: %lu", (unsigned long)lp.image_pages);
    const unsigned char *bytes = lp.image;
    int data_ok = 1, bss_ok = 1;
    for (int i = 0; i < 16; i++) {
        data_ok &= bytes[i] == 0xa0 + i;
    }
    for (int i = 16; i < 0x2000; i++) {
        bss_ok &= bytes[i] == 0;
    }
    CHECK(data_ok && bss_ok, "the segment is copied and its BSS is zero");
    CHECK(lp.entry == (uint64_t)lp.image + 4, "the entry point is moved with the image");
    frame_free_pages(lp.image, lp.image_pages);
}

static void test_stack_sizes(void) {
    static const uint64_t sizes[] = {0, 1, 4096, 5000, 65536, 0x100000, 0x7fffffffull};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        build(1);
        gnu_stack(1, sizes[i]);
        struct loaded_prog lp;
        CHECK(load(&lp) == 0 && lp.stack_size == sizes[i], "PT_GNU_STACK of %lu bytes: %lu",
              (unsigned long)sizes[i], (unsigned long)lp.stack_size);
        frame_free_pages(lp.image, lp.image_pages);
    }
    // The header may come before the PT_LOAD, as it may in any order.
    build(1);
    *phdr(1) = *phdr(0);
    memset(phdr(0), 0, sizeof(Elf64_Phdr));
    gnu_stack(0, 32768);
    struct loaded_prog lp;
    CHECK(load(&lp) == 0 && lp.stack_size == 32768, "PT_GNU_STACK first");
    frame_free_pages(lp.image, lp.image_pages);
}

static void test_refused(void) {
    struct loaded_prog lp;
    build(1);
    gnu_stack(1, 65536);
    image.bytes[1] = 'X';
    CHECK(load(&lp) == ELF_ERR_MAGIC, "not ELF");
    build(0);
    image.ehdr.e_type = 2;  // ET_EXEC
    CHECK(load(&lp) == ELF_ERR_TYPE, "not a PIE");
    build(0);
    image.ehdr.e_phnum = 0;
    CHECK(load(&lp) == ELF_ERR_PHDR, "no program headers");
    build(1);
    gnu_stack(1, 65536);
    phdr(0)->p_type = 0;  // PT_NULL: nothing to load
    CHECK(load(&lp) == ELF_ERR_NOLOAD, "only a stack header");
    build(0);
    phdr(0)->p_filesz = 0x3000;  // more file than memory
    CHECK(load(&lp) == ELF_ERR_PHDR, "p_filesz larger than p_memsz");
    CHECK(elf_load(&image, 16, &lp) == ELF_ERR_TOOSMALL, "a buffer smaller than the header");
    CHECK(live_runs == 0, "no image left allocated: %d", live_runs);
}

// The kernel's rule for how many pages a request gets (include/proc/task.h).
static void test_stack_pages(void) {
    CHECK(user_stack_pages(0, 1) == 1 && user_stack_pages(0, 4) == 4, "0: the default");
    CHECK(user_stack_pages(1, 1) == 1 && user_stack_pages(4096, 4) == 1 &&
              user_stack_pages(4097, 1) == 2 && user_stack_pages(65536, 1) == 16,
          "rounded up to whole pages, the default ignored");
    CHECK(user_stack_pages((uint64_t)USER_STACK_MAX_PAGES * PAGE_SIZE, 1) == USER_STACK_MAX_PAGES,
          "the largest request");
    CHECK(user_stack_pages((uint64_t)USER_STACK_MAX_PAGES * PAGE_SIZE + 1, 1) == 0 &&
              user_stack_pages(~0ull, 1) == 0,
          "beyond it: refused");
}

TEST_MAIN(test_no_stack_header, test_stack_sizes, test_refused, test_stack_pages)
