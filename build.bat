@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Optional overrides: BST_CC, BST_MSYS_PREFIX and BST_SQLITE_DIR.
set "SQLITE_DIR=%~dp0external\sqlite"
if defined BST_SQLITE_DIR set "SQLITE_DIR=%BST_SQLITE_DIR%"
if not exist "%SQLITE_DIR%\sqlite3.c" goto :missing_sqlite
if not exist "%SQLITE_DIR%\sqlite3.h" goto :missing_sqlite

if defined BST_CC goto :explicit_compiler
if defined BST_MSYS_PREFIX goto :msys_compiler
if exist "C:\msys64\ucrt64\bin\gcc.exe" set "BST_MSYS_PREFIX=C:\msys64\ucrt64"
if not defined BST_MSYS_PREFIX if exist "C:\msys64\mingw64\bin\gcc.exe" set "BST_MSYS_PREFIX=C:\msys64\mingw64"
if defined BST_MSYS_PREFIX goto :msys_compiler
if exist "D:\gcc\mingw32\bin\gcc.exe" (
    set "BST_CC=D:\gcc\mingw32\bin\gcc.exe"
    goto :explicit_compiler
)
for /f "delims=" %%G in ('where gcc.exe 2^>nul') do (
    set "CC=%%~fG"
    goto :compiler_ready
)
goto :missing_gcc

:explicit_compiler
set "CC=%BST_CC%"
goto :compiler_ready

:msys_compiler
set "CC=%BST_MSYS_PREFIX%\bin\gcc.exe"

:compiler_ready
if not exist "%CC%" goto :missing_gcc
for %%G in ("%CC%") do set "BIN_DIR=%%~dpG"
set "PATH=%BIN_DIR%;%PATH%"
if not exist build mkdir build
if not exist build goto :sqlite_failed
echo Using compiler: "%CC%"
echo Using SQLite sources: "%SQLITE_DIR%"
set "WARN=-std=c11 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00"
echo [1/3] Compiling SQLite ...
"%CC%" -std=c11 -O2 -DSQLITE_OMIT_LOAD_EXTENSION -c "%SQLITE_DIR%\sqlite3.c" -o "build\sqlite3.o"
if errorlevel 1 goto :sqlite_failed

echo [2/3] Building timestamp.exe ...
"%CC%" %WARN% -I"%SQLITE_DIR%" -municode src\cli.c src\common.c src\json.c src\session.c src\generator.c src\obs_win.c "build\sqlite3.o" -o timestamp.exe -lwinhttp -lcrypt32 -lbcrypt -lshell32 -lm
if errorlevel 1 goto :timestamp_failed

echo [3/3] Building config.exe ...
"%CC%" %WARN% -municode -mwindows src\config_gui.c src\common.c src\json.c -o config.exe -lcomdlg32 -lcrypt32 -luser32 -lgdi32 -lm
if errorlevel 1 goto :config_failed

rem Copy compiler runtime DLLs from the selected toolchain.
for %%D in (libwinpthread-1.dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll) do (
    if exist "%BIN_DIR%%%D" copy /Y "%BIN_DIR%%%D" "%~dp0%%D" >nul
)
if not exist sessions mkdir sessions
if not exist logs mkdir logs
echo.
echo Build complete: timestamp.exe / config.exe
exit /b 0

:missing_gcc
echo ERROR: GCC not found. Set BST_CC to the full path of MinGW gcc.exe.
exit /b 1

:missing_sqlite
echo ERROR: sqlite3.c and sqlite3.h are required in "%SQLITE_DIR%".
echo Restore external\sqlite or set BST_SQLITE_DIR to the amalgamation folder.
exit /b 1

:sqlite_failed
echo ERROR: Compiling SQLite or creating the build directory failed.
exit /b 1

:timestamp_failed
echo ERROR: timestamp.exe build failed.
exit /b 1

:config_failed
echo ERROR: config.exe build failed.
exit /b 1
