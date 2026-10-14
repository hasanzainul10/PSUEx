# PSU Client Address Map

Known addresses in the PSU Illuminus 14.30 client used by PSUEx. All are absolute (32-bit, fixed base).

## Local install

| Item | Path |
|---|---|
| Game folder | `D:\Games\PSUClementine_22072025` |
| Client exe | `D:\Games\PSUClementine_22072025\PSUC.exe` |
| RE binary | `D:\Games\PSUClementine_22072025\PSUC_Uncompressed.exe` (copy of `PSUC.exe`; this Clementine build is **not** UPX-packed) |
| Toolchain root | `D:\PSUExTools` (kept off the C: drive) |
| Ghidra project (GUI / MCP) | `D:\PSUExTools\ghidra-projects\PSUClementine` |
| Ghidra project (anchors fully created) | `D:\PSUExTools\ghidra-projects\PSUClementine_anchors` |
| Deploy Debug DLL as | `PSUEx.dll` (loader also tries `PSUExD.dll` first) |
| Loader | `dinput8.dll` proxy from `tools/dinput8_proxy` (see Load path) |

Clementine status:
- `unverified`: taken from existing PSUEx code (14.30); not yet checked against Clementine's `PSUC.exe`.
- `pe-match`: bytes at this VA look like a real function prologue in this Clementine build (static PE check).
- `ghidra-labeled`: renamed / prototyped / commented in Ghidra (live MCP and/or `PSUClementine_anchors`).
- `verified`: confirmed on Clementine by a runtime log or breakpoint.
- `differs`: Clementine uses a different address (put it in Notes).

Static PE check (2026-10-14): ImageBase `0x400000`, x86, **not UPX-packed**. All listed function VAs have plausible prologues. Globals are zeros on disk (runtime data).

Ghidra labeling (2026-10-14): Live MCP renamed `PSU_WinMain`, `PSU_InitializeDirect3D`, `PSU_InitializeDirectInput`, globals `g_pD3D` / `g_ViewMatrix` / `g_ProjectionMatrix`. Project copy `PSUClementine_anchors` created real functions for `TCameraTaskPlay_Update`, `TCameraTaskPlay_Refocus`, `PSU_UIDraw_Candidate`, `PSU_FileOpen`. Prefer opening **PSUClementine_anchors** in Ghidra for those four.

## Load path

`PSUC.exe` imports system `dinput8.dll` and does **not** reference `PSUEx` by name. `di.dll` in the game folder is Clementine's launcher helper (`DIInit` / `DIUpdateInputState` from `PsuLauncherPC`); it does not load PSUEx.

An existing `config.json` in the game folder shows PSUEx has run before (likely via a manual injector). For reliable loading we ship `tools/dinput8_proxy`:

1. Build `dinput8.dll` (Win32) and place it next to `PSUC.exe`.
2. On process attach it `LoadLibrary`s `PSUExD.dll` if present, else `PSUEx.dll`.
3. It forwards `DirectInput8Create` to `%SystemRoot%\SysWOW64\dinput8.dll`.

Debug builds also append to `psuex.log` in the game folder.

## Functions

| Address | Name | Signature | Used by | Clementine | Notes |
|---|---|---|---|---|---|
| `0x00774AD0` | PSU_WinMain | `int WINAPI (HINSTANCE, HINSTANCE, LPSTR, int)` | `Hook_EntryPoint.hpp` | ghidra-labeled | Live MCP rename + prototype |
| `0x00776590` | PSU_InitializeDirect3D | `char __cdecl (int)` | `Hook_InitializeD3D9.hpp` | verified | Runtime log 2026-10-14: hook hit; `g_pD3D` filled. VR: use `CreateDeviceEx` when Ex is active. D3D9Ex: remap `D3DPOOL_MANAGED` via `D3D9ExCompat` or game crashes (`d3d9.dll+0xF2A23`, read `0xB9197436`) |
| `0x00774F90` | PSU_InitializeDirectInput | `HRESULT __cdecl ()` | `Hook_00774F90.hpp` (disabled) | ghidra-labeled | Phase 3 input hook entry |
| `0x00793C40` | PSU_UIDraw_Candidate | `int __thiscall (void*, int, int, void*, int)` | `Hook_00774F90.hpp` (disabled) | ghidra-labeled | Function created in anchors project |
| `0x0048DA90` | TCameraTaskPlay_Update | `int __thiscall (CameraTaskPlayData*)` | `Hook_TCameraTaskPlay.hpp` | ghidra-labeled | Function created in anchors project |
| `0x0048E4C0` | TCameraTaskPlay_Refocus | `int __thiscall (void*)` | `Hook_TCameraTaskPlay.hpp` (disabled) | ghidra-labeled | Function created in anchors project |
| `0x00814040` | PSU_FileOpen | see hook | `Hook_00814040.hpp` | ghidra-labeled | Function created in anchors project |

## Globals

| Address | Name | Type | Clementine | Notes |
|---|---|---|---|---|
| `0x00A888C0` | g_pD3D | `IDirect3D9*` | verified | Runtime log 2026-10-14: non-null after `PSU_InitializeDirect3D` |
| `0x00A85C30` | g_ViewMatrix | `Matrix4x4` | ghidra-labeled | Stereo per-eye override target |
| `0x00A85B30` | g_ProjectionMatrix | `Matrix4x4` | ghidra-labeled | Stereo per-eye override target |
| `0x00A8E0EC` | Channel send function slot | function pointer | pe-match | Translation hook. VR must not touch it |

## Structs

- `CameraTaskPlayData`: defined in `PSUDetour/Hook_TCameraTaskPlay.hpp`. `m_position`/`m_focusPosition` sit after `padding_9C`, and `m_playerDataPtr` leads to the player data (Phase 2 head position).

## To find (VR roadmap)

| Target | Phase | Notes |
|---|---|---|
| Per-frame scene render entry point | 1 | Call between the camera update and the UI pass. Needed for stereo |
| Start of the UI/HUD pass | 1 | For drawing the HUD into its own target |
| Player world position and head bone | 2 | Through `m_playerDataPtr` |
| Local player head and hair mesh draws | 2 | To hide them in first person |
| Movement input read (left stick) | 2 | Rotate it by HMD yaw |
| DirectInput device objects and `GetDeviceState` | 3 | Synthetic gamepad |
| Hand and weapon bone transforms | 3 | Weapon follows the controller |
