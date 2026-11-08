import struct
import os
def load_syms(path=os.path.join(os.environ.get('OWE_DUMP','dump'),'1.0','static_demangled.txt')):
    syms={}
    for l in open(path):
        p=l.split(' ',2); syms[int(p[1],16)&~1]=p[2].strip()
    return syms
def mangled_map():
    m={}
    for l in open('/tmp/claude-0/-home-claude/f9dd3f5c-c55a-52bb-b488-d546a261c9b7/scratchpad/static_exports.txt'):
        p=l.split(' ',2); m[p[2].strip()]=int(p[1],16)
    return m
def enc_bl(src,dst,cond=0xE,link=True):
    off=((dst-(src+8))>>2)&0xFFFFFF
    return (cond<<28)|(0xB if link else 0xA)<<24|off
def load_cro(path, base=0, exports=None):
    d=open(path,'rb').read()
    v=struct.unpack_from('<45I',d,0x84)
    segs=[struct.unpack_from('<3I',d,v[17]+12*i) for i in range(v[18])]
    mem=bytearray(d)
    # place bss at end
    bss_addr=len(mem); mem.extend(b'\0'*segs[3][1]) if len(segs)>3 else None
    segaddr=[s[0] for s in segs]
    if len(segs)>3: segaddr[3]=bss_addr
    if exports is None: exports=mangled_map()
    unresolved=[]
    def apply(loc,typ,val):
        if typ==2: struct.pack_into('<I',mem,loc,val&0xFFFFFFFF)
        elif typ==3: struct.pack_into('<I',mem,loc,(val-(loc+base))&0xFFFFFFFF)
        elif typ in (0x1c,0x1d):
            ins=struct.unpack_from('<I',mem,loc)[0]
            cond=ins>>28
            if typ==0x1c and (val&1):  # blx to thumb - ignore
                pass
            struct.pack_into('<I',mem,loc,enc_bl(loc+base,val&~1,cond if cond!=0xF else 0xE, typ==0x1c))
    imports={}
    def run_batch(boff,val,name):
        while True:
            so,typ,last,_,_,add=struct.unpack_from('<IBBBBI',d,boff)
            loc=segaddr[so&0xF]+(so>>4)
            apply(loc,typ,val+add)
            imports[loc+base]=name
            if last: break
            boff+=12
    for i in range(v[32]):
        no,bo=struct.unpack_from('<2I',d,v[31]+8*i)
        name=d[no:d.index(b'\0',no)].decode()
        if name in exports: run_batch(bo,exports[name],name)
        else: unresolved.append(name)
    # internal relocations
    for i in range(v[42]):
        so,typ,segi,_,_,add=struct.unpack_from('<IBBBBI',d,v[41]+12*i)
        loc=segaddr[so&0xF]+(so>>4)
        apply(loc,typ,base+segaddr[segi]+add)
    return bytes(mem),segs,segaddr,imports,unresolved,v,d
