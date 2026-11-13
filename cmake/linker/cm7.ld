ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
__stack_limit = _estack - 8192;
cm4_parked = 0x38000000;
SECTIONS {
  .isr_vector : { . = ALIGN(1024); KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .gcov_info : { __gcov_info_start = .; KEEP(*(.gcov_info)) __gcov_info_end = .; } > FLASH
  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  .heap_forbidden (NOLOAD) : { end = .; _end = .; } > RAM
  /DISCARD/ : { *(.note.GNU-stack) }
  ASSERT(_ebss <= __stack_limit, "CM7 RAM overlaps reserved 8 KiB stack")
  ASSERT(SIZEOF(.isr_vector) >= 64, "CM7 vector table missing")
}
