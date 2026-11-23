#include "user.h"

#include "memory.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096u
#define USER_CODE_ADDRESS 0x00400000ull
#define USER_STACK_ADDRESS 0x00401000ull
#define USER_STACK_TOP (USER_STACK_ADDRESS + PAGE_SIZE)
#define SYSCALL_SERVICE 1u
#define SYSCALL_BUFFER_SUM 3u
#define SYSCALL_EXIT 2u


struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss64 tss;
static uint8_t kernel_entry_stack[16384] __attribute__((aligned(16)));
static volatile uint64_t probe_active;
static volatile uint64_t probe_exit_status;
static volatile uint64_t probe_fault_vector;
static volatile uint64_t probe_fault_error;

uint64_t user_arch_resume_stack;
uint64_t user_arch_resume_flags;

extern void user_arch_load_gdt(const struct gdt_pointer *pointer);
extern void user_arch_enter(uint64_t entry, uint64_t user_stack_top);
extern void user_arch_resume(void) __attribute__((noreturn));
extern const uint8_t user_payload_start[];
extern const uint8_t user_payload_end[];
extern const uint8_t user_probe_success[];
extern const uint8_t user_probe_kernel_read[];
extern const uint8_t user_probe_privileged[];
extern const uint8_t user_probe_write_code[];
extern const uint8_t user_probe_execute_stack[];

extern const uint8_t user_probe_buffer_valid[];
extern const uint8_t user_probe_buffer_null[];
extern const uint8_t user_probe_buffer_unmapped[];
extern const uint8_t user_probe_buffer_cross_page[];
extern const uint8_t user_probe_buffer_overflow[];
extern const uint8_t user_probe_buffer_length_zero[];
extern const uint8_t user_probe_buffer_length_large[];


int user_arch_init(void)
{
    gdt[1] = 0x00AF9A000000FFFFull;
    gdt[2] = 0x00CF92000000FFFFull;
    gdt[3] = 0x00CFF2000000FFFFull;
    gdt[4] = 0x00AFFA000000FFFFull;
    tss.rsp0 = (uintptr_t)(kernel_entry_stack + sizeof(kernel_entry_stack));
    tss.iomap_base = (uint16_t)sizeof(tss);

    const uint64_t base = (uintptr_t)&tss;
    const uint64_t limit = sizeof(tss) - 1u;
    gdt[5] = (limit & 0xFFFFu) | ((base & 0xFFFFFFu) << 16u) |
             (0x89ull << 40u) | ((limit & 0xF0000u) << 32u) |
             ((base & 0xFF000000u) << 32u);
    gdt[6] = base >> 32u;
    const struct gdt_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uintptr_t)gdt
    };
    user_arch_load_gdt(&pointer);
    return 1;
}

static int copy_user_payload(uint64_t physical_page)
{
    const uintptr_t start = (uintptr_t)user_payload_start;
    const uintptr_t end = (uintptr_t)user_payload_end;
    if (end <= start || end - start > PAGE_SIZE) {
        return 0;
    }
    uint8_t *destination = physical_to_virtual(physical_page);
    if (destination == NULL) {
        return 0;
    }
    const uint8_t *source = user_payload_start;
    for (size_t i = 0u; i < end - start; ++i) {
        destination[i] = source[i];
    }
    return 1;
}

static int run_user_entry(
    uint64_t user_root,
    uint64_t kernel_root,
    uint64_t entry,
    uint64_t stack_top,
    uint64_t expected_vector,
    uint64_t expected_error,
    uint64_t expected_exit)
{
    if (user_root == 0u || kernel_root == 0u || user_root == kernel_root ||
        entry == 0u || stack_top == 0u ||
        !paging_user_root_is_safe(user_root)) {
        return 0;
    }
    probe_active = 1u;
    probe_exit_status = UINT64_MAX;
    probe_fault_vector = 0u;
    probe_fault_error = 0u;
    paging_activate_root(user_root);
    user_arch_enter(entry, stack_top);
    paging_activate_root(kernel_root);
    probe_active = 0u;
    if (expected_vector == 0u) {
        return probe_fault_vector == 0u && probe_exit_status == expected_exit;
    }
    return probe_fault_vector == expected_vector &&
           probe_fault_error == expected_error &&
           probe_exit_status == UINT64_MAX;
}

static int run_user_probe(
    uint64_t user_root,
    uint64_t kernel_root,
    uint64_t user_code_address,
    uintptr_t entry_offset,
    uint64_t expected_vector,
    uint64_t expected_error)
{
    return run_user_entry(
        user_root, kernel_root, user_code_address + entry_offset,
        USER_STACK_TOP, expected_vector, expected_error, 0u);
}

static int test_user_buffers(uint64_t user_root, uint64_t kernel_root)
{
    const uintptr_t payload_start = (uintptr_t)user_payload_start;
    const uintptr_t probes[] = {
        (uintptr_t)user_probe_buffer_valid,
        (uintptr_t)user_probe_buffer_null,
        (uintptr_t)user_probe_buffer_unmapped,
        (uintptr_t)user_probe_buffer_cross_page,
        (uintptr_t)user_probe_buffer_overflow,
        (uintptr_t)user_probe_buffer_length_zero,
        (uintptr_t)user_probe_buffer_length_large
    };
    for (size_t i = 0u; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        if (!run_user_probe(
                user_root, kernel_root, USER_CODE_ADDRESS,
                probes[i] - payload_start, 0u, 0u)) {
            static const char message[] = "AK5: user-buffer syscall failed\r\n";
            serial_write(message, sizeof(message) - 1u);
            return 0;
        }
    }
    return 1;
}


int user_mode_execute_test(
    uint64_t root,
    uint64_t entry,
    uint64_t stack_top,
    uint64_t expected_vector,
    uint64_t expected_error,
    uint64_t expected_exit)
{
    return run_user_entry(
        root, paging_current_root(), entry, stack_top,
        expected_vector, expected_error, expected_exit);
}

int user_mode_self_test(void)
{
    const uintptr_t payload_start = (uintptr_t)user_payload_start;
    const uintptr_t success_offset = (uintptr_t)user_probe_success - payload_start;
    const uintptr_t read_offset = (uintptr_t)user_probe_kernel_read - payload_start;
    const uintptr_t write_offset = (uintptr_t)user_probe_write_code - payload_start;
    const uintptr_t execute_offset =
        (uintptr_t)user_probe_execute_stack - payload_start;
    const uintptr_t privileged_offset =
        (uintptr_t)user_probe_privileged - payload_start;
    const uint64_t kernel_root = paging_current_root();
    uint64_t user_root = paging_user_root_create();
    uint64_t code_page = 0u;
    uint64_t stack_page = 0u;
    int code_mapped = 0;
    int stack_mapped = 0;
    int success = 0;
    if (user_root == 0u) {
        static const char message[] = "AK5: user root creation failed\r\n";
        serial_write(message, sizeof(message) - 1u);
        return 0;
    }
    code_page = page_alloc();
    stack_page = page_alloc();
    if (code_page == 0u || stack_page == 0u || !copy_user_payload(code_page)) {
        static const char message[] = "AK5: user pages unavailable\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    code_mapped = paging_user_map_page(
        user_root, USER_CODE_ADDRESS, code_page, 0, 1);
    if (code_mapped == 0) {
        static const char message[] = "AK5: user code mapping failed\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    stack_mapped = paging_user_map_page(
        user_root, USER_STACK_ADDRESS, stack_page, 1, 0);
    if (stack_mapped == 0 || !paging_user_root_is_safe(user_root)) {
        static const char message[] = "AK5: user stack mapping failed\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }

    if (!run_user_probe(user_root, kernel_root, USER_CODE_ADDRESS,
                        success_offset, 0u, 0u)) {
        static const char message[] = "AK5: scalar syscall failed\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    if (!run_user_probe(user_root, kernel_root, USER_CODE_ADDRESS,
                        read_offset, 14u, 5u)) {
        static const char message[] = "AK5: supervisor read not contained\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    if (!run_user_probe(user_root, kernel_root, USER_CODE_ADDRESS,
                        write_offset, 14u, 7u)) {
        static const char message[] = "AK5: user code is writable\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    if (!run_user_probe(user_root, kernel_root, USER_CODE_ADDRESS,
                        execute_offset, 14u, 21u)) {
        static const char message[] = "AK5: user stack is executable\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    if (!run_user_probe(user_root, kernel_root, USER_CODE_ADDRESS,
                        privileged_offset, 13u, 0u)) {
        static const char message[] = "AK5: privileged instruction not contained\r\n";
        serial_write(message, sizeof(message) - 1u);
        goto cleanup;
    }
    if (!test_user_buffers(user_root, kernel_root)) {
        goto cleanup;
    }
    success = 1;

cleanup:
    if (code_page != 0u && code_mapped == 0 && !page_free(code_page)) {
        success = 0;
    }
    if (stack_page != 0u && stack_mapped == 0 && !page_free(stack_page)) {
        success = 0;
    }
    if (!paging_user_root_destroy(user_root)) {
        success = 0;
    }
    return success;
}

int user_handle_trap(
    uint64_t vector,
    uint64_t error,
    uint64_t code_selector,
    uint64_t *result,
    uint64_t argument,
    uint64_t second_argument)
{
    if (probe_active == 0u || (code_selector & 3u) != 3u) {
        return 0;
    }
    if (vector == 0x80u) {
        if (*result == SYSCALL_SERVICE) {
            *result = argument + 7u;
            return 1;
        }
        if (*result == SYSCALL_BUFFER_SUM) {
            uint8_t buffer[32];
            if (second_argument > sizeof(buffer) ||
                !paging_user_copy_in(
                    argument, buffer, (size_t)second_argument)) {
                *result = UINT64_MAX;
                return 1;
            }
            uint64_t sum = 0u;
            for (size_t i = 0u; i < (size_t)second_argument; ++i) {
                sum += buffer[i];
            }
            *result = sum;
            return 1;
        }
        if (*result == SYSCALL_EXIT) {
            probe_exit_status = argument;
            probe_active = 0u;
            user_arch_resume();
            __builtin_unreachable();
        }
        *result = UINT64_MAX;
        return 1;
    }
    if (vector < 32u && vector != 2u && vector != 8u && vector != 18u) {
        probe_fault_vector = vector;
        probe_fault_error = error;
        probe_exit_status = UINT64_MAX;
        probe_active = 0u;
        user_arch_resume();
        __builtin_unreachable();
    }
    return 0;
}
