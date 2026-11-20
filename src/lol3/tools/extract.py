# locallng.wsx の .trr から英文を抜き出す → translation/en/text.tsv
import os, sys, re, csv
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read, lol3_id
from trr import parse
GAME = os.path.join(HERE, '..', '..')
NAMES = {}
for n in ['devent.trr','dtext.trr','f_automp.trr','f_option.trr','help.trr','itemjt.trr','monsshps.trr','monster.trr',
          'pharma.trr','pophelp.trr','prompt.trr','qevent.trr','qtext.trr','skills.trr','skilshp2.trr','skilshp3.trr',
          'skilshp4.trr','skilshps.trr','tooltip.trr']:
    NAMES[lol3_id(n)] = n.upper()
# 名前の分からないもの
NAMES.update({0x1f652ce4:'X_CREDVID', 0xe3722e13:'X_WARN', 0x91650270:'X_CREDITS', 0x72f51a08:'X_SPELLA',
              0xe37426b3:'X_KEYS', 0x516525bc:'X_LANG', 0x516526bc:'X_SPELLB'})
SKIP = {'DEVENT.TRR','QEVENT.TRR','MONSSHPS.TRR','PHARMA.TRR','SKILSHP2.TRR','SKILSHP3.TRR','SKILSHP4.TRR','SKILSHPS.TRR','X_LANG'}

BS = chr(92); CR = chr(13)

def esc(b):
    s = b.decode('latin1')
    out = []
    for ch in s:
        o = ord(ch)
        if ch == CR: out.append(BS + 'r')
        elif ch == BS: out.append(BS + BS)
        elif o < 0x20 or o >= 0x7f: out.append('{%02X}' % o)
        else: out.append(ch)
    return ''.join(out)

def unesc(s):
    s = re.sub(r'\{([0-9A-F]{2})\}', lambda m: chr(int(m.group(1), 16)), s)
    s = s.replace(BS + 'r', CR).replace(BS + BS, BS)
    return s

def load_all():
    e, f = read(os.path.join(GAME, 'DAT', 'locallng.wsx'))
    res = {}
    for i, (o, s) in e.items():
        if i not in NAMES: continue
        f.seek(o); res[NAMES[i]] = (i, f.read(s))
    return res

def main():
    rows = []
    for name, (i, d) in sorted(load_all().items()):
        if name in SKIP: continue
        for k, (sid, t) in enumerate(parse(d)):
            rows.append((name, k, sid, esc(t)))
    with open(os.path.join(HERE, '..', 'translation', 'en', 'text.tsv'), 'w', encoding='utf-8', newline='') as f:
        f.write('src\tidx\tsid\ten\n')
        for r in rows: f.write('%s\t%d\t%d\t%s\n' % r)
    print(len(rows), 'rows')

if __name__ == '__main__':
    main()
