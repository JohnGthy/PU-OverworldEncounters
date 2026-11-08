import struct
al=lambda x:(x+63)&~63
def cia_contents(path):
    f=open(path,'rb'); h=f.read(0x20)
    hs,typ,ver,cs,ts,tms,ms,csz=struct.unpack('<IHHIIIIQ',h)
    off=al(hs)+al(cs)+al(ts)
    f.seek(off); tmd=f.read(tms)
    # TMD: sig type
    st=struct.unpack('>I',tmd[:4])[0]; sigsz={0x10004:0x100+0x3c,0x10005:0x3c+0x3c,0x10003:0x200+0x3c,0x10000:0x200+0x3c,0x10001:0x100+0x3c}[st]
    th=4+sigsz; ncont=struct.unpack('>H',tmd[th+0x9E:th+0xA0])[0]
    ci=th+0xC4+64*0x24
    cont=[]; p=al(off+tms)
    for i in range(ncont):
        cid,idx,ctype,size=struct.unpack('>IHHQ',tmd[ci+i*0x30:ci+i*0x30+16]); cont.append((cid,idx,ctype,size,p)); p+=al(size) if False else size
    return f,cont
def ncch_parts(f,base):
    f.seek(base); h=f.read(0x200); assert h[0x100:0x104]==b'NCCH',h[0x100:0x104]
    flags=h[0x188:0x190]
    exefs_off,exefs_sz=struct.unpack_from('<II',h,0x1A0); romfs_off,romfs_sz=struct.unpack_from('<II',h,0x1B0)
    return dict(flags=flags.hex(),exefs=(base+exefs_off*0x200,exefs_sz*0x200),romfs=(base+romfs_off*0x200,romfs_sz*0x200),exh=base+0x200)
def exefs_files(f,off):
    f.seek(off); h=f.read(0x200); out={}
    for i in range(10):
        n,o,s=struct.unpack_from('<8sII',h,i*16)
        if s: f.seek(off+0x200+o); out[n.rstrip(b'\0').decode()]=f.read(s)
    return out
def blz_decompress(cmp):
    # 3DS .code backward LZ
    buf_top,orig=struct.unpack_from('<II',cmp,len(cmp)-8)
    enc_len=buf_top&0xFFFFFF; hdr_len=buf_top>>24
    out=bytearray(cmp)+bytearray(orig)
    src=len(cmp)-hdr_len; dst=len(out); end=len(cmp)-enc_len
    while src>end:
        src-=1; flags=out[src]
        for b in range(8):
            if src<=end: break
            if flags&0x80:
                src-=2; v=out[src]|out[src+1]<<8
                ln=(v>>12)+3; disp=(v&0xFFF)+3
                for _ in range(ln):
                    dst-=1; out[dst]=out[dst+disp]
            else:
                src-=1; dst-=1; out[dst]=out[src]
            flags=(flags<<1)&0xFF
            if dst<=end: break
    return bytes(out)
def romfs_walk(f,romfs,prefix=''):
    f.seek(romfs); hdr=f.read(0x60); assert hdr[:4]==b'IVFC'
    master=struct.unpack_from('<I',hdr,8)[0]; bs=1<<struct.unpack_from('<I',hdr,0x4C)[0]
    lv3=romfs+((0x60+master+bs-1)//bs)*bs
    f.seek(lv3); h=struct.unpack('<10I',f.read(0x28))
    f.seek(lv3+h[3]); dt=f.read(h[4]); f.seek(lv3+h[7]); ft=f.read(h[8]); data=lv3+h[9]
    res={}
    def walk(off,path):
        _,_,child,ff,_,_=struct.unpack_from('<6I',dt,off)
        fo=ff
        while fo!=0xFFFFFFFF:
            _,sib,doff,dsz,_,nl=struct.unpack_from('<IIQQII',ft,fo)
            nm=ft[fo+0x20:fo+0x20+nl].decode('utf-16le'); res[(path+'/'+nm).lstrip('/')]=(data+doff,dsz); fo=sib
        co=child
        while co!=0xFFFFFFFF:
            _,sib,_,_,_,nl=struct.unpack_from('<6I',dt,co); nm=dt[co+0x18:co+0x18+nl].decode('utf-16le'); walk(co,path+'/'+nm); co=sib
    walk(0,''); return res
