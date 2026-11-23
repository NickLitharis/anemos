#ifndef ANEMOS_KERNEL_PROCESS_H
#define ANEMOS_KERNEL_PROCESS_H

#include <stddef.h>
#include <stdint.h>

struct process;

struct process *process_load_elf(const void *image, size_t image_size);
int process_destroy(struct process *process);
uint64_t process_address_space(const struct process *process);
uint64_t process_entry_point(const struct process *process);
uint64_t process_stack_top(const struct process *process);

#endif
