@echo off
if not exist "bin" mkdir bin

REM Set up the environment for MSVC (you may need to adjust the path)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

rc /fo version.res version.rc

echo Compiling VirtualESCPOS...
cl /nologo /EHsc /std:c++17 /MD /D_CRT_SECURE_NO_WARNINGS ^
    main.cpp VirtualPrinter.cpp Network.cpp version.res ^
    User32.lib Gdi32.lib Ws2_32.lib Advapi32.lib Shell32.lib Comdlg32.lib ^
    /Fe:bin\VirtualESCPOS.exe

if %ERRORLEVEL% EQU 0 (
    echo Compilation successful!
    echo Executable is in bin\VirtualESCPOS.exe
) else (
    echo Compilation failed.
)
