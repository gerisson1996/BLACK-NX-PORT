<p align="center">
  <img src="https://raw.githubusercontent.com/RanggaCasper/Black-Android/main/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" width="96" alt="Black">
</p>

<h1 align="center">Black for Android {{VERSION}}</h1>

<p align="center">
  An unofficial native Android port of the original Xbox version of <b>Black</b>,<br>
  statically recompiled with <a href="https://github.com/TGP482/XboxXBE-Recomp">XboxXBE-Recomp</a> and drawn with Vulkan.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Android-10%2B-3DDC84?logo=android&logoColor=white" alt="Android 10+">
  <img src="https://img.shields.io/badge/arch-arm64--v8a-blue" alt="arm64-v8a">
  <img src="https://img.shields.io/badge/Vulkan-1.1-AC162C?logo=vulkan&logoColor=white" alt="Vulkan 1.1">
  <img src="https://img.shields.io/badge/SDL-3-1D4F91" alt="SDL3">
</p>

> [!IMPORTANT]
> This release contains **no game assets**. You need the files from your own legally acquired copy of *Black* (USA or Europe release).

## 📦 Download

| File | Size | SHA-256 |
| --- | --- | --- |
| [`{{APK}}`](https://github.com/RanggaCasper/Black-Android/releases/download/{{TAG}}/{{APK}}) | {{SIZE}} | `{{SHA256}}` |

## 🚀 Installation

1. Install **`{{APK}}`** on your phone.
2. Open **Black Android** and allow **all files access** when it asks.
3. Copy the game into **`/storage/emulated/0/Black`**, or into a `Black` folder on your SD card: the disc image (`.iso`), or `default.xbe` with its data folders.
4. The game starts as soon as it finds the files.

> [!TIP]
> If you don't grant all files access, put the game in `Android/data/com.racas.black/files/assets` instead. Settings, saves, logs and the shader cache are stored in `Android/data/com.racas.black/files`.

## ✨ Features

| | |
| --- | --- |
| 🖥️ **Graphics** | Render resolution 20–200%, FOV 45–140°, viewmodel FOV, FMV scale, unlocked frame rate, V-Sync, FPS counter, anisotropic filtering, 2x / 4x / 8x draw distance, toggleable reload blur |
| 👆 **Touch Controls** | Presets and a layout editor to move and resize each control, touch sensitivity, gyroscope aiming with the phone or a DualShock 4 / DualSense |
| 🎮 **Other Input** | SDL3 controllers with rumble, keyboard and a captured mouse with rebindable controls |
| ⚙️ **General** | Language (with Indonesian), skip intros, audio output, update check, pause while in the background |
| 💀 **Cheats** | Infinite ammo and grenades, rapid fire, no recoil, god mode, one-hit kills, game speed |

The settings are in the game's own options menu, one page per group. Android's back button opens the pause menu.

## 📱 Requirements

- **Android** 10 or newer, arm64
- **GPU** with a Vulkan 1.1 driver (Adreno 6xx+, Mali-G7x+, Xclipse)
- **4 GB** RAM, **2 GB** free storage

## 🙏 Credits

Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by **TGP482**, with [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus) and [Dear ImGui](https://github.com/ocornut/imgui).

---
