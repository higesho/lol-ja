# Westwood FNT（4bpp）を読む
import struct, sys, collections

def load(b):
    size, comp, nblk, info, offb, widb, datb, heib = struct.unpack_from('<HBBHHHHH', b, 0)
    nchar = (widb - offb) // 2
    maxh = b[info + 4]; maxw = b[info + 5]
    glyphs = {}
    for c in range(nchar):
        off = struct.unpack_from('<H', b, offb + c * 2)[0]
        w = b[widb + c]
        yo, h = b[heib + c * 2], b[heib + c * 2 + 1]
        rows = []
        bw = (w + 1) // 2
        for y in range(h):
            row = []
            for x in range(w):
                v = b[off + y * bw + x // 2]
                row.append(v & 15 if x % 2 == 0 else v >> 4)
            rows.append(row)
        glyphs[c] = dict(w=w, yo=yo, h=h, rows=rows)
    return dict(nchar=nchar, maxh=maxh, maxw=maxw, glyphs=glyphs)

if __name__ == '__main__':
    f = load(open(sys.argv[1], 'rb').read())
    print('nchar', f['nchar'], 'maxh', f['maxh'], 'maxw', f['maxw'])
    cnt = collections.Counter()
    for c, g in f['glyphs'].items():
        for r in g['rows']:
            cnt.update(r)
    print(cnt)
    for ch in sys.argv[2:]:
        g = f['glyphs'][ord(ch)]
        print(ch, g['w'], g['yo'], g['h'])
        for y in range(g['yo']):
            print('.' * g['w'])
        for r in g['rows']:
            print(''.join('.' if v == 0 else '%x' % v for v in r))
