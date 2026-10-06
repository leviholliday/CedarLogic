# CedarLogic 0.4

## Download
- **Mac:** `CedarLogic-0.4-Mac.dmg`. Open it and drag CedarLogic into Applications. macOS 14 Sonoma or later, Apple silicon. The first time you open it, macOS may say it can't check it: open **System Settings › Privacy & Security** and click **Open Anyway**.
- **Windows:** `CedarLogic-0.4-Windows-Setup-x64.exe` for most PCs, `CedarLogic-0.4-Windows-Setup-ARM64.exe` for Windows on ARM (Snapdragon). It installs just for you, no administrator needed. If SmartScreen says it doesn't recognise the app, choose **More info**, then **Run anyway**.
- **Raspberry Pi and Linux:** `cedarlogic_0.4_arm64.deb` (Raspberry Pi 4 and 5, 64-bit Raspberry Pi OS) or `cedarlogic_0.4_amd64.deb` (PCs with Debian or Ubuntu): `sudo apt install ./cedarlogic_0.4_arm64.deb`. Or install with one line: `curl -fsSL https://github.com/leviholliday/CedarLogic/releases/download/linux-native-testing/install.sh | bash`

## What's new
- **Sync your circuits across your devices.** Settings › Sync › Turn On Sync gives you a sync code (and a QR code). There's no account: circuits are encrypted on your device first, so the website keeps them but can't read them. It works between Macs, Windows PCs, Raspberry Pis, CedarLogic Online and the phone app. If a circuit is changed in two places, both are kept: the other change goes to Version History. A circuit deleted on one device goes to Recently Deleted on the others.
- **Add a device by scanning, no typing.** On the new device, Settings › Sync › I Have a Code shows a QR code. On a phone that syncs, open Your Circuits › Sync › Add a Device and scan it, then press Link on the new device. Typing or pasting the code still works.
- **Check My Circuit (Shift-T).** Type the formula or minterms you were given, or paste the truth table. CedarLogic matches switches and lights by name and shows each row your circuit gets wrong, with what was expected and what it gave.
- **Export Lab Report (File menu).** One PDF to hand in: your name, the circuit, and the truth table, K-maps, simplest formulas and timing diagram you tick, in color or black and white.
- **Share Link (File menu).** Copies a link that opens your circuit in CedarLogic Online in any browser, or in the app. The circuit is in the link itself; nothing is uploaded.
- **Tidy Up, rebuilt** (Full Rearrange on the Mac, Tidy Up by Signal Flow on Windows and Linux). Cleaner layouts: groups you drew stay together, labels stay in order and straight wires stay straight. A page of hundreds of parts takes a second or two.
- **Fixes:** truth tables for circuits with TO/FROM labels; lab report headings stay with their content; with NOR only, Build from a Formula builds the right circuit; after a reset synced copy every device is listed again; Windows toolbar clicks land every time.
