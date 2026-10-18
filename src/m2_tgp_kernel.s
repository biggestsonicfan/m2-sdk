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
; against MAME): same argument order, same results. Commands not listed in the table are
; ignored (their arguments would then be read as commands: don't send them on 2A).
;
; Hardware used (MAME src/mame/sega/model2.cpp, copro_tgp_*): rf1 = input FIFO (a read
; waits for data), rf2 = output FIFO, io 0x20/0x21 = sin/cos of a 16-bit angle, io
; 0x28/0x29 = reciprocal seed, io 0x2a/0x2b = 1/sqrt seed. The divide and square-root
; sequences are the ones Dynamite Cop's 2A program uses with those tables.
;
; Assemble: python3 tools/tgpasm.py src/m2_tgp_kernel.s --header src/m2_tgp_kernel.h --name m2_tgp_kernel

.equ T0, 0x70           ; scratch data RAM
.equ T1, 0x71

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

; ---- dispatch: one entry per command number 0x00..0xff ----------------------------------------
.equ NONE, loop
table:
        brif alw #NONE          ; 00
        brif alw #NONE          ; 01
        brif alw #NONE          ; 02
        brif alw #NONE          ; 03
        brif alw #NONE          ; 04
        brif alw #NONE          ; 05
        brif alw #NONE          ; 06
        brif alw #NONE          ; 07
        brif alw #NONE          ; 08
        brif alw #NONE          ; 09
        brif alw #NONE          ; 0a
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
        brif alw #NONE          ; 2f
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
