/* The COP VM image: buffer RAM words 0x4000..0x7D7F (byte address 0x10000000 + word * 4). Word
   0x4000 is the mark rv2cvm.py puts before the code (the kernels check it); _start is 0x4001. */
MEMORY { RAM (rwx) : ORIGIN = 0x10010004, LENGTH = 0xF5FC }
ENTRY(_start)
SECTIONS {
    .text : { *(.text.start) *(.text .text.*) } > RAM
    _text_end = .;
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > RAM
    .data : { *(.data .data.* .sdata .sdata.*) } > RAM
    _image_end = .;
    .pool (NOLOAD) : { . = ALIGN(4); _pool = .; . += 0x600; _pool_end = .; } > RAM
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* COMMON) } > RAM
    _bss_end = .;
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
