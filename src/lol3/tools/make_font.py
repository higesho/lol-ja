# 日本語の字体を EUC-JP の並びの表にする → out/LOL3JP/JPFONT.BIN
#   頭: 'L3JF', 大きさの数 n、各 (幅, 高さ, 1 行のバイト数, 0, 位置 u32)
#   各大きさ: 94×94 字、1 字は 高さ×(1 行のバイト数)、左上が最上位ビット
import os, struct
HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, '..')
FONTS = [('misaki_gothic_2nd.bdf', 'uni'), ('shnmk12.bdf', 'jis'), ('shnmk14.bdf', 'jis'), ('shnmk16.bdf', 'jis')]

def read_bdf(path):
    g = {}; fbb = None; cur = None; bits = None
    for line in open(path, encoding='latin1'):
        p = line.split()
        if not p: continue
        if p[0] == 'FONTBOUNDINGBOX': fbb = list(map(int, p[1:5]))
        elif p[0] == 'ENCODING': cur = {'enc': int(p[1])}
        elif p[0] == 'BBX': cur['bbx'] = list(map(int, p[1:5]))
        elif p[0] == 'BITMAP': bits = []
        elif p[0] == 'ENDCHAR':
            cur['bits'] = bits; g[cur['enc']] = cur; bits = None
        elif bits is not None: bits.append(int(p[0], 16))
    return fbb, g

def main():
    out = bytearray(b'L3JF' + struct.pack('<I', len(FONTS)))
    hdr_at = len(out); out += b'\0' * (8 * len(FONTS))
    for n, (fn, kind) in enumerate(FONTS):
        fbb, g = read_bdf(os.path.join(W, 'font', fn))
        fw, fh, fx, fy = fbb
        bpr = (fw + 7) // 8
        table = bytearray(94 * 94 * fh * bpr)
        cnt = 0
        for enc, c in g.items():
            if kind == 'uni':
                try: e = chr(enc).encode('euc_jp')
                except UnicodeEncodeError: continue
                if len(e) != 2 or e[0] < 0xA1: continue
                hi, lo = e[0], e[1]
            else:
                hi, lo = (enc >> 8) | 0x80, (enc & 0xff) | 0x80
            if not (0xA1 <= hi <= 0xFE and 0xA1 <= lo <= 0xFE): continue
            idx = (hi - 0xA1) * 94 + (lo - 0xA1)
            bw, bh, bx, by = c['bbx']
            # 字の枠の中での位置（上端からの行）
            top = (fh + fy) - (bh + by)
            cbpr = (bw + 7) // 8
            for r, v in enumerate(c['bits']):
                y = top + r
                if not (0 <= y < fh): continue
                v <<= (bpr * 8 - cbpr * 8)  # 左寄せ
                v >>= max(0, bx - fx)
                row = v.to_bytes(bpr, 'big') if v < (1 << (bpr * 8)) else (v & ((1 << (bpr * 8)) - 1)).to_bytes(bpr, 'big')
                o = (idx * fh + y) * bpr
                table[o:o + bpr] = row
            cnt += 1
        struct.pack_into('<BBBBI', out, hdr_at + 8 * n, fw, fh, bpr, 0, len(out))
        out += table
        print(fn, fw, 'x', fh, cnt, '字')
    os.makedirs(os.path.join(W, 'out', 'LOL3JP'), exist_ok=True)
    open(os.path.join(W, 'out', 'LOL3JP', 'JPFONT.BIN'), 'wb').write(out)
    print('JPFONT.BIN', len(out))

if __name__ == '__main__':
    main()
