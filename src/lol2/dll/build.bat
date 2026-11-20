@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
rc /nologo /fo lol2jp.res lol2jp.rc || exit /b 1
cl /nologo /O2 /W3 /MT /LD /utf-8 lol2jp.c lol2jp.res /Fe:winmm.dll /link /DEF:winmm.def kernel32.lib user32.lib || exit /b 1
