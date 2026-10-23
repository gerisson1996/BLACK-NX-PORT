# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An unofficial Android (arm64) port of the original Xbox game *Black*, made with the [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) SDK. The game's x86 code in `default.xbe` is statically recompiled to C++ on a Windows PC. That C++ is then built for Android together with the SDK's portable runtime and this repo's host code. Drawing goes through Vulkan 1.1, and windowing, input and audio through SDL3. The repo ships no game assets. `assets/`, `iso/` and `generated/` hold the user's own files or files derived from them, and all three are gitignored.

## Build

Everything runs on Windows. Requirements: Visual Studio 2022 C++ tools, plus Android SDK Platform 34, NDK `28.2.13676358` and CMake `3.31.6`. Java 17–21 is needed for Gradle 8.7 (`.vscode/settings.json` points to JDK 17). Python is used only to pull the xbe out of an ISO.

- `build.bat` runs the full pipeline:
  1. Checks the tools.
  2. Gets `assets/default.xbe`. If it is missing, `python -I tools/xdvdfs.py <iso> default.xbe assets/default.xbe` extracts it from `assets/*.iso` or `iso/*.iso`.
  3. Builds the recompiler (`tools/CMakeLists.txt` → `out/build/android-tools/xbe_recomp.exe` and `xbe_symbols.exe`) with clang-cl.
  4. Writes the `[symbols]` table into `config/game.toml` with `xbe_symbols`, only if the file doesn't have one yet.
  5. Runs `xbe_recomp config/game.toml --xdk <sdk>/xdk` → `generated/game/`.
  6. Runs `gradlew.bat assembleRelease`.
- To rebuild only the APK after editing host code (no re-recompile): `gradlew.bat assembleRelease`. Output: `app/build/outputs/apk/release/app-release.apk`, signed with the debug key.
- After editing `config/game.toml` (hooks, symbols, ABI, partitions), the recompile step has to run again. Run `build.bat`, or run the two `out/build/android-tools` commands by hand.
- `gradlew.bat assembleDebug` still builds the native code as Release: unoptimized recompiled code is too slow to run.
- Install: `adb install -r app/build/outputs/apk/release/app-release.apk`. On the device, settings (`black.toml`, `android.toml`, `layout.toml`), saves (`hdd`), logs and the shader cache are in `Android/data/com.racas.black/files`.
- The first native build compiles 64 partitions of recompiled code and takes 30–90 minutes.
- `-PxbeGenerated=<dir> -PxbeImage=<file>` makes Gradle build recompiled code from somewhere other than `generated/game`.

There are no tests or linters in this repo.

## CI/CD

`.github/workflows/build.yml` runs only when a `v*` tag is pushed (`git tag v1.2.3 && git push origin v1.2.3`). It runs `build.bat` on a Windows runner and publishes a GitHub Release with `Black-<version>.apk`. The release body comes from `ci/release-notes.md`, with its `{{...}}` placeholders filled in. The xbe is committed only in encrypted form, as `ci/default.xbe.enc` (`openssl enc -aes-256-cbc -pbkdf2 -iter 200000`). The workflow decrypts it with the `XBE_KEY` secret. `KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS` and `KEY_PASSWORD` sign the release. `app/build.gradle` reads them through the `BLACK_KEYSTORE*` environment variables and falls back to the debug key when they're absent. The version comes from the tag (`-PversionName`) and the run number (`-PversionCode`), and is passed to Gradle through `GRADLE_ARGS` in `build.bat`. If `assets/default.xbe` changes, re-encrypt `ci/default.xbe.enc` with the same key.

## Where the SDK comes from

Both `CMakeLists.txt` files look for the SDK in this order: `sdk/` in the repo, then `../XboxXBE-Recomp` beside the repo. If neither exists, they FetchContent the SDK's `main` branch. A sibling checkout lets you read or change the runtime (`<sdk>/runtime/{app,core,kernel,xdk,gpu,audio}`), whose headers (`app/app.h`, `kernel/kernel.h`, `xdk/hle.h`, `core/input.h`, ...) the host code includes. Gradle's `fetchSDL` task shallow-clones SDL `release-3.4.16` into `third_party/SDL`. Its Java sources go into the APK and its C side becomes `libSDL3.so`. glslang, toml++ and imgui are fetched by CMake.

## Architecture

The root `CMakeLists.txt` builds one shared library, `libmain.so`, which SDL's activity loads. It is made from:

- **`generated/game/`**: recompiler output (`x86_recomp.N.cpp` for N < `partitions`, `x86_func_mapping.cpp`, `image_symbols.h`). Compiled with `-O2 -w`. Never edit it by hand. Change `config/game.toml` or the host code and recompile instead.
- **The SDK runtime** (`<sdk>/runtime/*/*.cpp`), without the Windows-only parts and the parts this repo replaces. The regex filter in `CMakeLists.txt` drops `app/`, `core/input.cpp`, `gpu/d3d12`, `gpu/render_thread`, `audio/xaudio2`, `xdk/dsound` and `xdk/d3d8`.
- **`src/`**, the game-specific host, organized as `src/<area>/*.cpp` and globbed by CMake:
  - `src/game/`: game hooks and patches (FOV, draw distance, intro skip, menus, HUD, mouse, prompts).
  - `src/d3d/`: the title's Direct3D 8 (XDK 5849) entry points. `render_thread.cpp` records D3D calls on the game's render thread and replays them on a dedicated Vulkan thread. Vertex, texture and surface memory is read at draw time, the way the console GPU reads it, so XDK fences, busy checks and locks wait on that replay.
  - `src/sound/`: DirectSound reimplemented (buffers, streams, ADPCM decode, mixer, reverb).
  - `src/common/`: `game_info.cpp` defines `app::kGame`, the struct that describes the game to the runtime (title, settings file, option bindings, load/save/init, pad and rumble filters, UI callback). Also here: the in-game options menu (`menu.cpp`), `options.h` (`game::Options`, saved to `black.toml`), translations, and `europe.patch` (see below).
- **`native/`**, the Android host. `main.cpp` runs SDL's window and event loop, the app lifecycle and foreground/surface handling. `vulkan.cpp` is the Vulkan backend, and `vk_shader.cpp` compiles the SDK's HLSL shader translations to SPIR-V with glslang. The rest: `touch.cpp` (touch controls and layout editor, plus keyboard and mouse), `gyro.cpp`, `input.cpp` (controllers), `audio.cpp`, `ui.cpp` (ImGui), and `game_files.cpp`, which finds the game files or ISO and checks the xbe's SHA1 against `kImageSha1`. `host.h` declares the interfaces these parts share. `native/compat/crt.h` is force-included into all host sources (not the generated ones).
- **`app/`**: Gradle module. `BlackActivity.java` extends `SDLActivity`. It asks for all-files access (the storage permission on Android 10) and creates `/storage/emulated/0/Black` and `files/assets`, which are where the game files are looked for, along with a `Black` folder at the top of each SD card. `UpdateChecker.java` calls the GitHub API (`/releases/latest`) at every launch, and from the options' Check for Updates. If that tag is newer than the installed `versionName`, it shows an update dialog listing the changes from the release body's `## What's Changed` section, which CI fills from the commit subjects since the previous tag.

### How recompiled code reaches the host

`config/game.toml` is the recompiler's input:
- `[xbe]`: path, entry point, XDK version, output dir, `partitions` (CMake reads this number to list the generated sources), and code sections.
- `[symbols]` / `[data]`: addresses of library functions and globals in the image, 0 where the image has none. Host code refers to them through `SYM(Name)` from `image_symbols.h`.
- `[abi]`: register-argument conventions for link-time-code-generated XDK functions.
- `[[hook]]`: entries `at = <x86 address>`, `call = "<Name>"`. The recompiler emits a call at that address to a host function `void Name(X86Context& ctx, uint8_t* base)`, mostly defined in `src/game/hooks.cpp`. To add a hook, add the `[[hook]]` entry, define the function, and recompile.

Host code reads and writes guest memory through the kernel helpers (`HostPtr(va)`, `R32`, ...) and guest registers through `X86Context`.

### Supported game versions

The build is recompiled from the **USA** `default.xbe`. At runtime a Europe xbe is turned into the US one with `src/common/europe.patch`, which is embedded in the library through `EUROPE_PATCH` in `game_files.cpp`. Code that hardcodes addresses assumes the US image.

## Conventions

- Native code is C++20 and is compiled with `-fsigned-char -fno-strict-aliasing`, matching x86 semantics.
- Only arm64-v8a is built, minSdk 29 (`native/compat/crt.h` supplies `memfd_create`, which bionic has from API 30). Libraries are linked with 16 KB page alignment.
- Comment style is terse, lowercase, and explains *why*, as in the existing files.
