# 訳を集めて DLL のデータ（dll/lol2jp.bin）を作る
#   python build.py          訳（translation/ja）から作る
#   python build.py --en     字幕を英文のまま入れる（試験用）
import os, sys, csv, re, unicodedata
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mix, mkdata, extract_ui
JP = os.path.join(HERE, '..')
T = os.path.join(JP, 'translation')
GAME = os.path.join(JP, '..')

NOSTART = set('、。，．・：；？！゛゜ヽヾゝゞ々ー）］｝」』〉》】〕’”…‥ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ〜～!?,.)')
NOEND = set('（［｛「『〈《【〔‘“(')

def to_euc(s):
    """日本語の文 → EUC-JP。半角カナ・JIS に無い字は近いものへ"""
    s = unicodedata.normalize('NFKC', s) if False else s
    rep = {'～': '〜', '―': '—', '－': '−', '･': '・', '“': '「', '”': '」', '‘': '『', '’': '』'}
    out = bytearray()
    for ch in s:
        ch = rep.get(ch, ch)
        try:
            out += ch.encode('euc_jp')
        except UnicodeEncodeError:
            out += b'?'
    return bytes(out)

def width(s):
    """表示幅（全角 2、半角 1）"""
    return sum(2 if ord(c) > 0x7f else 1 for c in s)

def wrap1(s, maxw):
    """全角換算 maxw/2 字で折り返す（行頭・行末の禁則あり）"""
    lines = []
    cur = ''
    for ch in s:
        if width(cur + ch) > maxw and cur:
            if ch in NOSTART:          # 行頭禁則: 句読点などは前の行にぶら下げ
                cur += ch
                continue
            while cur and cur[-1] in NOEND and len(cur) > 1:   # 行末禁則
                ch = cur[-1] + ch
                cur = cur[:-1]
            lines.append(cur)
            cur = ch.lstrip(' ')
        else:
            cur += ch
    if cur:
        lines.append(cur)
    return lines

def wrap(s, maxw):
    """行数を変えずに、各行がなるべく同じ長さになる幅で折り返す。区切りは句読点の後を好む"""
    lines = wrap1(s, maxw)
    if len(lines) <= 1:
        return lines
    n = len(lines)
    best = lines
    for w in range(maxw, 8, -2):
        t = wrap1(s, w)
        if len(t) > n:
            break
        best = t
    # 「、」「。」の後で切れる幅が近くにあれば、そちらを使う
    total = width(s)
    if n == 2:
        cands = [i + 1 for i, c in enumerate(s) if c in '、。！？…' and i + 1 < len(s)]
        good = [i for i in cands if abs(width(s[:i]) - total / 2) <= total * 0.2 and width(s[:i]) <= maxw and width(s[i:]) <= maxw]
        if good:
            i = min(good, key=lambda i: abs(width(s[:i]) - total / 2))
            return [s[:i], s[i:]]
    return best

def wrap_en(s, maxw):
    words = s.split(' ')
    lines, cur = [], ''
    for w in words:
        if cur and len(cur) + 1 + len(w) > maxw:
            lines.append(cur); cur = w
        else:
            cur = (cur + ' ' + w).strip()
    if cur:
        lines.append(cur)
    return lines

CMAP = []          # 画面の文字の日本語: 字の番号 → EUC
CIDX = {}

def to_game(s):
    """画面の文字 → ゲームに渡すバイト列。日本語は前 0x80-0x9F・後 0xA0-0xBF の 2 バイト（後に大文字・小文字のある字を使わない）、
    日本語どうしの間で折り返してよい所に半角空白（幅 0 の目印）を入れる"""
    rep = {'～': '〜', '―': '—', '－': '−', '･': '・'}
    out = bytearray()
    prev_j = None
    for ch in s:
        ch = rep.get(ch, ch)
        if ord(ch) < 0x80:
            out.append(ord(ch)); prev_j = None
            continue
        try:
            e = ch.encode('euc_jp')
        except UnicodeEncodeError:
            out.append(ord('?')); prev_j = None
            continue
        if len(e) != 2:
            out.append(ord('?')); prev_j = None
            continue
        code = e[0] << 8 | e[1]
        if code not in CIDX:
            CIDX[code] = len(CMAP); CMAP.append(code)
        i = CIDX[code]
        if prev_j is not None and ch not in NOSTART and prev_j not in NOEND:
            out.append(0x20)
        out += bytes([0x80 + i // 32, 0xA0 + i % 32])
        prev_j = ch
    if len(CMAP) > 32 * 32:
        raise SystemExit('字の種類が多すぎる')
    return bytes(out)

def load_dict(name):
    d = {}
    for l in open(os.path.join(T, name), encoding='utf-8'):
        l = l.rstrip('\n')
        if '\t' in l:
            a, b = l.split('\t', 1); d[a] = b
    return d

def load_ja():
    """translation/out/*.tsv（lid<TAB>ja）を全部読む"""
    d = {}
    jd = os.path.join(T, 'out')
    if not os.path.isdir(jd):
        return d
    for fn in sorted(os.listdir(jd)):
        if not fn.endswith('.tsv'):
            continue
        for line in open(os.path.join(jd, fn), encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line.startswith('#') or '\t' not in line:
                continue
            lid, ja = line.split('\t', 1)
            d[lid.strip()] = ja
    return d

def build_subs(ja, en_mode):
    """ja: 英文 → 訳"""
    subs = {}
    sizes = {}
    esize = {}
    n = miss = 0
    for line in open(os.path.join(JP, 'audio', 'manifest.tsv'), encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        if p[5] == 'vqa':       # 大きさは動画だけ渡す（DLL が飛ばしたのを見分けるのに使う）
            sizes[p[0]] = int(p[4])
    for r in csv.DictReader(open(os.path.join(T, 'en', 'subs.tsv'), encoding='utf-8'), delimiter='\t'):
        if en_mode:
            text = '\n'.join(wrap_en(r['en'], 70)).encode('ascii', 'replace')
        else:
            j = ja.get(r['en'])
            if j is None:
                miss += 1
                continue
            if j.startswith('#SKIP'):
                continue
            if r['key'].startswith('arc_998'):     # 物の名前は「。」なし
                j = j.rstrip('。.')
            text = '\n'.join(wrap(j, 76)[:3])
            text = to_euc(text)
        ent = subs.setdefault(r['mix'], {}).setdefault(int(r['off']), [])
        esize[(r['mix'], int(r['off']))] = sizes.get(r['key'], 0)
        start, end = int(r['start']), int(r['end'])
        end = max(end, start + 1200)        # 短すぎる区切りは 1.2 秒は出す
        ent.append((start, end, text))
        n += 1
    # 区切りが重ならないように
    out = {}
    for mixf, ents in subs.items():
        lst = []
        for off, segs in ents.items():
            segs.sort()
            for i in range(len(segs) - 1):
                if segs[i][1] > segs[i + 1][0]:
                    segs[i] = (segs[i][0], segs[i + 1][0], segs[i][2])
            lst.append((off, esize.get((mixf, off), 0), segs))
        out[mixf] = lst
    print('字幕', n, '訳が無い', miss)
    return out

def build_ui(ja):
    """訳した画面の文字 → 差し替える TRR"""
    loose, mixp = {}, []
    for name, wid, label in extract_ui.SOURCES:
        b = extract_ui.load_source(name, wid)
        ss = mix.trr_strings(b)
        out = []
        for sid, s in ss:
            en = extract_ui.esc(s)
            j = ja.get(en)
            if j and not j.startswith('#SKIP'):
                out.append((sid, to_game(extract_ui.unesc(j))))
            else:
                out.append((sid, s))
        t = mkdata.trr_build(out)
        if wid is None:
            loose[name] = t
        else:
            mixp.append((name, wid, t))
    return loose, mixp

def main():
    en_mode = '--en' in sys.argv
    import dict_from_out
    dict_from_out.main()
    loose, mixp = build_ui(load_dict('dict_ui.tsv'))
    subs = build_subs(load_dict('dict_subs.tsv'), en_mode)
    import struct
    mkdata.build(loose, mixp, subs, os.path.join(JP, 'dll', 'lol2jp.bin'),
                 extra=[('CMAP', struct.pack('<%dH' % len(CMAP), *CMAP))])
    print('画面の文字の字の種類', len(CMAP))

if __name__ == '__main__':
    main()
