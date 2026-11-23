#include "process_test.h"

#include "elf64.h"
#include "memory.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "user.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096u
#define PROCESS_HANDLE_CAPACITY 5u
#define TEST_DATA_ADDRESS 0x00401000ull
#define TEST_READ_ONLY_ADDRESS 0x00402000ull
#define TEST_STACK_BASE 0x00007fffffff0000ull
#define TEST_VALUE_A 0xA11CE001u
#define TEST_VALUE_B 0xB0BCA002u

struct test_image {
    struct elf64_header header;
    struct elf64_program_header programs[3];
    uint8_t header_padding[PAGE_SIZE - sizeof(struct elf64_header) -
                           3u * sizeof(struct elf64_program_header)];
    uint8_t code[PAGE_SIZE];
    uint8_t data[PAGE_SIZE];
    uint8_t read_only[PAGE_SIZE];
} __attribute__((packed));

_Static_assert(sizeof(struct elf64_header) == 64u, "ELF64 header size");
_Static_assert(sizeof(struct elf64_program_header) == 56u, "ELF64 program header size");
_Static_assert(offsetof(struct test_image, code) == PAGE_SIZE, "ELF code offset");
_Static_assert(offsetof(struct test_image, data) == 2u * PAGE_SIZE, "ELF data offset");
_Static_assert(offsetof(struct test_image, read_only) == 3u * PAGE_SIZE, "ELF rodata offset");

static struct test_image image;

static void write_failure(const char *message)
{
    size_t length = 0u;
    while (length < 64u && message[length] != '\0') {
        ++length;
    }
    serial_write(message, length);
}

static void make_image(uint32_t data_value)
{
    image.header = (struct elf64_header){
        .ident = {0x7Fu, 'E', 'L', 'F', 2u, 1u, 1u},
        .type = ELF64_ET_EXEC,
        .machine = ELF64_EM_X86_64,
        .version = 1u,
        .entry = 0x00400000u,
        .program_offset = sizeof(struct elf64_header),
        .header_size = sizeof(struct elf64_header),
        .program_entry_size = sizeof(struct elf64_program_header),
        .program_count = 3u
    };
    image.programs[0] = (struct elf64_program_header){
        .type = ELF64_PT_LOAD,
        .flags = ELF64_PF_READ | ELF64_PF_EXECUTE,
        .offset = PAGE_SIZE,
        .virtual_address = 0x00400000u,
        .file_size = PAGE_SIZE,
        .memory_size = PAGE_SIZE,
        .alignment = PAGE_SIZE
    };
    image.programs[1] = (struct elf64_program_header){
        .type = ELF64_PT_LOAD,
        .flags = ELF64_PF_READ | ELF64_PF_WRITE,
        .offset = 2u * PAGE_SIZE,
        .virtual_address = TEST_DATA_ADDRESS,
        .file_size = sizeof(data_value),
        .memory_size = 2u * sizeof(data_value),
        .alignment = PAGE_SIZE
    };
    image.programs[2] = (struct elf64_program_header){
        .type = ELF64_PT_LOAD,
        .flags = ELF64_PF_READ,
        .offset = 3u * PAGE_SIZE,
        .virtual_address = TEST_READ_ONLY_ADDRESS,
        .file_size = sizeof(data_value),
        .memory_size = sizeof(data_value),
        .alignment = PAGE_SIZE
    };
    for (size_t i = 0u; i < PAGE_SIZE; ++i) {
        image.code[i] = 0u;
        image.data[i] = 0u;
        image.read_only[i] = 0u;
    }
    static const uint8_t success_code[] = {
        0x48u, 0xB8u, 0x00u, 0x10u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x48u, 0x8Bu, 0x38u, 0x89u, 0x38u, 0x57u, 0x5Fu,
        0xB8u, 0x02u, 0x00u, 0x00u, 0x00u, 0xCDu, 0x80u
    };
    static const uint8_t read_only_write[] = {
        0x48u, 0xB8u, 0x00u, 0x20u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xC6u, 0x00u, 0x00u, 0xB8u, 0x02u, 0x00u, 0x00u, 0x00u, 0xCDu, 0x80u
    };
    static const uint8_t data_execute[] = {
        0x48u, 0xB8u, 0x00u, 0x10u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xFFu, 0xE0u
    };
    static const uint8_t code_write[] = {
        0x48u, 0xB8u, 0x00u, 0x00u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0xC6u, 0x00u, 0x00u, 0xB8u, 0x02u, 0x00u, 0x00u, 0x00u, 0xCDu, 0x80u
    };
    for (size_t i = 0u; i < sizeof(success_code); ++i) {
        image.code[i] = success_code[i];
    }
    for (size_t i = 0u; i < sizeof(read_only_write); ++i) {
        image.code[32u + i] = read_only_write[i];
    }
    for (size_t i = 0u; i < sizeof(data_execute); ++i) {
        image.code[64u + i] = data_execute[i];
    }
    for (size_t i = 0u; i < sizeof(code_write); ++i) {
        image.code[80u + i] = code_write[i];
    }
    image.data[0] = (uint8_t)data_value;
    image.data[1] = (uint8_t)(data_value >> 8u);
    image.data[2] = (uint8_t)(data_value >> 16u);
    image.data[3] = (uint8_t)(data_value >> 24u);
    image.read_only[0] = 0x5Au;
}

enum malformed_image {
    BAD_MAGIC,
    BAD_CLASS,
    BAD_ENDIAN,
    BAD_IDENT_VERSION,
    BAD_TYPE,
    BAD_MACHINE,
    BAD_HEADER_SIZE,
    BAD_PROGRAM_SIZE,
    BAD_PROGRAM_VERSION,
    BAD_PROGRAM_COUNT,
    BAD_PROGRAM_OFFSET,
    TRUNCATED_HEADER,
    TRUNCATED_PROGRAM_TABLE,
    TRUNCATED_FILE_SEGMENT,
    BAD_FILE_RANGE,
    BAD_MEMORY_RANGE,
    BAD_VIRTUAL_RANGE,
    BAD_VIRTUAL_OVERFLOW,
    BAD_ALIGNMENT,
    BAD_OFFSET_ALIGNMENT,
    BAD_SEGMENT_ALIGNMENT,
    BAD_FLAGS_WX,
    BAD_FLAGS_UNKNOWN,
    BAD_FLAGS_READ,
    OVERLAPPING_SEGMENTS,
    UNSUPPORTED_SEGMENT,
    BAD_ENTRY,
    TOO_MANY_PAGES
};

static int expect_rejected(size_t image_size)
{
    const uint64_t free_before = page_free_count();
    struct process *loaded = process_load_elf(&image, image_size);
    int cleanup_ok = 1;
    if (loaded != NULL) {
        cleanup_ok = process_destroy(loaded);
    }
    return loaded == NULL && cleanup_ok && page_free_count() == free_before;
}

static size_t malformed_image_size(enum malformed_image test)
{
    switch (test) {
    case TRUNCATED_HEADER:
        return sizeof(image.header) - 1u;
    case TRUNCATED_PROGRAM_TABLE:
        return sizeof(struct elf64_header) +
               3u * sizeof(struct elf64_program_header) - 1u;
    case TRUNCATED_FILE_SEGMENT:
        return 3u * PAGE_SIZE + sizeof(uint32_t) - 1u;
    default:
        return sizeof(image);
    }
}

static int mutate_header(enum malformed_image test)
{
    switch (test) {
    case BAD_MAGIC:
        image.header.ident[0] = 0u;
        break;
    case BAD_CLASS:
        image.header.ident[4] = 1u;
        break;
    case BAD_ENDIAN:
        image.header.ident[5] = 2u;
        break;
    case BAD_IDENT_VERSION:
        image.header.ident[6] = 0u;
        break;
    case BAD_TYPE:
        image.header.type = 3u;
        break;
    case BAD_MACHINE:
        image.header.machine = 3u;
        break;
    case BAD_HEADER_SIZE:
        image.header.header_size = 0u;
        break;
    case BAD_PROGRAM_SIZE:
        image.header.program_entry_size = 1u;
        break;
    case BAD_PROGRAM_VERSION:
        image.header.version = 0u;
        break;
    case BAD_PROGRAM_COUNT:
        image.header.program_count = 9u;
        break;
    case BAD_PROGRAM_OFFSET:
        image.header.program_offset = UINT64_MAX - 1u;
        break;
    default:
        return 0;
    }
    return 1;
}

static int mutate_segment(enum malformed_image test)
{
    switch (test) {
    case BAD_FILE_RANGE:
        image.programs[1].offset = UINT64_MAX - 1u;
        break;
    case BAD_MEMORY_RANGE:
        image.programs[1].file_size = 9u;
        break;
    case BAD_VIRTUAL_RANGE:
        image.programs[1].virtual_address = 0x40000000u;
        break;
    case BAD_VIRTUAL_OVERFLOW:
        image.programs[1].virtual_address = UINT64_MAX - PAGE_SIZE + 1u;
        break;
    case BAD_ALIGNMENT:
        image.programs[1].virtual_address += 1u;
        break;
    case BAD_OFFSET_ALIGNMENT:
        image.programs[1].offset += 1u;
        break;
    case BAD_SEGMENT_ALIGNMENT:
        image.programs[1].alignment = PAGE_SIZE / 2u;
        break;
    case BAD_FLAGS_WX:
        image.programs[0].flags |= ELF64_PF_WRITE;
        break;
    case BAD_FLAGS_UNKNOWN:
        image.programs[1].flags |= 8u;
        break;
    case BAD_FLAGS_READ:
        image.programs[1].flags = ELF64_PF_WRITE;
        break;
    case OVERLAPPING_SEGMENTS:
        image.programs[1].virtual_address = 0x00400000u;
        break;
    case UNSUPPORTED_SEGMENT:
        image.programs[1].type = 3u;
        break;
    case BAD_ENTRY:
        image.header.entry = TEST_DATA_ADDRESS;
        break;
    case TOO_MANY_PAGES:
        image.programs[1].memory_size = 9u * PAGE_SIZE;
        break;
    default:
        return 0;
    }
    return 1;
}

static int mutate_malformed_image(enum malformed_image test)
{
    return mutate_header(test) || mutate_segment(test);
}

static int reject_malformed(enum malformed_image test)
{
    make_image(TEST_VALUE_A);
    const size_t image_size = malformed_image_size(test);
    if (image_size != sizeof(image)) {
        return expect_rejected(image_size);
    }
    return mutate_malformed_image(test) &&
           expect_rejected(image_size);
}


static int test_malformed_images(void)
{
    static const enum malformed_image tests[] = {
        BAD_MAGIC, BAD_CLASS, BAD_ENDIAN, BAD_IDENT_VERSION, BAD_TYPE,
        BAD_MACHINE, BAD_HEADER_SIZE, BAD_PROGRAM_SIZE, BAD_PROGRAM_VERSION,
        BAD_PROGRAM_COUNT, BAD_PROGRAM_OFFSET, TRUNCATED_HEADER,
        TRUNCATED_PROGRAM_TABLE, TRUNCATED_FILE_SEGMENT, BAD_FILE_RANGE,
        BAD_MEMORY_RANGE, BAD_VIRTUAL_RANGE, BAD_VIRTUAL_OVERFLOW, BAD_ALIGNMENT,
        BAD_OFFSET_ALIGNMENT, BAD_SEGMENT_ALIGNMENT, BAD_FLAGS_WX,
        BAD_FLAGS_UNKNOWN, BAD_FLAGS_READ, OVERLAPPING_SEGMENTS,
        UNSUPPORTED_SEGMENT, BAD_ENTRY, TOO_MANY_PAGES
    };
    for (size_t i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!reject_malformed(tests[i])) {
            write_failure("AK5: malformed ELF acceptance or cleanup failed\r\n");
            return 0;
        }
    }
    return 1;
}

static int execute_process(
    const struct process *process,
    uint64_t entry,
    uint64_t expected_vector,
    uint64_t expected_error,
    uint64_t expected_exit)
{
    return user_mode_execute_test(
        process_address_space(process), entry, process_stack_top(process),
        expected_vector, expected_error, expected_exit);
}

static int destroy_processes(struct process **processes)
{
    int success = 1;
    for (size_t i = 0u; i < PROCESS_HANDLE_CAPACITY; ++i) {
        if (processes[i] != NULL) {
            if (!process_destroy(processes[i])) {
                success = 0;
            } else {
                processes[i] = NULL;
            }
        }
    }
    return success;
}

static const char *load_four_processes(
    struct process **processes,
    uint64_t *full_count)
{
    make_image(TEST_VALUE_A);
    processes[0] = process_load_elf(&image, sizeof(image));
    if (processes[0] == NULL) {
        return "AK5: valid ELF was rejected\r\n";
    }
    make_image(TEST_VALUE_B);
    processes[1] = process_load_elf(&image, sizeof(image));
    processes[2] = process_load_elf(&image, sizeof(image));
    processes[3] = process_load_elf(&image, sizeof(image));
    if (processes[1] == NULL || processes[2] == NULL || processes[3] == NULL) {
        return "AK5: process capacity below four\r\n";
    }
    *full_count = page_free_count();
    processes[4] = process_load_elf(&image, sizeof(image));
    if (processes[4] != NULL || page_free_count() != *full_count) {
        return "AK5: fifth process changed resources\r\n";
    }
    return NULL;
}

static int test_process_execution(struct process **processes)
{
    if (!execute_process(processes[0], process_entry_point(processes[0]),
                         0u, 0u, TEST_VALUE_A) ||
        !execute_process(processes[1], process_entry_point(processes[1]),
                         0u, 0u, TEST_VALUE_B)) {
        return 0;
    }
    return execute_process(processes[0], process_entry_point(processes[0]) + 32u,
                           14u, 7u, UINT64_MAX) &&
           execute_process(processes[0], process_entry_point(processes[0]) + 80u,
                           14u, 7u, UINT64_MAX) &&
           execute_process(processes[0], TEST_DATA_ADDRESS,
                           14u, 21u, UINT64_MAX) &&
           execute_process(processes[0], TEST_STACK_BASE,
                           14u, 21u, UINT64_MAX);
}

static int test_active_root_and_slot_reuse(
    struct process **processes,
    uint64_t kernel_root,
    uint64_t full_count)
{
    paging_activate_root(process_address_space(processes[0]));
    const int destroyed_active = process_destroy(processes[0]);
    paging_activate_root(kernel_root);
    if (destroyed_active != 0 || page_free_count() != full_count) {
        if (destroyed_active != 0) {
            processes[0] = NULL;
        }
        return 0;
    }
    if (!process_destroy(processes[0])) {
        return 0;
    }
    processes[0] = NULL;
    make_image(TEST_VALUE_A);
    processes[4] = process_load_elf(&image, sizeof(image));
    return processes[4] != NULL;
}

static int test_process_lifecycle(void)
{
    struct process *processes[PROCESS_HANDLE_CAPACITY] = {0};
    const uint64_t free_before = page_free_count();
    const uint64_t kernel_root = paging_current_root();
    uint64_t full_count = 0u;
    const char *failure = load_four_processes(processes, &full_count);
    int success = 0;

    if (failure == NULL && !test_process_execution(processes)) {
        failure = "AK5: process isolation or permissions failed\r\n";
    }
    if (failure == NULL &&
        !test_active_root_and_slot_reuse(processes, kernel_root, full_count)) {
        failure = "AK5: active-root teardown or slot reuse failed\r\n";
    }

    paging_activate_root(kernel_root);
    if (!destroy_processes(processes) || page_free_count() != free_before) {
        failure = "AK5: process teardown leaked pages or slots\r\n";
    }
    if (failure != NULL) {
        write_failure(failure);
    } else {
        success = 1;
    }
    return success;
}

int process_self_test(void)
{
    if (!test_malformed_images()) {
        return 0;
    }
    return test_process_lifecycle();
}
