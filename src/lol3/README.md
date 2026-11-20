# ランズ・オブ・ロア3 日本語化 ソース

GOG 版 Lands of Lore III 用の `winmm.dll`（ゲームのフォルダに置くと文を日本語にし、会話と動画に字幕を出す）と、そのデータを作る道具です。

- `dll/` … winmm.dll の本体。`build.bat`（Visual Studio の x86 の cl）で組み立てます。`build_all.bat` は試験用の DLL と `test_build.exe` も作ります
- `tools/wsx.py` / `tools/trr.py` … ゲームの書庫 .wsx と文の表 .trr を読み書きする
- `tools/extract.py` … 英文を `translation/en/text.tsv` に抜き出す
- `tools/check_ja.py` / `tools/build_text.py` / `tools/build_dat.py` … 訳を検査し、`out/LOL3JP/LOL3_JA.DAT` を作る
- `tools/make_font.py` … 字の形の表 `out/LOL3JP/JPFONT.BIN` を作る
- `tools/extract_voices.py` → `tools/transcribe_cpu.py` → `tools/match_voices.py` … 会話の字幕の表 `LOL3_SUB.DAT` を作る
- `tools/movie_audio.py` → `tools/transcribe_movies.py` → `tools/match_movies.py` … 動画の字幕の表 `LOL3_MOV.DAT` を作る
- `tools/make_dist.py` … 配布用の zip を作る

必要なもの: Python 3（字幕の表を作るときは faster-whisper・PyAV・numpy）、Visual Studio。
ゲーム本体（GOG 版）、訳のファイル、美咲フォント・東雲フォント（BDF、`font/` に置く）は各自で用意してください。
