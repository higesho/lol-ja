# 文字起こしが空だった音声（動画・効果音つきの声）を VAD なしで起こし直す → audio/transcript_retry.jsonl
import os, sys, json
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ['HF_TOKEN'] = ''
from faster_whisper import WhisperModel
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import transcribe

def main():
    A = os.path.join(transcribe.ROOT, 'audio')
    man = {}
    for l in open(os.path.join(A, 'manifest.tsv'), encoding='utf-8'):
        p = l.rstrip('\n').split('\t'); man[p[0]] = p[5]
    longk = {json.loads(l)['key'] for l in open(os.path.join(A, 'transcript_long.jsonl'), encoding='utf-8')}
    keys = []
    for l in open(os.path.join(A, 'transcript.jsonl'), encoding='utf-8'):
        r = json.loads(l)
        if r['key'] in longk or r.get('segs') or r['key'] not in man:
            continue
        if man[r['key']] in ('vqa', 'aud') or r.get('dur', 0) > 1.5:
            keys.append(r['key'])
    m = WhisperModel('large-v3', device='cuda', compute_type='float16')
    with open(os.path.join(A, 'transcript_retry.jsonl'), 'w', encoding='utf-8') as f:
        for k in keys:
            segs, info = m.transcribe(os.path.join(A, 'wav', k + '.wav'), language='en', beam_size=5,
                                      initial_prompt=transcribe.PROMPT, vad_filter=False,
                                      condition_on_previous_text=False)
            out = [dict(s=round(s.start, 2), e=round(s.end, 2), t=s.text.strip(),
                        nsp=round(s.no_speech_prob, 3), lp=round(s.avg_logprob, 3)) for s in segs]
            f.write(json.dumps(dict(key=k, dur=round(info.duration, 2), segs=out), ensure_ascii=False) + '\n')
            f.flush()
    print('done', len(keys))

if __name__ == '__main__':
    main()
