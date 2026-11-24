@echo off
rem build release DLL (dll\winmm.dll), test DLL (..\build\test\winmm.dll) and test_build.exe
cd /d "%~dp0"
call "%~dp0build.bat" || exit /b 1
call "%~dp0build.bat" test || exit /b 1
call "%~dp0build_tb.bat" || exit /b 1
echo BUILD OK
