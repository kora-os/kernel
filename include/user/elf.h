#pragma once

#include "common.h"

// Minimal ELF64 / AArch64 definitions and a loader for statically-linked
// position-independent executables (ET_DYN). Only what KoraOS needs: PT_LOAD
// segments and R_AARCH64_RELATIVE relocations. No dynamic linking, no symbol
// resolution, no other relocation types.

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    int64_t d_tag;
    uint64_t d_val;  // also used as d_ptr
} Elf64_Dyn;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

// e_ident indices and values.
#define EI_CLASS 4
#define EI_DATA 5
#define ELFCLASS64 2
#define ELFDATA2LSB 1

// e_type / e_machine.
#define ET_DYN 3
#define EM_AARCH64 183

// Program header types and flags.
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// Dynamic tags.
#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9

// Relocation type extraction and the only type we support.
#define ELF64_R_TYPE(info) ((uint32_t)((info) & 0xffffffffUL))
#define R_AARCH64_RELATIVE 1027

// Result of a successful load. The program's segments (and any BSS) occupy
// `image_pages` contiguous pages at `image`; `entry` is the absolute EL0 entry
// address. Use image/image_pages to free the region when the task exits.
struct loaded_prog {
    uint64_t entry;
    void *image;
    size_t image_pages;
};

// Load a PIE ELF image (`data`, `len` bytes) into a freshly allocated region
// and apply its RELATIVE relocations. Returns 0 on success, or a negative
// ELF_ERR_* code on failure.
int elf_load(const void *data, size_t len, struct loaded_prog *out);

#define ELF_ERR_TOOSMALL   (-1)  // buffer smaller than the ELF header
#define ELF_ERR_MAGIC      (-2)  // not an ELF file
#define ELF_ERR_CLASS      (-3)  // not ELF64 little-endian
#define ELF_ERR_TYPE       (-4)  // not ET_DYN / not AArch64
#define ELF_ERR_PHDR       (-5)  // malformed / out-of-bounds program headers
#define ELF_ERR_NOLOAD     (-6)  // no PT_LOAD segments
#define ELF_ERR_NOMEM      (-7)  // frame allocator exhausted
#define ELF_ERR_RELOC      (-8)  // unsupported relocation type
