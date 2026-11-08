import struct
def decompress(d):
    assert d[0]==0x11
    size=d[1]|d[2]<<8|d[3]<<16; p=4
    if size==0: size=struct.unpack_from('<I',d,4)[0]; p=8
    out=bytearray()
    while len(out)<size:
        flags=d[p]; p+=1
        for b in range(8):
            if len(out)>=size: break
            if flags&(0x80>>b):
                b1=d[p]; ind=b1>>4
                if ind==0:
                    ln=(((b1&0xF)<<4)|(d[p+1]>>4))+0x11; disp=((d[p+1]&0xF)<<8|d[p+2])+1; p+=3
                elif ind==1:
                    ln=(((b1&0xF)<<12)|(d[p+1]<<4)|(d[p+2]>>4))+0x111; disp=((d[p+2]&0xF)<<8|d[p+3])+1; p+=4
                else:
                    ln=ind+1; disp=((b1&0xF)<<8|d[p+1])+1; p+=2
                for _ in range(ln): out.append(out[-disp])
            else:
                out.append(d[p]); p+=1
    return bytes(out)
