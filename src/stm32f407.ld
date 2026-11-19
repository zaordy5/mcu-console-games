ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : { . = ALIGN(4); _data = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    _idata = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(4); _bss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
}
