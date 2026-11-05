# Scoop bucket for CedarLogic (Windows)

What people type, once the bucket exists (release day):

```powershell
scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic
scoop install cedarlogic/cedarlogic
```

(Two lines: Windows PowerShell 5.1 has no `&&`. On one line, join them with `;`.)

Scoop unzips the release's **portable zip** (`CedarLogic-<version>-Windows-<x64|ARM64>.zip`, the same files the app's own updater takes for Normal testers) into `~\scoop\apps\cedarlogic\<version>`, and adds CedarLogic to the Start menu. No installer, no administrator. `scoop update cedarlogic` updates it, and `scoop uninstall cedarlogic` removes it; Your Circuits and the settings are in `%APPDATA%\CedarLogic` and stay.

## What's here

```
packaging/scoop/
  cedarlogic.json   the manifest (the bucket's bucket/cedarlogic.json is the same file)
  bump.sh           writes a version's urls and hashes from its GitHub release
```

The committed `0.4` has **placeholder hashes** (64 zeros): `v0.4` doesn't have its Windows zips yet. `bump.sh 0.4` fills them in from the real release, after it's signed.

- `architecture`: `64bit` and `arm64`, one zip each. The zip holds a `CedarLogic\` folder (`extract_dir`).
- `shortcuts`: a Start menu entry. The zip copy asks once, on its first run, whether to open `.cdl` files and `cedarlogic://` links; that's the app's own question, not Scoop's.
- `checkver`: the newest full release of `leviholliday/CedarLogic-Releases` (GitHub's "latest" skips the Mac betas, which are pre-releases).
- `autoupdate`: the two zip urls for `$version`, and each hash read from the release's signed `SHA256SUMS` (`file <name> <size> <sha256>`).

## Release day: making the bucket

The bucket is its own public repo, made on release day (not before, so nothing points at an empty bucket):

```bash
gh repo create leviholliday/scoop-cedarlogic --public \
  --description "Scoop bucket for CedarLogic, the free logic gate simulator" \
  --homepage https://cedarlogic.netlify.app/
git clone https://github.com/leviholliday/scoop-cedarlogic ~/src/scoop-cedarlogic
packaging/scoop/bump.sh 0.4 --bucket ~/src/scoop-cedarlogic     # after packaging/sign-release.sh --upload
cd ~/src/scoop-cedarlogic
printf '# CedarLogic for Scoop\n\n    scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic\n    scoop install cedarlogic/cedarlogic\n\nThe app: https://cedarlogic.netlify.app\n' > README.md
git add -A && git commit -m "CedarLogic 0.4" && git push
```

Commit the bumped `packaging/scoop/cedarlogic.json` here too. Then, on a Windows PC: the two lines at the top, CedarLogic opens from Start, and `scoop uninstall cedarlogic`.

Later versions: `packaging/scoop/bump.sh <version> --bucket ~/src/scoop-cedarlogic`, commit, push. A day's lag breaks nothing: the app updates itself.

## Known limits

- **Two updaters.** CedarLogic updates itself in place (`CedarLogic.exe` and `res\` in Scoop's folder), and Scoop then still thinks the old version is installed until `scoop update`. Nothing breaks, but `scoop list` can show an older number than the app's About box. A Scoop-installed copy that left updating to Scoop would need a change in the app.
- **Mac-only releases.** If the newest full release has no Windows zips (a Mac-only fix), `checkver` still sees its version and `autoupdate` fails on the missing zip. From 0.4 on, every release is meant to carry all three platforms' files; if one doesn't, skip the bucket bump for it.
- **Not tested on Windows yet.** `bump.sh` was tried on a Mac against local copies of the test build's zip. Neither the manifest nor `scoop install` has been run on a PC.
