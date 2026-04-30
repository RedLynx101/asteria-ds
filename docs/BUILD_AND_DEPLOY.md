# Build And Deploy

## Requirements

- devkitPro
- devkitARM
- libctru
- 3DS Homebrew Launcher or equivalent modded 3DS setup

PowerShell should expose `DEVKITARM` and the devkitPro toolchain on `PATH`.

## Build

```powershell
make
```

The Makefile produces:

```text
asteria-ds.3dsx
asteria-ds.elf
asteria-ds.smdh
```

## Configure

Create host-side mobile bridge config:

```powershell
cd ..\asteria-command-station
python .\scripts\asteria_mobile_setup.py
```

Copy the generated `asteria-ds-import-config.json` to the handheld as:

```text
sdmc:/3ds/asteria-ds/config.json
```

Do not commit that generated config.

The host setup lives in [RedLynx101/asteria-command-station](https://github.com/RedLynx101/asteria-command-station).

## Install

Copy the app to:

```text
sdmc:/3ds/asteria-ds/asteria-ds.3dsx
```

Keep the ROMFS audio assets bundled through the build, not copied separately.

## Run

Start Asteria on the laptop:

```powershell
cd ..\asteria-command-station
powershell -ExecutionPolicy Bypass -File .\asteria\start_asteria.ps1 -BindHost 0.0.0.0
```

Launch Asteria DS from the Homebrew Launcher on the same trusted network.
