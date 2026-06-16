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
	# NINDY interrupt-handler prologue: reserve a 64-byte scratch frame ABOVE sp
	# FIRST, then save g0..g14 at negative offsets. The last is `stt` (TRIPLE:
	# g12/g13/g14) — NEVER a `stq` (which would also save g15 = fp). Hand-rolling sp
	# and saving fp (the old code) corrupts the i960 return frame and the interrupt
	# return jumps into the stack (m2emu: invalid opcode at _nindy_stack+0x48).
	ldconst 64, r4
	addo    sp, r4, sp
	stq     g0,  -64(sp)
	stq     g4,  -48(sp)
	stq     g8,  -32(sp)
	stt     g12, -16(sp)			# g12,g13,g14 only (leave g15/fp alone)

	mov     0,g14

	ld		_frameVBL,g0
	addi    1,g0,g0
	st		g0,_frameVBL			# frameVBL++

	ld		_RAMBASE_START,g0
	addi    1,g0,g0
	st		g0,_RAMBASE_START		# RAMBASE_START++ (STF VsyncScr; interrupt_wait spins on it)

	lda     0x00e80000,g0
	subo    2,0,g1					# g1 = 0xFFFFFFFE (clear vblank bit0; STF VsyncScr + m2_vsync)
	st      g1,(g0)					# ack vblank irq

	ldq     -64(sp), g0
	ldq     -48(sp), g4
	ldq     -32(sp), g8
	ldt     -16(sp), g12			# matching triple restore
	ret

# ---- STF interrupt-table ISRs (vectors 13/14/15 + 8-11/16+) -----------------
# Faithful to STF's _intr_table. VsyncObj/Timer/Other are dormant unless their
# board IRQ source is enabled; they save the g-regs they touch and ACK so an
# unexpected fire can't corrupt the foreground or storm. Internals that depend on
# STF game state (g0-based Timer math, send_sound_code) are omitted - those
# subsystems don't exist in geotest.

	.globl	_vsync_obj
	.globl	_timer_irq
	.globl	_other_irq
	.globl	_intr_halt

# VsyncObj (STF vector 13 @0xD10): just ACK (clear bit2).
_vsync_obj:
	lda     0x00e80000,r4
	subo    5,0,r5					# r5 = 0xFFFFFFFB
	st      r5,(r4)					# ack (STF VsyncObj)
	ret

# Timer (STF vector 14 @0xBEC): reset hardware timer TIMER_04 (0xF0000C) to max,
# flag timerFlag. STF's g0-scaled deadline math is frame-specific (g0 undefined in
# our context) so it is omitted; structure preserved.
_timer_irq:
	stq     g0,(sp)
	addo    16,sp,sp
	mov     0,g14
	lda     0x000fffff,g0
	lda     0x00f0000c,g1
	st      g0,(g1)					# TIMER_04 = 0xFFFFF
	lda     1,g0
	st      g0,_timerFlag			# byte_50008C = 1
	subo    16,sp,sp
	ldq     (sp),g0
	ret

# Other (STF vector 15 @0xDF0): STF checks IRQ bit10 -> send_sound_code (absent
# here). Just ACK defensively.
_other_irq:
	lda     0x00e80000,r4
	subi    1,0,r5					# r5 = 0xFFFFFFFF
	st      r5,(r4)					# ack
	ret

# IntrHalt (STF vectors 8-11, 16+ @0xE10): unexpected interrupt -> STF prints
# "Interrupt Halt #" then halts. We just halt (spin) so an unexpected vector is
# observable (frozen frame) rather than running off into garbage.
_intr_halt:
	b		_intr_halt

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


