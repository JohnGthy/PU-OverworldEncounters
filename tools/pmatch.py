import sys,struct;import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
def norm(b):
    out=[]
    for i in range(0,len(b),4):
        w=struct.unpack_from('<I',b,i)[0]
        if (w>>25)&7==5: w&=0xFF000000
        elif (w&0x0F7F0000)==0x051F0000: w&=0xFFFFF000  # ldr pc-rel
        out.append(w)
    return out
def find(src,addr,dst,lo,hi,n=16):
    pat=norm(src[addr:addr+4*n]); res=[]
    for off in range(lo,hi,4):
        if norm(dst[off:off+4*n])==pat: res.append(off)
    return res
