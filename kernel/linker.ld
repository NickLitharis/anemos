OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x200000;

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = KERNEL_VMA;

    .text : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA) ALIGN(0x1000)
    {
        *(.text .text.*)
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA) ALIGN(0x1000)
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
        *(.rodata .rodata.*)
    } :rodata

    .data : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA) ALIGN(0x1000)
    {
        *(.data .data.*)
    } :data

    .bss : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA) ALIGN(0x1000)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
        *(.comment)
    }
}
