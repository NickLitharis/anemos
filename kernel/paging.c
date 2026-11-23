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

#define USER_ROOT_CAPACITY 4u
#define USER_ROOT_OWNED_PAGE_CAPACITY 32u

struct user_root_state {
    uint64_t root_physical;
    uint64_t owned_pages[USER_ROOT_OWNED_PAGE_CAPACITY];
    unsigned owned_count;
    int destroying;
};

static struct user_root_state user_roots[USER_ROOT_CAPACITY];

static struct user_root_state *find_user_root(uint64_t root_physical)
{
    for (size_t i = 0u; i < USER_ROOT_CAPACITY; ++i) {
        if (user_roots[i].root_physical == root_physical) {
            return &user_roots[i];
        }
    }
    return NULL;
}

static struct user_root_state *find_free_user_root(void)
{
    for (size_t i = 0u; i < USER_ROOT_CAPACITY; ++i) {
        if (user_roots[i].root_physical == 0u) {
            return &user_roots[i];
        }
    }
    return NULL;
}

static int remember_user_root_page(
    struct user_root_state *owner,
    uint64_t physical_address)
{
    if (owner == NULL || physical_address == 0u ||
        owner->destroying != 0 ||
        owner->owned_count == USER_ROOT_OWNED_PAGE_CAPACITY) {
        return 0;
    }
    for (size_t i = 0u; i < USER_ROOT_CAPACITY; ++i) {
        for (unsigned j = 0u; j < user_roots[i].owned_count; ++j) {
            if (user_roots[i].owned_pages[j] == physical_address) {
                return 0;
            }
        }
    }
    owner->owned_pages[owner->owned_count++] = physical_address;
    return 1;
}

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

#define ENTRY_USER (1ull << 2u)
#define USER_ADDRESS_BASE 0x00400000ull
#define USER_ADDRESS_LIMIT 0x0000800000000000ull

uint64_t paging_user_root_create(void)
{
    const uint64_t current_root_physical =
        paging_current_root() & PHYSICAL_MASK;
    const uint64_t *current_root = physical_to_virtual(current_root_physical);
    struct user_root_state *state = find_free_user_root();
    if (current_root == NULL || state == NULL) {
        return 0u;
    }
    const uint64_t root_physical = page_alloc();
    uint64_t *user_root = physical_to_virtual(root_physical);
    if (root_physical == 0u || user_root == NULL) {
        if (root_physical != 0u && !page_free(root_physical)) {
            return 0u;
        }
        return 0u;
    }
    state->root_physical = root_physical;
    state->owned_count = 0u;
    state->destroying = 0;
    if (!remember_user_root_page(state, root_physical)) {
        state->root_physical = 0u;
        if (!page_free(root_physical)) {
            return 0u;
        }
        return 0u;
    }
    for (uint16_t i = 256u; i < 512u; ++i) {
        /* A supervisor PML4 bit protects every inherited lower-level mapping. */
        user_root[i] = current_root[i] & ~ENTRY_USER;
    }
    return root_physical;
}

int paging_user_map_page(
    uint64_t root_physical,
    uint64_t virtual_address,
    uint64_t physical_address,
    int writable,
    int executable)
{
    struct user_root_state *state = find_user_root(root_physical);
    if (root_physical == 0u || state == NULL || state->destroying != 0 ||
        virtual_address < USER_ADDRESS_BASE ||
        virtual_address >= USER_ADDRESS_LIMIT ||
        (virtual_address & PAGE_MASK) != 0u ||
        (physical_address & PAGE_MASK) != 0u ||
        (physical_address & ~PHYSICAL_MASK) != 0u) {
        return 0;
    }
    uint16_t indices[4] = {
        (uint16_t)((virtual_address >> 39u) & 0x1FFu),
        (uint16_t)((virtual_address >> 30u) & 0x1FFu),
        (uint16_t)((virtual_address >> 21u) & 0x1FFu),
        (uint16_t)((virtual_address >> 12u) & 0x1FFu)
    };
    uint64_t *table = physical_to_virtual(root_physical);
    if (table == NULL) {
        return 0;
    }
    for (size_t level = 0u; level < 3u; ++level) {
        uint64_t entry = table[indices[level]];
        if ((entry & ENTRY_PRESENT) == 0u) {
            const uint64_t next_physical = page_alloc();
            if (next_physical == 0u ||
                !remember_user_root_page(state, next_physical)) {
                if (next_physical != 0u && !page_free(next_physical)) {
                    return 0;
                }
                return 0;
            }
            entry = next_physical | ENTRY_PRESENT | ENTRY_WRITABLE | ENTRY_USER;
            table[indices[level]] = entry;
        } else if ((entry & ENTRY_HUGE) != 0u) {
            return 0;
        } else {
            entry |= ENTRY_WRITABLE | ENTRY_USER;
            table[indices[level]] = entry;
        }
        table = physical_to_virtual(entry & PHYSICAL_MASK);
        if (table == NULL) {
            return 0;
        }
    }
    if ((table[indices[3]] & ENTRY_PRESENT) != 0u ||
        !remember_user_root_page(state, physical_address)) {
        return 0;
    }
    uint64_t flags = ENTRY_USER | (writable != 0 ? ENTRY_WRITABLE : 0u);
    if (executable == 0) {
        flags |= ENTRY_NO_EXECUTE;
    }
    table[indices[3]] = physical_address | ENTRY_PRESENT | flags;
    return 1;
}

int paging_user_copy_in(
    uint64_t user_address,
    void *kernel_destination,
    size_t length)
{
    const uint64_t offset = user_address & PAGE_MASK;
    if (kernel_destination == NULL || length == 0u || length > 32u ||
        user_address < USER_ADDRESS_BASE ||
        user_address >= USER_ADDRESS_LIMIT ||
        (uint64_t)length > USER_ADDRESS_LIMIT - user_address ||
        (uint64_t)length > PAGE_SIZE - offset) {
        return 0;
    }
    uint16_t indices[4] = {
        (uint16_t)((user_address >> 39u) & 0x1FFu),
        (uint16_t)((user_address >> 30u) & 0x1FFu),
        (uint16_t)((user_address >> 21u) & 0x1FFu),
        (uint16_t)((user_address >> 12u) & 0x1FFu)
    };
    uint64_t *table = physical_to_virtual(paging_current_root());
    if (table == NULL) {
        return 0;
    }
    for (size_t level = 0u; level < 4u; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & (ENTRY_PRESENT | ENTRY_USER)) !=
            (ENTRY_PRESENT | ENTRY_USER)) {
            return 0;
        }
        if (level == 3u) {
            const uint64_t physical = (entry & PHYSICAL_MASK) + offset;
            const uint8_t *source = physical_to_virtual(physical);
            uint8_t *destination = kernel_destination;
            if (source == NULL) {
                return 0;
            }
            for (size_t i = 0u; i < length; ++i) {
                destination[i] = source[i];
            }
            return 1;
        }
        if ((entry & ENTRY_HUGE) != 0u) {
            return 0;
        }
        table = physical_to_virtual(entry & PHYSICAL_MASK);
        if (table == NULL) {
            return 0;
        }
    }
    return 0;
}

int paging_user_root_is_safe(uint64_t root_physical)
{
    const struct user_root_state *state = find_user_root(root_physical);
    const uint64_t *root = physical_to_virtual(root_physical);
    if (state == NULL || state->destroying != 0 || root == NULL ||
        (root_physical & PAGE_MASK) != 0u) {
        return 0;
    }
    for (uint16_t i = 256u; i < 512u; ++i) {
        if ((root[i] & (ENTRY_PRESENT | ENTRY_USER)) ==
            (ENTRY_PRESENT | ENTRY_USER)) {
            return 0;
        }
    }
    return 1;
}

int paging_user_root_destroy(uint64_t root_physical)
{
    struct user_root_state *state = find_user_root(root_physical);
    if (state == NULL || root_physical == 0u ||
        paging_current_root() == root_physical) {
        return 0;
    }
    state->destroying = 1;
    while (state->owned_count != 0u) {
        const unsigned index = state->owned_count - 1u;
        const uint64_t page = state->owned_pages[index];
        if (!page_free(page)) {
            return 0;
        }
        state->owned_pages[index] = 0u;
        state->owned_count = index;
    }
    state->root_physical = 0u;
    state->destroying = 0;
    return 1;
}

uint64_t paging_current_root(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PHYSICAL_MASK;
}

void paging_activate_root(uint64_t root_physical)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(root_physical) : "memory");
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
