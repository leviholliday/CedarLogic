# Contributing to CedarLogic

Thanks for wanting to help. Everyone is welcome, students most of all. A change is judged on what it does, not on who sent it.

Bug fixes, steadier behaviour, tests, docs and work that makes the apps match each other are the most likely to be merged. For a new feature, open an issue first so we can talk about it. Features nobody discussed are often turned down, however good they are.

## Where things live

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
| `CMakeLists.txt`, `cmake/`, `flake.nix`, `Taskfile.yaml`, `Doxyfile` | The original wxWidgets app's build (legacy). |

The engine every app runs is `logic/`, `format/`, the parts of `src/gui/` that `mac/build.sh` lists, and `mac/CedarCore/`. A change there reaches the Mac, Windows and Linux apps at once, so it needs the most care.

Each app has a README with the full story: [`mac/README.md`](mac/README.md), [`windows/README.md`](windows/README.md), [`linux/README.md`](linux/README.md). The `.cdl` format is in [`docs/CDL-Format.md`](docs/CDL-Format.md).

## Build and check each app

### The shared engine (any computer with CMake and a C++17 compiler)

These are the commands CI runs (`.github/workflows/ci.yml` and `core-hardening.yml`, on Ubuntu 24.04 with clang):

```sh
# logic/ and format/ unit tests, with AddressSanitizer and UBSan
cmake -S logic -B build/logic -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build/logic
ctest --test-dir build/logic --output-on-failure

cmake -S format -B build/format -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build/format
ctest --test-dir build/format --output-on-failure

# the headless engine, its checks and short fuzz runs
cmake -S mac/Tools/headless -B build/headless -G Ninja -DCL_SANITIZE=ON -DCL_LIBFUZZER=ON
cmake --build build/headless
ctest --test-dir build/headless --output-on-failure
```

`docs/CORE-HARDENING.md` says what each check looks for. If you change the engine, run these before you open a pull request.

### Mac

Needs macOS 14 or later and the Xcode Command Line Tools (Swift 6). Xcode itself is not needed.

```sh
mac/build.sh              # -> mac/build/CedarLogic.app
OPEN=1 mac/build.sh       # build, then launch
CLEAN=1 mac/build.sh      # rebuild everything (after changing a header)
mac/package.sh            # build, then mac/build/CedarLogic-<version>-Mac.dmg
```

The first build downloads Sparkle 2.10.1 and checks it against its SHA-256. The app builds for the Mac it runs on (Apple silicon).

The headless checks:

```sh
mac/Tools/build-tools.sh                                          # builds them into mac/build
mac/build/cl_check res/cl_gatedefs.xml
mac/build/edit_check res/cl_gatedefs.xml res/samples/practice.cdl 0
mac/build/save_check res/cl_gatedefs.xml format/tests/fixtures/Lab5.cdl
mac/Tools/session-check.sh 0                                      # the scripted part of a long session, about a minute
```

`mac/README.md` lists every check and what it covers. No CI job builds the Mac app yet, so say in your pull request which checks you ran.

### Windows

Needs Visual Studio 2019 or later with the C++ desktop workload, CMake, and a Windows SDK (the "Windows 11 SDK" or "Windows 10 SDK" component in the Visual Studio Installer).

```sh
cmake -S windows -B windows/build -A x64
cmake --build windows/build --config Release
windows\build\Release\CedarLogic.exe [circuit.cdl ...]
```

The same checks as the Mac's are built beside the app (`-DCL_BUILD_TOOLS=OFF` skips them). The app has its own self-tests too, for example `CedarLogic.exe --click-test` and `CedarLogic.exe --session-check`. `windows/README.md` has the full list, how to build without admin rights, and how to cross-compile with MinGW-w64 from a Mac.

### Linux

Needs a C++17 compiler, CMake, and the GTK 3, Cairo, Pango, fontconfig, FreeType and OpenSSL development files. On Debian, Ubuntu and Raspberry Pi OS:

```sh
sudo apt install build-essential cmake ninja-build \
  libgtk-3-dev libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype-dev libssl-dev
```

Then:

```sh
cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build
linux/build/cedarlogic [circuit.cdl ...]

linux/package.sh          # an AppImage (needs linuxdeploy)
linux/package-deb.sh      # a .deb
```

The checks are built beside the app, as on Windows. `linux/Tools/session-check.sh` runs the long session under Xvfb. `linux/README.md` has the rest.

### The website

The web app, CedarLogic Online, the feedback inbox and the sync server live in a separate repository, `cedarlogic-site`. Its README says how to build and test it.

### The legacy wx app

The original wxWidgets app still builds from the root `CMakeLists.txt` (see [`docs/Building.md`](docs/Building.md)), and `ci.yml` still builds it on Linux and Windows. It is retired: only fix it when a shared-engine change breaks it.

## What CI runs

| Workflow | When | What |
|---|---|---|
| `ci.yml` | pushes to `main`, every pull request | `logic/` and `format/` unit tests, coverage, the wx app on Linux and Windows, the WebAssembly engine |
| `core-hardening.yml` | every push, every pull request | the engine under ASan/UBSan and TSan, its checks, a few minutes of each fuzzer |
| `windows-native.yml` | pushes to `windows/native` and `windows/native-*` | builds the Windows app for x64 and ARM64, runs its checks and self-tests, builds and tries the installer |
| `linux-native.yml` | pushes to `linux/native` and `linux/native-*` | builds the Linux app for x86_64 and aarch64, runs its checks, packages and starts the AppImage and .deb |
| `nix.yml` | when the flake or the Skia setup changes, and weekly | the wx app through Nix |
| `windows.yml` | pushes to other `windows/*` branches | the wx app on Windows |

The Windows and Linux native workflows do not run on pull requests yet. If your change touches `windows/` or `linux/` (or the engine they share), push it to a `windows/native-<topic>` or `linux/native-<topic>` branch as well, so their CI runs, and link the green run in your pull request.

## Sending a change

1. Open an issue first for anything bigger than a bug fix.
2. Make a branch from `main`. Name it after the area and the topic, the way the existing branches are:
   - `mac/<topic>`, `core/<topic>`, `docs/<topic>`, `pkg/<topic>`, `sec/<topic>`, `tools/<topic>`
   - `ui/windows-<topic>`, `ui/linux-<topic>` for app look and behaviour
   - `windows/native-<topic>`, `linux/native-<topic>` when you want the native Windows or Linux CI to run
3. Keep commits small, one idea each, with a message that says what changed and why.
4. Open a pull request to `main`. Fill in the template: what changed, why, and how you checked it.
5. CI must be green before it is merged.

A few rules:

- **Don't push to `windows/native` or `linux/native`.** CI publishes test builds from those two branches, and people's installed apps update from them.
- **Don't edit `.github/workflows/skia.yml` without the owner.** It builds and caches Skia for the wx app (20 to 40 minutes cold), and a change there can break every other build that uses it.
- **Keep the apps in step.** If you add something to one app, say in the pull request whether the others need it too. `docs/V4-PARITY-MATRIX.md` tracks this.
- **Keep `.cdl` files readable by older versions** where you can. `docs/CDL-Format.md` describes the format.

## Questions

Open an issue on [GitHub](https://github.com/leviholliday/CedarLogic/issues), or use Send Feedback in the app. Security problems go through [SECURITY.md](SECURITY.md) instead. Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md).
