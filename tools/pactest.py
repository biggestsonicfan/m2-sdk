#!/usr/bin/env python3
"""pactest.py [-o src/pactest_rom.h]

Builds a small homebrew test program for the Pac-Man board, with its own graphics and
colour PROMs, so src/pacman.c builds and runs without Namco's ROMs. It exercises what the
real game uses: IM 2 vblank IRQ with the vector from OUT (0), the 0x5000 IRQ-enable latch,
IN0/IN1, VRAM + colour RAM through the tilemap scan, and sprites (code, colour, flips).

On screen: a border, text, a frame counter and the raw IN0/IN1 bytes in hex, a Pac-Man
sprite you steer with the P1 stick, and three ghost sprites moving on their own.
Everything here is original data; the header it writes is committed.
"""
import argparse, os, re, sys

sys.path.insert(0, os.path.dirname(__file__))
from pacrom import write_header

# ---------------------------------------------------------------------------- font
def load_gfont():
    """gFont from src/m2font.h: 128 glyphs x 32 bytes, Model 2 char-RAM nibble order."""
    src = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'm2font.h')).read()
    body = src[src.index('gFont'):]
    body = body[body.index('{') + 1:body.index('}')]
    vals = [int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', body)]
    return (vals + [0] * (128 * 32))[:128 * 32]     # the table stops one glyph short of 128


def glyph_pixel(font, ch, x, y):
    b = font[ch * 32 + y * 4 + ((x >> 1) ^ 1)]      # same decode as m2_tilefb.h tfb_glyph
    return (b & 15) if x & 1 else (b >> 4)


# ---------------------------------------------------------------------------- gfx encode
TXOFF = [64, 65, 66, 67, 0, 1, 2, 3]
SXOFF = [64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195, 0, 1, 2, 3]


def put(buf, base, bitoff, v):
    """store 2bpp pixel v at MAME gfx bit offset (planes {0,4}, plane 0 = MSB)."""
    if v & 2: buf[base + (bitoff >> 3)] |= 0x80 >> (bitoff & 7)
    if v & 1: buf[base + ((bitoff + 4) >> 3)] |= 0x80 >> ((bitoff + 4) & 7)


def encode_tile(buf, code, portrait):
    """portrait[y][x] (as seen on the upright monitor) -> native char `code`."""
    for y in range(8):
        for x in range(8):
            put(buf, code * 16, TXOFF[x] + y * 8, portrait[x][7 - y])   # native (x,y) = portrait (7-y, x)


def encode_sprite(buf, code, portrait):
    """portrait[y][x], 16x16 -> native sprite `code`."""
    for j in range(16):
        for i in range(16):
            put(buf, code * 64, SXOFF[i] + (j & 7) * 8 + (j >> 3) * 256, portrait[i][15 - j])


def sprite_art(kind):
    p = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            dx, dy = x - 7.5, y - 7.5
            r2 = dx * dx + dy * dy
            if kind in ('open', 'closed'):
                if r2 <= 7.2 * 7.2:
                    p[y][x] = 1
                    if kind == 'open' and dx > 0 and abs(dy) < dx * 0.8:
                        p[y][x] = 0                  # mouth, facing right
            else:                                    # ghost: dome, body, wavy skirt, eyes
                body = (y < 8 and r2 <= 7.2 * 7.2) or (8 <= y < 14 and 1 <= x <= 14)
                skirt = y >= 14 and 1 <= x <= 14 and ((x - 1) % 4) < 2
                if body or skirt: p[y][x] = 1
                for ex in (4, 10):
                    if abs(x - ex) <= 1 and 4 <= y <= 8: p[y][x] = 2
                    if abs(x - ex - 0.5) <= 0.5 and 6 <= y <= 7: p[y][x] = 3
    return p


# ---------------------------------------------------------------------------- screen map
def vram(tx, ty):
    """portrait cell (tx 0..27, ty 0..35) -> VRAM offset (namco/pacman_v.cpp scan)."""
    row, col = 29 - tx, ty - 2
    return (row + ((col & 0x1f) << 5)) if col & 0x20 else (col + (row << 5))


# ---------------------------------------------------------------------------- assembler
class Asm:
    def __init__(self):
        self.code = bytearray(0x4000)
        self.pc = 0
        self.labels, self.fix = {}, []

    def org(self, a): self.pc = a
    def label(self, n): self.labels[n] = self.pc
    def b(self, *bs):
        for v in bs:
            self.code[self.pc] = v & 0xff
            self.pc += 1
    def w(self, v):
        if isinstance(v, str): self.fix.append(('w', self.pc, v)); v = 0
        self.b(v, v >> 8)
    def rel(self, op, n):                            # JR/DJNZ-style op + displacement
        self.b(op); self.fix.append(('r', self.pc, n)); self.b(0)
    def op16(self, op, v):                          # op nn (LD rr,nn / JP / CALL / LD (nn),A ...)
        if isinstance(op, int): op = (op,)
        self.b(*op); self.w(v)
    def link(self):
        for kind, at, n in self.fix:
            t = self.labels[n]
            if kind == 'w': self.code[at], self.code[at + 1] = t & 0xff, t >> 8
            else:
                d = t - (at + 1)
                assert -128 <= d < 128, n
                self.code[at] = d & 0xff
        return bytes(self.code)


# Z80 opcodes used
DI, EI, RETI = 0xf3, 0xfb, (0xed, 0x4d)
LD_SP, LD_HL, LD_DE, LD_BC, JP, CALL, RET = 0x31, 0x21, 0x11, 0x01, 0xc3, 0xcd, 0xc9
LD_NN_A, LD_A_NN = 0x32, 0x3a
JR, JRZ, JRNZ = 0x18, 0x28, 0x20


def program(texts, dl_items):
    a = Asm()
    a.b(DI); a.op16(LD_SP, 0x4ff0); a.op16(JP, 'start')
    a.org(0x100); a.w('isr')                        # IM 2 table: I = 0x01, vector 0x00

    a.org(0x200); a.label('start')
    for base, fill in ((0x4000, 0x20), (0x4400, 0x01)):          # clear VRAM, colour RAM
        a.op16(LD_HL, base); a.op16(LD_DE, base + 1); a.op16(LD_BC, 0x3ff)
        a.b(0x36, fill); a.b(0xed, 0xb0)                         # ld (hl),n ; ldir
    a.op16(LD_HL, 0x4c00); a.op16(LD_DE, 0x4c01); a.op16(LD_BC, 0x3ff)   # clear work RAM
    a.b(0x36, 0); a.b(0xed, 0xb0)
    a.op16(LD_HL, 'drawlist')                       # records: offs lo, hi (0x40xx), tile, colour
    a.label('dl')
    a.b(0x5e, 0x23, 0x56, 0x23)                     # ld e,(hl); inc hl; ld d,(hl); inc hl
    a.b(0x7a, 0xb3); a.rel(JRZ, 'dldone')           # ld a,d; or e; jr z
    a.b(0x7e, 0x23, 0x12)                           # ld a,(hl); inc hl; ld (de),a
    a.b(0xcb, 0xd2)                                 # set 2,d  (0x40xx -> 0x44xx colour RAM)
    a.b(0x7e, 0x23, 0x12)
    a.rel(JR, 'dl')
    a.label('dldone')
    a.b(0x3e, 0x80); a.op16(LD_NN_A, 0x4c00); a.op16(LD_NN_A, 0x4c01)   # player x,y regs
    a.b(0x3e, 0x01, 0xed, 0x47)                     # ld a,1 ; ld i,a
    a.b(0xed, 0x5e)                                 # im 2
    a.b(0xaf, 0xd3, 0x00)                           # xor a ; out (0),a  -> vector 0x00
    a.b(0x3e, 0x01); a.op16(LD_NN_A, 0x5000)        # IRQ enable
    a.b(EI)
    # busy idle loop, never HALTs (like the real game's task loop), with a game-like mix of
    # IX-indexed, CB, 16-bit, stack and call work, so SPEED is a stress figure
    a.label('idle')
    a.op16((0xdd, 0x21), 0x4c20); a.b(0x06, 0x08)           # ld ix,0x4c20 ; ld b,8
    a.label('mix')
    a.b(0xdd, 0x7e, 0x00, 0xdd, 0x86, 0x01, 0xdd, 0x77, 0x02) # ld a,(ix+0) ; add a,(ix+1) ; ld (ix+2),a
    a.b(0xcb, 0x47); a.rel(JRZ, 'mix1')                     # bit 0,a ; jr z
    a.b(0xdd, 0x34, 0x03)                                   # inc (ix+3)
    a.label('mix1')
    a.b(0xdd, 0x23, 0xc5); a.op16(CALL, 'work'); a.b(0xc1)  # inc ix ; push bc ; call ; pop bc
    a.rel(0x10, 'mix')                                      # djnz mix
    a.op16(0x2a, 0x4c10); a.b(0x23); a.op16(0x22, 0x4c10)   # ld hl,(nn) ; inc hl ; ld (nn),hl
    a.rel(JR, 'idle')
    a.label('work')
    a.op16(LD_HL, 0x4c40); a.op16(LD_DE, 0x0001); a.b(0x19) # ld hl,nn ; ld de,1 ; add hl,de
    a.b(0x7e, 0x07, 0x77, RET)                              # ld a,(hl) ; rlca ; ld (hl),a ; ret

    # ---- vblank ISR
    a.label('isr')
    a.b(0xf5, 0xc5, 0xd5, 0xe5)                     # push af bc de hl
    a.b(0xaf); a.op16(LD_NN_A, 0x5000)              # IRQ off while we work
    a.op16(LD_HL, 0x4c02); a.b(0x34)                # frame++
    a.op16(LD_A_NN, 0x5000); a.b(0x47)              # b = IN0 (active low)
    for bit, addr, op in ((0, 0x4c01, 0x34), (3, 0x4c01, 0x35), (1, 0x4c00, 0x34), (2, 0x4c00, 0x35)):
        # up: y reg ++ ; down: y reg -- ; left: x reg ++ ; right: x reg --  (the board counts down)
        a.b(0xcb, 0x40 | (bit << 3)); a.rel(JRNZ, 'j%d' % bit)   # bit n,b
        a.op16(LD_HL, addr); a.b(op)                             # inc/dec (hl)
        a.label('j%d' % bit)
    # sprite 0: the player
    a.op16(LD_A_NN, 0x4c00); a.op16(LD_NN_A, 0x5060)
    a.op16(LD_A_NN, 0x4c01); a.op16(LD_NN_A, 0x5061)
    a.op16(LD_A_NN, 0x4c02); a.b(0xe6, 0x08, 0x0f)  # and 8 ; rrca -> 0 or 4 = code 0/1 (chomp)
    a.op16(LD_NN_A, 0x4ff0); a.b(0x3e, 9); a.op16(LD_NN_A, 0x4ff1)
    # ghosts: 1 runs across, 2 runs down, 3 sits still x-flipped
    ghosts = ((1, 'frame', 0x60, 0x08, 3), (2, 0x70, 'frame', 0x08, 4), (3, 0x98, 0xa0, 0x09, 5))
    for n, x, y, attr, col in ghosts:
        for reg, v in ((0x5060 + 2 * n, x), (0x5061 + 2 * n, y)):
            if v == 'frame': a.op16(LD_A_NN, 0x4c02)
            else: a.b(0x3e, v)
            a.op16(LD_NN_A, reg)
        a.b(0x3e, attr); a.op16(LD_NN_A, 0x4ff0 + 2 * n)
        a.b(0x3e, col); a.op16(LD_NN_A, 0x4ff1 + 2 * n)
    # counters: frame, IN0, IN1 as hex
    for src, (tx, ty) in ((0x4c02, texts['frame']), (0x5000, texts['in0']), (0x5040, texts['in1'])):
        a.op16(LD_A_NN, src); a.op16(LD_HL, 0x4000 + vram(tx, ty)); a.op16(CALL, 'hex2')
    a.b(0x3e, 0x01); a.op16(LD_NN_A, 0x5000)        # IRQ back on
    a.b(0xe1, 0xd1, 0xc1, 0xf1)
    a.b(EI); a.b(*RETI)

    # hex2: A -> two hex digit tiles at (HL) and the next portrait column (HL - 0x20)
    a.label('hex2')
    a.b(0x4f)                                       # ld c,a
    a.b(0x0f, 0x0f, 0x0f, 0x0f, 0xcd); a.w('hexd')  # rrca x4 ; call hexd
    a.b(0x11, 0xe0, 0xff, 0x19)                     # ld de,-0x20 ; add hl,de
    a.b(0x79, 0xcd); a.w('hexd')                    # ld a,c ; call hexd
    a.b(RET)
    a.label('hexd')                                 # (HL) = tile for low nibble of A
    a.b(0xe6, 0x0f, 0xe5)                           # and 0x0f ; push hl
    a.op16(LD_HL, 'hexchars'); a.b(0x5f, 0x16, 0x00, 0x19, 0x7e)   # ld e,a ; ld d,0 ; add hl,de ; ld a,(hl)
    a.b(0xe1, 0x77, RET)                            # pop hl ; ld (hl),a ; ret
    a.label('hexchars'); a.b(*b'0123456789ABCDEF')

    a.label('drawlist')
    for offs, tile, col in dl_items: a.w(0x4000 + offs); a.b(tile, col)
    a.w(0)
    return a.link()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('-o', '--output', default=os.path.join(os.path.dirname(__file__), '..', 'src', 'pactest_rom.h'))
    out = ap.parse_args().output

    font = load_gfont()
    tiles, sprites = bytearray(0x1000), bytearray(0x1000)
    for ch in range(0x20, 0x80):                    # ASCII tiles; font ink = 1, outline = 2
        encode_tile(tiles, ch, [[glyph_pixel(font, ch, x, y) if glyph_pixel(font, ch, x, y) in (1, 2) else 0
                                 for x in range(8)] for y in range(8)])
    encode_tile(tiles, 1, [[1 if (x in (0, 7) or y in (0, 7)) else (2 if (x + y) % 2 else 3)
                            for x in range(8)] for y in range(8)])   # border block
    encode_sprite(sprites, 0, sprite_art('open'))
    encode_sprite(sprites, 1, sprite_art('closed'))
    encode_sprite(sprites, 2, sprite_art('ghost'))

    # 7f palette: bits 0-2 R, 3-5 G, 6-7 B
    pal = [0x00, 0x07, 0x38, 0xc0, 0x3f, 0xf8, 0xc7, 0xff, 0x52, 0x2f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00]
    pal += [0] * 16
    lut = [0] * 256                                 # 4a lookup: colour c pixel p -> palette index
    colours = {1: (0, 7, 8, 7), 2: (0, 4, 1, 7), 3: (0, 1, 7, 3), 4: (0, 9, 7, 3), 5: (0, 5, 7, 3),
               6: (0, 3, 10, 5), 7: (0, 2, 8, 7), 9: (0, 4, 4, 4)}
    for c, ps in colours.items():
        for p, v in enumerate(ps): lut[c * 4 + p] = v

    items, texts = [], {}
    for tx in range(28):                            # border round the playfield (rows 2..33)
        for ty in (2, 33): items.append((vram(tx, ty), 1, 6))
    for ty in range(3, 33):
        for tx in (0, 27): items.append((vram(tx, ty), 1, 6))

    def text(tx, ty, s, col):
        for i, ch in enumerate(s): items.append((vram(tx + i, ty), ord(ch), col))
    text(3, 0, 'PAC-MAN BOARD TEST', 2)
    text(2, 1, 'Z80 ON THE MODEL 2 I960', 1)
    text(3, 6, 'NO NAMCO ROMS FOUND', 3)
    text(3, 8, 'RUN TOOLS/PACROM.PY', 7)
    text(3, 9, 'ON PACMAN.ZIP', 7)
    text(3, 13, 'STICK MOVES PAC-MAN', 1)
    text(3, 28, 'FRAME', 1); texts['frame'] = (9, 28)
    text(3, 30, 'IN0', 1);   texts['in0'] = (9, 30)
    text(13, 30, 'IN1', 1);  texts['in1'] = (19, 30)
    text(4, 34, 'M2-SDK  PACMAN.C', 4)
    text(6, 35, '2026 HOMEBREW', 5)

    rom = program(texts, items)
    write_header(out, {'rom': rom, 'tiles': tiles, 'sprites': sprites, 'pal': pal, 'lut': lut},
                 'nothing (original test program)', 'Safe to commit: no Namco data.')
    print(out)


if __name__ == '__main__':
    main()
