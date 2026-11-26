@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Prefer an MSYS2 toolchain that has both GCC and the SQLite development files.
rem Set BST_MSYS_PREFIX first to override the default (example: D:\msys64\ucrt64).
if defined BST_MSYS_PREFIX goto :select_toolchain
if exist "C:\msys64\ucrt64\bin\gcc.exe" set "BST_MSYS_PREFIX=C:\msys64\ucrt64"
if not defined BST_MSYS_PREFIX if exist "C:\msys64\mingw64\bin\gcc.exe" set "BST_MSYS_PREFIX=C:\msys64\mingw64"

:select_toolchain
set "CC=gcc"
set "INC="
set "LIB="
set "BIN_DIR="
if not defined BST_MSYS_PREFIX goto :path_compiler
if not exist "%BST_MSYS_PREFIX%\bin\gcc.exe" goto :missing_gcc
if not exist "%BST_MSYS_PREFIX%\include\sqlite3.h" goto :missing_sqlite
if not exist "%BST_MSYS_PREFIX%\lib\libsqlite3.dll.a" if not exist "%BST_MSYS_PREFIX%\lib\libsqlite3.a" goto :missing_sqlite
set "BIN_DIR=%BST_MSYS_PREFIX%\bin"
set "CC=%BIN_DIR%\gcc.exe"
set "INC=-I%BST_MSYS_PREFIX%\include"
set "LIB=-L%BST_MSYS_PREFIX%\lib"
set "PATH=%BIN_DIR%;%PATH%"
goto :compiler_ready

:path_compiler
where gcc >nul 2>nul
if errorlevel 1 goto :missing_gcc
for /f "delims=" %%G in ('where gcc') do (
    set "BIN_DIR=%%~dpG"
    goto :compiler_ready
)

:compiler_ready
echo Using compiler: %CC%
set "WARN=-std=c11 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00"
echo [1/2] Building timestamp.exe ...
"%CC%" %WARN% %INC% -municode src\cli.c src\common.c src\json.c src\session.c src\generator.c src\obs_win.c %LIB% -o timestamp.exe -lsqlite3 -lwinhttp -lcrypt32 -lbcrypt -lshell32 -lm
if errorlevel 1 goto :timestamp_failed

echo [2/2] Building config.exe ...
"%CC%" %WARN% %INC% -municode -mwindows src\config_gui.c src\common.c src\json.c %LIB% -o config.exe -lcomdlg32 -lcrypt32 -luser32 -lgdi32 -lm
if errorlevel 1 goto :config_failed

rem Put the runtime DLLs beside the EXEs. Only copy from the compiler selected above.
if defined BIN_DIR (
    for %%D in (libsqlite3-0.dll libwinpthread-1.dll libgcc_s_seh-1.dll) do (
        if exist "%BIN_DIR%\%%D" copy /Y "%BIN_DIR%\%%D" "%~dp0%%D" >nul
    )
)
if not exist sessions mkdir sessions
if not exist logs mkdir logs
echo.
echo Build complete: timestamp.exe / config.exe
exit /b 0

:missing_gcc
echo ERROR: GCC not found. Install MSYS2 UCRT64 GCC.
echo   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sqlite3
exit /b 1

:missing_sqlite
echo ERROR: SQLite3 development files are missing from %BST_MSYS_PREFIX%.
echo Open the matching MSYS2 terminal and install SQLite3:
echo   UCRT64 : pacman -S --needed mingw-w64-ucrt-x86_64-sqlite3
echo   MINGW64: pacman -S --needed mingw-w64-x86_64-sqlite3
exit /b 1

:timestamp_failed
echo ERROR: timestamp.exe build failed.
echo If you see ^"sqlite3.h: No such file or directory^", install the
echo matching SQLite3 DEVELOPMENT package for the GCC being used.
exit /b 1

:config_failed
echo ERROR: config.exe build failed.
exit /b 1
