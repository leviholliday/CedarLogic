<!--
DRAFT of the README for github.com/leviholliday/CedarLogic-Releases, for 0.4
(the first release for Mac, Windows and Linux together). Not pushed there.
Copy it over on release day: go-list step 12 in packaging/README.md, after
the release is signed and out of beta (steps 6 and 7) and the Scoop bucket
exists (step 10). Keep the winget line's "once it's listed" until the
winget-pkgs PR is merged (step 16).
-->

<p align="center"><a href="https://cedarlogic.netlify.app"><img src="https://cedarlogic.netlify.app/assets/img/og.png" alt="CedarLogic: Build it. Watch it think. The free logic gate simulator." width="100%"></a></p>

# CedarLogic downloads

**CedarLogic** is a free, open-source digital logic simulator, for Mac, Windows and Linux. Drag in gates, wire them up and watch the signals flow. Then press one key for the truth table, the Karnaugh map and the simplest formula.

**Website: [cedarlogic.netlify.app](https://cedarlogic.netlify.app)** · [Download](https://cedarlogic.netlify.app/download/) · [What's new](https://cedarlogic.netlify.app/whats-new/) · [Source code](https://github.com/leviholliday/CedarLogic)

All three apps run the same engine, open the same `.cdl` files and come out together at the same version. Each release below has the files for every computer.

## Get it

### Mac

The easiest way, if you use [Homebrew](https://brew.sh):

```sh
brew install --cask leviholliday/tap/cedarlogic
```

Or download **`CedarLogic-<version>-Mac.dmg`** from the [newest release](../../releases/latest), open it and drag CedarLogic into Applications.

**The first time you open it,** macOS may say it can't check the app. Open **System Settings › Privacy & Security**, scroll down and click **Open Anyway** next to CedarLogic. You only do this once, whichever way you installed it.

### Windows

Paste this into PowerShell:

```powershell
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

It downloads the right installer for your PC (Intel/AMD or ARM), checks it, installs CedarLogic for you and opens it. No administrator needed.

With winget, once CedarLogic is listed there:

```powershell
winget install --id LeviHolliday.CedarLogic -e
```

With [Scoop](https://scoop.sh):

```powershell
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

Or download the installer from the [newest release](../../releases/latest): **`CedarLogic-<version>-Windows-Setup-x64.exe`** for most PCs and laptops, or **`-ARM64.exe`** for Windows on ARM (Snapdragon). If Windows says "Windows protected your PC", click **More info**, then **Run anyway**. The commands above don't show that warning. To run it without installing, take the portable zip, `CedarLogic-<version>-Windows-x64.zip` (or `-ARM64.zip`).

It installs just for you, in the Start menu. `.cdl` files open in it, and Settings › Apps removes it. It keeps itself up to date.

### Linux and Raspberry Pi

Paste this into a terminal:

```sh
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

On Ubuntu, Debian, Zorin, Mint and Raspberry Pi OS it installs CedarLogic with apt (it asks for your password). On Fedora and other systems it puts the AppImage in your home folder. Either way CedarLogic appears in your applications menu and keeps itself up to date, and the script checks CedarLogic's signature before it installs anything. Add `-s -- --dry-run` to see what it would do first.

Or download a file from the [newest release](../../releases/latest):

- **`.deb`** for Ubuntu, Debian and Raspberry Pi OS: `cedarlogic_<version>_amd64.deb` for PCs, `cedarlogic_<version>_arm64.deb` for a Raspberry Pi 4 or 5 or an ARM laptop. Double-click it, or `sudo apt install ./cedarlogic_<version>_amd64.deb`.
- **AppImage** for any distribution, nothing to install: `CedarLogic-Native-<version>-x86_64.AppImage` or `-aarch64.AppImage`. Make it executable (`chmod +x`, or Properties › Allow executing) and run it; it adds itself to the applications menu. Both update themselves. If it won't open, install FUSE 2 (Fedora: `sudo dnf install fuse fuse-libs`; Ubuntu 24.04: `sudo apt install libfuse2t64`).

### No install

[CedarLogic Online](https://cedarlogic.netlify.app/online-logic-gate-simulator/) runs in any browser, on a Chromebook, a tablet or a phone too.

## System requirements

| | Needs |
|---|---|
| **Mac** | macOS 14 Sonoma or later, on Apple silicon (M1 or later). Intel Macs aren't supported yet. |
| **Windows** | Windows 10 or 11, 64-bit: x64 (Intel or AMD) or ARM64. No administrator. |
| **Linux** | 64-bit Ubuntu 22.04 or later, Debian 12 or later, Raspberry Pi OS Bookworm (64-bit, on a Pi 4 or 5), or Fedora 36 or later (with the AppImage). |

## Updates and testing groups

CedarLogic keeps itself up to date. It checks once a day, and you can check now: **CedarLogic › Check for Updates…** on a Mac, **Help › Check for Updates…** on Windows and Linux. The bigger updates open with a short tour of what's new.

**Settings › General › Testing group** picks what you get:

- **Normal tester** (where every new install starts): the releases here, once they're settled.
- **Beta tester**: new versions first, before they're ready for everyone. On a Mac these are the releases marked *Pre-release* here. On Windows and Linux they're the rolling test builds, [`windows-native-testing`](https://github.com/leviholliday/CedarLogic/releases/tag/windows-native-testing) and [`linux-native-testing`](https://github.com/leviholliday/CedarLogic/releases/tag/linux-native-testing) in the source repository.

Switching back from Beta to Normal never takes you back to an older version; you stay where you are until the next release passes it. **Check Now** beside the choice checks straight away.

## How releases are checked

- **Every release has a signed list of its files.** `SHA256SUMS` gives each file's size and SHA-256, and `SHA256SUMS.sig` is its Ed25519 signature, made with CedarLogic's update key. The list names its own release, so it can't be passed off as another one. The Windows and Linux one-line installs refuse a file that doesn't match it.
- **Homebrew** checks the disk image against the SHA-256 written in the cask.
- **The apps' own updates are signed too.** The Mac app takes an update only if its EdDSA signature matches the key built into the app (`appcast-native.xml` and `appcast-native-beta.xml` here are the update feeds it reads). The Windows and Linux apps check each update against the signed list.
- **Not signed by Apple or Microsoft yet.** The Mac app isn't notarized and the Windows installer has no code-signing certificate. Both cost money every year, and CedarLogic is a free project. That's why macOS asks for Open Anyway and Windows shows SmartScreen the first time.

To check a download yourself (OpenSSL 3), save this as `cedarlogic-update-key.pem`:

```
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAQYTauigwwdtBa+eGmn2HN4gXDqHAAQSry/0BjX/jz0A=
-----END PUBLIC KEY-----
```

Then, in the folder with the release's `SHA256SUMS`, `SHA256SUMS.sig` and your file:

```sh
openssl pkeyutl -verify -pubin -inkey cedarlogic-update-key.pem -rawin -in SHA256SUMS -sigfile SHA256SUMS.sig
```

That should say `Signature Verified Successfully`. The list names its release (the `tag` line), then has a line `file <name> <size> <sha256>` for each file. Compare your file's line with its real SHA-256:

```sh
grep '^file CedarLogic-<version>-Mac.dmg ' SHA256SUMS
shasum -a 256 CedarLogic-<version>-Mac.dmg
```

## What's in CedarLogic

- **Build fast.** 90+ parts: gates, flip-flops, registers, counters, multiplexers, decoders, adders, RAM, ROM, keypads and displays. Press **A** and type to add one; drag pin to pin to wire; **C** connects while you drag.
- **Check your work.** **T** makes the truth table, a Karnaugh map for each light, and the simplest sum of products and product of sums. **Check My Circuit** compares your circuit with the assignment. **Build from Formula** goes the other way.
- **See it run.** Simulation View runs the signal along every lit wire; the oscilloscope exports timing diagrams.
- **It keeps itself.** Every circuit lives in Your Circuits and saves itself, with version history and Recently Deleted. Sync keeps them on all your devices, with no account.
- **Hand it in.** Export a lab report PDF with your name, a picture, or a share link; print; or save a `.cdl` for older CedarLogic. It opens classic `.cdl` circuits.

## About

CedarLogic began at [Cedarville University](https://www.cedarville.edu/) and was modernized and brought to the Mac by [Kieran Klukas](https://github.com/taciturnaxolotl/CedarLogic). This version was rebuilt as one app for Mac, Windows and Linux, and extended, with Claude, Anthropic's AI model. Free software under the GPL-3.0; the source is at [leviholliday/CedarLogic](https://github.com/leviholliday/CedarLogic).

The Mac app can send anonymous crash reports and usage stats, but only if you say yes; Settings › Privacy changes it ([what's sent](https://github.com/leviholliday/CedarLogic/blob/main/docs/PRIVACY.md)).

Found a bug or have an idea? Use **Send Feedback** in the app, [open an issue](https://github.com/leviholliday/CedarLogic/issues), or email [cedarlogic@proton.me](mailto:cedarlogic@proton.me). Security problems: see [SECURITY.md](https://github.com/leviholliday/CedarLogic/blob/main/SECURITY.md).

<sub>`appcast-native.xml` and `appcast-native-beta.xml` are the update feeds the Mac app reads; `appcast.xml` and `appcast-beta.xml` are the retired classic app's. The classic CedarLogic 4.x releases are here too, under their own names.</sub>
