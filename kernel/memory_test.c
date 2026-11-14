#include "memory_test.h"

#include "heap.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define TEST_FAIL(message) do { \
    static const char failure[] = message "\r\n"; \
    serial_write(failure, sizeof(failure) - 1u); \
    return 0; \
} while (0)
#define PAGE_SIZE 4096ull

int memory_system_init(void)
{
    static const char map_error[] = "AK3: Limine memory map unavailable\r\n";
    static const char paging_error[] = "AK3: page-table setup failed\r\n";
    static const char heap_error[] = "AK3: heap setup failed\r\n";
    if (!memory_init()) {
        serial_write(map_error, sizeof(map_error) - 1u);
        return 0;
    }
    if (!paging_init()) {
        serial_write(paging_error, sizeof(paging_error) - 1u);
        return 0;
    }
    if (!heap_init()) {
        serial_write(heap_error, sizeof(heap_error) - 1u);
        return 0;
    }
    return 1;
}

int memory_self_test(void)
{
    uint64_t free_before = page_free_count();
    uint64_t first = page_alloc();
    uint64_t second = page_alloc();
    if (first == 0u || second == 0u || first == second ||
        (first & (PAGE_SIZE - 1u)) != 0u || (second & (PAGE_SIZE - 1u)) != 0u) {
        TEST_FAIL("AK3: page allocation invariant failed");
    }

    volatile uint64_t *first_alias = physical_to_virtual(first);
    volatile uint64_t *second_alias = physical_to_virtual(second);
    if (first_alias == NULL || second_alias == NULL ||
        first_alias[0] != 0u || second_alias[0] != 0u) {
        TEST_FAIL("AK3: zero-page invariant failed");
    }
    second_alias[0] = 0xA3E0051u;
    if (!page_free(first) || !page_free(second)) {
        TEST_FAIL("AK3: page free invariant failed");
    }
    uint64_t reused = page_alloc();
    volatile uint64_t *reused_alias = physical_to_virtual(reused);
    if (reused != second || reused_alias == NULL || reused_alias[0] != 0u) {
        TEST_FAIL("AK3: page reuse invariant failed");
    }
    if (!page_free(reused) || page_free(first) || page_free_count() != free_before) {
        TEST_FAIL("AK3: page free/count invariant failed");
    }

    uint64_t mapped_first = page_alloc();
    uint64_t mapped_second = page_alloc();
    uint64_t map_base = paging_region_base();
    if (mapped_first == 0u || mapped_second == 0u ||
        !paging_map_page(map_base, mapped_first) ||
        !paging_map_page(map_base + PAGE_SIZE, mapped_second)) {
        TEST_FAIL("AK3: virtual map setup failed");
    }
    volatile uint64_t *virtual_first = (volatile uint64_t *)(uintptr_t)map_base;
    volatile uint64_t *virtual_second = (volatile uint64_t *)(uintptr_t)(map_base + PAGE_SIZE);
    volatile uint64_t *physical_first = physical_to_virtual(mapped_first);
    volatile uint64_t *physical_second = physical_to_virtual(mapped_second);
    virtual_first[0] = 0x123456789ABCDEF0ull;
    virtual_second[0] = 0x0FEDCBA987654321ull;
    if (physical_first == NULL || physical_second == NULL ||
        physical_first[0] != virtual_first[0] ||
        physical_second[0] != virtual_second[0] ||
        virtual_first[0] == virtual_second[0]) {
        TEST_FAIL("AK3: virtual alias invariant failed");
    }

    void *small = heap_alloc(64u);
    void *large = heap_alloc(256u);
    if (small == NULL || large == NULL ||
        ((uintptr_t)small & 15u) != 0u || ((uintptr_t)large & 15u) != 0u ||
        !heap_free(small) || !heap_free(large) || heap_free(large)) {
        TEST_FAIL("AK3: heap split/free invariant failed");
    }

    size_t maximum = heap_max_payload();
    void *whole_heap = heap_alloc(maximum);
    if (whole_heap == NULL || heap_alloc(16u) != NULL || !heap_free(whole_heap)) {
        TEST_FAIL("AK3: heap exhaustion/coalesce invariant failed");
    }
    whole_heap = heap_alloc(maximum);
    if (whole_heap == NULL || !heap_free(whole_heap)) {
        TEST_FAIL("AK3: heap reuse invariant failed");
    }
    return 1;
}
