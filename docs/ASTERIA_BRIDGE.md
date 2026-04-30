# Asteria Bridge Contract

Asteria DS is a client of the host daemon in [RedLynx101/asteria-command-station](https://github.com/RedLynx101/asteria-command-station).

## Authentication

Every bridge request includes:

```text
Authorization: Bearer <device_token>
```

Generate the token from the host repo:

```powershell
cd ..\asteria-command-station
python .\scripts\asteria_mobile_setup.py
```

Copy the generated Asteria DS import config to:

```text
sdmc:/3ds/asteria-ds/config.json
```

## Endpoints

The current app uses:

- `GET /api/mobile/status`
- `GET /api/mobile/images/preview?width=176&height=132`
- `POST /api/mobile/prompt`
- `POST /api/mobile/teleop/claim`
- `POST /api/mobile/teleop/release`
- `POST /api/mobile/teleop/vector`
- `POST /api/mobile/teleop/stop`
- `POST /api/mobile/teleop/command`
- `POST /api/mobile/images/capture`

## Control Semantics

- Claim before teleop.
- Release when done.
- Stop should halt handheld motion without silently unloading an active host FSM.
- Emergency stop should remain reachable from the main Pilot screen.

## Preview Semantics

The 3DS does not decode arbitrary full JPEGs here. The host generates cached RGB565 preview frames sized for the top screen budget. The app requests a small preview and renders it directly.
