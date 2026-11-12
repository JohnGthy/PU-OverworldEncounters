# Minimal GARC v6 (Gen 7) reader/writer for archives where every entry holds exactly one file.
import struct
def read(path):
    d=open(path,'rb').read()
    assert d[:4]==b'CRAG'
    hsz=struct.unpack_from('<I',d,4)[0]; data_off=struct.unpack_from('<I',d,0x10)[0]
    pad=struct.unpack_from('<I',d,0x20)[0] if hsz>=0x24 else 4
    o=hsz; assert d[o:o+4]==b'OTAF'
    fsz,cnt=struct.unpack_from('<IH',d,o+4); fato=struct.unpack_from('<%dI'%cnt,d,o+12)
    b=o+fsz; assert d[b:b+4]==b'BTAF'; base=b+12
    files=[]
    for i in range(cnt):
        p=base+fato[i]; vec=struct.unpack_from('<I',d,p)[0]; assert vec==1,(i,vec)
        st,en,ln=struct.unpack_from('<III',d,p+4)
        files.append(d[data_off+st:data_off+st+ln])
    return files,pad
def write(path,files,pad=4):
    cnt=len(files)
    fato=b''.join(struct.pack('<I',16*i) for i in range(cnt))
    otaf=b'OTAF'+struct.pack('<IHH',12+4*cnt,cnt,0xFFFF)+fato
    body=b''; fatb=b''
    for f in files:
        st=len(body); body+=f
        while len(body)%pad: body+=b'\xff'
        fatb+=struct.pack('<IIII',1,st,len(body),len(f))
    btaf=b'BTAF'+struct.pack('<II',12+len(fatb),cnt)+fatb
    bmif=b'BMIF'+struct.pack('<II',12,len(body))
    hdr_sz=0x24
    data_off=hdr_sz+len(otaf)+len(btaf)+len(bmif)
    largest=max(len(f) for f in files)
    largest_p=max((len(f)+pad-1)//pad*pad for f in files)
    hdr=b'CRAG'+struct.pack('<IHHIIIIII',hdr_sz,0xFEFF,0x0600,4,data_off,data_off+len(body),largest_p,largest,pad)
    open(path,'wb').write(hdr+otaf+btaf+bmif+body)
