#!/usr/bin/env python3
"""Builds owe_icons.bin (the 2D icon mode's icons) from Ultra Sun / Ultra Moon's own icon archive.

Usage: make_icons.py <romfs a/0/6/2 GARC> <out owe_icons.bin>
The GARC holds one LZ11-compressed BFLIM per icon: 64x32 RGBA5551, stored rotated (32x64, 8x8
Morton tiles). Output: b"OWEI", u32 count, 8 bytes pad, then per icon 64x32 u16 pixels,
row-major top-down (bit 0 = opaque)."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import garc, lz11

def decode_rotated(d):
    w, h = 32, 64                      # as stored
    out = [0] * (64 * 32)
    i = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for k in range(64):
                x = y = 0
                for b in range(3):
                    x |= ((k >> (2 * b)) & 1) << b
                    y |= ((k >> (2 * b + 1)) & 1) << b
                v = struct.unpack_from('<H', d, i * 2)[0]; i += 1
                sx, sy = tx + x, ty + y            # stored pixel
                # rotate 90 degrees counter-clockwise: 32x64 -> 64x32
                X, Y = sy, (w - 1 - sx)
                out[Y * 64 + X] = v
    return out

def main():
    files, _ = garc.read(sys.argv[1])
    body = bytearray()
    for f in files:
        px = decode_rotated(lz11.decompress(f))
        body += struct.pack('<%dH' % len(px), *px)
    open(sys.argv[2], 'wb').write(b'OWEI' + struct.pack('<III', len(files), 0, 0) + bytes(body))
    print(len(files), 'icons')

if __name__ == '__main__':
    main()
