/******************************************************************/
/* 		Copyright (c) 1989, Intel Corporation

   Intel hereby grants you permission to copy, modify, and 
   distribute this software and its documentation.  Intel grants
   this permission provided that the above copyright notice 
   appears in all copies and that both the copyright notice and
   this permission notice appear in supporting documentation.  In
   addition, Intel grants this permission provided that you
   prominently mark as not part of the original any modifications
   made to this software or documentation, and that the name of 
   Intel Corporation not be used in advertising or publicity 
   pertaining to distribution of the software or the documentation 
   without specific, written prior permission.  

   Intel Corporation does not warrant, guarantee or make any 
   representations regarding the use of, or the results of the use
   of, the software and documentation in terms of correctness, 
   accuracy, reliability, currentness, or otherwise; and you rely
   on the software, documentation and results solely at your own 
   risk.							  */
/******************************************************************/
	.globl	_user_intr_empty
	.globl	_user_NMI

	.globl  _irq_vblank
	.globl  _irq_serial


_user_intr_empty:
	ret

_user_NMI:
	ret


_irq_vblank:
	stq     g0,(sp)
	addo    16,sp,sp
	stq     g4,(sp)
	addo    16,sp,sp
	stq     g8,(sp)
	addo    16,sp,sp
	stq     g12,(sp)
	addo    16,sp,sp				# push to stack

	mov     0,g14

	ld		_frameVBL,g0
	addi    1,g0,g0
	st		g0,_frameVBL			# increase frame counter

#	ld		0x01000000,g0			# increase TILE at top left
#	addi    1,g0,g0
#	st		g0,0x01000000

	lda     0x00e80000,g0
	lda     0x0001,g1
	st      g1,(g0)					# clear irq

	subo    16,sp,sp
	ldq     (sp),g12
	subo    16,sp,sp
	ldq     (sp),g8
	subo    16,sp,sp
	ldq     (sp),g4
	subo    16,sp,sp
	ldq     (sp),g0					# pop from stack

	ret

_irq_serial:

	ret

	stq     g0,(sp)
	addo    16,sp,sp
	stq     g4,(sp)
	addo    16,sp,sp
	stq     g8,(sp)
	addo    16,sp,sp
	stq     g12,(sp)
	addo    16,sp,sp				# push to stack

	mov     0,g14

	call	_handleSerialIRQ

	subo    16,sp,sp
	ldq     (sp),g12
	subo    16,sp,sp
	ldq     (sp),g8
	subo    16,sp,sp
	ldq     (sp),g4
	subo    16,sp,sp
	ldq     (sp),g0					# pop from stack

	lda     0x00e80000,r4
	lda     0x0400,r5
	st      r5,(r4)					# clear irq
	ret


