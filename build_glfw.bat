@echo off
rem Build ussr.exe with the Petersburg GUI (GLFW + OpenGL) using MSVC.
rem
rem     build_glfw.bat [glfw folder]
rem
rem The folder is a GLFW Windows binary release, the one CI uses
rem (https://github.com/glfw/glfw/releases, glfw-3.5.1.bin.WIN64.zip),
rem holding include\ and lib-vc2022\. Default: GLFW_DIR if set, else
rem C:\glfw-3.5.1.bin.WIN64\glfw-3.5.1.bin.WIN64. The folder path must not
rem contain spaces (Makefile.msc passes it unquoted).
rem
rem ussr.exe links the GLFW DLL, so glfw3.dll is copied next to it.
rem
rem As in CI, the parser and lexer are generated first, by hand: this script
rem does not run bison or flex (see the check below).
setlocal
cd /d "%~dp0"

set "NO_PARSER="
if not exist "src\parser.tab.c" set "NO_PARSER=1"
if not exist "src\parser.tab.h" set "NO_PARSER=1"
if not exist "src\lexer.c" set "NO_PARSER=1"
if defined NO_PARSER (
    echo build_glfw: generate the parser and lexer first:
    echo     win_bison -d -o src\parser.tab.c src\parser.y
    echo     win_flex -o src\lexer.c src\lexer.l
    exit /b 1
)

set "GLFW=%~1"
if "%GLFW%"=="" set "GLFW=%GLFW_DIR%"
if "%GLFW%"=="" set "GLFW=C:\glfw-3.5.1.bin.WIN64\glfw-3.5.1.bin.WIN64"
if not exist "%GLFW%\include\GLFW\glfw3.h" (
    echo build_glfw: no GLFW in "%GLFW%" ^(expected include\GLFW\glfw3.h^)
    exit /b 1
)
if not exist "%GLFW%\lib-vc2022\glfw3dll.lib" (
    echo build_glfw: no lib-vc2022\glfw3dll.lib in "%GLFW%"
    exit /b 1
)

rem MSVC: the newest Visual Studio with C++ tools, else the usual path
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >NUL 2>&1
if errorlevel 1 (
    echo build_glfw: could not set up MSVC with "%VCVARS%"
    exit /b 1
)

rem Makefile.msc compiles petersburg.c for GLFW to its own object
rem (petersburg_glfw.obj) and links glfw3dll.lib by default; relink so
rem ussr.exe is the GUI one even when a build without GLFW came last.
if exist ussr.exe del /q ussr.exe >NUL 2>&1
if exist ussr.exe (
    echo build_glfw: ussr.exe is in use: end the program running it and build again
    exit /b 1
)

nmake /nologo /f Makefile.msc PREGENERATED=1 GLFW_DIR=%GLFW% ussr.exe
if errorlevel 1 (
    echo build_glfw: build failed
    exit /b 1
)

copy /y "%GLFW%\lib-vc2022\glfw3.dll" . >NUL
echo build_glfw: ussr.exe built with GLFW from %GLFW%
