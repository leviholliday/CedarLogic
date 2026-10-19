# Mac app audit (v4/mac-app, 2026-10-16)

The owner: the Mac app "hits a bug about every other time I open it, or
every time I use it for more than 10 minutes". This round looked at the
Swift side only (`mac/App`); the engine is being fuzzed separately.

## What the Mac itself recorded

- **No crash or hang reports** for the native app (bundle
  `com.leviholliday.cedarlogic.native`) in `~/Library/Logs/DiagnosticReports`
  or `/Library/Logs/DiagnosticReports` (no `.ips`, `.crash`, `.hang`,
  `.spin`).
- The unified log keeps 10–15 October. Every native app session in it
  (9 of them) ended with `termination reported by launchd (0, 0, 0)`: a normal
  quit, no signal, no Force Quit. So whatever went wrong in those days was a
  glitch the owner quit out of, not a crash.
- What the log did show, every session: SwiftUI's "Publishing changes from
  within view updates is not allowed, this will cause undefined behavior",
  22 times each time a circuit window opened (fixed, below). Also, rarely:
  one "Modifying state during view update" (15 Oct 09:51) and three
  `com.apple.renderbox` "precondition failure" lines (12 Oct 16:33, 17:10;
  15 Oct 09:46, the last next to the Share menu opening). See the list below.
- `~/Library/Application Support/CedarLogic/crashdb` belongs to the classic
  (wx) app's crash reporter (`cedarlogic@3.3.2`), not this app.

## Fixed in this round

| Commit | What | Check that keeps it fixed |
| --- | --- | --- |
| Stop every circuit window publishing changes while SwiftUI draws it | `CanvasController.attach` set two `@Published` values inside `makeNSView` | `mac/Tools/launch-check.sh` (22 warnings per launch before, 0 after) |
| Play the first-launch sound off the main thread | `AVAudioPlayer.prepareToPlay` on the main thread never returned when the audio service didn't answer: the first launch froze on the launch screen | `launch-check.sh`, launch 1 (a first launch) |
| Print the Classroom recovery sheet and class pass with their content | an NSHostingView printed outside a window: no QR code, no white page | `--render-classroom` now checks the printed PDF (words, QR code scans) |
| Keep saving a circuit after its 500th undo step | past the engine's 500-step history nothing marked the window edited, so nothing more was autosaved | `CedarLogic --undo-check` |
| Let a circuit window's controller go when the window closes | Add a Gate (A) made the controller hold itself: its circuit and 60 Hz clock kept running for the rest of the session | `CedarLogic --leak-check` |

The last two are the best matches for "after 10 minutes": 500 undo steps
come quickly (every move, wire and pen stroke), and every window closed or
replaced after pressing A left its simulation running.

## Already fixed before this round (please confirm by hand)

These were on the 2026-10-08 list; the code shows fixes that came later.

1. **Show on Projector can't be exited** — fixed in a5410d7 (0.3.15): Escape
   in any of the app's windows, clicking the code, or File › Classroom ›
   Close Projector.
   To try: Classroom › a class you teach › Show on Projector (with and without
   a second display). Then, from the laptop: press Escape; show it again and
   use File › Classroom › Close Projector; show it again and press Escape
   *straight away*, while it is still sliding into full screen (the one case
   the code may not handle: closing during the full-screen animation). Each
   time the projector window and its full-screen space must go.
2. **Copy Image broken** (Export as Image) — rewritten in 127b98c (0.3.16):
   an image object plus PNG data on the pasteboard, at real size.
   To try: Export as Image › Copy Image, then paste into Pages, Keynote,
   Messages, Notes, Preview (File › New from Clipboard) and Google Docs in
   Safari and Chrome. Each should paste the picture at a sensible size.
3. **⌘O glitch** ("opened a circuit from CedarLogic Online with ⌘O and the
   window stopped taking edits until the app was restarted") — 98cb980
   hardened what such an open can leave behind (hidden sheets are cancelled,
   View › Make This Window Editable, the opening card's cover always goes)
   but the cause was never pinned down.
   To try: with a circuit open and edited in the last 2 seconds, press ⌘O,
   open a different circuit (and one downloaded from CedarLogic Online), and
   edit straight away. Repeat with Settings › General › Opening a circuit
   set both ways (replace / new window). If the window ever stops taking
   clicks: does View › Make This Window Editable bring it back? Is a
   question sheet hidden behind something? Note what was on screen.

## Suspected, not verified (exact steps to try)

4. **"Modifying state during view update"** (once, 15 Oct 09:51:40, source
   unknown). To find it: run `mac/Tools/launch-check.sh` style under `lldb`
   with a breakpoint on `write` with `$x0 == 2` (that's how the launch
   warnings were traced) while doing what you were doing then. By hand: if
   something looked wrong around a sheet closing or a toolbar toggle, note it.
5. **RenderBox "precondition failure" near the Share menu**. To try: Export
   as Image › Share… several times, pick Messages / AirDrop / cancel; watch
   for the sheet or the export window drawing wrongly or freezing.
6. **Quit while a circuit is still unsaved**. QuitConfirm fades every window
   to transparent, then quits; if macOS has to ask about a circuit, the
   question would be on a transparent window. Autosave normally saves within
   2 s, so it's rare. To try: make an edit and press ⌘Q › Quit within a
   second, on a new circuit and on one from Your Circuits. Does the app ever
   look gone but stay in the Dock?
7. **Closing a window while a sheet is open** (Truth Table, Build from
   Formula, Export). To try: open each sheet, then ⇧⌘W, ⌘Q and a Your
   Circuits open that replaces the window. Nothing should be left frozen.
8. **Split view and tabs**: closing, moving and reopening tabs while split,
   then undo/redo across them (page indices vs page ids). To try: split,
   put different tabs on each side, close a tab on the right side, ⌘Z, move
   a tab, ⌘Z again, Ctrl-Tab between them. Each side should keep showing a
   real page.
9. **Sync's engine thread waits on the main thread** (`DispatchQueue.main.sync`
   in `SyncHooks.swift` `on_main`, `Sync.swift` `onMainNow`). The engine
   has timeouts for quitting and stopping, and nothing was seen to hang;
   noted only because it's the classic deadlock shape. If the app ever
   beachballs with sync on, a `sample CedarLogic` taken then would show it.
10. **Every window runs a 60 Hz timer**, the second side of a split too, even
    when hidden or minimised (battery, not correctness).

## How this was checked

- `mac/build.sh`, then every headless mode on a copy of the build with its
  own bundle id and home folder (so the owner's settings, circuits, sync and
  classroom were never touched): `--render-ui` (232 pictures; the yellow
  boxes in some are SwiftUI's placeholder for AppKit controls ImageRenderer
  can't draw, not a bug), `--render-window`, `--render-classroom`,
  `--render-presenter` (30 PASS), `--undo-check`, `--leak-check`,
  `launch-check.sh`. `bar-test.sh` was not run (it needs the real mouse).
