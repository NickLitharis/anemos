#include "interrupts.h"

#include "memory.h"
#include "paging.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

typedef void (*interrupt_stub_t)(void);

#define DECLARE_ISR(n) extern void isr_##n(void)
DECLARE_ISR(0);  DECLARE_ISR(1);  DECLARE_ISR(2);  DECLARE_ISR(3);
DECLARE_ISR(4);  DECLARE_ISR(5);  DECLARE_ISR(6);  DECLARE_ISR(7);
DECLARE_ISR(8);  DECLARE_ISR(9);  DECLARE_ISR(10); DECLARE_ISR(11);
DECLARE_ISR(12); DECLARE_ISR(13); DECLARE_ISR(14); DECLARE_ISR(15);
DECLARE_ISR(16); DECLARE_ISR(17); DECLARE_ISR(18); DECLARE_ISR(19);
DECLARE_ISR(20); DECLARE_ISR(21); DECLARE_ISR(22); DECLARE_ISR(23);
DECLARE_ISR(24); DECLARE_ISR(25); DECLARE_ISR(26); DECLARE_ISR(27);
DECLARE_ISR(28); DECLARE_ISR(29); DECLARE_ISR(30); DECLARE_ISR(31);
DECLARE_ISR(32);
#undef DECLARE_ISR

static struct idt_entry idt[256] __attribute__((aligned(16)));
static volatile uint64_t tick_count;

static interrupt_stub_t const stubs[33] = {
    isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
    isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    isr_32
};

static void read_msr(uint32_t msr, uint32_t *low, uint32_t *high)
{
    __asm__ volatile ("rdmsr" : "=a"(*low), "=d"(*high) : "c"(msr));
}

static void write_msr(uint32_t msr, uint32_t low, uint32_t high)
{
    __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

static int enable_legacy_pic_delivery(void)
{
    uint32_t apic_low;
    uint32_t apic_high;
    read_msr(0x1Bu, &apic_low, &apic_high);
    if ((apic_low & (1u << 11u)) == 0u) {
        return 0;
    }

    uint64_t lvt0;
    if ((apic_low & (1u << 10u)) != 0u) {
        uint32_t lvt_low;
        uint32_t lvt_high;
        read_msr(0x835u, &lvt_low, &lvt_high);
        lvt0 = ((uint64_t)lvt_high << 32u) | lvt_low;
        lvt0 = (lvt0 & ~(7ull << 8u)) | (7ull << 8u);
        lvt0 &= ~(1ull << 16u);
        write_msr(0x835u, (uint32_t)lvt0, (uint32_t)(lvt0 >> 32u));
        return 1;
    }

    const uint64_t apic_address = ((uint64_t)apic_high << 32u) |
                                  ((uint64_t)apic_low & 0x000ffffffffff000ull);
    volatile uint32_t *lvt0_register;
    if (physical_address_is_hhdm_mapped(apic_address + 0x350u)) {
        lvt0_register =
            (volatile uint32_t *)physical_to_virtual(apic_address + 0x350u);
    } else {
        const uint64_t mmio_virtual = paging_region_base() + 0x200000u;
        if (!paging_map_mmio_page(mmio_virtual, apic_address)) {
            return 0;
        }
        lvt0_register = (volatile uint32_t *)(uintptr_t)(mmio_virtual + 0x350u);
    }
    if (lvt0_register == NULL) {
        return 0;
    }
    uint32_t value = *lvt0_register;
    value = (value & ~(7u << 8u)) | (7u << 8u);
    value &= ~(1u << 16u);
    *lvt0_register = value;
    (void)*lvt0_register;
    return 1;
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t current_code_selector(void)
{
    uint16_t selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}

static void set_gate(uint8_t vector, uintptr_t address, uint16_t selector)
{
    idt[vector].offset_low = (uint16_t)(address & 0xFFFFu);
    idt[vector].selector = selector;
    idt[vector].ist = 0u;
    idt[vector].attributes = 0x8Eu;
    idt[vector].offset_middle = (uint16_t)((address >> 16u) & 0xFFFFu);
    idt[vector].offset_high = (uint32_t)(address >> 32u);
    idt[vector].reserved = 0u;
}

static void pic_remap(void)
{
    outb(0x21u, 0xFFu);
    outb(0xA1u, 0xFFu);

    outb(0x20u, 0x11u);
    outb(0xA0u, 0x11u);
    outb(0x21u, 0x20u);
    outb(0xA1u, 0x28u);
    outb(0x21u, 0x04u);
    outb(0xA1u, 0x02u);
    outb(0x21u, 0x01u);
    outb(0xA1u, 0x01u);

    outb(0x21u, 0xFFu);
    outb(0xA1u, 0xFFu);
}

int interrupts_init(void)
{
    __asm__ volatile ("cli");
    for (size_t i = 0u; i < 33u; ++i) {
        set_gate((uint8_t)i, (uintptr_t)stubs[i], current_code_selector());
    }

    const struct idtr descriptor = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uintptr_t)idt
    };
    __asm__ volatile ("lidt %0" : : "m"(descriptor));
    pic_remap();
    return enable_legacy_pic_delivery();
}

void timer_init(void)
{
    const uint16_t divisor = 11932u;
    outb(0x43u, 0x36u);
    outb(0x40u, (uint8_t)(divisor & 0xFFu));
    outb(0x40u, (uint8_t)(divisor >> 8u));
    outb(0x21u, 0xFEu);
    outb(0xA1u, 0xFFu);
}

uint64_t timer_ticks(void)
{
    return tick_count;
}

static __attribute__((noreturn)) void panic_exception(const struct interrupt_frame *frame)
{
    static const char prefix[] = "PANIC: exception vector=";
    static const char error_text[] = " error=";
    static const char rip_text[] = " rip=";
    static const char newline[] = "\r\n";

    __asm__ volatile ("cli");
    serial_write(prefix, sizeof(prefix) - 1u);
    serial_write_u64(frame->vector);
    serial_write(error_text, sizeof(error_text) - 1u);
    serial_write_u64(frame->error);
    serial_write(rip_text, sizeof(rip_text) - 1u);
    serial_write_u64(frame->rip);
    serial_write(newline, sizeof(newline) - 1u);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void interrupt_dispatch(void *raw_frame)
{
    const struct interrupt_frame *frame = raw_frame;
    if (frame->vector == 32u) {
        outb(0x20u, 0x20u);
        ++tick_count;
        return;
    }
    panic_exception(frame);
}
