/* PY32F003F18P6: 64 KB flash (last 1 KB = 8 pages for Pine's bond), 8 KB SRAM */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K - 1K
  BOND  (r)   : ORIGIN = 0x0800FC00, LENGTH = 1K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
}
ENTRY(Reset_Handler)
STACK_RESERVE = 1024;

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text   : { . = ALIGN(4); *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .data   : { _data_start = .; *(.ramfunc .ramfunc.*) *(.data .data.*) . = ALIGN(4); _data_end = .; } > RAM AT > FLASH
  _data_load = LOADADDR(.data);
  .bss (NOLOAD) : { _bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > RAM
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  ASSERT(_bss_end + STACK_RESERVE <= _stack_top, "RAM overflow: static data + stack reserve > 8 KB")
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
