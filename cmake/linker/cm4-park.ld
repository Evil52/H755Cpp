ENTRY(Park_Reset_Handler)
MEMORY { FLASH (rx) : ORIGIN = 0x08100000, LENGTH = 128K }
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) } > FLASH
  /DISCARD/ : { *(.note.GNU-stack) }
  ASSERT(SIZEOF(.isr_vector) == 64, "CM4 core vectors missing")
}
