# CedarLogic 0.4: what's new, and what changed

Written 2026-10-31 from the commits on `private/v4/mac` (tip `2a732c5`, 131 commits since the 0.3.17 beta, `5029f4c`) and the other branches listed in section 4. Nothing here is released yet. Sections 1 and 2 are the text for the release page and the in-app What's New. Sections 3 and 4 are for the owner and the testers.

If a line says a thing is on a platform, a commit or doc on the branches in section 4 says so. Where a fix lives on a branch that is not yet in `v4/mac`, the line says so.

## 1. What's new

### On every computer

**Recently Deleted.** Deleting a circuit from Your Circuits no longer ends it. Open "Recently Deleted" in Your Circuits to see what you deleted, newest first. Restore puts a circuit back. Delete Now removes it for good, and asks first. Circuits stay for 30 days, then they go. They stay on that computer. They aren't synced. (Mac, Windows, Linux.)

**Clear Drawing asks first.** Clearing all the drawing on a page now asks "Clear all drawing on this page?" and tells you Undo brings it back. (Mac, Windows, Linux.)

**Send Feedback has a daily limit.** You can send 10 reports a day. A short summary is needed before Send turns on. If you reach the limit, you can ask for more with "Request more" and say why in a line. You can ask once a day, and it gives you 10 more. (Mac, Windows, Linux. The limit needs the new feedback server, see section 4.)

**Wires and clicks behave the same every time.** Clicking where two pins sit close together, or dragging a wire at a corner, now picks the same one each time you open the app.

**Connect Nearby is quicker on big pages.** Holding C while you move parts, and Connect Nearby Pins, no longer stop to think for minutes on a page with thousands of parts.

**Circuits save the numbers you see.** Part positions are saved to a ten-thousandth of a grid square. Save, reopen and save again gives the same file.

### Mac

**Open the Practice Circuit.** It's in the File menu and the Help menu. It opens as a new circuit of your own, called Practice Circuit.

**Your sync code and class keys are in the Keychain.** They used to sit in a file that backups carried along.

**Crash reports say less.** A report says where the app crashed. It no longer carries a trail of clicks, web addresses, or your user name in a file path.

### Windows

**A menu bar.** File, Edit, Circuit, View, Simulate, Window and Help sit in the title bar, with the window buttons. Alt or F10 opens the menus, and Alt with a letter opens one. The ••• button is gone, and Settings is under File.

**Record your window for Send Feedback.** Record hides the form and records only the CedarLogic window, for up to a minute. It shows your pointer and where you click. The recording is an MP4 video.

**Predict, Then Reveal.** In Simulation View, guess what each light and display will show, then reveal. Right guesses get a green ring, wrong ones red, and ones you left alone grey. It's in the Simulate menu.

**Export as Image has Match Window.** A circuit in dark mode now exports dark, as it looks on screen.

**Projector Mode is in the View menu.**

**Settings has Tabs and Opening files.** Choose Modern or Classic tabs. See which app opens a double-clicked .cdl file, and choose CedarLogic with Windows' own Open with.

**Opening a circuit replaces the one you have open**, as on the Mac. You can change it in Settings.

**Ctrl+Tab shows a panel of your tabs**, with rounded corners and your current tab picked out, like the Mac's. Settings, focus mode, Simulation View and the side panel fade in and out. Nothing fades if Windows' animations are off.

**Send Feedback shows how it's going.** While it sends, a bar shows what's going. When it's done, you see a thank-you with Send Another and Done. If it fails, the button says Try Again and what you wrote stays.

**Testing group.** Settings > General has Normal tester and Beta tester, with Check Now beside it. See section 3.

### Linux

**A menu bar.** File, Edit, View, Simulation, Window and Help, in the same order as the Mac. Alt with a letter opens a menu, and F10 opens File. The ••• and ☰ buttons are gone. Settings is in File (Ctrl+,).

**Record your window for Send Feedback.** The same as on Windows. With ffmpeg on the computer it sends an MP4. Without it, it sends an animated picture. Both play in any browser.

**Predict, Then Reveal** is in the Simulation menu.

**Export as Image has Match Window and Share.** Match Window exports a dark-mode circuit dark. Share shows the picture in your file manager so you can drag it into a message or email.

**Open the Practice Circuit** is in the File menu.

**Opening a circuit replaces the one you have open**, as on the Mac. You can change it in Settings.

**Ctrl+Tab shows a panel of your tabs**, like the Mac's. Focus mode, Simulation View and the side panel fade. Nothing fades if your desktop's animations are off, or the circuit is too big to draw smoothly.

**A smoother launch screen.** The circuit window no longer flashes up around it.

**Settings fits a short screen.** Every setting can be reached, and the pages fade as you move between them.

**Safer on a shared computer.** Your circuits' folders can only be read by you. The sync code goes into the desktop's keyring, when there is one that's already unlocked.

**Testing group.** Settings > General has Normal tester and Beta tester, with Check Now. See section 3.

### Installing

Once 0.4 is out there will be one line to install on each system. See "Install commands" in section 3.

## 2. Fixed

### All apps (the shared engine)

The engine got a long round of checking: damaged files, damaged pastes and very long editing sessions. It found 40 problems and all are fixed (`docs/CORE-HARDENING.md`). The ones you could have met:

- The app no longer quits when you paste a label that has two lines.
- A damaged .cdl file or a damaged paste no longer crashes the app, freezes it, or leaves a part connected to the wrong thing.
- A .cdl file can no longer make a memory part read or overwrite a file on your computer when it opens.
- A wire no longer falls into two pieces after you drag a part. Zero-length bits of wire after a drag are gone too.
- A wire is no longer left stretched or broken after a paste.
- Disconnecting a RAM's data pin no longer leaves a wire running to nothing.
- Redo of Straighten Wires, Tidy Up or Build gives the same wires as the step itself.
- A counter at its largest number, an encoder with no inputs, a wide register, and an adder, register or equivalence gate with 0 or 1 inputs no longer crash the app or read past their data.
- A RAM no longer grows without limit when you only read it.
- Saving the same circuit twice gives the same file.
- A bus width or decoder size of millions in a file no longer freezes the app and runs it out of memory.
- Cancelling Check My Circuit part way through no longer touches a circuit that has already closed.

### Mac

- Once a circuit had 500 undo steps, later changes weren't saved. They are now.
- A change made in a gate's Settings sheet, such as a label's text, is saved. Undo and Redo save too.
- Undo and Redo of a move, a Delete or a Disconnect give the wires back the bends you drew, not new ones.
- Escape on a pasted copy or a new part that's still on the pointer no longer brings it back on Redo.
- A copy put on the pointer while you're drawing can be placed.
- Cmd-W with a sheet open no longer closes the tab or the window behind it.
- Option-Left in the oscilloscope no longer quits the app after a long recording has been trimmed.
- A closed circuit window now lets go of its circuit.
- Redo of a closed tab closes the same tab after you reorder tabs.
- Opening a damaged file whose wire lost its id shows a notice instead of crashing.
- The first-launch sound no longer holds up the launch.

### Windows

- Share Link no longer carries your private Notes into the link you paste in a chat. The circuit you send with Send Feedback leaves them out too.
- C while moving a gate you dragged from the side panel now connects it.
- Dragging a part draws one frame for each screen refresh, not one for every mouse move.
- Sync no longer fails when a security scanner holds a file for a moment while a folder is renamed. It tries again for about a second.
- Key hints say Esc, as Windows keyboards do. Pressing Alt again to leave the menu bar no longer takes it back when you let go.

### Linux

- C connects a moving part wherever the keyboard focus is, including after a drag from the side panel and with the side panel's search box focused.
- Settings no longer prints a GTK warning when it changes page.
- A button that is switched off looks switched off.

### Website

These are on site branches that are not merged or deployed (section 4).

- Learn pages load their live circuit when you get near it, not up front. Each topic page goes from about 394 KB to about 109 KB as sent.
- CedarLogic Online loads Classroom and the QR code when you use them. A visit goes from 667.6 KB to 593.9 KB as sent.
- Smaller icons, and icons kept for an hour, so a repeat view makes fewer requests.

## 3. For testers

### Testing groups

The Mac already has these. Windows and Linux now have the same row: Settings > General > Testing group, with Normal tester, Beta tester and Check Now.

| Group | Mac | Windows | Linux |
|---|---|---|---|
| Normal tester | Sparkle feed `appcast-native.xml` | newest full release in `leviholliday/CedarLogic-Releases` that has this CPU's `CedarLogic-<v>-Windows-<arch>.zip` | newest full release `v<version>` in `leviholliday/CedarLogic-Releases` with this computer's `.deb` or AppImage and a signed `SHA256SUMS` |
| Beta tester | `appcast-native-beta.xml`, which also carries the betas | the rolling `windows-native-testing` pre-release, as everyone had before | the rolling `linux-native-testing` build, as before |

- Pre-releases and drafts never count for a Normal tester. A release with no file for your system is skipped.
- Both Windows and Linux check the signed `SHA256SUMS` for the release's own tag and commit. A build must be newer than the one installed. Switching from Beta to Normal never takes you back to an older build. A Normal tester is never offered a version older than their own.
- A new install starts as a Normal tester. A copy whose settings are from before the choice existed came from the test builds, so it becomes a Beta tester, and the app says so once.
- The installers write an `install-channel` file that the first run reads. The Windows setup decides by its own file name: `CedarLogic-Windows-Setup-<arch>.exe` is the test build (beta), and `CedarLogic-<version>-Windows-Setup-<arch>.exe` is a release (normal), or `/CHANNEL=beta|normal`. On Linux, `install.sh` writes `beta` with `--preview` or when it fell back to the test build, and `normal` for a release.
- Until 0.4 has files on the release page, a Normal tester's Check Now on Linux says no finished version is out yet and points at Beta tester.
- Feedback reports the group, as on the Mac ("Normal tester" or "Beta tester"). Linux does this in `610f567`.
- Windows and Linux still have no crash reporter. The Mac has an opt-in one.

### Install commands

All four install the versioned release in `leviholliday/CedarLogic-Releases` (never the rolling `*-native-testing` builds), and all check what they download. These are the commands from `packaging/README.md`. None of them work until the `v0.4` release exists with its files signed and the site serves the two scripts (section 4).

| Platform | Command |
|---|---|
| Mac | `brew install --cask leviholliday/cedarlogic/cedarlogic` |
| Windows | `winget install --id LeviHolliday.CedarLogic -e` |
| Windows | `irm https://cedarlogic.netlify.app/install.ps1 \| iex` |
| Linux | `curl -fsSL https://cedarlogic.netlify.app/install.sh \| sh` |

- The Homebrew command assumes the tap is `leviholliday/homebrew-cedarlogic`. If the cask goes in the existing `leviholliday/homebrew-tap`, the command is `brew install --cask leviholliday/tap/cedarlogic`. This is not decided (section 4).
- Mac: the app is not notarized, so the first launch from Homebrew or the DMG needs System Settings > Privacy & Security > Open Anyway. The cask's caveats say so. The app is Apple silicon only, on macOS 14 or newer.
- Windows: winget and `install.ps1` install for the current user, with no administrator, and don't show SmartScreen's warning. The installer you download in a browser does ("More info", then "Run anyway"). `install.ps1` checks the installer's size and SHA-256, and the Ed25519 signature on `SHA256SUMS` when the release has one.
- Linux: on Ubuntu, Debian, Zorin, Mint and Raspberry Pi OS it installs with apt and asks for your password. On Fedora and the rest it puts the AppImage in your home folder. The test build is `sh linux/install.sh --preview`. `--dry-run` shows what it would do.
- Nothing from `packaging/` has been run on Windows or Linux: no winget, no PowerShell, no apt, no AppImage placement. `packaging/README.md` step 3 is that test. The Homebrew pieces were run on this Mac on 2026-10-31 (`bump.sh`, `brew style`, `brew audit --cask --online --strict`, `brew livecheck`, an install into a scratch folder).

### Windows PC test (`windows/PC-TEST.md`, 2026-10-29, on `ui/windows-menubar` `54f1236`)

- A Lenovo desktop, Core i7-12700, Windows 11 Education 26200, one 3440x1440 monitor at 59.97 Hz.
- It would not build at first: Visual Studio 2026 had no Windows SDK. The test used Microsoft's NuGet SDK packages and Ninja instead. With "Windows 11 SDK" added in the Visual Studio Installer, CI's commands work as they are. `windows/README` now says so (`5d9df5d`).
- 166 of 166 targets built. The drag self-test, the draw benchmarks, four 300-action session checks and the Start-menu test all passed.
- Dragging showed a new frame at every refresh at 100%, 125% and 150%. The old drawing path reproduced the "jumps around" report: 5 frames in 4.3 s.
- `--sync-test` failed 1 run in 3, at s47 (line 2014). That is core hardening bug 39, fixed in `f2e7501`. It has not been rerun on a PC since.
- During a drag the app painted about 330 times a second on a 60 Hz screen. That is fixed in `e0d1577` on `ui/windows-polish`. It has not been measured on hardware since.

### Things to try by hand

- Recently Deleted: delete a circuit, restore it, delete it again, and Delete Now it. On all three apps.
- Send Feedback: no summary (Send stays off); the daily limit and Request more (needs the new server).
- Windows: the menu bar by keyboard (Alt, F10, arrows, Escape), in dark mode and in focus mode; recording the window; Predict, Then Reveal; Share Link with a note in the circuit, and check the link doesn't carry it.
- Linux: the menu bar on Zorin and on the Pi (the Pi's themes); recording with and without ffmpeg; the Normal and Beta groups with Check Now.
- Mac: Open the Practice Circuit; Clear Drawing's question; a long session past 500 steps, then quit and reopen.

## 4. Internal

### Branches

All on `private` (`github.com/leviholliday/CedarLogic`). Counts are commits not on the base.

| Branch | Tip | Base | What it is |
|---|---|---|---|
| `v4/mac` | `2a732c5` | `5029f4c` (0.3.17 beta), 131 commits | The Mac 0.4 line. It has the merges of `core/hardening`, `pkg/installers`, `docs/parity-2026-10-31`, `mac/parity-gaps`, `mac/feedback-cap` and `sec/mac-apps`, and the Mac betas 0.3.18 and 0.3.19 (`b879b1a`, `fb8c926`). Last commits: sync self-test, Check My Circuit cancel fix (`90acd1a`), a Windows build fix in the engine (`964db1c`), feedback-check cleanup. |
| `core/hardening` | `ca3237b` | `5029f4c`, 101 commits | The shared engine: file fuzzer, `cl_document_check`, `thread_check`, ThreadSanitizer and ASan/UBSan in CI, 40 bugs fixed. `docs/CORE-HARDENING.md`. Merged into `v4/mac` (`1e7664a`). Not an ancestor of `ui/windows-channel` or `ui/linux-channel` (checked with `git merge-base --is-ancestor`), so Windows and Linux don't have these engine fixes yet. |
| `mac/parity-gaps` | `7fa7dff` | `v4/mac` (merged in `770d347`) | Mac: Recently Deleted (`7ce4a5d`, its self-test `7fa7dff`), Clear Drawing asks (`f342677`), Open the Practice Circuit (`72aa0ae`). Rules in `docs/RECENTLY-DELETED.md`. |
| `mac/feedback-cap` | `1f7b5bd` | merged in `2c9fbfd` | Mac client for the feedback cap: install id, required summary, Request more (`946b64e`), and `feedback-check` (`1f7b5bd`). |
| `sec/mac-apps` | `25769f8` | merged in `d43c3ae` | Keychain for the sync code and class keys, hardened runtime, crash reports scrubbed, Sparkle 2.10.1 (2.9.6 for the wx app) hash-pinned, 64 MB cap on a sync answer, secrets wiped on release. |
| `docs/parity-2026-10-31` | `5f1292f` | merged in `e5e2f4d` | `docs/V4-PARITY-MATRIX.md` rewritten from the code. Written before Recently Deleted, Predict on Windows, the fades and the rest below landed, so its "No", "In progress" and section 12 and 15 lists are out of date now. |
| `ui/windows-polish` | `47b6656` | `windows/native` `0baf7c0`, 26 commits | Windows menu bar and Settings under File, record for Send Feedback, Projector Mode, Clear Drawing asks, C-connect from the side panel, replace-on-open default, fades, Ctrl+Tab look, feedback progress and thank-you, cap and summary, Share Link without Notes, Predict, Export Match Window, Tabs and Opening files rows, Recently Deleted, one frame per refresh while dragging. |
| `ui/windows-channel` | `59a3ccc` | `windows/native` `0baf7c0`, 33 commits | Everything in `ui/windows-polish`, plus Normal and Beta testing groups (`3550a90`), the Linux app's group rules for one manifest per release (`8c56679`), Check Now beside the picker, and the installer marking a first run's group by its own name (`59a3ccc`). It does not touch `packaging/` (`bb5e07a`). |
| `ui/linux-channel` | `48c5ba9` | `linux/native` `12f3944`, 38 commits | The Linux 0.4 app: menu bar, record for Send Feedback, Predict, Match Window and Share, cap handling, Recently Deleted, fades, Ctrl+Tab, launch screen, owner-only folders, keyring, testing groups (`610f567`), `docs/LINUX-0.4.md`. Contains `pkg/installers` (merged in `2ff0bea`) and `sign-release.sh --build` / `--check-only` (`7ecee4c`, `48c5ba9`). |
| `pkg/installers` | `3eb9899` | `v4/mac` (merged in `f9338e4`) | `packaging/`: Homebrew cask and `bump.sh`, winget manifests for 0.4.0 and `bump.sh` and `validate.py`, `install.ps1`, `linux/install.sh` (one script for releases and the test build), `sign-release.sh`, README. |
| `windows/pc-test` | `223a17f` | `windows/native`, 11 commits (the 9 commits that `ui/windows-polish` also has, then 2 for the test) | `windows/PC-TEST.md`. Not in `ui/windows-channel`. |

`ui/windows-channel` (and with it `windows-polish`) and `ui/linux-channel` are each separate from `v4/mac`. The Mac line and the two native lines still need merging with each other before a release.

Not on this list but in the history: `windows/native` `0baf7c0` and `linux/native` `12f3944` are the bases the two channels are measured from. The old release prep branches `rel/mac-0.4`, `rel/win-0.4`, `rel/linux-0.4` (local; dated 2026-10-05–06) hold `docs/RELEASE-0.4.md`, the step-by-step runbook that `packaging/README.md` refers to. It isn't on `v4/mac`.

### Website (`/Users/leviholliday/Coding Projects/2026/cedarlogic-site`, off `main` `d900041`)

The three branches are stacked: each includes the one before.

| Branch | Tip | What it adds |
|---|---|---|
| `feat/feedback-cap` | `583105c` | 10 reports per sender a day, `POST /api/feedback/more` ("Request more", 10 more once a day), required summary (`bb05384`); the admin page lists who asked for more (`01ab53b`); page-weight trims: smaller icons, two unused pictures removed (-504 KB deployed), icon caching, `scripts/measure_pages.mjs`, `docs/SIZE.md`; README. 10 commits. |
| `perf/lazy-scripts` | `2ea5ba6` | On top of the above: CedarLogic Online loads Classroom and the QR code on first use (`43f422e`); Learn pages load the simulator when its circuit is about to be seen (`2ea5ba6`). |
| `fix/learn-dates` | `edee8d9` | On top of the above: one commit, a test fix. `test_learn_circuits` now expects the Article's and the site-wide WebPage node's dates separately, and that they agree (it failed on all eight topics). It changes no page. |

None of these is merged to `main` or deployed. The apps' cap handling on all three platforms only does anything once `feat/feedback-cap` is live. Until then the server has no cap, and the apps will not meet one.

### Open items

- **Merge order.** `v4/mac`, `ui/windows-channel` and `ui/linux-channel` all change shared files, and each is based on a different line. The engine fixes in `core/hardening` reach Windows and Linux only when they are merged in. `docs/V4-PARITY-MATRIX.md` found `mac/CedarCore` identical on all three native branches; `core/hardening` changes engine files the two channel branches don't have.
- **Windows and Linux builds from different commits.** `sign-release.sh` finds the manifest's build number from the two test builds' signed lists and refuses when they disagree. Sign soon after copying the files from the test builds (before another test build publishes), or pass `--build N`. Details in `packaging/README.md` on `ui/linux-channel`.
- **Stale text in `packaging/README.md`.** On `v4/mac` it says the Windows and Linux updaters follow only the `*-native-testing` builds. `ui/linux-channel` rewrites the Linux part, but its copy still says the Windows updater follows `windows-native-testing` only and doesn't read versioned releases. `ui/windows-channel` makes that wrong, and does not touch `packaging/`, so the README needs one more edit after both are merged.
- **The release must say its own version.** The released Linux build must have `project(CedarLogicNative VERSION 0.4.x)` to match the release, because the app never offers a Normal tester a version older than its own.
- **The AppImages** are not in the `RELEASE-0.4.md` runbook, which copies only the `.deb` files. Add them before running `sign-release.sh`. Any file added to `v0.4` after signing means signing again, or the installers refuse that file.
- **The site** needs: `install.sh` and `install.ps1` copied into `public/` and served as plain text (`netlify.toml` headers in `packaging/README.md`); the `/get/` redirects pointed at the `v0.4` files, with AppImage ones added; download-page wording; a production deploy with the owner there.
- **winget.** Fork `microsoft/winget-pkgs`, accept Microsoft's CLA on the first PR, run `packaging/winget/bump.sh 0.4`. The committed 0.4.0 manifests have placeholder hashes (64 zeros) until then. Merging takes hours to days. Show the winget line on the site only after the PR merges.
- **Homebrew.** The cask in the repo is at `0.3.4`. Run `packaging/homebrew/bump.sh 0.4` after the DMG is on the release, then push the tap. A day's lag breaks nothing, because the app updates itself.
- **The Mac's What's New tour** is still version `0.3` in `mac/App/WhatsNew.swift` on `v4/mac`. There is no 0.4 tour on this branch. The old `rel/*-0.4` branches had Windows and Linux 0.4 tours from 2026-10-05; those were not re-read for this file.
- **Apple silicon only on Mac.** `mac/build.sh` builds for the Mac it runs on. Intel Macs get "This app isn't supported". Say so on the download page until a universal build exists.
- **Core hardening, still open.** Bug 28's fix has no `core_check` case and may not be reachable. The Linux session check's seed 9 mismatch was not reproduced in the engine; `ca3237b` adds the six steps as a `core_check` case, and `7b8648c` on `ui/linux-channel` makes the Linux check report an interrupted undo/redo round trip instead of failing it. Several fuzz seeds are "unfinished" rather than passing: slow, not stuck (a page pasted onto itself until thousands of parts overlap). Known limits are in the doc.
- **Not run.** Core hardening and `v4/mac` were checked on a Mac. The commit messages for `ui/windows-channel` and `ui/linux-channel` name CI checks (for example `--channel-check`, `--trash-check`, `--feedback-check`). The one real-PC run (`windows/PC-TEST.md`) was on `54f1236`, before the polish commits. I did not run or build anything for this file.
- **Parity gaps still listed** in `docs/V4-PARITY-MATRIX.md` and `docs/LINUX-0.4.md`: Classroom on Windows and Linux (after 0.4), Presenter mode (Mac only), crash reports on Windows and Linux, screen-reader labels (partial on every platform), keyboard-only wiring on the native apps, and the web app's missing teaching-day basics (Find, templates, lab report PDF, focus mode and so on).

### Owner decisions still pending

1. **Homebrew tap name.** `leviholliday/homebrew-cedarlogic` (command `brew install --cask leviholliday/cedarlogic/cedarlogic`), or the existing `leviholliday/homebrew-tap` (command `brew install --cask leviholliday/tap/cedarlogic`). The command is printed on the website, so decide before the site goes up. (`packaging/homebrew/README.md`)
2. **Windows installer publisher.** The installer says `Cedarville University` (`AppPublisher` in `windows/installer/CedarLogic.iss`), so the winget manifest says it too, and the winget package id is `LeviHolliday.CedarLogic`. To put your own name there, change the `.iss` and the manifest together. (`packaging/README.md`)
3. **Gatekeeper handling in the cask.** The cask does not remove the quarantine flag, so a Homebrew install needs Open Anyway on first launch and its caveats say so. A `postflight` with `xattr -dr com.apple.quarantine` would make the first launch smooth because the cask pins the DMG's sha256, but it turns off a macOS safety check on users' Macs. The lasting fix is a Developer ID certificate and notarization. (`packaging/homebrew/README.md`)
4. **Delete Now confirmation.** In Recently Deleted, Delete Now asks "Delete “Name” now?" with "It and all its versions will be deleted for good. This can't be undone.", then Delete Now or Cancel (`docs/RECENTLY-DELETED.md`). The Mac (`LibraryView.swift`), Windows (`LibraryWindow.cpp`) and Linux (`LibraryWindow.cpp`) all ask. No doc or commit says what is still undecided about it, so I have not guessed.
