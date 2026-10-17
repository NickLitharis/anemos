/* freestanding i386 program that talks to the QEMU debug console (port 0xE9) */
static void outb(unsigned short port, unsigned char v){
  __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static void puts_(const char *s){ while(*s) outb(0xE9,(unsigned char)*s++); }
void _start(void){ puts_("XP-MODERNIZATION-V0.1: harness binary executing under QEMU\n"); for(;;) __asm__ volatile("hlt"); }
