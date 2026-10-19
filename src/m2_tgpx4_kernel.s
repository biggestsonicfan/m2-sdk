; m2_tgpx4_kernel.s - the SDK's own Model 2C coprocessor program (Fujitsu MB86235 "TGPx4").
;
; The 2C counterpart of m2_tgp_kernel.s (2A): the same FIFO protocol, the same commands, the
; same results. The i960 writes a command word (COP_CMD(n), m2_constants.h) and its arguments
; to 0x884000 and reads the results from the same address. The command number is bits 30:23
; of the word, which is also what the 2C function port (0x880000 + n*4) makes, so both work.
; The commands cpres1 has and this file has no code for (0x00-0x87, except the ones cpres1
; ignores) run in the VM at the end of the file; 0x88-0xff and those are ignored (their
; arguments would then be read as commands).
;
; Hardware used (MAME src/mame/sega/model2.cpp, copro_tgpx4_*; src/devices/cpu/mb86235):
; FI = input FIFO (a read waits for data), FO0 = output FIFO (a write waits for room). The
; external bus at 0x400000 is buffer RAM (i960 0x900000), one word per address: SUBMIT
; and the VM reach it through EB + EO. Internal A RAM (0x400 words) holds the dispatch table
; (0x000), the current matrix (0x100), the matrix stack (0x200), the VM's registers (0x300) and
; its op table (0x3c0). A fixed word is A(@AR6+n) with AR6 = 0, as in Dynamite
; Cop's program: upstream MAME has no absolute address mode (0x0c; docs/model2-boards.md).
;
; Timing rules this code relies on (mb86235ops.cpp): one delay slot after every branch, call
; and return; a branch tests the flags left by an earlier instruction; in one instruction the
; transfer reads its source before the ALU/MUL and writes after it. FSUB x, y = y - x and
; FCMP x, y sets the flags on y - x; the same for SUB/CMP. FRCP and FRSQ are seeds on the
; chip (exact in MAME): divide and square root refine them with two Newton steps, as Dynamite
; Cop's program does.
;
; Assemble: python3 tools/tgpx4asm.py src/m2_tgpx4_kernel.s --header src/m2_tgpx4_kernel.h --name m2_tgpx4_kernel

.equ MAT, 100           ; current matrix, as cpres1 keeps it: col0, col1, col2, T (12 words)
.equ C0,  100
.equ C1,  103
.equ C2,  106
.equ TV,  109
.equ DEP, 110           ; matrix stack depth
.equ SPX, 111           ; offset of the next free slot (12 per slot); the slots are A RAM 200 + SPX
.equ SLOTS, 200

; ---- start-up ---------------------------------------------------------------------------------

start:  NOP : SETM #0090H               ; the mode Dynamite Cop's program sets
        MOV3 #00400000, EB              ; external base = buffer RAM, kept for SUBMIT
        MOV3 #0, EO
        MOV3 #0, SP
        MOV3 #0, AR6                    ; AR6 stays 0: A(@AR6+n) is A RAM word n
        NOP : CLRF
        MOV3 #0, AR0                    ; dispatch table: 0x00-0x87 -> the VM, the rest -> none
        MOV3 #vm, AA0
        NOP : REP #88H
        NOP : MOV2 AA0, A(@AR0++)
        MOV3 #none, AA0
        NOP : REP #78H
        NOP : MOV2 AA0, A(@AR0++)
        MOV3 #none,   A(@AR6+33H)
        MOV3 #none,   A(@AR6+3CH)
        MOV3 #none,   A(@AR6+40H)
        MOV3 #none,   A(@AR6+4BH)
        MOV3 #none,   A(@AR6+4CH)
        MOV3 #none,   A(@AR6+4DH)
        MOV3 #none,   A(@AR6+4EH)
        MOV3 #none,   A(@AR6+4FH)
        MOV3 #none,   A(@AR6+50H)
        MOV3 #none,   A(@AR6+52H)
        MOV3 #none,   A(@AR6+53H)
        MOV3 #none,   A(@AR6+5FH)
        MOV3 #none,   A(@AR6+60H)
        MOV3 #none,   A(@AR6+61H)
        MOV3 #none,   A(@AR6+64H)
        MOV3 #none,   A(@AR6+65H)
        MOV3 #none,   A(@AR6+66H)
        MOV3 #none,   A(@AR6+6CH)
        MOV3 #none,   A(@AR6+6DH)
        MOV3 #none,   A(@AR6+6EH)
        MOV3 #none,   A(@AR6+6FH)
        MOV3 #init,   A(@AR6+00H)
        MOV3 #push,   A(@AR6+01H)
        MOV3 #pop,    A(@AR6+02H)
        MOV3 #ident,  A(@AR6+03H)
        MOV3 #ldmat,  A(@AR6+04H)
        MOV3 #rmat,   A(@AR6+05H)
        MOV3 #setpos, A(@AR6+06H)
        MOV3 #scale,  A(@AR6+07H)
        MOV3 #angx,   A(@AR6+08H)
        MOV3 #angy,   A(@AR6+09H)
        MOV3 #angz,   A(@AR6+0AH)
        MOV3 #fadd,   A(@AR6+13H)
        MOV3 #fsub,   A(@AR6+14H)
        MOV3 #fmul,   A(@AR6+15H)
        MOV3 #fdiv,   A(@AR6+16H)
        MOV3 #int2f,  A(@AR6+17H)
        MOV3 #f2int,  A(@AR6+18H)
        MOV3 #sqrt,   A(@AR6+1AH)
        MOV3 #sin,    A(@AR6+21H)
        MOV3 #cos,    A(@AR6+22H)
        MOV3 #tan,    A(@AR6+23H)
        MOV3 #sinsc,  A(@AR6+24H)
        MOV3 #cossc,  A(@AR6+25H)
        MOV3 #dist2d, A(@AR6+2BH)
        MOV3 #dist3d, A(@AR6+2CH)
        MOV3 #mag2d,  A(@AR6+2DH)
        MOV3 #atan2,  A(@AR6+2FH)
        MOV3 #dot2d,  A(@AR6+59H)
        MOV3 #add3,   A(@AR6+5CH)
        MOV3 #sub3,   A(@AR6+5DH)
        MOV3 #scale3, A(@AR6+5EH)
        MOV3 #submit, A(@AR6+78H)
        ; the VM's op table (VTAB), one entry per op in tools/rv2cvm.py's OPS order
        MOV3 #vmnext,  A(@AR6+3C0H)        ; NOP
        MOV3 #vLI,     A(@AR6+3C1H)        ; LI
        MOV3 #vADD,    A(@AR6+3C2H)        ; ADD
        MOV3 #vSUB,    A(@AR6+3C3H)        ; SUB
        MOV3 #vAND,    A(@AR6+3C4H)        ; AND
        MOV3 #vOR,     A(@AR6+3C5H)        ; OR
        MOV3 #vXOR,    A(@AR6+3C6H)        ; XOR
        MOV3 #vSLL,    A(@AR6+3C7H)        ; SLL
        MOV3 #vSRL,    A(@AR6+3C8H)        ; SRL
        MOV3 #vSRA,    A(@AR6+3C9H)        ; SRA
        MOV3 #vSLT,    A(@AR6+3CAH)        ; SLT
        MOV3 #vSLTU,   A(@AR6+3CBH)        ; SLTU
        MOV3 #vADDI,   A(@AR6+3CCH)        ; ADDI
        MOV3 #vANDI,   A(@AR6+3CDH)        ; ANDI
        MOV3 #vORI,    A(@AR6+3CEH)        ; ORI
        MOV3 #vXORI,   A(@AR6+3CFH)        ; XORI
        MOV3 #vSLLI,   A(@AR6+3D0H)        ; SLLI
        MOV3 #vSRLI,   A(@AR6+3D1H)        ; SRLI
        MOV3 #vSRAI,   A(@AR6+3D2H)        ; SRAI
        MOV3 #vSLTI,   A(@AR6+3D3H)        ; SLTI
        MOV3 #vSLTIU,  A(@AR6+3D4H)        ; SLTIU
        MOV3 #vLW,     A(@AR6+3D5H)        ; LW
        MOV3 #vSW,     A(@AR6+3D6H)        ; SW
        MOV3 #vLBU,    A(@AR6+3D7H)        ; LBU
        MOV3 #vSB,     A(@AR6+3D8H)        ; SB
        MOV3 #vFLW,    A(@AR6+3D9H)        ; FLW
        MOV3 #vFSW,    A(@AR6+3DAH)        ; FSW
        MOV3 #vBEQ,    A(@AR6+3DBH)        ; BEQ
        MOV3 #vBNE,    A(@AR6+3DCH)        ; BNE
        MOV3 #vBLT,    A(@AR6+3DDH)        ; BLT
        MOV3 #vBGE,    A(@AR6+3DEH)        ; BGE
        MOV3 #vBLTU,   A(@AR6+3DFH)        ; BLTU
        MOV3 #vBGEU,   A(@AR6+3E0H)        ; BGEU
        MOV3 #vJAL,    A(@AR6+3E1H)        ; JAL
        MOV3 #vJAL,    A(@AR6+3E2H)        ; J
        MOV3 #vJALR,   A(@AR6+3E3H)        ; JALR
        MOV3 #vJALR,   A(@AR6+3E4H)        ; JR
        MOV3 #vFADD,   A(@AR6+3E5H)        ; FADD
        MOV3 #vFSUB,   A(@AR6+3E6H)        ; FSUB
        MOV3 #vFMUL,   A(@AR6+3E7H)        ; FMUL
        MOV3 #vFDIV,   A(@AR6+3E8H)        ; FDIV
        MOV3 #vFSQRT,  A(@AR6+3E9H)        ; FSQRT
        MOV3 #vFMOV,   A(@AR6+3EAH)        ; FMOV
        MOV3 #vFNEG,   A(@AR6+3EBH)        ; FNEG
        MOV3 #vFABS,   A(@AR6+3ECH)        ; FABS
        MOV3 #vFLT,    A(@AR6+3EDH)        ; FLT
        MOV3 #vFLE,    A(@AR6+3EEH)        ; FLE
        MOV3 #vFEQ,    A(@AR6+3EFH)        ; FEQ
        MOV3 #vFCVTWS, A(@AR6+3F0H)        ; FCVTWS
        MOV3 #vFCVTSW, A(@AR6+3F1H)        ; FCVTSW
        MOV3 #vFMAX,   A(@AR6+3F2H)        ; FMAX
        MOV3 #vFMVXW,  A(@AR6+3F3H)        ; FMVXW
        MOV3 #vFMVWX,  A(@AR6+3F4H)        ; FMVWX
        MOV3 #vIN,     A(@AR6+3F5H)        ; IN
        MOV3 #vOUT,    A(@AR6+3F6H)        ; OUT
        MOV3 #vSIN,    A(@AR6+3F7H)        ; SIN
        MOV3 #vCOS,    A(@AR6+3F8H)        ; COS
        MOV3 #vIRD,    A(@AR6+3F9H)        ; IRD
        MOV3 #vIWR,    A(@AR6+3FAH)        ; IWR
        MOV3 #vPUSH,   A(@AR6+3FBH)        ; PUSH
        MOV3 #vPOP,    A(@AR6+3FCH)        ; POP
        MOV3 #vRSQRT,  A(@AR6+3FDH)        ; RSQRT
        MOV3 #vHALT,   A(@AR6+3FEH)        ; HALT
        MOV3 #0, A(@AR6+DEP)                 ; empty matrix stack
        MOV3 #0, A(@AR6+SPX)
        NOP : DJMP ident                ; start with the identity matrix
        NOP : NOP

; ---- dispatch -----------------------------------------------------------------------------------

loop:   NOP : MOV2 FI, AA0              ; command word
        LSR AA0, #17H, AA0 : NOP        ; bits 30:23 = command number
        MOV3 #0FF, AA1
        AND AA0, AA1, AA0 : NOP
        NOP : MOV2 AA0, AR1
        NOP : DJMP A(AR1)
        NOP : NOP

none:   NOP : DJMP loop
        NOP : NOP

; Every handler leaves through "NOP : DJMP loop" with its last result move in the delay slot.

; ---- scalar -------------------------------------------------------------------------------------

fadd:   NOP : MOV2 FI, AA0              ; 0x13 a + b
        NOP : MOV2 FI, AA1
        FADD AA0, AA1, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

fsub:   NOP : MOV2 FI, AA0              ; 0x14 a - b
        NOP : MOV2 FI, AA1
        FSUB AA1, AA0, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

fmul:   NOP : MOV2 FI, MA0              ; 0x15 a * b
        NOP : MOV2 FI, MA1
        FMUL MA0, MA1, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

fdiv:   NOP : MOV2 FI, AA0              ; 0x16 a / b
        NOP : MOV2 FI, AA1
        NOP : DCALL div
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

int2f:  NOP : MOV2 FI, AA0              ; 0x17 (float)i
        CIF AA0, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

f2int:  NOP : MOV2 FI, AA0              ; 0x18 floor(f): cpres1 rounds down (-3.7 -> -4, 5.9 -> 5)
        NOP : DCALL floor
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA1, FO0

sqrt:   NOP : MOV2 FI, AA0              ; 0x1a sqrt(a)
        NOP : DCALL sqrtd
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

; ---- trig (16-bit angle, 0x10000 = 360 degrees) ---------------------------------------------------

sin:    NOP : MOV2 FI, AA0              ; 0x21 sin(ang)
        NOP : DCALL sincos
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA6, FO0

cos:    NOP : MOV2 FI, AA0              ; 0x22 cos(ang)
        NOP : DCALL sincos
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA7, FO0

tan:    NOP : MOV2 FI, AA0              ; 0x23 sin(ang) / cos(ang)
        NOP : DCALL sincos
        NOP : NOP
        NOP : MOV2 AA6, AA0
        NOP : DCALL div
        NOP : MOV2 AA7, AA1
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

sinsc:  NOP : MOV2 FI, AA0              ; 0x24 (ang, s) -> sin(ang) * s
        NOP : DCALL sincos
        NOP : NOP
        NOP : MOV2 FI, MA0
        FMUL MB6, MA0, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

cossc:  NOP : MOV2 FI, AA0              ; 0x25 (ang, s) -> cos(ang) * s
        NOP : DCALL sincos
        NOP : NOP
        NOP : MOV2 FI, MA0
        FMUL MB7, MA0, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

; ---- distances / products -------------------------------------------------------------------------

dist2d: NOP : DCALL sqdiff              ; 0x2b (x1, x2, z1, z2) -> sqrt((x2-x1)^2 + (z2-z1)^2)
        NOP : NOP
        NOP : DCALL sqdiff
        NOP : MOV2 AA0, AB0
        FADD AA0, AB0, AA0 : NOP
        NOP : DCALL sqrtd
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

dist3d: NOP : DCALL sqdiff              ; 0x2c (x1, x2, y1, y2, z1, z2) -> 3D distance
        NOP : NOP
        NOP : DCALL sqdiff
        NOP : MOV2 AA0, AB0
        FADD AA0, AB0, AB0 : NOP
        NOP : DCALL sqdiff
        NOP : NOP
        FADD AA0, AB0, AA0 : NOP
        NOP : DCALL sqrtd
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

mag2d:  NOP : MOV2 FI, MA0              ; 0x2d (a, b) -> sqrt(a*a + b*b)
        FMUL MA0, MA0, AB0 : NOP
        NOP : MOV2 FI, MA0
        FMUL MA0, MA0, AA0 : NOP
        FADD AA0, AB0, AA0 : NOP
        NOP : DCALL sqrtd
        NOP : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

dot2d:  NOP : MOV2 FI, MA0              ; 0x59 (a, b, c, d) -> a*b + c*d
        NOP : MOV2 FI, MA1
        FMUL MA0, MA1, AB0 : NOP
        NOP : MOV2 FI, MA0
        NOP : MOV2 FI, MA1
        FMUL MA0, MA1, AA0 : NOP
        FADD AA0, AB0, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0

add3:   NOP : SETL #3                   ; 0x5c (a0, b0, a1, b1, a2, b2) -> (a0+b0, a1+b1, a2+b2)
add3l:  NOP : MOV2 FI, AA0
        NOP : MOV2 FI, AA1
        FADD AA0, AA1, AA0 : NOP
        NOP : DBLP add3l
        NOP : MOV2 AA0, FO0
        NOP : DJMP loop
        NOP : NOP

sub3:   NOP : SETL #3                   ; 0x5d (a0, b0, a1, b1, a2, b2) -> (a0-b0, a1-b1, a2-b2)
sub3l:  NOP : MOV2 FI, AA0
        NOP : MOV2 FI, AA1
        FSUB AA1, AA0, AA0 : NOP
        NOP : DBLP sub3l
        NOP : MOV2 AA0, FO0
        NOP : DJMP loop
        NOP : NOP

scale3: NOP : MOV2 FI, MA1              ; 0x5e (s, x, y, z) -> (s*x, s*y, s*z)
        NOP : SETL #3
sc3l:   NOP : MOV2 FI, MA0
        FMUL MA0, MA1, AA0 : NOP
        NOP : DBLP sc3l
        NOP : MOV2 AA0, FO0
        NOP : DJMP loop
        NOP : NOP

; ---- the current matrix (what m2_obj_submit sends before polygon_submit) --------------------------
; Same results as cpres1 and the 2A kernel: each angle command post-multiplies the rotation, so
; it changes two columns; SET_POS adds one column at a time, in the order cpres1 rounds it.

ident:  MOV3 #f:1.0, A(@AR6+100H)            ; 0x03 rot = I, T = 0
        MOV3 #0, A(@AR6+101H)
        MOV3 #0, A(@AR6+102H)
        MOV3 #0, A(@AR6+103H)
        MOV3 #f:1.0, A(@AR6+104H)
        MOV3 #0, A(@AR6+105H)
        MOV3 #0, A(@AR6+106H)
        MOV3 #0, A(@AR6+107H)
        MOV3 #f:1.0, A(@AR6+108H)
        MOV3 #0, A(@AR6+109H)
        MOV3 #0, A(@AR6+10AH)
        MOV3 #0, A(@AR6+10BH)
        NOP : DJMP loop
        NOP : NOP

rmat:   MOV3 #MAT, AR4                  ; 0x05 -> col0, col1, col2, T (the slot as cpres1 holds it)
        NOP : SETL #0C
rmatl:  NOP : MOV2 A(@AR4++), AA0
        NOP : DBLP rmatl
        NOP : MOV2 AA0, FO0
        NOP : DJMP loop
        NOP : NOP

setpos: MOV3 #C0, AR4                   ; 0x06 (x, y, z): T += x*col0, then y*col1, then z*col2
        NOP : SETL #3
spl:    NOP : MOV2 FI, MA1
        MOV3 #TV, AR5
        NOP : MOV2 A(@AR4++), MA0
        FMUL MA0, MA1, AA0 : MOV2 A(@AR5), AA1
        FADD AA0, AA1, AA0 : NOP
        NOP : MOV2 AA0, A(@AR5++)
        NOP : MOV2 A(@AR4++), MA0
        FMUL MA0, MA1, AA0 : MOV2 A(@AR5), AA1
        FADD AA0, AA1, AA0 : NOP
        NOP : MOV2 AA0, A(@AR5++)
        NOP : MOV2 A(@AR4++), MA0
        FMUL MA0, MA1, AA0 : MOV2 A(@AR5), AA1
        FADD AA0, AA1, AA0 : NOP
        NOP : DBLP spl
        NOP : MOV2 AA0, A(@AR5++)
        NOP : DJMP loop
        NOP : NOP

scale:  MOV3 #C0, AR4                   ; 0x07 (sx, sy, sz): col k *= s_k
        NOP : SETL #3
scl:    NOP : MOV2 FI, MA1
        NOP : MOV2 A(@AR4), MA0
        FMUL MA0, MA1, AA0 : NOP
        NOP : MOV2 AA0, A(@AR4++)
        NOP : MOV2 A(@AR4), MA0
        FMUL MA0, MA1, AA0 : NOP
        NOP : MOV2 AA0, A(@AR4++)
        NOP : MOV2 A(@AR4), MA0
        FMUL MA0, MA1, AA0 : NOP
        NOP : DBLP scl
        NOP : MOV2 AA0, A(@AR4++)
        NOP : DJMP loop
        NOP : NOP

angx:   NOP : MOV2 FI, AA0              ; 0x08 (ang): col2' = c*col2 + s*col1, col1' = c*col1 - s*col2
        NOP : DCALL sincos
        NOP : NOP
        MOV3 #C2, AR4
        MOV3 #C1, AR5
        NOP : DCALL rot
        NOP : NOP
        NOP : DJMP loop
        NOP : NOP

angy:   NOP : MOV2 FI, AA0              ; 0x09 (ang): col0' = c*col0 + s*col2, col2' = c*col2 - s*col0
        NOP : DCALL sincos
        NOP : NOP
        MOV3 #C0, AR4
        MOV3 #C2, AR5
        NOP : DCALL rot
        NOP : NOP
        NOP : DJMP loop
        NOP : NOP

angz:   NOP : MOV2 FI, AA0              ; 0x0a (ang): col1' = c*col1 + s*col0, col0' = c*col0 - s*col1
        NOP : DCALL sincos
        NOP : NOP
        MOV3 #C1, AR4
        MOV3 #C0, AR5
        NOP : DCALL rot
        NOP : NOP
        NOP : DJMP loop
        NOP : NOP

; ---- matrix stack (cpres1 Fn_push_matrix / Fn_pop_matrix) ----------------------------------------
; As in the 2A kernel: the current matrix stays at MAT, PUSH copies it into the next slot and
; POP copies the slot back. PUSH at depth >= 7 and POP at depth >= 8 only change the count, POP
; at depth 0 does nothing (cpres1 counts the depth in its jump's delay slots).

push:   NOP : DCALL pushs               ; 0x01
        NOP : NOP
        NOP : DJMP loop
        NOP : NOP

pop:    NOP : DCALL pops                ; 0x02
        NOP : NOP
        NOP : DJMP loop
        NOP : NOP

; depth++, and below depth 7 save the matrix. Uses AA0-AA3, AR4, AR5 (also the VM's PUSH).
pushs:  NOP : MOV2 A(@AR6+DEP), AA0
        MOV3 #7, AA1
        ADD AA0, 1, AA2 : NOP
        CMP AA1, AA0 : MOV2 AA2, A(@AR6+DEP) ; old depth - 7
        NOP : DBNAN pret                ; the old depth was >= 7
        NOP : NOP
        NOP : MOV2 A(@AR6+SPX), AA0
        MOV3 #0C, AA1
        ADD AA0, AA1, AA1 : NOP
        MOV3 #SLOTS, AA3
        ADD AA0, AA3, AA3 : MOV2 AA1, A(@AR6+SPX)
        NOP : MOV2 AA3, AR5
        MOV3 #MAT, AR4
        NOP : SETL #0C
pushl:  NOP : MOV2 A(@AR4++), AA0
        NOP : DBLP pushl
        NOP : MOV2 AA0, A(@AR5++)
pret:   NOP : DRET
        NOP : NOP

; depth--, and below depth 8 restore the matrix. Uses AA0-AA3, AR4, AR5 (also the VM's POP).
pops:   NOP : MOV2 A(@AR6+DEP), AA0
        MOV3 #8, AA1
        ADD AA0, -1, AA2 : NOP
        CMP AA0, 0 : NOP
        NOP : DBAZ pret                 ; depth 0: nothing
        NOP : NOP
        CMP AA1, AA0 : MOV2 AA2, A(@AR6+DEP) ; old depth - 8
        NOP : DBNAN pret                ; the old depth was >= 8
        NOP : NOP
        NOP : MOV2 A(@AR6+SPX), AA0
        MOV3 #0C, AA1
        SUB AA1, AA0, AA0 : NOP
        MOV3 #SLOTS, AA3
        ADD AA0, AA3, AA3 : MOV2 AA0, A(@AR6+SPX)
        NOP : MOV2 AA3, AR5
        MOV3 #MAT, AR4
        NOP : SETL #0C
popl:   NOP : MOV2 A(@AR5++), AA0
        NOP : DBLP popl
        NOP : MOV2 AA0, A(@AR4++)
        NOP : DRET
        NOP : NOP

init:   NOP : MOV2 A(@AR6+DEP), AA0     ; 0x00 (cpres1 Fn_initialize): depth 0, back to the first
        CMP AA0, 0 : NOP                ; slot. That slot holds the matrix the first PUSH saved,
        NOP : DBAZ init0                ; so with a depth the matrix becomes that one
        NOP : NOP
        MOV3 #SLOTS, AR5
        MOV3 #MAT, AR4
        NOP : SETL #0C
initl:  NOP : MOV2 A(@AR5++), AA0
        NOP : DBLP initl
        NOP : MOV2 AA0, A(@AR4++)
init0:  MOV3 #0, A(@AR6+DEP)
        MOV3 #0, A(@AR6+SPX)
        NOP : DJMP loop
        NOP : NOP

ldmat:  MOV3 #MAT, AR4                  ; 0x04 (12 words) -> col0, col1, col2, T
        NOP : SETL #0C
ldml:   NOP : MOV2 FI, AA0
        NOP : DBLP ldml
        NOP : MOV2 AA0, A(@AR4++)
        NOP : DJMP loop
        NOP : NOP

; ---- SUBMIT (cpres1 Fn_put_poly) ------------------------------------------------------------------
; In: wr, flag, tpa, tha, oba, obc, p2, p. Writes 18 words to buffer RAM at byte offset wr:
; 0x05800b0b, the 12 matrix words, 0x00800101, tpa, tha, oba, obc & 0xffff. Out: p2, p + n,
; where n = obc & 0xffff. With flag != 0 it also adds obc >> 14 to tha and n to p2.

submit: NOP : MOV2 FI, AA0              ; 0x78
        LSR AA0, #2, AA0 : NOP
        NOP : MOV2 AA0, EO              ; buffer RAM word index
        NOP : MOV2 FI, AB0              ; flag
        NOP : MOV2 FI, AB1              ; tpa
        NOP : MOV2 FI, AB2              ; tha
        NOP : MOV2 FI, AB3              ; oba
        NOP : MOV2 FI, AB4              ; obc
        MOV3 #05800B0B, AA0
        NOP : MOV2 AA0, E(@EB+EO++1H)
        MOV3 #MAT, AR4
        NOP : SETL #0C
subl:   NOP : MOV2 A(@AR4++), AA0
        NOP : DBLP subl
        NOP : MOV2 AA0, E(@EB+EO++1H)
        MOV3 #00800101, AA0
        NOP : MOV2 AA0, E(@EB+EO++1H)
        NOP : MOV2 AB1, E(@EB+EO++1H)   ; tpa
        LSR AB4, #0EH, AA1 : NOP        ; obc >> 14
        MOV3 #0FFFF, AA2
        AND AB4, AA2, AA2 : NOP         ; n = obc & 0xffff
        CMP AB0, 0 : NOP
        NOP : DBAZ subnf
        NOP : NOP
        ADD AB2, AA1, AB2 : NOP         ; tha += obc >> 14
        NOP : MOV2 FI, AA3
        NOP : DJMP subw
        ADD AA3, AA2, AA3 : NOP         ; p2 + n
subnf:  NOP : MOV2 FI, AA3              ; p2
subw:   NOP : MOV2 AB2, E(@EB+EO++1H)   ; tha
        NOP : MOV2 AB3, E(@EB+EO++1H)   ; oba
        NOP : MOV2 AA2, E(@EB+EO++1H)   ; n
        NOP : MOV2 AA3, FO0
        NOP : MOV2 FI, AA0              ; p
        ADD AA0, AA2, AA0 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA0, FO0             ; p + n

; ---- atan2 ----------------------------------------------------------------------------------------
; cpres1's algorithm, as in the 2A kernel: t = min/max of |x|,|y|, reduced by 30 degrees above
; tan 15, a rational polynomial, then the octant and quadrant. The angle is
; floor(rad * 32768/pi) & 0xffff. The constants are cpres1's. State: AB0 x, AB1 y, AB2 |x|,
; AB3 |y|, AB4 swapped, AB5 t (then the angle), AB6/AB7 the octant bases.

atan2:  NOP : MOV2 FI, AA0              ; 0x2f (x1, x2, z1, z2) -> atan2(z2 - z1, x2 - x1)
        NOP : MOV2 FI, AA1
        FSUB AA0, AA1, AB0 : NOP        ; x = x2 - x1
        NOP : MOV2 FI, AA0
        NOP : MOV2 FI, AA1
        FSUB AA0, AA1, AB1 : NOP        ; y = z2 - z1
        FABS AB0, AB2 : NOP
        FABS AB1, AB3 : NOP
        FADD AB2, AB3, AA0 : NOP        ; |x| + |y|
        NOP : DBAZ atzero               ; (0, 0) -> 0, as cpres1
        NOP : NOP
        FCMP AB2, AB3 : NOP             ; |y| - |x|
        NOP : DBAN atnosw
        NOP : NOP
        NOP : DBAZ atnosw
        NOP : NOP
        NOP : MOV2 AB2, AA0             ; |y| > |x|: t = |x| / |y|, the angle is pi/2 - atan(t)
        NOP : MOV2 AB3, AA1
        NOP : DJMP atdiv
        MOV3 #1, AB4
atnosw: NOP : MOV2 AB3, AA0             ; t = |y| / |x|
        NOP : MOV2 AB2, AA1
        MOV3 #0, AB4
atdiv:  NOP : DCALL div
        NOP : NOP
        NOP : MOV2 AA0, AB5             ; t
        MOV3 #0, AB6                    ; octant base: 0 (pi/6 once t is reduced)
        MOV3 #3FC90FDA, AB7             ; HALFPI when swapped (pi/3 once t is reduced)
        MOV3 #3E8930A2, AA1             ; TAN15
        FCMP AA1, AA0 : NOP             ; t - tan 15
        NOP : DBAN atpoly               ; t < tan 15: no reduction
        NOP : NOP
        MOV3 #3FDDB3D7, MA0             ; SQRT3
        NOP : MOV2 AB5, MA1
        FMUL MA0, MA1, AA0 : NOP        ; sqrt3 * t
        FADD AA0, -1.0E+0, AA0 : NOP    ; sqrt3*t - 1
        MOV3 #3FDDB3D7, AA2
        FADD AA2, AB5, AA1 : NOP        ; sqrt3 + t
        NOP : DCALL div
        NOP : NOP
        NOP : MOV2 AA0, AB5             ; t = (sqrt3*t - 1) / (sqrt3 + t)
        MOV3 #3F060A91, AB6             ; PI6
        MOV3 #3F860A91, AB7             ; PI3
atpoly: NOP : MOV2 AB5, MA0             ; atan(t) = t + t * g*(P0*g + P1) / ((g + Q0)*g + Q1), g = t*t
        FMUL MA0, MA0, MA1 : NOP        ; g
        MOV3 #0BF3853AD, MA2            ; P0
        FMUL MA2, MA1, AA0 : NOP        ; P0 * g
        MOV3 #0BFB854A7, AA1            ; P1
        FADD AA0, AA1, MA2 : NOP        ; P0*g + P1
        FMUL MA2, MA1, AA0 : NOP        ; (P0*g + P1) * g
        NOP : MOV2 AA0, MB0
        MOV3 #4098123B, AA1             ; Q0
        NOP : MOV2 MA1, AA0
        FADD AA0, AA1, MA2 : NOP        ; g + Q0
        FMUL MA2, MA1, AA0 : NOP        ; (g + Q0) * g
        MOV3 #408A3F7D, AA1             ; Q1
        FADD AA0, AA1, AA1 : NOP        ; + Q1
        NOP : DCALL div
        NOP : MOV2 MB0, AA0
        NOP : MOV2 AA0, MA0
        NOP : MOV2 AB5, MA1
        FMUL MA0, MA1, AA0 : NOP
        FADD AA0, AB5, AB5 : NOP        ; atan(t)
        CMP AB4, 0 : NOP
        NOP : DBAZ atns
        NOP : NOP
        NOP : DJMP atq
        FSUB AB5, AB7, AB5 : NOP        ; swapped: base - atan(t)
atns:   FADD AB5, AB6, AB5 : NOP        ; base + atan(t)
atq:    FADD AB0, 0.0E+0, AA0 : NOP     ; flags of x
        NOP : DBNAN atpos
        NOP : NOP
        MOV3 #40490FDA, AA1             ; PI
        FSUB AB5, AA1, AB5 : NOP        ; x < 0: pi - angle
atpos:  FADD AB1, 0.0E+0, AA0 : NOP     ; flags of y
        NOP : DBNAN atout
        NOP : NOP
        FSUB AB5, 0.0E+0, AB5 : NOP     ; y < 0: -angle
atout:  NOP : MOV2 AB5, MA0
        MOV3 #4622F983, MA1             ; ANGK = 32768/pi
        FMUL MA0, MA1, AA0 : NOP
        NOP : DCALL floor
        NOP : NOP
        MOV3 #0FFFF, AA2
        AND AA1, AA2, AA1 : NOP
        NOP : DJMP loop
        NOP : MOV2 AA1, FO0
atzero: NOP : DJMP loop
        MOV3 #0, FO0

; ---- subroutines (DCALL; they return with DRET) ----------------------------------------------------

; AA0 = (second - first)^2 of two FIFO words.
sqdiff: NOP : MOV2 FI, AA0
        NOP : MOV2 FI, AA1
        FSUB AA0, AA1, MA0 : NOP
        NOP : DRET
        FMUL MA0, MA0, AA0 : NOP

; AA0 = AA0 / AA1: reciprocal seed and two Newton steps (Dynamite Cop's sequence).
; Uses MA0-MA2.
div:    FRCP AA1, MA2 : MOV2 AA1, MA0   ; MA2 = r0, MA0 = d
        FMUL MA0, MA2, AA1 : MOV2 AA0, MA1      ; AA1 = d*r0, MA1 = n
        FSUB AA1, 2.0E+0, MA0 : FMUL MA2, MA1, MA2 : MOV1 AA1, MA1  ; MA0 = 2 - d*r0, MA2 = n*r0, MA1 = d*r0
        FMUL MA0, MA1, AA1 : NOP        ; AA1 = d*r1
        FSUB AA1, 2.0E+0, MA0 : FMUL MA2, MA0, MA1 : DRET   ; MA0 = 2 - d*r1, MA1 = n*r1
        FMUL MA0, MA1, AA0 : NOP        ; n * r2

; AA0 = sqrt(AA0), AA0 >= 0: x * (1/sqrt(x)), the seed refined twice by s*(3 - x*s*s)/2.
; Uses MA0-MA2.
sqrtd:  FCMP AA0, 0.0E+0 : NOP
        NOP : DBAZ sqz                  ; sqrt(0) = 0
        NOP : NOP
        FRSQ AA0, MA1 : MOV2 AA0, MA0   ; MA1 = s, MA0 = x
        FMUL MA1, MA1, MA2 : NOP
        FMUL MA0, MA2, AA0 : NOP        ; x*s*s
        FSUB AA0, 3.0E+0, MA1 : FMUL MA1, 0.5E+0, MA2 : NOP
        FMUL MA1, MA2, MA1 : NOP        ; s = s/2 * (3 - x*s*s)
        FMUL MA1, MA1, MA2 : NOP
        FMUL MA0, MA2, AA0 : NOP
        FSUB AA0, 3.0E+0, MA1 : FMUL MA1, 0.5E+0, MA2 : NOP
        FMUL MA1, MA2, MA1 : DRET
        FMUL MA0, MA1, AA0 : NOP        ; x * s
sqz:    NOP : DRET
        NOP : NOP

; AA1 = floor(AA0) as an integer: truncate, then step down if that went up.
floor:  CFI AA0, AA1 : NOP
        CIF AA1, AA2 : NOP
        FCMP AA2, AA0 : NOP             ; f - trunc(f)
        NOP : DBNAN flr1
        NOP : NOP
        ADD AA1, -1, AA1 : NOP
flr1:   NOP : DRET
        NOP : NOP

; AA6 = sin, AA7 = cos of the 16-bit angle in AA0 (copies in MB6, MB7 for the multiplier).
; The quadrant q = (a >> 14) & 3 and x = (a & 0x3fff) * 2pi/65536 in [0, pi/2): Taylor series,
; sin to x^11 and cos to x^12, then (sin, cos) of a = q0 (S, C), q1 (C, -S), q2 (-S, -C),
; q3 (-C, S). Uses AA0-AA3, MA0-MA2, AR3.
sincos: MOV3 #3FFF, AA1
        AND AA0, AA1, AA1 : NOP         ; r
        LSR AA0, #0EH, AA2 : NOP
        MOV3 #3, AA3
        AND AA2, AA3, AA2 : NOP         ; q
        CIF AA1, MA0 : MOV2 AA2, AR3
        MOV3 #f:9.587379924285257e-05, MA1   ; 2pi / 65536
        FMUL MA0, MA1, MA0 : NOP        ; x
        FMUL MA0, MA0, MA1 : NOP        ; z = x*x
        MOV3 #f:-2.505210838544172e-08, MA2  ; -1/11!
        MOV3 #f:2.755731922398589e-06, AA3   ;  1/9!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:-1.984126984126984e-04, AA3  ; -1/7!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:8.333333333333333e-03, AA3   ;  1/5!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:-1.666666666666667e-01, AA3  ; -1/3!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, 1.0E+0, MA2 : NOP
        FMUL MA2, MA0, AA6 : NOP        ; S = x * (1 - z/3! + ...)
        MOV3 #f:2.08767569878681e-09, MA2    ;  1/12!
        MOV3 #f:-2.755731922398589e-07, AA3  ; -1/10!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:2.48015873015873e-05, AA3    ;  1/8!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:-1.388888888888889e-03, AA3  ; -1/6!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:4.166666666666666e-02, AA3   ;  1/4!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        MOV3 #f:-0.5, AA3                    ; -1/2!
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, AA3, MA2 : NOP
        FMUL MA2, MA1, AA0 : NOP
        FADD AA0, 1.0E+0, AA7 : NOP     ; C = 1 - z/2! + ...
        NOP : DBBC AR3:0, scq2          ; q bit 0: (S, C) -> (C, -S)
        NOP : NOP
        FSUB AA6, 0.0E+0, AA7 : MOV2 AA7, AA6
scq2:   NOP : DBBC AR3:1, scout         ; q bit 1: negate both
        NOP : NOP
        FSUB AA6, 0.0E+0, AA6 : NOP
        FSUB AA7, 0.0E+0, AA7 : NOP
scout:  NOP : MOV2 AA6, MB6
        NOP : DRET
        NOP : MOV2 AA7, MB7

; rotate two columns by (s, c) = (MB6, MB7): for each row, u' = c*u + s*v and v' = c*v - s*u,
; u at A(@AR4), v at A(@AR5). Uses AA0-AA3, MA0, MA1.
rot:    NOP : SETL #3
rotl:   NOP : MOV2 A(@AR4), MA0
        NOP : MOV2 A(@AR5), MA1
        FMUL MA0, MB7, AA0 : NOP        ; c*u
        FMUL MA1, MB6, AA1 : NOP        ; s*v
        FMUL MA1, MB7, AA2 : NOP        ; c*v
        FMUL MA0, MB6, AA3 : NOP        ; s*u
        FADD AA0, AA1, AA0 : NOP
        FSUB AA3, AA2, AA2 : NOP
        NOP : MOV2 AA0, A(@AR4++)
        NOP : DBLP rotl
        NOP : MOV2 AA2, A(@AR5++)
        NOP : DRET
        NOP : NOP

; ---- the COP VM: every other cpres1 command ---------------------------------------------------
; As in the 2A kernel: the commands without code above run as C (m2-hle2's cpres1 model,
; src/cop_ext/) compiled for RV32IF and translated by tools/rv2cvm.py into one bytecode word per
; RISC-V instruction:
;     bit 31 = 0 | op 30:23 | A 22:18 | B 17:13 | C 12:0 (signed; or rs2 in its low 5 bits)
; The i960 uploads the program into buffer RAM words 0x4000..0x7D7F (m2_cop_ext_upload, m2_3d.h). A VM
; address a is buffer RAM word (a >> 2) & 0x7fff, read and written through EB + EO; EO is the
; VM's pc while it runs (the fetch steps it). The registers are A RAM: x0..x31 at 0x300, f0..f31
; at 0x320. The program reads its arguments with IN and sends its results with OUT, so the FIFO
; protocol is cpres1's. Natives: IN OUT, SIN COS (sincos), IRD IWR (A RAM 0xA0 + the address:
; the matrix, which the C knows as 0x60..0x6b), PUSH POP (pushs, pops), RSQRT, HALT.
;
; While the VM runs: AB7 = the instruction word, AR1 = its op's VTAB entry, AR2 = A, AR0 = B,
; AR7 = C & 31; AB6 = 0x1f, AB5 = VTAB, AB4 = 0x7fff, AB2 = 0x10000000 (return addresses), AB3 =
; the pc while a load or store has EO. Handlers leave through wx (x[A] = AA0), wf (f[A] = AA0)
; or vmnext. The subroutines the VM calls (sincos, div, sqrtd, pushs, pops) keep off AB, AR0-2, AR7.

.equ VTAB, 3C0

vm:     MOV3 #4000, EO                  ; the mark in buffer RAM word 0x4000 (image word 0, rv2cvm.py):
        NOP : MOV2 E(@EB+EO++0H), AA2   ; is the program there (M2_COP_EXT)?
        MOV3 #4D32564D, AA1             ; "M2VM"
        XOR AA2, AA1, AA2 : NOP
        NOP : DBNAZ loop                ; no: ignore the command, as cpres1's unknown ones
        NOP : NOP
        NOP : MOV2 AA0, A(@AR6+30AH)    ; a0 = the command number (AA0, from loop)
        MOV3 #0, A(@AR6+300H)           ; x0
        MOV3 #1001F600, A(@AR6+302H)    ; sp
        MOV3 #4001, EO                  ; pc = _start (src/cop_ext/vm.ld)
        MOV3 #1F, AB6
        MOV3 #VTAB, AB5
        MOV3 #7FFF, AB4
        NOP : DJMP vmnext
        MOV3 #10000000, AB2

wf:     NOP : DJMP vmnext               ; f[A] = AA0
        NOP : MOV2 AA0, A(@AR2+320H)
wx:     NOP : MOV2 AA0, A(@AR2+300H)    ; x[A] = AA0, then x0 = 0 again
        MOV3 #0, A(@AR6+300H)
vmnext: NOP : MOV2 E(@EB+EO++1H), AB7   ; fetch, pc + 1
        LSR AB7, #17H, AA4 : NOP        ; op
        ADD AA4, AB5, AA4 : NOP
        LSR AB7, #12H, AA5 : MOV2 AA4, AR1
        AND AA5, AB6, AA5 : NOP         ; A
        LSR AB7, #0DH, AA4 : MOV2 AA5, AR2
        AND AA4, AB6, AA4 : NOP         ; B
        AND AB7, AB6, AA5 : MOV2 AA4, AR0
        NOP : DJMP A(AR1)
        NOP : MOV2 AA5, AR7             ; C & 31

; EO = the buffer RAM word of x[B] + C, AA0 = the address; AB3 = the pc. Uses AA1.
vea:    LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : NOP
        ADD AA0, AA1, AA0 : NOP
        LSR AA0, #2, AA1 : MOV2 EO, AB3
        AND AA1, AB4, AA1 : DRET
        NOP : MOV2 AA1, EO

; AA0 = (AA0 < AA1) as 0 or 1, signed (slts) or unsigned (sltus); the flags are set from it.
sltus:  XOR AA0, AA1, AA2 : NOP
        NOP : DBNAN sltsd               ; the same top bit: the sign of the difference
        NOP : NOP
        NOP : DRET                      ; top bits differ: AA0 < AA1 when AA1 has it
        LSR AA1, #1FH, AA0 : NOP
slts:   XOR AA0, AA1, AA2 : NOP
        NOP : DBAN slts1                ; signs differ: AA0 < AA1 when AA0 is the negative one
        NOP : NOP
sltsd:  SUB AA1, AA0, AA0 : NOP         ; AA0 - AA1, which can't overflow here
slts1:  NOP : DRET
        LSR AA0, #1FH, AA0 : NOP

vLI:    MOV3 #7FFF, AA1                 ; x[A] = the pool word
        AND AB7, AA1, AA1 : MOV2 EO, AB3
        NOP : MOV2 AA1, EO
        NOP : MOV2 E(@EB+EO++0H), AA0
        NOP : DJMP wx
        NOP : MOV2 AB3, EO

vADD:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        ADD AA0, AA1, AA0 : NOP
vSUB:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        SUB AA1, AA0, AA0 : NOP
vAND:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        AND AA0, AA1, AA0 : NOP
vOR:    NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        OR AA0, AA1, AA0 : NOP
vXOR:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        XOR AA0, AA1, AA0 : NOP
vSLT:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL slts
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        NOP : NOP
vSLTU:  NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL sltus
        NOP : MOV2 A(@AR7+300H), AA1
        NOP : DJMP wx
        NOP : NOP

; the shifts: by the amount's bits 0-4 (AR5) one at a time, the shifts take an immediate only
vSLL:   NOP : MOV2 A(@AR7+300H), AR5
        NOP : DJMP shl
        NOP : MOV2 A(@AR0+300H), AA0
vSLLI:  NOP : MOV2 AR7, AR5
        NOP : DJMP shl
        NOP : MOV2 A(@AR0+300H), AA0
vSRL:   NOP : MOV2 A(@AR7+300H), AR5
        NOP : DJMP shr
        NOP : MOV2 A(@AR0+300H), AA0
vSRLI:  NOP : MOV2 AR7, AR5
        NOP : DJMP shr
        NOP : MOV2 A(@AR0+300H), AA0
vSRA:   NOP : MOV2 A(@AR7+300H), AR5
        NOP : DJMP sha
        NOP : MOV2 A(@AR0+300H), AA0
vSRAI:  NOP : MOV2 AR7, AR5
        NOP : DJMP sha
        NOP : MOV2 A(@AR0+300H), AA0

shl:    NOP : DBBC AR5:0, shl1
        NOP : NOP
        LSL AA0, #1, AA0 : NOP
shl1:   NOP : DBBC AR5:1, shl2
        NOP : NOP
        LSL AA0, #2, AA0 : NOP
shl2:   NOP : DBBC AR5:2, shl3
        NOP : NOP
        LSL AA0, #4, AA0 : NOP
shl3:   NOP : DBBC AR5:3, shl4
        NOP : NOP
        LSL AA0, #8, AA0 : NOP
shl4:   NOP : DBBC AR5:4, wx
        NOP : NOP
        NOP : DJMP wx
        LSL AA0, #10H, AA0 : NOP

shr:    NOP : DBBC AR5:0, shr1
        NOP : NOP
        LSR AA0, #1, AA0 : NOP
shr1:   NOP : DBBC AR5:1, shr2
        NOP : NOP
        LSR AA0, #2, AA0 : NOP
shr2:   NOP : DBBC AR5:2, shr3
        NOP : NOP
        LSR AA0, #4, AA0 : NOP
shr3:   NOP : DBBC AR5:3, shr4
        NOP : NOP
        LSR AA0, #8, AA0 : NOP
shr4:   NOP : DBBC AR5:4, wx
        NOP : NOP
        NOP : DJMP wx
        LSR AA0, #10H, AA0 : NOP

sha:    NOP : DBBC AR5:0, sha1
        NOP : NOP
        ASR AA0, #1, AA0 : NOP
sha1:   NOP : DBBC AR5:1, sha2
        NOP : NOP
        ASR AA0, #2, AA0 : NOP
sha2:   NOP : DBBC AR5:2, sha3
        NOP : NOP
        ASR AA0, #4, AA0 : NOP
sha3:   NOP : DBBC AR5:3, sha4
        NOP : NOP
        ASR AA0, #8, AA0 : NOP
sha4:   NOP : DBBC AR5:4, wx
        NOP : NOP
        NOP : DJMP wx
        ASR AA0, #10H, AA0 : NOP

; immediates: AA1 = C, sign-extended from bit 12
vADDI:  LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DJMP wx
        ADD AA0, AA1, AA0 : NOP
vANDI:  LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DJMP wx
        AND AA0, AA1, AA0 : NOP
vORI:   LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DJMP wx
        OR AA0, AA1, AA0 : NOP
vXORI:  LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DJMP wx
        XOR AA0, AA1, AA0 : NOP
vSLTI:  LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DCALL slts
        NOP : NOP
        NOP : DJMP wx
        NOP : NOP
vSLTIU: LSL AB7, #13H, AA1 : MOV2 A(@AR0+300H), AA0
        ASR AA1, #13H, AA1 : DCALL sltus
        NOP : NOP
        NOP : DJMP wx
        NOP : NOP

; loads and stores: the word through EO (vea), then the pc back
vLW:    NOP : DCALL vea
        NOP : NOP
        NOP : MOV2 E(@EB+EO++0H), AA0
        NOP : DJMP wx
        NOP : MOV2 AB3, EO
vFLW:   NOP : DCALL vea
        NOP : NOP
        NOP : MOV2 E(@EB+EO++0H), AA0
        NOP : DJMP wf
        NOP : MOV2 AB3, EO
vSW:    NOP : DCALL vea
        NOP : NOP
        NOP : MOV2 A(@AR2+300H), AA0
        NOP : MOV2 AA0, E(@EB+EO++0H)
        NOP : DJMP vmnext
        NOP : MOV2 AB3, EO
vFSW:   NOP : DCALL vea
        NOP : NOP
        NOP : MOV2 A(@AR2+320H), AA0
        NOP : MOV2 AA0, E(@EB+EO++0H)
        NOP : DJMP vmnext
        NOP : MOV2 AB3, EO
vLBU:   NOP : DCALL vea                 ; x[A] = byte (addr & 3) of the word
        NOP : NOP
        MOV3 #3, AA2
        AND AA0, AA2, AA2 : MOV2 E(@EB+EO++0H), AA0
        NOP : MOV2 AA2, AR5
        NOP : DBBC AR5:0, lbu1
        NOP : MOV2 AB3, EO
        LSR AA0, #8, AA0 : NOP
lbu1:   NOP : DBBC AR5:1, lbu2
        NOP : NOP
        LSR AA0, #10H, AA0 : NOP
lbu2:   MOV3 #0FF, AA1
        NOP : DJMP wx
        AND AA0, AA1, AA0 : NOP
vSB:    NOP : DCALL vea                 ; byte (addr & 3) of the word = x[A] & 0xff
        NOP : NOP
        MOV3 #3, AA2
        AND AA0, AA2, AA2 : MOV2 A(@AR2+300H), AA0
        MOV3 #0FF, AA3
        AND AA0, AA3, AA0 : MOV2 AA2, AR5
        NOP : DBBC AR5:0, sb1
        NOP : NOP
        LSL AA0, #8, AA0 : NOP
        LSL AA3, #8, AA3 : NOP
sb1:    NOP : DBBC AR5:1, sb2
        NOP : NOP
        LSL AA0, #10H, AA0 : NOP
        LSL AA3, #10H, AA3 : NOP
sb2:    NOT AA3, AA3 : MOV2 E(@EB+EO++0H), AA1
        AND AA1, AA3, AA1 : NOP
        OR AA1, AA0, AA1 : NOP
        NOP : MOV2 AA1, E(@EB+EO++0H)
        NOP : DJMP vmnext
        NOP : MOV2 AB3, EO

; branches: x[B] against x[A]; taken, pc = the branch + C
vBEQ:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR2+300H), AA1
        CMP AA0, AA1 : DJMP bz
        NOP : NOP
vBNE:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : MOV2 A(@AR2+300H), AA1
        CMP AA0, AA1 : DJMP bnz
        NOP : NOP
vBLT:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL slts
        NOP : MOV2 A(@AR2+300H), AA1
        NOP : DJMP bnz
        NOP : NOP
vBGE:   NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL slts
        NOP : MOV2 A(@AR2+300H), AA1
        NOP : DJMP bz
        NOP : NOP
vBLTU:  NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL sltus
        NOP : MOV2 A(@AR2+300H), AA1
        NOP : DJMP bnz
        NOP : NOP
vBGEU:  NOP : MOV2 A(@AR0+300H), AA0
        NOP : DCALL sltus
        NOP : MOV2 A(@AR2+300H), AA1
        NOP : DJMP bz
        NOP : NOP
bz:     NOP : DBAZ take                 ; the flags are the compare's
        NOP : NOP
        NOP : DJMP vmnext
        NOP : NOP
bnz:    NOP : DBNAZ take
        NOP : NOP
        NOP : DJMP vmnext
        NOP : NOP
take:   LSL AB7, #13H, AA1 : MOV2 EO, AA0
        ASR AA1, #13H, AA1 : NOP
        ADD AA0, AA1, AA0 : NOP
        ADD AA0, -1, AA0 : DJMP vmnext
        NOP : MOV2 AA0, EO

vJAL:   LSL AB7, #0EH, AA1 : MOV2 EO, AA0       ; x[A] = the return address, pc = the jump +
        ASR AA1, #0EH, AA1 : NOP                ; bits 17:0 (J: A = 0)
        ADD AA0, AA1, AA1 : NOP
        ADD AA1, -1, AA1 : NOP
        LSL AA0, #2, AA0 : MOV2 AA1, EO
        NOP : DJMP wx
        OR AA0, AB2, AA0 : NOP
vJALR:  NOP : DCALL vea                 ; pc = word of x[B] + C, x[A] = the return address (JR: A = 0)
        NOP : NOP
        NOP : MOV2 AB3, AA0
        LSL AA0, #2, AA0 : NOP
        NOP : DJMP wx
        OR AA0, AB2, AA0 : NOP

; floats
vFADD:  NOP : MOV2 A(@AR0+320H), AA0
        NOP : MOV2 A(@AR7+320H), AA1
        NOP : DJMP wf
        FADD AA0, AA1, AA0 : NOP
vFSUB:  NOP : MOV2 A(@AR0+320H), AA0
        NOP : MOV2 A(@AR7+320H), AA1
        NOP : DJMP wf
        FSUB AA1, AA0, AA0 : NOP
vFMUL:  NOP : MOV2 A(@AR0+320H), MA0
        NOP : MOV2 A(@AR7+320H), MA1
        NOP : DJMP wf
        FMUL MA0, MA1, AA0 : NOP
vFDIV:  NOP : MOV2 A(@AR0+320H), AA0    ; div, then the residual's quotient added (as the 2A divp)
        NOP : MOV2 A(@AR7+320H), AA1
        NOP : MOV2 AA0, AB0             ; n
        NOP : DCALL div
        NOP : MOV2 AA1, AB1             ; d
        NOP : MOV2 AA0, MB2             ; q0
        NOP : MOV2 AB1, MB1
        FMUL MB1, MB2, AA2 : MOV2 AA0, AA4
        FSUB AA2, AB0, AA0 : NOP        ; n - d*q0
        NOP : DCALL div
        NOP : MOV2 AB1, AA1
        NOP : DJMP wf
        FADD AA0, AA4, AA0 : NOP
vFSQRT: NOP : MOV2 A(@AR0+320H), AA0    ; sqrt(+-0) = +-0, sqrt(x < 0) = NaN as on cpres1
        ABS AA0, AA1 : NOP
        NOP : DBAZ wf
        NOP : NOP
        ADD AA0, 0, AA1 : NOP
        NOP : DBAN fnan
        NOP : NOP
        NOP : DCALL sqrtd
        NOP : NOP
        NOP : DJMP wf
        NOP : NOP
fnan:   NOP : DJMP wf
        MOV3 #0FFC00000, AA0
vFMOV:  NOP : DJMP wf
        NOP : MOV2 A(@AR0+320H), AA0
vFNEG:  NOP : MOV2 A(@AR0+320H), AA0
        MOV3 #80000000, AA1
        NOP : DJMP wf
        XOR AA0, AA1, AA0 : NOP
vFABS:  NOP : MOV2 A(@AR0+320H), AA0
        NOP : DJMP wf
        ABS AA0, AA0 : NOP
vFLT:   NOP : MOV2 A(@AR0+320H), AA0    ; FCMP: the flags of f[B] - f[C]
        NOP : MOV2 A(@AR7+320H), AA1
        FCMP AA1, AA0 : NOP
        MOV3 #1, AA0
        NOP : DBAN wx
        NOP : NOP
        NOP : DJMP wx
        MOV3 #0, AA0
vFLE:   NOP : MOV2 A(@AR0+320H), AA0
        NOP : MOV2 A(@AR7+320H), AA1
        FCMP AA1, AA0 : NOP
        MOV3 #1, AA0
        NOP : DBAN wx
        NOP : NOP
        NOP : DBAZ wx
        NOP : NOP
        NOP : DJMP wx
        MOV3 #0, AA0
vFEQ:   NOP : MOV2 A(@AR0+320H), AA0
        NOP : MOV2 A(@AR7+320H), AA1
        FCMP AA1, AA0 : NOP
        MOV3 #1, AA0
        NOP : DBAZ wx
        NOP : NOP
        NOP : DJMP wx
        MOV3 #0, AA0
vFCVTWS: NOP : MOV2 A(@AR0+320H), AA0   ; truncates
        NOP : DJMP wx
        CFI AA0, AA0 : NOP
vFCVTSW: NOP : MOV2 A(@AR0+300H), AA0
        NOP : DJMP wf
        CIF AA0, AA0 : NOP
vFMAX:  NOP : MOV2 A(@AR0+320H), AA0    ; f[B] unless f[C] is greater
        NOP : MOV2 A(@AR7+320H), AA1
        FCMP AA1, AA0 : NOP
        NOP : DBNAN wf
        NOP : NOP
        NOP : DJMP wf
        NOP : MOV2 AA1, AA0
vFMVXW: NOP : DJMP wx
        NOP : MOV2 A(@AR0+320H), AA0
vFMVWX: NOP : DJMP wf
        NOP : MOV2 A(@AR0+300H), AA0

; natives
vIN:    NOP : DJMP wx
        NOP : MOV2 FI, AA0
vOUT:   NOP : DJMP vmnext
        NOP : MOV2 A(@AR0+300H), FO0
vSIN:   NOP : DCALL sincos
        NOP : MOV2 A(@AR0+300H), AA0
        NOP : DJMP wx
        NOP : MOV2 AA6, AA0
vCOS:   NOP : DCALL sincos
        NOP : MOV2 A(@AR0+300H), AA0
        NOP : DJMP wx
        NOP : MOV2 AA7, AA0
vIRD:   NOP : MOV2 A(@AR0+300H), AR5    ; x[A] = the matrix word x[B] - 0x60
        NOP : MOV2 A(@AR5+0A0H), AA0
        NOP : DJMP wx
        NOP : NOP
vIWR:   NOP : MOV2 A(@AR0+300H), AR5    ; the matrix word x[B] - 0x60 = x[C]
        NOP : MOV2 A(@AR7+300H), AA0
        NOP : DJMP vmnext
        NOP : MOV2 AA0, A(@AR5+0A0H)
vPUSH:  NOP : DCALL pushs
        NOP : NOP
        NOP : DJMP vmnext
        NOP : NOP
vPOP:   NOP : DCALL pops
        NOP : NOP
        NOP : DJMP vmnext
        NOP : NOP
vRSQRT: NOP : MOV2 A(@AR0+300H), AA0    ; 1/sqrt(x[B]); 0 for 0, NaN for x < 0 (cpres1 _L2029B)
        ADD AA0, 0, AA1 : NOP
        NOP : DBAZ wx
        NOP : NOP
        NOP : DBAN xnan
        NOP : NOP
        FRSQ AA0, MA1 : MOV2 AA0, MA0   ; sqrtd's seed and steps, without its final multiply
        FMUL MA1, MA1, MA2 : NOP
        FMUL MA0, MA2, AA0 : NOP
        FSUB AA0, 3.0E+0, MA1 : FMUL MA1, 0.5E+0, MA2 : NOP
        FMUL MA1, MA2, MA1 : NOP
        FMUL MA1, MA1, MA2 : NOP
        FMUL MA0, MA2, AA0 : NOP
        FSUB AA0, 3.0E+0, MA1 : FMUL MA1, 0.5E+0, MA2 : NOP
        NOP : DJMP wx
        FMUL MA1, MA2, AA0 : NOP
xnan:   NOP : DJMP wx                   ; 0xffc00000, the NaN the SHARC and cop_ext give
        MOV3 #0FFC00000, AA0
vHALT:  NOP : DJMP loop
        NOP : NOP
