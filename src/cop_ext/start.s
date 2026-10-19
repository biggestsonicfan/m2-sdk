        .section .text.start, "ax"
        .globl _start
_start: call vm_main
        .insn r 0x0b, 0, 2, x0, x0, x0      # HALT: back to the kernel
