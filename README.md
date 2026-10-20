# PSUEx VR

**Play Phantasy Star Universe (Clementine) in a PC VR headset.** Stereo 3D in the game world, first or third person, motion controllers or a gamepad, and the HUD and menus on a panel in front of you.

PSUEx VR is a fork of [PSUEx](https://github.com/HikikoMarmy/PSUEx), a DLL extension for **Phantasy Star Universe Ver. 14.30**. Everything PSUEx does still works; the VR mode is added on top and is off unless you turn it on.

> **Preview.** Tested on PS VR2 through SteamVR. Expect rough edges, and please report issues.

**[Download the latest release](https://github.com/hasanzainul10/PSUEx-VR/releases)** · **[VR player guide](docs/VR_GUIDE.md)** · **[Install reference](docs/INSTALL.md)**

---

## Quick start

1. Download `PSUEx-VR-v….zip` from [Releases](https://github.com/hasanzainul10/PSUEx-VR/releases).
2. Extract everything into your Clementine game folder (next to `PSUC.exe` and `online.exe`).
3. Start SteamVR (PS VR2: start the **PlayStation VR2 App** first).
4. Start the game with `online.exe`.
5. The first time you're in the game world, the **VR Setup** wizard opens in the headset: recenter, eye height, hand calibration and a controls tour.

VR settings: `PSUExVRConfig.exe` (with the game closed), or in game **both grips + left Square** (Ctrl+F10 on the keyboard). The [VR player guide](docs/VR_GUIDE.md) has every control and setting.

---

## VR features

- **Stereo rendering** in the game world through OpenXR, with both eyes drawn in the same game tick. Login, menus and loading screens show on a big floating screen.
- **First and third person.** In first person you're in your character's head, and walking in your room walks the character.
- **Motion controllers** work as a virtual gamepad. Combat follows the weapon type: swing or pull the trigger to attack, grip + swing for a Photon Art, free aim with the gun hand and a reticle on the controller line. The combo buzz comes at the Just Attack timing.
- **First person hands:** your character's hands follow the controllers, with a bundled PS VR2 Sense calibration.
- **In-headset UI:** a controller laser pointer for the PSUEx windows, label cards at the controllers that show what each button does right now, and the setup wizard.
- **HUD and menus** on a panel that follows your view, with the mission map terrain fitted into its frame.
- **Speech to text** (optional): hold a button, talk, and the text goes into the game chat. It runs offline on your PC (Moonshine).
- **Gamepad mode** for playing without motion controllers.

## Other PSUEx features

- Extended resolution support, with FOV correction.
- Floor Reader for extended item loot information.
- Camera controls for distance, height and rotation speed.
- Japanese translation of the game's text (see below).
- Engine hooks and a number of bug fixes.

---

## Building

- Visual Studio 2022 Build Tools (MSVC v143) and the DirectX SDK (June 2010, through `$(DXSDK_DIR)`).
- Open `PSUEx.sln` and build **Win32** (`x86`). Only 32-bit builds load into the game.
  ```powershell
  msbuild PSUEx.sln /p:Configuration=Debug /p:Platform=x86 /m /v:minimal
  ```
- Debug builds `PSUExD.dll` and opens a console for logging. Release builds the VR DLL and `PSUExVRConfig.exe`.
- `tools/package-release.ps1 -Version x.y.z` builds Release and packages the release zip into `dist/`.

Developer notes: [docs/VR_ROADMAP.md](docs/VR_ROADMAP.md) (architecture), [docs/ADDRESS_MAP.md](docs/ADDRESS_MAP.md) (game addresses).

## Dependencies

- [Microsoft Detours](https://github.com/microsoft/Detours)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [nlohmann/json](https://github.com/nlohmann/json)
- [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) (loader)
- [Moonshine](https://github.com/moonshine-ai/moonshine) and [ONNX Runtime](https://github.com/microsoft/onnxruntime) (speech to text)
- DirectX 9 SDK

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the licenses.

---

## Japanese translation

PSUEx can optionally show the game's text in Japanese. 
Text the game shows, including item names, is shown in Japanese, and shop searches can be typed in Japanese.

It's off unless a `translation.txt` is in the game folder. Without it, PSUEx
works exactly as before.

### Setup

1. Put this fork's `PSUEx.dll` in the game folder.
2. Put your `translation.txt` in the game folder, next to `PSUEx.dll`.
3. Start the game.

`translation.log` in the game folder shows how many lines were read, and any
lines that couldn't be read.

The file is read again when you save it, so you can edit it while the game is
running.

### Shop search

Type an item's name in Japanese in the shop search, whole or just the start
(モノメ for モノメイト). It's sent to the server in English, using the
translation file, so you get the usual list of shops. Names typed in English
are searched as usual.

### The translation file

`translation.txt` is a UTF-8 text file with one line per translation:

```text
# Lines starting with # are comments.
Monomate => モノメイト
Lobby menu => ロビーメニュー
```

- The English must match the game's text exactly, including case and spaces.
- `{}` matches any text, such as a number or a name, and puts it in the
  translation in the same place:
  ```text
  {} has\ninvited you to join a party. => {}から\nパーティーに招待されました。
  [B] {} => 基板/{}
  ```
  Text matched by `{}` is translated too, if it has its own line (`[B] Monomate`
  shows as `基板/モノメイト`).
- `{1}`, `{2}`, ... in the translation pick a `{}` by number, for a different
  word order:
  ```text
  Partner card : {}/{} => {2}枚中{1}枚
  ```
- `\n` is a line break, `\t` a tab, and `\u{f805}` a character by its code
  (for the game's color and formatting codes). Write `\\`, `\{` and `\}` for a
  `\`, `{` or `}`.
- Exact lines win over `{}` lines. `{}` lines are tried from the top of the
  file down, so put the more specific ones first.

---

## License

GPLv3, like upstream PSUEx. Source for each release is its `v…-vr` tag in this repository.
