# 声の書庫（.wsx の中の音声の束）を調べ、会話の行（devent の符号）と声の名前を対応させる
import sys, os, struct, re, collections, glob, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read, lol3_id
from trr import parse
G = os.path.join(HERE, '..', '..')

def aud_index(path):
    """書庫の中の音声の束から 名前 → (書庫, 項目番号, 位置, 大きさ)"""
    e, f = read(path)
    res = {}
    for i, (o, s) in e.items():
        if s < 0x300: continue
        f.seek(o); h = f.read(0x264)
        if b'AUDIO' not in h[:0x60].upper(): continue
        f.seek(o + 0x258)
        cnt, ixo, ixl = struct.unpack('<III', f.read(12))
        if cnt > 20000 or ixo > s: continue
        f.seek(o + ixo); ix = f.read(cnt * 60)
        for k in range(cnt):
            off, sz = struct.unpack_from('<II', ix, k * 60)
            n = ix[k * 60 + 16:k * 60 + 32].split(b'\0')[0].decode('latin1').lower()
            res[n] = (os.path.basename(path), i, o + off, sz)
    return res

def main():
    allv = {}
    for p in sorted(set(glob.glob(os.path.join(G, 'DAT', '*.wsx')) + glob.glob(os.path.join(G, 'DAT', '*.WSX')))):
        r = aud_index(p)
        dn = [n for n in r if re.match(r'da\d\d', n)]
        if r: print(os.path.basename(p), len(r), '声', len(dn), dn[:3])
        allv.update(r)
    e, f = read(os.path.join(G, 'DAT', 'locallng.wsx'))
    def get(n):
        oo, ss = e[lol3_id(n)]; f.seek(oo); return parse(f.read(ss))
    dev = get('DEVENT.TRR')
    suf = collections.defaultdict(list)
    for n in allv:
        m = re.match(r'da(\d\d)(.+)\.aud$', n)
        if m: suf[m.group(2)].append(n)
    hit = [c for _, c in dev if c.decode().lower() in suf]
    print('devent の符号', len(dev), '一致', len(hit), '候補が複数', sum(1 for c in hit if len(suf[c.decode().lower()]) > 1))
    print([(c, suf.get(c.decode().lower())) for _, c in dev[:8]])
    json.dump({n: list(v) for n, v in allv.items()}, open(os.path.join(HERE, '..', 'voices.json'), 'w'))

if __name__ == '__main__':
    main()
