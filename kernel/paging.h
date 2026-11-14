#ifndef ANEMOS_KERNEL_PAGING_H
#define ANEMOS_KERNEL_PAGING_H

#include <stdint.h>

int paging_init(void);
int paging_map_mmio_page(uint64_t virtual_address, uint64_t physical_address);
int paging_map_page(uint64_t virtual_address, uint64_t physical_address);
uint64_t paging_region_base(void);

#endif
