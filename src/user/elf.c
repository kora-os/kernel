#include "user/elf.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "lib/printf.h"

static uint64_t page_down(uint64_t v) {
    return v & ~((uint64_t)PAGE_SIZE - 1);
}

static uint64_t page_up(uint64_t v) {
    return page_down(v + PAGE_SIZE - 1);
}

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static const Elf64_Phdr *phdr_at(const void *data, const Elf64_Ehdr *eh, uint16_t i) {
    return (const Elf64_Phdr *)((const uint8_t *)data + eh->e_phoff +
                                (size_t)i * eh->e_phentsize);
}

// Apply R_AARCH64_RELATIVE relocations from the image's DT_RELA table. Every
// other relocation type is rejected -- user programs are built to only need
// RELATIVE (see the user link flags). `bias` is (load address - link address).
static int apply_relocations(uint8_t *base, uint64_t bias, const Elf64_Phdr *dyn_ph) {
    const Elf64_Dyn *dyn = (const Elf64_Dyn *)(base + dyn_ph->p_vaddr);

    uint64_t rela_addr = 0;
    uint64_t rela_size = 0;
    uint64_t rela_ent = sizeof(Elf64_Rela);
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_RELA:
            rela_addr = d->d_val;
            break;
        case DT_RELASZ:
            rela_size = d->d_val;
            break;
        case DT_RELAENT:
            rela_ent = d->d_val;
            break;
        default:
            break;
        }
    }

    if (rela_addr == 0 || rela_size == 0) {
        return 0;  // Nothing to relocate.
    }

    for (uint64_t off = 0; off + sizeof(Elf64_Rela) <= rela_size; off += rela_ent) {
        const Elf64_Rela *r = (const Elf64_Rela *)(base + rela_addr + off);
        if (ELF64_R_TYPE(r->r_info) != R_AARCH64_RELATIVE) {
            printf("elf: unsupported reloc type %u\n", ELF64_R_TYPE(r->r_info));
            return ELF_ERR_RELOC;
        }
        // RELATIVE: *(target) = load_base + addend, where the target lives at
        // the relocated address r_offset + bias.
        uint64_t *target = (uint64_t *)(base + r->r_offset);
        *target = bias + (uint64_t)r->r_addend;
    }

    return 0;
}

int elf_load(const void *data, size_t len, struct loaded_prog *out) {
    if (len < sizeof(Elf64_Ehdr)) {
        return ELF_ERR_TOOSMALL;
    }

    const Elf64_Ehdr *eh = data;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        return ELF_ERR_MAGIC;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB) {
        return ELF_ERR_CLASS;
    }
    if (eh->e_type != ET_DYN || eh->e_machine != EM_AARCH64) {
        return ELF_ERR_TYPE;
    }
    if (eh->e_phentsize < sizeof(Elf64_Phdr) || eh->e_phnum == 0) {
        return ELF_ERR_PHDR;
    }
    if (eh->e_phoff + (size_t)eh->e_phnum * eh->e_phentsize > len) {
        return ELF_ERR_PHDR;
    }

    // Find the virtual-address span covered by all PT_LOAD segments.
    uint64_t lo = UINT64_MAX;
    uint64_t hi = 0;
    const Elf64_Phdr *dyn_ph = NULL;
    bool have_load = false;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = phdr_at(data, eh, i);
        if (ph->p_type == PT_DYNAMIC) {
            dyn_ph = ph;
        }
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_filesz > ph->p_memsz) {
            return ELF_ERR_PHDR;
        }
        if (ph->p_offset + ph->p_filesz > len) {
            return ELF_ERR_PHDR;
        }
        have_load = true;
        if (ph->p_vaddr < lo) {
            lo = ph->p_vaddr;
        }
        if (ph->p_vaddr + ph->p_memsz > hi) {
            hi = ph->p_vaddr + ph->p_memsz;
        }
    }
    if (!have_load) {
        return ELF_ERR_NOLOAD;
    }

    lo = page_down(lo);
    hi = page_up(hi);
    size_t pages = (size_t)((hi - lo) / PAGE_SIZE);

    uint8_t *region = frame_alloc_pages(pages);
    if (region == NULL) {
        return ELF_ERR_NOMEM;
    }

    // Place the image so that link address `lo` lands at `region`. `bias` then
    // maps any link address X to X + bias at runtime.
    uint64_t bias = (uint64_t)region - lo;

    // Copy each loadable segment; frame_alloc_pages already zeroed the region,
    // so the BSS tail (p_memsz - p_filesz) needs no extra work.
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = phdr_at(data, eh, i);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        uint8_t *dst = (uint8_t *)(ph->p_vaddr + bias);
        copy_bytes(dst, (const uint8_t *)data + ph->p_offset, ph->p_filesz);
    }

    if (dyn_ph != NULL) {
        int rc = apply_relocations(region, bias, dyn_ph);
        if (rc != 0) {
            frame_free_pages(region, pages);
            return rc;
        }
    }

    out->entry = eh->e_entry + bias;
    out->image = region;
    out->image_pages = pages;
    return 0;
}
