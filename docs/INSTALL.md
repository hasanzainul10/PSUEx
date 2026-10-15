# Installing PSUEx (and VR) on Clementine

This guide is for **players** who want to drop PSUEx into an existing **Phantasy Star Universe Clementine** client folder. It assumes you already have a working game install that launches through `online.exe`.

> **Status:** VR is experimental (Phase 1). Expect bugs. Flat (non-VR) PSUEx features still work with VR turned off.

> **Players:** the step-by-step setup is in **[VR_GUIDE.md](VR_GUIDE.md)**. This page is the full reference.

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
| `PSUExVR.dll` **or** `PSUExD.dll` | Yes | The mod itself. Release builds are `PSUExVR.dll`, debug builds `PSUExD.dll`. The loader tries **`PSUExD.dll`, then `PSUExVR.dll`, then Clementine's own `PSUEx.dll`** (non-VR). Don't name the VR build `PSUEx.dll`: Clementine's launcher re-downloads that file on every start. |
| `openxr_loader.dll` | For VR | OpenXR loader (x86). Needed when `vr.enabled` is true. |
| `D3DX9_43.dll` | Often | DirectX helper used by the overlay / VR path. Copy if the game folder does not already have a working D3DX9. |
| `psuex_vr.json` | For VR | Sidecar VR settings (recommended — survives the game rewriting `config.json`). |
| `PSUExVRConfig.exe` | Optional | VR settings app that works without starting the game (edits `psuex_vr.json` in its folder). |
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
| `backgroundInput` | Keep the gamepad working when the game window isn't focused (default **true** with VR; read by `dinput8.dll` at launch). In the headset the game window is often not the focused one. |
| `backgroundKeyboard` | Also keep the keyboard working unfocused (default **false**: keys typed in other apps would reach the game). |
| `hudFollow` | HUD and menus sit on a panel in front of your body instead of following your head (default **true**). Turn your head to read the corners; look away for a moment and the panel glides back in front. `false` = the old head-locked visor. |
| `hudFollowAngle` | How far (degrees) you can look away before the panel follows (default **35**). |
| `hudDrop` | How far below eye level the panel sits, in meters (default **0.1**). |
| `hudMapBlackAlpha` | Opacity kept for the black backdrop around the mission map on the visor (default **0.15**; `0` = invisible, `1` = as in the flat game). |
| `hudWorldPanel` | Default **false**: all town UI (HUD, menus, shop windows) sits on the head-locked visor exactly as the game lays it out. `true` enables the experimental world-locked kiosk panel for centered windows. |
| `uiScale` | Enlarge HUD widgets / nameplates when capturing for VR (default **1.5**). Useful at high game resolutions. Try `1.3`–`2.0`. Does not change the flat monitor. |
| `cameraMode` | `thirdPerson` (default) or `firstPerson` (town stereo only; other scenes stay on the virtual screen). |
| `fpEyeHeight` | First person: eye height above the character's feet in meters (default **1.6**). |
| `fpEyeForward` | First person: how far in front of the face the eyes sit, in meters (default **0.12**), so you don't see the inside of your own head. |
| `tpDistance` / `tpHeight` | Third person: camera distance behind and height above your character's head, in meters (defaults **4** / **1**). The right stick zoom changes `tpDistance` live. |
| `tpZoomSpeed`, `tpMinDistance`, `tpMaxDistance` | Third person zoom speed (m/s) and range (defaults **3**, **1.5**, **10**). |
| `tpSnapTurn` | Third person turning, separate from first person: `true` (default) = steps of `tpOrbitDegrees` (default **45**); `false` = smooth at `tpTurnSpeed` (default **90** deg/s). |
| `tpPitch` / `tpTiltSpeed` | Third person camera tilt in degrees (default **0** = level horizon; negative looks down) and how fast Back + right stick left/right changes it (default **45** deg/s). A tilt also tilts the horizon. |
| `tpLbCamera` | Third person, what LB does to the VR camera: `"follow"` (default) = as vanilla, the camera swings behind you or frames the locked-on target; `"off"` = LB never moves the camera. |
| `fpLbCamera` | First person: `"off"` (default) = LB locks on but your view stays yours; `"follow"` = the view turns toward the target (less comfortable). |
| `lbWindowSeconds` | How long after an LB press the game's camera direction is followed (default **2**; also while LB is held). |
| `snapTurn` | First person: `true` (default, comfort) = the right stick turns in steps; `false` = smooth turning. |
| `snapTurnDegrees` | Snap step size (default **30**). |
| `comfortVignette` | Darken the edges of your view while you smooth-turn or move with the stick (default **false**, opt-in). |
| `vignetteStrength` | How far the vignette closes in at full speed, 0-1 (default **0.7**). |
| `fpTurnSpeed` | Smooth turn speed in degrees per second (default **120**). |
| `fpMoveDirection` | `"head"` (default): left stick forward walks where you look. `"body"`: forward is where your body faces, and the head looks around without steering. |
| `fpBody` | Your own character in first person: `"headless"` (default) cuts away the head and hair so arms, hands, torso and legs stay visible; `"hidden"` removes the whole body; `"full"` draws everything. |
| `fpNeckDrop` | How far below your eyes the headless cut sits, in meters (default **0.22**). Raise it if hair or the head still shows; lower it if the shoulders disappear. |
| `fpLeanLimit` | How far you can physically lean before your character's body follows, in meters (default **0.15**). Walking around your room no longer pulls the view out of the body. `0` locks the view to the body. |
| `fpCrouchLimit` | The same for ducking and standing up, in meters (default **0.2**). |
| `stereoPanel` | Leave **`false`**. Turning it on also shows a flat PiP of the monitor frame in stereo (usually worse). |

**Comfort:** the mod never moves your view on its own. Only the stick and your own head move it: game camera re-aims (NPC talk), smoothing and auto-follow are cancelled, and the horizon stays level, also in third person. If you get motion sick, use first person with snap turn (the defaults), keep `hudFollow` on, try `comfortVignette: true`, and recenter (Ctrl+F9) whenever "forward" feels off.

**First-person controls (gamepad):** the right stick turns your body and view. By default the left stick moves where you look, and the right stick adds turning on top. `fpMoveDirection: "body"` gives FPS-style body-relative movement instead. Recenter with Ctrl+F9 or Back+X. Turning reads the right stick through XInput (Xbox-style pads). Without XInput it falls back to the game's own camera orbit.

You can also put a `"vr"` object inside `config.json`, but Clementine or other tools sometimes rewrite that file. **`psuex_vr.json` overrides it** and is the safer place for VR settings.

To play **without** VR, set `"enabled": false` (or delete `psuex_vr.json` and leave VR off in `config.json`).

### VR settings UI

You don't have to edit JSON by hand:

- **Without the game:** run `PSUExVRConfig.exe` from the game folder (or point it at the folder with Browse). It edits `psuex_vr.json`. Close the game first: the game rewrites that file when it saves.
- **In game:** press **Ctrl+F10** (or hold **Back** and press **Start**), or open the PSUEx menu with **Insert** → Window → **VR Settings**. In the headset the window shows on the HUD panel, with a mouse pointer drawn for you. Changes apply immediately; **Save** writes `psuex_vr.json`. **Reload from file** picks up edits made in `PSUExVRConfig.exe`.

**VR on/off:** "VR enabled" applies on the **next launch** (the game's graphics device is set up for VR at startup). **Pause VR** works instantly: the game turns flat and the headset shows it on a big screen. Pause is not saved.

### VR controls

Keyboard shortcuts use Ctrl because the game uses the plain F-keys (F1-F7 are main-menu shortcuts). Rebind them under **Controls** in either UI (hold Ctrl/Shift/Alt with the key to include them); after upgrading, **Reset controls** gives these defaults. Pad shortcuts are chords: **hold Back/Select, then press the button**.

| Action | Key | Pad |
|---|---|---|
| Recenter | Ctrl + F9 | Back + X |
| Switch first / third person | Ctrl + F7 | Back + Y |
| Show / hide VR settings | Ctrl + F10 | Back + Start |
| Pause VR | Ctrl + F8 | — |
| Snap / smooth turn | — | — |
| Body-locked HUD on / off | — | — |

**Third person camera:** the camera never rotates by itself. Right stick left/right turns it around your character (snap steps or smooth, see `tpSnapTurn`), up/down zooms. Hold **Back** and push the right stick left/right to tilt the camera, or up/down to raise or lower it in 0.5 m steps. **LB** works like vanilla: the camera swings behind your character, or frames the target you lock on to (`tpLbCamera`). The left stick moves relative to the camera.

Each shortcut shows a short message in the headset. The game still sees the Back and button presses (PSUEx can't block pad input yet), so pick chords you don't need in the moment.

---

## Launch

1. Put on / wake the headset and start your OpenXR runtime (SteamVR, Quest Link, …).
2. Start the game with **`online.exe`** from the Clementine folder.
3. Log in as usual. Menus should appear as a large floating screen; town (mode 5) uses stereo 3D with floating UI panels.

### In-headset tips

- **Ctrl+F9** or **Back+X** recenters the VR camera (keys need the game window focused). See [VR controls](#vr-controls).
- Use a **gamepad** for now (`inputMode": "gamepad"`). Motion controllers are not finished.
- The HUD and menus sit on a panel in front of your body (`hudFollow`); turn your head to read its corners.

---

## Verify it loaded

After one launch, check these files in the game folder:

| Log | What you want to see |
|-----|----------------------|
| `dinput8_proxy.log` | For `online.exe`: `Skip PSUEx (host is not PSUC)`. For the game process: `Loaded PSUExVR.dll` (or `Loaded PSUExD.dll` for a debug build). `Loaded PSUEx.dll` means Clementine's stock PSUEx was used, without VR. |
| `psuex.log` | PSUEx started; with VR on, lines about OpenXR / stereo / HUD quads. |

If `dinput8_proxy.log` says it failed to load PSUEx, the DLL is missing, wrong bitness (must be **x86**), or a dependency is missing.

---

## Troubleshooting

**Game starts but no VR / no overlay**

- Confirm `dinput8.dll` is in the game folder (not only `di.dll` — that is Clementine’s own helper and does **not** load PSUEx).
- Confirm `PSUExVR.dll` (or `PSUExD.dll`) is present and **32-bit**.
- Read `dinput8_proxy.log` and `psuex.log`.

**Crash or black headset**

- Start the OpenXR runtime before the game.
- Ensure `openxr_loader.dll` (x86) is next to the game exe.
- Try `"enabled": false` in `psuex_vr.json` to confirm the flat client still works.

**Two copies of PSUEx / weird overlay**

- Clementine's `online.exe` injects and re-downloads its own `PSUEx.dll`. PSUEx VR blocks that second copy when it is loaded first (`Blocked second PSUEx load` in `psuex.log`); never rename the VR build to `PSUEx.dll`.

**Menus fine, town broken / UI wrong**

- Keep `"stereoMode": 5` and `"stereoPanel": false`.
- Tune `hudDistance` / `hudSize` if panels feel too close or too large.

**Uninstall**

1. Delete `dinput8.dll`, `PSUExVR.dll` / `PSUExD.dll` (keep Clementine's `PSUEx.dll`), and optionally `openxr_loader.dll` / `psuex_vr.json` from the game folder.
2. Leave Clementine’s own files (`di.dll`, `PSUC.exe`, `online.exe`, …) alone.
3. Launch with `online.exe` as before — the stock client should run without the mod.

---

## Server / ToS note

Check **your private server’s mod rules** before using PSUEx online. This project hooks the local client only; it does not document or encourage server-side cheating.
