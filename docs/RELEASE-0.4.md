# Releasing CedarLogic 0.4

Everything for 0.4 is prepared on branches that publish nothing until the steps
below are run. Run them in this order, only after the owner says go.

| Piece | Branch (worktree) | Where it goes when released |
|---|---|---|
| Mac | `rel/mac-0.4` (`.claude/worktrees/sync-m`) | `mac/native`, then the Sparkle feeds and the v0.4 release |
| Windows | `rel/win-0.4` (`.claude/worktrees/sync-x`) | `windows/native` (CI publishes `windows-native-testing`, which installed apps update from), then copies in v0.4 |
| Linux / Pi | `rel/linux-0.4` (`.claude/worktrees/sync-l`) | `linux/native` (CI publishes `linux-native-testing`, which the Pi updates from and `install.sh` installs from), then copies in v0.4 |
| Website | `rel/site-0.4` (`cedarlogic-site-sync-w`) | site `main`, then `netlify deploy --build --prod` |

The website's `releases.json` has a switch per platform: `"channel": "release"`
(Windows/Linux are 0.4, downloads from v0.4) or `"preview"` (today's preview
pages from the testing builds). Both are on `"release"`. If Windows or the Pi
isn't ready, set that one to `"preview"` and skip its steps below.

## 0. Before

- Set `released` (all three) in the site's `releases.json` to the release day.
- `gh release view v0.4 -R leviholliday/CedarLogic-Releases` must say "not found".

## 1. Mac (to everyone)

```bash
cd .claude/worktrees/sync-m        # on rel/mac-0.4, clean
git push private rel/mac-0.4:mac/native
DMGBUILD=<venv>/bin/dmgbuild bash mac/package.sh   # python3 -m venv v && v/bin/pip install dmgbuild
/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" mac/build/CedarLogic.app/Contents/Info.plist   # 0.4
gh release create v0.4 -R leviholliday/CedarLogic-Releases --title "CedarLogic 0.4" --notes-file docs/RELEASE-0.4-notes.md
cd ../beta-updates-tester-settings-1be804
bash scripts/publish-update.sh normal 0.4 --native "<abs>/sync-m/mac/build/CedarLogic.app" --no-ci --notes <abs>/sync-m/docs/RELEASE-0.4-notes.md --dry-run
bash scripts/publish-update.sh normal 0.4 --native "<abs>/sync-m/mac/build/CedarLogic.app" --no-ci --notes <abs>/sync-m/docs/RELEASE-0.4-notes.md
gh release upload v0.4 -R leviholliday/CedarLogic-Releases "<abs>/sync-m/mac/build/CedarLogic-0.4-Mac.dmg"
```

Then verify the published zip's EdDSA signature against the app's
`SUPublicEDKey` with CryptoKit (`Curve25519.Signing.PublicKey`), as for every
release. Set the Mac `sizeMB` in `releases.json` from the DMG.

## 2. Windows

```bash
git push private rel/win-0.4:windows/native
gh run watch <run> -R leviholliday/CedarLogic --exit-status    # build, click tests, installer test (reads version 0.4.0), publish
mkdir w && cd w && gh release download windows-native-testing -R leviholliday/CedarLogic
mv CedarLogic-Windows-Setup-x64.exe   CedarLogic-0.4-Windows-Setup-x64.exe
mv CedarLogic-Windows-Setup-ARM64.exe CedarLogic-0.4-Windows-Setup-ARM64.exe
mv CedarLogic-Windows-native-x64.zip   CedarLogic-0.4-Windows-x64.zip
mv CedarLogic-Windows-native-ARM64.zip CedarLogic-0.4-Windows-ARM64.zip
gh release upload v0.4 -R leviholliday/CedarLogic-Releases CedarLogic-0.4-Windows-*
```

(The testing release must be at the `rel/win-0.4` commit: check its title says so.)

## 3. Linux / Raspberry Pi

```bash
git push private rel/linux-0.4:linux/native
gh run watch <run> -R leviholliday/CedarLogic --exit-status    # builds, .deb installs on Debian 12 / Ubuntu 24.04, publish
mkdir l && cd l && gh release download linux-native-testing -R leviholliday/CedarLogic -p 'cedarlogic_*.deb'
mv cedarlogic_amd64.deb cedarlogic_0.4_amd64.deb
mv cedarlogic_arm64.deb cedarlogic_0.4_arm64.deb
gh release upload v0.4 -R leviholliday/CedarLogic-Releases cedarlogic_0.4_*.deb
```

## 4. Website

```bash
cd cedarlogic-site            # main checkout (real node_modules: deploying from a worktree with a symlinked one breaks the functions)
git merge rel/site-0.4        # then set the released dates / Mac sizeMB if not done
python3 scripts/build_pages.py && python3 scripts/check_links.py --remote   # the v0.4 links resolve now
git commit -am "CedarLogic 0.4" && git push origin main
netlify deploy --build --prod
```

Then: every `/get/` redirect answers (`curl -sI https://cedarlogic.netlify.app/get/mac` etc.),
`/whats-new/` shows the 0.4 tiles and the GitHub notes, and the sync live check
(`scripts/sync-live-check.py https://cedarlogic.netlify.app --production`) passes.

## Rolling back

- Mac: Sparkle only offers higher builds; a bad 0.4 is fixed by a 0.4.1. The
  website's download can go back by restoring `releases.json`'s Mac block.
- Windows/Linux: push the previous commit to `windows/native` / `linux/native`
  (CI republishes the testing release); set the site's channel to `"preview"`.
- Website: Netlify keeps every deploy (publish the previous one in the dashboard).
