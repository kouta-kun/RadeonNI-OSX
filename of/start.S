/* Entry of the Open Firmware client: r5 is the client interface.
 * Returns to Open Firmware (LR and SP are saved and restored). */
	.globl _start
	.section .text.start,"ax"
_start:
	mflr	%r0
	mr	%r12,%r1
	lis	%r1,_stack_top@h
	ori	%r1,%r1,_stack_top@l
	stwu	%r1,-32(%r1)
	stw	%r0,28(%r1)		/* caller's LR */
	stw	%r12,24(%r1)		/* caller's SP */
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
	lwz	%r0,28(%r1)
	lwz	%r1,24(%r1)
	mtlr	%r0
	li	%r3,0
	blr
