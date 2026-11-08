# Extracts the RomFS of partition 0 from a decrypted CCI. Usage: python romfs_extract.py rom.cci outdir [subpath_prefix]
import struct, sys, os
cci, out = sys.argv[1], sys.argv[2]
prefix = sys.argv[3] if len(sys.argv) > 3 else ""
f = open(cci, "rb")
f.seek(0x120); p0_off = struct.unpack("<I", f.read(4))[0] * 0x200
f.seek(p0_off + 0x1B0); rom_off, rom_sz = struct.unpack("<II", f.read(8))
romfs = p0_off + rom_off * 0x200
f.seek(romfs); hdr = f.read(0x60)
assert hdr[:4] == b"IVFC", hdr[:4]
master_sz = struct.unpack_from("<I", hdr, 8)[0]
lv3_bs = struct.unpack_from("<I", hdr, 0x4C)[0]
bs = 1 << lv3_bs
lv3 = romfs + ((0x60 + master_sz + bs - 1) // bs) * bs
f.seek(lv3); h = struct.unpack("<10I", f.read(0x28))
dir_tab_off, dir_tab_sz = h[3], h[4]
file_tab_off, file_tab_sz = h[7], h[8]
data_off = h[9]
f.seek(lv3 + dir_tab_off); dtab = f.read(dir_tab_sz)
f.seek(lv3 + file_tab_off); ftab = f.read(file_tab_sz)
count = 0
def walk_dir(off, path):
    global count
    parent, sib, child, ffile, nxt, nlen = struct.unpack_from("<6I", dtab, off)
    fo = ffile
    while fo != 0xFFFFFFFF:
        par, fsib, doff, dsz, nh, fnl = struct.unpack_from("<IIQQII", ftab, fo)
        name = ftab[fo+0x20:fo+0x20+fnl].decode("utf-16le")
        rel = path + "/" + name if path else name
        if rel.startswith(prefix):
            dst = os.path.join(out, *rel.split("/"))
            if not (os.path.exists(dst) and os.path.getsize(dst) == dsz):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                f.seek(lv3 + data_off + doff)
                with open(dst, "wb") as o:
                    rem = dsz
                    while rem:
                        c = f.read(min(rem, 1 << 22)); o.write(c); rem -= len(c)
                count += 1
        fo = fsib
    co = child
    while co != 0xFFFFFFFF:
        _, csib, _, _, _, cnl = struct.unpack_from("<6I", dtab, co)
        cname = dtab[co+0x18:co+0x18+cnl].decode("utf-16le")
        walk_dir(co, path + "/" + cname if path else cname)
        co = csib
walk_dir(0, "")
print("extracted", count, "files")
