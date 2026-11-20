"""英文を translation/en/*.tsv に書き出す
   LANDS.ENG と LEVELxx.ENG（圧縮、展開して書く）、LOLINTRO.DIP・HISTORY.DIP・LOLFINAL.DIP（圧縮なし）"""
import sys, os, glob
sys.path.insert(0, os.path.dirname(__file__))
import engtext, textutil

os.makedirs('translation/en', exist_ok=True)
total = 0


def dump(name, rows):
    global total
    with open(f'translation/en/{name}.tsv', 'w', encoding='utf-8', newline='\n') as o:
        o.write('id\ten\n')
        for i, s in enumerate(rows):
            o.write(f'{i}\t{textutil.esc(s)}\n')
    total += len(rows)


for name, f in [('LANDS', 'orig/pak/STARTUP.PAK/LANDS.ENG')] + [
        (os.path.basename(f)[:-4], f) for f in sorted(glob.glob('orig/pak/L*.PAK/LEVEL*.ENG'))]:
    dump(name, [s for raw, s in engtext.read_eng(open(f, 'rb').read())])

for name, f in [('LOLINTRO', 'orig/pak/ENG_STARTUP.PAK/LOLINTRO.DIP'),
                ('HISTORY', 'orig/pak/ENG_STARTUP.PAK/HISTORY.DIP'),
                ('LOLFINAL', 'orig/pak/ENG_FINALE2.PAK/LOLFINAL.DIP')]:
    dump(name, [raw for raw, s in engtext.read_eng(open(f, 'rb').read())])

print('strings', total)
