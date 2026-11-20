# audio/wav/*.wav を Whisper large-v3 で英語の文字に起こす → audio/transcript.jsonl（1 行 1 ファイル）
# 途中から再開できる（済んだものは飛ばす）
import os, sys, json, time
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ['HF_TOKEN'] = ''
from faster_whisper import WhisperModel

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
WAV = os.path.join(ROOT, 'audio', 'wav')
OUT = os.path.join(ROOT, 'audio', 'transcript.jsonl')

PROMPT = ("Luther, Dawn, the Draracle, Belial, Huline, Kit'yara, Bacatta, Gladstone, Scotia, Dracoid, "
          "Ruloi, Kelsrick, Shalla, Anyar, Ja Kel, Larkhon, Mauri, Morgan, Julian, Ra'Shar, Talamari, "
          "Dreamstone, Silverleaf, Amaranth, Rix, Daniel, Kenneth, Ssar, Wild Ones, Claw Mountains, Citadel.")

def main():
    done = set()
    if os.path.exists(OUT):
        for line in open(OUT, encoding='utf-8'):
            try:
                done.add(json.loads(line)['key'])
            except Exception:
                pass
    files = sorted(f[:-4] for f in os.listdir(WAV) if f.endswith('.wav'))
    # 書庫（ルーサーなどの声）を先に
    files.sort(key=lambda k: (not k.startswith('arc_'), k))
    todo = [k for k in files if k not in done]
    print('todo', len(todo), 'done', len(done), flush=True)
    m = WhisperModel('large-v3', device='cuda', compute_type='float16')
    t0 = time.time()
    with open(OUT, 'a', encoding='utf-8') as f:
        for n, k in enumerate(todo):
            try:
                segs, info = m.transcribe(os.path.join(WAV, k + '.wav'), language='en', beam_size=5,
                                          initial_prompt=PROMPT, vad_filter=True,
                                          vad_parameters=dict(min_silence_duration_ms=500),
                                          condition_on_previous_text=False)
                segs = [dict(s=round(s.start, 2), e=round(s.end, 2), t=s.text.strip(),
                             nsp=round(s.no_speech_prob, 3), lp=round(s.avg_logprob, 3)) for s in segs]
                rec = dict(key=k, dur=round(info.duration, 2), segs=segs)
            except Exception as e:
                rec = dict(key=k, err=str(e)[:200])
            f.write(json.dumps(rec, ensure_ascii=False) + '\n')
            f.flush()
            if n % 100 == 0:
                print(n, k, round(time.time() - t0), flush=True)
    print('done', flush=True)

if __name__ == '__main__':
    main()
