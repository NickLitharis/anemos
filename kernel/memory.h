#ifndef ANEMOS_KERNEL_MEMORY_H
#define ANEMOS_KERNEL_MEMORY_H

#include <stdint.h>

int memory_init(void);
uint64_t page_alloc(void);
int page_free(uint64_t physical_address);
void *physical_to_virtual(uint64_t physical_address);
int physical_address_is_hhdm_mapped(uint64_t physical_address);
uint64_t page_free_count(void);

#endif
