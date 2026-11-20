# 配布物を作る → dist/LandsOfLore3_JP.zip
#   winmm.dll、LOL3JP\{LOL3_JA.DAT, JPFONT.BIN, lol3jp.ini}、日本語化MOD_はじめにお読みください.txt
import os, zipfile, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); W = os.path.join(HERE, '..')
INI = '; Lands of Lore III 日本語化の設定\r\n[LOL3JP]\r\n; 0 にすると日本語化しません\r\nEnable=1\r\n; 1 にすると LOL3JP\\log.txt に記録を書きます\r\nLog=0\r\n; 0 にすると会話と動画の字幕を出しません\r\nSubtitles=1\r\n'

def main():
    os.makedirs(os.path.join(W, 'dist'), exist_ok=True)
    z = os.path.join(W, 'dist', 'LandsOfLore3_JP.zip')
    files = [
        ('winmm.dll', os.path.join(W, 'dll', 'winmm.dll')),
        ('LOL3JP/LOL3_JA.DAT', os.path.join(W, 'out', 'LOL3JP', 'LOL3_JA.DAT')),
        ('LOL3JP/JPFONT.BIN', os.path.join(W, 'out', 'LOL3JP', 'JPFONT.BIN')),
    ]
    sub = os.path.join(W, 'out', 'LOL3JP', 'LOL3_SUB.DAT')
    if os.path.exists(sub): files.append(('LOL3JP/LOL3_SUB.DAT', sub))
    mov = os.path.join(W, 'out', 'LOL3JP', 'LOL3_MOV.DAT')
    if os.path.exists(mov): files.append(('LOL3JP/LOL3_MOV.DAT', mov))
    readme = '日本語化MOD_はじめにお読みください.txt'
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
        for arc, src in files:
            zf.write(src, arc)
        zf.writestr('LOL3JP/lol3jp.ini', INI.encode('utf-8'))
        zf.write(os.path.join(W, 'dist_src', readme), readme)
    for arc, src in files:
        print(arc, os.path.getsize(src), hashlib.md5(open(src, 'rb').read()).hexdigest())
    print(z, os.path.getsize(z), hashlib.md5(open(z, 'rb').read()).hexdigest())

if __name__ == '__main__':
    main()
