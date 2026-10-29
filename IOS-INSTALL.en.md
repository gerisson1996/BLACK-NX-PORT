# Installing Black on iPhone (iOS)

🇧🇷 [Português](IOS-INSTALL.md) · 🇺🇸 **English** · 🇪🇸 [Español](IOS-INSTALL.es.md)

This guide covers the three usual ways to install `Black-iOS-0.2.4.ipa`:

- **A — Sideloadly** (Windows or macOS PC, USB cable, signed with your Apple ID)
- **B — 3uTools** (Windows PC; installs the app and also copies the game files)
- **C — on the phone itself** (TrollStore, or AltStore/SideStore after a one-time PC install)

In every case the app is the same: the port ships no game. You need your own copy of Black (the folder with `default.xbe`, or a `.iso`).

---

## Before you start

| Item | Detail |
|---|---|
| iOS | 15 or newer |
| App file | `Black-iOS-0.2.4.ipa` (from the Releases page) |
| Game | a `game` folder with `default.xbe` plus `chars`, `data`, `export`, `language`, `levels`, `sound`, `videos` — **or** a single `.iso` |
| Apple ID | needed to sign (Sideloadly/3uTools/AltStore). A free account lasts **7 days** per install |
| TrollStore | only on iOS 14.0–16.6.1 (and 17.0 on some devices); there the `.ipa` needs **no** signing |

> The app downloads nothing from the internet. The game files go in over the cable (iTunes/3uTools) or through the iPhone's **Files** app.

---

## A — Sideloadly (PC + cable)

Sideloadly signs the `.ipa` with your Apple ID and installs it over the cable. Works on Windows and macOS.

### 1. On the PC

1. Get **Sideloadly** from <https://sideloadly.io> and install it.
2. On Windows, also install **iTunes** (Apple's, not the Microsoft Store one) — it provides the iPhone drivers. Not needed on macOS.
3. Connect the iPhone with the cable and tap **Trust** on the phone. Enter the passcode if asked.

### 2. Install

1. Open **Sideloadly**.
2. Under **iOS Device**, pick your iPhone.
3. Drag `Black-iOS-0.2.4.ipa` into the window (or click the `.ipa` icon and choose the file).
4. In **Apple ID**, type your Apple account e-mail. The password goes into a Sideloadly pop-up (not into the app).
   - With two-factor authentication, type the code when asked.
5. Click **Start**.
6. Wait for the bar to finish; the app appears on the home screen.

### 3. Trust the app (first time only)

On the iPhone: **Settings → General → VPN & Device Management → [your Apple ID] → Trust**.

Then open **Black**.

> A free account signs for **7 days**. When the app stops opening, repeat step 2 (Sideloadly installs over it and the data in Documents is kept).

---

## B — 3uTools (PC + cable)

3uTools installs the `.ipa` and is also the most direct way to copy the game into the app.

### 1. On the PC

1. Get **3uTools** from <https://www.3u.com> and install it.
2. Connect the iPhone with the cable and tap **Trust**.
3. If 3uTools asks, let it install its driver/“3uTools mobile” and trust it on the phone.

### 2. Install the app

3uTools does not install an **unsigned** `.ipa` on a normal device. Use one of these:

- **Already signed:** `Apps → IPA Install` (or drag the `.ipa` into the window) and choose the file.
- **Sign first:** `Toolbox → IPA Signature` (in some versions `Apps → IPA Signature`), choose the `.ipa`, sign in with the Apple ID and sign it; then install the signed file with `IPA Install`.

On the iPhone, trust the certificate as in method A: **Settings → General → VPN & Device Management**.

### 3. Copy the game

1. In 3uTools: **Apps** → find **Black** in the list → **Documents** (or “Browse”).
2. Drag in either:
   - the `game` folder (with `default.xbe` inside), or
   - any `.iso` file.
3. Done: the app looks for `Documents/game`, `Documents/black/game` and any `.iso` in `Documents` or `Documents/black`.

---

## C — On the phone (no PC at install time)

### C1 — TrollStore (the simplest, when iOS allows it)

TrollStore installs `.ipa` files **unsigned** and without the 7-day limit.

1. Check whether your iOS/device is supported (iOS 14.0–16.6.1, and 17.0 in some cases) at <https://github.com/opa334/TrollStore>.
2. Install TrollStore with the method for your iOS (TrollInstallerX, TrollHelper, etc.).
3. On the iPhone, download `Black-iOS-0.2.4.ipa` (Safari) and save it to **Files**.
4. Open **Files**, tap the `.ipa` → **Share** → **TrollStore** (or open TrollStore → **Apps** tab → `+` → choose the `.ipa`).
5. Confirm the install and open the app.

### C2 — AltStore / SideStore (renewal without a cable)

Needs a PC **once** to install AltServer; after that renewal happens over Wi-Fi.

1. On the PC: install **AltServer** (<https://altstore.io>) and iTunes.
2. Connect the iPhone and trust the computer.
3. In AltServer (Windows tray): **Install AltStore → [your iPhone]** and sign in with the Apple ID.
4. On the iPhone: **Settings → General → VPN & Device Management** → trust the AltStore profile.
5. Open **AltStore** → **My Apps** → **+** (top corner) → choose the `Black-iOS-0.2.4.ipa` in **Files**.
6. AltStore signs and installs it. Renewal happens with AltStore open and the PC on the same network (or use **SideStore**, which renews by itself).

### C3 — Copying the game without a PC

1. Download the game folder (or the `.iso`) on the iPhone and save it to **Files**.
2. In **Files**, hold the folder/file → **Move** → **On My iPhone** → **Black**.
   - The `game` folder can go in `Black/game`; the `.iso` can go in the root of `Black`.
3. Open Black.

---

## Expected file layout

```
On My iPhone/Black/             <- the app's Documents folder, visible in the Files app
├── game/
│   ├── default.xbe
│   ├── chars/  data/  export/  language/  levels/  sound/  videos/
└── (optional) some .iso
```

- `default.xbe` must be **directly** inside `game` (a subfolder such as `Black PT-BR` in between breaks it).
- The `.iso` can be in the root of `Black` or inside `game`.
- Logs are in `Black/logs/` — useful when something fails.

---

## Common problems

| Symptom | What to do |
|---|---|
| “Unable to verify app” / the app won't open | Trust the certificate in **Settings → General → VPN & Device Management** |
| It worked and stopped after ~7 days | Signature expired: reinstall with Sideloadly/AltStore/3uTools (data is kept) |
| Black screen and back to the home screen | Game not found: check the layout above and read `Black/logs/black_*.log` |
| Black screen but the app stays open | Read the log: the last line before it stops names the step (Vulkan, boot, …) |
| Game runs but there is no touch | The controls overlay appears when you touch the screen; the editor is in the port menu → **TOUCH CONTROLS → EDIT LAYOUT** |
| Slow / stuttering | In the port menu, lower **RENDER SCALE** (e.g. 50–67%) — the iPhone heats up and drops its clock |

---

## Where to download

- App: the repository's **Releases** page (`Black-iOS-0.2.4.ipa`).
- Logs and settings: inside the app, in **Files → On My iPhone → Black** (`black.toml`, `logs/`, `hdd/`).

The port ships no game files: use your own copy of Black.
