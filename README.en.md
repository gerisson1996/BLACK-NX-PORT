# BLACK NX PORT

🇧🇷 [Português](README.md) · 🇺🇸 **English** · 🇪🇸 [Español](README.es.md)

A port of **Black** (original Xbox, Criterion 2006) to the **Nintendo Switch** as homebrew (`.nro`), made by **static recompilation**. The game's x86 code is translated to C++ on a PC and compiled for ARM64. This is not emulation: the game runs natively, with the Xbox's Direct3D 8 and DirectSound rewritten on top of Vulkan (NVK) and the Switch's audio.

The port supports the **Brazilian Portuguese translation by Tokyo Games RGH**. Its European `default.xbe` is converted to the US one on the console itself.

**Status:** the whole game runs, including menus, videos, sound and levels, at up to 60 fps.

**Download:** get `black.nro` from [Releases](https://github.com/gerisson1996/BLACK-NX-PORT/releases).

**Gyro aiming (v0.2.0):** in *Options → TOUCH CONTROLS → MOTION CONTROLS*, turn **GYROSCOPE** on (**ALWAYS**, or **WHILE FIRING** for ZR held only). It works in handheld mode, with a Pro Controller or with the right Joy-Con. Sensitivity and axis inversion are in the same menu and are saved to `gyro.toml`. While the gyro aims, the game's aim assist is off.

**Rumble (v0.2.0):** works in handheld mode, with a Joy-Con pair, a single Joy-Con and a Pro Controller. The Xbox heavy motor shakes low on the left side, the light one buzzes high on the right.

**Videos and exit (v0.2.4):** FMVs run at 30 fps, including the 640×480 ones (logos, credits and cutscenes) — the game's recompiled WMV decoder is now NEON-vectorized. Closing with the HOME button ends the game cleanly, without a crash. The CPU boost during films is optional (`cpu_boost = true` in `black.toml`).

**iOS (v0.2.4):** the same game runs on iPhone/iPad as an unsigned `.ipa` (`Black-iOS-0.2.4.ipa` in the same release): SDL3 for window, audio and input, and Vulkan through MoltenVK for Direct3D 8. The game goes in the app's Documents folder (`black/game` or a single `.iso`); install with TrollStore or re-sign with Sideloadly/AltStore. Step by step in [IOS-INSTALL.md](IOS-INSTALL.md).


### ☕ Support the project

If you enjoy the port, consider supporting it:

- **Ko-fi:** [ko-fi.com/gerissoncassiano](https://ko-fi.com/gerissoncassiano)
- **PayPal:** [Donate with PayPal](https://www.paypal.com/donate/?hosted_button_id=B3YTKT29F5ZLG)
- **Pix (Brazil):** `pocketneogames@gmail.com`

> [!IMPORTANT]
> This repository **contains no game files** and no code derived from them. You need your own original copy of Black.

---

## How it works

| Folder | What it is |
|---|---|
| `switch/` | The Switch layer (libnx): the applet loop, logs and crash handler (`main.cpp`), the Vulkan/NVK renderer (`vulkan.cpp`), audio (`audio.cpp`), Joy-Con input (`input.cpp`), finding the game on the SD card (`game_files.cpp`), and the CMake build. |
| `XboxXBE-Recomp/` | The recompilation SDK ([TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), commit `99078b1`), with the Switch changes: code-memory guest RAM, SD card file access, threads. |
| `Black-Android/` | The Android port ([RanggaCasper/Black-Android](https://github.com/RanggaCasper/Black-Android) v1.5.0). It provides `src/` (D3D8, DirectSound, game hooks) and `config/game.toml`, plus the `Lock2DSurface` fix for the Switch. |
| `third_party/` | glslang (with HLSL), Dear ImGui, toml++. |
| `tools/` | `recompile.ps1` (recompiles the xbe), `package_switch.ps1`, `europe_to_us.py`. |
| `MIGRACAO.md` | The changes made for the Switch (in Portuguese). |

---

## Building

### Requirements

- **Visual Studio 2022 Build Tools** with "Desktop development with C++" and "C++ Clang tools".
- **devkitPro** with `switch-dev`. In its bash shell, run: `pacman -S switch-cmake switch-libzstd switch-libexpat switch-zlib cmake ninja`.
- **NVK (mesa-switch)**, built and placed in `nvk_sdk/` at the repository root: `include/` and a `lib/` folder with `libvulkan.a` and the `.rlib` files listed in `switch/CMakeLists.txt`. It is not in the repository because `libvulkan.a` is over 350 MB.
- Python 3.
- The repository at a path **without accents or spaces** (e.g. `C:\bnx2`): the recompiler cannot open paths with non-ASCII characters.

### Steps

1. Put the US xbe at `Black-Android/assets/default.xbe`. To convert the European one (the one with the Brazilian Portuguese translation):

   ```bash
   python tools/europe_to_us.py "<your game>/default.xbe" Black-Android/src/common/europe.patch Black-Android/assets/default.xbe
   ```

2. Recompile. This writes `Black-Android/generated/game`:

   ```bash
   powershell -ExecutionPolicy Bypass -File tools\recompile.ps1
   ```

3. Build the `.nro` in devkitPro's bash:

   ```bash
   JOBS=8 ./switch/build.sh
   ```

4. Run `tools\package_switch.ps1`. It copies the `.nro` to `switch_sd/switch/black/`.

---

## Installing on the Switch

```
sdmc:/switch/black/black.nro
sdmc:/switch/black/game/        <- the files from your disc (default.xbe + folders) or an .iso
```

- You need Atmosphère and hbmenu.
- Launch it in **title mode**: hold R while opening any installed game, then choose Black in hbmenu.
- Logs are saved to `sdmc:/switch/black/logs/`.

---

## Credits

- **XboxXBE-Recomp**: TGP482 (MIT)
- **Black-Android**: RanggaCasper (MIT)
- **Brazilian Portuguese translation**: Tokyo Games RGH ([tokyogamesrgh.com.br](https://www.tokyogamesrgh.com.br))
- **Switch port**: gerisson1996
- **Libraries**: Mesa/NVK, glslang, Dear ImGui, toml++, libnx/devkitPro

Black is a trademark of Criterion Games / Electronic Arts. This project is not affiliated with them.
