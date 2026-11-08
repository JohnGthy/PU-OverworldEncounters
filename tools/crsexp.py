import struct,sys,subprocess
def exports(path):
    d=open(path,'rb').read(); v=struct.unpack_from('<45I',d,0x84)
    segs=[struct.unpack_from('<3I',d,v[17]+12*i) for i in range(v[18])]
    out={}
    for i in range(v[20]):
        no,sr=struct.unpack_from('<2I',d,v[19]+8*i)
        out[d[no:d.index(b'\0',no)].decode()]=segs[sr&0xF][0]+(sr>>4)
    return out
