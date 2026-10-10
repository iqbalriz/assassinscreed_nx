<div align="center">

<img src="launcher/icon.jpg" alt="Assassin's Creed: Altair's Chronicles HD" width="160">

# assassinscreed_nx

**Assassin's Creed: Altair's Chronicles HD for Nintendo Switch**

An unofficial Nintendo Switch native wrapper for the 32-bit Android release of  
**Assassin's Creed: Altair's Chronicles HD** (Gameloft).

[![Nintendo Switch](https://img.shields.io/badge/Nintendo_Switch-Homebrew-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)](#)
[![Version](https://img.shields.io/badge/Version-0.1.0-4C8BF5?style=for-the-badge)](#)
[![Architecture](https://img.shields.io/badge/AArch32-32--bit_Native-6A1B9A?style=for-the-badge)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)

</div>

---

## About

`assassinscreed_nx` is a native wrapper that runs the 32-bit ARM Android build of **Assassin's Creed: Altair's Chronicles HD 1.0.5** (`com.gameloft.android.TBFV.GloftASCR.ML`) on Nintendo Switch.

It loads the game's original `libassissinscreed.so` (Gameloft's own engine) and recreates what the game's Java side did: the OpenGL ES 1 context, the activity lifecycle, touch input, the sound player, the data folders, and the answers the game asks of a phone.

Because the Tegra X1 CPU in the Nintendo Switch natively supports 32-bit ARM (AArch32) execution, the game code runs **directly on the hardware at full native speed**, with no CPU emulation.

> [!NOTE]
> **No game code or assets are included in this repository or in the release.**  
> Users must supply their own legitimate copy of the game's APK and data folder.

---

## Features

- **Full Native Performance:** Runs natively on the Switch ARM Cortex-A57 CPU in AArch32 mode.
- **Hardware-Accelerated Graphics:** Uses OpenGL ES 1 (the game's fixed-function renderer) via `mesa32` (Nouveau), at the game's own 800x480 scaled to the screen.
- **Touchscreen & Controller Support:** The game is made for touch only. Touch works as on mobile, and a controller plays it too: the sticks and buttons are turned into touches on the game's on-screen controls, and a **virtual cursor** covers the menus and minigames.
- **Sound:** The game's music, effects and voices (Ogg Vorbis) played through the Switch's audio output.
- **Saves & HOME menu:** Progress is saved on the SD card; HOME and sleep pause and resume the game.
- **Atmosphère & Sphaira Integration:** A dedicated launcher NRO registers a HOME menu forwarder icon.

---

## Requirements

### For Players
- A Nintendo Switch running **Atmosphère** custom firmware.
- The [Sphaira](https://github.com/ITotalJustice/sphaira) homebrew menu (Important: you need to install Sphaira's forwarder, otherwise the port won't work — the system would start it as a 64-bit program instead of 32-bit).
- A copy of **Assassin's Creed: Altair's Chronicles HD 1.0.5** for Android (`com.gameloft.android.TBFV.GloftASCR.ML`), the APK with `lib/armeabi/libassissinscreed.so` inside. The name of the APK does not matter.
- The game's data folder from an Android device: `gameloft/games/assassinscreed` (the APK does not contain the levels, graphics and sounds).

---

## Installation Guide

1. Download the latest release from the [Releases](../../releases) tab:
   - `assassinscreed_nx_0.1.0.zip`
2. Copy the `switch/assassinscreed_nx/` folder from the ZIP to the root of your SD card, so you get:
   ```text
   sdmc:/switch/assassinscreed_nx/
   ```
3. Place your APK and your game data inside that folder:
   - `assassinscreed_nx.nro` is already there (from the ZIP).
   - Copy your APK into `sdmc:/switch/assassinscreed_nx/` (the name of the apk does not matter).
   - Copy your phone's `gameloft` folder into `sdmc:/switch/assassinscreed_nx/` (it holds `games/assassinscreed/`).
4. The final folder structure on your SD card must look like:
   ```text
   sdmc:/switch/assassinscreed_nx/
   ├── assassinscreed_nx.nro
   ├── your-game.apk
   └── gameloft/
       └── games/
           └── assassinscreed/
               ├── data.bar
               ├── level01.bar
               ├── raw_0000.ogg
               ├── UK.bar
               └── ...
   ```
5. Launch **Sphaira** on your Switch:
   - Navigate to **Homebrew** › **Assassin's Creed: Altair's Chronicles HD**.
   - Choose **Install Forwarder**.
   - Return to the Switch HOME Menu and launch the game directly from its icon!

The first start unpacks the game's library from the APK (a few seconds, with a progress bar; again only when the APK changes). Your progress is saved in the `gameloft/games/assassinscreed/` folder.

---

## Controls

| Input | Action |
| :--- | :--- |
| **Touchscreen** | Direct touch controls (identical to the mobile version) |
| **Left Stick** | Move (the on-screen stick) |
| **X** | Run / Jump |
| **A** | Light attack (dagger) |
| **B** | Sword |
| **Y** | Context action above Run (pickpocket, assassinate, ...) |
| **L** | Guard |
| **ZL** | The top-left button (hand / weapon) |
| **R** | The scroll button (top right) |
| **Right Stick** | Show and move the **cursor** (it fades a few seconds after you stop) |
| **ZR** | Tap with the cursor. **Hold ZR and move the right stick** to drag (the pickpocket minigame) |
| **Main menu: D-Pad Up / Down, A** | Pick a button, tap it |
| **− (Minus)** | Pause |
| **+ (Plus)** | Nothing |

---

## Configuration

`config.ini` is written to `sdmc:/switch/assassinscreed_nx/` on the first start. Changes apply the next time the game starts.

<details>
<summary>All options</summary>

| Section | Option | Values |
| :--- | :--- | :--- |
| `[game]` | `language` | `en` `de` `fr` `it` `es` `pt` `pt-br` `zh` (written into the game's options file at start) |
| | `frame_limit` | `true` (default): the game's own 20 frames a second. `false` makes the game run far too fast |
| `[sound]` | `volume` | 0 to 100 |
| `[graphics]` | `render_size` | `800x480` (default) or `screen` |
| `[controls]` | `touch_screen` | `true` / `false` |
| `[display]` | `resolution` | `auto`, `720`, `1080` (used when `render_size = screen`) |
| `[performance]` | `boost_cpu_when_loading` | CPU at 1785 MHz until the first picture |
| `[debug]` | `log_input`, `log_file_access`, `gl_trace`, `log_java_calls`, `gl_selftest`, `boot_log_on_screen` | for bug reports |

</details>

If something goes wrong, `debug.log` and `crash.log` are written next to `config.ini`. For a bug report, turn on `log_input` or `log_file_access` and send the log.

---

## Not in This Port

The intro movie (`intro.mp4`), the store and purchases, ads, "more games" links, the web browser and the online features are switched off: the game's Java side answers as a phone with no account and no network.

---

## Building from Source

### Prerequisites
- Linux (Ubuntu / Debian / Linux Mint recommended), or Windows with PowerShell
- **Docker**
- Git

> [!IMPORTANT]
> **On Windows, run `git config --global core.autocrlf false` before cloning.** With `autocrlf=true` the runtime's scripts and Makefiles are checked out with CRLF line endings and do not run in the Linux container.

### Build Instructions

1. **Clone the repository with submodules:**
   ```bash
   git clone --recursive https://github.com/iqbalriz/assassinscreed_nx.git
   cd assassinscreed_nx
   ```

2. **Pull the required Docker toolchains:**
   ```bash
   docker pull ghcr.io/vita2hos/devcontainer/vita2hos
   docker pull devkitpro/devkita64
   ```

3. **Set up `libnx32` and `mesa32`:**
   - Compile `libnx32` next to this folder (the build looks for `../libnx32/prefix`):
     ```bash
     git clone https://github.com/aks796/libnx32.git ../libnx32
     ../libnx32/build.sh
     ```
   - Download the prebuilt `mesa32` release into `portlibs32/`:
     ```bash
     tools/get_portlibs.sh
     ```
     (On Windows: `tools\get_portlibs.ps1`.)

4. **Compile the 32-bit program:**
   ```bash
   ./build.sh
   ```
   (On Windows: `.\build.ps1`.) This makes `assassinscreed_nx.nsp`.

5. **Compile the 64-bit launcher NRO:**
   ```bash
   launcher/build.sh
   ```
   The compiled launcher will be located at `launcher/assassinscreed_nx.nro`.

---

## Credits & Acknowledgments

- **Gameloft & Ubisoft**: Original creators of Assassin's Creed: Altair's Chronicles. Assassin's Creed is a trademark of Ubisoft.
- **[aks796](https://github.com/aks796)**: For the [`android32`](https://github.com/aks796/android32) runtime, [`libnx32`](https://github.com/aks796/libnx32), [`mesa32`](https://github.com/aks796/mesa32), and the [`flappybirdsfamily_nx`](https://github.com/aks796/flappybirdsfamily_nx) reference port this family of ports started from.
- **Andy Nguyen (TheOfficialFloW) & fgsfds**: Dynamic `.so` loader implementations.
- **xerpi**: For `vita2hos`, pioneer of AArch32 native execution on Switch.
- **Switchbrew**: For `libnx` and tools.
- **ITotalJustice**: For Sphaira.
- **Sean Barrett**: For `stb_vorbis` (public domain / MIT).

---

## License

This project is licensed under the [MIT License](LICENSE).
Assassin's Creed is a trademark of Ubisoft Entertainment; the game is by Gameloft. This project is not affiliated with or endorsed by Ubisoft, Gameloft or Nintendo.
