@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /W3 /MT /LD /utf-8 /D_CRT_SECURE_NO_WARNINGS %* lol3jp.c /Fe:winmm.dll /link /DEF:winmm.def kernel32.lib user32.lib || exit /b 1
