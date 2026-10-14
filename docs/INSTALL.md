# Installing PSUEx (and VR) on Clementine

This guide is for **players** who want to drop PSUEx into an existing **Phantasy Star Universe Clementine** client folder. It assumes you already have a working game install that launches through `online.exe`.

> **Status:** VR is experimental (Phase 1). Expect bugs. Flat (non-VR) PSUEx features still work with VR turned off.

---

## Requirements

| Item | Notes |
|------|--------|
| Game | Clementine client matching the build PSUEx was built for (Illuminus **14.30** addresses) |
| OS | Windows 10/11, **64-bit OS is fine** — the game and DLLs are **32-bit (x86)** |
| Launcher | Always start with **`online.exe`** (do not double-click `PSUC.exe` alone) |
| VR (optional) | An OpenXR runtime (SteamVR, Meta Quest Link / Air Link, WMR OpenXR, etc.) and a headset |

---

## Files to copy

Put these files in the **same folder as `PSUC.exe` / `online.exe`**:

| File | Required? | What it does |
|------|-----------|--------------|
| `dinput8.dll` | Yes | Loader proxy. Forwards real DirectInput to Windows and loads PSUEx into `PSUC.exe` only. |
| `PSUEx.dll` **or** `PSUExD.dll` | Yes | The mod itself. Debug builds are named `PSUExD.dll`; release builds are `PSUEx.dll`. The loader tries **`PSUExD.dll` first**, then `PSUEx.dll`. |
| `openxr_loader.dll` | For VR | OpenXR loader (x86). Needed when `vr.enabled` is true. |
| `D3DX9_43.dll` | Often | DirectX helper used by the overlay / VR path. Copy if the game folder does not already have a working D3DX9. |
| `psuex_vr.json` | For VR | Sidecar VR settings (recommended — survives the game rewriting `config.json`). |
| `translation.txt` | Optional | Japanese translation overlay (see [README](../README.md)). |

### Where to get the binaries

- **Prebuilt package** (if you were given a zip): extract the files above into the game folder.
- **From this repo** (developers): build `PSUEx.sln` as **Debug|x86** or **Release|x86**, build `tools/dinput8_proxy`, then run:

  ```powershell
  .\tools\deploy-debug.ps1 -GameDir "D:\Path\To\Your\Clementine"
  ```

  That copies `PSUExD.dll`, `dinput8.dll`, and `openxr_loader.dll` for you.

---

## Enable VR

Create (or edit) **`psuex_vr.json`** next to `PSUC.exe`:

```json
{
    "enabled": true,
    "runtime": "openxr",
    "stereo": true,
    "stereoMode": 5,
    "hudDistance": 2.5,
    "hudSize": 1.0,
    "worldScale": 1.0,
    "stereoPanel": false,
    "stereoSkipScreenQuads": true,
    "cameraMode": "thirdPerson",
    "inputMode": "gamepad"
}
```

| Setting | Meaning |
|---------|---------|
| `enabled` | Master VR switch. `false` = normal flat PSUEx. |
| `stereo` | Per-eye 3D in supported scenes (default town). |
| `stereoMode` | Scene mode that gets stereo. **`5` = town**. `0` = every mode (old / experimental). Login and menus stay on a large head-locked virtual screen. |
| `hudDistance` | Distance in meters for the virtual screen / HUD panels (~2.5 is a good start). |
| `hudSize` | Scale of those panels. |
| `stereoPanel` | Leave **`false`**. Turning it on also shows a flat PiP of the monitor frame in stereo (usually worse). |

You can also put a `"vr"` object inside `config.json`, but Clementine or other tools sometimes rewrite that file. **`psuex_vr.json` overrides it** and is the safer place for VR settings.

To play **without** VR, set `"enabled": false` (or delete `psuex_vr.json` and leave VR off in `config.json`).

---

## Launch

1. Put on / wake the headset and start your OpenXR runtime (SteamVR, Quest Link, …).
2. Start the game with **`online.exe`** from the Clementine folder.
3. Log in as usual. Menus should appear as a large floating screen; town (mode 5) uses stereo 3D with floating UI panels.

### In-headset tips

- **F9** recenters the VR camera (game window must be focused).
- Use a **gamepad** for now (`inputMode": "gamepad"`). Motion controllers are not finished.
- Town HUD/personal chrome is a **head-locked visor**; shop / terminal prompts prefer a **world-locked** panel in front of you.

---

## Verify it loaded

After one launch, check these files in the game folder:

| Log | What you want to see |
|-----|----------------------|
| `dinput8_proxy.log` | For `online.exe`: `Skip PSUEx (host is not PSUC)`. For the game process: `Loaded PSUExD.dll` or `Loaded PSUEx.dll`. |
| `psuex.log` | PSUEx started; with VR on, lines about OpenXR / stereo / HUD quads. |

If `dinput8_proxy.log` says it failed to load PSUEx, the DLL is missing, wrong bitness (must be **x86**), or a dependency is missing.

---

## Troubleshooting

**Game starts but no VR / no overlay**

- Confirm `dinput8.dll` is in the game folder (not only `di.dll` — that is Clementine’s own helper and does **not** load PSUEx).
- Confirm `PSUExD.dll` or `PSUEx.dll` is present and **32-bit**.
- Read `dinput8_proxy.log` and `psuex.log`.

**Crash or black headset**

- Start the OpenXR runtime before the game.
- Ensure `openxr_loader.dll` (x86) is next to the game exe.
- Try `"enabled": false` in `psuex_vr.json` to confirm the flat client still works.

**Two copies of PSUEx / weird overlay**

- Some launchers also inject `PSUEx.dll`. Prefer one loader: either the `dinput8` proxy + `PSUExD.dll`, or the launcher’s DLL — not conflicting debug/release pairs fighting each other. Check `psuex.log` for duplicate-load messages.

**Menus fine, town broken / UI wrong**

- Keep `"stereoMode": 5` and `"stereoPanel": false`.
- Tune `hudDistance` / `hudSize` if panels feel too close or too large.

**Uninstall**

1. Delete `dinput8.dll`, `PSUEx.dll` / `PSUExD.dll`, and optionally `openxr_loader.dll` / `psuex_vr.json` from the game folder.
2. Leave Clementine’s own files (`di.dll`, `PSUC.exe`, `online.exe`, …) alone.
3. Launch with `online.exe` as before — the stock client should run without the mod.

---

## Server / ToS note

Check **your private server’s mod rules** before using PSUEx online. This project hooks the local client only; it does not document or encourage server-side cheating.
