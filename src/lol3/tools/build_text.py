# 訳から日本語の TRR を組み立てる → out/LOL3JP/*.TRR
#   日本語は EUC-JP。日本語の字と字の間には折り返し用の空白（0x20）を入れる（DLL が幅 0 で扱う）。
import os, sys, re, csv, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import extract
from trr import parse, build
from check_ja import load
W = os.path.join(HERE, '..')
OUT = os.path.join(W, 'out', 'LOL3JP')

# 分かった名前（今の本体は読まないが、念のため置く）
REAL = {'X_CREDVID': 'CREDITS.TRR', 'X_WARN': 'ERRTEXT.TRR', 'X_SPELLA': 'JOURNAL.TRR', 'X_KEYS': 'OPTTEXT.TRR'}

# 表記の統一（訳を集めた後に当てる）
UNIFY = [
    ('エリック{92}sの亡霊', 'エリックの亡霊'),
    ('ドンナ', 'ドナ'), ('キエラン', 'キーラン'), ('キティヤラ', 'キトヤラ'), ('シャッラ', 'シャラ'),
    ('アニアー', 'アニャール'), ('南大陸', '南の大陸'), ('カバルの戦士ソラン', 'ソラン（カバルの戦士）'),
]
UNIFY_RE = [(re.compile('ラシャー(?!ル)'), 'ラシャール')]

# 行頭に来てはいけない字（この前には空白を入れない）／行末に来てはいけない字（この後には入れない）
NO_HEAD = set('、。，．・：；？！゛゜ヽヾゝゞ々ー）〕］｝〉》」』】’”…‥ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ～')
NO_TAIL = set('（〔［｛〈《「『【‘“')

KATA = re.compile('[ァ-ヺー・]')
def is_kata(ch):
    return bool(KATA.match(ch))

def is_jp(ch):
    return ord(ch) >= 0x3000 or 0xff00 <= ord(ch) <= 0xffef

def unesc(s):
    B = chr(92)
    s = re.sub(r'\{([0-9A-F]{2})\}', lambda m: chr(0xE000 + int(m.group(1), 16)), s)  # 一時的に私用領域へ
    return s.replace(B + 'r', '\r')

NO_BREAK_FILES = {'F_OPTION.TRR', 'TOOLTIP.TRR', 'F_AUTOMP.TRR', 'X_KEYS'}

def encode(s, en, brk=True):
    """訳（エスケープ付き）→ ゲームに渡すバイト列"""
    for a, b in UNIFY: s = s.replace(a, b)
    for r, b in UNIFY_RE: s = r.sub(b, s)
    if 'Ssar' in en: s = re.sub('(?<!ス)サール', 'スサール', s)
    s = s.replace('{85}', '……')
    t = unesc(s)
    # % の直後が日本語なら空白をはさむ（% と次の 1 字を印として読む所があるため）
    t = re.sub(r'%(?=[^\x00-\x7f-])', '% ', t)
    out = bytearray()
    prev = None
    for ch in t:
        o = ord(ch)
        if 0xE000 <= o <= 0xE0FF:
            out.append(o - 0xE000); prev = None; continue
        if is_jp(ch):
            if brk and prev is not None and is_jp(prev) and ch not in NO_HEAD and prev not in NO_TAIL                     and not (is_kata(prev) and is_kata(ch)):
                out.append(0x20)
            out += ch.encode('euc_jp')
        elif o < 0x80:
            out.append(o)
        else:
            # 全角でない記号（「×」「÷」「°」など）は EUC-JP にあれば使う
            out += ch.encode('euc_jp')
        prev = ch
    return bytes(out)

def main():
    en, ja, _ = load()
    lids = json.load(open(os.path.join(W, 'translation', 'lids.json'), encoding='utf-8'))
    os.makedirs(OUT, exist_ok=True)
    allfiles = extract.load_all()
    n_tr = n_all = 0
    for name, (i, d) in sorted(allfiles.items()):
        if name in extract.SKIP: continue
        items = []
        for sid, t in parse(d):
            n_all += 1
            e = extract.esc(t)
            lid = lids.get(e)
            if lid and lid in ja:
                items.append((sid, encode(ja[lid], e, name not in NO_BREAK_FILES))); n_tr += 1
            else:
                items.append((sid, t))
        fn = REAL.get(name, name if name.endswith('.TRR') else None)
        if not fn: continue
        open(os.path.join(OUT, fn), 'wb').write(build(items))
    print('組み立て', n_tr, '/', n_all, '件 →', OUT)

if __name__ == '__main__':
    main()
