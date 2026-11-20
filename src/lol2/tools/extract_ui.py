# 画面の文字（TRR）を translation/en/ui.tsv に取り出す
#   src: ファイル名か MIX:id、sid: 文字列の id、en: 英文（制御文字は {0d} のように書く）
import os, sys, csv, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mix
GAME = os.path.join(HERE, '..', '..')
OUT = os.path.join(HERE, '..', 'translation', 'en')

SOURCES = [
    ('OPTTEXT.TRR', None, None),
    ('ERRTEXT.TRR', None, None),
    ('CREDITS.TRR', None, None),
    ('LOCALLNG.MIX', 0xae4c441b, 'OPTTEXT'),
    ('LOCALLNG.MIX', 0x3c33f3f7, 'HELPTEXT'),
    ('LOCALLNG.MIX', 0x9a4429f3, 'MAPTEXT'),
    ('LOCALLNG.MIX', 0x6e0e43f3, 'CREDITS'),
    ('DAT/MOVIES.MIX', 0x6e0e43f3, 'CREDITS'),
]

def esc(b):
    out = []
    for c in b:
        if c == 0x0d:
            out.append('\\r')
        elif c == 0x0a:
            out.append('\\n')
        elif c < 0x20 or c >= 0x7f:
            out.append('{%02x}' % c)
        else:
            out.append(chr(c))
    return ''.join(out)

def unesc(s):
    import re
    s = s.replace('\\r', '\r').replace('\\n', '\n')
    return re.sub(r'\{([0-9a-f]{2})\}', lambda m: chr(int(m.group(1), 16)), s)

def load_source(name, wid):
    if wid is None:
        return open(os.path.join(GAME, name), 'rb').read()
    return mix.get(os.path.join(GAME, name), wid)

def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for name, wid, label in SOURCES:
        b = load_source(name, wid)
        src = name if wid is None else '%s:%08x' % (name, wid)
        for sid, s in mix.trr_strings(b):
            rows.append((src, sid, esc(s)))
    with open(os.path.join(OUT, 'ui.tsv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n', quoting=csv.QUOTE_NONE, escapechar=None)
        w.writerow(['src', 'sid', 'en'])
        for r in rows:
            w.writerow(r)
    uniq = collections.OrderedDict()
    for src, sid, en in rows:
        if en.strip() and en not in uniq:
            uniq[en] = src
    print('rows', len(rows), 'unique', len(uniq), 'chars', sum(len(x) for x in uniq))

if __name__ == '__main__':
    main()
