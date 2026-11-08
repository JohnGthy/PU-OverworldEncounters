# FieldRo 1.2 analysis helpers (mirror of fdis for the update)
import sys,struct,capstone,cxxfilt
import os
TOOLS=os.path.dirname(os.path.abspath(__file__))
DUMP=os.environ.get('OWE_DUMP', os.path.join(TOOLS, '..', 'dump'))   # your own game dumps (see tools/README.md)
sys.path.insert(0,TOOLS); import croload,crsexp
EXP=crsexp.exports(os.path.join(DUMP,'1.2','static.crs'))
syms={}
for n,a in EXP.items():
    try: syms[a&~1]=cxxfilt.demangle(n)
    except Exception: syms[a&~1]=n
m,segs,sa,imp,unr,v,d=croload.load_cro(os.path.join(DUMP,'1.2','FieldRo.cro'),exports=EXP)
code=open(os.path.join(DUMP,'1.2','code.bin'),'rb').read()
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM); md.skipdata=True
stubs={}
for loc,n in imp.items():
    if loc>=4 and struct.unpack_from('<I',m,loc-4)[0]==0xE51FF004: stubs[loc-4]=n
def dem(n):
    try: return cxxfilt.demangle(n)
    except Exception: return n
def name(t):
    if t in stubs: return '>'+dem(stubs[t])
    return syms.get(t,'')
def dis(addr,n,src='cro'):
    buf=m if src=='cro' else code; off=addr if src=='cro' else addr-0x100000
    for i in md.disasm(buf[off:off+n*4],addr):
        c=''
        if i.mnemonic.startswith('b') and i.op_str.startswith('#'):
            t=int(i.op_str[1:],16); c=name(t)[:120] or ('FR_'+hex(t) if src=='cro' else '')
        if src!='cro' and i.address in syms: print('---',syms[i.address][:150])
        print(f"{i.address:08x} {i.mnemonic:8} {i.op_str:34} {c}")
def find_calls(sub):
    targets=[a for a,n in stubs.items() if sub in n]
    res=[]
    for off in range(0x180,segs[0][0]+segs[0][1],4):
        ins=struct.unpack_from('<I',m,off)[0]
        if (ins>>25)&7==5 and (ins>>28)!=0xF:
            o=ins&0xFFFFFF
            if o&0x800000:o-=0x1000000
            t=off+8+o*4
            if t in targets: res.append((off,dem(stubs[t])))
    return res
def calls_to(t):
    r=[]
    for off in range(0x180,segs[0][0]+segs[0][1],4):
        ins=struct.unpack_from('<I',m,off)[0]
        if (ins>>25)&7==5 and (ins>>28)!=0xF:
            o=ins&0xFFFFFF
            if o&0x800000:o-=0x1000000
            if off+8+o*4==t: r.append(off)
    return r
if __name__=='__main__':
    dis(int(sys.argv[2],16),int(sys.argv[3]),sys.argv[1])
