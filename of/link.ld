OUTPUT_FORMAT("elf32-powerpc")
ENTRY(_start)
SECTIONS
{
	. = 0x01000000;
	.text : { *(.text.start) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.* .sdata2 .sbss2) }
	/* .bss is part of the file image: only memory Open Firmware loaded */
	.data : { *(.data .data.* .sdata .got2 .got)
		. = ALIGN(16);
		__bss_start = .;
		*(.sbss) *(.bss .bss.* COMMON)
		. = ALIGN(16);
		__bss_end = .;
		. += 0x10000; _stack_top = .; }
}
