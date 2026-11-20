"""translation/ja/*.tsv（id, en, ja）から DLL が読む lol_ja.dat を作る"""
import sys, os, glob, struct, re
sys.path.insert(0, os.path.dirname(__file__))
import textutil

FIX = {'～': '〜', '－': '−', '―': '—'}


def to_euc(s, where):
    out = bytearray()
    for ch in s:
        ch = FIX.get(ch, ch)
        try:
            out += ch.encode('euc_jp')
        except UnicodeEncodeError:
            raise SystemExit(f'EUC-JP にない字 {ch!r}: {where}')
    for b in out:
        if 0x80 <= b < 0xA1 and b != 0x8E:
            raise SystemExit(f'半角カナ等は使えない: {where}')
    return bytes(out)


def load(dirs):
    pairs = {}
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, '*.tsv'))):
            for ln, line in enumerate(open(f, encoding='utf-8'), 1):
                line = line.rstrip('\n')
                if ln == 1 and line.startswith(('id\t', 'key\t')):
                    continue
                cols = line.split('\t')
                if len(cols) < 3 or not cols[2]:
                    continue
                en = textutil.unesc(cols[1])
                ja = to_euc(textutil.unesc_str(cols[2]), f'{f}:{ln}')
                add(pairs, en, ja, f'{f}:{ln}')
                if cols[0].startswith(('LOLINTRO:', 'HISTORY:', 'LOLFINAL:')):
                    # 声の札 $NAME$ を外した形と、行ごとの形でも引けるようにする
                    m = re.match(rb'\$[^$]*\$', en)
                    if m:
                        en2, ja2 = en[m.end():], ja[m.end():] if ja.startswith(m.group()) else ja
                        add(pairs, en2, ja2, f'{f}:{ln}')
                    else:
                        en2, ja2 = en, ja
                    le, lj = en2.split(b'\r'), ja2.split(b'\r')
                    if len(le) > 1 and len(le) == len(lj):
                        for a, b in zip(le, lj):
                            if a.strip():
                                add(pairs, a, b, f'{f}:{ln}', quiet=True)
    return pairs


def add(pairs, en, ja, where, quiet=False):
    if en in pairs and pairs[en] != ja:
        if not quiet:
            print(f'注意: 同じ英文に別の訳 {where} {en[:40]!r}')
        return
    pairs[en] = ja


def write(pairs, path):
    ents = sorted(pairs.items(), key=lambda kv: textutil.fnv(kv[0]))
    pool = bytearray()
    idx = []
    base = 12 + 12 * len(ents)
    for en, ja in ents:
        eo = base + len(pool); pool += en + b'\0'
        jo = base + len(pool); pool += ja + b'\0'
        idx.append((textutil.fnv(en), eo, jo))
    out = bytearray(b'LOLJ' + struct.pack('<II', 1, len(ents)))
    for h, eo, jo in idx:
        out += struct.pack('<III', h, eo, jo)
    out += pool
    open(path, 'wb').write(out)
    print(f'{path}: {len(ents)} 件 {len(out)} バイト')


if __name__ == '__main__':
    out = sys.argv[1]
    write(load(sys.argv[2:] or ['translation/ja']), out)
