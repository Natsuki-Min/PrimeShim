ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x30600000, LENGTH = 256M
}

SECTIONS {
    /* --- Fixed custom sections first --- */
    .ER_RO 0x30600000 : {
        *(.ER_RO_text .ER_RO.*)
    } > RAM

    .ER_RW 0x30d37574 : {
        *(.ER_RW_data .ER_RW.*)
    } > RAM

    .empty 0x30d3e144 (NOLOAD) : {
        . += 0x606c;
    } > RAM

    /* --- Set location counter to end of last fixed section --- */
    __end_fixed = 0x30d3e144 + 0x606c;  /* 0x30cb6190 */
    . = __end_fixed;

    /* --- Standard sections AFTER fixed sections --- */
    .text : {
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    .data : {
        *(.data .data.*)
    } > RAM

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > RAM

    /* --- Dynamic sections --- */
    .plt : { *(.plt) } > RAM
    .got : { *(.got.plt) *(.got) } > RAM
    .dynamic : { *(.dynamic) } > RAM
    .dynsym : { *(.dynsym) } > RAM
    .dynstr : { *(.dynstr) } > RAM
    .rel.dyn : { *(.rel.dyn) } > RAM

    /* --- Heap/stack boundary --- */
    . = ALIGN(8);
    _end = .;
    PROVIDE(end = .);
}
