# Easy installs (0.4, phase 5)

One command per platform, all installing the **versioned** release in
`leviholliday/CedarLogic-Releases` (never the rolling `*-native-testing`
builds), and all checking what they download.

| Platform | Command | Files here |
|---|---|---|
| Mac | `brew install --cask leviholliday/cedarlogic/cedarlogic` | `homebrew/` (cask, `bump.sh`, README) |
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

1. **Homebrew tap repo.** Create `leviholliday/homebrew-cedarlogic` (public)
   with `Casks/cedarlogic.rb` from `homebrew/Casks/`, or put the cask in
   the tap you already have, `leviholliday/homebrew-tap` (then the command
   is `brew install --cask leviholliday/tap/cedarlogic`). Decide before the
   site goes up; `homebrew/README.md` has both.
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
   packaging/sign-release.sh v0.4            # check
   packaging/sign-release.sh v0.4 --upload   # SHA256SUMS + .sig onto v0.4
   ```
   Any file added to v0.4 later (a fixed DMG, say) means running this again,
   or the installers refuse that file.
3. **Try the one-liners against the real release** before the site shows them:
   - Linux (Zorin laptop, the Pi): `sh linux/install.sh --dry-run`,
     then the real `curl -fsSL https://cedarlogic.netlify.app/install.sh | sh`
     once the site has it (or `sh linux/install.sh` before).
   - Windows: `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -SelfTest`,
     then `-DryRun`, then a real install (`windows/README.md`).
4. **Homebrew:** `packaging/homebrew/bump.sh 0.4 --cask <tap checkout>/Casks/cedarlogic.rb`,
   `brew style` / `brew audit`, commit, push the tap. Commit the bumped copy here too.
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
> `brew install --cask leviholliday/cedarlogic/cedarlogic`
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
- **Where installed apps update from.** The Windows and Linux apps' updaters
  follow `windows-native-testing` / `linux-native-testing` (by design: those
  are what `sign-preview.sh` signs). So someone who installs 0.4 with winget
  or the one-liner gets later signed test builds through the app. winget is
  fine with that: it just sees a newer installed version.
- **Classrooms and GitHub's API limit** (60 requests an hour per network,
  without a token): `install.sh` falls back to the release page and the
  signed list; `install.ps1` says to wait or use the download page.
  Homebrew and winget don't use that API.
- **Untested here:** anything that needs Windows or Linux to run (winget,
  PowerShell, apt, the AppImage placement). No `pwsh` on this Mac, and Docker
  isn't running. Step 3 above is that test.
