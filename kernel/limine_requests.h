#ifndef ANEMOS_KERNEL_LIMINE_REQUESTS_H
#define ANEMOS_KERNEL_LIMINE_REQUESTS_H

#include <stdint.h>

#define LIMINE_MEMMAP_USABLE 0u
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2u
#define LIMINE_MEMMAP_ACPI_NVS 3u
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5u
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6u
#define LIMINE_MEMMAP_FRAMEBUFFER 7u
#define LIMINE_MEMMAP_RESERVED_MAPPED 8u
#define LIMINE_PAGING_MODE_X86_64_4LVL 0u

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_paging_mode_response {
    uint64_t revision;
    uint64_t mode;
};

struct limine_memmap_response *limine_memory_map(void);
struct limine_hhdm_response *limine_hhdm(void);
struct limine_paging_mode_response *limine_paging_mode(void);
int limine_base_revision_supported(void);

#endif
