#include "mm/mmu.h"
#include "arch/sysregs.h"
#include "common.h"
#include "mm.h"
#include "peripherals/base.h"

// End of the executable (code) region, 2 MB aligned by the linker script.
extern char text_end[];

// Translation tables for a 48-bit VA space covering the low 4 GB with 2 MB
// blocks: one L0 (512 GB/entry), one L1 (1 GB/entry, 4 entries used) and four
// L2 tables (2 MB/entry). Statically reserved in BSS, 4 KB aligned.
#define ENTRIES_PER_TABLE 512
#define GIB_COVERED 4

static uint64_t l0_table[ENTRIES_PER_TABLE] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l1_table[ENTRIES_PER_TABLE] __attribute__((aligned(PAGE_SIZE)));
static uint64_t l2_tables[GIB_COVERED][ENTRIES_PER_TABLE]
    __attribute__((aligned(PAGE_SIZE)));

static void build_identity_map(void) {
    uint64_t code_end = (uint64_t)text_end;

    l0_table[0] = (uint64_t)l1_table | PD_TABLE;

    for (uint64_t g = 0; g < GIB_COVERED; g++) {
        l1_table[g] = (uint64_t)l2_tables[g] | PD_TABLE;

        for (uint64_t i = 0; i < ENTRIES_PER_TABLE; i++) {
            // Physical base of this 2 MB block.
            uint64_t addr = (g << 30) | (i << 21);

            uint64_t flags;
            if (addr >= PBASE) {
                flags = MMU_DEVICE_BLOCK_FLAGS;       // peripherals / MMIO
            } else if (addr < code_end) {
                flags = MMU_CODE_BLOCK_FLAGS;         // kernel + user text
            } else {
                flags = MMU_NORMAL_BLOCK_FLAGS;       // general RAM, EL0+EL1 RW
            }

            l2_tables[g][i] = addr | flags;
        }
    }
}

void mmu_init(void) {
    build_identity_map();

    // Physical address size supported by the CPU (PARange), clamped to 48-bit.
    uint64_t mmfr0;
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    uint64_t parange = mmfr0 & 0xF;
    if (parange > 5) {
        parange = 5;
    }

    uint64_t tcr = TCR_VALUE | (parange << 32);

    asm volatile("msr mair_el1, %0" ::"r"((uint64_t)MAIR_VALUE));
    asm volatile("msr tcr_el1, %0" ::"r"(tcr));
    asm volatile("msr ttbr0_el1, %0" ::"r"((uint64_t)l0_table));
    asm volatile("isb");

    // Make sure the tables are visible and the TLB is clean before enabling.
    asm volatile("dsb ish");
    asm volatile("tlbi vmalle1");
    asm volatile("dsb ish");
    asm volatile("isb");

    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= SCTLR_MMU_ENABLED | SCTLR_D_CACHE | SCTLR_I_CACHE;
    asm volatile("msr sctlr_el1, %0" ::"r"(sctlr));
    asm volatile("isb");
}
