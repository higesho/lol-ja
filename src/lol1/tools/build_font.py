"""美咲ゴシック第2 (BDF, Unicode) → EUC-JP 順 94x94 の 8x8 表"""
import sys
src, dst = sys.argv[1], sys.argv[2]
glyphs = {}
cur = None
lines = open(src, encoding='latin-1').read().split('\n')
i = 0
while i < len(lines):
    l = lines[i]
    if l.startswith('ENCODING'):
        cur = int(l.split()[1])
    elif l.startswith('BBX'):
        bw, bh, bx, by = map(int, l.split()[1:])
    elif l == 'BITMAP':
        rows = []
        i += 1
        while lines[i] != 'ENDCHAR':
            rows.append(int(lines[i], 16)); i += 1
        # 8x8 セルに置く（ascent 7, descent 1 の前提: baseline=1）
        cell = [0]*8
        top = 8 - 1 - (by + bh)   # descent=1
        for r, v in enumerate(rows):
            y = top + 1 + r if False else (7 - by - bh) + r
            if 0 <= y < 8:
                cell[y] = (v >> (8 - bw)) << (8 - bw - bx) if bw <= 8 else v >> 8
        glyphs[cur] = bytes(c & 0xFF for c in cell)
    i += 1
out = bytearray(94*94*8)
miss = 0
for ku in range(94):
    for ten in range(94):
        b = bytes([0xA1+ku, 0xA1+ten])
        try:
            ch = b.decode('euc_jp')
        except UnicodeDecodeError:
            continue
        g = glyphs.get(ord(ch))
        if g is None:
            miss += 1; continue
        out[(ku*94+ten)*8:(ku*94+ten)*8+8] = g
open(dst, 'wb').write(out)
print('glyphs', len(glyphs), 'missing', miss)
# 確認用に「あ」「漢」を表示
for ch in 'あ漢ア、':
    e = ch.encode('euc_jp'); idx = ((e[0]-0xA1)*94 + e[1]-0xA1)*8
    for r in out[idx:idx+8]: print(''.join('#' if r & (0x80>>k) else '.' for k in range(8)))
    print()
