# 声の入っていそうな音声をすべて 16kHz モノラル WAV に取り出す（文字起こし用）
#   音声書庫（LOCALLNG.MIX 内）の各レコード → audio/wav/arc_<名前>.wav
#   MIX 内の VQA と AUD                     → audio/wav/<MIX名>_<id>.wav
# 一覧は audio/manifest.tsv
import sys, os, glob, struct, subprocess, tempfile, concurrent.futures as cf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mix, survey

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OUT = os.path.join(ROOT, 'jp_work', 'audio')
WAV = os.path.join(OUT, 'wav')
TMP = os.path.join(OUT, 'tmp')
os.makedirs(WAV, exist_ok=True); os.makedirs(TMP, exist_ok=True)
REC = 60

def arc_records(b):
    """音声書庫の索引: 60 バイト固定。末尾 0x354 バイト手前で終わる"""
    import re
    ms = [m.start() for m in re.finditer(rb'(?<![0-9a-zA-Z_])[0-9a-zA-Z_]{1,8}\.aud\x00', b)]
    import collections
    r = collections.Counter(p % REC for p in ms).most_common(1)[0][0]
    al = sorted(p for p in ms if p % REC == r)
    first, last = al[0], al[-1]
    # レコードは [位置・大きさ・id・flags の 16 バイト][名前 13][札 31] の 60 バイト。数値は名前の 16 バイト前
    recs = []
    for p in range(first, last + 1, REC):
        name = b[p:p + 13].split(b'\x00')[0]
        try:
            name = name.decode('ascii')
            if not name.lower().endswith('.aud'):
                raise ValueError
        except Exception:
            name = 'rec%05d.aud' % ((p - first) // REC)
        lab = b[p + 13:p + 44]
        off, size, wid, flg = struct.unpack_from('<IIII', b, p - 16)
        recs.append(dict(name=name, label=lab, off=off, size=size, id=wid, flags=flg, pos=p))
    return recs

def label_text(lab):
    import re
    best = b''
    for part in lab.split(b'\x00'):
        m = re.findall(rb'[A-Za-z0-9_\.\'!?,\-]{3,}', part)
        for x in m:
            if len(x) > len(best):
                best = x
    return best.decode('latin1').lstrip('v').lstrip('p') if best else ''

def to_wav(data, key, ext):
    out = os.path.join(WAV, key + '.wav')
    if os.path.exists(out):
        return key, 'skip'
    fd, tmp = tempfile.mkstemp(suffix=ext, dir=TMP)
    os.write(fd, data); os.close(fd)
    r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', tmp, '-vn', '-ac', '1', '-ar', '16000', out],
                       capture_output=True)
    os.remove(tmp)
    return key, 'ok' if r.returncode == 0 and os.path.exists(out) else 'fail:' + r.stderr.decode(errors='replace')[:100]

def main():
    jobs = []
    man = []
    lng = os.path.join(ROOT, 'LOCALLNG.MIX')
    d, base, ents = mix.read_mix(lng)
    for i, o, s in ents:
        if i == 0x5d9ad8b8:
            arc = d[base + o:base + o + s]
            arc_base = base + o
    for r in arc_records(arc):
        key = 'arc_' + r['name'][:-4]
        man.append((key, 'LOCALLNG.MIX', '5d9ad8b8', arc_base + r['off'], r['size'], r['flags'], label_text(r['label'])))
        if r['flags'] == 0 and r['size'] > 16:
            jobs.append((arc[r['off']:r['off'] + r['size']], key, '.aud'))
    for p in sorted(glob.glob(os.path.join(ROOT, '*.MIX')) + glob.glob(os.path.join(ROOT, 'DAT', '*.MIX'))):
        mname = os.path.basename(p).split('.')[0]
        if mname in ('DMUSIC', 'SETUP'):
            continue
        d, base, ents = mix.read_mix(p)
        for i, o, s in ents:
            k = survey.kind(d[base + o:base + o + 16])
            if k not in ('vqa', 'aud'):
                continue
            key = '%s_%08x' % (mname, i)
            man.append((key, os.path.relpath(p, ROOT), '%08x' % i, base + o, s, k, ''))
            jobs.append((d[base + o:base + o + s], key, '.' + k))
    with open(os.path.join(OUT, 'manifest.tsv'), 'w', encoding='utf-8') as f:
        for m in man:
            f.write('\t'.join(str(x) for x in m) + '\n')
    print('jobs', len(jobs), flush=True)
    n = 0
    with cf.ThreadPoolExecutor(12) as ex:
        for key, st in ex.map(lambda j: to_wav(*j), jobs):
            n += 1
            if not st.startswith(('ok', 'skip')):
                print(key, st, flush=True)
            if n % 200 == 0:
                print(n, flush=True)
    print('done', n)

if __name__ == '__main__':
    main()
