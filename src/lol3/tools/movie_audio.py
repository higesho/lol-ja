# 動画（MOVIES*.WSX の VQA）の音を取り出す → audio/movie/<書庫>_<番号>.wav（16kHz モノラル）
import sys, os, glob, wave
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from wsx import read
import av
import numpy as np
G = os.path.join(HERE, '..', '..'); OUT = os.path.join(HERE, '..', 'audio', 'movie')

def main():
    os.makedirs(OUT, exist_ok=True)
    seen = set(); done_ids = set()
    for p in sorted(glob.glob(os.path.join(G, 'DAT', '*'))):
        b = os.path.basename(p)
        if not b.lower().startswith('movies') or b.lower() in seen: continue
        seen.add(b.lower())
        e, f = read(p)
        for i, (o, s) in e.items():
            if i in done_ids: continue
            done_ids.add(i)
            tmp = os.path.join(OUT, 'tmp.vqa')
            f.seek(o); open(tmp, 'wb').write(f.read(s))
            c = av.open(tmp)
            a = [st for st in c.streams if st.type == 'audio']
            if not a: c.close(); continue
            rs = av.AudioResampler(format='s16', layout='mono', rate=16000)
            pcm = []
            try:
                for pk in c.demux(a[0]):
                    try:
                        for fr in pk.decode():
                            for r in rs.resample(fr): pcm.append(r.to_ndarray().reshape(-1))
                    except av.error.FFmpegError:
                        pass
            except av.error.FFmpegError as ex:
                print('  途中で止まった', ex.__class__.__name__)
            c.close()
            x = np.concatenate(pcm) if pcm else np.zeros(0, np.int16)
            fn = os.path.join(OUT, '%s_%08x.wav' % (b[:-4].upper(), i))
            w = wave.open(fn, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(x.astype(np.int16).tobytes()); w.close()
            print(os.path.basename(fn), round(len(x) / 16000, 1), '秒')
    os.remove(os.path.join(OUT, 'tmp.vqa'))

if __name__ == '__main__':
    main()
