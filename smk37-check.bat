@echo off
rem SMK-37: check the backup in the "backup" folder again (no keyboard needed). Put it in the sq80-fm1 folder.
cd /d "%~dp0"
py -3.12 tools\smk_dump_check.py backup\smk37-dump-1.bin backup\smk37-dump-2.bin
echo.
echo Send Claude the lines above (never the files).
pause
