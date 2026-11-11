@echo off
rem SMK-37 / Donner Starrykey 37: install SQ-80 (docs/SMK37-BACKUP.md, "Then"). Only after the backup.
rem Put this file in the sq80-fm1 folder and double-click it. It takes the newest sq80-smk37 package it finds.
setlocal
cd /d "%~dp0"
echo.
echo === SQ-80 on the SMK-37 ===
echo.
if not exist "fm1\base\tools\fm1_install.py" (
  echo This file is not in the sq80-fm1 folder. Move it next to the folders fm1, tools and app.
  goto end
)
if not exist "backup\smk37-dump-1.bin" (
  echo No backup found in the "backup" folder. Run smk37-backup.bat first.
  goto end
)
set "PKG="
for /f "delims=" %%f in ('powershell -NoProfile -Command "Get-ChildItem -Path '%CD%','%USERPROFILE%\Downloads' -Recurse -Filter 'sq80-smk37-*.fwsc' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch 'elite|mke' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName"') do set "PKG=%%f"
if not defined PKG (
  echo The package sq80-smk37-....fwsc was not found. Download "packages" from the newest smk37 run on GitHub
  echo ^(Actions tab^), unzip it into Downloads, then double-click this again.
  goto end
)
echo Package: %PKG%
echo.
echo The keyboard: switched off and on after the backup (it is in update mode), connected by USB.
echo Close every program that uses MIDI.
pause
py -3.12 fm1\base\tools\fm1_install.py "%PKG%"
echo.
echo When it says "done", open the Load page in Chrome and send the ROM pack.
:end
echo.
pause
