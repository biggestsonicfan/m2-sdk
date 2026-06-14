/* GNU ld (i960-elf) script — ELF port of lib/testlinkrom.ld (gld960/COFF).
 *
 * Layout matches the i960 boot the kx_init.s startup expects:
 *   - ROM at 0x000000 (1 MB). The Core Initialization Block (system address
 *     table) must sit at offset 0, so kx_init.o(.text) is placed first.
 *   - RAM at 0x500000 (512 KB). Initialized .data lives in RAM but is stored
 *     (LMA) right after .text in ROM; _move_data_area copies _etext..-> _ram.
 *   - const/rodata stays in ROM (textures + cpres firmware are large).
 *   - cs1 is the boot checksum word, computed at link time.
 */
OUTPUT_ARCH(i960:kb)
ENTRY(system_address_table)

MEMORY
{
    rom (rwx) : ORIGIN = 0x00000000, LENGTH = 0x100000
    ram (rwx) : ORIGIN = 0x00500000, LENGTH = 0x080000
}

SECTIONS
{
    .text :
    {
        *kx_init.o(.text)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.gcc_except_table*)
        . = ALIGN(16);
        _etext = .;
    } > rom

    .data : AT (_etext)
    {
        _ram = .;
        *(.data)
        *(.data.*)
        . = ALIGN(16);
        _edata = .;
    } > ram

    .bss (NOLOAD) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(16);
        _end = .;
    } > ram

    cs1 = -(system_address_table + prcb_ptr + start_ip);

    _heap_size = 0x20000;
    _heap_base = (_end + 0xf) & ~0xf;
    _heap_end  = _heap_base + _heap_size - 1;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
