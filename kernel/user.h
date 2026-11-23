#ifndef ANEMOS_KERNEL_USER_H
#define ANEMOS_KERNEL_USER_H

#include <stdint.h>

int user_arch_init(void);
int user_mode_self_test(void);
int user_mode_execute_test(
    uint64_t root,
    uint64_t entry,
    uint64_t stack_top,
    uint64_t expected_vector,
    uint64_t expected_error,
    uint64_t expected_exit);
int user_handle_trap(
    uint64_t vector,
    uint64_t error,
    uint64_t code_selector,
    uint64_t *result,
    uint64_t argument,
    uint64_t second_argument);

#endif
