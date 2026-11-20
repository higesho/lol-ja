# 声と会話の行を対応させ、字幕の表を作る → out/LOL3JP/LOL3_SUB.DAT
#   地方の書庫の声: 文字起こし（audio/transcript.jsonl）を会話の英文と照らし合わせる
#   声の束（locallng.wsx の中）: 名前 "da" + 2 桁 + devent の符号 から決まる
#   表: 'L3SB' u32 件数、各件 (WAV の頭 2048 バイトの指紋, data の頭 2048 バイトの指紋, 長さ ms, 行番号 u16, 0 u16)
import sys, os, re, json, struct, collections, difflib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read, lol3_id
from trr import parse
from voices import aud_index
W = os.path.join(HERE, '..'); G = os.path.join(W, '..')

def fnv(b):
    h = 2166136261
    for c in b: h = ((h ^ c) * 16777619) & 0xffffffff
    return h

def wav_info(b):
    """(WAV 全体の頭の指紋, data の頭の指紋, 長さ ms)"""
    ha = fnv(b[:2048])
    p = 12; data = None; rate = 0
    while p + 8 <= len(b):
        cid, sz = b[p:p + 4], struct.unpack_from('<I', b, p + 4)[0]
        if cid == b'fmt ': rate = struct.unpack_from('<I', b, p + 16)[0]   # 1 秒あたりのバイト数
        if cid == b'data': data = b[p + 8:p + 8 + sz]; break
        p += 8 + sz + (sz & 1)
    if data is None: return ha, 0, 0
    ms = int(len(data) * 1000 / rate) if rate else 0
    return ha, fnv(data[:2048] if len(data) >= 2048 else data), ms

def norm(s):
    s = s.lower().replace('’', "'")
    s = re.sub(r"[^a-z0-9' ]+", ' ', s)
    return ' '.join(s.split())

def main():
    e, f = read(os.path.join(G, 'DAT', 'locallng.wsx'))
    def get(n):
        oo, ss = e[lol3_id(n)]; f.seek(oo); return parse(f.read(ss))
    dev = [c.decode('latin1') for _, c in get('DEVENT.TRR')]
    dt = [t.decode('latin1') for _, t in get('DTEXT.TRR')]
    clean = []
    for t in dt:
        t = re.sub(r'[\x02\x03].', '', t).replace('\r', ' ')
        t = re.sub(r'^\s*[^:]{1,40}:\s*', '', t)        # 話し手
        t = re.sub(r'\(pause\)', ' ', t, flags=re.I)
        clean.append(norm(t))
    # 単語 → 行（照らし合わせの候補を絞る）
    inv = collections.defaultdict(set)
    df = collections.Counter()
    for i, t in enumerate(clean):
        for w in set(t.split()): df[w] += 1
    for i, t in enumerate(clean):
        for w in set(t.split()):
            if df[w] < 200: inv[w].add(i)
    ents = []
    # 1) 声の束（名前で決まる）
    bundle = aud_index(os.path.join(G, 'DAT', 'locallng.wsx'))
    code2idx = {}
    for i, c in enumerate(dev): code2idx.setdefault(c.lower(), i)
    bf = open(os.path.join(G, 'DAT', 'locallng.wsx'), 'rb')
    nb = 0
    for name, (arc, wid, off, sz) in bundle.items():
        m = re.match(r'da\d\d(.+)\.aud$', name)
        if not m or m.group(1) not in code2idx: continue
        bf.seek(off); b = bf.read(sz)
        ha, hb, ms = wav_info(b)
        ents.append((ha, hb, ms, code2idx[m.group(1)], 'bundle:' + name, 1.0)); nb += 1
    # 2) 地方の書庫（文字起こしで照らす）
    tr = [json.loads(l) for l in open(os.path.join(W, 'audio', 'transcript.jsonl'), encoding='utf-8')]
    ok = weak = none = 0
    rows = []
    for r in tr:
        t = norm(r['text'])
        if len(t) < 4: none += 1; continue
        cand = collections.Counter()
        for w in set(t.split()):
            for i in inv.get(w, ()): cand[i] += 1
        best = (0, -1)
        for i, _ in cand.most_common(40):
            sc = difflib.SequenceMatcher(None, t, clean[i], autojunk=False).ratio()
            if sc > best[0]: best = (sc, i)
        sc, i = best
        rows.append((r['key'], sc, i, r['text'], dt[i] if i >= 0 else ''))
        thr = 0.8 if len(t) < 20 else (0.7 if len(t) < 40 else 0.6)
        if sc < thr: none += 1; continue
        if sc < thr + 0.1: weak += 1
        ok += 1
        p = os.path.join(W, 'audio', 'wav', r['key'] + '.wav')
        b = open(p, 'rb').read()
        ha, hb, ms = wav_info(b)
        ents.append((ha, hb, ms or int(r['dur'] * 1000), i, r['key'], sc))
    print('声の束', nb, '地方の声: 当たり', ok, '（うち弱い', weak, '）外れ', none)
    with open(os.path.join(W, 'audio', 'match.tsv'), 'w', encoding='utf-8') as o:
        for k, sc, i, a, b in sorted(rows, key=lambda x: x[1]):
            o.write('%s\t%.2f\t%d\t%s\t%s\n' % (k, sc, i, a, b.replace('\r', ' ')))
    out = bytearray(b'L3SB' + struct.pack('<I', len(ents)))
    for ha, hb, ms, idx, _, _ in ents:
        out += struct.pack('<IIIHH', ha, hb, ms, idx, 0)
    open(os.path.join(W, 'out', 'LOL3JP', 'LOL3_SUB.DAT'), 'wb').write(out)
    cover = len(set(x[3] for x in ents))
    print('LOL3_SUB.DAT', len(ents), '件、字幕の付く行', cover, '/', len(dt))

if __name__ == '__main__':
    main()
