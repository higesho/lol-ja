import struct
def parse(d):
    n=struct.unpack_from('<I',d,0)[0]
    ids=struct.unpack_from('<%dI'%n,d,4)
    offs=struct.unpack_from('<%dI'%n,d,4+4*n)
    base=4
    out=[]
    for k in range(n):
        o=base+offs[k]; e=d.index(b'\0',o)
        out.append((ids[k],d[o:e]))
    return out

def build(items):
    """items: [(id, bytes)] → TRR（件数、番号、位置（先頭+4 から）、全体の大きさ-4、文）"""
    n = len(items)
    head = 4 + 8 * n + 4
    offs = []; body = b''
    for _, t in items:
        offs.append(head - 4 + len(body)); body += t + b'\0'
    total = head + len(body)
    out = struct.pack('<I', n) + struct.pack('<%dI' % n, *[i for i, _ in items]) + struct.pack('<%dI' % n, *offs)
    out += struct.pack('<I', total - 4) + body
    return out
