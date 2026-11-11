@echo off
setlocal EnableDelayedExpansion

:: Always run from the repository root, wherever the script was started from
cd /d "%~dp0..\.."

:: Configuration
set "MSYS2_URL=https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe"
set "INSTALLER_EXE=msys2_installer.exe"
set "INSTALL_DIR=C:\msys64"

:: Define the specific bins to add to PATH
set "USR_BIN=%INSTALL_DIR%\usr\bin"
set "MINGW_BIN=%INSTALL_DIR%\mingw32\bin"
:: gdb only exists as a 64-bit package; it can still debug the 32-bit build
set "MINGW64_BIN=%INSTALL_DIR%\mingw64\bin"

echo [1/5] Downloading MSYS2 installer...
curl -L %MSYS2_URL% -o %INSTALLER_EXE%

echo [2/5] Installing MSYS2 silently to %INSTALL_DIR%...
start /wait "" %INSTALLER_EXE% in --confirm-command --accept-messages --root %INSTALL_DIR%

if not exist "%USR_BIN%\bash.exe" (
    echo Installation failed.
    exit /b 1
)

echo [3/5] Installing MinGW32, CMake and GDB...
"%USR_BIN%\bash.exe" -lc "pacman -Syu --noconfirm"
"%USR_BIN%\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-i686-toolchain mingw-w64-i686-cmake make mingw-w64-x86_64-gdb"

echo [4/5] Adding MSYS2, MinGW32 and GDB to User PATH...
:: Append each folder to the User PATH only if it is missing.
:: PowerShell is used instead of setx, which truncates PATH at 1024 characters.
set "PATH_DIRS=%USR_BIN%;%MINGW_BIN%;%MINGW64_BIN%"
powershell -NoProfile -Command "$parts = @([Environment]::GetEnvironmentVariable('Path','User') -split ';' | Where-Object { $_ }); $added = $false; foreach ($d in $env:PATH_DIRS -split ';') { if ($parts -notcontains $d) { $parts += $d; $added = $true } }; if ($added) { [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User'); 'PATH updated successfully. Open a new Command Prompt to use it.' } else { 'Paths already exist in Environment Variables.' }"

echo.
echo Setup Complete!

if not exist build-win32 md build-win32
cd build-win32

echo [5/5] Executing build inside MSYS2 shell...

:: Use MSYSTEM=MINGW32 to ensure the correct environment variables are loaded
:: CHERE_INVOKING=1 keeps the login shell in the current directory
set "CHERE_INVOKING=1"
set "MSYSTEM=MINGW32"

:: --fresh discards any cache left by another generator (e.g. the Linux cross-build)
:: MSYS Makefiles uses the MSYS 'make' installed above; MSYS2 no longer ships mingw32-make for i686
"%USR_BIN%\bash.exe" -lc "cmake .. --fresh -G 'MSYS Makefiles' && cmake --build . -j"
set "BUILD_RC=!ERRORLEVEL!"

:: Copy libwinpthread next to every installed GCC version
for /d %%G in ("%INSTALL_DIR%\mingw32\lib\gcc\i686-w64-mingw32\*") do (
    copy /y "%MINGW_BIN%\libwinpthread-1.dll" "%%G\" >nul
)

if %BUILD_RC% EQU 0 (
    echo Build complete.
) else (
    echo Build failed with error %BUILD_RC%
)

del %INSTALLER_EXE%
pause
