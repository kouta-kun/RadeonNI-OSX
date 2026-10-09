	.globl _mstart
	.section .text.start,"ax"
_mstart:
	lis	%r5,mockf@h
	ori	%r5,%r5,mockf@l
	bl	_start
	li	%r0,1
	li	%r3,0
	sc
