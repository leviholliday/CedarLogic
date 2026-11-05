<!--
The notes for the v0.4 release page in leviholliday/CedarLogic-Releases, for
Mac, Windows and Linux (go-list step 5: scripts/release-assets.sh sets them,
without this comment). Made from docs/RELEASE-0.4-notes.md sections 1 and 2.

-->
# CedarLogic 0.4

**CedarLogic** is a free, open-source digital logic simulator. Drag in gates, wire them up and watch the signals flow, then press one key for the truth table, the Karnaugh map and the simplest formula. It began at Cedarville University, and 0.4 is the first version for **Mac, Windows and Linux** together: one app, built natively for each.

## Install

**Mac** (macOS 14 Sonoma or newer, Apple silicon)

```
brew install --cask leviholliday/tap/cedarlogic
```

Or download `CedarLogic-0.4-Mac.dmg` below and drag CedarLogic to Applications. The first time you open it, macOS may say it can't check the app: open System Settings › Privacy & Security, scroll down and click **Open Anyway**. You only do this once.

**Windows** (Windows 10 or 11, 64-bit)

```
irm https://cedarlogic.netlify.app/install.ps1 | iex
```

Or with Scoop:

```
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

(winget is on its way: `winget install --id LeviHolliday.CedarLogic -e` works once CedarLogic is listed there.) Or download the installer below: `CedarLogic-0.4-Windows-Setup-x64.exe` for most PCs, `-ARM64.exe` for Windows on ARM. If Windows says "Windows protected your PC", click **More info**, then **Run anyway**. The `.zip` files run without installing. No administrator is needed.

**Linux** (Ubuntu 22.04, Debian 12, Raspberry Pi OS Bookworm 64-bit on a Pi 4 or 5, Fedora 36, or newer)

```
curl -fsSL https://cedarlogic.netlify.app/install.sh | sh
```

On Ubuntu, Debian, Mint, Zorin and Raspberry Pi OS it installs the .deb with apt; on Fedora and the rest it puts the AppImage in your home folder. Or download `cedarlogic_0.4_amd64.deb` (PCs) or `cedarlogic_0.4_arm64.deb` (Raspberry Pi), or an AppImage (`x86_64` or `aarch64`) below.

Every installer checks CedarLogic's signature before it installs anything, and CedarLogic keeps itself up to date after that. `SHA256SUMS` and its `.sig` below list every file.

## What's new

### On every computer

- **An intro.** Every time CedarLogic opens, a short intro draws the circuit and lights it up (about three and a half seconds, silent). The first launch and the first after an update play a longer one with a chime. Any key or click skips it. In Settings › General, "When CedarLogic opens" picks Short intro, Full intro, Classic launch screen or None, and Help › Play the Intro plays it again.
- **The Actions panel.** The commands you use most, as buttons down the window's right edge, on for everyone. Drag them into your own order, or search every menu command in Customize. One switch in Settings › Actions turns it off, and "Open on hover" slides it out when the pointer reaches the edge (Control-Command-S on a Mac, Ctrl+B on Windows, F9 on Linux).
- **Show Me Around.** A short guided walk around the window that starts once by itself, and any time from Help › Show Me Around. Help › Learn Logic Online opens the lessons on the website.
- **A small favour.** CedarLogic asks once whether you'd like to send anonymous crash reports and usage stats. Both are off unless you say yes, nothing identifies you, and Settings › Privacy shows exactly what would be sent and lets you delete it.
- **Recently Deleted.** A circuit you delete from Your Circuits waits 30 days in Recently Deleted, where you can restore it.
- **Clear Drawing asks first**, and Undo brings it back.
- **Steadier wires and clicks.** Clicking between close pins or dragging a wire at a corner picks the same thing every time, and Connect Nearby no longer stalls on big pages.
- **Circuits save the numbers you see**: save, reopen and save again gives the same file.

### Mac

- The menus and shortcuts work the moment the app opens, and the welcome and What's New no longer reopen over your circuit.
- Open the Practice Circuit from the File or Help menu.
- Your sync code and class keys live in the Keychain, and crash reports carry less about you.

### Windows and Linux

- **A real menu bar**, in the same order as the Mac's.
- **Record your window for Send Feedback**: up to a minute of just the CedarLogic window.
- **Predict, Then Reveal**: in Simulation View, guess each light and display, then reveal.
- **Export as Image** can match the window, so a dark-mode circuit exports dark.
- Opening a circuit replaces the one you have open, as on the Mac (you can change it in Settings), and Ctrl+Tab shows a panel of your tabs.
- Settings › General › Testing group: stay on finished versions (Normal) or get test builds too (Beta tester).

## Fixed

- **A long round of checking of the shared engine** found 40 problems with damaged files, damaged pastes and very long editing sessions, and all are fixed. Among them: pasting a two-line label no longer quits the app, a damaged file or paste can't crash or freeze it, a wire no longer falls into two pieces after a drag, and a file can no longer make a memory part touch other files on your computer.
- **Mac:** changes after 500 undo steps are saved again, a gate's Settings changes are saved, Undo and Redo give wires back the bends you drew, and Cmd-W with a sheet open closes just the sheet.
- **Windows:** Share Link no longer carries your private Notes, and dragging a part is smoother.
- **Linux:** C connects a moving part wherever the keyboard focus is.

## Questions, bugs, ideas

Use **Help › Send Feedback** in the app, or write to **cedarlogic@proton.me**. More at [cedarlogic.netlify.app](https://cedarlogic.netlify.app).
