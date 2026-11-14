#ifndef ANEMOS_KERNEL_SERIAL_H
#define ANEMOS_KERNEL_SERIAL_H

#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_write(const char *text, size_t length);
void serial_write_u64(uint64_t value);

#endif
