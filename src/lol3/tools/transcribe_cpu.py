# audio/wav/*.wav を CPU の Whisper（base.en）で文字に起こす → audio/transcript.jsonl
#   会話の英文と照らし合わせるためなので小さい型で足りる。途中から再開できる。優先度は低くする。
import os, sys, json, time
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ['HF_TOKEN'] = ''
import ctypes
try:
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)  # BELOW_NORMAL
except Exception:
    pass
from faster_whisper import WhisperModel
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
WAV = os.path.join(ROOT, 'audio', 'wav'); OUT = os.path.join(ROOT, 'audio', 'transcript.jsonl')
PROMPT = ("Copper, LeGre, Gladstone, Draracle, Jakel, Dawn, King Richard, Griselda, Lig, Syruss, Goldy, Celerian, "
          "Sioned, Jadin, Dash, Leina, Finch, Bacchanal, Iron Ring, Talamari, Ruloi, Dracoid, Morphera, Chesara, Jeron.")

def main():
    done = set()
    if os.path.exists(OUT):
        for line in open(OUT, encoding='utf-8'):
            try: done.add(json.loads(line)['key'])
            except Exception: pass
    todo = sorted(f[:-4] for f in os.listdir(WAV) if f.endswith('.wav') and f[:-4] not in done)
    print('todo', len(todo), 'done', len(done), flush=True)
    m = WhisperModel('base.en', device='cpu', compute_type='int8', cpu_threads=6)
    t0 = time.time()
    with open(OUT, 'a', encoding='utf-8') as o:
        for k, key in enumerate(todo):
            try:
                segs, info = m.transcribe(os.path.join(WAV, key + '.wav'), beam_size=1, initial_prompt=PROMPT,
                                          condition_on_previous_text=False, vad_filter=False)
                text = ' '.join(s.text.strip() for s in segs).strip()
                dur = info.duration
            except Exception as ex:
                text = ''; dur = 0; print('err', key, ex, flush=True)
            o.write(json.dumps({'key': key, 'dur': round(dur, 2), 'text': text}, ensure_ascii=False) + '\n'); o.flush()
            if k % 200 == 0: print(k, len(todo), int(time.time() - t0), 's', flush=True)
    print('done', int(time.time() - t0), 's')

if __name__ == '__main__':
    main()
