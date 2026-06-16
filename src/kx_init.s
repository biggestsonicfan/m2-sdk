# ******************************************************************
# 		Copyright (c) 1989, Intel Corporation
#
#   Intel hereby grants you permission to copy, modify, and 
#   distribute this software and its documentation.  Intel grants
#   this permission provided that the above copyright notice 
#   appears in all copies and that both the copyright notice and
#   this permission notice appear in supporting documentation.  In
#   addition, Intel grants this permission provided that you
#   prominently mark as not part of the original any modifications
#   made to this software or documentation, and that the name of 
#   Intel Corporation not be used in advertising or publicity 
#   pertaining to distribution of the software or the documentation 
#   without specific, written prior permission.  
#
#   Intel Corporation does not warrant, guarantee or make any 
#   representations regarding the use of, or the results of the use
#   of, the software and documentation in terms of correctness, 
#   accuracy, reliability, currentness, or otherwise; and you rely
#   on the software, documentation and results solely at your own 
#   risk.
# ******************************************************************

# ******************************************************************
#	Below is system initialization code and tables.
#	The code builds the PRCB in memory, sets up the stack frame,
#	the interrupt, fault, and system procedure tables, and
# 	then vectors to a user defined routine.
# ******************************************************************
	
		# declare the below symbols public
		.globl		system_address_table
		.globl		prcb_ptr
#		.globl		_prcb_ram
		.globl		start_ip

		# my functions here	
		.globl		_move_data_area
		.globl		_waitCycles
		.globl		_setCoproTextureData1
		.globl		_flip_geo_list
		.globl		_process_geo_list
		.globl		_copro_upload_code
#		.globl		_clear_geo_buffer
		.globl		_send_data_copro_unknown
		.globl		_wait_for_copro
		.globl		_geo_push_log_data
		.globl		_build_colour_translation
		.globl		_switch_proc_pri_low

		.globl		cs1
	
		.globl		_reinit_iac				# "reset" command
		.globl		_nindy_stack			# used for nindy commands
		.globl		_intr_stack				# used for interrupts

# ------ define IAC address
		.set		local_IAC, 0xff000010

# ------ core initialization block (located at address 0)
# ------ (8 words)

		.text
start:

system_address_table:
		.word		system_address_table	# 0 - SAT pointer
		.word		prcb_ptr				# 4 - PRCB pointer
		.word		0
		.word		start_ip				# 12 - Pointer to first IP
		.word		cs1						# 16 - calculated at link time
		.word		0						# 20 - cs1= -(SAT + PRCB + startIP)
		.word		0
		.word		-1
	
		# NINDY config information
		.word		0
		.word		0
		.word		1
		.word		0
	
		.space		72

		.word		sys_proc_table			# 120 - initialization words
		.word		0x304000fb

		.space		8

		.word		system_address_table	# 136 -
		.word		0x00fc00fb				# 140 - initialization words
	
		.space		8

		.word		sys_proc_table			# 152 - initialization words
		.word		0xfc00a3				# STF value (was 0x304000fb) — supervisor
										# proc-table descriptor, dormant for us
	
		.space		8

		.word		fault_proc_table		# 168 - initialization words
		.word		0xfc00a3				# STF value (was 0x304000fb)
	

# ------ initial PRCB
# ------ This is our startup PRCB.  After initialization, this will be copied to RAM
		.align		6
prcb_ptr:
		.word		0x0         			#   0 - reserved
		.word		0xc						#   4 - initialize to 0x0c 
		.word		0x0         			#   8 - reserved
		.word		0x0 	  				#  12 - reserved 
		.word		0x0	  					#  16 - reserved 
		.word		_intr_table				#  20 - interrupt table address
		.word		_intr_stack				#  24 - interrupt stack pointer
		.word		0x0						#  28 - reserved
		.word		0x000001ff				#  32 - pointer to offset zero
		.word		0x0000027f				#  36 - system procedure table pointer
		.word		fault_table				#  40 - fault table
		.word		0x0						#  44 - reserved
		.space		12						#  48 - reserved
		.word		0x0						#  60 - reserved
		.space		8						#  64 - reserved
		.word		0x0						#  72 - reserved
		.word		0x0						#  76 - reserved
		.space		48						#  80 - scratch space (resumption)
		.space		44						# 128 - scratch space ( error)


# The system procedure table will only be used if the user makes a supervisor procedure call
		.align		6

sys_proc_table:
		.word		0						# Reserved  
		.word		0						# Reserved
		.word		0						# Reserved
		.word		(_trap_stack + 0x01)	# Supervisor stack pointer      
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# (_console_io + 0x2)	# 0 - console I/O routines 
		.word		0						# (_file_io + 0x2)	# 1 - remote host service request
		.word		0						# (_lpt_io + 0x2)		# 2 - laser printer I/O routines
		.word		0						# 3 - reserved for CX compatibility
		.word		0 						# 4 - reserved for CX compatibility
		.word		0						# 5 - reserved for CX compatibility


# Below is the fault table for calls to the fault handler.
# This table is provided because the above table (supervisor table) will allow tracing of fault events,
# whereas this table will not allow tracing of fault events

		.align		6
fault_proc_table:
		.word		0						# Reserved
		.word		0						# Reserved
		.word		0						# Reserved
		.word		_trap_stack 			# Supervisor stack pointer      
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# Preserved
		.word		0						# (_switch_stack_on_fault + 0x2)	# Fault Handler 
		.word		0						# (_switch_stack_on_fault + 0x2)	# Trace Handler 






# --
# -- Processor starts execution at this spot after reset.
# --
start_ip:
		lda     0xe00000,g0
		lda     _cpu_ctrl_wait_data,g1
		subo    1,0,g3

loop_copy_cpu_ctrl_wait_data:
		ld      (g1),g2
		cmpo    g2,g3
		be      clear_work_ram
		st      g2,(g0)
		addo    4,g0,g0
		addo    4,g1,g1
		b       loop_copy_cpu_ctrl_wait_data

# --
# --  Blanket-zero work RAM the way Sonic the Fighters does in start_ip, BEFORE the
# --  copros are booted and BEFORE the intr-table/PRCB are moved into 0x5FF000+.
# --  STF clears 0x500000..0x59CFE0 then 0x59D000..0x600000, deliberately preserving
# --  the 32-byte gap at 0x59CFE0 (a boot scratch region). The GEO/object_data path
# --  reads scratch all over this region, so leaving it as garbage is a likely reason
# --  object_data renders on STF but not on us. (Cold-boot path only; the "rs" re-entry
# --  at _reinit_iac is below this, so a soft reset won't re-clear the RAM-based PRCB.)
clear_work_ram:
		lda     0x500000,r14				# RAMBASE_START
		mov     0,r15
		lda     160760,r13					# -> 0x59CFE0
clear_work_ram_lo:
		st      r15,(r14)
		lda     4(r14),r14
		cmpdeco 1,r13,r13
		bl      clear_work_ram_lo
		lda     0x59D000,r14				# skip the 32-byte gap at 0x59CFE0
		mov     0,r15
		lda     101376,r13					# -> 0x600000
clear_work_ram_hi:
		st      r15,(r14)
		lda     4(r14),r14
		cmpdeco 1,r13,r13
		bl      clear_work_ram_hi

copy_rom_to_main_ram:
		shlo    17,1,g0						# g0 = 0x20000
		lda     0x0,g4						# g4 = 0x0
		lda     0x0,g1						# g1 = 0x1
		lda     0x200000,g2					# g2 = 0x200000
		bal     move_data					# burstcopy(g0 longs from g1 to g2, offset g4)

_reinit_iac:								# re-entry point for the "rs" command
		mov     0x1f,r3						#
		st      r3,0x1800000				# write palette 0

		mov     3,r3						# poke 0x03 into 0xf80000
		st      r3,0xf80000					# prevent writes to main ram

# --
# --  copy the .data area into RAM, it has been packed in the EPROM after the code area so call a routine to move it
# --
		bal		_move_data_area
		mov		0, g14

# --
# --   copy the interrupt table to RAM
# --
		lda		0x400, g0					# load length of int. table
		lda		0x00, g4					# initialize offset to 0
		lda		_intr_table, g1				# load source 
#		lda		intr_ram, g2				# load address of new table
		lda		0x5ff000, g2				# load address of new table
		bal		move_data					# branch to move routine

# --
# --   copy PRCB to RAM space, located at _prcb_ram
# --
		lda		0xb0, g0					# load length of PRCB
		lda		0x00, g4					# initialize offset to 0
		lda		prcb_ptr, g1				# load source
#		lda		_prcb_ram, g2				# load destination
		lda		0x5ff400, g2				# load destination
		bal		move_data					# branch to move routine
# --
# --  fix up the PRCB to point to a new interrupt table
# --
#		lda		intr_ram, g12				# load address
		lda		0x5ff000, g12				# load address
		st		g12,0x14(g2)				# store into PRCB

# 
# --  At this point, the PRCB, and interrupt table have been moved to RAM.
# --  It is time to issue a REINITIALIZE IAC, which will start us anew with our RAM based PRCB.
# --  
# --  The IAC message, found in the 4 words located at the reinitialize_iac label, contain pointers to the current
# --  System Address Table, the new RAM based PRCB, and to the Instruction Pointer labeled start_again_ip
#
		lda		local_IAC, g5	
		lda		reinitialize_iac, g6	
		synmovq	g5, g6

end_code_loop1:
		bl		_irq_vblank
#		bl		end_code_loop1
# --
# --   Below is the software loop to move data
# --
move_data:	
		ldq		(g1)[g4*1], g8				# load 4 words into g8
		stq		g8, (g2)[g4*1]				# store to RAM block
		addi	g4,16, g4					# increment index	
		cmpibg	g0,g4, move_data			# loop until done
		bx		(g14)


fix_stack:
		flushreg
		or		pfp, 7, pfp					# put interrupt return
											# code into pfp

# 	we have reserved area on the stack before the call to this
#	routine.  We need to build a phony interrupt record here
#	to force the processor to pick it up on return.  Also, we
#	will take advantage of the fact that the processor will
#	restore the pc and ac to it's registers


		ldconst	0x1f0002, g0
		st		g0, -16(fp)					# store contrived pc
		ldconst	0x3b001000, g0				# set up arith. controls 
		st		g0, -12(fp)					# store contrived ac
		ret


# --  The processor will begin execution here after being
# --  reinitialized.  We will now set up the stacks and continue.
# --
start_again_ip:
#/*		call	_disable_ints				# disable board interrupts */
#
#	Before call to main, we need to take the processor out
#	of the "interrupted" state.  In order to do this, we will
# 	execute a call statement, then "fix up" the stack frame
#	to cause an interrupt return to be executed.
#
		ldconst	64, g0						# bump up stack to make
		addo	sp, g0, sp					# room for simulated
											# interrupt frame
		call	fix_stack					# exit the post-IAC interrupted state via a
											# simulated interrupt-return, so the armed vblank
											# IRQ is later taken from a clean frame (STF parity)

		# crtm2 step: switch to the application's OWN stack before entering main
		# (NINDY manual 5-14/5-16 + crtnin.s). The app MUST NOT run on the interrupt
		# stack — when the vblank IRQ fires the i960 switches to _intr_stack, so a main
		# running there gets its frame clobbered by the ISR (that was the _intr_stack+8
		# crash). _nindy_stack lives in the free low region, well clear of _intr_stack
		# (0x5FF500) and M2_EXIT_CTL (0x5F8000).
		lda		_nindy_stack, fp			# application stack base
		lda		-0x40(fp), pfp				# pfp (just in case; the app never returns)
		lda		0x40(fp), sp				# current stack ptr

		mov		0, g14						# g14 used by C compiler
											# for argument lists of
											# more than 12 arguments.
											# Initialize to 0.


# -- 	initialize floating point registers, if any
# --   SKIPPED for the Snake homebrew: it uses no floating point, and the
# --   cvtir/movre in _init_fp write FP regs the m2-hle2 i960 core mis-routes,
# --   corrupting the call frame so the following ret jumps wild.
#		callx	_init_fp

#		main code entry
call_main:

		lda     0xff000004,r4	
		lda     irq_control_word,r5	
		synmov  r4,r5

# disable interrupts and clear requests
		lda     0xe80000,r4					# r4 = 0xe80000
		mov     0,r5						# r5 = 0x0
		st      r5,(r4)						# write 0x0 to e80000 (irq request)

#		lda		0x0401,r5					# enable vblank & serial interrupts
		lda		0x0021,r5					# enable board IRQ bits 0 (vblank) + 5 (STF)
		st      r5,0x4(r4)					# write 0x21 to e80004 (irq enable)

		lda     0xff1f917f,r4
		lda     0x3f001000,r5				# 0x3f001000 (00 1 1 1 1 1 1 xxx 0 0 0 0 0 0 xx 1 xxx 0 x 0000 000)
		modac   r4,r5,r5

# --  Unmask interrupts: clear the process-priority field (bits 16-20) to 0 so the
# --  i960 accepts the vblank (vector 12, priority 1) interrupt. STF does this same
# --  modpc in start_again_ip before entering main; we previously left priority high
# --  (the vblank ISR never fired, so m2_vsync polled instead).
		shlo	0x10, 0x1f, r4				# r4 = 0x1f0000 (priority field mask)
		mov		0, r5						# r5 = 0 (new priority = 0)
		modpc	r4, r4, r5					# PC priority -> 0 (enable interrupts)

		mov		0, g14						# compiler expects g14 = 0
		callx	_main						# crtm2: enter the app via a real call frame on its
											# own stack (NINDY crtnin.s does `callx _main`); main
											# never returns.

end_code_loop2:
		bl		end_code_loop2

call_testrom:
#		call	0x40238
		mov     0,g0						# for padding

# switch process controls priority low, enable interrupts
_switch_proc_pri_low:
		shlo    16,31,g0                   # g0 = 1f0000
		mov     0,g1                       # g1 = 0
		modpc   g0,g0,g1                   # pc was 1f2002 before - 2002 after, g1 now 1f2002 | was 2002 before - 1f2002 after
		mov     g1,g0
		ret




# _init_fp removed: it was never called (the only callx was commented out) and its
# cvtir/movre are i960 hardware-FP opcodes, which m2emulator (no FPU emulation) treats
# as invalid. Dropping it keeps soft-float (M2_SOFTFLOAT) ROMs provably FP-free.


_move_data_area:

		lda		_ram, r4				# get start address of ram
		lda		_edata, r5				# get end address of data
		lda		_etext, r6				# get end address of code

move_loop:
		cmpibg	r4, r5, targ			# see if done
		ld		(r6), r7				# load data word from ROM
		addo	r6, 4, r6				# increment pointer
		st		r7, (r4)				# store data to memory
		addo	r4, 4, r4				# increment destination
		b		move_loop

targ:
		bx		(g14)





_waitCycles:
		st		r3,(sp)					# was stl
		addo	8,sp,sp
		mov		g0,r3
loopWaitC:
		cmpdeco	1,r3,r3
		bl		loopWaitC
		subo	8,sp,sp
		ld		(sp),r3					# was ldl
		ret

waitCyclesGEO:
		st		r3,(sp)					# was stl
		addo	8,sp,sp
		lda		0x80000,r3
loopWaitCg:
		cmpdeco	1,r3,r3
		bl		loopWaitCg
		subo	8,sp,sp
		ld		(sp),r3					# was ldl
		ret



_flip_geo_list:
		lda     0x00800000,g10
		lda     0x00880000,g11
		lda     0x00004000,g12

		ld		0x3008(g10),r4
		lda     0x0,r3
		cmpobne r3,r4,flipSkip

		lda     0x10000,r3

flipSkip:
		st      r3,0x501368					# geo list toggle int
		st      r3,0x1008(g10)
		ret

_process_geo_list:
		lda     0x00800000,g10
		lda     0x00880000,g11
		lda     0x00004000,g12

		lda     0x2525,g14
		st      g14,0x250(g11)				# (copro acc_get)			; 0x2525 to 0x880250 (0x12802525 to fifo)
		ld      (g11)[g12],r3				# read data from the copro and discard it!
		mov     0,g14
		ld      0x501368,r3					# geo list toggle int
		st      r3,0xf0(g10)
		st      r3,0x3008(g10)
		ret

# setup copro (texture data #1) start:00800000 count:00005000 
_setCoproTextureData1:
		lda      0x00800000,g10
		lda      0x00880000,g11
		lda      0x00004000,g12

		lda     0x028f0000,g1
		shlo    23,1,g2						# 0x800000
		mov     10,g4

setCoproTexLoop1:
		mov     0,r3
		shlo    11,1,g5						# 0x0800
		st		r3,0x40(g10)				# 0x02000000
		st      g2,(g10)[g12]
		st      g5,(g10)[g12]
		addo    g5,g2,g2
		shro    2,g5,g5

setCoproTexLoop2:
		ldos    0x0(g1),r3
		ldos    0x2(g1),r4
		ldos    0x4(g1),r5
		ldos    0x6(g1),r6
		st      r3,(g10)[g12]
		st      r4,(g10)[g12]
		st      r5,(g10)[g12]
		st      r6,(g10)[g12]
		addo    8,g1,g1
		cmpdeco 1,g5,g5
		bl      setCoproTexLoop2

		call    _process_geo_list			# process geo list
#;	call    000013e8						# wait even frame
		call	waitCyclesGEO
		cmpdeco 1,g4,g4
		bl      setCoproTexLoop1

		ret


_copro_upload_code:
		shlo    31,1,r3						# r3 = 0x80000000
		shlo    19,19,r4					# r4 = 0x00980000
		st      r3,(r4)

		shlo    19,17,r5					# r4 = 0x00880000
		lda     0x2800020,r6
		lda     0x2801fc0,r7

L0FC0:
		ld      (r6),r3
		st      r3,0x4000(r5)				# store to fifo
		addo    4,r5,r5
		addo    4,r6,r6
		cmpobl  r6,r7,L0FC0					# if r6 < r7 loop to 00000FC0

		mov     0,r3
		st      r3,(r4)
		ret



# mis-named???   _clear_geo_buffer:
_send_data_copro_unknown:
		lda     0x800000,r4
		lda     0x2801fc0,r5
		mov     0,r7

L1014:
		ldos    (r5)[r7*2],r3              # read2 r3 from r4 + r7*2
		st      r3,0x4(r4)                 # write r3 to r4 + 4
		shro    8,r3,r3
		st      r3,0x8(r4)
		addo    16,r4,r4
		lda     0x3f,r3
		cmpinco r3,r7,r7
		bg      L1014                   # while r3 > r7 loop to 00001014

		ret





_wait_for_copro:
#		shlo    19,17,g11					# g11 = 0x880000 (copro function port)
#		shlo    14,1,g12					# g12 = 0x4000
#		shlo    19,19,r4					# r4 = 0x00980000
#
#L0FE8:
#		ld      0x4(r4),r3					# read from 0x00980004
#		bbs     0,r3,L0FF8					# if bit0 set goto 00000FF8
#		ld      (g11)[g12],r3
#		b       L0FE8
#
#L0FF8:
#		st      r3,0x80(g11)				# (copro ?? 0x08 = clear_stack)
		ret


_geo_push_log_data:
		lda		0x00800000,g10
		lda		0x00880000,g11
		lda		0x00001000,g12

		lda     0x2802fa4,g1
		lda     0xb1,g2
		mov     0,g3

		ld      (g1),g4
		addo    4,g1,g1
		lda     0x5a,g5
		mov     16,g6
		mov     0,r6
		mov     4,r9

L15ec:
		shlo    2,r6,r7
		shlo    11,1,r8                    # 0x800
		addo    r8,r6,r6
		mov     0,r3
		st      r3,0x140(g10)              # r3 to 0x800140 (0x0a000000 to geo list)
		st      r7,(g10)[g12]              # push r7 to geo list
		st      r8,(g10)[g12]              # push r8 to geo list

L1608:
		call    L1628
		cmpdeco 1,r8,r8
		bl      L1608

#		call    000183f0                   # process geo list
#		call    000013e8                   # wait even frame

		call	_kickGEO
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL
		call	_waitVBL

		cmpdeco 1,r9,r9
		bl      L15ec
		ret


L1628:
		mov     0,r14
		mov     4,r11

L1630:
		cmpdeco 1,g5,g5
		bl      L1660

		and     3,g4,r13
		addo    r13,g2,g2
		mov     g2,g5
		addo    1,g3,g3
		shro    2,g4,g4
		cmpdeco 1,g6,g6
		bl      L1660

		mov     16,g6
		ld      (g1),g4
		addo    4,g1,g1

L1660:
		or      g3,r14,r14
		rotate  24,r14,r14
		cmpdeco 1,r11,r11
		bl      L1630

		st      r14,(g10)[g12]             # push r14 to geo list (reading fifo)
		ret


# build color translation (0x1810000 - 0x181bfff)
_build_colour_translation:
		lda     0x1800000,r4
		mov     0,r5
		shlo    8,1,r10 

L0aa8:
		mov     12,r9
		shlo    8,3,r11
		lda     0x1160,r12
		lda     0x8b,r13

L0abc:
		mov     0,r6

L0ac0:
		mulo    r5,r6,r7
		cmpobe  0,r7,L0acc

		addo    r12,r7,r7

L0acc:
		shro    6,r7,r7
		cmpobl  r7,r10,L0ad8

		subo    1,0,r7

L0ad8:
		stos    r7,0x10000(r4)
		stos    r7,0x14000(r4)
		stos    r7,0x18000(r4)
		addo    2,r4,r4
		addo    r9,r6,r6
		cmpobl  r6,r11,L0ac0

		mov     0,r8
		mulo    r9,r5,r6
		cmpobe  0,r6,L0b10

		addo    r13,r6,r7
		shro    1,r7,r8

L0b10:
		shlo    6,3,r6

L0b14:
		stos    r8,0x10000(r4)
		stos    r8,0x14000(r4)
		stos    r8,0x18000(r4)
		addo    2,r4,r4
		cmpdeco 1,r6,r6
		bl      L0b14

		cmpinco 31,r5,r5
		bg      L0abc

		ret


_cpu_ctrl_wait_data:				# bus-controller (0xE00000) region config.
		.word	0x00000004			# Corrected to match STF's _wait_data exactly:
		.word	0x00000002			# entries 4,5,8 were wrong (0x10,0x10,0x08), which
		.word	0x00000042			# left the COP region's bus timing misconfigured so
		.word	0x00000002			# 0x8C0000 never asserted READY (boot hung at the
		.word	0x00000001			#  [4] was 0x10  first COP IOP write on real hardware).
		.word	0x00000002			#  [5] was 0x10
		.word	0x00000010
		.word	0x00000010
		.word	0x00000020			#  [8] was 0x08
		.word	0x00000010
		.word	0x00000010
		.word	0x00000010
		.word	0x00000010
		.word	0x00000010
		.word	0xffffffff

irq_control_word:
		.word	0x0f0e0d0c					# STF interrupt_targets (was 0xff000010,
											# the IAC-port addr — a bogus IMAP value).
											# Benign while we run at priority 31, but
											# now correct/faithful per STF start_again_ip.




		.align	4

reinitialize_iac:	
		.word	0x93000000					# reinitialize IAC message
		.word	system_address_table 
		.word	0x5ff400					# use newly copied PRCB
		.word	start_again_ip				# start here 


#		.bss	intr_ram, 1028, 6
#		.bss	_prcb_ram, 176, 6

# -- Stacks
#	The _trap_stack should never get used because we never take
#	the processor out of supervisor mode (and thus never transition
#	into it).  If application code will us out of supervisor mode, 
#	care must be taken to increase the size of the _trap_stack.
#
# All i960 stacks live HIGH, by the tables (0x5FF000) and RAM PRCB (0x5FF400),
# isolated from the heap (ends 0x5F0000) and .bss. The old cramped low .bss stacks
# (_nindy_stack=0x502000, _intr_stack=0x504000 jammed together) let the vblank ISR
# smash the user stack on m2emu (invalid opcode at 0x502048). _intr/_trap match STF
# exactly; the user stack sits in the free region just below them.
		.set	_nindy_stack, 0x00520000	# application's OWN stack (crtm2): low free region
											# above .bss, far from the high interrupt stack
											# (0x5FF500) and M2_EXIT_CTL (0x5F8000). main runs
											# here; the IRQ switches to _intr_stack so the ISR
											# never lands on this frame. 128K to the app-load
											# region at 0x540000 — ample for the soft-float chains.
		.set	_intr_stack,  0x005FF500	# STF interrupt stack
		.set	_trap_stack,  0x005FF800	# STF fault/supervisor stack













