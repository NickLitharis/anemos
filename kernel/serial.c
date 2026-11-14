#include "serial.h"

#define COM1 0x3F8u

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    outb(COM1 + 1u, 0x00u);
    outb(COM1 + 3u, 0x80u);
    outb(COM1 + 0u, 0x01u);
    outb(COM1 + 1u, 0x00u);
    outb(COM1 + 3u, 0x03u);
    outb(COM1 + 2u, 0xC7u);
    outb(COM1 + 4u, 0x0Bu);
}

void serial_write(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        while ((inb(COM1 + 5u) & 0x20u) == 0u) {
        }
        outb(COM1, (uint8_t)text[i]);
    }
}

void serial_write_u64(uint64_t value)
{
    char digits[20];
    size_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    while (count != 0u) {
        --count;
        serial_write(&digits[count], 1u);
    }
}
