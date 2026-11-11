@echo off
rem SMK-37 / Donner Starrykey 37: the full flash backup in one go (docs/SMK37-BACKUP.md).
rem Put this file in the sq80-fm1 folder (the one with fm1, tools, app) and double-click it.
rem Nothing of the keyboard is changed: it reads the flash twice and checks the copies.
setlocal enabledelayedexpansion

rem ---- as administrator (reading the USB drive needs it)
net session >nul 2>&1
if errorlevel 1 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
cd /d "%~dp0"

echo.
echo === SMK-37 backup ===
echo.
if not exist "fm1\base\tools\fm1_install.py" (
  echo This file is not in the sq80-fm1 folder. Move it next to the folders fm1, tools and app, then double-click it again.
  goto end
)

rem ---- Python 3.12
set "PY=py -3.12"
%PY% --version >nul 2>&1
if errorlevel 1 (
  echo Python 3.12 was not found. Install it from python.org ^(Windows installer 64-bit^), then double-click this again.
  goto end
)
echo Getting the Python packages ready...
%PY% -m pip install -q mido python-rtmidi crcmod pyyaml pycryptodomex tqdm
if errorlevel 1 (
  echo The packages did not install. Send Claude what is printed above.
  goto end
)

rem ---- jl-uboot-tool, anywhere in this folder or in Downloads
set "JLU="
for /f "delims=" %%f in ('where /r "%CD%" jluboottool.py 2^>nul') do if not defined JLU set "JLU=%%f"
if not defined JLU for /f "delims=" %%f in ('where /r "%USERPROFILE%\Downloads" jluboottool.py 2^>nul') do if not defined JLU set "JLU=%%f"
if not defined JLU (
  echo jl-uboot-tool was not found. Unzip it into this folder ^(or Downloads^), then double-click this again.
  goto end
)

rem ---- the SMK package (the plain one: not -elite, not -mke), here or in Downloads; a packages zip is unzipped
set "PKG="
call :findpkg
if not defined PKG (
  for /f "delims=" %%z in ('where /r "%USERPROFILE%\Downloads" packages*.zip 2^>nul') do (
    echo Unzipping %%z ...
    powershell -NoProfile -Command "Expand-Archive -Force -LiteralPath '%%z' -DestinationPath '%CD%\packages'"
  )
  call :findpkg
)
if not defined PKG (
  echo The package sq80-smk37-0.13.2.fwsc was not found. Download "packages" from the smk37 run on GitHub
  echo ^(Actions tab^) into Downloads, then double-click this again.
  goto end
)
echo Package: %PKG%
echo.

rem ---- 1. the keyboard
echo Step 1 of 4: connect the keyboard by USB and close every program that uses MIDI.
echo (Is the keyboard already in its USB drive mode from an earlier try? Then type R and press Enter.)
set "GO="
set /p GO=Otherwise just press Enter: 
if /i "%GO%"=="R" (
  set "DEV="
  goto reads
)
%PY% fm1\base\tools\fm1_install.py --info
if errorlevel 1 (
  echo The keyboard was not found. Send Claude what is printed above.
  goto end
)
echo.

rem ---- 2. into the chip's bootloader
echo Step 2 of 4: the keyboard checks the package and restarts as a USB drive "WL82 UBOOT1.00".
echo Answer y when asked. If Windows offers to format the drive: click NO.
%PY% fm1\base\tools\fm1_install.py "%PKG%" --uboot-after-check
if errorlevel 1 (
  echo That did not work. Send Claude what is printed above.
  goto end
)
echo.
echo Wait 10 seconds. If a drive WL82 UBOOT1.00 shows in Explorer, type its letter (for example E).
set "DRV="
set /p DRV=Otherwise just press Enter and it will be found by itself: 
set "DRV=%DRV::=%"
set "DEV="
if defined DRV set "DEV=--device \\.\%DRV%:"
echo.

:reads
rem ---- 3. two reads
if not exist backup mkdir backup
echo Step 3 of 4: reading the flash, twice (a minute or two each). Note the flash ID it prints.
echo (If it keeps saying "No devices found yet", the keyboard is not in its USB drive mode: send Claude this window.)
%PY% "%JLU%" %DEV% --chip wl82 "read 0 0x100000 backup\smk37-dump-1.bin"
if errorlevel 1 goto readfail
%PY% "%JLU%" %DEV% --chip wl82 "read 0 0x100000 backup\smk37-dump-2.bin"
if errorlevel 1 goto readfail
echo.

rem ---- 4. the check
echo Step 4 of 4: checking the two copies.
echo.
%PY% tools\smk_dump_check.py backup\smk37-dump-1.bin backup\smk37-dump-2.bin
echo.
echo Send Claude the lines above and the flash ID. Never send the files in the "backup" folder:
echo copy that folder to a USB stick as well. Then switch the keyboard off and on.
goto end

:readfail
echo Reading did not work. Check the drive letter, then send Claude what is printed above.

:end
echo.
pause
exit /b

:findpkg
for /f "delims=" %%f in ('where /r "%CD%" sq80-smk37-*.fwsc 2^>nul ^| findstr /v /i "elite mke"') do if not defined PKG set "PKG=%%f"
if not defined PKG for /f "delims=" %%f in ('where /r "%USERPROFILE%\Downloads" sq80-smk37-*.fwsc 2^>nul ^| findstr /v /i "elite mke"') do if not defined PKG set "PKG=%%f"
exit /b
