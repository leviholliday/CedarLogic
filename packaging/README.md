# Easy installs (0.4, phase 5)

One command per platform, all installing the **versioned** release in
`leviholliday/CedarLogic-Releases` (never the rolling `*-native-testing`
builds), and all checking what they download.

| Platform | Command | Files here |
|---|---|---|
| Mac | `brew install --cask leviholliday/tap/cedarlogic` | `homebrew/` (cask, `bump.sh`, README) |
| Windows | `winget install --id LeviHolliday.CedarLogic -e` | `winget/` (manifests, `bump.sh`, `validate.py`, README) |
| Windows | `irm https://cedarlogic.netlify.app/install.ps1 \| iex` | `windows/install.ps1` (+ README) |
| Linux | `curl -fsSL https://cedarlogic.netlify.app/install.sh \| sh` | the repo's `linux/install.sh`: one script for releases and the test build (`--preview`); `packaging/linux/` has a symlink to it and the README |
| all | signed `SHA256SUMS` for a release | `sign-release.sh` |

Each folder's README has the details and what was tested. Nothing here
publishes anything by itself. The scripts that upload (`sign-release.sh
--upload`) only do it when asked.

## What the release must contain

The installers find files by name in the release `v<version>`:

| File | Who uses it | From |
|---|---|---|
| `CedarLogic-<v>-Mac.dmg` | Homebrew cask | `mac/package.sh` (RELEASE-0.4.md step 1) |
| `CedarLogic-<v>-Windows-Setup-x64.exe`, `-ARM64.exe` | winget, `install.ps1` | renamed from `windows-native-testing` (step 2) |
| `cedarlogic_<v>_amd64.deb`, `_arm64.deb` | `install.sh` (apt systems) | renamed from `linux-native-testing` (step 3) |
| `CedarLogic-Native-<v>-x86_64.AppImage`, `-aarch64.AppImage` | `install.sh` (Fedora and the rest) | **not in the runbook yet**: add it (below) |
| `SHA256SUMS`, `SHA256SUMS.sig` | `install.sh`, `install.ps1` | `packaging/sign-release.sh v<v> --upload`, **last** |

The release must be a full release (not a pre-release): every installer and
the cask's livecheck skip pre-releases, which is what keeps the Mac betas
out.

## One-time setup (the owner)

1. **Homebrew tap repo.** Done: the cask is in your tap,
   `leviholliday/homebrew-tap` (`Casks/cedarlogic.rb`, next to `glideball`),
   so the command is `brew install --cask leviholliday/tap/cedarlogic`.
   Keep a checkout of it for bumps (`git clone
   https://github.com/leviholliday/homebrew-tap ~/src/homebrew-tap`);
   `homebrew/README.md` has the details.
2. **winget-pkgs fork.** `gh repo fork microsoft/winget-pkgs --clone=false`
   (makes `leviholliday/winget-pkgs`). On the first PR, accept Microsoft's
   CLA when the bot asks (`@microsoft-github-policy-service agree`).
3. **The site** serves the two scripts (copies of `packaging/windows/install.ps1`
   and the repo's `linux/install.sh` in `public/`), as plain text. In the site's
   `netlify.toml`:
   ```toml
   [[headers]]
     for = "/install.sh"
     [headers.values]
       Content-Type = "text/plain; charset=utf-8"
       Cache-Control = "public, max-age=300"
   [[headers]]
     for = "/install.ps1"
     [headers.values]
       Content-Type = "text/plain; charset=utf-8"
       Cache-Control = "public, max-age=300"
   ```
   Make sure no `_redirects` rule or the 404 page catches them (`curl -sI
   https://cedarlogic.netlify.app/install.sh` must say `200` and
   `text/plain`). Fallback URLs, once `pkg/installers` is merged to `main`:
   `https://raw.githubusercontent.com/leviholliday/CedarLogic/main/linux/install.sh` and
   `.../main/packaging/windows/install.ps1`. `linux/install.sh` is the same file
   `sign-preview.sh` uploads to `linux-native-testing`. Merging this branch into
   `linux/native` conflicts on it once: take this branch's (`packaging/linux/README.md`).

No other repos, secrets or CI changes are needed.

## At release time, in order

After docs/RELEASE-0.4.md steps 1 to 3 (Mac, Windows, Linux files on `v0.4`):

1. **Add the AppImages** to v0.4 (the runbook copies only the .debs):
   ```bash
   mkdir a && cd a
   gh release download linux-native-testing -R leviholliday/CedarLogic -p 'CedarLogic-Native-*.AppImage'
   ls    # CedarLogic-Native-0.4.0-x86_64.AppImage  CedarLogic-Native-0.4.0-aarch64.AppImage (already versioned)
   gh release upload v0.4 -R leviholliday/CedarLogic-Releases CedarLogic-Native-0.4.0-*.AppImage
   ```
2. **Sign the release, last** (Keychain key; shows what it signs):
   ```bash
   packaging/sign-release.sh v0.4 --check-only   # every check, and the build it finds
   packaging/sign-release.sh v0.4            # sign (Keychain), upload nothing
   packaging/sign-release.sh v0.4 --upload   # SHA256SUMS + .sig onto v0.4
   ```
   Sign before linux/native publishes another test build: the build number
   is found from the test build's own signed list (below), or pass
   `--build N`. Any file added to v0.4 later (a fixed DMG, say) means
   running this again, or the installers refuse that file.
3. **Try the one-liners against the real release** before the site shows them:
   - Linux (Zorin laptop, the Pi): `sh linux/install.sh --dry-run`,
     then the real `curl -fsSL https://cedarlogic.netlify.app/install.sh | sh`
     once the site has it (or `sh linux/install.sh` before).
   - Windows: `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -SelfTest`,
     then `-DryRun`, then a real install (`windows/README.md`).
4. **Homebrew:** `packaging/homebrew/bump.sh 0.4 --tap ~/src/homebrew-tap`
   (updates the copy here and the tap's), `brew style` / `brew audit
   leviholliday/tap/cedarlogic`, commit, push the tap. Commit the bumped copy here too.
5. **winget:** `packaging/winget/bump.sh 0.4`, test on Windows
   (`winget validate`, `winget install --manifest`), then the PR
   (`winget/README.md`). Merging takes from hours to a few days.
6. **Website:** copy the two scripts into `public/`, add the headers, the
   download-page wording below (winget line hidden until step 5's PR is
   merged), deploy with the owner there (production rule).
7. After the winget PR merges: `winget install --id LeviHolliday.CedarLogic -e`
   on a clean PC, then show the winget line on the site.

For later releases (0.4.1...), the same steps, with the new version in each
command. Homebrew and winget bumps can lag a day: installed apps update
themselves.

## Download-page wording

Pages: `/download/mac/`, `/download/windows/`, `/download/linux/` (built by the
site's `scripts/build_pages.py`). Each command goes in a copy-able `.cmd` box,
as the Linux page does today. Each box gets a "What does this do?" link to
the script's source on GitHub (`packaging/...` on `main`).

### Mac

> **Install with Homebrew** (recommended if you use Homebrew)
>
> `brew install --cask leviholliday/tap/cedarlogic`
>
> Homebrew downloads CedarLogic, checks it and puts it in Applications.
> CedarLogic then keeps itself up to date.
>
> **Or download the app**
>
> [Download CedarLogic for Mac (DMG)] Open it and drag CedarLogic to
> Applications.
>
> *The first time you open it* (either way), macOS may say it can't check
> the app: open **System Settings › Privacy & Security**, scroll down and
> click **Open Anyway**. You only do this once.
>
> Needs macOS 14 Sonoma or newer on a Mac with Apple silicon (M1 or newer).

(The DMG and the cask are Apple silicon only: `mac/build.sh` builds for the
Mac it runs on. Intel Macs get "This app isn't supported". Say so until a
universal build exists.)

### Windows

> **Install with winget** (built into Windows 11 and recent Windows 10)
>
> `winget install --id LeviHolliday.CedarLogic -e`
>
> **Or paste this into PowerShell**
>
> `irm https://cedarlogic.netlify.app/install.ps1 | iex`
>
> It downloads the right installer for your PC (Intel/AMD or ARM), checks
> its signature, installs CedarLogic for you (no administrator needed) and
> opens it.
>
> **Or download the installer**
>
> [CedarLogic for Windows (x64)] for most PCs and laptops ·
> [ARM64] for Windows on ARM (Snapdragon) laptops
>
> If Windows shows "Windows protected your PC", click **More info** ›
> **Run anyway**. (The commands above don't show it.)
>
> Installs in the Start menu; .cdl files open in it; remove it from
> Settings › Apps. It keeps itself up to date. Windows 10 or 11, 64-bit.

### Linux

> **Paste this into a terminal** (recommended)
>
> `curl -fsSL https://cedarlogic.netlify.app/install.sh | sh`
>
> On Ubuntu, Debian, Zorin, Mint and Raspberry Pi OS it installs CedarLogic
> with apt (it asks for your password). On Fedora and other systems it puts
> the AppImage in your home folder. Either way CedarLogic appears in the
> applications menu and keeps itself up to date. It checks CedarLogic's
> signature before installing anything.
>
> Needs a 64-bit system: Ubuntu 22.04, Debian 12 (Raspberry Pi OS Bookworm,
> 64-bit, on a Pi 4 or 5), Fedora 36 or newer.
>
> **Other downloads**
>
> - **.deb** (Ubuntu, Debian, Raspberry Pi OS): [amd64] for PCs ·
>   [arm64] for a Raspberry Pi 4/5 or ARM laptop. Double-click it, or
>   `sudo apt install ./cedarlogic_0.4_amd64.deb`.
> - **AppImage** (any distribution, nothing to install): [x86_64] · [aarch64].
>   Make it executable (`chmod +x`, or Properties › Allow executing) and run
>   it; it adds itself to the applications menu. If it won't open, install
>   FUSE 2 (Fedora: `sudo dnf install fuse fuse-libs`; Arch: `sudo pacman -S
>   fuse2`; Ubuntu 24.04: `sudo apt install libfuse2t64`).

The `/get/` redirects behind those links point at `*-native-testing` today
(`public/_redirects`). For 0.4 they should point at the v0.4 files. The
site's `rel/site-0.4` branch and its `releases.json` `"channel"` switch
handle the Mac, Windows and .deb links; check they do, and add AppImage
ones (`/get/linux-x86_64-appimage`, `/get/linux-aarch64-appimage`).

## Things to know (decisions left to the owner)

- **Not notarized / not code-signed.** Homebrew quarantines the app like a
  browser download, so the first launch needs Open Anyway (the cask's
  caveats say so). The cask could strip the quarantine in a `postflight`,
  but that switches off a macOS check on users' Macs, so it doesn't
  (`homebrew/README.md`). On Windows, SmartScreen warns about the browser
  download only; winget and `install.ps1` don't trigger it.
- **Apple silicon only** on Mac (see above).
- **Publisher.** The Windows installer says `Cedarville University`
  (`AppPublisher` in `CedarLogic.iss`), so the winget manifest does too; the
  package id is `LeviHolliday.CedarLogic`. Change both together if you want
  your name there.
- **Where installed apps update from.** The Linux app has the Mac's
  testing groups (Settings > General > Testing group; below): a Normal
  tester's updater follows the versioned releases here, a Beta tester's
  `linux-native-testing`. The Windows app's updater still follows
  `windows-native-testing` only, so someone who installs 0.4 with winget
  or `install.ps1` gets later signed test builds through the app. winget is
  fine with that: it just sees a newer installed version.
- **`/CHANNEL=normal` on the Windows setup.** The setup writes
  `{app}\install-channel` (the testing group a first run starts in) from
  `/CHANNEL=`, else from its own file name (`windows/installer/CedarLogic.iss`
  on `ui/windows-channel`). winget caches the installer under a name of its
  own and a download can be renamed, so the winget manifest passes
  `/CHANNEL=normal` (`InstallerSwitches: Custom`) for each installer, and
  `install.ps1` passes `/CHANNEL=normal` for a versioned release (`beta`
  only when `-Tag` picks the test build, whose setup name has no version).
- **Classrooms and GitHub's API limit** (60 requests an hour per network,
  without a token): `install.sh` falls back to the release page and the
  signed list; `install.ps1` says to wait or use the download page.
  Homebrew and winget don't use that API.
- **Untested here:** anything that needs Windows or Linux to run (winget,
  PowerShell, apt, the AppImage placement). No `pwsh` on this Mac, and Docker
  isn't running. Step 3 above is that test.

## The Linux app's testing groups, and what sign-release.sh must add

Since `ui/linux-channel`, the Linux app has the Mac's **Testing group** row
in Settings > General (Normal tester / Beta tester, the Mac's wording and
hint, with Check Now). Its updater (`linux/App/Updater.h`) then looks in one
of two places:

| Group | Follows | Offered when |
|---|---|---|
| **Beta tester** | `linux-native-testing` in `leviholliday/CedarLogic`, as before | its signed `SHA256SUMS` is for `tag linux-native-testing` and the tag's commit, with a build newer than the app's |
| **Normal tester** | the newest **full** release `v<version>` in `leviholliday/CedarLogic-Releases` (newest by version, among the 50 most recent releases) that has this computer's file and `SHA256SUMS` + `.sig` | the same checks with that release's tag (`tag v0.4.1`) and the commit its tag is on, a build newer than the app's, a version no older than the app's (`CL_VERSION`), and not the release this updater already installed |

Pre-releases (the Mac's betas, Classic 4.x) and drafts never count for a
Normal tester. A release with no Linux file for this format (a Mac-only fix)
is skipped, so a Normal tester gets the newest release that has one. Its
file names: `cedarlogic_<v>_<amd64|arm64>.deb` (or the test build's
`cedarlogic_<arch>.deb`), and `CedarLogic-Native-<v>-<x86_64|aarch64>.AppImage`
(or `CedarLogic-<v>-...`), the names `install.sh` takes. The .deb's root step
(`--install-update`, through pkexec) accepts either tag kind, with its own
names, and refuses a build that isn't newer than the installed one.

**Which group a copy starts in** (`linux/App/Prefs.cpp`, `startingChannel`):
the group chosen in Settings; else what `install.sh` wrote to
`~/.config/CedarLogic/install-channel` (`beta` with `--preview` or when it
fell back to the test build, `normal` for a release); else, for a copy whose
settings file is older than this change (every Linux build until now came
from `linux-native-testing`), **Beta**, saying so once (a note in the window,
or with the next update question, and under the row in Settings until then);
else, a new install, **Normal**. So existing testers keep getting test builds.
A copy installed from a downloaded .deb or AppImage with no settings yet
starts Normal; until 0.4 has Linux files, its Check Now says no finished
version is out yet and points at Beta tester.

### The build number sign-release.sh writes

The Linux updater compares the manifest's `build` with its own
`CL_BUILD_TIME`, the **committer time of the commit the Linux files were
built from**. The signing time would be wrong: a Beta tester on a test build
newer than the release's code, but older than the signing time, who switches
to Normal would be offered the release (a downgrade: both say 0.4.0), and a
copy that installed the release by hand would be offered it once more.

So `sign-release.sh` writes the files' commit time:

- **By default it finds it.** Each file's SHA-256 is looked up in the signed
  `SHA256SUMS` of `linux-native-testing` and, if it has one,
  `windows-native-testing` (each checked against the update key, and for its
  own tag). The `build` line of every list that has one of the release's
  files is the build; they must agree. It refuses, saying which list has which
  build, when they don't (Windows and Linux built from different commits),
  and when no file is in either list (the test build has moved on since the
  files were copied). Copied files keep their bytes, so `cedarlogic_amd64.deb`
  matches the release's `cedarlogic_0.4_amd64.deb`. A .deb, AppImage or .exe
  on the release that no list has is named in a note.
- **`--build N`** sets it instead: `git log -1 --format=%ct <the commit the
  Linux files were built from>`.
- **`--check-only`** does every check and finds the build, prints it, and
  stops: no key, nothing signed or written. Without `--upload` it signs (or
  with `--test-key`, signs with a throwaway key) and uploads nothing, as before.

So sign soon after copying the Linux files from `linux-native-testing`
(before linux/native publishes again), or use `--build`. The tag line stays
`tag v<version>` (the app checks it's the release's tag) and the commit line
the commit the release's tag is on in this repo (what the app reads back
from GitHub).

The released Linux build must also say the release's version
(`project(CedarLogicNative VERSION 0.4.1)` in `linux/CMakeLists.txt` for
v0.4.1): the app never offers a Normal tester a version older than its own.

Tested (2026-10-31, on the Mac): 11 cases against fake test builds signed
with a throwaway key (`CEDARLOGIC_SIGN_TEST_RELEASE`, `_PREVIEWS`,
`_PUBHEX`, which only work with `--test-key` or `--check-only`): both lists
agree (signed with that build, every file listed); `--check-only` writes
nothing; they disagree (refused, both named); `--build` wins; Windows has no
list (Linux's build, the .exe noted); no file matches (refused, asks for
`--build`); no lists; a list with a bad signature; a list for another tag; a
file whose bytes changed (not matched); bad `--build`, `--test-key --upload`,
test folders signing for real (all refused). Against GitHub, read only:
`v0.3.4 --check-only` checks both real test builds' lists with the real key
and refuses (no file of it is in them); with `--build` it passes; the real
`linux-native-testing` .deb renamed `cedarlogic_0.4_amd64.deb` gives build
1793161366, its commit's time.

The Mac and Windows files share that one manifest. The Mac doesn't read it;
the Windows app doesn't read versioned releases yet. If Windows gets the same
groups and its files come from another commit, the release needs one build
number that is right for both (or a manifest per platform), so decide that
before then.

Nothing else changes: CI's publish job and `scripts/sign-preview.sh` work as
before for the test build. CI checks all of this headlessly
(`.github/workflows/linux-native.yml`, "Testing group check":
`cedarlogic --channel-check` against a static stand-in for GitHub, and the
signed-folder check with `--tag v0.4.1`, the way `sign-release.sh` signs).
