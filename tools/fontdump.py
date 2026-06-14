#!/usr/bin/env python3
"""Decode the built-in gFont from m2font.h and print glyphs as ASCII, so we can
see orientation/layout without compiling or running MAME.

Each glyph = 32 bytes = 8 rows x 4 bytes; each pixel is a nibble. gFont is
two-layer: value 1 = letter strokes, value 2 = fill/shadow. We show value-1.
"""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "m2font.h")

def load_font():
    text = open(SRC).read()
    # grab everything between the first '{' and the matching '}'
    body = text[text.index("{") + 1: text.index("}")]
    vals = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", body)]
    return vals

def pixel(glyph, gx, gy, order="std"):
    """Return nibble value for pixel (gx,gy) under a given decode 'order'."""
    if order == "std":           # gx0 = byte low nibble (existing g2d_text)
        b = glyph[gy * 4 + (gx >> 1)]
        return (b >> 4) if (gx & 1) else (b & 0xf)
    if order == "hi":            # gx0 = byte high nibble
        b = glyph[gy * 4 + (gx >> 1)]
        return (b & 0xf) if (gx & 1) else (b >> 4)
    raise ValueError(order)

def show(vals, ch, order="std", flipx=False, flipy=False, inkval=1, swapx=False):
    c = ord(ch)
    glyph = vals[c * 32: c * 32 + 32]
    print(f"'{ch}' (0x{c:02x})  order={order} flipx={flipx} flipy={flipy} ink={inkval}")
    for gy in range(8):
        row = ""
        for gx in range(8):
            sx = (gx + 4) & 7 if swapx else gx
            if flipx:
                sx = 7 - sx
            sy = (7 - gy) if flipy else gy
            v = pixel(glyph, sx, sy, order)
            if inkval == "any":
                row += "#" if v else "."
            else:
                row += "#" if v == inkval else ("+" if v else ".")
        print("  " + row)
    print()

if __name__ == "__main__":
    vals = load_font()
    print(f"loaded {len(vals)} bytes ({len(vals)//32} glyphs)\n")
    order = sys.argv[1] if len(sys.argv) > 1 else "std"
    flipx = "flipx" in sys.argv
    flipy = "flipy" in sys.argv
    swapx = "swapx" in sys.argv
    ink = "any" if "any" in sys.argv else 1
    # asymmetric letters + counters + descenders reveal orientation unambiguously
    for ch in "FELPRJ" "bdpqg" "G2":
        show(vals, ch, order=order, flipx=flipx, flipy=flipy, inkval=ink, swapx=swapx)
