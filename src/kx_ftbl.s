/******************************************************************/
/*      User Fault Table  (modelled on Sonic the Fighters)        */
/*                                                                */
/* Local-call entries (word0 = handler, word1 = 0), one per fault */
/* type, matching STF's fault_table mapping. Each handler loads a  */
/* fault-name string (STF's names) and branches to fault_show,     */
/* which draws "<name>" then "IP :" then the hex faulting IP to the */
/* FG tile layer the same way STF's print_fault does (tile entry = */
/* 0x8000 | char, i.e. priority bit + palbank 0), then halts.      */
/* (The old Daytona table routed every fault to a null procedure-  */
/* table handler, so faults silently froze the board.)            */
/******************************************************************/

	.globl	fault_table
	.align	8
fault_table:
	.word	f_unknown	# 0  override / parallel
	.word	0
	.word	f_trace		# 1  Trace
	.word	0
	.word	f_operation	# 2  Operation
	.word	0
	.word	f_arith		# 3  Arithmetic
	.word	0
	.word	f_rtarith	# 4  Real (FP) Arithmetic
	.word	0
	.word	f_constrain	# 5  Constraint
	.word	0
	.word	f_unknown	# 6
	.word	0
	.word	f_protect	# 7  Protection
	.word	0
	.word	f_machine	# 8  Machine
	.word	0
	.word	f_unknown	# 9
	.word	0
	.word	f_type		# 10 Type
	.word	0
	.word	f_unknown	# 11
	.word	0
	.word	f_unknown	# 12
	.word	0
	.word	f_unknown	# 13
	.word	0
	.word	f_unknown	# 14
	.word	0
	.word	f_unknown	# 15
	.word	0
	.word	f_unknown	# 16
	.word	0
	.word	f_unknown	# 17
	.word	0
	.word	f_unknown	# 18
	.word	0
	.word	f_unknown	# 19
	.word	0
	.word	f_unknown	# 20
	.word	0
	.word	f_unknown	# 21
	.word	0
	.word	f_unknown	# 22
	.word	0
	.word	f_unknown	# 23
	.word	0
	.word	f_unknown	# 24
	.word	0
	.word	f_unknown	# 25
	.word	0
	.word	f_unknown	# 26
	.word	0
	.word	f_unknown	# 27
	.word	0
	.word	f_unknown	# 28
	.word	0
	.word	f_unknown	# 29
	.word	0
	.word	f_unknown	# 30
	.word	0
	.word	f_unknown	# 31
	.word	0

/* ---- per-type fault handlers (STF names + the fault-table type index in r5) ---- */
	.align	4
f_trace:	lda	s_trace, r4
		mov	1, r5
		b	fault_show
f_operation:	lda	s_operation, r4
		mov	2, r5
		b	fault_show
f_arith:	lda	s_arith, r4
		mov	3, r5
		b	fault_show
f_rtarith:	lda	s_rtarith, r4
		mov	4, r5
		b	fault_show
f_constrain:	lda	s_constrain, r4
		mov	5, r5
		b	fault_show
f_protect:	lda	s_protect, r4
		mov	7, r5
		b	fault_show
f_machine:	lda	s_machine, r4
		mov	8, r5
		b	fault_show
f_type:		lda	s_type, r4
		mov	10, r5
		b	fault_show
f_unknown:	lda	s_unknown, r4
		lda	0xff, r5
		b	fault_show

/* SYNTHETIC fault entry (C-callable: `fault_selftest()`): drives the record + pivot +
 * recovery chain below without CPU fault dispatch — MAME's i960 core has none (invalid
 * ops fatalerror the emulator), so this is how the chain is exercised there; genuine
 * silicon faults arrive via the table above and join the very same path. Called
 * normally, so rip = the caller's return IP — a truthful "faulting IP". */
	.globl	fault_selftest
	.align	4
fault_selftest:
	lda	s_selftest, r4
	lda	0xfe, r5
	b	fault_show

/* fault_show: r4 = fault-name string, r5 = type code, rip(r2) = faulting IP.
 * 1. draw "<name>" / "IP :" / "<hex ip>" on the tile layer (works with no serial);
 * 2. record {type, ip, count++} into the FIXED m2_fault_t @0x5F0040 (m2_fault.h —
 *    layout asserted there; keep the offsets below in sync);
 * 3. if a recovery entry is ARMED (recover_fn != 0): flush the register cache,
 *    pivot to a fresh frame at recover_sp, and BRANCH into it (the kx_init
 *    `b _main` pattern) — the monitor survives the fault. Unarmed: halt (legacy). */
	.align	4
fault_show:
	# ---- NINDY register-frame capture (m2_regs @ 0x5F0060; m2_fault.h m2_regs_t) ----
	# Do this FIRST: g0-g15 are still the faulting code's globals here (fault entry is a
	# local call, which preserves globals; the per-type handler only touched r4/r5).
	# The print code below clobbers g0-g15, so snapshot them now using local scratch.
	# register_set order: r0-15 @+0, g0-15 @+64, pc @+128, ac @+132, ip @+136, tc @+140.
	lda	0x005F0060, r6		# r6 -> m2_regs
	stq	g0,  64(r6)		# g0-g3   -> register_set[16..19]
	stq	g4,  80(r6)		# g4-g7   -> [20..23]
	stq	g8,  96(r6)		# g8-g11  -> [24..27]
	stq	g12, 112(r6)		# g12-g15 -> [28..31] (g15 = fp)
	mov	rip, r7
	st	r7, 136(r6)		# rip -> register_set[34] = ip (solid)
	# faulting frame's locals r0-r15 via pfp (NINDY kx faultasm recipe; silicon-pending)
	flushreg			# make the faulting frame's locals current in memory
	ldconst	0xfffffff0, r8
	mov	pfp, r9
	and	r9, r8, r9		# mask pfp return bits -> faulting frame base
	ldq	0(r9),  r12
	stq	r12, 0(r6)		# r0-r3
	ldq	16(r9), r12
	stq	r12, 16(r6)		# r4-r7
	ldq	32(r9), r12
	stq	r12, 32(r6)		# r8-r11
	ldq	48(r9), r12
	stq	r12, 48(r6)		# r12-r15
	mov	0, r12			# pc/ac/tc: from the fault record (Tier-B/silicon TODO)
	st	r12, 128(r6)		# pc = 0 (honest placeholder, not a wrong value)
	st	r12, 132(r6)		# ac = 0
	st	r12, 140(r6)		# tc = 0

	# make sure palbank-0 pen 1 (the glyph pen) is visible white
	lda	0x01800002, g2		# palette entry: palbank 0, pixel 1
	lda	0x00007fff, g3		# white (BGR555)
	stos	g3, (g2)
	# name @ row 30 col 2
	lda	0x01000f04, g9
	mov	r4, g0
	call	pmes
	# "IP :" @ row 31 col 2
	lda	0x01000f84, g9
	lda	s_ip, g0
	call	pmes
	# hex faulting IP @ row 31 col 8
	lda	0x01000f90, g9
	mov	rip, g0
	call	phex

	# ---- record the fault (m2_fault_t @ 0x5F0040: magic/count/type/ip/fn/sp) ----
	lda	0x005F0040, r10
	lda	0x4D325246, r11		# 'M2FR'
	ld	(r10), r12		# magic valid? (cold RAM -> start count at 0)
	cmpo	r11, r12
	be	fr_count
	st	r11, (r10)		# fresh record
	mov	0, r12
	st	r12, 4(r10)
fr_count:
	ld	4(r10), r12
	addo	1, r12, r12
	st	r12, 4(r10)		# count++
	st	r5, 8(r10)		# type
	st	rip, 12(r10)		# faulting IP

	# ---- recovery armed? pivot to the fresh stack and branch into the kernel ----
	ld	16(r10), r12		# recover_fn
	cmpo	r12, 0
	be	fs_halt			# unarmed: legacy print-and-halt
	ld	24(r10), r14		# FAULT-STORM GUARD: a fault DURING recovery means
	cmpo	r14, 0			# the kernel/bus state is too damaged to recover —
	bne	fs_halt			# halt (legacy) instead of looping fault->recover
	mov	1, r14
	st	r14, 24(r10)		# busy = 1 (recovery clears it at steady state)
	ld	20(r10), r13		# recover_sp (fresh frame base)
	flushreg			# spill/empty the register cache before re-basing
	mov	0, pfp			# terminate the frame chain
	mov	r13, fp
	lda	64(r13), sp		# fresh frame: sp = fp + 64
	mov	0, g14			# C ABI: g14 = 0 (the kx_init `b _main` pattern)
	bx	(r12)			# never returns (re-enters the serve loop)
fs_halt:
	b	fs_halt

/* pmes(g0=asciiz, g9=tile dest): write 0x8000|char per glyph (STF print_mes). */
	.align	4
pmes:
	mov	g0, r3
	mov	g9, r4
	lda	0x00008000, r5		# tile priority bit (palbank 0)
pmes_l:
	ldob	(r3), r6
	addo	1, r3, r3
	cmpo	r6, 0
	be	pmes_x
	or	r5, r6, r6
	stos	r6, (r4)
	addo	2, r4, r4
	b	pmes_l
pmes_x:
	ret

/* phex(g0=value, g9=tile dest): write 8 hex digits as 0x8000|char. */
	.align	4
phex:
	mov	g0, r3			# value
	mov	g9, r4			# dest
	lda	0x00008000, r5		# tile priority bit
	mov	8, r6			# 8 nibbles
	mov	28, r7			# shift, high nibble first
phex_l:
	shro	r7, r3, r8
	and	0xf, r8, r8
	cmpi	r8, 9
	bg	phex_a
	lda	0x30, r9		# '0'
	b	phex_e
phex_a:
	lda	0x37, r9		# 'A' - 10
phex_e:
	addo	r9, r8, r8
	or	r5, r8, r8
	stos	r8, (r4)
	addo	2, r4, r4
	subo	4, r7, r7
	cmpdeco	1, r6, r6
	bl	phex_l
	ret

/* ---- fault-name strings (STF's names) ---------------------------------- */
	.align	4
s_trace:	.asciz	"trace"
s_operation:	.asciz	"operation"
s_arith:	.asciz	"arithmetic"
s_rtarith:	.asciz	"real_arithmetic"
s_constrain:	.asciz	"constrain"
s_protect:	.asciz	"protection"
s_machine:	.asciz	"machine"
s_type:		.asciz	"type"
s_unknown:	.asciz	"unknown"
s_selftest:	.asciz	"selftest"
s_ip:		.asciz	"IP :"
