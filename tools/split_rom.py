# Splits the used part of the CCI into ~380MB chunks for upload. Usage: python split_rom.py rom.cci outdir
import sys, os, struct
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
f = open(src, "rb")
f.seek(0x100); hdr = f.read(0x100)
# NCSD: partition table at 0x120; compute end of last partition
pt = struct.unpack_from("<16I", hdr, 0x20)
end = max((pt[i*2] + pt[i*2+1]) * 0x200 for i in range(8))
CH = 380 * 1024 * 1024
f.seek(0); i = 0; done = 0
while done < end:
    n = min(CH, end - done)
    with open(os.path.join(out, "rom.part%02d" % i), "wb") as o:
        rem = n
        while rem:
            c = f.read(min(rem, 1 << 22)); o.write(c); rem -= len(c)
    done += n; i += 1
print("total", end, "parts", i)
