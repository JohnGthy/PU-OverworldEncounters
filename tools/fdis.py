import sys,struct,capstone
import os
TOOLS=os.path.dirname(os.path.abspath(__file__))
DUMP=os.environ.get('OWE_DUMP', os.path.join(TOOLS, '..', 'dump'))   # your own game dumps (see tools/README.md)
sys.path.insert(0,TOOLS); import croload
syms=croload.load_syms()
m,segs,sa,imp,unr,v,d=croload.load_cro(os.path.join(DUMP,'1.0','FieldRo.cro'))
code=open(os.path.join(DUMP,'1.0','code.bin'),'rb').read()
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM); md.skipdata=True
def name(t):
    if t in syms: return syms[t]
    return ''
def callers(target_name_sub):
    res=[]
    for loc,n in imp.items():
        if target_name_sub in n: res.append((loc,n))
    return sorted(res)
def dis(addr,n,src='cro'):
    buf = m if src=='cro' else code
    off = addr if src=='cro' else addr-0x100000
    for i in md.disasm(buf[off:off+n*4],addr):
        c=''
        if i.mnemonic.startswith('b') and i.op_str.startswith('#'):
            t=int(i.op_str[1:],16)
            c=name(t)[:120] if (src!='cro' or t>=0x100000 or t in stubs) else ('FR_'+hex(t))
        if src=='cro' and i.address in imp and not c: c='['+imp[i.address][:80]+']'
        if src!='cro' and i.address in syms: print('---',syms[i.address][:150])
        print(f"{i.address:08x} {i.mnemonic:8} {i.op_str:34} {c}")
stubs={}
for loc,n in imp.items():
    if loc>=4 and struct.unpack_from('<I',m,loc-4)[0]==0xE51FF004: stubs[loc-4]=n
import cxxfilt
def dem(n):
    try: return cxxfilt.demangle(n)
    except Exception: return n
_old_name=name
def name(t):
    if t in stubs: return '>'+dem(stubs[t])
    return _old_name(t)
def find_calls(sub):
    targets=[a for a,n in stubs.items() if sub in n]
    res=[]
    text_end=segs[0][0]+segs[0][1]
    for off in range(0x180,text_end,4):
        ins=struct.unpack_from('<I',m,off)[0]
        if (ins>>24)&0xF in (0xB,0xA) and (ins>>28)!=0xF:
            o=ins&0xFFFFFF
            if o&0x800000: o-=0x1000000
            t=off+8+o*4
            if t in targets: res.append((off,dem(stubs[t])))
    return res

if __name__=='__main__':
    import argparse
    if sys.argv[1]=='callers':
        import cxxfilt
        for loc,n in callers(sys.argv[2]): print(hex(loc),n)
    else:
        dis(int(sys.argv[2],16),int(sys.argv[3]),sys.argv[1])
