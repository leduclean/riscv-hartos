    #include <arch/abi.h>
    #include <arch/asm_defs.h>
    .equ STACK_SIZE, 4096
    .section .text.init,"ax",@progbits
    .globl entry

entry:
    csrr s1, mhartid
    addi t1, s1, 1
    li t2, STACK_SIZE
    mul t1, t1, t2
    la t2, boot_stack_start

    /* Sp points to the top of the allocated .bss stack memory. */
    add sp, t1, t2

    /* Only hart id 0 will boot */
    li t1, BOOT_HART_ID
    bne s1, t1, wait_boot
hart0_boot:
    # RAZ of bss.
    la t0, _bss_start
    la t1, _bss_end
1:  sd zero, 0(t0)
    addi t0, t0, 8
    bltu t0, t1, 1b
    j start
wait_boot:
    la      t1, boot_rdy
1:  lbu     t2, 0(t1)
    beqz    t2, 1b
    fence   r, rw
start:
    mv a0, s1
    call init_cpu

    mv a0, s1
    j hart_init 
    .bss
    .balign STACK_ALIGN
boot_stack_start:
    .skip NCPU * STACK_SIZE
boot_stack_top:
