/* Entry of the Open Firmware client: r5 is the client interface. */
	.globl _start
	.section .text.start,"ax"
_start:
	lis	%r1,_stack_top@h
	ori	%r1,%r1,_stack_top@l
	li	%r0,0
	stwu	%r0,-16(%r1)
	/* clear .bss, then keep the client interface (r5) */
	lis	%r3,__bss_start@h
	ori	%r3,%r3,__bss_start@l
	lis	%r4,__bss_end@h
	ori	%r4,%r4,__bss_end@l
	li	%r0,0
1:	cmplw	%r3,%r4
	bge	2f
	stw	%r0,0(%r3)
	addi	%r3,%r3,4
	b	1b
2:	lis	%r9,of_entry@ha
	stw	%r5,of_entry@l(%r9)
	bl	of_main
	/* of_main returned: back to Open Firmware */
	li	%r3,0
	blr
