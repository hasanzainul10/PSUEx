# VR work handoff (2026-10-20)

State of the `psu_vr` branch at hand-over, for the next session. Everything listed is committed
and pushed to origin `psu_vr` unless noted.

## Environment / workflow

- Build: `D:\VSBuildTools\MSBuild\Current\Bin\MSBuild.exe PSUEx.sln -p:Configuration=Debug|Release -p:Platform=x86 -m -v:minimal` (vswhere doesn't work).
- Deploy (only when PSUC.exe / PSUExVRConfig.exe aren't running): copy `Debug\PSUExD.dll` **and**
  `Debug\PSUExVRConfig.exe` to `D:\Games\PSUClementine_22072025`. The user tests the Debug DLL.
- Log: `D:\Games\PSUClementine_22072025\psuex.log` (huge). Read it with `tools/lastlog.sh [-d] [PATTERN]` (last attach only, repeats collapsed; `-h` for options).
- Settings: `psuex_vr.json` (read last, wins) and `config.json` (`vr` object) in the game folder. Keep both in sync when editing; back up first (`.bakN`).
- Edits with `\n` inside C strings: write a Python script with the Write tool (Bash heredocs turn `\n` into real newlines and break the build).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Features must be toggleable (a setting + a checkbox in `VRSettingsPanel.cpp`).
- Ghidra MCP: often disconnected; fallback `uv run --with capstone --with pefile tools/disasm.py 0xADDR 0xLEN` (set `UV_CACHE_DIR=D:\PSUExTools\uv-cache`).
- The user prefers plain short lists, wants to be told what to test, and reports results; never enter credentials/PINs for them.

## In-flight at hand-over (do these first)

1. **The game folder is in a test state (updated 2026-10-20, later session).** The loader
   `dinput8.dll` is back in place, and the latest Debug build (with `aa61991` + `68fa71c`) **is
   deployed**. VR is still `"enabled": false` in both settings files (backups
   `psuex_vr.json.bak5` / `config.json.bak5`) for the VR-off map test below. No game run has
   happened since. After the map test(s): set VR `enabled` true again in both files.
2. **Mission map test.** Vanilla (no PSUEx) shows the map terrain (off-centre: the game's own
   high-resolution problem). With PSUEx + VR the terrain is missing (only the box + north
   indicator, a stuck player marker). Every logged session had VR on, so the pending test is PSUEx
   with VR truly off. Hypothesis: the D3D9Ex upgrade (`PSUVR/D3D9ExCompat.cpp`,
   `RemapManagedTexture`: MANAGED -> DEFAULT + DYNAMIC) breaks a CPU-written / managed map texture.
   If VR-off works, fix the remap for that texture rather than disabling the upgrade.
   With VR off and `forceD3D9Ex` / `d3d9exOnly` false, the Ex upgrade is not installed
   (`VRSystem.cpp`), so this test isolates it. If the map works VR-off, the next test is
   `"d3d9exOnly": true` (Ex upgrade, no VR): broken map there confirms the remap. The last VR log
   shows no `D3D9ExCompat ... FAILED` creates.
   Note: commit `8696cb6` (route map pieces by FVF) made the map vanish and was reverted (`aa61991`):
   the VRHud pass-B routing misreads transformed 3D (non-XYZRHW) map draws at several checks
   (`ClassifyWorldLayer` sent pieces to the hidden world panel).
3. **Glare (floating glow off enemies, glow through the weapon, "rectangles").** Layer bisect found:
   layer `0x12` = `TLandDisplayerHevenTask` (sky / galaxy), `0x1F` = `TBlur`, `0x20` = `TGlare`.
   These layers make no draw calls themselves; the visible output is a later full-screen blend.
   Hiding layer `0x20` removes glow + rectangles but freezes the right eye (stale image), and
   hiding a layer from area start crashed on mission entry (object never set up). Current attempt
   (`68fa71c`, untested): with `eyeGlare` off, skip the draw at return address `0x004B6331` (full-screen
   alpha blend of a 2560x1440 RT) in eye passes only. If that's wrong, use the Debug draw-site
   bisect (Ctrl+Shift+Insert / Delete) to find the composite draw.
4. **Galaxy**: solved by hiding the sky layer in the headset (`eyeSky` false; set in the user's
   settings). Moving the sky per eye (`skyAtEye`) and keeping the viewport depth range didn't change
   it: the mission's sky is a partial box made for the flat camera.

## Features done this stretch (all toggles in the VR settings panel)

- Combat by weapon category (`+0x2D4`/`+0x2D8`): trigger/swing = attack / tech slot 1, grip + = PA /
  slot 2 (`gripPhotonArt`); staff/rod/TCSM casting; rod slots 3/4 on the left (R1, unverified);
  bow on the left hand (aims, fires, PA); RCSM no controls; class `0x21` twin handguns (no free aim).
- Arms: Photon Art / technique animation toggle (`handsPhotonArtAnimation`, default off);
  staff/rod held still while casting (`techWeaponStill`).
- Free aim: "leash" (`aimLeash`, `aimLeashMeters` 10): reticle on the controller line, shots aimed
  from the game's firing point (player + ~1.07 m up, learned from bullets) to the reticle. Per-weapon
  gun model angle (`gunAdjust{Handgun,Bow,Rifle}{Pitch,Yaw}`) set in the headset with both grips +
  right stick while aiming. Old gun pointing (`gunPointsAtAim`) off/ignored. Works well per the user.
- Effects: world sprites from the 2D sprite drawer `0x00747248` re-projected per eye (`reprojectSprites`)
  with forced LESSEQUAL depth test; camera-locked tint sheets kept on the eye (`recenterSky`).
- Menus: dark backdrop projection layer behind 2D/menu panels (`menuBackdrop`) so SteamVR doesn't
  show its boundary; menu scanline exception (`hudOverlays`, not confirmed working).
- Left controller: Triangle = chat log window (Y in menus), Square = speech to text placeholder
  (X in menus). Chat log window (`PSUOverlay/Interface/LogDisplay.cpp`) tails `LOG\ChatLog_<date>.txt`.
- Hooks: `DrawIndexedPrimitiveUP` now attached (counters + eye skip only, no HUD capture).

## Debug tools (Debug build only)

- Ctrl+Shift+PageUp/PageDown: hide one render layer `0x02..0x3F` (toast shows it).
- Ctrl+Shift+Home: write every layer's nodes (callback, object, RTTI class) to the log.
- Ctrl+Shift+Insert/Delete: hide one eye-pass draw site at a time (toast shows address + layer).
- Logs: `VREffect:` (screen-space draw kinds), `Layers 5 s:` (sky/blur/glare draw counts),
  `Stereo: model near the camera` / `3D near camera`, `VRBullet:`, `VRAim:`.

## Key findings (also in `docs/ADDRESS_MAP.md`)

- The game's VIEW is identity: world matrices are already camera space; eye views are built
  relative to the game camera. SetTransform wrapper `0x00749D40` (returns at `0x00749D5C`; real
  caller 4 dwords up the stack).
- `PSU_DrawLayer` `0x00560830` stores the layer being drawn at `0x009946C8`.
- Free-aim bullets start at a fixed point (player + ~(0, 10.7, 0) units), not at the muzzle.

## Ideas not started

- Speech to text with Moonshine (ONNX Runtime x86, record while Square held, type into chat as
  local input). Needs a plan first.
