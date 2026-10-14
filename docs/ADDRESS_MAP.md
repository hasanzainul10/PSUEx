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

Scene↔UI hunt (2026-10-14): Live Ghidra (`PSUC_Uncompressed.exe`) still lacks `createFunction` at `0x0048DA90` / `0x00793C40` — open **PSUClementine_anchors** (or create them) before decompiling. Runtime breadcrumbs: `PSUOverlay/FrameTrace` logs the first 8 frames of `BeginScene` / `SetRenderTarget` / `SetTransform(VIEW|PROJ)` / `UIDraw(0x793C40)` / `EndScene` / `Present` with `_ReturnAddress()` into `psuex.log`.

## Load path

`PSUC.exe` imports system `dinput8.dll` and does **not** reference `PSUEx` by name. `di.dll` in the game folder is Clementine's launcher helper (`DIInit` / `DIUpdateInputState` from `PsuLauncherPC`); it does not load PSUEx.

An existing `config.json` in the game folder shows PSUEx has run before (likely via a manual injector). For reliable loading we ship `tools/dinput8_proxy`:

1. Build `dinput8.dll` (Win32) and place it next to `PSUC.exe`.
2. On process attach it `LoadLibrary`s `PSUExD.dll` / `PSUEx.dll` **only if the host is `PSUC.exe`** (`online.exe` also loads this `dinput8`; injecting there Detours PSUC VAs and AVs on char-list).
3. It forwards `DirectInput8Create` to `%SystemRoot%\SysWOW64\dinput8.dll`.

Debug: `psuex.log` in the game folder; `dinput8_proxy.log` should show `Skip PSUEx` for `online.exe` and `Loaded …` for `PSUC.exe`.

## Functions

| Address | Name | Signature | Used by | Clementine | Notes |
|---|---|---|---|---|---|
| `0x00774AD0` | PSU_WinMain | `int WINAPI (HINSTANCE, HINSTANCE, LPSTR, int)` | `Hook_EntryPoint.hpp` | ghidra-labeled | Live MCP rename + prototype |
| `0x00776590` | PSU_InitializeDirect3D | `char __cdecl (int)` | `Hook_InitializeD3D9.hpp` | verified | Runtime log 2026-10-14: hook hit; `g_pD3D` filled. VR: use `CreateDeviceEx` when Ex is active. D3D9Ex: remap `D3DPOOL_MANAGED` via `D3D9ExCompat` or game crashes (`d3d9.dll+0xF2A23`, read `0xB9197436`) |
| `0x00774F90` | PSU_InitializeDirectInput | `HRESULT __cdecl ()` | `Hook_00774F90.hpp` (disabled) | ghidra-labeled | Phase 3 input hook entry |
| `0x00793C40` | PSU_UIDraw_Candidate | `int __thiscall (void*, int, int, void*, int)` | `Hook_00774F90.hpp` (**hooked** for FrameTrace) | ghidra-labeled | Slot 0/3 of UI vtable at `0x00870948`. **UI draw — not the 3D scene.** Stereo must stop before this. |
| `0x0048DA90` | TCameraTaskPlay_Update | `int __thiscall (CameraTaskPlayData*)` | `Hook_TCameraTaskPlay.hpp` | verified | Slot 0 of camera vtable at `0x008C7764`. Runtime log hits when in play. Writes orbit camera; matrices still come from elsewhere. |
| `0x0048E4C0` | TCameraTaskPlay_Refocus | `int __thiscall (void*)` | `Hook_TCameraTaskPlay.hpp` (disabled) | ghidra-labeled | Later slot of camera vtable `0x008C7764` |
| `0x00479E70` | PSU_MainLoopPump | `void __cdecl (void)` | — | ghidra-labeled | Message/pump loop; repeatedly calls `PSU_FrameRender` |
| `0x00479F50` | PSU_FrameRender | `unsigned char __cdecl (void)` | `Hook_FrameRender.hpp` | verified | **Per-frame orchestrator.** Order: begin → `PSU_BeginOffscreenRT` → **mode scene callback** → `FUN_00772800` → `FUN_0047a080` → `PSU_EndOffscreenToBB`. FrameTrace 2026-10-14. |
| `0x0046F080` | PSU_SceneMode1_Callback | `void __cdecl (void)` (hypothesis) | `Hook_FrameRender.hpp` | verified | Mode-1 entry from table `0x0099325C + mode*0x10`. Runtime: `callback=0046F080` (mode=1, slot `0x0099326C`). Contains UI (`UIDraw` while offscreen). **Town DIP does not return inside this callback.** |
| `0x0074E490` | PSU_DrawIndexed_Wrapper | `void __cdecl (…)` (~2 stack args; sites `add esp,8`) | FrameTrace (observe) | verified | Town: all `DrawIndexedPrimitive` returns were `0x0074E4FD` **outside** SceneCB (`DIP(out)`). Body: `call [ecx+0x148]` (D3D9 DIP vtable slot 82) on `g_pD3DDevice_ba8c40` (`0x00BA8C40`). ≥17 callers in `0x00750xxx`–`0x00763xxx`. Leaf draw helper — not a safe stereo double-call by itself. |
| `0x004FF1B0` | PSU_DrawAgg_Candidate | `int __thiscall (void*, int)` (`retn 4`; push `0xC47`) | `Hook_FrameRender.hpp` | pe-match | 12 E8 sites. Town hits vary by session; one session showed calls from `0x0065157C` only. Not confirmed as the world-pass root. |
| `0x004FD990` | PSU_RenderPassA | `void __cdecl (void)` | `Hook_FrameRender.hpp` | pe-match | Draws render layers `0x02`–`0x29` via `0x00560830`. Called from all 4 scene callbacks (`0x46F0D3`, `0x4663B3`, `0x46AD2C`, `0x474C53`). Town 3D draws are under it (stack chain). Stereo candidate. |
| `0x004FDBC0` | PSU_RenderPassB | `void __cdecl (void)` | `Hook_FrameRender.hpp` | pe-match | Draws layers `0x2B`–`0x3F`. Called right after pass A. Likely 2D/UI. |
| `0x005620A0` | PSU_FlushDeferredLists | `void __cdecl (void)` | `Hook_FrameRender.hpp` | pe-match | Called in pass A between layers `0x0C` and `0x0D`, only if `[0xB9DB88] & 1` (set at pass A entry, cleared here). Draws the lists `[0xB911A4]` and `[0xB911A8]` (`[0xB911A8]` is wrapped by callbacks `[0xB911B0]`/`[0xB911AC]`) via `0x005621F0`, which **empties** them. Zeroes `0xB9DB98`–`0xB9DBA0`. Verified (`vr.debugDoublePass`, town): both lists are empty at pass A entry and refilled during the pass (`listA4 0->1`) on both calls. The second call draws the same 165 DIPs, and animation speed is unchanged. **Pass A is repeat-safe.** |
| `0x005E3350` | PSU_ShadowMapTarget | `int __cdecl (void)` | — | pe-match | Read-only: returns the shadow-map object `[[0xB9E504]+0x88]` when shadows are on. |
| `0x005E3630` / `0x005E37B0` | PSU_ShadowBegin / PSU_ShadowEnd | `void __cdecl (void)` | — | pe-match | Bracket layer 2 at the start of pass A: render the shadow casters into the shadow RT, then composite. Eye-independent, so it could run once per frame. |
| (runtime) | Stereo screen-quad skip | — | `StereoRenderer` + `PSUOverlay` DP/DP-UP/DIP/StretchRect | verified | Pass A includes XYZRHW quads that sample RT textures filled during the **flat** pass (glare/afterimage). Re-running pass A per eye still samples that flat copy → translucent mono ghost over stereo. With `vr.stereoSkipScreenQuads` (default true), eye passes skip XYZRHW draws whose stage-0 texture is an RT **not** written this eye (SetRT / StretchRect dest). Quads that sample RTs rewritten during the eye pass (e.g. shadow composite) still draw. Log lines: `StereoQuad:` / `StereoStretch:`. |
| `0x00560830` | PSU_DrawLayer | custom: layer index in `EDI` | — | pe-match | Walks a layer's node list and calls each node's draw callback (`call [node+0x10]` @ `0x0056086A`). 57 callers. Don't Detour without a naked stub. |
| `0x0056086D` | PSU_DrawLayer callback return | code address | — | runtime (stack scan) | Seen on the stack for pass-B layer-object draws: nameplates, NPC markers (z = projected depth 0.94-0.98); the own nameplate (object fn around `0x0065FEC3`) has z = 0 at screen center. Stack scans are unreliable (stale values); use the exact layer from `Hook_PassB` instead. |
| `0x004FDBC0` | PSU_RenderPassB | `void __cdecl (void)` | `Hook_FrameRender.hpp` | disasm | Exactly `mov edi, layer; call PSU_DrawLayer` for layers `0x2B..0x3F`. `Hook_PassB` runs that loop itself and records the current layer (`VRHud::SetPassBLayer`). Don't attribute draws by scanning the stack for these return addresses: stale copies in live frames give wrong layers. |
| `0x00B9DB38` | g_DrawLayers | `DrawLayer*` (64 × 8 bytes) | `PSUVR/VRPlayerDraw.cpp` | disasm | Entry `+0` = first node, `+6` = layer-disabled byte. Node: `+0x08` next, `+0x0C` object, `+0x10` `cdecl void cb( object )`; skipped when `object+8 & 0x223`. First person swaps the local player's node callback for a thunk to know when its meshes draw. |
| `0x0046ACB0` | PSU_SceneMode5_Callback | `void __cdecl (void)` | `Hook_FrameRender.hpp` | verified | **Town render callback** (mode 5, slot `0x009932AC`). Runtime `PSU_FrameRender gameplay mode=5`. |
| `0x00466360` | PSU_SceneMode3_Callback | `void __cdecl (void)` | `Hook_FrameRender.hpp` | pe-match | Render callback for modes 3 and 9. |
| `0x00474C00` | PSU_SceneMode11_Callback | `void __cdecl (void)` | `Hook_FrameRender.hpp` | pe-match | Render callback for mode 11. |
| `0x0050B580` | PSU_WorldDraw_Candidate | `void __thiscall (void*, int, int)` | — (probe detached) | pe-match | ECX=this; two stack args. Called from mode-1 @ `0x46F215`; not the DIP choke point seen in town. |
| `0x0046F3A0` | PSU_SceneSub_Candidate | `int? __cdecl (void*)` | — (probe detached) | pe-match | Caller `0x46F240` tests EAX (`test eax,eax`). |
| `0x0049E3D0` | PSU_BeginOffscreenRT | `void __cdecl (void)` | — | verified | `SetRenderTarget` to `DAT_00b8ebe8` (OFFSCREEN 640×480), `Clear`. FrameTrace `SetRT … OFFSCREEN ret=0049E42D`. |
| `0x0049E460` | PSU_EndOffscreenToBB | `void __cdecl (void)` | — | verified | Restores BB and composites. FrameTrace `SetRT … BB ret=0049E59C`. |
| `0x0048C960` | PSU_CameraProjectionSetup | `undefined4 __cdecl (void)` | — | ghidra-labeled | Builds projection into `DAT_00b8eb20` then calls `PSU_SetProjectionMatrix`. Init path — **not** the per-frame 3D draw. |
| `0x0074AD80` | PSU_SetProjectionMatrix | `void __cdecl (float* mat)` | — | verified | Copies into `g_ProjectionMatrix` + `SetTransform(PROJ)`. FrameTrace `SetTransform PROJ ret=0074ADAA`. |
| `0x0048FA00` | TCameraTaskPlay_ApplyCamera | `void (task in EAX, new pos in EBX, push target)` | — | disasm | Copies the render camera's position/target into the task mirrors `+0xC8`/`+0xD4`, then writes new values via `0x0048B9A0` (pos → `cam+0xFC`) and `0x0048B9F0` (target → `cam+0x108`), both `cam+0x130 |= 1`. `cam = *( *(task+0x24) + 4 )`. Writing only the task mirrors does nothing (first-person test 2026-10-14). |
| `0x008C7794` | TCameraTaskPlay state handlers | `void (__thiscall*)[]` indexed by `task+0x20` | — | disasm | `0x0048DA90` dispatches on the state byte; slot 1 = `0x0048DB60`. |
| `0x00749D40` | PSU_SetTransform | `void __cdecl (D3DTRANSFORMSTATETYPE, const D3DMATRIX*)` | — | disasm | Thin wrapper over `IDirect3DDevice9::SetTransform` (vtable `+0xB0`). 24 direct callers (byte scan). CamTrace's `ret=00749D5C` is its return. |
| `0x0074E9A0` | PSU_SetWorldMatrix | `void __cdecl (const D3DMATRIX* or null)` | — | disasm | `PSU_SetTransform(0x100 WORLD, m ? m : identity@0x0086D830)`. |
| `0x00759BB0` | PSU_CopyViewMatrix | `void __cdecl (void* src)` | — | ghidra-labeled | Copies into `g_ViewMatrix`. Called from `PSU_InitViewLights` (init). |
| `0x004FC890` | PSU_InitViewLights | `undefined4 __cdecl (void)` | — | ghidra-labeled | Init/lights path; calls `PSU_CopyViewMatrix` + `FUN_0075a070`. |
| `0x00785308` | PSU_BeginScene_Wrapper | `void __cdecl (void)` | — | verified | Thin `BeginScene` on `g_pD3DDevice_a89130`. FrameTrace `BeginScene ret=0078530E`. EndScene `0x00785318`, Present `0x00785328`. |
| `0x00814040` | PSU_FileOpen | see hook | `Hook_00814040.hpp` | ghidra-labeled | Function created in anchors project |

## Globals

| Address | Name | Type | Clementine | Notes |
|---|---|---|---|---|
| `0x00A888C0` | g_pD3D | `IDirect3D9*` | verified | Runtime log 2026-10-14: non-null after `PSU_InitializeDirect3D` |
| `0x00A89130` | g_pD3DDevice_a89130 | `IDirect3DDevice9*` | ghidra-labeled | Device used by Begin/End/Present wrappers at `0x00785308`+ |
| `0x00BA8C40` | g_pD3DDevice_ba8c40 | `IDirect3DDevice9*` | ghidra-labeled | Device used by matrix/`SetRenderState` helpers (`PSU_SetProjectionMatrix`, etc.) |
| `0x00A85C30` | g_ViewMatrix | `Matrix4x4` | ghidra-labeled | **Not the per-frame camera.** Raw byte scan (2026-10-14): only 5 refs, all in `PSU_CopyViewMatrix` (init, from `PSU_InitViewLights`) and the light loop `0x0075A070` (lights → view space, `SetLight`). The orbit camera is folded into each WORLD by the callers of `0x0074E9A0`, so first person steers `TCameraTaskPlay` instead of using this matrix. |
| `0x009CA8AC` | g_pLocalPlayer (via `PSUPlayer`, `ReadMemory(0x5CA8AC)` is base-relative) | `PSUPlayer*` | code-used | `+0x114` position (`Vector3F`), `+0x124` angle. Zero during area / universe change. |
| `0x00A85B30` | g_ProjectionMatrix | `Matrix4x4` | ghidra-labeled | Stereo per-eye override target |
| `0x00B90EA8` | g_SceneModeIndex | `uint32` | verified | Index into scene callback table (`*0x10` stride) |
| `0x00B90ED0` | g_SceneCallbackSkip | `uint32` | verified | Non-zero skips the mode callback in `PSU_FrameRender` |
| `0x0099325C` | g_SceneCallbackTable | `void*[]` (stride 0x10) | verified | Slot = base + mode*0x10 holds the per-mode render callback pointer |
| `0x00B8EBE8` | g_OffscreenRT | `IDirect3DSurface9*` | verified | Bound by `PSU_BeginOffscreenRT` |
| `0x00A8E0EC` | Channel send function slot | function pointer | pe-match | Translation hook. VR must not touch it |

## Vtables (scene / UI hunt)

| Address | Role | Notes |
|---|---|---|
| `0x008C7764` | `TCameraTaskPlay` vtable | `[0]=0x0048DA90` Update, later slot `0x0048E4C0` Refocus |
| `0x00870948` | UI draw object vtable | `[0]=0x00793C40`, also `0x00791F60`, `0x00793C70`, `0x00793C60` |

## Structs

- `CameraTaskPlayData`: defined in `PSUDetour/Hook_TCameraTaskPlay.hpp`. `m_position`/`m_focusPosition` sit after `padding_9C`, and `m_playerDataPtr` leads to the player data (Phase 2 head position).

## Stereo gate (do not implement StereoRenderer yet)

| Question | Answer (2026-10-14) |
|---|---|
| Per-frame **orchestrator**? | **`PSU_FrameRender` `0x00479F50`** (verified). |
| Mode-1 body? | **`PSU_SceneMode1_Callback` `0x0046F080`** — `SceneCB ENTER/LEAVE` runtime-verified (title/lobby). |
| Where is UI? | **Inside `0x0046F080`.** Log: `UIDraw … IN_SceneCB` and `FIRST UIDraw ret=0079932C`. |
| Where is 3D DIP (town)? | Town runs **mode 5, callback `0x0046ACB0`**, so the draws were outside the *mode-1* hook. Leaf wrapper **`0x0074E490`**. Next run hooks `0x0046ACB0` to check whether town DIP happens inside it, before or after UI. |
| Exact scene-only function? | **DIP leaf named** (`0x0074E490`). Higher-level “draw whole world once” entry still TBD (walk callers `0x007502B0`…`0x00763857`). **Do not** start `StereoRenderer` on the leaf alone. |
| Safe to call twice per tick? | **`PSU_FrameRender` — NO.** **`0x0046F080` — NO** (UI). **`0x00793C40` — NO.** **`0x0074E490` — NO** (per-mesh leaf). Need a world-pass root above the `0x750xxx` callers. |
| Virtual screen | Mono quad stays live. |

### Verified order (title/lobby)

`PSU_BeginOffscreenRT` → **`0x0046F080` ENTER** → (no DIP) → **FIRST `UIDraw 0x793C40` ret=`0x0079932C`** → more UI → **LEAVE** → `PSU_EndOffscreenToBB` → Present.

### Town note

Indexed world draws go through **`0x0074E490`** while **not** inside the mode-1 callback.

**Town is scene mode 5, not mode 1** (runtime 2026-10-14: `PSU_FrameRender gameplay mode=5 callback=0046ACB0 slot=009932AC`). The mode-1 hook never saw town draws because the town frame never enters `0x0046F080`.

Render callback table `0x0099325C` (stride `0x10`, slot 0 = render callback, called as `call eax` with no args):

| Mode | Slot 0 (render) | Notes |
|---|---|---|
| 0, 4 | — | no render callback |
| 1, 2, 6, 7, 8, 10 | `0x0046F080` | title/lobby (mode 1 verified) |
| 3, 9 | `0x00466360` | |
| 5 | `0x0046ACB0` | **town** (verified) |
| 11 | `0x00474C00` | |

Live callers of `0x0074E490` in town (runtime return addresses): `0x007502B5` (fn `0x007500F0`, seen before play camera), `0x00761312` (fn `0x00761180`), `0x00750862` (fn `0x00750650`), `0x0075263A` (fn `0x007524E0`). `0x004FF1B0` did fire in this session from `0x0065157C` (`this=0F53E5A0`, `flag=0xC47`), so the earlier "zero hits" was session-dependent.

Duplicate load: `online.exe` injects its own (Release, launcher-updated) `PSUEx.dll` into PSUC, separate from our `dinput8` proxy loading `PSUExD.dll`. `dllmain.cpp` skips hooking if another PSUEx module is already present. Otherwise it Detours `LoadLibrary{A,W,ExA,ExW}` and returns its own handle for any later `PSUEx.dll` / `PSUExD.dll` load.

### Render passes (stack chains, 2026-10-14)

Town (mode 5) inside `0x0046ACB0`: ~175 DIP before the first `UIDraw` (`ret=007992F8`), then 3 UI draws. Verified call chain:

```
PSU_FrameRender 0x00479F50  --call eax-->  scene callback (0x0046ACB0 town / 0x00466360 title)
  if( byte[0x00993328] ) { 0x004FD990(); 0x004FDBC0(); }        (same pattern in all 4 callbacks)
  0x004FD990  PSU_RenderPassA  -> 0x00560830(layer in EDI) for layers 0x02..0x29 (many gated by bytes 0x009D1088+)
  0x004FDBC0  PSU_RenderPassB  -> 0x00560830 for layers 0x2B..0x3F
    0x00560830  PSU_DrawLayer  -> per-object virtual call [obj+0x10] @ 0x00560868
      town objects -> 0x00526B00 / 0x005E4040 / 0x00552800 -> 0x004FF076 -> 0x004FEC10 -> mesh wrappers -> 0x0074E490
```

Runtime (town, pass hooks, 8 frames each):

| Pass | DIP | DrawPrimitiveUP | UIDraw | SetTransform | VS consts |
|---|---|---|---|---|---|
| A `0x004FD990` | 165 | 4 | 0 | WORLD ×84, no VIEW/PROJ | 0 |
| B `0x004FDBC0` | 0 | 66 | 0 | PROJ ×2 (ret `0x0074ADAA`) | 0 |

Pass A is the fixed-function 3D world with no UI. Camera VIEW/PROJ is set before pass A, not inside it. Pass B is 2D (UP quads with its own projection). The 3 `0x793C40` UI draws happen after both passes.

Camera (CamTrace, town, 3 frames): the game never calls `SetTransform(VIEW)`, so VIEW stays identity. The camera is baked into every WORLD matrix (`0x00749D5C` sets WORLD = model×view; first sample has pitch ≈20.5°, X mirrored, translation `(0, -14.0, -5.3)`). PROJ is set once per callback at `0x0074ADAA` before pass A: right-handed, infinite far, near = 1, `cot(fovY/2)` = 2.415 (≈45°), 4:3. Pass B sets an off-center PROJ (`_31=-0.65`, `_32=-0.58`) and then restores it. No vertex-shader constants are uploaded, so it's fixed-function.

Stereo implication: at `0x004FD990` entry, set VIEW = per-eye offset (eye-space translation) and PROJ = per-eye frustum. Restore VIEW = identity and the game PROJ on exit.

`0x004FD990` / `0x004FDBC0` are `void __cdecl (void)`. `0x00560830` uses a custom convention (layer index in `EDI`), so hook its callers, not it. Hypothesis to confirm: pass A = 3D world, pass B = 2D/UI. If pass A has no UI draws and is safe to repeat, it is the stereo double-call point.

PE note: the code section `0x00401000`–`0x0086B000` is **not** flagged executable in its header (only the 8 KB `.mackt` stub is). Don't filter sections on `IMAGE_SCN_MEM_EXECUTE` when bounding game code.

### Static call graph above DIP leaf (PE, no further playtest)

```
0x0074E490  PSU_DrawIndexed_Wrapper   (leaf; ret 0x0074E4FD)
    ^ 17 mesh wrappers (0x007500F0 … 0x007634E0)
    ^ per-wrapper parents (0x007504E0, 0x00751610, …)
    ^ aggregators seen in PE:
         0x004FEC10  (thiscall-ish; 2 E8 sites)
         0x005625F0 → 0x005628E0 / 0x00562980  (draw helpers)
         0x004FF1B0  (__thiscall; 12 E8 sites — best observation candidate)
         vtable 0x008C8120 → 0x0049ABC0 / 0x0049A760 / 0x0049AA50
              (RTTI name after vtable: TAstarteCollisionDisplayer — one mesh class, not whole world)
```

`PSU_FrameRender` / `0x0046F080` do **not** `E8` directly to the DIP leaf or `0x004FF1B0`. World draw is reached via other tasks / virtual calls. Next: confirm `0x004FF1B0` at runtime (first-hit log only), then find its per-frame root.

## To find (VR roadmap)

| Target | Phase | Notes |
|---|---|---|
| Per-frame scene render entry point | 1 | Orchestrator + mode-1 found. **DIP leaf `0x0074E490` verified in town.** World-pass root above its callers still open |
| Offscreen RT → BB transition | 1 | **Found:** `PSU_BeginOffscreenRT` / `PSU_EndOffscreenToBB`; surface `g_OffscreenRT` |
| Start of the UI/HUD pass | 1 | **`0x00793C40` verified inside `0x0046F080`**; first ret `0x0079932C` |
| Player world position and head bone | 2 | Through `m_playerDataPtr` |
| Local player head and hair mesh draws | 2 | To hide them in first person |
| Movement input read (left stick) | 2 | Rotate it by HMD yaw |
| DirectInput device objects and `GetDeviceState` | 3 | Synthetic gamepad |
| Hand and weapon bone transforms | 3 | Weapon follows the controller |
