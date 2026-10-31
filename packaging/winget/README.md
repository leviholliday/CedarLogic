# winget package for CedarLogic (Windows)

What people type, once the package is in the winget catalog:

```powershell
winget install --id LeviHolliday.CedarLogic -e
```

(`winget install cedarlogic` also finds it by its moniker once nothing else
claims the name.) It runs the release's Inno Setup installer silently, for
the current user only: no administrator, `%LOCALAPPDATA%\Programs\CedarLogic`,
Start menu entry, `.cdl` files and `cedarlogic://` links, an entry in
Settings > Apps. After that the app updates itself; `winget upgrade` also
works.

## What's here

```
packaging/winget/
  manifests/l/LeviHolliday/CedarLogic/0.4.0/
    LeviHolliday.CedarLogic.yaml                 version manifest
    LeviHolliday.CedarLogic.installer.yaml       the two installers (x64, arm64)
    LeviHolliday.CedarLogic.locale.en-US.yaml    name, description, license, links
  bump.sh        writes a version's manifests from its GitHub release
  validate.py    schema + winget-validate-style checks, for when winget isn't there (a Mac)
```

The folder layout is winget-pkgs' own (`manifests/<first letter of the
id, lower case>/<Publisher>/<Name>/<version>/`), so a version folder is copied
into the fork as it is.

Schema 1.12.0 (what winget-pkgs' own manifests use today; anything 1.6.0 or
newer is accepted).

**The committed 0.4.0 has placeholder hashes** (64 zeros) and the v0.4 URLs,
because v0.4 doesn't have its Windows installers yet. `bump.sh 0.4` fills
them in from the real release. `validate.py` refuses zero hashes unless it's
given `--placeholders-ok`.

### Why the manifest says what it says (from `windows/installer/CedarLogic.iss`)

| Field | Value | From |
|---|---|---|
| `InstallerType` | `inno` | Inno Setup 6 (CI pins 6.7.1). winget passes Inno's own silent switches (`/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`), the ones CI's installer test uses |
| `Scope` | `user` | `PrivilegesRequired=lowest`, `{localappdata}\Programs\CedarLogic`, HKCU registry |
| `ProductCode` | `{59AB891F-9536-471C-BAE2-13C34308A7C3}_is1` | `AppId` + Inno's `_is1`: the Settings > Apps key, so winget sees an installed copy, from any source, as this package |
| `AppsAndFeaturesEntries` | DisplayName `CedarLogic`, Publisher `Cedarville University`, DisplayVersion = package version | `UninstallDisplayName`, `AppPublisher`, `AppVersion` (CMake's `project(... VERSION 0.4.0)`) |
| `Publisher` | `Cedarville University` | `AppPublisher`. winget matches the installed app's publisher against it. If you'd rather it say your name, change `AppPublisher` in the .iss too, or they disagree |
| `Architecture` | `x64`, `arm64` | `CedarLogic-<v>-Windows-Setup-x64.exe` / `-ARM64.exe` (the x64 one also runs on Windows 11 on ARM, but winget picks arm64 there) |
| `FileExtensions` / `Protocols` | `cdl` / `cedarlogic` | the `[Registry]` section |
| `MinimumOSVersion` | `10.0.0.0` | `MinVersion=10.0` |

The package version is three numbers (`0.4.0`), what the installer writes
in Settings > Apps, even though the release and its files say `0.4`. If they
differed, winget would think the installed copy is a different version and keep
offering an "upgrade".

## Releasing a version: the bump

After the release (`v0.4` in `leviholliday/CedarLogic-Releases`) has
`CedarLogic-0.4-Windows-Setup-x64.exe` and `-ARM64.exe` (docs/RELEASE-0.4.md,
step 2) and is a full release, not a pre-release:

```bash
packaging/winget/bump.sh 0.4          # -> manifests/l/LeviHolliday/CedarLogic/0.4.0/
```

It downloads both installers, checks each against GitHub's sha256 digest,
checks that each is an Inno Setup installer whose version info says `0.4.0`,
writes the three manifests (copying the newest version folder here, so
wording changes carry over) with the new version, URLs, upper-case SHA-256s
and the release's date, and runs `validate.py`. (It needs PyYAML and
jsonschema for that: `python3 -m venv ~/.venvs/winget && ~/.venvs/winget/bin/pip
install pyyaml jsonschema`, then `WINGET_VALIDATE_PYTHON=~/.venvs/winget/bin/python3`.)
Commit the new folder here.

The installer URL must be the versioned release asset, never
`windows-native-testing` (its files change on every push, so its hash
would stop matching the next day).

## The PR to microsoft/winget-pkgs

One-time setup: **fork `microsoft/winget-pkgs`** to `leviholliday/winget-pkgs`
(`gh repo fork microsoft/winget-pkgs --clone=false`). The first PR also needs
the Microsoft CLA: the bot comments on the PR, and you reply
`@microsoft-github-policy-service agree`.

Each version:

1. **Test on Windows first** (a real PC, the friend's, or a VM):
   ```powershell
   winget validate --manifest .\manifests\l\LeviHolliday\CedarLogic\0.4.0
   winget settings --enable LocalManifestFiles          # once, in an admin terminal
   winget install --manifest .\manifests\l\LeviHolliday\CedarLogic\0.4.0
   # CedarLogic is in Start; Settings > Apps shows CedarLogic 0.4.0
   winget list --id LeviHolliday.CedarLogic             # only works once it's in the catalog; before that: winget list CedarLogic
   winget uninstall --manifest .\manifests\l\LeviHolliday\CedarLogic\0.4.0
   ```
2. **Put the folder in the fork** (the repo is huge: check out only your folder):
   ```bash
   gh repo sync leviholliday/winget-pkgs -b master                 # the fork up to date
   git clone --filter=blob:none --sparse https://github.com/leviholliday/winget-pkgs ~/src/winget-pkgs
   cd ~/src/winget-pkgs
   git sparse-checkout set manifests/l/LeviHolliday
   git switch -c LeviHolliday.CedarLogic-0.4.0
   mkdir -p manifests/l/LeviHolliday/CedarLogic
   cp -R "<this repo>/packaging/winget/manifests/l/LeviHolliday/CedarLogic/0.4.0" manifests/l/LeviHolliday/CedarLogic/
   git add manifests/l/LeviHolliday/CedarLogic/0.4.0
   git commit -m "New package: LeviHolliday.CedarLogic version 0.4.0"     # later: "New version: ..."
   git push -u origin LeviHolliday.CedarLogic-0.4.0
   ```
3. **Open the PR** against `microsoft/winget-pkgs` `master`:
   ```bash
   gh pr create -R microsoft/winget-pkgs -B master -H leviholliday:LeviHolliday.CedarLogic-0.4.0 \
     --title "New package: LeviHolliday.CedarLogic version 0.4.0" --body-file - <<'EOF'
   - [x] Have you signed the [Contributor License Agreement](https://cla.opensource.microsoft.com/microsoft/winget-pkgs)?
   - [x] Is there a linked Issue? No: a new package.
   - [x] Have you checked that there aren't other open pull requests for the same manifest update/change?
   - [x] This PR only modifies one (1) manifest
   - [x] Have you validated your manifest locally with `winget validate --manifest <path>`?
   - [x] Have you tested your manifest locally with `winget install --manifest <path>`?
   - [x] Does your manifest conform to the 1.12 schema?

   CedarLogic: a digital logic simulator (GPL-3.0). Per-user Inno Setup installer, no elevation.
   EOF
   ```
   (Check the checklist against the repo's current PR template:
   `.github/PULL_REQUEST_TEMPLATE.md` in winget-pkgs. It changes now and then.)
4. **Wait for the bots.** The validation pipeline installs it in a clean VM and
   runs Defender over it. Labels to look for: `Azure-Pipeline-Passed`, then
   `Validation-Completed`; a moderator merges. A first package can take a few
   days. If a label like `Validation-Installation-Error` or
   `Error-Hash-Mismatch` appears, the bot comment links the log; fix the
   manifest in the same branch and push. A `Needs-Author-Feedback` label
   means a human asked something in the PR.
5. **After the merge**, `winget install --id LeviHolliday.CedarLogic -e` works
   within an hour or so (the catalog's index rebuilds). Only then put the
   winget line on the website.

Later versions are the same with "New version: LeviHolliday.CedarLogic
version 0.4.1". From a Mac, `komac` (`brew install komac`, a Homebrew core
formula) can do steps 2 and 3 in one go:
`komac update LeviHolliday.CedarLogic --version 0.4.1 --urls <x64 url> <arm64 url> --submit`
(it asks for a GitHub token). Its manifests are equivalent; keep this folder
in step with what was submitted.

Unsigned installers are accepted (SmartScreen only matters for downloads in
the browser; winget doesn't show it). If Defender ever flags the installer,
the PR fails validation and the fix is code signing.

## Tested (2026-10-31, on the Mac)

- `bump.sh` against the rolling test release (`--repo leviholliday/CedarLogic
  --tag windows-native-testing` and its unversioned asset names, written to a
  scratch folder): both installers matched GitHub's digests, both are Inno
  Setup and say 0.4.0, and the manifests it wrote pass `validate.py`.
- `validate.py` on the committed 0.4.0 (`--placeholders-ok`): valid against
  the 1.12.0 schemas. On a broken copy (architecture `aarch64`, scope `me`,
  versions that differ between files): all three caught.
- Not run: `winget validate` / `winget install --manifest` (no winget on a
  Mac). Do step 1 on Windows before the PR.
