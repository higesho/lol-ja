# 配布物 dist/LandsOfLore2_JP.zip を作る（DOSBOX/winmm.dll と説明書）: python -I tools/make_dist.py
# DLL は配布版（dll/build.bat で作る dll/winmm.dll）を使う。試験版（build/test/winmm.dll）は入れない
import os, zipfile, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
JP = os.path.join(HERE, '..')
out = os.path.join(JP, 'dist', 'LandsOfLore2_JP.zip')
os.makedirs(os.path.dirname(out), exist_ok=True)
dll = os.path.join(JP, 'dll', 'winmm.dll')
README = '日本語化MOD_はじめにお読みください.txt'
# 説明書（メモ帳で読めるよう BOM 付き UTF-8、改行は CRLF）
txt = open(os.path.join(JP, 'dist_src', 'はじめにお読みください.txt'), encoding='utf-8-sig').read().replace('\r\n', '\n')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr(README, b'\xef\xbb\xbf' + txt.replace('\n', '\r\n').encode('utf-8'))
    z.write(dll, 'DOSBOX/winmm.dll')
for p in (dll, out):
    print(os.path.basename(p), os.path.getsize(p), hashlib.md5(open(p, 'rb').read()).hexdigest())
