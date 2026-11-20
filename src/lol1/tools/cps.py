"""Westwood CPS（LCW 圧縮）を 320x200 の画素列に戻す"""
import struct, sys


def lcw(src, size):
    out = bytearray(); i = 0
    while i < len(src) and len(out) < size:
        c = src[i]; i += 1
        if c == 0x80:
            break
        if not c & 0x80:  # 相対コピー
            n = (c >> 4) + 3; rel = ((c & 0x0F) << 8) | src[i]; i += 1
            p = len(out) - rel
            for k in range(n): out.append(out[p + k])
        elif not c & 0x40:  # そのまま
            n = c & 0x3F; out += src[i:i + n]; i += n
        else:
            if c == 0xFE:
                n, v = struct.unpack_from('<HB', src, i); i += 3; out += bytes([v]) * n
            elif c == 0xFF:
                n, p = struct.unpack_from('<HH', src, i); i += 4
                for k in range(n): out.append(out[p + k])
            else:
                n = (c & 0x3F) + 3; p, = struct.unpack_from('<H', src, i); i += 2
                for k in range(n): out.append(out[p + k])
    return bytes(out)


def read(path):
    d = open(path, 'rb').read()
    _, comp, usize, palsz = struct.unpack_from('<HHIH', d, 0)
    pal = d[10:10 + palsz]
    body = d[10 + palsz:]
    pix = lcw(body, usize) if comp == 4 else body[:usize]
    return pix, pal


if __name__ == '__main__':
    pix, pal = read(sys.argv[1])
    print(len(pix), 'palette', len(pal))
