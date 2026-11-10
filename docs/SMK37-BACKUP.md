# SMK-37: the full flash backup (and what it tells us)

Before SQ-80 goes on an M-Vave SMK-37 (or a Donner Starrykey 37 Play), read the keyboard's whole 1 MiB flash and
keep it: it holds things unique to the unit (its key and pad calibration, its Bluetooth identity) that cannot be
downloaded again. The same backup tells us whether the stock firmware writes the stretch 0xEB000-0xF3FFF, which SQ-80
would like to use. This follows the port kit's method A
([fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37), docs/02-update-and-recovery.md, section 3.1).
Nothing of the keyboard's firmware is written; at the end it is put back as it was.

First use the keyboard a while on M-Vave's firmware (save presets, change settings), so the backup shows what that
firmware keeps.

## What you need

- A computer with **Linux**, or **Windows** with an administrator command prompt. Python 3.
- This repository (`git clone https://github.com/leonardolopezchavira/sq80-fm1`, or its "Download ZIP").
- `pip install mido python-rtmidi` (our install tool), and **jl-uboot-tool**:
  `git clone https://github.com/kagaimiq/jl-uboot-tool` then `pip install -r jl-uboot-tool/requirements.txt`.
- The SMK-37 package `sq80-smk37-X.Y.fwsc`: GitHub, the repository's **Actions** tab, the latest **smk37** run,
  its **packages** artifact (a zip). It is only used to get the keyboard into the right mode; it is not installed.
- A USB data cable. Close every other program that uses MIDI.

## Steps

1. Connect the keyboard. In the repository's folder, check that it is found and what it calls itself:

       python3 fm1/base/tools/fm1_install.py --info

   It should say `SMK-37 Pro_...` (or another SMK model). If it is not found, add `--port` and a part of its MIDI
   port's name.

2. Let the keyboard check the package and switch to its chip's bootloader:

       python3 fm1/base/tools/fm1_install.py sq80-smk37-X.Y.fwsc --uboot-after-check

   Answer `y`. The keyboard restarts as a USB drive named **WL82 UBOOT1.00** (Windows may say it needs to be
   formatted: say **no**, and close that window).

3. Find the drive: Linux, `ls /dev/sg*` (the newest; `python3 jl-uboot-tool/jldevfind.py` lists it); Windows, the
   drive letter Explorer gave it, written as `\\.\E:`.

4. Read the whole flash twice (Linux with `sudo`; Windows in the administrator prompt):

       python3 jl-uboot-tool/jluboottool.py --device /dev/sgN --chip wl82 "read 0 0x100000 smk37-dump-1.bin"
       python3 jl-uboot-tool/jluboottool.py --device /dev/sgN --chip wl82 "read 0 0x100000 smk37-dump-2.bin"

   Note the flash ID it prints when it connects.

5. Check them:

       python3 tools/smk_dump_check.py smk37-dump-1.bin smk37-dump-2.bin

   "backup OK" means both copies are whole, identical, and hold the calibration. Copy the two files to two places
   (another disk, a USB stick). Send the four lines under "backup OK" (and the flash ID); never the files.

6. Switch the keyboard off and on. It comes back in update mode. To keep M-Vave's firmware, install it again with
   M-Vave's updater (the stock package for your model); the calibration and Bluetooth data are untouched.

If anything fails, stop and send what it printed.
