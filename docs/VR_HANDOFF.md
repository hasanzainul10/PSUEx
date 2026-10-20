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

1. **Game folder state (2026-10-20).** Loader `dinput8.dll` in place; the Debug build with the JA combo buzz is deployed (it includes the map fix).
   VR is back on in both settings files (`enabled` true,
   `d3d9exOnly` false; `.bak7` = the earlier d3d9exOnly test state). No test state left behind.
2. **Mission map: terrain fixed (user-confirmed 2026-10-20).** The D3D9Ex upgrade was cleared
   (VR off and `d3d9exOnly` both matched vanilla). Cause: the terrain is a `DrawIndexedPrimitive`
   from `0x0074E4FD` (layer 0x31, FVF XYZ|NORMAL, ~837 triangles), and pass B only captured DP-UPs
   and screen-space DIPs for the HUD panel. `VRHud::CaptureMapMeshDraw` now replays transformed
   DP / DIP / DIP-UP draws in layers 0x31 / 0x32 onto the user panel (`hudMapMeshes`, default on,
   checkbox "Mission map terrain on the panel"). Debug logs: `tools/lastlog.sh 'VRMap'`.
   **Map fit (user-confirmed 2026-10-20):** the game draws the minimap frame at 640 x 480 pixel size
   (layer 0x30, 128 x 128 at (2240,108)-(2368,236) on 2560 x 1440) but sizes the 3D map for the
   whole screen (off-centre perspective, `_31` -0.65 / `_32` -0.583 -> centre (2112,300)), so only
   a sliver showed in the frame (vanilla too). On the panel replay only, `VRHud::MapFitProjection`
   rescales PROJ by 640/W x 480/H and moves the map centre onto the frame centre (`hudMapFit`,
   default on; zoom `hudMapFitScale`). With the fit on, transformed map-layer DP-UPs skip the
   depth routing (eyes / skip-own) and go to the panel. The flat frame is unchanged.
   Not done: the red 2D arrow right of the frame (layer 0x33, DP-UP `0x0074722D`, ~(2437,274)) is
   still where the game puts it; ask the user whether it matters.
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

5. **Combo buzz at Just Attack timing: done (user-confirmed 2026-10-20).** The buzz fires once per
   hit when the current motion's input window opens, read from the weapon type's motion table
   (`0x005366E0`: `[0x00B9DAA4]` by active class `+0x1DD` and motion `+0x170`, entry `+0x08` / `+0x0C`
   in animation frames, `0x005532F0`). Pressing on the buzz gives JUST ATTACK on sword, knuckles,
   spear, twin daggers, saber and dagger. Code: `PSUCombat::ReadMotion` / `JustAttackWindowOpen`,
   `ComboFeedback` (fallback: the old `+0x710 == 0x0E` edge when the table can't be read). `+0x710`
   is the allowed-input mask (`0x0053DEB0`), not a combo state. Debug log: `tools/lastlog.sh VRCombat`.
   Not done: `comboBuffer` still waits on `Swinging` (`+0x710 == 0x02`); it could use the JA window too.
   Ghidra: the full analysis came from a headless run (`tools/ghidra/PrepareFullAnalysis.java`, 19,940
   functions; backup `PSUClementine.rep.bak-2026-10-20`).
   Address scan scripts (pefile / capstone, run with `uv run --with capstone --with pefile`): `tools/findref.py` (dword refs),
   `tools/thiscalls.py` (`lea ecx,[reg+DISP]; call` targets), `tools/dispscan.py` (`[reg+DISP]` uses, `w` = writes only).

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
- Ctrl+Shift+End: save the flat frame and the HUD panel as `psuex_dump_frame.png` / `psuex_dump_hud.png` in the game folder (the last run's press didn't save; check the `Dump:` log line).
- `VRMap new:` / `VRMap 5 s:` / `VRMap geo:` / `VRMap fit:`: mission map draws, screen boxes and the fit.
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
