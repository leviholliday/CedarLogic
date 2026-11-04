# Releasing CedarLogic 0.4

The runbook is one ordered list now: [`packaging/README.md`](../packaging/README.md), "Release day: the go-list". It covers the Windows and Linux builds, the Mac build and its promotion to everyone, signing, Homebrew, Scoop, winget, the Releases repo's README and the website.

The older runbook of this name (on the `rel/mac-0.4` branch, 2026-10-05) ran from the `rel/*-0.4` branches, which are merged into `main` now. Its renames are still the ones the apps and installers expect (go-list step 4):

| Test build's file (`*-native-testing`) | On `v0.4` |
|---|---|
| `CedarLogic-Windows-Setup-x64.exe`, `-ARM64.exe` | `CedarLogic-0.4-Windows-Setup-x64.exe`, `-ARM64.exe` |
| `CedarLogic-Windows-native-x64.zip`, `-ARM64.zip` | `CedarLogic-0.4-Windows-x64.zip`, `-ARM64.zip` |
| `cedarlogic_amd64.deb`, `cedarlogic_arm64.deb` | `cedarlogic_0.4_amd64.deb`, `cedarlogic_0.4_arm64.deb` |
| `CedarLogic-Native-0.4.0-x86_64.AppImage`, `-aarch64.AppImage` | the same names |

What's new for the release page and the testers: [`RELEASE-0.4-notes.md`](RELEASE-0.4-notes.md).
