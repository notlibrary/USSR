@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >NUL 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
nmake /f Makefile.msc PREGENERATED=1
