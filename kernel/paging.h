#ifndef ANEMOS_KERNEL_PAGING_H
#define ANEMOS_KERNEL_PAGING_H

#include <stdint.h>
#include <stddef.h>

int paging_init(void);
int paging_map_mmio_page(uint64_t virtual_address, uint64_t physical_address);
int paging_map_page(uint64_t virtual_address, uint64_t physical_address);
uint64_t paging_region_base(void);

uint64_t paging_user_root_create(void);
int paging_user_map_page(
    uint64_t root_physical,
    uint64_t virtual_address,
    uint64_t physical_address,
    int writable,
    int executable);
int paging_user_root_is_safe(uint64_t root_physical);
int paging_user_root_destroy(uint64_t root_physical);
int paging_user_copy_in(
    uint64_t user_address,
    void *kernel_destination,
    size_t length);

uint64_t paging_current_root(void);
void paging_activate_root(uint64_t root_physical);

#endif
