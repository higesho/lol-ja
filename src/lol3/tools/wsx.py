import struct, sys, zlib
def ra_id(name):
    name=name.upper().encode()
    i=0; idv=0
    while i<len(name):
        a=0
        for j in range(4):
            a>>=8
            if i<len(name): a|=name[i]<<24
            i+=1
        idv=((idv<<1)|(idv>>31))&0xffffffff
        idv=(idv+a)&0xffffffff
    return idv
def crc_id(name):
    n=name.upper().encode()
    l=len(n); a=l&~3
    if l&3:
        n+=bytes([l-a]); n+=bytes([n[a]])*(3-(l&3))
    return zlib.crc32(n)&0xffffffff
def read(path):
    f=open(path,'rb'); d=f.read(6)
    cnt,size=struct.unpack('<HI',d)
    ent=[struct.unpack('<III',f.read(12)) for _ in range(cnt)]
    base=6+12*cnt
    return {i:(base+o,s) for i,o,s in ent}, f
def lol3_id(name):
    v=0
    for c in name.upper().encode():
        v=((v>>6)|(v<<26))&0xffffffff
        v=(v+((c-0x30)&0x3f))&0xffffffff
    return v
