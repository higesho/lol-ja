"""Borland TD 3.x デバッグ情報から記号を読む"""
import struct, sys
def syms(path):
    d = open(path, 'rb').read()
    h = struct.unpack_from('<14H', d, 0)
    size = (h[2]-1)*512 + h[1] if h[1] else h[2]*512
    b = size
    assert struct.unpack_from('<H', d, b)[0] == 0x52FB
    names_size = struct.unpack_from('<I', d, b+4)[0]
    nsym = struct.unpack_from('<H', d, b+14)[0]
    npool = d[len(d)-names_size:]
    names = [None] + npool.split(b'\0')
    out = []
    p = b + 0x50
    for i in range(nsym):
        ni, ty, off, seg, fl = struct.unpack_from('<HHHHB', d, p + i*9)
        out.append((names[ni].decode('latin-1') if ni < len(names) else '?', seg, off, fl, ty))
    return out
if __name__ == '__main__':
    for n, s, o, f, t in syms(sys.argv[1]):
        if len(sys.argv) < 3 or any(k.lower() in n.lower() for k in sys.argv[2:]):
            print(f'{s:04X}:{o:04X} fl={f:02X} {n}')
