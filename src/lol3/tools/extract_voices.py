# 地方の書庫（*s.wsx）の声（WAV）を取り出す → audio/wav/<書庫>_<番号>.wav
import sys, os, glob, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read
G = os.path.join(HERE, '..', '..'); OUT = os.path.join(HERE, '..', 'audio', 'wav')

def main():
    os.makedirs(OUT, exist_ok=True)
    seen = set(); n = 0
    for p in sorted(glob.glob(os.path.join(G, 'DAT', '*'))):
        b = os.path.basename(p)
        if not b.lower().endswith('s.wsx') or b.lower() in seen: continue
        seen.add(b.lower())
        e, f = read(p)
        for i, (o, s) in e.items():
            f.seek(o); h = f.read(12)
            if h[:4] != b'RIFF' or h[8:12] != b'WAVE': continue
            fn = os.path.join(OUT, '%s_%08x.wav' % (b[:-4].upper(), i))
            if not os.path.exists(fn):
                f.seek(o); open(fn, 'wb').write(f.read(s))
            n += 1
    print('声', n)

if __name__ == '__main__':
    main()
