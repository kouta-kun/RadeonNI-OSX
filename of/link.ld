OUTPUT_FORMAT("elf32-powerpc")
ENTRY(_start)
SECTIONS
{
	. = 0x01000000;
	.text : { *(.text.start) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.* .sdata2 .sbss2) }
	.data : { *(.data .data.* .sdata .got2 .got) }
	. = ALIGN(16);
	__bss_start = .;
	.bss : { *(.sbss) *(.bss .bss.* COMMON) . = ALIGN(16); . += 0x20000; _stack_top = .; }
	__bss_end = .;
}
