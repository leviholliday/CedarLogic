<p align="center"><a href="https://cedarlogic.netlify.app"><img src="https://cedarlogic.netlify.app/assets/img/og.png" alt="CedarLogic: Build it. Watch it think. The free logic gate simulator." width="100%"></a></p>

# CedarLogic

CedarLogic is a free, open-source digital logic simulator made for classroom teaching. Drag in gates, wire them up and watch the signals move. It has all the basic gates, buses, J-K and D flip-flops, muxes, decoders, RAM and a [Z80 microprocessor](https://en.wikipedia.org/wiki/Zilog_Z80). Press T for the truth table, the Karnaugh maps and the simplest formula, check your circuit against an assignment, and export a lab report. There are native apps for Mac, Windows and Linux, and [CedarLogic Online](https://cedarlogic.netlify.app/online-logic-gate-simulator/) in the browser. At [Cedarville University](https://www.cedarville.edu/) students in Computer Architecture 1 have used it to build and simulate a full [Mano machine](https://en.wikipedia.org/wiki/Mano_machine).

**[Website](https://cedarlogic.netlify.app)** · **[Download](https://cedarlogic.netlify.app/download/)** · [All releases](https://github.com/leviholliday/CedarLogic-Releases/releases) · [What's new](CHANGELOG.md)

<p align="center">
  <img src="https://cedarlogic.netlify.app/assets/img/window-light.webp" alt="CedarLogic for Mac with a full adder circuit" width="49%">
  <img src="https://cedarlogic.netlify.app/assets/img/windows-window.webp" alt="CedarLogic for Windows in dark mode" width="49%">
</p>

<!-- TODO: a Linux screenshot (2400x1500, like the Mac's window-light.webp). The website has none yet. -->

## Install

| Computer | Command |
|---|---|
| Mac | `brew install --cask leviholliday/tap/cedarlogic` |
| Windows | `irm https://cedarlogic.netlify.app/install.ps1 \| iex` (PowerShell) |
| Windows | `winget install --id LeviHolliday.CedarLogic -e` (coming) |
| Linux | `curl -fsSL https://cedarlogic.netlify.app/install.sh \| sh` |

Each command installs the current release and checks what it downloads. The Windows and Linux commands start working with 0.4. Or download the app yourself from **[cedarlogic.netlify.app/download](https://cedarlogic.netlify.app/download/)**.

- **Mac:** macOS 14 Sonoma or later, Apple silicon. The app isn't notarized yet, so the first time you open it, go to System Settings > Privacy & Security and click Open Anyway.
- **Windows:** Windows 10 or 11, 64-bit (x64 or ARM64). No administrator needed.
- **Linux:** 64-bit Ubuntu 22.04, Debian 12 (and Raspberry Pi OS on a Pi 4 or 5), Fedora 36 or newer. On apt systems it installs a .deb; elsewhere it puts an AppImage in your home folder.

The apps keep themselves up to date.

## Which app is which

| App | Folder | Built with | Status |
|---|---|---|---|
| CedarLogic for Mac | `mac/` | Swift and AppKit over the shared C++ engine | Current |
| CedarLogic for Windows | `windows/` | Win32 and Direct2D over the shared engine | Current |
| CedarLogic for Linux | `linux/` | GTK 3 and Cairo over the shared engine | Current |
| CedarLogic Online | separate `cedarlogic-site` repository, engine in `wasm/` | JavaScript and WebAssembly | Current |
| CedarLogic Classic | repository root (`CMakeLists.txt`, `src/`) | wxWidgets and Skia | **Legacy**, retired after 4.0.2 |

All three native apps run the same simulation engine, so a circuit behaves the same everywhere. They read and write the same `.cdl` files.

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

## Building

Each app builds on its own:

```sh
mac/build.sh                                                  # Mac: mac/build/CedarLogic.app
cmake -S windows -B windows/build -A x64 && cmake --build windows/build --config Release    # Windows
cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build linux/build   # Linux
```

[CONTRIBUTING.md](CONTRIBUTING.md) has what each one needs, the checks to run, and how to send a change. The app READMEs ([Mac](mac/README.md), [Windows](windows/README.md), [Linux](linux/README.md)) have the details.

## More

- [Contributing](CONTRIBUTING.md): building, checking and sending a change
- [Security](SECURITY.md): how to report a problem privately
- [Support](SUPPORT.md): which versions get fixes, where to ask, and what the apps send
- [Changelog](CHANGELOG.md): what changed in each version
- [Third-party code](THIRD-PARTY.md): what CedarLogic ships with and its licenses
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [CDL format](docs/CDL-Format.md): the `.cdl` file specification
- [Sync design](docs/SYNC.md), [Classroom design](docs/CLASSROOM.md), [Core hardening](docs/CORE-HARDENING.md)

## The legacy wx app (CedarLogic Classic)

The original cross-platform app is still in this repository: the root `CMakeLists.txt`, the wx parts of `src/` and `include/`, `cmake/`, `flake.nix` and `Taskfile.yaml`. CI still builds it, because it shares the engine with the native apps. It is retired: its last release was CedarLogic Classic 4.0.2 (beta), and new work goes into the native apps. To build it, see [docs/Building.md](docs/Building.md).

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
