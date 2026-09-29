#!/usr/bin/env python3
"""snd2rom.py <program.bin> <out_dir>  ->  <out_dir>/epr-19021.31

Turns a raw big-endian 68000 sound program (e.g. snd/scsp_passthru.bin, linked at
0x600000) into the sound program EPROM image of the sfight set: epr-19021.31, 512 KB,
the 68000's 16-bit words stored low byte first (MAME sega/model2.cpp loads it with
ROM_LOAD16_WORD_SWAP into "audiocpu", mapped at 0x600000), unused space 0xFF. Burn it
in place of the sound EPROM, or put it beside the two program EPROMs in a MAME rompath.
The first 16 bytes are the reset vectors (MAME copies them to sound RAM at reset).
"""
import os, sys

SIZE = 0x80000


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    prog = open(sys.argv[1], 'rb').read()
    if len(prog) > SIZE:
        sys.exit('%s: %d bytes, more than the 512 KB EPROM' % (sys.argv[1], len(prog)))
    img = bytearray(prog + b'\xff' * (SIZE - len(prog)))
    img[0::2], img[1::2] = img[1::2], img[0::2]          # 68000 big-endian -> word-swapped
    out = os.path.join(sys.argv[2], 'epr-19021.31')
    open(out, 'wb').write(img)
    print('%s: %d-byte 68000 program in a 512 KB sound EPROM image' % (out, len(prog)))


if __name__ == '__main__':
    main()
