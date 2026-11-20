"""キャラ選び確認の Yes/No ボタン（BACKGRND.CPS に描き込み）の日本語版を作る
出力: DOSBOX/LOLJP に置く buttons.bin
  形式: 'LJBT' + 個数 u16、各ボタン: x u16, y u16, w u16, h u16, 英語の画素 w*h, 日本語の画素 w*h"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import cps

FONT = open('dll/out/jpfont8.bin' if os.path.exists('dll/out/jpfont8.bin') else '../DOSBOX/LOLJP/jpfont8.bin', 'rb').read()
YEL, SHD = 0xC0, 0x52


def glyph(ch):
    b = ch.encode('euc_jp')
    o = (((b[0] - 0xA1) * 94) + (b[1] - 0xA1)) * 8
    return FONT[o:o + 8]


def make(pix, x0, y0, w, h, text):
    img = [[pix[(y0 + y) * 320 + x0 + x] for x in range(w)] for y in range(h)]
    orig = [r[:] for r in img]
    # 文字（黄と影）を、同じ行の文字のない列の画素で埋める
    for y in range(1, h - 1):
        clean = [x for x in range(1, w - 1) if all(orig[yy][x] not in (YEL, SHD) for yy in range(1, h - 1))]
        for x in range(1, w - 1):
            if orig[y][x] in (YEL, SHD):
                img[y][x] = orig[y][clean[(x * 7) % len(clean)]]
    tw = 8 * len(text)
    tx = 1 + (w - 2 - tw + 1) // 2
    ty = 4
    on = set()
    for k, ch in enumerate(text):
        g = glyph(ch)
        for r in range(8):
            for c in range(8):
                if g[r] & (0x80 >> c):
                    on.add((tx + k * 8 + c, ty + r))
    for (x, y) in on:  # 影は左下
        for dx, dy in ((-1, 1),):
            p = (x + dx, y + dy)
            if p not in on and 1 <= p[0] < w - 1 and 1 <= p[1] < h - 1:
                img[p[1]][p[0]] = SHD
    for (x, y) in on:
        img[y][x] = YEL
    return bytes(sum(orig, [])), bytes(sum(img, []))


if __name__ == '__main__':
    pix, pal = cps.read('orig/pak/ENG_INTRO9.PAK/BACKGRND.CPS')
    out = bytearray(b'LJBT' + struct.pack('<H', 2))
    shots = []
    for x0, text in ((88, 'はい'), (196, 'いいえ')):
        en, ja = make(pix, x0, 180, 41, 15, text)
        out += struct.pack('<HHHH', x0, 180, 41, 15) + en + ja
        shots.append((x0, ja))
    open(sys.argv[1], 'wb').write(out)
    print(sys.argv[1], len(out), 'バイト')
    if len(sys.argv) > 2:
        from PIL import Image
        p2 = bytearray(pix)
        for x0, ja in shots:
            for y in range(15):
                p2[(180 + y) * 320 + x0:(180 + y) * 320 + x0 + 41] = ja[y * 41:(y + 1) * 41]
        im = Image.frombytes('P', (320, 200), bytes(p2)); im.putpalette(bytes(min(255, c * 4) for c in pal))
        im.crop((60, 160, 270, 200)).convert('RGB').resize((210 * 5, 40 * 5), Image.NEAREST).save(sys.argv[2])
