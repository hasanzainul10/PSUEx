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
    "uiScale": 1.5,
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
| `hudSize` | Scale of those floating panels. The town visor HUD is 1.4 m × `hudSize` tall; lower it (e.g. `0.85`) if corner UI sits too far out in your view. |
| `hudMapBlackAlpha` | Opacity kept for the black backdrop around the mission map on the visor (default **0.15**; `0` = invisible, `1` = as in the flat game). |
| `hudWorldPanel` | Default **false**: all town UI (HUD, menus, shop windows) sits on the head-locked visor exactly as the game lays it out. `true` enables the experimental world-locked kiosk panel for centered windows. |
| `uiScale` | Enlarge HUD widgets / nameplates when capturing for VR (default **1.5**). Useful at high game resolutions. Try `1.3`–`2.0`. Does not change the flat monitor. |
| `cameraMode` | `thirdPerson` (default) or `firstPerson` (town stereo only; other scenes stay on the virtual screen). |
| `fpEyeHeight` | First person: eye height above the character's feet in meters (default **1.6**). |
| `fpEyeForward` | First person: how far in front of the face the eyes sit, in meters (default **0.12**), so you don't see the inside of your own head. |
| `snapTurn` | First person: `false` (default) = the right stick turns smoothly; `true` = 30° steps (comfort). |
| `snapTurnDegrees` | Snap step size (default **30**). |
| `fpTurnSpeed` | Smooth turn speed in degrees per second (default **120**). |
| `fpMoveDirection` | `"head"` (default): left stick forward walks where you look. `"body"`: forward is where your body faces, and the head looks around without steering. |
| `fpHideBody` | Hide your own character in first person so you don't see inside it (default **true**). |
| `fpHideRadius` | How close to your character's center a mesh has to be to get hidden, in meters (default **0.6**). Raise it if a weapon or hair still pokes into view; lower it if nearby characters vanish. |

**First-person controls (gamepad):** the right stick turns your body and view. By default the left stick moves where you look, and the right stick adds turning on top. `fpMoveDirection: "body"` gives FPS-style body-relative movement instead. F9 recenters. Turning reads the right stick through XInput (Xbox-style pads). Without XInput it falls back to the game's own camera orbit.
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
