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
		lda     0x0,g1						# g1 = 0x0
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


# --  The processor will begin execution here after being
# --  reinitialized.  We will now set up the stacks and continue.
# --
start_again_ip:
#/*		call	_disable_ints				# disable board interrupts */
#
# FAITHFUL TO STF start_again_ip: NO fix_stack, NO _nindy_stack override. STF (a game
# running directly, not the NINDY monitor) does not exit the interrupted state via a
# simulated interrupt-return, and does not switch to a separate user stack — main runs on
# the IAC/supervisor stack the REINITIALIZE-IAC established, entered via `b main`. The tail
# below (synmov / clear / enable 0x21 / modpc / modac / b main) mirrors STF's
# _interrupt_register_write (ROM 0x554-0x5A4) exactly.

		mov		0, g14						# g14 used by C compiler (arg lists). Init to 0.


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

# --  STF order: modpc (unmask) FIRST, then modac. Clear the process-priority field
# --  (bits 16-20) to 0 so the i960 accepts the vblank (vector 12, priority 1) interrupt.
		shlo	0x10, 0x1f, r4				# r4 = 0x1f0000 (priority field mask)
		mov		0, r5						# r5 = 0 (new priority = 0)
		modpc	r4, r4, r5					# PC priority -> 0 (enable interrupts)

		lda     0xff1f917f,r4
		lda     0x3f001000,r5				# 0x3f001000 arithmetic-controls default
		modac   r4,r5,r5

		b		_main						# STF: `b main` — run main on the IAC/supervisor stack
											# in the start_again_ip frame; main never returns.

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













