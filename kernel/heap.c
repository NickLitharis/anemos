#include "heap.h"

#include "memory.h"
#include "paging.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ull
#define HEAP_PAGE_COUNT 16u
#define HEAP_SIZE (PAGE_SIZE * HEAP_PAGE_COUNT)
#define HEAP_OFFSET 0x100000ull
#define HEAP_ALIGNMENT 16u

struct heap_block {
    size_t size;
    struct heap_block *next;
    uint64_t free;
    uint64_t reserved;
};

_Static_assert(sizeof(struct heap_block) == 32u, "heap header must be 16-byte aligned");

static uint8_t *heap_start;
static struct heap_block *first_block;

int heap_init(void)
{
    uint64_t base = paging_region_base() + HEAP_OFFSET;
    size_t mapped = 0u;

    if (base < paging_region_base()) {
        return 0;
    }
    for (; mapped < HEAP_PAGE_COUNT; ++mapped) {
        uint64_t physical = page_alloc();
        if (physical == 0u || !paging_map_page(base + mapped * PAGE_SIZE, physical)) {
            if (physical != 0u) {
                (void)page_free(physical);
            }
            return 0;
        }
    }

    heap_start = (uint8_t *)(uintptr_t)base;
    first_block = (struct heap_block *)heap_start;
    first_block->size = (size_t)(HEAP_SIZE - sizeof(*first_block));
    first_block->next = NULL;
    first_block->free = 1u;
    first_block->reserved = 0u;
    return 1;
}

void *heap_alloc(size_t size)
{
    if (size == 0u || size > SIZE_MAX - (HEAP_ALIGNMENT - 1u)) {
        return NULL;
    }
    size = (size + (HEAP_ALIGNMENT - 1u)) & ~(size_t)(HEAP_ALIGNMENT - 1u);

    for (struct heap_block *block = first_block; block != NULL; block = block->next) {
        if (block->free == 0u || block->size < size) {
            continue;
        }
        size_t remainder = block->size - size;
        if (remainder >= sizeof(*block) + HEAP_ALIGNMENT) {
            struct heap_block *split = (struct heap_block *)
                ((uint8_t *)(block + 1) + size);
            split->size = remainder - sizeof(*split);
            split->next = block->next;
            split->free = 1u;
            split->reserved = 0u;
            block->next = split;
            block->size = size;
        }
        block->free = 0u;
        return block + 1;
    }
    return NULL;
}

int heap_free(void *allocation)
{
    if (allocation == NULL || heap_start == NULL) {
        return 0;
    }

    struct heap_block *previous = NULL;
    for (struct heap_block *block = first_block; block != NULL; block = block->next) {
        if ((void *)(block + 1) != allocation) {
            previous = block;
            continue;
        }
        if (block->free != 0u) {
            return 0;
        }
        block->free = 1u;
        if (block->next != NULL && block->next->free != 0u) {
            block->size += sizeof(*block) + block->next->size;
            block->next = block->next->next;
        }
        if (previous != NULL && previous->free != 0u) {
            previous->size += sizeof(*previous) + block->size;
            previous->next = block->next;
        }
        return 1;
    }
    return 0;
}

size_t heap_max_payload(void)
{
    return (size_t)(HEAP_SIZE - sizeof(struct heap_block));
}
