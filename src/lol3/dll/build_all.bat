@echo off
rem build release DLL, test DLL and test_build.exe
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /O2 /W3 /MT /LD /utf-8 /D_CRT_SECURE_NO_WARNINGS lol3jp.c /Fe:winmm.dll /link /DEF:winmm.def kernel32.lib user32.lib || exit /b 1
cl /nologo /O2 /W3 /MT /LD /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTESTCTL lol3jp.c /Fo:lol3jp_test.obj /Fe:winmm_test.dll /link /DEF:winmm.def kernel32.lib user32.lib || exit /b 1
cl /nologo /O2 /W3 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS test_build.c /Fe:test_build.exe /link kernel32.lib user32.lib || exit /b 1
echo BUILD OK
