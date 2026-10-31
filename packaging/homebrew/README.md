# Homebrew cask for CedarLogic (Mac)

What people type:

```bash
brew install --cask leviholliday/cedarlogic/cedarlogic
```

That taps `leviholliday/homebrew-cedarlogic` (GitHub repo) and installs the
cask `cedarlogic` from it. The cask installs `CedarLogic.app` from the DMG of
the newest full release in `leviholliday/CedarLogic-Releases`
(`v<version>/CedarLogic-<version>-Mac.dmg`). After that the app updates itself
(Sparkle), so the cask says `auto_updates true` and `brew upgrade` leaves it
alone unless asked (`brew upgrade --cask --greedy cedarlogic`).

## The tap repo

Create **`leviholliday/homebrew-cedarlogic`** (public; Homebrew needs the
`homebrew-` prefix, and drops it in the command). Layout:

```
homebrew-cedarlogic/
  README.md            (one line: brew install --cask leviholliday/cedarlogic/cedarlogic)
  Casks/
    cedarlogic.rb      (a copy of packaging/homebrew/Casks/cedarlogic.rb)
```

Nothing else is needed: no CI, no bottles (a cask downloads the release's
DMG).

**Or skip the new repo:** you already have `leviholliday/homebrew-tap`
("the central place for all Homebrew packages", with `glideball`). Put
`Casks/cedarlogic.rb` there instead and the command becomes
`brew install --cask leviholliday/tap/cedarlogic`. Everything below works the
same; only the tap path and the command differ. Pick one before the website
goes up, since the command is printed there.

Current Homebrew (7.x on this Mac) only loads third-party taps the user
trusts (`brew trust`). Installing with the full name
(`leviholliday/cedarlogic/cedarlogic`) counts as trusting that one cask for
`install`, `reinstall` and `upgrade` (Homebrew's `trust.rb`). Anything else
done by the short name may ask for `brew trust leviholliday/cedarlogic` first.

## Bumping it for a release

After the release's DMG is on GitHub (and the release isn't a pre-release):

```bash
# in this repo, to check it and update the copy here:
packaging/homebrew/bump.sh 0.4                  # or no version: the newest full release with a DMG
# or straight in the tap's checkout:
packaging/homebrew/bump.sh 0.4 --cask ~/src/homebrew-cedarlogic/Casks/cedarlogic.rb
```

`bump.sh` finds release `v0.4` and its `CedarLogic-0.4-Mac.dmg`, downloads it,
compares its sha256 with the digest GitHub keeps for the file, opens the DMG
read-only to check it holds `CedarLogic.app` with version 0.4 and bundle id
`com.leviholliday.cedarlogic.native` (and warns if the CPUs it runs on don't
match the cask's `depends_on arch`), then rewrites the cask's `version` and
`sha256` lines. `--dry-run` checks everything and changes nothing. It never
commits or pushes.

Then check the cask and publish the tap:

```bash
cd ~/src/homebrew-cedarlogic
brew style Casks/cedarlogic.rb
brew audit --cask --online --strict leviholliday/cedarlogic/cedarlogic   # with the tap installed locally
brew livecheck --cask leviholliday/cedarlogic/cedarlogic                 # says "<version> ==> <version>"
git commit -am "cedarlogic <version>" && git push
```

Someone who already has it gets the new version from the app itself; `brew`
only matters for new installs, so a day's delay in bumping the tap breaks
nothing.

If a release's DMG is ever replaced under the same version, run `bump.sh`
again: the sha256 changes, and anyone installing with the old cask gets a
checksum error until the tap is pushed.

## What's in the cask, and why

- **url / version**: `v#{version}` and `CedarLogic-#{version}-Mac.dmg`, the
  names `mac/package.sh` and `docs/RELEASE-0.4.md` use. The tag and the DMG
  must use the same version text (`v0.4` and `0.4`, not `0.4.0`).
- **livecheck**: GitHub releases of `CedarLogic-Releases`, skipping drafts and
  pre-releases (every Mac beta is a pre-release), and skipping releases with
  no Mac DMG (a Windows- or Linux-only release).
- **depends_on arch: :arm64**: `mac/build.sh` builds for the Mac it runs on.
  The 0.3.4 DMG is Apple silicon only (`lipo -archs`: arm64). If a release is
  ever built universal, delete that line (`bump.sh` says so when it sees
  x86_64).
- **depends_on macos: :sonoma**: `LSMinimumSystemVersion` is 14.0. (In
  Homebrew 7, `:sonoma` means Sonoma or newer.)
- **uninstall quit**: the bundle id, so `brew uninstall` asks the running app
  to quit first.
- **zap** (only `brew uninstall --zap`): everything the app writes under
  `~/Library`, found in `mac/App`:
  - `Application Support/CedarLogic`: `Library` (Your Circuits),
    `Parts`, `Templates`, `Sync` (with the sync secret, a 0600 file),
    `Classroom`
  - `Preferences/com.leviholliday.cedarlogic.native.plist` (UserDefaults)
  - `Caches/com.leviholliday.cedarlogic.native` (Sparkle's downloads, URL cache)
  - `HTTPStorages/com.leviholliday.cedarlogic.native` (and `.binarycookies`)
  - `Saved Application State/com.leviholliday.cedarlogic.native.savedState`
  - `WebKit/com.leviholliday.cedarlogic.native` (the Help window is a WKWebView)

  **Zap deletes Your Circuits.** The circuits library lives in Application
  Support, so `--zap` removes the student's circuits too; the caveats say so.
  Plain `brew uninstall` removes only the app.

  **Not removed by zap:**
  - Keychain items. Zap can't delete them, but CedarLogic doesn't create any
    today (sync keeps its secret in a file, which zap removes). If a future
    version stores something in the Keychain, list it in the README, not in zap.
  - `~/Library/Preferences/CedarLogic Preferences`: the old wx app's settings
    file. The native app only reads it once to carry settings over, and
    the wx app still uses it.
  - `~/Library/Caches/io.sentry`: Sentry's crash-report cache is shared by
    every app that uses Sentry, so it isn't CedarLogic's to delete.
  - Temporary folders (`$TMPDIR/CedarLogic Pictures`, `CedarLogic Feedback`,
    `CedarLogic Classroom`): macOS clears them.
- **Gatekeeper**: the app is signed ad hoc, not notarized. Homebrew quarantines
  what it downloads, so the first launch is blocked until the user clicks
  **Open Anyway** in System Settings > Privacy & Security, the same as the
  DMG. The caveats say this. Some taps remove the quarantine in a
  `postflight` (`xattr -dr com.apple.quarantine`). That would make the first
  launch smooth, since the cask pins the DMG's sha256, but it switches off a
  macOS safety check on users' Macs. That's your call, so the cask doesn't do
  it. The lasting fix is a Developer ID certificate and notarization, which
  would also make the caveat unnecessary.

## Tested (2026-10-31, Homebrew 7.0.11, on a scratch tap)

- `bump.sh` against the live release: found `v0.3.4` (skipping the 0.3.5 to
  0.3.19 betas), sha256 matched GitHub's digest, the DMG holds CedarLogic.app
  0.3.4 / `com.leviholliday.cedarlogic.native` / macOS 14 / arm64.
- `brew style`: no offenses. `brew audit --cask --online --strict`: passes.
- `brew livecheck`: `0.3.4 ==> 0.3.4`.
- `brew install --cask --appdir=<scratch>`: installs, shows the caveats; the
  app is quarantined and ad hoc signed, as expected. `brew uninstall`: removes it.
