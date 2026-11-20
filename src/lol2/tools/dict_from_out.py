# work と out から「英文→訳」の辞書を作る → translation/dict_ui.tsv, dict_subs.tsv
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
T = os.path.join(HERE, '..', 'translation')

def load_pairs():
    ui, subs = {}, {}
    for fn in sorted(os.listdir(os.path.join(T, 'work'))):
        work = [l.split('\t', 3) for l in open(os.path.join(T, 'work', fn), encoding='utf-8').read().split('\n')[1:] if l]
        op = os.path.join(T, 'out', fn)
        if not os.path.exists(op):
            continue
        out = {}
        for l in open(op, encoding='utf-8'):
            l = l.rstrip('\n')
            if l and not l.startswith('#NEW') and '\t' in l:
                a, b = l.split('\t', 1); out[a] = b
        d = ui if fn.startswith('U') else subs
        bs = chr(92)
        for lid, g, h, en in work:
            if lid in out:
                ja = out[lid]
                if fn.startswith('U'):      # 作業表は逆スラッシュが二重に書かれている（\\r → \r）
                    en = en.replace(bs + bs, bs); ja = ja.replace(bs + bs, bs)
                d[en] = ja
    return ui, subs

def main():
    ui, subs = load_pairs()
    for name, d in (('dict_ui.tsv', ui), ('dict_subs.tsv', subs)):
        with open(os.path.join(T, name), 'w', encoding='utf-8') as f:
            for en, ja in d.items():
                f.write(en + '\t' + ja + '\n')
    print('ui', len(ui), 'subs', len(subs))

if __name__ == '__main__':
    main()
