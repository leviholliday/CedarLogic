# Easy installs, and the 0.4 go-list

One command per platform, all installing the **versioned** release in
`leviholliday/CedarLogic-Releases` (never the rolling `*-native-testing`
builds), and all checking what they download.

| Platform | Command | Works | Files here |
|---|---|---|---|
| Mac | `brew install --cask leviholliday/tap/cedarlogic` | now (0.3.4; 0.4 after step 9) | `homebrew/` (cask, `bump.sh`, README) |
| Windows | `irm https://cedarlogic.netlify.app/install.ps1 \| iex` | from release day | `windows/install.ps1` (+ README) |
| Windows | `winget install --id LeviHolliday.CedarLogic -e` | after release day, once Microsoft merges the PR | `winget/` (manifests, `bump.sh`, `validate.py`, README) |
| Windows | `scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic` then `scoop install cedarlogic/cedarlogic` | from release day (step 10 makes the bucket) | `scoop/` (manifest, `bump.sh`, README) |
| Linux | `curl -fsSL https://cedarlogic.netlify.app/install.sh \| sh` | from release day (before, it installs the test build) | the repo's `linux/install.sh`: one script for releases and the test build (`--preview`); `packaging/linux/` has a symlink to it and the README |
| all | signed `SHA256SUMS` for a release | | `sign-release.sh` |

Linux has no package manager of its own here: an apt repository, Flathub or
the AUR each need an outside account or a review. The one-liner, the .deb
and the AppImage cover it, and the app updates itself.

Each folder's README has the details and what was tested. Nothing here
publishes anything by itself. The scripts that upload (`sign-release.sh
--upload`) only do it when asked.

## What the release must contain

On release day `v0.4` is a **full release** for everyone on all three
platforms (not a pre-release, no "beta" or "test build" anywhere). The
installers and updaters find files by name in it:

| File | Who uses it | From (go-list step) |
|---|---|---|
| `CedarLogic-<v>-Mac.dmg` | the download page, the Homebrew cask | `mac/package.sh` (2, 3) |
| `CedarLogic-Native-<v>-mac.zip` | the Mac app's updater (Sparkle) | `scripts/publish-update.sh` (3) |
| `CedarLogic-<v>-Windows-Setup-x64.exe`, `-ARM64.exe` | the download page, `install.ps1`, winget | renamed from `windows-native-testing` (4) |
| `CedarLogic-<v>-Windows-x64.zip`, `-ARM64.zip` | **the Windows app's updater for Normal testers** (`windows/App/Updater.cpp`, `normalAsset`), Scoop | renamed from `windows-native-testing` (4) |
| `cedarlogic_<v>_amd64.deb`, `_arm64.deb` | the download page, `install.sh` (apt systems), the Linux updater | renamed from `linux-native-testing` (4) |
| `CedarLogic-Native-<v>.0-x86_64.AppImage`, `-aarch64.AppImage` | the download page, `install.sh` (Fedora and the rest), the Linux updater | from `linux-native-testing`, already versioned (4) |
| `SHA256SUMS`, `SHA256SUMS.sig` | every installer and updater on Windows and Linux, Scoop's autoupdate | `packaging/sign-release.sh v<v> --upload`, **last** (6) |

The Windows zips were missing from this list before: without them a
Windows app on Normal never sees 0.4 or anything after it.

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
3. **The site** serves the two scripts (done: they're in its `public/`, with these headers) (copies of `packaging/windows/install.ps1`
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

4. **Scoop bucket.** Made on release day, not before (go-list step 10,
   `scoop/README.md`): a public repo `leviholliday/scoop-cedarlogic`.

No other repos, secrets or CI changes are needed.

## Release day: the go-list

One list, in order. Each step says what it runs and what to check before
the next. Steps 1 to 7 make the release; 8 to 14 put it everywhere; 15 is
checking it all; 16 comes days later. The website's own steps (14) are
spelled out in the site repo's README, "Release day", under the same
numbers.

**0. Ready.**
- Everything for 0.4 is merged into `main` on `private` and CI is green on
  it, including the Actions panel change ("Show the Actions panel", "Open
  on hover", the setup step, Help > Show Me Around).
- `gh release view v0.4 -R leviholliday/CedarLogic-Releases`: today it's a
  pre-release holding the Mac's 0.4 beta 1 (build 1077, from 2026-11-01,
  before the intro and the Actions rail). It stays the release; the steps
  add to it.
- On the Mac: the update key and Sparkle's key in the Keychain (account
  `cedarlogic`), a dmgbuild venv, `~/src/homebrew-tap`. A Windows PC, the
  Zorin laptop and the Pi on hand for step 15.

**1. Windows and Linux builds, from `main`.** CI publishes only from the
`windows/native` and `linux/native` branch names (their workflows' publish
jobs), so:
```bash
git push private main:windows/native
git push private main:linux/native
gh run list -R leviholliday/CedarLogic -w "Windows native build" -L 1   # then: gh run watch <id> -R leviholliday/CedarLogic --exit-status
gh run list -R leviholliday/CedarLogic -w "Linux native build" -L 1     # the same
scripts/sign-preview.sh windows
scripts/sign-preview.sh linux
```
`sign-preview.sh` checks the test release, the branch and the green run are
on one commit, then signs it. Beta testers get this build, and step 6 finds
its build number from these signed lists. Write down the commit:
`C=$(git rev-parse private/linux/native)`.

**2. The Mac build, from the same `main`.** In a clean checkout (nothing
uncommitted under `mac/`, or the build number gets a "+"):
```bash
CLEAN=1 mac/build.sh
DMGBUILD=<venv>/bin/dmgbuild mac/package.sh        # -> mac/build/CedarLogic-0.4-Mac.dmg
/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" -c "Print :CFBundleVersion" mac/build/CedarLogic.app/Contents/Info.plist
```
It must say `0.4` and a build above 1077.

**3. The Mac build onto v0.4, to Beta testers first.** From a worktree under
`.claude/worktrees/` (so `publish-update.sh` finds Sparkle's `sign_update`):
```bash
bash scripts/publish-update.sh beta 0.4 --native "<abs>/mac/build/CedarLogic.app" --no-ci --notes <mac-notes.md> --dry-run
bash scripts/publish-update.sh beta 0.4 --native "<abs>/mac/build/CedarLogic.app" --no-ci --notes <mac-notes.md>
gh release upload v0.4 -R leviholliday/CedarLogic-Releases --clobber "<abs>/mac/build/CedarLogic-0.4-Mac.dmg"
```
The beta feed's 0.4 item is replaced by this build (same version, new
build). Check the published zip's `edSignature` in `appcast-native-beta.xml`
against the app's `SUPublicEDKey` with CryptoKit, as for every Mac release.

**4. The Windows and Linux files onto v0.4, renamed.** No script does this
yet:
```bash
mkdir -p ~/rel04 && cd ~/rel04
gh release download windows-native-testing -R leviholliday/CedarLogic -p 'CedarLogic-Windows-*'
gh release download linux-native-testing -R leviholliday/CedarLogic -p 'cedarlogic_*.deb' -p 'CedarLogic-Native-*.AppImage'
mv CedarLogic-Windows-Setup-x64.exe    CedarLogic-0.4-Windows-Setup-x64.exe
mv CedarLogic-Windows-Setup-ARM64.exe  CedarLogic-0.4-Windows-Setup-ARM64.exe
mv CedarLogic-Windows-native-x64.zip   CedarLogic-0.4-Windows-x64.zip
mv CedarLogic-Windows-native-ARM64.zip CedarLogic-0.4-Windows-ARM64.zip
mv cedarlogic_amd64.deb cedarlogic_0.4_amd64.deb
mv cedarlogic_arm64.deb cedarlogic_0.4_arm64.deb
ls    # + CedarLogic-Native-0.4.0-x86_64.AppImage, CedarLogic-Native-0.4.0-aarch64.AppImage
gh release upload v0.4 -R leviholliday/CedarLogic-Releases CedarLogic-0.4-Windows-* cedarlogic_0.4_*.deb CedarLogic-Native-0.4.0-*.AppImage
```
The installer's name is what makes a fresh install a Normal tester
(`CedarLogic-<version>-Windows-Setup-*.exe` = normal), so the rename matters.

**5. The release's notes, for all three.** `gh release edit v0.4 -R
leviholliday/CedarLogic-Releases --notes-file <notes.md>`. Its notes today
are the Mac beta's ("CedarLogic 0.4 for Mac, beta 1 ... Help > Check for
Updates", which on a Mac is CedarLogic > Check for Updates). There's no
public notes file yet: make it from `docs/RELEASE-0.4-notes.md` sections 1
and 2, with a download line for each computer.

**6. Sign the release, last of the files.**
```bash
packaging/sign-release.sh v0.4 --check-only                 # every check, and the build it finds
packaging/sign-release.sh v0.4 --build "$(git log -1 --format=%ct "$C")" --upload
```
`--build` is the committer time of step 1's commit (the Linux app's
`CL_BUILD_TIME`); `--check-only` should find the same number. Any file
added to v0.4 after this (a fixed DMG, say) means signing again.

**7. Out of beta: v0.4 to everyone.**
```bash
bash scripts/publish-update.sh promote 0.4 --dry-run
bash scripts/publish-update.sh promote 0.4
```
The Mac's normal feed gets 0.4, and v0.4 becomes a full release titled
"CedarLogic 0.4", marked Latest. From now on Normal testers on all three
update to it, and `install.ps1`/`install.sh` install it.
`gh release view v0.4 -R leviholliday/CedarLogic-Releases --json isPrerelease,isLatest,assets`
should say `false`, `true` and the 12 files above.

**8. Try the one-liners against the real release**, before the site leads
with them:
- Linux (Zorin laptop, the Pi): `sh linux/install.sh --dry-run`, then a real
  `sh linux/install.sh`.
- Windows: `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -SelfTest`,
  then `-DryRun`, then a real install. (The site's copy of `install.ps1` is
  older than `main`'s until step 14 copies it.)

**9. Homebrew.**
```bash
packaging/homebrew/bump.sh 0.4 --tap ~/src/homebrew-tap
brew style leviholliday/tap/cedarlogic && brew audit --cask --online leviholliday/tap/cedarlogic
git -C ~/src/homebrew-tap commit -am "cedarlogic 0.4" && git -C ~/src/homebrew-tap push
```
Commit the bumped `packaging/homebrew/Casks/cedarlogic.rb` here too.

**10. Scoop: make the bucket.** `scoop/README.md`, "Release day": `gh repo
create leviholliday/scoop-cedarlogic --public ...`, clone it,
`packaging/scoop/bump.sh 0.4 --bucket ~/src/scoop-cedarlogic`, its README,
commit, push. Commit the bumped `packaging/scoop/cedarlogic.json` here too.

**11. winget: the pull request.** `packaging/winget/bump.sh 0.4`, then on the
PC `winget validate --manifest <dir>` and `winget install --manifest <dir>`,
then the PR to `microsoft/winget-pkgs` from `leviholliday/winget-pkgs`
(`winget/README.md`; accept the CLA on the first one). Merging takes hours
to days: step 16.

**12. The Releases repo's README.** Copy `docs/drafts/Releases-README.md`
(without its top comment) to `README.md` in `leviholliday/CedarLogic-Releases`,
commit and push. Its winget line says "once CedarLogic is listed in winget"
until step 16.

**13. This repo.** Commit the bumped cask and Scoop manifest, bring
`SUPPORT.md`'s versions up to date (0.4 everywhere, no "until 0.4 is out"),
and merge the docs PR (#20).

**14. The website, with the owner there** (production rule). In the site's
main checkout (its real `node_modules`), the site README's "Release day":
merge the site PRs (#1, #2), `scripts/sync_installers.sh <app repo> main
main`, `python3 scripts/release_day.py --brew --scoop` (checks v0.4 is a
full, signed release with every file, writes today's date and the real file
sizes into `releases.json` and turns the switches on), build, check links
(`--remote`), the tests, commit, push, `npm run deploy`.

**15. Check every way in, as a user would.**
- `curl -sI https://cedarlogic.netlify.app/install.sh` and `/install.ps1`:
  `200`, `text/plain`. Every `/get/` link (`/get/mac`, `/get/windows-x64`,
  `/get/windows-arm64`, `/get/windows-x64-zip`, `/get/windows-arm64-zip`,
  `/get/linux-amd64`, `/get/linux-arm64`, `/get/linux-x86_64-appimage`,
  `/get/linux-aarch64-appimage`) lands on a v0.4 file.
- Mac: `brew install --cask leviholliday/tap/cedarlogic` on a Mac without
  CedarLogic (Open Anyway once), and the DMG from the site. A Mac on 0.3.4
  (Normal) is offered 0.4 by CedarLogic > Check for Updates.
- Windows: `irm https://cedarlogic.netlify.app/install.ps1 | iex` on a clean
  PC, the x64 installer from the site (SmartScreen: More info, Run anyway),
  and the two Scoop lines.
- Linux: `curl -fsSL https://cedarlogic.netlify.app/install.sh | sh` on
  Ubuntu or Zorin, on the Pi, and on Fedora (the AppImage); a downloaded
  .deb by double-click.
- In each app, Settings > General > Testing group > Normal tester, Check Now:
  "up to date" at 0.4.
- The site: `/whats-new/` shows 0.4, no page says preview, test build or
  beta; `scripts/sync-live-check.py https://cedarlogic.netlify.app --production`.

**16. Later: winget.** When the winget-pkgs PR is merged and `winget install
--id LeviHolliday.CedarLogic -e` works on a clean PC: `python3
scripts/release_day.py --winget` in the site, build, commit, deploy (owner
there); drop "once listed" from this repo's README and the Releases README.

### What no script does yet

- Steps 4 and 5: copying and renaming the Windows and Linux files into
  `v0.4`, and the public notes for all three. Both by hand, as above.
- Step 1: CI still publishes only from the `windows/native` and
  `linux/native` branch names, not from `main`.
- Step 3's `publish-update.sh` comes from the wx days: its Windows/Linux CI
  download is for the old workflows (so `--no-ci`), and its default notes
  say "Help > Check for Updates", which isn't where it is on a Mac.
- The Scoop bucket and the winget package don't exist yet (by design).

### Rolling back

- Mac: Sparkle only offers higher builds; a bad 0.4 is fixed by a 0.4.1.
- Windows/Linux: a bad file on v0.4 is replaced (re-upload, then step 6
  again). Normal testers only move forward, so a fix is a 0.4.1.
- Website: Netlify keeps every deploy (publish the previous one in the
  dashboard), or restore `releases.json` and deploy.

## Download-page wording

Pages: `/download/` (the Mac panel), `/download/mac/`, `/download/windows/`,
`/download/linux/` (built by the site's `scripts/build_pages.py`). On the
website **the download buttons come first**; the package managers and the
one-liners follow on each page as a clear, secondary "Or install with ..."
part, each command in a copy-able `.cmd` box with a "What does this do?"
link to its source on GitHub. (In the READMEs it's the other way round: the
commands lead, since people reading a README usually like them.)

### Mac (`/download/`'s Mac panel)

> [Download for Mac] (DMG). Open it and drag CedarLogic to Applications.
>
> **Or install with Homebrew:** `brew install --cask leviholliday/tap/cedarlogic`
> (Homebrew downloads CedarLogic, checks it and puts it in Applications.)
>
> *The first time you open it* (either way), macOS may say it can't check
> the app: open **System Settings › Privacy & Security**, scroll down and
> click **Open Anyway**. You only do this once.
>
> Needs macOS 14 Sonoma or newer on a Mac with Apple silicon (M1 or newer).

(The DMG and the cask are Apple silicon only: `mac/build.sh` builds for the
Mac it runs on. Intel Macs get "This app isn't supported".)

### Windows

> [Download for Windows (x64)] · [ARM64], then the installer's steps (If
> Windows shows "Windows protected your PC", click **More info** › **Run
> anyway**.) Or the portable zip.
>
> **Or install from the command line** (none of these show the warning):
>
> - PowerShell: `irm https://cedarlogic.netlify.app/install.ps1 | iex`. It
>   downloads the right installer for your PC (Intel/AMD or ARM), checks its
>   signature, installs CedarLogic for you (no administrator needed) and
>   opens it.
> - winget: `winget install --id LeviHolliday.CedarLogic -e` (once it's live,
>   step 16)
> - Scoop: `scoop bucket add cedarlogic https://github.com/leviholliday/scoop-cedarlogic; scoop install cedarlogic/cedarlogic`
>
> Installs in the Start menu; .cdl files open in it; remove it from
> Settings › Apps. It keeps itself up to date. Windows 10 or 11, 64-bit.

### Linux

> [.deb for PCs (amd64)] · [.deb for a Raspberry Pi 4/5 (arm64)] ·
> [AppImage x86_64] · [AppImage aarch64]. Double-click the .deb, or
> `sudo apt install ./cedarlogic_0.4_amd64.deb`. Make the AppImage
> executable and run it. (FUSE 2 note.)
>
> **Or install from a terminal:** `curl -fsSL https://cedarlogic.netlify.app/install.sh | sh`.
> On Ubuntu, Debian, Zorin, Mint and Raspberry Pi OS it installs the .deb
> with apt (it asks for your password); on Fedora and other systems it puts
> the AppImage in your home folder. It checks CedarLogic's signature first.
>
> Either way CedarLogic appears in the applications menu and keeps itself
> up to date. Needs a 64-bit system: Ubuntu 22.04, Debian 12 (Raspberry Pi
> OS Bookworm, 64-bit, on a Pi 4 or 5), Fedora 36 or newer.

The site's `releases.json` holds each platform's files, and `/get/` links
for each (`public/_redirects`), following its release-day switches.
