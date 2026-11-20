"""Westwood PAK (Lands of Lore) の読み書き"""
import struct, sys, os

def read_pak(data):
    ents = []
    p = 0
    while True:
        off = struct.unpack_from('<I', data, p)[0]
        e = data.index(b'\0', p + 4)
        name = data[p+4:e].decode('latin-1')
        p = e + 1
        if not name or off == 0:
            break
        ents.append([name, off])
    out = []
    for i, (n, o) in enumerate(ents):
        end = ents[i+1][1] if i + 1 < len(ents) else len(data)
        out.append((n, data[o:end]))
    return out

def write_pak(files):
    hdr = sum(4 + len(n) + 1 for n, _ in files) + 4 + 1
    out = bytearray()
    off = hdr
    for n, d in files:
        out += struct.pack('<I', off) + n.encode('latin-1') + b'\0'
        off += len(d)
    out += struct.pack('<I', 0) + b'\0'
    assert len(out) == hdr
    for n, d in files:
        out += d
    return bytes(out)

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    for root, _, fs in os.walk(src):
        for f in fs:
            if not f.upper().endswith('.PAK'):
                continue
            path = os.path.join(root, f)
            rel = os.path.relpath(path, src).replace(os.sep, '_')
            data = open(path, 'rb').read()
            try:
                files = read_pak(data)
            except Exception as ex:
                print('NG', rel, ex); continue
            od = os.path.join(dst, rel)
            os.makedirs(od, exist_ok=True)
            for n, d in files:
                open(os.path.join(od, n), 'wb').write(d)
            # 書き戻しで一致するか
            same = write_pak(files) == data
            print(rel, len(files), 'same' if same else 'DIFF')
