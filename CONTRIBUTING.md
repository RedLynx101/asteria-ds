# Contributing to Asteria DS

Contributions, experiments, and questions are welcome. Asteria DS is a small old-3DS homebrew companion for the Asteria Command Station, and it is meant to be approachable for people interested in handheld UI, robotics teleop, agent prompts, and homebrew development.

Good contribution areas include:

- UI refinements that make status, teleop, and prompt entry clearer on 3DS screens.
- Safer handheld control behavior, including stop, lease, timeout, and reconnect flows.
- Build, install, and troubleshooting documentation for devkitPro and real hardware.
- Small audio, visual, or accessibility improvements that fit the handheld constraints.
- Bug reports with hardware model, network setup, config shape, screenshots, and reproduction steps.

Please keep changes practical and reviewable:

- Do not commit generated configs, bearer tokens, build outputs, SD-card dumps, or personal runtime artifacts.
- Keep the app scoped to the authenticated Asteria mobile bridge; live robot dependencies belong in the host repo.
- Prefer focused pull requests over broad rewrites.
- Be clear about whether a change was tested in an emulator, built only, or run on real 3DS hardware.

This repository pairs with Asteria Command Station: https://github.com/RedLynx101/asteria-command-station
