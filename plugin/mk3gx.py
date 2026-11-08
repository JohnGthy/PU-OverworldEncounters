#!/usr/bin/env python3
"""Minimal .3gx packer (format of 3gxtool v1.3, no symbols, default checksum payload).
Usage: mk3gx.py input.elf output.3gx --title T --author A --summary S --desc D --target 0x1B5100 ...
"""
import struct, sys, argparse

p = argparse.ArgumentParser()
p.add_argument("elf"); p.add_argument("out")
p.add_argument("--title", default=""); p.add_argument("--author", default="")
p.add_argument("--summary", default=""); p.add_argument("--desc", default="")
p.add_argument("--target", action="append", default=[])
p.add_argument("--version", default="0.1.0")
p.add_argument("--mem", default="5MiB")
a = p.parse_args()

img = open(a.elf, "rb").read()
assert img[:4] == b"\x7fELF"
e_entry, e_phoff = struct.unpack_from("<II", img, 0x18)
e_phentsize, e_phnum = struct.unpack_from("<HH", img, 0x2A)
segs = {}
base = None
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    ptype, poff, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", img, off)
    if memsz == 0 or ptype != 1:
        continue
    if base is None:
        base = vaddr
    segs[flags] = (img[poff:poff + filesz], memsz, filesz)
assert e_entry == base, "entry must be segment start"
code, codesz, _ = segs[5]
ro, rosz, _ = segs.get(4, (b"", 0, 0))
data, datamem, datafile = segs.get(6, (b"", 0, 0))
code = code.ljust(codesz, b"\0"); ro = ro.ljust(rosz, b"\0")
blob = code + ro + data
assert len(blob) % 4 == 0
checksum = sum(struct.unpack("<%dI" % (len(blob) // 4), blob)) & 0xFFFFFFFF

DEF = bytes([0xC0,0x40,0x2D,0xE9,0x00,0x70,0xA0,0xE3,0x04,0x60,0x90,0xE4,0x06,0x70,0x87,0xE0,
             0x01,0x00,0x50,0xE1,0xFB,0xFF,0xFF,0x1A,0x07,0x00,0xA0,0xE1,0xC0,0x80,0xBD,0xE8,
             0x00,0xF0,0x20,0xE3])
HDR_SIZE = 8 + 4 + 4 + (4 * 8 + 4 + 4 + 16 + 16) + 4 * 10 + 8 + 12
out = bytearray(HDR_SIZE)
def put_str(s):
    if not s:
        return 0, 0
    pos = len(out); out.extend(s.encode() + b"\0"); return len(s) + 1, pos
title = put_str(a.title); author = put_str(a.author)
summary = put_str(a.summary); desc = put_str(a.desc)
targets = [int(t, 0) for t in a.target]
tpos = 0
if targets:
    tpos = len(out); out.extend(struct.pack("<%dI" % len(targets), *targets))
exe_dec = len(out); out.extend(DEF)
swap_enc = len(out); out.extend(DEF)
swap_dec = len(out); out.extend(DEF)
pad = 16 - (len(out) & 0xF); out.extend(b"\0" * pad)
code_off = len(out); out.extend(code)
ro_off = len(out); out.extend(ro)
data_off = len(out); out.extend(data)

mem = {"5MiB": 0, "2MiB": 1, "10MiB": 2}[a.mem]
flags = 1 | (1 << 1) | (mem << 2) | (2 << 4)  # embedded dec/swap funcs, mem size, compat=any
ma, mi, rv = (int(x) for x in a.version.split("."))
ver = (ma << 24) | (mi << 16) | (rv << 8)
hdr = struct.pack("<QII", 0x3230303024584733, ver, 0)
hdr += struct.pack("<8I", author[0], author[1], title[0], title[1], summary[0], summary[1], desc[0], desc[1])
hdr += struct.pack("<II", flags, checksum) + b"\0" * 32
hdr += struct.pack("<10I", code_off, ro_off, data_off, len(code), len(ro), datafile, datamem - datafile, exe_dec, swap_enc, swap_dec)
hdr += struct.pack("<II", len(targets), tpos)
hdr += struct.pack("<III", 0, 0, 0)
assert len(hdr) == HDR_SIZE, (len(hdr), HDR_SIZE)
out[:HDR_SIZE] = hdr
open(a.out, "wb").write(out)
print("wrote", a.out, len(out), "bytes; code", hex(len(code)), "ro", hex(len(ro)), "data", hex(datafile), "bss", hex(datamem - datafile))
