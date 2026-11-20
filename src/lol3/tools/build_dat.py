# 訳のデータ（日本語と英文の指紋だけ）→ out/LOL3JP/LOL3_JA.DAT
#   'L3JD' u32 版 u32 ファイル数
#   各ファイル: 名前[16]（ゲームが開く名前）, 書庫の番号 u32, 件数 u32
#     各件: 何番目 u32, 英文の指紋 u32（FNV-1a）, 長さ u16, 日本語（EUC-JP、終わりの 0 なし）
import os, sys, struct, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import extract
from trr import parse
from check_ja import load
from build_text import encode, REAL, NO_BREAK_FILES
W = os.path.join(HERE, '..')
VERSION = 1

def fnv(b):
    h = 2166136261
    for c in b: h = ((h ^ c) * 16777619) & 0xffffffff
    return h

def main():
    en, ja, _ = load()
    lids = json.load(open(os.path.join(W, 'translation', 'lids.json'), encoding='utf-8'))
    files = []
    for name, (wid, d) in sorted(extract.load_all().items()):
        if name in extract.SKIP: continue
        fn = REAL.get(name, name if name.endswith('.TRR') else None)
        if not fn: continue
        ents = []
        for k, (sid, t) in enumerate(parse(d)):
            e = extract.esc(t); lid = lids.get(e)
            if lid and lid in ja:
                j = encode(ja[lid], e, name not in NO_BREAK_FILES)
                if j != t: ents.append((k, fnv(t), j))
        files.append((fn, wid, ents))
    out = bytearray(b'L3JD' + struct.pack('<II', VERSION, len(files)))
    for fn, wid, ents in files:
        out += fn.encode().ljust(16, b'\0') + struct.pack('<II', wid, len(ents))
        for k, h, j in ents:
            out += struct.pack('<IIH', k, h, len(j)) + j
    os.makedirs(os.path.join(W, 'out', 'LOL3JP'), exist_ok=True)
    open(os.path.join(W, 'out', 'LOL3JP', 'LOL3_JA.DAT'), 'wb').write(out)
    print('LOL3_JA.DAT', len(out), 'バイト', len(files), 'ファイル', sum(len(e) for _, _, e in files), '件')

if __name__ == '__main__':
    main()
