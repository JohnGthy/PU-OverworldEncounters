# Unpacks a GARC archive into numbered files. Usage: python garc_unpack.py <garc> <outdir>
import struct, sys, os
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
f = open(src, "rb")
d = f.read(0x1C)
assert d[:4] == b"CRAG", d[:4]
hsz = struct.unpack_from("<I", d, 4)[0]
data_off = struct.unpack_from("<I", d, 0x10)[0]
f.seek(hsz)
magic, fsz, cnt = struct.unpack("<4sIH2x", f.read(12))
assert magic == b"OTAF"
fato = struct.unpack("<%dI" % cnt, f.read(4 * cnt))
fatb_pos = f.tell()
f.seek(fatb_pos); m, bsz, fcnt = struct.unpack("<4sII", f.read(12))
assert m == b"BTAF"
base = f.tell()
n = 0
for i in range(cnt):
    f.seek(base + fato[i])
    vec = struct.unpack("<I", f.read(4))[0]
    sub = 0
    for b in range(32):
        if vec & (1 << b):
            st, en, ln = struct.unpack("<III", f.read(12))
            pos = f.tell()
            f.seek(data_off + st)
            blob = f.read(en - st)
            name = "%04d" % i if vec == 1 else "%04d_%02d" % (i, b)
            open(os.path.join(out, name), "wb").write(blob)
            f.seek(pos); n += 1
print("entries", cnt, "files", n)
