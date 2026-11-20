# 動画の音（audio/movie/*.wav）を時刻付きで文字に起こす → audio/movie_transcript.json
#   CPU の Whisper（small.en）、優先度は低め
import os, sys, json, time
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ['HF_TOKEN'] = ''
import ctypes
try:
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
except Exception:
    pass
from faster_whisper import WhisperModel
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'audio', 'movie'); OUT = os.path.join(ROOT, 'audio', 'movie_transcript.json')
PROMPT = ("Copper, LeGre, Eric, David, Mark, Gladstone, Draracle, Dawn, King Richard, Ruloi, Dracoid, Belial, "
          "Rift Hounds, Shard, Morphera.")

def main():
    res = json.load(open(OUT, encoding='utf-8')) if os.path.exists(OUT) else {}
    m = WhisperModel('small.en', device='cpu', compute_type='int8', cpu_threads=6)
    for f in sorted(os.listdir(SRC)):
        if not f.endswith('.wav'): continue
        k = f[:-4]
        if k in res: continue
        t0 = time.time()
        segs, info = m.transcribe(os.path.join(SRC, f), beam_size=5, initial_prompt=PROMPT, vad_filter=True)
        res[k] = [{'start': round(s.start, 2), 'end': round(s.end, 2), 'text': s.text.strip()} for s in segs]
        json.dump(res, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print(k, round(info.duration), '秒', len(res[k]), '区切り', int(time.time() - t0), 's', flush=True)
    print('done')

if __name__ == '__main__':
    main()
