import sys, os, glob, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mix
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

def kind(h):
    if h[:4] == b'FORM' and h[8:12] == b'WVQA':
        return 'vqa'
    if h[:2] in (b'\x22\x56', b'\x11\x2b') and len(h) > 11 and h[11] in (1, 99):
        return 'aud'
    if h[:3] == b'F:' + bytes([92]):
        return 'audarc'
    return 'other'

if __name__ == '__main__':
    tot = collections.Counter(); sz = collections.Counter()
    for p in sorted(glob.glob('*.MIX') + glob.glob('DAT/*.MIX')):
        d, base, ents = mix.read_mix(p)
        c = collections.Counter()
        for i, o, s in ents:
            k = kind(d[base + o:base + o + 16])
            c[k] += 1; tot[k] += 1; sz[k] += s
        print(p, dict(c))
    print(tot, {k: v // 1000000 for k, v in sz.items()})
