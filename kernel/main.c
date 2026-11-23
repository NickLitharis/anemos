#include "interrupts.h"
#include "memory_test.h"
#include "serial.h"
#include "thread.h"
#include "user.h"
#include "user_root_test.h"
#include "process_test.h"

#include "event_test.h"
static int ak5_self_test(void)
{
    static const char privilege_message[] = "AK5: privilege tests ok\r\n";
    static const char address_space_message[] = "AK5: address-space tests ok\r\n";
    if (!user_mode_self_test()) {
        return 0;
    }
    serial_write(privilege_message, sizeof(privilege_message) - 1u);
    if (!user_root_self_test()) {
        return 0;
    }
    serial_write(address_space_message, sizeof(address_space_message) - 1u);
    static const char process_message[] = "AK5: process tests ok\r\n";
    if (!process_self_test()) {
        return 0;
    }
    serial_write(process_message, sizeof(process_message) - 1u);
    return 1;
}

static int ak6_self_test(void)
{
    static const char message[] = "AK6: event and handle tests ok\r\n";
    static const char cancel_message[] = "AK6: wait cancellation tests ok\r\n";
    if (!event_self_test()) {
        return 0;
    }
    serial_write(message, sizeof(message) - 1u);
    if (!event_cancel_self_test()) {
        return 0;
    }
    serial_write(cancel_message, sizeof(cancel_message) - 1u);
    return 1;
}



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

__attribute__((noreturn)) void _start(void);
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
    static const char user_arch_error[] = "AK5: user architecture setup failed\r\n";
    if (!user_arch_init()) {
        serial_write(user_arch_error, sizeof(user_arch_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    static const char interrupt_error[] = "AK3: interrupt setup failed\r\n";
    if (!interrupts_init()) {
        serial_write(interrupt_error, sizeof(interrupt_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    timer_init();
    static const char thread_error[] = "AK4: thread setup or test failed\r\n";
    static const char thread_message[] = "AK4: thread tests ok\r\n";
    if (!thread_init()) {
        serial_write(thread_error, sizeof(thread_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    __asm__ volatile ("sti" : : : "memory");
    if (!thread_self_test()) {
        serial_write(thread_error, sizeof(thread_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    serial_write(thread_message, sizeof(thread_message) - 1u);
    static const char ak5_error[] = "AK5: tests failed\r\n";
    if (!ak5_self_test()) {
        serial_write(ak5_error, sizeof(ak5_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    static const char ak6_error[] = "AK6: event tests failed\r\n";
    if (!ak6_self_test()) {
        serial_write(ak6_error, sizeof(ak6_error) - 1u);
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    while (timer_ticks() < 3u) {
        __asm__ volatile ("hlt");
    }
    serial_write(timer_message, sizeof(timer_message) - 1u);
    trigger_divide_by_zero();
}
