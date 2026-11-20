"""配布物を dist/ に組み立てる: python -I tools/make_dist.py
dist/ランズ・オブ・ロア日本語化MOD/ と dist/LandsOfLore_JP.zip を作る（DLL と訳は dll/out の物を使う）
zip の一番上は DOSBOX フォルダと説明書なので、ゲームのフォルダにそのまま展開すればよい"""
import os, shutil, zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
os.chdir(ROOT)
NAME = 'ランズ・オブ・ロア日本語化MOD'
README = '日本語化MOD_はじめにお読みください.txt'
OUT = os.path.join('dist', NAME)
FILES = [
    ('dll/out/winmm.dll', 'DOSBOX/winmm.dll'),
    ('dll/out/lol_ja.dat', 'DOSBOX/LOLJP/lol_ja.dat'),
    ('../DOSBOX/LOLJP/jpfont8.bin', 'DOSBOX/LOLJP/jpfont8.bin'),
    ('dll/out/buttons.bin', 'DOSBOX/LOLJP/buttons.bin'),
    ('dll/out/pics.bin', 'DOSBOX/LOLJP/pics.bin'),
]

if os.path.exists(OUT):
    shutil.rmtree(OUT)
for src, dst in FILES:
    d = os.path.join(OUT, dst)
    os.makedirs(os.path.dirname(d), exist_ok=True)
    shutil.copyfile(src, d)
# 設定（配布用は Enable だけ）
with open(os.path.join(OUT, 'DOSBOX/LOLJP/loljp.ini'), 'wb') as f:
    f.write(b'[Main]\r\nEnable=1\r\n')
# 説明書（メモ帳で読めるよう BOM 付き UTF-8、改行は CRLF）
txt = open('dist_src/はじめにお読みください.txt', encoding='utf-8').read().replace('\r\n', '\n')
with open(os.path.join(OUT, README), 'wb') as f:
    f.write(b'\xef\xbb\xbf' + txt.replace('\n', '\r\n').encode('utf-8'))

zp = os.path.join('dist', 'LandsOfLore_JP.zip')
with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
    for base, dirs, files in os.walk(OUT):
        for fn in sorted(files):
            p = os.path.join(base, fn)
            z.write(p, os.path.relpath(p, OUT).replace(os.sep, '/'))
for i in zipfile.ZipFile(zp).infolist():
    print(f'{i.file_size:9d}  {i.filename}')
print(zp, os.path.getsize(zp), 'バイト')
