# 文字起こしから字幕の元（英文）を作る
#   → translation/en/subs.tsv   key, mix, 位置, 段, 始め(ms), 終わり(ms), 英文
#   → translation/en/lines.tsv  同じ英文は 1 行（訳す単位）: lid, 英文, 出る所の数, 最初の key, 群
import os, sys, json, re, csv, collections
HERE = os.path.dirname(os.path.abspath(__file__))
JP = os.path.join(HERE, '..')
OUT = os.path.join(JP, 'translation', 'en')
os.makedirs(OUT, exist_ok=True)

BLACK = re.compile(r"(?i)^(thanks? (you )?for watching!?|thank you for watching!?|you|\.+|subtitles.*|transcrib.*|(ha[ \-,!]*)+!?|(heh[ \-,!]*)+!?|(uh[ \-,!]*)+!?|(oh[ \-,!]*)+!?|(ah[ \-,!]*)+!?|a+h+!*|ugh!*|ow!*|(hmm+[ \-,!.]*)+|music|♪.*)$")

def speech_name(key):
    n = key[4:]
    return bool(re.match(r'^\d{6,7}[a-z]$', n))

# 書庫の名前（ラベル）から声だと分かるもの: 幽霊のささやき・ベリアル・物の名前
VOICE_KEY = re.compile(r'^arc_(7(behind|coming|joinus|look|luth|nogo|over|please|urours)\d|belial|beliale|c_ran2|998\d\d24e)$')

# 聞き違いの直し（物の名前は書庫のラベルに合わせる）
FIX = {
    'And X.': 'Hand Axe.', 'Doc Bo.': 'Dark Bow.', 'Male Shirt': 'Mail Shirt.', 'Earn.': 'Urn.',
    'Black Lycan.': 'Black Lichen.', 'Allo.': 'Aloe.', 'Cave Allo.': 'Cave Aloe.', 'Acts of the Long Arm.': 'Axe of the Long Arm.',
    'Arendt Prak.': 'Arrant Prac.', "Venom's Sack.": 'Venom Sac.', 'Death Sack': 'Death Sac.',
    'Acid Sack': 'Acid Sac.', 'Braces of Valor.': 'Bracers of Valor.', 'Manor Foil.': 'Mana Foil.',
    'Brooke Flounder.': 'Brook Flounder.', 'Plate Male Breastplate': 'Platemail Breastplate.',
    'Reaver of the Great Ork.': 'Reaver of the Great Orc.', 'Halberd': 'Halberd.',
    'Pleased.': 'Please.', 'Join us, loser.': 'Join us, Luther.', 'Join us, Luthor.': 'Join us, Luther.',
    'Luthor.': 'Luther.', 'LUTHER': 'Luther!', "We're coming for you, Luthor.": "We're coming for you, Luther.",
    'You are.': 'You are ours.', 'LOOOOOO-': 'Luther...',
}

def keep(seg, dur, key):
    t = seg['t'].strip()
    if VOICE_KEY.match(key) and re.search('[A-Za-z]{2}', t) and not BLACK.match(t):
        return seg['nsp'] < 0.3
    if not re.search('[A-Za-z]', t):
        return False
    if BLACK.match(t):
        return False
    if seg['e'] > dur + 1.0 or seg['lp'] < -1.5:
        return False
    nsp, lp = seg['nsp'], seg['lp']
    if key.startswith('arc_') and not speech_name(key):
        return nsp < 0.05 and lp > -0.4
    return nsp < 0.2 or (nsp < 0.5 and lp > -0.5)

def main():
    man = {}
    for line in open(os.path.join(JP, 'audio', 'manifest.tsv'), encoding='utf-8'):
        k, mixf, wid, off, size, kind, label = line.rstrip('\n').split('\t')
        man[k] = dict(mix=mixf.replace('\\', '/'), id=wid, off=int(off), size=int(size), kind=kind, label=label)
    rows = []
    longv = {}
    lp = os.path.join(JP, 'audio', 'transcript_long.jsonl')
    if os.path.exists(lp):
        for line in open(lp, encoding='utf-8'):
            r = json.loads(line); longv[r['key']] = r
    retry = {}
    rp = os.path.join(JP, 'audio', 'transcript_retry.jsonl')
    if os.path.exists(rp):
        for line in open(rp, encoding='utf-8'):
            r = json.loads(line); retry[r['key']] = r
    for line in open(os.path.join(JP, 'audio', 'transcript.jsonl'), encoding='utf-8'):
        r = json.loads(line)
        r = longv.get(r['key'], r)    # 長い音声は起こし直した方を使う
        if not r.get('segs'):
            r = retry.get(r['key'], r)    # 空だったものは VAD なしで起こし直した方
        k = r['key']
        if k not in man or 'segs' not in r:
            continue
        segs = [s for s in r['segs'] if keep(s, r['dur'], k)]
        for i, s in enumerate(segs):
            t = s['t'].strip()
            rows.append((k, man[k]['mix'], man[k]['off'], i, int(s['s'] * 1000), int(s['e'] * 1000), FIX.get(t, t)))
    rows.sort(key=lambda x: (x[1], x[2], x[3]))
    with open(os.path.join(OUT, 'subs.tsv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['key', 'mix', 'off', 'seg', 'start', 'end', 'en'])
        w.writerows(rows)
    # 同じ英文をまとめる
    uniq = collections.OrderedDict()
    for k, mixf, off, i, s, e, t in rows:
        g = mixf.split('/')[-1].split('.')[0]
        if k.startswith('arc_'):
            g = 'VOICE'
        if t not in uniq:
            uniq[t] = [k, g, 0]
        uniq[t][2] += 1
    with open(os.path.join(OUT, 'lines.tsv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['lid', 'group', 'key', 'count', 'en'])
        for n, (t, (k, g, c)) in enumerate(uniq.items()):
            w.writerow(['S%05d' % n, g, k, c, t])
    print('segments', len(rows), 'unique', len(uniq), 'chars', sum(len(t) for t in uniq))
    print(collections.Counter(v[1] for v in uniq.values()).most_common(50))

if __name__ == '__main__':
    main()
