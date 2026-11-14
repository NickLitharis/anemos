#include "memory.h"

#include "limine_requests.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ull
#define PAGE_MASK (PAGE_SIZE - 1ull)
#define LOW_MEMORY_LIMIT 0x100000ull
#define MAX_MEMORY_MAP_ENTRIES 4096ull

struct free_page {
    struct free_page *next;
};

static struct free_page *free_pages;
static uint64_t free_count;
static uint64_t hhdm_offset;
static struct limine_memmap_response *memory_map;

void *physical_to_virtual(uint64_t physical_address)
{
    if (physical_address > UINTPTR_MAX - hhdm_offset) {
        return NULL;
    }
    return (void *)(uintptr_t)(hhdm_offset + physical_address);
}


int physical_address_is_hhdm_mapped(uint64_t physical_address)
{
    if (memory_map == NULL) {
        return 0;
    }
    for (uint64_t i = 0u; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memory_map->entries[i];
        if (physical_address < entry->base ||
            physical_address - entry->base >= entry->length) {
            continue;
        }
        return entry->type == LIMINE_MEMMAP_USABLE ||
               entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
               entry->type == LIMINE_MEMMAP_ACPI_NVS ||
               entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
               entry->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES ||
               entry->type == LIMINE_MEMMAP_FRAMEBUFFER ||
               entry->type == LIMINE_MEMMAP_RESERVED_MAPPED;
    }
    return 0;
}
static int is_usable_page(uint64_t physical_address)
{
    for (uint64_t i = 0u; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memory_map->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE &&
            physical_address >= entry->base &&
            physical_address - entry->base < entry->length) {
            return 1;
        }
    }
    return 0;
}

int memory_init(void)
{
    struct limine_memmap_response *map = limine_memory_map();
    struct limine_hhdm_response *hhdm = limine_hhdm();
    struct limine_paging_mode_response *paging = limine_paging_mode();

    if (!limine_base_revision_supported() || map == NULL || hhdm == NULL ||
        paging == NULL || map->entries == NULL || map->entry_count == 0u ||
        map->entry_count > MAX_MEMORY_MAP_ENTRIES ||
        paging->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
        return 0;
    }

    for (uint64_t i = 0u; i < map->entry_count; ++i) {
        if (map->entries[i] == NULL) {
            return 0;
        }
    }

    memory_map = map;
    hhdm_offset = hhdm->offset;
    free_pages = NULL;
    free_count = 0u;

    for (uint64_t i = 0u; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE ||
            entry->base > UINT64_MAX - entry->length) {
            continue;
        }

        uint64_t start = entry->base;
        uint64_t end = entry->base + entry->length;
        if (start < LOW_MEMORY_LIMIT) {
            start = LOW_MEMORY_LIMIT;
        }
        if (start > UINT64_MAX - PAGE_MASK) {
            continue;
        }
        start = (start + PAGE_MASK) & ~PAGE_MASK;
        end &= ~PAGE_MASK;

        for (uint64_t address = start; address < end; address += PAGE_SIZE) {
            struct free_page *page = physical_to_virtual(address);
            if (page == NULL) {
                continue;
            }
            page->next = free_pages;
            free_pages = page;
            ++free_count;
        }
    }

    return free_count != 0u;
}

uint64_t page_alloc(void)
{
    struct free_page *page = free_pages;
    if (page == NULL) {
        return 0u;
    }

    free_pages = page->next;
    --free_count;

    uint64_t physical_address = (uint64_t)(uintptr_t)page - hhdm_offset;
    volatile uint64_t *words = (volatile uint64_t *)page;
    for (size_t i = 0u; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0u;
    }
    return physical_address;
}

int page_free(uint64_t physical_address)
{
    if ((physical_address & PAGE_MASK) != 0u ||
        physical_address < LOW_MEMORY_LIMIT || !is_usable_page(physical_address)) {
        return 0;
    }

    struct free_page *page = physical_to_virtual(physical_address);
    if (page == NULL) {
        return 0;
    }
    for (struct free_page *current = free_pages; current != NULL; current = current->next) {
        if (current == page) {
            return 0;
        }
    }

    page->next = free_pages;
    free_pages = page;
    ++free_count;
    return 1;
}

uint64_t page_free_count(void)
{
    return free_count;
}
