.text

.global user_arch_load_gdt
.type user_arch_load_gdt, @function
user_arch_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lreload_cs(%rip), %rax
    pushq %rax
    lretq
.Lreload_cs:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x28, %ax
    ltr %ax
    retq
.size user_arch_load_gdt, .-user_arch_load_gdt

.global user_arch_enter
.type user_arch_enter, @function
user_arch_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, user_arch_resume_stack(%rip)
    pushfq
    popq %rax
    movq %rax, user_arch_resume_flags(%rip)
    movq %rdi, %rax
    pushq $0x1b
    pushq %rsi
    pushq $0x2
    pushq $0x23
    pushq %rax
    iretq
.size user_arch_enter, .-user_arch_enter

.global user_arch_resume
.type user_arch_resume, @function
user_arch_resume:
    movq user_arch_resume_stack(%rip), %rsp
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq user_arch_resume_flags(%rip), %rax
    pushq %rax
    popfq
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
.size user_arch_resume, .-user_arch_resume

.section .text.user_payload,"ax",@progbits
.balign 16
.global user_payload_start
.global user_payload_end
.global user_probe_success
.global user_probe_kernel_read
    .global user_probe_write_code
    .global user_probe_execute_stack
.global user_probe_privileged
    .global user_probe_buffer_valid
    .global user_probe_buffer_null
    .global user_probe_buffer_unmapped
.global user_probe_buffer_overflow
    .global user_probe_buffer_cross_page
    .global user_probe_buffer_length_zero
    .global user_probe_buffer_length_large
user_payload_start:
user_probe_success:
    movq $1, %rax
    movq $40, %rdi
    pushq %rdi
    popq %rdi
    int $0x80
    cmpq $47, %rax
    jne .Luser_failed
    movq $2, %rax
    xorq %rdi, %rdi
    int $0x80
.Luser_failed:
    movq $2, %rax
    movq $1, %rdi
    int $0x80
    jmp .Luser_failed
user_probe_kernel_read:
    movabsq $0xffffffff80000000, %rax
    movq (%rax), %rax
    movq $2, %rax
    movq $1, %rdi
    int $0x80
    jmp .Luser_failed
user_probe_write_code:
    movabsq $0x00400000, %rax
    movb $0, (%rax)
    jmp .Luser_failed
user_probe_execute_stack:
    movabsq $0x00401000, %rax
    jmp *%rax
user_probe_privileged:
    cli
    movq $2, %rax
    movq $1, %rdi
    int $0x80
    jmp .Luser_failed
user_probe_buffer_valid:
    subq $16, %rsp
    movb $1, 0(%rsp)
    movb $2, 1(%rsp)
    movb $3, 2(%rsp)
    movb $4, 3(%rsp)
    movq $3, %rax
    movq %rsp, %rdi
    movq $4, %rsi
    int $0x80
    cmpq $10, %rax
    jne .Luser_failed
    movq $2, %rax
    xorq %rdi, %rdi
    int $0x80
    jmp .Luser_failed
user_probe_buffer_null:
    movq $3, %rax
    xorq %rdi, %rdi
    movq $1, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
    jmp .Luser_exit_ok
user_probe_buffer_unmapped:
    movq $3, %rax
    movq $0x00600000, %rdi
    movq $1, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
    jmp .Luser_exit_ok
user_probe_buffer_cross_page:
    movq $3, %rax
    movabsq $0x00401fff, %rdi
    movq $2, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
    jmp .Luser_exit_ok
user_probe_buffer_overflow:
    movq $3, %rax
    movabsq $0xfffffffffffffff0, %rdi
    movq $32, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
user_probe_buffer_length:
    jmp .Luser_exit_ok
.Luser_exit_ok:
    movq $2, %rax
    xorq %rdi, %rdi
    int $0x80
    jmp .Luser_failed
user_probe_buffer_length_zero:
    movq $3, %rax
    movq %rsp, %rdi
    xorq %rsi, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
    jmp .Luser_exit_ok
user_probe_buffer_length_large:
    movq $3, %rax
    movq %rsp, %rdi
    movq $33, %rsi
    int $0x80
    cmpq $-1, %rax
    jne .Luser_failed
    jmp .Luser_exit_ok
user_payload_end:

.section .note.GNU-stack,"",@progbits
