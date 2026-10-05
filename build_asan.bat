@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >NUL 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
cl /nologo /W3 /Zi /Od /fsanitize=address /D_CRT_SECURE_NO_WARNINGS /DYY_NO_UNISTD_H /Isrc /Isrc\win /Fe:ussr_asan.exe src\ussr.c src\pp.c src\main.c src\uno.c src\ussr_oop_builtins.c src\ussr_bytecode.c src\scheduler.c src\eventq.c src\gc.c src\parser.tab.c src\lexer.c src\prng64_xrp32.c src\autocomplete.c src\completion_fs_win32.c src\win\process_win32.c src\win\worstline.c src\win\getopt.c ussr.res user32.lib
