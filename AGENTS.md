# Agent Guide

You are working in the public Asteria DS homebrew client repo.

## Boundaries

- Keep this repo focused on the 3DS client.
- Do not copy host daemon code into this repo.
- Do not commit generated `config.json`, `config.generated.json`, bearer tokens, deploy backups, or built `.3dsx`/`.elf`/`.smdh` files.
- Treat `../asteria-command-station` as the host-side source of truth for bridge behavior.

## Build Expectations

The normal build path requires devkitPro and libctru:

```powershell
make
```

If devkitPro is unavailable, inspect and validate source changes without pretending the 3DS build passed.

## Bridge Contract

When changing network behavior, check the host bridge docs and keep the app aligned with:

- mobile auth header handling
- status parsing
- RGB565 preview dimensions
- teleop claim/release/stop semantics
- prompt submission payloads

## UX Rules

- Keep button labels short enough for the 3DS screen.
- Preserve emergency stop access.
- Prefer explicit status and audio feedback over hidden state.
- Keep controls usable with touch and physical buttons.
