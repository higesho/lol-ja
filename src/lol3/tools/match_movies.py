# 動画の文字起こしの区切りを会話の行に当て、動画の字幕の表を作る → out/LOL3JP/LOL3_MOV.DAT
#   表: 'L3MV' u32 件数、各件 (動画名の指紋 u32（名前 "mintro01" を lol3_id）, 始め ms u32, 終わり ms u32, 行番号 u16, 0 u16)
import sys, os, re, json, struct, difflib, collections
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read, lol3_id
from trr import parse
W = os.path.join(HERE, '..'); G = os.path.join(W, '..')

# 照らし合わせで取り違えたもの（動画, 区切りの始め秒）→ 行
OVERRIDE = {
    ('mintro01', 3.0): [0, 1],      # Draracle: All is ready... / Place the Seal...
    ('mintro02', 3.7): 26,          # Frank the Guard: Lad!  Be still!  We thought you dead!
    ('mking', 51.6): 3283,    # King Richard: Jeron.
    ('mking', 195.4): 3306,   # King Richard: I suppose you're right.
    ('mking', 207.4): 3309,   # Jeron: Come along, I'll show you out.
    ('mking', 215.6): 3310,   # Jeron: You've had your meeting with the king, now get out!
}

def norm(s):
    return ' '.join(re.sub(r"[^a-z0-9' ]+", ' ', s.lower().replace('’', "'")).split())

def main():
    e, f = read(os.path.join(G, 'DAT', 'locallng.wsx'))
    o, s = e[lol3_id('DTEXT.TRR')]; f.seek(o)
    dt = [t.decode('latin1') for _, t in parse(f.read(s))]
    clean = [norm(re.sub(r'^\s*[^:]{1,40}:\s*', '', re.sub(r'[\x02\x03].', '', t).replace('\r', ' '))) for t in dt]
    inv = collections.defaultdict(set)
    for i, t in enumerate(clean):
        for w in set(t.split()): inv[w].add(i)
    T = json.load(open(os.path.join(W, 'audio', 'movie_transcript.json'), encoding='utf-8'))
    ents = []; rep = []
    for mv, segs in T.items():
        prev = -1
        for sg in segs:
            t = norm(sg['text'])
            if len(t) < 3: continue
            cand = collections.Counter()
            for w in set(t.split()):
                if len(inv.get(w, ())) < 300:
                    for i in inv.get(w, ()): cand[i] += 1
            best = (0, -1)
            near = [prev + d for d in range(0, 5)] if prev >= 0 else []   # 動画の台詞はほぼ行の順
            for i in [i for i, _ in cand.most_common(60)] + [i for i in near if i < len(clean)]:
                c = clean[i]
                # 区切りが行の一部のこともあるので、行の中の同じ長さの所と比べる
                sm = difflib.SequenceMatcher(None, t, c, autojunk=False)
                sc = sm.ratio()
                if len(c) > len(t) * 1.3:
                    blocks = sm.get_matching_blocks()
                    m = sum(b.size for b in blocks)
                    sc = max(sc, m / max(len(t), 1) * 0.95)
                if i == prev: sc += 0.05            # 前の区切りと同じ行は少し優先
                elif prev >= 0 and prev < i <= prev + 4: sc += 0.08   # すぐ後の行も優先
                if sc > best[0]: best = (sc, i)
            sc, i = best
            ov = OVERRIDE.get((mv, sg['start']))
            if isinstance(ov, list):       # 1 つの区切りに複数の行: 時間を等分する
                st, en = int(sg['start'] * 1000), int(sg['end'] * 1000) + 300
                step = (en - st) // len(ov)
                for k, j in enumerate(ov):
                    ents.append([mv, st + step * k, st + step * (k + 1), j])
                rep.append((mv, sg['start'], 2.0, ov[-1], sg['text'], dt[ov[-1]][:80]))
                prev = ov[-1]
                continue
            if ov is not None: sc, i = 2.0, ov
            ok = sc >= (0.8 if len(t) < 20 else 0.65)
            rep.append((mv, sg['start'], round(sc, 2), i if ok else -1, sg['text'], dt[i][:80] if i >= 0 else ''))
            if not ok: continue
            st, en = int(sg['start'] * 1000), int(sg['end'] * 1000) + 300
            if ents and ents[-1][0] == mv and ents[-1][3] == i and st - ents[-1][2] < 2500:
                ents[-1][2] = en            # 同じ行の続き
            else:
                ents.append([mv, st, en, i])
            prev = i
    with open(os.path.join(W, 'audio', 'movie_match.tsv'), 'w', encoding='utf-8') as o:
        for r in rep: o.write('%s\t%.1f\t%.2f\t%d\t%s\t%s\n' % (r[0], r[1], r[2], r[3], r[4], r[5].replace('\r', ' ')))
    out = bytearray(b'L3MV' + struct.pack('<I', len(ents)))
    for mv, st, en, i in ents:
        out += struct.pack('<IIIHH', lol3_id(mv), st, en, i, 0)
    open(os.path.join(W, 'out', 'LOL3JP', 'LOL3_MOV.DAT'), 'wb').write(out)
    print('区切り', len(rep), '当たり', sum(1 for r in rep if r[3] >= 0), '字幕', len(ents), '件')

if __name__ == '__main__':
    main()
