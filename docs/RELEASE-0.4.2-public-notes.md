<!--
The notes for the v0.4.2 release page in leviholliday/CedarLogic-Releases, for
Mac, Windows and Linux (go-list step 5: scripts/release-assets.sh sets them,
without this comment), and for the Mac's update window.

-->
# CedarLogic 0.4.2

A small update for **Mac, Windows and Linux**. If you have 0.4 or 0.4.1, CedarLogic will offer it by itself (or use Check for Updates).

## New

- **Messages from CedarLogic, once in a while.** When the daily update check finds nothing new, CedarLogic also reads one small file next to the updates and, on the day a message is due, shows it once, with **Remind Me Later** and **No Thanks**. Nothing about you is sent, messages only link to cedarlogic.netlify.app, and with automatic update checks turned off there are none. Nothing shows up today: the first one is planned for next year.

## Install

**Mac** (macOS 14 Sonoma or newer, Apple silicon)

```
brew install --cask leviholliday/tap/cedarlogic
```

Or download `CedarLogic-0.4.2-Mac.dmg` below and drag CedarLogic to Applications. The first time you open it, macOS may say it can't check the app: open System Settings › Privacy & Security, scroll down and click **Open Anyway**. You only do this once.

**Windows** (Windows 10 or 11, 64-bit)

```
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

Or with Scoop:

```
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

Or download the installer below: `CedarLogic-0.4.2-Windows-Setup-x64.exe` for most PCs, `-ARM64.exe` for Windows on ARM. If Windows says "Windows protected your PC", click **More info**, then **Run anyway**. The `.zip` files run without installing. No administrator is needed.

**Linux** (Ubuntu 22.04, Debian 12, Raspberry Pi OS Bookworm 64-bit on a Pi 4 or 5, Fedora 36, or newer)

```
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

On Ubuntu, Debian, Mint, Zorin and Raspberry Pi OS it installs the .deb with apt; on Fedora and the rest it puts the AppImage in your home folder. Or download `cedarlogic_0.4.2_amd64.deb` (PCs) or `cedarlogic_0.4.2_arm64.deb` (Raspberry Pi), or an AppImage (`x86_64` or `aarch64`) below.

Every installer checks CedarLogic's signature before it installs anything. `SHA256SUMS` and its `.sig` below list every file.

## Questions, bugs, ideas

Use **Help › Send Feedback** in the app, or write to **cedarlogic@proton.me**. More at [cedarlogic.netlify.app](https://cedarlogic.netlify.app). The fixes in 0.4.1 are in [its notes](https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.4.1), and everything new in 0.4 in [its own](https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.4).
