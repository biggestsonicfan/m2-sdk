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

/* ---- per-type fault handlers (STF names) -------------------------------- */
	.align	4
f_trace:	lda	s_trace, r4
		b	fault_show
f_operation:	lda	s_operation, r4
		b	fault_show
f_arith:	lda	s_arith, r4
		b	fault_show
f_rtarith:	lda	s_rtarith, r4
		b	fault_show
f_constrain:	lda	s_constrain, r4
		b	fault_show
f_protect:	lda	s_protect, r4
		b	fault_show
f_machine:	lda	s_machine, r4
		b	fault_show
f_type:		lda	s_type, r4
		b	fault_show
f_unknown:	lda	s_unknown, r4
		b	fault_show

/* fault_show: r4 = fault-name string, rip(r2) = faulting IP.
 * Draw  "<name>"  /  "IP :"  /  "<hex ip>"  on the tile layer, then halt. */
	.align	4
fault_show:
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
s_ip:		.asciz	"IP :"
