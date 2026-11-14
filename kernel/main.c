#include "interrupts.h"
#include "memory_test.h"
#include "serial.h"

static __attribute__((noreturn)) void trigger_divide_by_zero(void)
{
    __asm__ volatile (
        "xorl %%edx, %%edx\n\t"
        "movl $1, %%eax\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "divl %%ecx"
        :
        :
        : "rax", "rcx", "rdx", "cc");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void _start(void)
{
    static const char boot_message[] = "ANEMOS: boot ok\r\n";
    static const char memory_message[] = "AK3: memory tests ok\r\n";
    static const char memory_error[] = "AK3: memory setup failed\r\n";
    static const char timer_message[] = "AK2: timer ok\r\n";
    serial_init();
    serial_write(boot_message, sizeof(boot_message) - 1u);
    static const char test_error[] = "AK3: memory invariant failed\r\n";
    if (!memory_system_init()) {
        serial_write(memory_error, sizeof(memory_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    if (!memory_self_test()) {
        serial_write(test_error, sizeof(test_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    serial_write(memory_message, sizeof(memory_message) - 1u);

    static const char interrupt_error[] = "AK3: interrupt setup failed\r\n";
    if (!interrupts_init()) {
        serial_write(interrupt_error, sizeof(interrupt_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    timer_init();
    __asm__ volatile ("sti");
    while (timer_ticks() < 3u) {
        __asm__ volatile ("hlt");
    }
    serial_write(timer_message, sizeof(timer_message) - 1u);
    trigger_divide_by_zero();
}
