/* Debian95 user executable: static ET_EXEC, loaded at 4 MiB by the ELF loader. */
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }

    /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) }
}
