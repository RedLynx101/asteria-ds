# External Repositories

This repo intentionally stays small and client-focused.

## Required Pair

- `../asteria-command-station`
  Host daemon, mobile bridge, config generator, robot runtime, and desktop command station.

## Toolchain

- devkitPro
- devkitARM
- libctru

These are installed system-wide and are not vendored.

## Optional Systems

OpenClaw, VEX AIM tools, and any agent runtime sit behind the host Asteria daemon. The 3DS app should talk only to the Asteria mobile bridge.
