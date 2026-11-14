#ifndef ANEMOS_KERNEL_HEAP_H
#define ANEMOS_KERNEL_HEAP_H

#include <stddef.h>

int heap_init(void);
void *heap_alloc(size_t size);
int heap_free(void *allocation);
size_t heap_max_payload(void);

#endif
