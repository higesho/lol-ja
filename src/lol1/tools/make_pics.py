"""絵に描き込まれた英語（勇者の肖像の巻物、灰色鷲亭の看板）の日本語版を作る
出力 pics.bin: 'LJPC' + 個数 u16、各: w u16, h u16, 英語の画素 w*h, 日本語の画素 w*h
DLL はメモリ上（320 幅の頁）と画面で英語の画素の塊を探して日本語に替える"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import cps

FONT = open('../DOSBOX/LOLJP/jpfont8.bin', 'rb').read()


def glyph(ch):
    b = ch.encode('euc_jp')
    o = (((b[0] - 0xA1) * 94) + (b[1] - 0xA1)) * 8
    return FONT[o:o + 8]


def draw(img, x0, y0, text, col, shadow=None, sdx=1, sdy=1):
    on = set()
    for k, ch in enumerate(text):
        g = glyph(ch)
        for r in range(8):
            for c in range(8):
                if g[r] & (0x80 >> c):
                    on.add((x0 + k * 8 + c, y0 + r))
    if shadow is not None:
        for x, y in on:
            p = (x + sdx, y + sdy)
            if p not in on: img[p[1]][p[0]] = shadow
    for x, y in on:
        img[y][x] = col


def scroll(name, ja):
    pix, pal = cps.read(f'orig/pak/ENG_FINALE1.PAK/{name}.CPS')
    img = [list(pix[y * 320:(y + 1) * 320]) for y in range(200)]
    PARCH = (0x83, 0x84, 0x85)
    INK = range(0x8C, 0x96)
    # 巻物の上下の縁はインクの横線（200 画素ほど）。その間が字の行
    def run(y):
        row, best, x = img[y], (0, 0), 0
        while x < 320:
            if row[x] in PARCH or row[x] in INK:
                s0 = x
                while x < 320 and (row[x] in PARCH or row[x] in INK): x += 1
                if x - s0 > best[1] - best[0]: best = (s0, x)
            x += 1
        return best
    def inks(y):
        xs, xe = run(y)
        return sum(1 for v in img[y][xs:xe] if v in INK)
    yt = next(y for y in range(150, 200) if inks(y) > 150)
    yb = next(y for y in range(yt + 4, 200) if inks(y) > 150)
    xs, xe = run(yt - 1)
    for y in range(yt + 1, yb):
        last = 0x84
        for x in range(xs, xe):
            v = img[y][x]
            if v in INK:
                img[y][x] = last
            elif v in PARCH:
                last = v
    tw = 8 * len(ja)
    tx = xs + (xe - xs - tw) // 2
    draw(img, tx, yt + 1 + (yb - yt - 1 - 8) // 2 + 1, ja, 0x90)
    bx0, by0, bx1, by1 = xs, yt + 1, xe, yb
    en = bytes(pix[y * 320 + x] for y in range(by0, by1) for x in range(bx0, bx1))
    jb = bytes(img[y][x] for y in range(by0, by1) for x in range(bx0, bx1))
    return (bx1 - bx0, by1 - by0, en, jb), img, pal


def sign():
    pix, pal = cps.read('orig/pak/O03B.PAK/GREYEAGL.CPS')
    img = [list(pix[y * 320:(y + 1) * 320]) for y in range(200)]
    STONE = [0x12, 0x13, 0x12, 0x11, 0x13, 0x12, 0x14, 0x13]
    fx0, fx1, fy0, fy1 = 27, 46, 68, 94
    for y in range(fy0, fy1 + 1):
        for x in range(fx0, fx1 + 1):
            img[y][x] = STONE[(x * 7 + y * 13 + (x * y) % 5) % len(STONE)]
    # 灰色／鷲亭 を彫った字（暗い字、右下に明るい縁）
    draw(img, 29, 72, '灰色', 0x39, 0x14)
    draw(img, 29, 83, '鷲亭', 0x39, 0x14)
    bx0, by0, bx1, by1 = 24, 62, 50, 98
    en = bytes(pix[y * 320 + x] for y in range(by0, by1) for x in range(bx0, bx1))
    jb = bytes(img[y][x] for y in range(by0, by1) for x in range(bx0, bx1))
    return (bx1 - bx0, by1 - by0, en, jb), img, pal


if __name__ == '__main__':
    from PIL import Image
    items, shots = [], []
    for n, ja in (("AK'SHEL", 'アクシェル、王国の英雄。'), ('CONRAD', 'コンラッド、王国の英雄。'),
                  ('KIERAN', 'キーラン、王国の英雄。'), ('MICHAEL', 'マイケル、王国の英雄。')):
        it, img, pal = scroll(n, ja); items.append(it); shots.append((img, pal, (0, 160, 320, 200)))
    it, img, pal = sign(); items.append(it); shots.append((img, pal, (0, 40, 80, 120)))
    out = bytearray(b'LJPC' + struct.pack('<H', len(items)))
    for w, h, en, jb in items:
        out += struct.pack('<HH', w, h) + en + jb
    open(sys.argv[1], 'wb').write(out)
    print(sys.argv[1], len(out), 'バイト', [(w, h) for w, h, _, _ in items])
    if len(sys.argv) > 2:
        sheet = Image.new('RGB', (960, 5 * 130), (40, 40, 40))
        for k, (img, pal, box) in enumerate(shots):
            im = Image.frombytes('P', (320, 200), bytes(sum(img, []))); im.putpalette(bytes(min(255, c * 4) for c in pal))
            c = im.convert('RGB').crop(box)
            c = c.resize((c.width * 3, c.height * 3), Image.NEAREST)
            sheet.paste(c.crop((0, 0, 960, 120)) if k < 4 else c, (0, k * 130))
        sheet.save(sys.argv[2])
