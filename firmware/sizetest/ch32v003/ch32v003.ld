/* CH32V003F4P6: 16 KB flash (last 64 B page reserved for Pine's bond), 2 KB SRAM */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 16K - 64
  BOND  (r)   : ORIGIN = 0x00003FC0, LENGTH = 64
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 2K
}
ENTRY(reset_handler)
STACK_RESERVE = 384;

SECTIONS
{
  .init   : { KEEP(*(.init)) } > FLASH
  .vector : { . = ALIGN(4); KEEP(*(.vector)) } > FLASH
  .text   : { . = ALIGN(4); *(.text .text.*) *(.rodata .rodata.* .srodata .srodata.*) . = ALIGN(4); } > FLASH
  .data   : { _data_start = .; *(.data .data.* .sdata .sdata.*) . = ALIGN(4); _data_end = .; } > RAM AT > FLASH
  _data_load = LOADADDR(.data);
  .bss (NOLOAD) : { _bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); _bss_end = .; } > RAM
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  ASSERT(_bss_end + STACK_RESERVE <= _stack_top, "RAM overflow: static data + stack reserve > 2 KB")
  /DISCARD/ : { *(.comment) *(.riscv.attributes) }
}
