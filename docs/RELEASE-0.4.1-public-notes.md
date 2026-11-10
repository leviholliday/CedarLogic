<!--
The notes for the v0.4.1 release page in leviholliday/CedarLogic-Releases, for
Mac, Windows and Linux (go-list step 5: scripts/release-assets.sh sets them,
without this comment), and for the Mac's update window.

-->
# CedarLogic 0.4.1

A data-safety and privacy update for **Mac, Windows and Linux**. If you have 0.4, CedarLogic will offer it by itself (or use Check for Updates).

## Fixed

- **Mac: a circuit opened from a share link is kept.** It now joins Your Circuits. In 0.4 it was saved in a temporary folder that macOS empties after a few days. If you opened a link in 0.4 and want to keep that circuit, open the link again.
- **A new circuit with only notes or a drawing is saved.** In 0.4 a circuit wasn't kept until it had a part on it.
- **Mac: Version History's Restore brings back the right circuit**, even when another circuit's window is in front.
- **Linux:** sync no longer changes or closes a circuit while a dialog is open over it.
- A file with a part type this version doesn't have no longer adds a blank category to the parts list.

## Privacy and security

- **Mac: crash reports send nothing unless CedarLogic crashes.** If you turned crash reports on, 0.4's crash reporter was also set to count each time the app opened. It never sent circuits, names or files, but it was more than Settings › Privacy said. It's off now. Windows and Linux never had it on.
- The one-line installs now refuse a release that has no signed list of its files.

## Install

**Mac** (macOS 14 Sonoma or newer, Apple silicon)

```
brew install --cask leviholliday/tap/cedarlogic
```

Or download `CedarLogic-0.4.1-Mac.dmg` below and drag CedarLogic to Applications. The first time you open it, macOS may say it can't check the app: open System Settings › Privacy & Security, scroll down and click **Open Anyway**. You only do this once.

**Windows** (Windows 10 or 11, 64-bit)

```
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

Or with Scoop:

```
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

Or download the installer below: `CedarLogic-0.4.1-Windows-Setup-x64.exe` for most PCs, `-ARM64.exe` for Windows on ARM. If Windows says "Windows protected your PC", click **More info**, then **Run anyway**. The `.zip` files run without installing. No administrator is needed.

**Linux** (Ubuntu 22.04, Debian 12, Raspberry Pi OS Bookworm 64-bit on a Pi 4 or 5, Fedora 36, or newer)

```
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

On Ubuntu, Debian, Mint, Zorin and Raspberry Pi OS it installs the .deb with apt; on Fedora and the rest it puts the AppImage in your home folder. Or download `cedarlogic_0.4.1_amd64.deb` (PCs) or `cedarlogic_0.4.1_arm64.deb` (Raspberry Pi), or an AppImage (`x86_64` or `aarch64`) below.

Every installer checks CedarLogic's signature before it installs anything. `SHA256SUMS` and its `.sig` below list every file.

## Questions, bugs, ideas

Use **Help › Send Feedback** in the app, or write to **cedarlogic@proton.me**. More at [cedarlogic.netlify.app](https://cedarlogic.netlify.app). Everything new in 0.4 is in [its notes](https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.4).
