@echo off
if not exist "bin" mkdir bin

if not exist "build_number.txt" (set /a BUILD=0) else (set /p BUILD=<build_number.txt)
set /a BUILD+=1
> build_number.txt echo %BUILD%
echo Build number: %BUILD%

REM Set up the environment for MSVC (you may need to adjust the path)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

rc /d BUILD_NUMBER=%BUILD% /fo version.res version.rc

echo Compiling VirtualESCPOS...
cl /nologo /EHsc /std:c++17 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
    /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 /DNTDDI_VERSION=0x06010000 ^
    /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR ^
    main.cpp VirtualPrinter.cpp Barcode.cpp CodePages.cpp QRCode.cpp Network.cpp version.res ^
    User32.lib Gdi32.lib Ws2_32.lib Advapi32.lib Shell32.lib Comdlg32.lib ^
    /Fe:bin\VirtualESCPOS.exe ^
    /link /SUBSYSTEM:WINDOWS,"5.01"

if %ERRORLEVEL% EQU 0 (
    echo Compilation successful! Build %BUILD%
    echo Executable is in bin\VirtualESCPOS.exe
	del /Q *.obj 2>nul
) else (
    echo Compilation failed.
)
