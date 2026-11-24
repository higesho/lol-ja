# ランズ・オブ・ロア 日本語化 ソース

GOG 版 Lands of Lore（DOS・CD 版）用の `DOSBOX\winmm.dll`（DOSBox の中のゲームを書き換えて日本語を描く）と、そのデータを作る道具です。

- `dll/` … winmm.dll の本体。`build.bat`（Visual Studio の x86 の cl）で `dll/out/winmm.dll` ができます。`build.bat test` で作る試験版（`build/test/winmm.dll`、試験用の操作口 `testctl.c` とログ入り）は配布しません
- `tools/gen_winmm.py` … Windows の winmm.dll から転送の表（`winmm.def`・`winmm_exports.h`）を作る
- `tools/gen_patterns.py` … LANDS.EXE から照合用の命令列の表（`patterns.h`）を作る
- `tools/pak.py` … ゲームの PAK を `orig/pak/` に展開する
- `tools/extract.py` … 英文を `translation/en/` に抜き出す
- `tools/build_text.py` / `tools/build_font.py` … 訳と字の形から `lol_ja.dat`・`jpfont8.bin` を作る
- `tools/make_buttons.py` / `tools/make_pics.py` … 絵の中の文字の差し替えデータを作る
- `tools/make_dist.py` … 配布用の zip を作る

必要なもの: Python 3（pefile・capstone・Pillow）、Visual Studio。
ゲーム本体（GOG 版）、訳のファイル、美咲フォント（misaki_gothic_2nd.bdf）は各自で用意してください。
