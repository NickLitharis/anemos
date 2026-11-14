#include "paging.h"

#include "memory.h"

#include <stddef.h>

#define PAGE_SIZE 4096ull
#define PAGE_MASK (PAGE_SIZE - 1ull)
#define PHYSICAL_MASK 0x000ffffffffff000ull
#define ENTRY_PRESENT 0x001ull
#define ENTRY_WRITABLE 0x002ull
#define ENTRY_WRITE_THROUGH 0x008ull
#define ENTRY_CACHE_DISABLE 0x010ull
#define ENTRY_HUGE 0x080ull
#define EFER_MSR 0xC0000080u
#define ENTRY_NO_EXECUTE (1ull << 63u)
#define EFER_NXE (1u << 11u)

static uint64_t *root_table;
static uint64_t region_base;
static uint16_t region_pml4_index;

static int cpu_has_nx(void)
{
    uint32_t maximum_leaf;
    uint32_t feature_edx;
    __asm__ volatile (
        "movl $0x80000000, %%eax\n\t"
        "cpuid\n\t"
        : "=a"(maximum_leaf)
        :
        : "rbx", "rcx", "rdx");
    if (maximum_leaf < 0x80000001u) {
        return 0;
    }
    __asm__ volatile (
        "movl $0x80000001, %%eax\n\t"
        "cpuid\n\t"
        : "=d"(feature_edx)
        :
        : "rax", "rbx", "rcx");
    return (feature_edx & (1u << 20u)) != 0u;
}

static void enable_nx(void)
{
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(EFER_MSR));
    low |= EFER_NXE;
    __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(EFER_MSR));
}

int paging_init(void)
{
    if (!cpu_has_nx()) {
        return 0;
    }
    enable_nx();

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    root_table = physical_to_virtual(cr3 & PHYSICAL_MASK);
    if (root_table == NULL) {
        return 0;
    }

    for (uint16_t index = 256u; index < 511u; ++index) {
        if (root_table[index] == 0u) {
            region_pml4_index = index;
            region_base = 0xffff000000000000ull | ((uint64_t)index << 39u);
            return 1;
        }
    }
    return 0;
}

static uint64_t *next_table(uint64_t *table, uint16_t index)
{
    uint64_t entry = table[index];
    if ((entry & ENTRY_PRESENT) == 0u) {
        uint64_t physical = page_alloc();
        if (physical == 0u) {
            return NULL;
        }
        table[index] = physical | ENTRY_PRESENT | ENTRY_WRITABLE;
        return physical_to_virtual(physical);
    }
    if ((entry & ENTRY_HUGE) != 0u) {
        return NULL;
    }
    return physical_to_virtual(entry & PHYSICAL_MASK);
}


static int paging_map_page_with_flags(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags)
{
    if (root_table == NULL || (virtual_address & PAGE_MASK) != 0u ||
        (physical_address & PAGE_MASK) != 0u ||
        ((virtual_address >> 39u) & 0x1FFu) != region_pml4_index ||
        (physical_address & ~PHYSICAL_MASK) != 0u) {
        return 0;
    }

    uint16_t indices[4] = {
        (uint16_t)((virtual_address >> 39u) & 0x1FFu),
        (uint16_t)((virtual_address >> 30u) & 0x1FFu),
        (uint16_t)((virtual_address >> 21u) & 0x1FFu),
        (uint16_t)((virtual_address >> 12u) & 0x1FFu)
    };
    uint64_t *table = root_table;
    for (size_t level = 0u; level < 3u; ++level) {
        table = next_table(table, indices[level]);
        if (table == NULL) {
            return 0;
        }
    }

    if ((table[indices[3]] & ENTRY_PRESENT) != 0u) {
        return 0;
    }
    table[indices[3]] = physical_address | ENTRY_PRESENT | flags | ENTRY_NO_EXECUTE;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 1;
}

int paging_map_page(uint64_t virtual_address, uint64_t physical_address)
{
    return paging_map_page_with_flags(
        virtual_address, physical_address, ENTRY_WRITABLE);
}

int paging_map_mmio_page(uint64_t virtual_address, uint64_t physical_address)
{
    return paging_map_page_with_flags(
        virtual_address,
        physical_address,
        ENTRY_WRITABLE | ENTRY_WRITE_THROUGH | ENTRY_CACHE_DISABLE);
}

uint64_t paging_region_base(void)
{
    return region_base;
}
