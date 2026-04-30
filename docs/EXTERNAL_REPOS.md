# External Repositories

This repo intentionally stays small and client-focused.

## Required Pair

- [RedLynx101/asteria-command-station](https://github.com/RedLynx101/asteria-command-station)
  Host daemon, mobile bridge, config generator, robot runtime, and desktop command station.

## Toolchain

- devkitPro
- devkitARM
- libctru

These are installed system-wide and are not vendored.

## Optional Systems

OpenClaw, VEX AIM tools, and any agent runtime sit behind the host Asteria daemon. The 3DS app should talk only to the Asteria mobile bridge.

For live robot work, the host repo may use:

- [`touretzkyds/vex-aim-tools`](https://github.com/touretzkyds/vex-aim-tools)
- [`touretzkyds/AIM_Websocket_Library`](https://github.com/touretzkyds/AIM_Websocket_Library)

Those are host-side dependencies and should not be copied into this 3DS client repo.
