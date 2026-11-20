# 20 秒を超える音声（動画の語りなど）を、VAD なし・前文脈ありで起こし直す → audio/transcript_long.jsonl
import os, sys, json
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ['HF_TOKEN'] = ''
from faster_whisper import WhisperModel
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import transcribe

def main():
    A = os.path.join(transcribe.ROOT, 'audio')
    recs = [json.loads(l) for l in open(os.path.join(A, 'transcript.jsonl'), encoding='utf-8')]
    keys = [r['key'] for r in recs if r.get('dur', 0) > 20]
    m = WhisperModel('large-v3', device='cuda', compute_type='float16')
    with open(os.path.join(A, 'transcript_long.jsonl'), 'w', encoding='utf-8') as f:
        for k in keys:
            segs, info = m.transcribe(os.path.join(A, 'wav', k + '.wav'), language='en', beam_size=5,
                                      initial_prompt=transcribe.PROMPT, vad_filter=False,
                                      condition_on_previous_text=True, word_timestamps=True)
            out = []
            for s in segs:
                out.append(dict(s=round(s.start, 2), e=round(s.end, 2), t=s.text.strip(),
                                nsp=round(s.no_speech_prob, 3), lp=round(s.avg_logprob, 3)))
            f.write(json.dumps(dict(key=k, dur=round(info.duration, 2), segs=out), ensure_ascii=False) + '\n')
            f.flush()
    print('done', len(keys))

if __name__ == '__main__':
    main()
