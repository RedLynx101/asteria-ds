# Config

`config.example.json` documents the shape of the 3DS config file.

Real device configs contain bearer tokens and are ignored by Git:

```text
config/config.json
config/config.generated.json
```

Generate a real config from the sibling host repo:

```powershell
cd ..\asteria-command-station
python .\scripts\asteria_mobile_setup.py
```
