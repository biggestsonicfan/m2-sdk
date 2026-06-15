/******************************************************************/
/* 		Copyright (c) 1989, Intel Corporation                     */
/* (Intel i960 example fault table; permission to copy/modify.)   */
/******************************************************************/
/* **************************************** */
/*      User Fault Table                    */
/*                                          */
/* Every fault vectors to fault_handler (a  */
/* LOCAL-call entry, like STF's fault table)*/
/* which draws "FAULT @ <ip>" on the tile   */
/* layer and halts. The previous Daytona    */
/* table used system-call entries routing   */
/* to a NULL fault_proc_table handler, so    */
/* any fault silently froze the board.       */
/* Local-call entries also bypass the system */
/* procedure table, so the SAT descriptors   */
/* (0x98/0xA8) are irrelevant for faults.    */
/* **************************************** */

	.globl	fault_table
	.align	8
fault_table:
	.word	fault_handler	# 0  Override Fault (MC)   (word0=handler, low bits 00 = local)
	.word	0
	.word	fault_handler	# 1  Trace Fault
	.word	0
	.word	fault_handler	# 2  Operation Fault
	.word	0
	.word	fault_handler	# 3  Arithmetic Fault
	.word	0
	.word	fault_handler	# 4  Floating Point Fault
	.word	0
	.word	fault_handler	# 5  Constraint Fault
	.word	0
	.word	fault_handler	# 6  Virtual Memory Fault (MC)
	.word	0
	.word	fault_handler	# 7  Protection Fault
	.word	0
	.word	fault_handler	# 8  Machine Fault
	.word	0
	.word	fault_handler	# 9  Structural Fault (MC)
	.word	0
	.word	fault_handler	# 10 Type Fault
	.word	0
	.word	fault_handler	# 11 reserved
	.word	0
	.word	fault_handler	# 12 Process Fault (MC)
	.word	0
	.word	fault_handler	# 13 Descriptor Fault (MC)
	.word	0
	.word	fault_handler	# 14 Event Fault (MC)
	.word	0
	.word	fault_handler	# 15 reserved
	.word	0
	.word	fault_handler	# 16
	.word	0
	.word	fault_handler	# 17
	.word	0
	.word	fault_handler	# 18
	.word	0
	.word	fault_handler	# 19
	.word	0
	.word	fault_handler	# 20
	.word	0
	.word	fault_handler	# 21
	.word	0
	.word	fault_handler	# 22
	.word	0
	.word	fault_handler	# 23
	.word	0
	.word	fault_handler	# 24
	.word	0
	.word	fault_handler	# 25
	.word	0
	.word	fault_handler	# 26
	.word	0
	.word	fault_handler	# 27
	.word	0
	.word	fault_handler	# 28
	.word	0
	.word	fault_handler	# 29
	.word	0
	.word	fault_handler	# 30
	.word	0
	.word	fault_handler	# 31
	.word	0

/* ---- debug fault handler ------------------------------------------------ *
 * Entered via a local fault call: rip (r2) = the faulting instruction's IP.
 * Draw the 8 hex digits of rip to the FG tile layer (row 36, col 24), then
 * halt so the address stays on screen. Uses palbank 1 (white text) — the same
 * pen the boot/menu shell sets up. Clobbers globals freely (we never return). */
	.globl	fault_handler
	.align	4
fault_handler:
	mov	rip, g0			# g0 = faulting instruction pointer
	lda	0x01000000, g1		# M2_TILE_FG (foreground tilemap, 64 wide)
	lda	0x1230, g2		# byte offset: (36*64 + 24) * 2
	addo	g1, g2, g1		# g1 -> destination tile cell
	mov	8, g5			# 8 hex nibbles
	mov	28, g2			# shift for the high nibble first
fh_digit:
	shro	g2, g0, g3		# g3 = ip >> shift
	and	0xf, g3, g3		# g3 = nibble (0..15)
	cmpi	g3, 9
	bg	fh_alpha
	lda	0x30, g6		# '0' (i960 literals are 0..31, so load it)
	b	fh_add
fh_alpha:
	lda	0x37, g6		# 'A' - 10
fh_add:
	addo	g6, g3, g3		# g3 = ASCII hex char
	lda	0x8080, g4		# tile: above-3D prio (0x8000) | palbank 1 (0x80)
	or	g4, g3, g3
	stos	g3, (g1)		# write glyph cell (16-bit)
	addo	2, g1, g1		# next cell
	subo	4, g2, g2		# next lower nibble
	cmpdeco	1, g5, g5
	bl	fh_digit
fh_halt:
	b	fh_halt			# park here so the fault address stays visible
