#ifndef ANEMOS_KERNEL_ELF64_H
#define ANEMOS_KERNEL_ELF64_H

#include <stdint.h>

#define ELF64_ET_EXEC 2u
#define ELF64_EM_X86_64 62u
#define ELF64_PT_LOAD 1u
#define ELF64_PF_EXECUTE 1u
#define ELF64_PF_WRITE 2u
#define ELF64_PF_READ 4u

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_string_index;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

#endif
