import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from lz11 import decompress
def load(fn):
    raw=open(fn,'rb').read(); return decompress(raw) if raw[0]==0x11 else raw
def unpack(d):
    n=struct.unpack_from('<H',d,2)[0]; offs=struct.unpack_from('<%dI'%(n+1),d,4)
    return [d[offs[i]:offs[i+1]] for i in range(n)]
