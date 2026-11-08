import struct
def fieldro_stubs(crs):
    d=open(crs,'rb').read(); v=struct.unpack_from('<45I',d,0x84)
    segs=[struct.unpack_from('<3I',d,v[17]+12*i) for i in range(v[18])]
    res={}
    for i in range(v[28]):
        no,io,inum,ao,anum=struct.unpack_from('<5I',d,v[27]+20*i)
        nm=d[no:d.index(b'\0',no)].decode()
        for j in range(anum):
            so2,bo=struct.unpack_from('<2I',d,ao+8*j)
            p=bo
            while True:
                so,typ,last,_,_,add=struct.unpack_from('<IBBBBI',d,p)
                loc=segs[so&0xF][0]+(so>>4)
                res[loc]=(nm,so2&0xF,(so2>>4)+add,typ)
                if last: break
                p+=12
    return res
