import struct, sys, os

def read_mix(path):
    d = open(path, 'rb').read()
    n, size = struct.unpack_from('<HI', d, 0)
    ents = []
    for i in range(n):
        id_, off, sz = struct.unpack_from('<III', d, 6 + i * 12)
        ents.append((id_, off, sz))
    base = 6 + n * 12
    return d, base, ents

def get(path, wid):
    d, base, ents = read_mix(path)
    for i, o, s in ents:
        if i == wid:
            return d[base + o:base + o + s]
    return None

def is_trr(b):
    if len(b) < 8:
        return False
    n = struct.unpack_from('<I', b, 0)[0]
    if n == 0 or n > 100000 or 4 + n * 8 > len(b):
        return False
    offs = struct.unpack_from('<%dI' % n, b, 4 + n * 4)
    if offs[0] != 4 + n * 8:
        return False
    return all(offs[i] <= offs[i + 1] for i in range(n - 1)) and offs[-1] < len(b)

def trr_strings(b):
    n = struct.unpack_from('<I', b, 0)[0]
    ids = struct.unpack_from('<%dI' % n, b, 4)
    offs = struct.unpack_from('<%dI' % n, b, 4 + n * 4)
    out = []
    for k in range(n):
        e = b.index(b'\x00', offs[k] + 4)
        out.append((ids[k], b[offs[k] + 4:e]))
    return out

if __name__ == '__main__':
    for p in sys.argv[1:]:
        d, base, ents = read_mix(p)
        for id_, off, sz in ents:
            b = d[base + off:base + off + sz]
            if is_trr(b):
                n = struct.unpack_from('<I', b, 0)[0]
                print(os.path.basename(p), '%08x' % id_, sz, n)
