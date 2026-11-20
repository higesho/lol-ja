# 訳を集めて検査する → translation/ja.tsv（lid, en, ja）と問題の一覧
import os, csv, re, glob, collections, sys
HERE = os.path.dirname(os.path.abspath(__file__)); T = os.path.join(HERE, '..', 'translation')
TOK = re.compile(r'\{[0-9A-F]{2}\}.|%.|\^|@|\*|\$|#|\r')
def toks(s):
    # {xx} の後の 1 文字は {02}/{03} のときだけ印の一部
    out = []
    for m in re.finditer(r'\{([0-9A-F]{2})\}(.?)|%([a-z0-9]?)|\^|@|\*|\$|#|\r', s):
        g = m.group(0)
        if m.group(1):
            g = '{%s}' % m.group(1) + (m.group(2) if m.group(1) in ('02', '03') else '')
        out.append(g)
    return out
def load():
    en = {}
    for p in sorted(glob.glob(os.path.join(T, 'work', '*.tsv'))):
        for r in csv.DictReader(open(p, encoding='utf-8'), delimiter='\t', quoting=csv.QUOTE_NONE):
            en[r['lid']] = (os.path.basename(p)[:-4], r['group'], r['en'])
    en_ids_hint = set(en)
    ja = {}; new = []
    for p in sorted(glob.glob(os.path.join(T, 'out', '*.tsv'))):
        for line in open(p, encoding='utf-8-sig'):
            line = line.rstrip('\n').rstrip('\r')
            if not line.strip(): continue
            if line.startswith('#NEW'): new.append(line.split('\t')[1:] + [os.path.basename(p)]); continue
            if '\t' not in line: ja.setdefault('?', []).append((p, line)); continue
            lid, t = line.split('\t', 1)
            ja[lid.strip()] = t
    # 読み直しの直し（lid, 直した訳, 理由）
    for p in sorted(glob.glob(os.path.join(T, 'review', '*.tsv'))):
        for line in open(p, encoding='utf-8-sig'):
            line = line.rstrip('\n').rstrip('\r')
            if not line.strip() or line.startswith('#') or '\t' not in line: continue
            parts = line.split('\t')
            if parts[0].strip() in en_ids_hint and len(parts) >= 2 and parts[1].strip():
                ja[parts[0].strip()] = parts[1]
    fix = os.path.join(T, 'fix.tsv')
    if os.path.exists(fix):
        for line in open(fix, encoding='utf-8'):
            if line.strip() and not line.startswith('#') and '\t' in line:
                lid, t = line.rstrip('\n').split('\t', 1); ja[lid] = t
    return en, ja, new
def main():
    en, ja, new = load()
    prob = collections.Counter(); rows = []
    for lid, (b, g, e) in en.items():
        j = ja.get(lid)
        if j is None: prob['訳なし'] += 1; rows.append((lid, b, '訳なし', e, '')); continue
        if toks(e) != toks(j) and not (g in ('X_CREDVID', 'X_CREDITS') and sorted(toks(e)) == sorted(toks(j))):
            prob['印'] += 1; rows.append((lid, b, '印', e, j))
        if '\t' in j: prob['タブ'] += 1
        try: re.sub(r'\{..\}|\r', '', j).encode('euc_jp')
        except UnicodeEncodeError as x: prob['EUC 外'] += 1; rows.append((lid, b, 'EUC 外 ' + repr(x.object[x.start:x.end]), e, j))
        if re.search('[ぁ-んァ-ン一-龥]', e) is None and re.search('[A-Za-z]{4,}', re.sub(r'\{..\}.|%.|\r', '', j)) and g not in ('X_CREDVID', 'X_CREDITS'):
            w = re.findall(r'[A-Za-z]{4,}', re.sub(r'\{..\}.|%.|\r', '', j))
            if set(w) - {'CTRL', 'SHIFT', 'SPACE', 'ENTER', 'TAB', 'ESC', 'DEL', 'HOME', 'PGUP', 'PGDN', 'LMOUSE', 'RMOUSE', 'Glide', 'Voodoo', 'README', 'Direct', 'DirectX', 'Westwood', 'Lands', 'Lore', 'Bobo', 'Coco', 'Laugh', 'cross', 'When', 'Guild', 'Scroll', 'Read'}:
                prob['英語残り'] += 1; rows.append((lid, b, '英語残り', e, j))
    extra = [k for k in ja if k not in en]
    print('全', len(en), '訳', sum(1 for k in en if k in ja), dict(prob), '余分', len(extra), '#NEW', len(new))
    with open(os.path.join(T, 'problems.tsv'), 'w', encoding='utf-8') as f:
        for r in rows: f.write('\t'.join(r) + '\n')
    with open(os.path.join(T, 'new_terms.tsv'), 'w', encoding='utf-8') as f:
        for r in new: f.write('\t'.join(r) + '\n')
    with open(os.path.join(T, 'ja.tsv'), 'w', encoding='utf-8') as f:
        for lid, (b, g, e) in en.items():
            if lid in ja: f.write('%s\t%s\t%s\n' % (lid, e, ja[lid]))
if __name__ == '__main__':
    main()
