; abstest_tgpx4.s - a Model 2C coprocessor (MB86235 TGPx4) program for src/abstest.c: every
; transfer form that has a displacement field, with the absolute address mode (EA type 0x0c,
; written A(nnnH) / B(nnnH)): the address is the displacement field alone, no AR register.
;
; Each test writes with one mode and reads back with the other: absolute, or A(@AR7+n) with
; AR7 = 0. AR0 is 40H and the other ARs are nonzero too. An absolute operand encodes AR0 in its
; AR field, so a core that added an AR would miss the word by 40H. The A bus from 400H up goes
; out to the external bus at EB = 400000H, buffer RAM (i960 0x900000 + word*4).
;
; The two-op MOV1 lines carry an FMUL on MA7, which nothing reads (a two-op word always has one).
;
; Protocol: the program sends 0AB5H once it is ready (after CLRF, which drops anything the i960
; sent before). The i960 then writes a start word, the 7 input words and reads the 10 results in
; turn (abstest.c has the same list). The loop then waits for the next start word.
;
; Assemble: python3 tools/tgpx4asm.py src/abstest_tgpx4.s --header src/abstest_tgpx4.h --name abstest_tgpx4

start:  NOP : SETM #0090H               ; the mode Dynamite Cop's program sets
        MOV3 #00400000, EB              ; external base = buffer RAM
        MOV3 #0, EO
        MOV3 #0, SP
        MOV3 #40, AR0                   ; what an absolute operand's AR field names
        MOV3 #11, AR1
        MOV3 #22, AR2
        MOV3 #33, AR3
        MOV3 #44, AR4
        MOV3 #55, AR5
        MOV3 #66, AR6
        MOV3 #0, AR7                    ; A(@AR7+n) is word n
        NOP : CLRF                      ; empties the FIFOs: the i960 waits for the next word
        MOV3 #0AB5, AA0
        NOP : MOV2 AA0, FO0             ; ready

loop:   NOP : MOV2 FI, AA0              ; start word

        NOP : MOV2 FI, A(123H)          ; 1  MOV2 write A
        NOP : MOV2 A(@AR7+123H), FO0

        NOP : MOV2 FI, A(@AR7+2A5H)     ; 2  MOV2 read A
        NOP : MOV2 A(2A5H), FO0

        NOP : MOV2 FI, B(155H)          ; 3  MOV2 write B
        NOP : MOV2 B(@AR7+155H), FO0

        NOP : MOV2 FI, B(@AR7+3FFH)     ; 4  MOV2 read B (the last word)
        NOP : MOV2 B(3FFH), FO0

        NOP : FMUL MA7, MA7, MA7 : MOV1 FI, A(1BH) ; 5  MOV1 write A (5-bit field)
        NOP : MOV2 A(@AR7+1BH), FO0

        MOV3 #12345678, A(3C0H)         ; 6  MOV3 write A (12-bit field)
        NOP : MOV2 A(@AR7+3C0H), FO0

        MOV3 #0CAFEF00D, A(0FF0H)       ; 7  MOV3 write to the external bus: buffer RAM word FF0
        NOP : MOV2 FI, A(3F00H)         ; 8  MOV2 write to the external bus: buffer RAM word 3F00
        NOP : MOV2 A(3F01H), FO0        ; 9  MOV2 read from the external bus: word 3F01 (the i960's)

        NOP : MOV2 FI, A(@AR7+15H)      ; 10 MOV1 read A
        NOP : FMUL MA7, MA7, MA7 : MOV1 A(15H), FO0

        NOP : MOV2 AR0, FO0             ; 11 AR0 is still 40H: absolute modes change no AR
        NOP : DJMP loop
        NOP : MOV2 AR7, FO0             ; 12 and AR7 is still 0
