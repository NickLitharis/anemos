/* multiboot v1 header so QEMU -kernel loads us */
__attribute__((section(".multiboot"))) __attribute__((used))
static const unsigned int mb_header[3] = { 0x1BADB002, 0x0, 0xE4524FFE };
