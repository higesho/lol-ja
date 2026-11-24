# ランズ・オブ・ロア2 日本語化 ソース

GOG 版 Lands of Lore: Guardians of Destiny（DOS）用の `DOSBOX\winmm.dll`（DOSBox の中のゲームを書き換えて日本語と字幕を描く）と、そのデータを作る道具です。

- `dll/` … winmm.dll の本体。`build.bat`（Visual Studio の x86 の cl と rc）で組み立てます。先に `tools/build.py` で `dll/lol2jp.bin` を作ってください。`build.bat test` で作る試験版（`build/test/winmm.dll`、試験用の操作口とログ入り）は配布しません
- `tools/mix.py` … ゲームの MIX 書庫を読む
- `tools/extract_ui.py` … 画面の英文を `translation/en/` に抜き出す
- `tools/build.py` / `tools/mkdata.py` / `tools/dict_from_out.py` … 訳と字の形から `dll/lol2jp.bin` を作る
- `tools/make_dist.py` … 配布用の zip を作る
- `tools/extract_audio.py` → `tools/transcribe*.py` → `tools/collect_subs.py` … 声を取り出し、文字に起こして字幕の元を作る

必要なもの: Python 3（faster-whisper は字幕の元を作るときだけ）、Visual Studio。
ゲーム本体（GOG 版）、訳のファイル、美咲フォント・東雲フォント（BDF、`font/` に置く）は各自で用意してください。
