<p align="center"><a href="https://cedarlogic.netlify.app"><img src="https://cedarlogic.netlify.app/assets/img/og.png" alt="CedarLogic: Build it. Watch it think. The free logic gate simulator." width="100%"></a></p>

# CedarLogic

CedarLogic is a free, open-source digital logic simulator made for classroom teaching. Drag in gates, wire them up and watch the signals move. It has all the basic gates, buses, J-K and D flip-flops, muxes, decoders, RAM and a [Z80 microprocessor](https://en.wikipedia.org/wiki/Zilog_Z80). Press T for the truth table, the Karnaugh maps and the simplest formula, check your circuit against an assignment, and export a lab report.

It is one app on three computers: native apps for **Mac**, **Windows** and **Linux** (the Raspberry Pi too), all on the same simulation engine, and [CedarLogic Online](https://cedarlogic.netlify.app/online-logic-gate-simulator/) in the browser. At [Cedarville University](https://www.cedarville.edu/) students in Computer Architecture 1 have used it to build and simulate a full [Mano machine](https://en.wikipedia.org/wiki/Mano_machine).

**[Website](https://cedarlogic.netlify.app)** · **[Download](https://cedarlogic.netlify.app/download/)** · [All releases](https://github.com/leviholliday/CedarLogic-Releases/releases) · [Truth table generator](https://cedarlogic.netlify.app/truth-table-generator/) · [What's new](CHANGELOG.md)

<p align="center">
  <img src="https://cedarlogic.netlify.app/assets/img/window-light.webp" alt="CedarLogic for Mac with a full adder circuit" width="49%">
  <img src="https://cedarlogic.netlify.app/assets/img/windows-window.webp" alt="CedarLogic for Windows in dark mode" width="49%">
</p>

<!-- TODO: a Linux screenshot (2400x1500, like the Mac's window-light.webp). The website has none yet. -->

## Install

0.4 is the first version for Mac, Windows and Linux together. Every way below installs the same version, and the apps keep themselves up to date after that.

### Mac

With [Homebrew](https://brew.sh):

```sh
brew install --cask leviholliday/tap/cedarlogic
```

Or download `CedarLogic-<version>-Mac.dmg` from the [download page](https://cedarlogic.netlify.app/download/) or the [releases](https://github.com/leviholliday/CedarLogic-Releases/releases), open it and drag CedarLogic to Applications.

Needs macOS 14 Sonoma or later on Apple silicon (M1 or later). On an Intel Mac, use CedarLogic Online. The app isn't notarized yet, so the first time you open it (from Homebrew or the disk image), go to **System Settings > Privacy & Security** and click **Open Anyway**. You only do this once.

### Windows

In PowerShell (from 0.4's release day):

```powershell
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

It picks the installer for your PC (x64 or ARM64), checks it, and installs CedarLogic for you, with no administrator.

With [winget](https://learn.microsoft.com/windows/package-manager/) (after release day, once Microsoft has accepted the listing):

```powershell
winget install --id LeviHolliday.CedarLogic -e
```

With [Scoop](https://scoop.sh) (from release day):

```powershell
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

Or download the installer, `CedarLogic-<version>-Windows-Setup-x64.exe` (or `-ARM64.exe`), from the [download page](https://cedarlogic.netlify.app/download/windows/). If Windows says it protected your PC, click **More info**, then **Run anyway**. The commands above don't show that warning. There's also a portable zip, `CedarLogic-<version>-Windows-x64.zip`, to run without installing.

Needs Windows 10 or 11, 64-bit.

### Linux and Raspberry Pi

In a terminal:

```sh
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

On Ubuntu, Debian, Zorin, Mint and Raspberry Pi OS it installs the `.deb` with apt (it asks for your password). On Fedora and the rest it puts the AppImage in your home folder. Either way it checks CedarLogic's signature first, CedarLogic shows up in your applications menu, and it updates itself from then on. Add `-s -- --dry-run` to see what it would do.

Or download a file from the [download page](https://cedarlogic.netlify.app/download/linux/): `cedarlogic_<version>_amd64.deb`, or `_arm64.deb` for a Raspberry Pi 4 or 5, or the `x86_64` or `aarch64` AppImage. They update themselves too.

Needs a 64-bit system: Ubuntu 22.04, Debian 12 (Raspberry Pi OS Bookworm on a Pi 4 or 5), Fedora 36 or newer. There's no apt repository, Flathub or AUR package yet.

[`packaging/README.md`](packaging/README.md) says what each command does and how the releases are signed.

## One engine, three apps

| App | Folder | Built with | Status |
|---|---|---|---|
| CedarLogic for Mac | `mac/` | Swift and AppKit over the shared C++ engine | Current |
| CedarLogic for Windows | `windows/` | Win32 and Direct2D over the shared engine | Current |
| CedarLogic for Linux | `linux/` | GTK 3 and Cairo over the shared engine | Current |
| CedarLogic Online | separate `cedarlogic-site` repository, engine in `wasm/` | JavaScript and WebAssembly | Current |
| CedarLogic Classic | repository root (`CMakeLists.txt`, `src/`) | wxWidgets and Skia | **Legacy**, retired after 4.0.2 |

The three native apps run the same simulation engine, so a circuit behaves the same on every computer. They read and write the same `.cdl` files, sync Your Circuits with each other, and come out together at the same version. Each one is built for its own system, not wrapped in a browser, so it starts fast and fits in. [`docs/V4-PARITY-MATRIX.md`](docs/V4-PARITY-MATRIX.md) lists, feature by feature, what each app has.

## Where things are

| Folder | What it is |
|---|---|
| `mac/` | The native Mac app (Swift/AppKit, `App/`), `build.sh`, signing and DMG scripts. Also holds the shared engine glue `CedarCore/` and the shared headless checks `Tools/` that the Windows and Linux builds use too. |
| `windows/` | The native Windows app (Win32 + Direct2D), its Inno Setup installer and tools. |
| `linux/` | The native Linux app (GTK3 + Cairo), AppImage/deb packaging and `install.sh`. |
| `logic/` | The simulator: gates, wires, events. No UI. Own unit tests. |
| `format/` | The `.cdl` file format: reader, writer, old-format reader, migrations. Own unit tests. |
| `src/gui/`, `include/gui/` | The circuit model, wire routing and edit/undo commands that every native app shares, and (legacy) the wxWidgets UI. |
| `update-signing/` | Ed25519 check of signed updates, used by the Windows and Linux updaters. |
| `packaging/` | Homebrew cask, winget manifests, `install.ps1`, `install.sh`, `sign-release.sh`. |
| `scripts/` | Mac packaging, appcast, DMG background, `sign-preview.sh`. |
| `res/` | The gate library (`cl_gatedefs.xml`), sample circuit, built-in templates, classic help, icons. |
| `tests/`, `tools/` | Fixtures and cases for the checks; small generators. |
| `wasm/` | Browser build of the engine (Emscripten). |
| `docs/` | Format spec, release runbooks, parity matrix, design notes. |
| `CMakeLists.txt`, `cmake/`, `flake.nix`, `Taskfile.yaml`, `Doxyfile` | The original wxWidgets app's build (legacy; see below). |

## Build from source

Each app builds on its own kind of computer, from the repository root.

### Mac

Needs macOS 14 or later and the Xcode Command Line Tools (Swift 6). Xcode itself isn't needed.

```sh
mac/build.sh              # -> mac/build/CedarLogic.app
OPEN=1 mac/build.sh       # build, then launch
CLEAN=1 mac/build.sh      # rebuild everything (after changing a header)
mac/package.sh            # build, then mac/build/CedarLogic-<version>-Mac.dmg
```

The first build downloads Sparkle and checks it against its SHA-256. The app builds for the Mac it runs on.

### Windows

Needs Visual Studio 2019 or later with the C++ desktop workload, CMake, and a Windows SDK (the "Windows 11 SDK" component in the Visual Studio Installer).

```sh
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
windows\build\Release\CedarLogic.exe [circuit.cdl ...]
```

[`windows/README.md`](windows/README.md) also covers building without admin rights, the installer, and cross-compiling with MinGW-w64 from a Mac.

### Linux

Needs a C++17 compiler, CMake, and the GTK 3, Cairo, Pango, fontconfig, FreeType and OpenSSL development files. On Debian, Ubuntu and Raspberry Pi OS:

```sh
sudo apt install build-essential cmake ninja-build \
  libgtk-3-dev libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype-dev libssl-dev
cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build
linux/build/cedarlogic [circuit.cdl ...]

linux/package.sh          # an AppImage (needs linuxdeploy)
linux/package-deb.sh      # a .deb
```

### The engine's checks

The engine is shared, so a change there reaches all three apps at once. Its unit tests run anywhere with CMake and a C++17 compiler, and CI runs them on every pull request:

```sh
cmake -S logic -B build/logic -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build/logic && ctest --test-dir build/logic --output-on-failure
```

[CONTRIBUTING.md](CONTRIBUTING.md) has the rest (`format/`, the headless engine and its fuzz runs, each app's own checks) and how to send a change. The app READMEs ([Mac](mac/README.md), [Windows](windows/README.md), [Linux](linux/README.md)) have the details.

## Help and more

- [Support](SUPPORT.md): which versions get fixes, where to ask, and what the apps send. In any of the apps, **Send Feedback** reaches the developer fastest.
- Contact: [cedarlogic@proton.me](mailto:cedarlogic@proton.me)
- [Security](SECURITY.md): how to report a problem privately
- [Contributing](CONTRIBUTING.md): building, checking and sending a change
- [Changelog](CHANGELOG.md): what changed in each version
- [Third-party code](THIRD-PARTY.md): what CedarLogic ships with and its licenses
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [CDL format](docs/CDL-Format.md): the `.cdl` file specification
- [Sync design](docs/SYNC.md), [Classroom design](docs/CLASSROOM.md), [Core hardening](docs/CORE-HARDENING.md)

## The legacy wx app (CedarLogic Classic)

The original cross-platform app is still in this repository: the root `CMakeLists.txt`, the wx parts of `src/` and `include/`, `cmake/`, `flake.nix` and `Taskfile.yaml`. CI still builds it, because it shares the engine with the native apps. It is retired: its last release was CedarLogic Classic 4.0.2 (beta), and new work goes into the native apps, which open its `.cdl` files. To build it, see [docs/Building.md](docs/Building.md).

<details>
<summary>What this fork added to the classic app</summary>

It builds on [Kieran Klukas's modernized CedarLogic](https://github.com/taciturnaxolotl/CedarLogic) (which brought the Skia renderer, the Sparkle updater and much more) and on the original from Cedarville University.

**Look and feel**
- Dark mode, an accent color, a dot grid, and a wire thickness setting
- A native Preferences window in place of the old dialog
- A restyled toolbar, a side panel you can resize by dragging, and gates that resize smoothly
- A status bar showing zoom, cursor position, and gate counts (each can be turned off)
- Smoother zooming and panning, with separate scroll settings for trackpad and mouse

**Simulation**
- Simulation View: a dark live mode where signals move along the wires as dashes, with a control bar
- A truth table generator

**Editing**
- Delete wires, connect by clicking, and press C to connect while dragging
- Pins that are close together connect when you drop a gate, not only pins that touch
- Straighten Route on a wire's right-click menu, and straightening several selected wires at once without them overlapping
- Cmd+D to duplicate, with a choice of whether it goes through the clipboard
- Shortcuts for zoom, nudging, focus mode, and panning with Space

**Pages and files**
- A Ctrl+Tab page switcher, and new tabs that open in front with a short animation
- Circuits are kept in the app with version history
- Exported images can include a strip with the circuit's name and result

**Stability**
- Fixed races when choosing New or Open while a simulation is running, which could make circuits behave unpredictably after opening
- Fixed a hang when letting a circuit settle, and false "circuit too heavy" warnings on manual steps
- Closed tabs no longer stay in memory after their undo history is gone
- Invalid time-step settings are corrected instead of crashing the simulation

</details>

## Credits

CedarLogic began at [Cedarville University](https://www.cedarville.edu/), written by Benjamin Sprague, Matthew Lewellyn, David Knierim, Joshua Lansford and Nathan Harro (2007), and later Tyler Drake, Julian Pernia, Jackson Vaugn and Colin Broberg (2017). [Kieran Klukas](https://github.com/taciturnaxolotl/CedarLogic) modernized it and brought it to the Mac. This version was rebuilt and extended with Claude, Anthropic's AI model.

CedarLogic is free software under the [GNU GPL v3](LICENSE). The original Cedarville code's notice is in [`res/LICENSE.txt`](res/LICENSE.txt).
