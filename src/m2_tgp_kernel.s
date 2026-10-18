; m2_tgp_kernel.s - the SDK's own Model 2A coprocessor program (Fujitsu MB86234 "TGP").
;
; Model 2B runs Sonic the Fighters' cpres1 SHARC program as its COP. The 2A board has a TGP
; instead, so this program gives it the same FIFO protocol for the math commands: the i960
; writes a command word (COP_CMD(n) = n<<23 | n<<8 | n, m2_constants.h) and its arguments to
; 0x884000 and reads the results back from the same address. The command number is bits
; 30:23 of the word, which is also what the 2A function port (0x880000 + n*4) produces, so
; both ways of sending a command work.
;
; Semantics follow STF's cpres1 as recorded in m2-hle (src/board/sharc_exec.h, checked
; against MAME): same argument order, same results. That covers the math commands and the
; matrix ones m2_obj_submit sends (IDENTITY, SET_POS, ANG_X/Y/Z, SCALE) plus RMATRIX to read
; the matrix back, and ATAN2 (the azimuth m2_obj_frame_setup asks for). There is one matrix
; (no PUSH/POP stack), and no SUBMIT yet.
; Commands not listed in the table are ignored (their arguments would then be read as
; commands: don't send them on 2A).
;
; Hardware used (MAME src/mame/sega/model2.cpp, copro_tgp_*): rf1 = input FIFO (a read
; waits for data), rf2 = output FIFO, io 0x20/0x21 = sin/cos of a 16-bit angle, io
; 0x28/0x29 = reciprocal seed, io 0x2a/0x2b = 1/sqrt seed. The divide and square-root
; sequences are the ones Dynamite Cop's 2A program uses with those tables.
;
; Assemble: python3 tools/tgpasm.py src/m2_tgp_kernel.s --header src/m2_tgp_kernel.h --name m2_tgp_kernel

.equ T0, 0x70           ; scratch data RAM
.equ T1, 0x71
.equ SN, 0x72           ; sin / cos of the angle a rotation command is working with
.equ CS, 0x73
.equ R0, 0x74
.equ DN, 0x72           ; divp's denominator, numerator and first quotient (SN/CS/R0 are free then)
.equ NM, 0x73
.equ Q,  0x74
.equ XV, 0x75           ; atan2: x, y, |x|, |y|, swapped flag, t, octant bases, t*t, numerator
.equ YV, 0x76
.equ AX, 0x77
.equ AY, 0x78
.equ SW, 0x79
.equ TT, 0x7a
.equ BS, 0x7b
.equ BW, 0x7c
.equ G,  0x7d
.equ NU, 0x7e

; The current matrix, laid out as cpres1 keeps its bone slot (m2-hle sharc_exec.h, Fn_get_matrix):
; three rotation columns, then the translation T. A point p maps to col0*p.x + col1*p.y + col2*p.z + T.
.equ C0X, 0x60
.equ C0Y, 0x61
.equ C0Z, 0x62
.equ C1X, 0x63
.equ C1Y, 0x64
.equ C1Z, 0x65
.equ C2X, 0x66
.equ C2Y, 0x67
.equ C2Z, 0x68
.equ TX,  0x69
.equ TY,  0x6a
.equ TZ,  0x6b

        brif alw #start         ; 0: reset
        brif alw #irq           ; 1-5: interrupt vectors (model2.cpp raises none)
        brif alw #irq
        brif alw #irq
        brif alw #irq
        brif alw #irq
irq:    iret

start:  set #0x0800             ; same start-up state as Dynamite Cop's program
        ldi #0x2, mask
        stmh fp rz              ; float mode, round toward zero (as Dynamite Cop runs)
        brif alw #ident         ; start with the identity matrix

loop:   mov rf1, b              ; command word
        mov bh, d               ; bits 30:23 = command number
        lia #table
        addd
        brul alw d

; ---- scalar ---------------------------------------------------------------------------------

fadd:   mov rf1, d              ; 0x13 a + b
        mov rf1, a
        fadd
        mov d, rf2
        brif alw #loop

fsub:   mov rf1, d              ; 0x14 a - b
        mov rf1, a
        fsbd
        mov d, rf2
        brif alw #loop

fmul:   mov rf1, a              ; 0x15 a * b
        mov rf1, b
        fml
        mov p, rf2
        brif alw #loop

fdiv:   mov rf1, $T0            ; 0x16 a / b
        mov rf1, a
        bsif alw #div
        mov p, rf2
        brif alw #loop

int2f:  mov rf1, d              ; 0x17 (float)i
        cxfd
        mov d, rf2
        brif alw #loop

f2int:  mov rf1, d              ; 0x18 floor(f): cpres1 rounds down (-3.7 -> -4, 5.9 -> 5)
        stmh fp rm
        cfxd
        stmh fp rz
        mov d, rf2
        brif alw #loop

sqrt:   mov rf1, d              ; 0x1a sqrt(a)
        bsif alw #sqrtd
        mov p, rf2
        brif alw #loop

; ---- trig (16-bit angle, 0x10000 = 360 degrees) --------------------------------------------

sin:    mov rf1, $0x20 (e)      ; 0x21 sin(ang)
        mov $0x20 (e), rf2
        brif alw #loop

cos:    mov rf1, $0x20 (e)      ; 0x22 cos(ang)
        mov $0x21 (e), rf2
        brif alw #loop

tan:    mov rf1, $0x20 (e)      ; 0x23 sin(ang) / cos(ang)
        mov $0x20 (e), $T0
        mov $0x21 (e), a
        bsif alw #div
        mov p, rf2
        brif alw #loop

sinsc:  mov rf1, $0x20 (e)      ; 0x24 sin(ang) * s
        mov $0x20 (e), a
        mov rf1, b
        fml
        mov p, rf2
        brif alw #loop

cossc:  mov rf1, $0x20 (e)      ; 0x25 cos(ang) * s
        mov $0x21 (e), a
        mov rf1, b
        fml
        mov p, rf2
        brif alw #loop

; ---- distances / products -------------------------------------------------------------------

dist2d: bsif alw #sqdiff        ; 0x2b (x1, x2, z1, z2) -> sqrt((x2-x1)^2 + (z2-z1)^2)
        mov p, $T1
        bsif alw #sqdiff
        mov $T1, d
        fsmd
        bsif alw #sqrtd
        mov p, rf2
        brif alw #loop

dist3d: bsif alw #sqdiff        ; 0x2c (x1, x2, y1, y2, z1, z2) -> 3D distance
        mov p, $T1
        bsif alw #sqdiff
        mov $T1, d
        fsmd
        mov d, $T1
        bsif alw #sqdiff
        mov $T1, d
        fsmd
        bsif alw #sqrtd
        mov p, rf2
        brif alw #loop

mag2d:  mov rf1, a              ; 0x2d (a, b) -> sqrt(a*a + b*b)
        mov a, b
        fml
        mov p, $T1
        mov rf1, a
        mov a, b
        fml
        mov $T1, d
        fsmd
        bsif alw #sqrtd
        mov p, rf2
        brif alw #loop

dot2d:  mov rf1, a              ; 0x59 (a, b, c, d) -> a*b + c*d
        mov rf1, b
        fml
        mov p, $T1
        mov rf1, a
        mov rf1, b
        fml
        mov $T1, d
        fsmd
        mov d, rf2
        brif alw #loop

add3:   bsif alw #add1          ; 0x5c (a0, b0, a1, b1, a2, b2) -> (a0+b0, a1+b1, a2+b2)
        bsif alw #add1
        bsif alw #add1
        brif alw #loop
add1:   mov rf1, d
        mov rf1, a
        fadd
        mov d, rf2
        rtif alw

sub3:   bsif alw #sub1          ; 0x5d (a0, b0, a1, b1, a2, b2) -> (a0-b0, a1-b1, a2-b2)
        bsif alw #sub1
        bsif alw #sub1
        brif alw #loop
sub1:   mov rf1, d
        mov rf1, a
        fsbd
        mov d, rf2
        rtif alw

scale3: mov rf1, b              ; 0x5e (s, x, y, z) -> (s*x, s*y, s*z)
        mov rf1, a
        fml
        mov p, rf2
        mov rf1, a
        fml
        mov p, rf2
        mov rf1, a
        fml
        mov p, rf2
        brif alw #loop

; ---- the current matrix (what m2_obj_submit sends before polygon_submit) ----------------------
; Same results as cpres1 (m2-hle sharc_exec.h / sharc.h, checked against MAME): each angle
; command post-multiplies the rotation, so a rotation changes two columns; SET_POS adds one
; column at a time, in the order cpres1 rounds it.

ident:  ldi #1, d               ; 0x03 rot = I, T = 0
        cxfd
        mov d, $C0X
        mov d, $C1Y
        mov d, $C2Z
        ldi #0, d
        mov d, $C0Y
        mov d, $C0Z
        mov d, $C1X
        mov d, $C1Z
        mov d, $C2X
        mov d, $C2Y
        mov d, $TX
        mov d, $TY
        mov d, $TZ
        brif alw #loop

setpos: mov rf1, b              ; 0x06 (x, y, z): T += x*col0, then y*col1, then z*col2
        mov $C0X, a
        fml
        mov $TX, d
        fsmd
        mov d, $TX
        mov $C0Y, a
        fml
        mov $TY, d
        fsmd
        mov d, $TY
        mov $C0Z, a
        fml
        mov $TZ, d
        fsmd
        mov d, $TZ
        mov rf1, b
        mov $C1X, a
        fml
        mov $TX, d
        fsmd
        mov d, $TX
        mov $C1Y, a
        fml
        mov $TY, d
        fsmd
        mov d, $TY
        mov $C1Z, a
        fml
        mov $TZ, d
        fsmd
        mov d, $TZ
        mov rf1, b
        mov $C2X, a
        fml
        mov $TX, d
        fsmd
        mov d, $TX
        mov $C2Y, a
        fml
        mov $TY, d
        fsmd
        mov d, $TY
        mov $C2Z, a
        fml
        mov $TZ, d
        fsmd
        mov d, $TZ
        brif alw #loop

scale:  mov rf1, b              ; 0x07 (sx, sy, sz): col k *= s_k
        mov $C0X, a
        fml
        mov p, $C0X
        mov $C0Y, a
        fml
        mov p, $C0Y
        mov $C0Z, a
        fml
        mov p, $C0Z
        mov rf1, b
        mov $C1X, a
        fml
        mov p, $C1X
        mov $C1Y, a
        fml
        mov p, $C1Y
        mov $C1Z, a
        fml
        mov p, $C1Z
        mov rf1, b
        mov $C2X, a
        fml
        mov p, $C2X
        mov $C2Y, a
        fml
        mov p, $C2Y
        mov $C2Z, a
        fml
        mov p, $C2Z
        brif alw #loop

angx:   mov rf1, $0x20 (e)      ; 0x08 (ang): rot = rot * Rx; col2' = c*col2 + s*col1, col1' = c*col1 - s*col2
        mov $0x20 (e), $SN
        mov $0x21 (e), $CS
        mov $C2X, a             ; row 0: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C1X, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C1X, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C2X, a
        mov $SN, b
        fml
        fmrd
        mov d, $C1X
        mov $R0, d
        mov d, $C2X
        mov $C2Y, a             ; row 1: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C1Y, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C1Y, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C2Y, a
        mov $SN, b
        fml
        fmrd
        mov d, $C1Y
        mov $R0, d
        mov d, $C2Y
        mov $C2Z, a             ; row 2: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C1Z, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C1Z, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C2Z, a
        mov $SN, b
        fml
        fmrd
        mov d, $C1Z
        mov $R0, d
        mov d, $C2Z
        brif alw #loop

angy:   mov rf1, $0x20 (e)      ; 0x09 (ang): rot = rot * Ry; col0' = c*col0 + s*col2, col2' = c*col2 - s*col0
        mov $0x20 (e), $SN
        mov $0x21 (e), $CS
        mov $C0X, a             ; row 0: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C2X, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C2X, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C0X, a
        mov $SN, b
        fml
        fmrd
        mov d, $C2X
        mov $R0, d
        mov d, $C0X
        mov $C0Y, a             ; row 1: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C2Y, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C2Y, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C0Y, a
        mov $SN, b
        fml
        fmrd
        mov d, $C2Y
        mov $R0, d
        mov d, $C0Y
        mov $C0Z, a             ; row 2: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C2Z, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C2Z, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C0Z, a
        mov $SN, b
        fml
        fmrd
        mov d, $C2Z
        mov $R0, d
        mov d, $C0Z
        brif alw #loop

angz:   mov rf1, $0x20 (e)      ; 0x0a (ang): rot = rot * Rz; col1' = c*col1 + s*col0, col0' = c*col0 - s*col1
        mov $0x20 (e), $SN
        mov $0x21 (e), $CS
        mov $C1X, a             ; row 0: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C0X, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C0X, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C1X, a
        mov $SN, b
        fml
        fmrd
        mov d, $C0X
        mov $R0, d
        mov d, $C1X
        mov $C1Y, a             ; row 1: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C0Y, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C0Y, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C1Y, a
        mov $SN, b
        fml
        fmrd
        mov d, $C0Y
        mov $R0, d
        mov d, $C1Y
        mov $C1Z, a             ; row 2: u' = c*u + s*v
        mov $CS, b
        fml
        mov p, d
        mov $C0Z, a
        mov $SN, b
        fml
        fsmd
        mov d, $R0
        mov $C0Z, a             ; v' = c*v - s*u
        mov $CS, b
        fml
        mov p, d
        mov $C1Z, a
        mov $SN, b
        fml
        fmrd
        mov d, $C0Z
        mov $R0, d
        mov d, $C1Z
        brif alw #loop

rmat:   mov $C0X, rf2           ; 0x05 -> col0, col1, col2, T (the slot as cpres1 holds it)
        mov $C0Y, rf2
        mov $C0Z, rf2
        mov $C1X, rf2
        mov $C1Y, rf2
        mov $C1Z, rf2
        mov $C2X, rf2
        mov $C2Y, rf2
        mov $C2Z, rf2
        mov $TX, rf2
        mov $TY, rf2
        mov $TZ, rf2
        brif alw #loop

; ---- subroutines ----------------------------------------------------------------------------

; p = (next - next)^2 from two FIFO words.
sqdiff: mov rf1, a
        mov rf1, d
        fsbd
        mov d, a
        mov d, b
        fml
        rtif alw

; p = $T0 / a: reciprocal seed from io 0x28/0x29 with a linear correction, as Dynamite Cop does.
div:    mov a, $0x28 (e)
        ldi #0x7f, ah
        mov $0x28 (e), d
        fabd
        mov d, b
        fml : mov $0x29 (e), d
        fmrd
        mov d, b
        mov $T0, a
        fml
        rtif alw

; p = sqrt(d), d >= 0: s * (1/sqrt(s)) from the io 0x2a/0x2b seed, as Dynamite Cop does.
sqrtd:  mov d, $T0
        mov d, a
        mov a, $0x2a (e)
        ldi #0x7f, ah
        mov $0x2a (e), b
        fml : mov $0x2b (e), d
        fmrd : mov $T0, a
        mov d, b
        fml
        rtif alw

; ---- atan2 ---------------------------------------------------------------------------------
; cpres1's algorithm (m2-hle2 sharc.h sharc_fw_atan2, Fn_get_2d_dir): t = min/max of |x|,|y|,
; reduced by 30 degrees above tan 15, a rational polynomial, then the octant and quadrant. The
; angle is floor(rad * 32768/pi) & 0xffff. The TGP's own atan table (io 0x24-0x27) is only good
; to about one unit, so this uses the float path; divides get one residual correction (divp).
; The constants are cpres1's.

atan2:  mov rf1, a              ; 0x2f (x1, x2, z1, z2) -> atan2(z2 - z1, x2 - x1) as a 16-bit angle
        mov rf1, d
        fsbd
        mov d, $XV              ; x = x2 - x1
        fabd
        mov d, $AX
        mov rf1, a
        mov rf1, d
        fsbd
        mov d, $YV              ; y = z2 - z1
        fabd
        mov d, $AY
        mov $AX, a
        fadd                    ; |x| + |y|
        brif zrd #atzero        ; (0, 0) -> 0, as cpres1
        mov $AY, d
        fcpd                    ; |y| - |x| (a = |x|)
        brif !led #atswp
        mov d, $T0              ; |y| <= |x|: t = |y| / |x|
        ldi #0, d
        brif alw #atdiv
atswp:  mov $AX, d              ; |y| > |x|: t = |x| / |y|, the angle is pi/2 - atan(t)
        mov d, $T0
        mov $AY, a
        ldi #1, d
atdiv:  mov d, $SW
        bsif alw #divp
        mov d, $TT
        ldi #0, d
        mov d, $BS              ; octant base: 0 (pi/6 once t is reduced)
        ldi #0x490fda, al       ; a = HALFPI (0x3FC90FDA)
        ldi #0x7f, ah
        mov a, $BW              ; the same when swapped: pi/2 (pi/3 once t is reduced)
        mov $TT, d
        ldi #0x0930a2, al       ; a = TAN15 (0x3E8930A2)
        ldi #0x7d, ah
        fcpd
        brif !ged #atpoly       ; t < tan 15: no reduction
        ldi #0x5db3d7, al       ; a = SQRT3 (0x3FDDB3D7)
        ldi #0x7f, ah
        mov $TT, b
        fml                     ; sqrt3 * t
        ldi #1, d
        cxfd
        mov d, a
        mov p, d
        fsbd
        mov d, $T0              ; sqrt3*t - 1
        ldi #0x5db3d7, al       ; a = SQRT3 (0x3FDDB3D7)
        ldi #0x7f, ah
        mov $TT, d
        fadd                    ; sqrt3 + t
        mov d, a
        bsif alw #divp
        mov d, $TT              ; t = (sqrt3*t - 1) / (sqrt3 + t)
        ldi #0x060a91, al       ; a = PI6 (0x3F060A91)
        ldi #0x7e, ah
        mov a, $BS
        ldi #0x060a91, al       ; a = PI3 (0x3F860A91)
        ldi #0x7f, ah
        mov a, $BW
atpoly: mov $TT, a              ; atan(t) = t + t * g*(P0*g + P1) / ((g + Q0)*g + Q1), g = t*t
        mov a, b
        fml
        mov p, $G
        ldi #0xb853ad, al       ; a = P0 (0xBF3853AD)
        ldi #0x7e, ah
        mov $G, b
        fml                     ; P0 * g
        mov p, d
        ldi #0xb854a7, al       ; a = P1 (0xBFB854A7)
        ldi #0x7f, ah
        fadd                    ; P0*g + P1
        mov d, a
        fml                     ; (P0*g + P1) * g
        mov p, $NU
        ldi #0x18123b, al       ; a = Q0 (0x4098123B)
        ldi #0x81, ah
        mov $G, d
        fadd                    ; g + Q0
        mov d, a
        fml                     ; (g + Q0) * g
        mov p, d
        ldi #0x0a3f7d, al       ; a = Q1 (0x408A3F7D)
        ldi #0x81, ah
        fadd                    ; + Q1
        mov d, a
        mov $NU, d
        mov d, $T0
        bsif alw #divp
        mov d, a
        mov $TT, b
        fml
        mov $TT, d
        fsmd                    ; atan(t)
        mov d, $TT
        mov $SW, d
        ldi #0, a
        orad
        brif zrd #atns
        mov $TT, a
        mov $BW, d
        fsbd                    ; swapped: base - atan(t)
        brif alw #atq
atns:   mov $TT, a
        mov $BS, d
        fadd                    ; base + atan(t)
atq:    mov d, $TT
        mov $XV, d
        ldi #0, a
        fcpd
        brif ged #atpos
        ldi #0x490fda, al       ; a = PI (0x40490FDA)
        ldi #0x80, ah
        mov a, d
        mov $TT, a
        fsbd                    ; x < 0: pi - angle
        mov d, $TT
atpos:  mov $YV, d
        ldi #0, a
        fcpd
        brif ged #atout
        mov $TT, d
        fned                    ; y < 0: -angle
        mov d, $TT
atout:  mov $TT, a
        ldi #0x22f983, bl       ; b = ANGK (0x4622F983)
        ldi #0x8c, bh
        fml                     ; radians * 32768/pi
        mov p, d
        stmh fp rm              ; floor, as cpres1's fix under TRUNCATE
        cfxd
        stmh fp rz
        ldi #0xffff, a
        andd
        mov d, rf2
        brif alw #loop
atzero: ldi #0, d
        mov d, rf2
        brif alw #loop

; d = $T0 / a to nearly full float precision: div, then one correction with the residual.
divp:   mov a, $DN
        mov $T0, d
        mov d, $NM
        bsif alw #div           ; q0
        mov p, $Q
        mov $DN, a
        mov p, b
        fml                     ; den * q0
        mov $NM, d
        fmrd                    ; residual = num - den*q0
        mov d, $T0
        mov $DN, a
        bsif alw #div           ; residual / den
        mov $Q, d
        fsmd
        rtif alw

; ---- dispatch: one entry per command number 0x00..0xff ----------------------------------------
.equ NONE, loop
table:
        brif alw #NONE          ; 00
        brif alw #NONE          ; 01
        brif alw #NONE          ; 02
        brif alw #ident         ; 03 IDENTITY
        brif alw #NONE          ; 04
        brif alw #rmat          ; 05 RMATRIX
        brif alw #setpos        ; 06 SET_POS
        brif alw #scale         ; 07 SCALE
        brif alw #angx          ; 08 ANG_X
        brif alw #angy          ; 09 ANG_Y
        brif alw #angz          ; 0a ANG_Z
        brif alw #NONE          ; 0b
        brif alw #NONE          ; 0c
        brif alw #NONE          ; 0d
        brif alw #NONE          ; 0e
        brif alw #NONE          ; 0f
        brif alw #NONE          ; 10
        brif alw #NONE          ; 11
        brif alw #NONE          ; 12
        brif alw #fadd          ; 13 FADD
        brif alw #fsub          ; 14 FSUB
        brif alw #fmul          ; 15 FMUL
        brif alw #fdiv          ; 16 FDIV
        brif alw #int2f         ; 17 INT2F
        brif alw #f2int         ; 18 F2INT
        brif alw #NONE          ; 19
        brif alw #sqrt          ; 1a SQRT
        brif alw #NONE          ; 1b
        brif alw #NONE          ; 1c
        brif alw #NONE          ; 1d
        brif alw #NONE          ; 1e
        brif alw #NONE          ; 1f
        brif alw #NONE          ; 20
        brif alw #sin           ; 21 SIN
        brif alw #cos           ; 22 COS
        brif alw #tan           ; 23 TAN
        brif alw #sinsc         ; 24 SINSCALE
        brif alw #cossc         ; 25 COSSCALE
        brif alw #NONE          ; 26
        brif alw #NONE          ; 27
        brif alw #NONE          ; 28
        brif alw #NONE          ; 29
        brif alw #NONE          ; 2a
        brif alw #dist2d        ; 2b DIST2D
        brif alw #dist3d        ; 2c DIST3D
        brif alw #mag2d         ; 2d MAG2D
        brif alw #NONE          ; 2e
        brif alw #atan2         ; 2f ATAN2
        brif alw #NONE          ; 30
        brif alw #NONE          ; 31
        brif alw #NONE          ; 32
        brif alw #NONE          ; 33
        brif alw #NONE          ; 34
        brif alw #NONE          ; 35
        brif alw #NONE          ; 36
        brif alw #NONE          ; 37
        brif alw #NONE          ; 38
        brif alw #NONE          ; 39
        brif alw #NONE          ; 3a
        brif alw #NONE          ; 3b
        brif alw #NONE          ; 3c
        brif alw #NONE          ; 3d
        brif alw #NONE          ; 3e
        brif alw #NONE          ; 3f
        brif alw #NONE          ; 40
        brif alw #NONE          ; 41
        brif alw #NONE          ; 42
        brif alw #NONE          ; 43
        brif alw #NONE          ; 44
        brif alw #NONE          ; 45
        brif alw #NONE          ; 46
        brif alw #NONE          ; 47
        brif alw #NONE          ; 48
        brif alw #NONE          ; 49
        brif alw #NONE          ; 4a
        brif alw #NONE          ; 4b
        brif alw #NONE          ; 4c
        brif alw #NONE          ; 4d
        brif alw #NONE          ; 4e
        brif alw #NONE          ; 4f
        brif alw #NONE          ; 50
        brif alw #NONE          ; 51
        brif alw #NONE          ; 52
        brif alw #NONE          ; 53
        brif alw #NONE          ; 54
        brif alw #NONE          ; 55
        brif alw #NONE          ; 56
        brif alw #NONE          ; 57
        brif alw #NONE          ; 58
        brif alw #dot2d         ; 59 DOT2D
        brif alw #NONE          ; 5a
        brif alw #NONE          ; 5b
        brif alw #add3          ; 5c ADD3
        brif alw #sub3          ; 5d SUB3
        brif alw #scale3        ; 5e SCALE3
        .fill 0x100 - 0x5f, brif alw #NONE  ; 5f..ff
