# Tsugaru (FM Towns) - libretro core

A libretro core port of TOWNSEMU (the FM Towns / Marty emulator "Tsugaru"),
so you can run FM Towns software inside RetroArch and any libretro frontend.

## Status

- Core name: tsugaru_libretro
- Base: TOWNSEMU v20260522 Pre-release
- License: BSD 3-Clause (TOWNSEMU) + MIT (libretro.h)
- Platforms: Windows x64, Linux x86_64 and aarch64, macOS arm64 and x86_64,
  Android arm64-v8a, armeabi-v7a and x86_64. The GitHub Actions workflow
  `libretro.yml` builds all of them; `.gitlab-ci.yml` is the recipe for the
  libretro buildbot, which names the core `tsugaru_libretro`.
- [Latest Release](https://github.com/duskgao/Tsugaru-libretro/releases)

## Download (Windows)

Grab the prebuilt towns_libretro.dll from the
[Releases](https://github.com/duskgao/Tsugaru-libretro/releases) page, rename it
to tsugaru_libretro.dll, and place it in RetroArch's cores/ directory.
Put the BIOS ROMs in RetroArch's system/: FMT_SYS.ROM, FMT_DOS.ROM,
FMT_FNT.ROM and FMT_DIC.ROM are required (FMT_F20.ROM is optional), or a
combined FMT_ALL.ROM.

## Features

- 60 fps VSync-driven stepping (no internal sleep)
- Video: XRGB8888 capture from the VM renderer
- Audio: FM / PCM / CDDA mixed to 16-bit stereo at 44100
- Input: keyboard, gamepad, physical mouse
- Core options: machine model, CPU, memory, FPU, CD speed, extra hard disk, etc.
- Save states (retro_serialize), CMOS / floppy / memcard persistence

## Build and Porting Notes

See src/main_libretro/README.md for porting notes and
使用说明书.md for a step-by-step beginner guide (Chinese).

## Upstream

- TOWNSEMU (Tsugaru): https://github.com/captainys/TOWNSEMU
- Free ROM: http://ysflight.com/FM/towns/FreeTOWNS/e.html