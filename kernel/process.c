#include "process.h"

#include "elf64.h"
#include "memory.h"
#include "paging.h"

#include <stddef.h>
#include <stdint.h>

#define PROCESS_CAPACITY 4u
#define PROCESS_LOADING 2u
#define PROCESS_ACTIVE 1u
#define ELF_PROGRAM_CAPACITY 8u
#define ELF_IMAGE_PAGE_CAPACITY 8u
#define PAGE_SIZE 4096u
#define PAGE_MASK (PAGE_SIZE - 1u)
#define ELF_USER_BASE 0x00400000ull
#define ELF_USER_LIMIT 0x40000000ull
#define USER_STACK_BASE 0x00007fffffff0000ull
#define USER_STACK_TOP (USER_STACK_BASE + PAGE_SIZE)

struct process {
    uint64_t root;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t pending_page;
    unsigned state;
};

struct load_segment {
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t page_count;
    uint32_t flags;
};

struct validated_image {
    uint64_t entry;
    unsigned segment_count;
    uint64_t page_count;
    struct load_segment segments[ELF_PROGRAM_CAPACITY];
};

static struct process processes[PROCESS_CAPACITY];

_Static_assert(sizeof(struct elf64_header) == 64u, "ELF64 header size");
_Static_assert(sizeof(struct elf64_program_header) == 56u, "ELF64 program header size");

static int file_range_fits(size_t image_size, uint64_t offset, uint64_t length)
{
    const uint64_t available = (uint64_t)image_size;
    return offset <= available && length <= available - offset;
}

static int valid_header(
    const uint8_t *image,
    size_t image_size,
    const struct elf64_header **header_out)
{
    if (image == NULL || image_size < sizeof(struct elf64_header)) {
        return 0;
    }
    const struct elf64_header *header =
        (const struct elf64_header *)image;
    if (header->ident[0] != 0x7Fu || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2u || header->ident[5] != 1u ||
        header->ident[6] != 1u || header->ident[7] != 0u ||
        header->ident[8] != 0u || header->type != ELF64_ET_EXEC ||
        header->machine != ELF64_EM_X86_64 || header->version != 1u ||
        header->flags != 0u ||
        header->header_size != sizeof(struct elf64_header) ||
        header->program_entry_size != sizeof(struct elf64_program_header) ||
        header->program_count == 0u ||
        header->program_count > ELF_PROGRAM_CAPACITY ||
        header->program_offset < sizeof(struct elf64_header)) {
        return 0;
    }
    const uint64_t table_size =
        (uint64_t)header->program_count * sizeof(struct elf64_program_header);
    if (!file_range_fits(image_size, header->program_offset, table_size)) {
        return 0;
    }
    *header_out = header;
    return 1;
}

static int valid_load_segment(
    size_t image_size,
    const struct elf64_program_header *program,
    struct load_segment *segment)
{
    const uint32_t allowed_flags =
        ELF64_PF_READ | ELF64_PF_WRITE | ELF64_PF_EXECUTE;
    if (program->type != ELF64_PT_LOAD ||
        (program->flags & ~allowed_flags) != 0u ||
        (program->flags & ELF64_PF_READ) == 0u ||
        (program->flags & (ELF64_PF_WRITE | ELF64_PF_EXECUTE)) ==
            (ELF64_PF_WRITE | ELF64_PF_EXECUTE) ||
        program->alignment != PAGE_SIZE ||
        (program->offset & PAGE_MASK) != 0u ||
        (program->virtual_address & PAGE_MASK) != 0u ||
        program->memory_size == 0u ||
        program->file_size > program->memory_size ||
        !file_range_fits(image_size, program->offset, program->file_size) ||
        program->virtual_address < ELF_USER_BASE ||
        program->virtual_address >= ELF_USER_LIMIT ||
        program->memory_size > ELF_USER_LIMIT - program->virtual_address) {
        return 0;
    }
    const uint64_t pages =
        (program->memory_size + PAGE_MASK) / PAGE_SIZE;
    if (pages == 0u ||
        pages > (ELF_USER_LIMIT - program->virtual_address) / PAGE_SIZE) {
        return 0;
    }
    segment->offset = program->offset;
    segment->virtual_address = program->virtual_address;
    segment->file_size = program->file_size;
    segment->memory_size = program->memory_size;
    segment->page_count = pages;
    segment->flags = program->flags;
    return 1;
}

static int segments_overlap(
    const struct load_segment *left,
    const struct load_segment *right)
{
    const uint64_t left_end =
        left->virtual_address + left->page_count * PAGE_SIZE;
    const uint64_t right_end =
        right->virtual_address + right->page_count * PAGE_SIZE;
    return left->virtual_address < right_end &&
           right->virtual_address < left_end;
}

static int entry_is_executable(const struct validated_image *validated)
{
    for (unsigned i = 0u; i < validated->segment_count; ++i) {
        const struct load_segment *segment = &validated->segments[i];
        if ((segment->flags & ELF64_PF_EXECUTE) != 0u &&
            validated->entry >= segment->virtual_address &&
            validated->entry - segment->virtual_address < segment->file_size) {
            return 1;
        }
    }
    return 0;
}

static int validate_elf(
    const void *image,
    size_t image_size,
    struct validated_image *validated)
{
    const struct elf64_header *header = NULL;
    if (!valid_header(image, image_size, &header)) {
        return 0;
    }
    validated->entry = header->entry;
    validated->segment_count = 0u;
    validated->page_count = 0u;
    const uint8_t *bytes = image;
    const uint8_t *program_bytes =
        bytes + (size_t)header->program_offset;
    for (unsigned i = 0u; i < header->program_count; ++i) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)(program_bytes +
                i * sizeof(struct elf64_program_header));
        struct load_segment segment;
        if (!valid_load_segment(image_size, program, &segment) ||
            segment.page_count >
                ELF_IMAGE_PAGE_CAPACITY - validated->page_count) {
            return 0;
        }
        for (unsigned j = 0u; j < validated->segment_count; ++j) {
            if (segments_overlap(&segment, &validated->segments[j])) {
                return 0;
            }
        }
        validated->segments[validated->segment_count++] = segment;
        validated->page_count += segment.page_count;
    }
    return validated->segment_count != 0u &&
           entry_is_executable(validated);
}

static struct process *find_free_process(void)
{
    for (unsigned i = 0u; i < PROCESS_CAPACITY; ++i) {
        if (processes[i].state == 0u) {
            return &processes[i];
        }
    }
    return NULL;
}

static struct process *find_active_process(const struct process *candidate)
{
    if (candidate == NULL) {
        return NULL;
    }
    for (unsigned i = 0u; i < PROCESS_CAPACITY; ++i) {
        if (&processes[i] == candidate &&
            processes[i].state == PROCESS_ACTIVE) {
            return &processes[i];
        }
    }
    return NULL;
}

static int load_segment_pages(
    struct process *process,
    const uint8_t *image,
    const struct load_segment *segment)
{
    for (uint64_t page_index = 0u;
         page_index < segment->page_count; ++page_index) {
        const uint64_t physical = page_alloc();
        if (physical == 0u) {
            return 0;
        }
        process->pending_page = physical;
        uint8_t *destination = physical_to_virtual(physical);
        if (destination == NULL) {
            return 0;
        }
        const uint64_t file_offset = page_index * PAGE_SIZE;
        if (file_offset < segment->file_size) {
            uint64_t remaining = segment->file_size - file_offset;
            if (remaining > PAGE_SIZE) {
                remaining = PAGE_SIZE;
            }
            const uint8_t *source =
                image + (size_t)(segment->offset + file_offset);
            for (size_t i = 0u; i < (size_t)remaining; ++i) {
                destination[i] = source[i];
            }
        }
        if (!paging_user_map_page(
                process->root, segment->virtual_address + file_offset,
                physical, (segment->flags & ELF64_PF_WRITE) != 0u,
                (segment->flags & ELF64_PF_EXECUTE) != 0u)) {
            return 0;
        }
        process->pending_page = 0u;
    }
    return 1;
}

static int map_image(
    struct process *process,
    const uint8_t *image,
    const struct validated_image *validated)
{
    for (unsigned i = 0u; i < validated->segment_count; ++i) {
        if (!load_segment_pages(process, image, &validated->segments[i])) {
            return 0;
        }
    }
    const uint64_t stack = page_alloc();
    if (stack == 0u) {
        return 0;
    }
    process->pending_page = stack;
    if (!paging_user_map_page(process->root, USER_STACK_BASE, stack, 1, 0)) {
        return 0;
    }
    process->pending_page = 0u;
    return paging_user_root_is_safe(process->root);
}

static int release_pending_page(struct process *process)
{
    if (process->pending_page == 0u) {
        return 1;
    }
    if (!page_free(process->pending_page)) {
        return 0;
    }
    process->pending_page = 0u;
    return 1;
}

static void clear_process(struct process *process)
{
    process->root = 0u;
    process->entry = 0u;
    process->stack_top = 0u;
    process->pending_page = 0u;
    process->state = 0u;
}

struct process *process_load_elf(const void *image, size_t image_size)
{
    struct validated_image validated;
    if (!validate_elf(image, image_size, &validated)) {
        return NULL;
    }
    struct process *process = find_free_process();
    if (process == NULL) {
        return NULL;
    }
    process->state = PROCESS_LOADING;
    process->pending_page = 0u;
    const uint64_t root = paging_user_root_create();
    if (root == 0u) {
        clear_process(process);
        return NULL;
    }
    process->root = root;
    if (!map_image(process, image, &validated)) {
        if (release_pending_page(process) &&
            paging_user_root_destroy(root)) {
            clear_process(process);
        }
        return NULL;
    }
    process->entry = validated.entry;
    process->stack_top = USER_STACK_TOP;
    process->state = PROCESS_ACTIVE;
    return process;
}

int process_destroy(struct process *candidate)
{
    struct process *process = find_active_process(candidate);
    if (process == NULL || paging_current_root() == process->root ||
        !paging_user_root_destroy(process->root)) {
        return 0;
    }
    clear_process(process);
    return 1;
}

uint64_t process_address_space(const struct process *candidate)
{
    const struct process *process = find_active_process(candidate);
    return process == NULL ? 0u : process->root;
}

uint64_t process_entry_point(const struct process *candidate)
{
    const struct process *process = find_active_process(candidate);
    return process == NULL ? 0u : process->entry;
}

uint64_t process_stack_top(const struct process *candidate)
{
    const struct process *process = find_active_process(candidate);
    return process == NULL ? 0u : process->stack_top;
}
