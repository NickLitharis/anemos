#include "user_root_test.h"

#include "memory.h"
#include "paging.h"
#include "serial.h"

#include <stdint.h>

#define TEST_ADDRESS 0x00600000ull

static int release_test_root(uint64_t root, uint64_t page, int mapped)
{
    int success = 1;
    if (page != 0u && mapped == 0 && !page_free(page)) {
        success = 0;
    }
    if (root != 0u && !paging_user_root_destroy(root)) {
        success = 0;
    }
    return success;
}

static void write_failure(const char *message)
{
    size_t length = 0u;
    while (length < 64u && message[length] != '\0') {
        ++length;
    }
    serial_write(message, length);
}

int user_root_self_test(void)
{
    const uint64_t free_before = page_free_count();
    const uint64_t kernel_root = paging_current_root();
    uint64_t root_a = 0u;
    uint64_t root_b = 0u;
    uint64_t page_a = 0u;
    uint64_t page_b = 0u;
    int mapped_a = 0;
    int mapped_b = 0;
    const char *failure = NULL;

    root_a = paging_user_root_create();
    if (root_a == 0u) {
        failure = "AK5: first process root unavailable\r\n";
        goto cleanup;
    }
    page_a = page_alloc();
    if (page_a == 0u) {
        failure = "AK5: first process page unavailable\r\n";
        goto cleanup;
    }
    *(volatile uint64_t *)physical_to_virtual(page_a) = 0xA11CE001u;
    mapped_a = paging_user_map_page(root_a, TEST_ADDRESS, page_a, 1, 0);
    if (mapped_a == 0) {
        failure = "AK5: first process mapping failed\r\n";
        goto cleanup;
    }

    root_b = paging_user_root_create();
    if (root_b == 0u) {
        failure = "AK5: second process root unavailable\r\n";
        goto cleanup;
    }
    page_b = page_alloc();
    if (page_b == 0u) {
        failure = "AK5: second process page unavailable\r\n";
        goto cleanup;
    }
    *(volatile uint64_t *)physical_to_virtual(page_b) = 0xB0BCA002u;
    mapped_b = paging_user_map_page(root_b, TEST_ADDRESS, page_b, 1, 0);
    if (mapped_b == 0 || !paging_user_root_is_safe(root_a) ||
        !paging_user_root_is_safe(root_b)) {
        failure = "AK5: process root setup failed\r\n";
        goto cleanup;
    }

    paging_activate_root(root_a);
    if (*(volatile uint64_t *)(uintptr_t)TEST_ADDRESS != 0xA11CE001u) {
        failure = "AK5: first root did not isolate its page\r\n";
        goto cleanup;
    }
    paging_activate_root(root_b);
    if (*(volatile uint64_t *)(uintptr_t)TEST_ADDRESS != 0xB0BCA002u) {
        failure = "AK5: second root did not isolate its page\r\n";
        goto cleanup;
    }

cleanup:
    paging_activate_root(kernel_root);
    const int release_b = release_test_root(root_b, page_b, mapped_b);
    const int release_a = release_test_root(root_a, page_a, mapped_a);
    if (!release_b || !release_a || page_free_count() != free_before) {
        failure = "AK5: process root cleanup leaked pages\r\n";
    }
    if (failure != NULL) {
        write_failure(failure);
        return 0;
    }
    return 1;
}
