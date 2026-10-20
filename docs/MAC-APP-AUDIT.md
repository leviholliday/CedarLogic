# Mac app audit (v4/mac-app, 2026-10-16; rounds 2 and 3 2026-10-19/20)

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

## Round 2 (2026-10-19): fixed

| Commit | What | Check that keeps it fixed |
| --- | --- | --- |
| Let a closed circuit window's controller, circuit and clock go | the window's SwiftUI `openWindow`, kept on its controllers, held the window's views, which held the controllers: no circuit window was ever freed, so every closed or replaced window kept simulating at 60 Hz for the rest of the session | `mac/Tools/session-check.sh` (before: FAIL controller and circuit kept, even with no edits) |
| Keep both sides of a split view in step when the circuit runs or pauses | Space/Step/Simulation View on the second side changed only that side and the engine: the first side (which runs the clock) could show Pause over a stopped circuit, or leave a "running" circuit frozen | `session-check.sh` (its five split lines) |
| Let go of a held Space when the canvas loses the keyboard | a Space key-up that went elsewhere (Cmd-Tab, a sheet) left `spaceDown` on: every click panned, nothing could be selected or moved | hand test below |
| Close the tab you meant, and keep the tab in front, when closing tabs | a second close within the first's 140 ms fade closed a different tab (by stale index, without its question); closing a background tab switched you to another tab | hand test below |
| Don't crash the oscilloscope's Option-Left after its recording is trimmed | after 60,000 steps the recording drops 10,000 and the cursor wasn't clamped: Option-Left read past the end and the app quit | hand test below |
| Don't put the Quit card over a question that's waiting for its answer | Cmd-Q during an app-modal question (Close Tab?) covered it with the dimmed screen and a card that couldn't be clicked | hand test below |

The first is the best match yet for "every time I use it for more than ten
minutes": each circuit opened over another (Settings > General > Opening a
circuit: replace) or closed kept a hidden simulation running.

### New headless checks (round 2)

- `mac/Tools/session-check.sh [actions] [seeds...]` (`CedarLogic
  --session-check`): the real circuit window, never shown, over a document
  that autosaves into Your Circuits, through 2000 random actions (a quarter
  hour of work; ~4 min per seed). Seeds 1-4 pass: every change leaves the
  window edited (past the 500-step history too), the file on disk matches
  the circuit at all 51 autosave pauses, undo steps stay in step, pages in
  range, a version is kept after an (11-minute, backdated) break, memory
  levels off (44 MB at start, ~110 MB after the first quarter, no growth
  after), no SwiftUI runtime warnings, and closing frees everything.
- `mac/Tools/launch-selftest.sh [rounds]` (`CedarLogic --launch-selftest`):
  ten saved states (first launch, normal, after an update, no Your Circuits,
  last circuit missing / damaged / empty, work waiting from before a break,
  a broken library, damaged circuits), 30 launches, all PASS: the right
  greeting, the right circuit reopened or a new one, Your Circuits and its
  versions, Sync and Classroom starting offline, nothing holding the main
  thread up (every step under 0.02 s).

### Found in the engine (for the engine's owner; not fixed here)

- **Crash opening some damaged circuits**: a null pointer in
  `GUICircuit::createWire` (called from `CircuitParse::parseGateToSend`,
  `applyCircuitFile`, `clOpenText`). About 1 in 900 damaged copies of the
  sample circuits, always with changed bytes (cut-short files are refused
  cleanly). If it were the circuit you were last in, the app would crash at
  every launch. Reproduce: `STATES_ONLY=damaged mac/Tools/launch-selftest.sh
  30` (rounds 9, 14, 25 and 27 crash; each leaves the circuit it crashed on
  as `crash<n>.cdl` in its folder).
- **Redo of a tab close after tabs were reordered closes the wrong tab**:
  `ClosePageCommand` (CedarCore/Extras.cpp) keeps a page index, and moving a
  tab isn't an undo step. Tabs A, B, C; close B; Cmd-Z; drag B to the end;
  Shift-Cmd-Z: C closes, with its work and no question. Holding the page by
  pointer (as the other commands do) would fix it.
- **Page ids are memory addresses** (`cl_document_page_id`): a page freed
  when its close falls off the 500-step history can have its address reused
  by a new tab, which then opens with the old tab's camera. Cosmetic.

### Ruled out (round 2)

- **Sync's engine thread waiting on the main thread** (old item 9): the
  hooks that use `DispatchQueue.main.sync` are called from `core->sync`
  outside the engine's mutex (SyncEngine.cpp `runCycle`), and the main
  thread's only waits on the engine thread are bounded (3 s in `~Engine`,
  8 s quit fallback). No deadlock path.
- **Quit leaving every window transparent while the app keeps running**
  (old item 6): if `NSApp.terminate` returns (a save question, a cancel),
  QuitConfirm puts every window back; with sync on, quitting always
  finishes within 8 s. (The related Cmd-Q-over-a-question case is fixed
  above.)
- **A split side showing a page that doesn't exist** (old item 8): every
  page index is clamped or checked (`reconcile`, `paneView`, the tab
  switcher, the engine's `page(i)`); `session-check` saw none in 8000
  actions with 35-51 split toggles per seed.
- **Autosave and versions over a long session**: covered by
  `session-check` (above); the 500th-undo fix holds in a real window.
- **Launch with a missing, damaged or empty last circuit, no library, a
  broken library, offline Sync/Classroom**: `launch-selftest` (above).
- **The updater offline**: done in round 3 (`launch-selftest`'s offline
  state, below).

### Still suspected after round 2 (all six settled in round 3, below)

- A status-bar message that never clears with Settings > "Show zoom,
  cursor position, and counts" off (`CLWindow.swift` clears messages with
  an `onChange` that doesn't fire for the bar's first value).
- A sheet cancelled by "Never stuck" (`checkHiddenSheet`/`makeEditable`
  call `endSheet` behind SwiftUI's back) may leave its flag set, so the
  same sheet (Add a Gate, a gate's Settings) won't open again in that
  window.
- At the full 500-step history, a plain click (no change) within 2 s of
  an edit registers one more do-nothing Undo entry; Undo can stay enabled
  for a few presses that do nothing.
- With the oscilloscope open and running, the whole window re-renders 15
  times a second (`scopeVersion` on the controller), reading `name.txt`
  twice each time: sluggish, not wrong.
- Cmd-W while a Truth Table / Build from Formula / Export sheet is up may
  close the tab (or the window) under the sheet.
- "Replace the current one" + an edited untitled circuit + a file that was
  imported before + more than 2 s to answer "save?": the hidden new window
  can be closed and replaced under the question, leaving a window with no
  document.

### Hand tests for round 2 (only the owner can confirm)

1. **Closed windows stop running**: open five circuits one after another
   from Your Circuits (with "replace" on), then look at CedarLogic in
   Activity Monitor: CPU should settle near 0 with one paused circuit,
   instead of climbing with each circuit opened.
2. **Split run/pause**: split view, click the right side, press Space:
   both sides show paused; Space again: both run and the circuit moves.
3. **Held Space**: hold Space over the canvas, Cmd-Tab to another app, let
   go, come back, click a part: it selects (before: the click panned).
4. **Tabs**: four tabs, C in front; click C's x and A's x quickly: A and C
   close, B and D stay. Then with A in front, close D from its x: A stays
   in front.
5. **Oscilloscope**: Simulation > fastest speed, scope open (Cmd-G), put
   the cursor near the right end, wait a minute or two, press Option-Left:
   no crash.
6. **Cmd-Q over a question**: put a part on a tab, close that tab (the
   "All work on this tab will be lost" question), press Cmd-Q: a beep, the
   question stays usable.
7. **Updater offline**: Wi-Fi off, launch: no update alert; Check for
   Updates... shows Sparkle's own "can't reach" message.

## Round 3 (2026-10-20)

Every suspect left open by rounds 1 and 2 is now fixed or ruled out, and
`session-check` drives the canvas through the gestures students use most.

### Fixed

| Commit | What | Check that keeps it fixed |
| --- | --- | --- |
| Let a status message go again when the bar's figures are off | with Settings > "Show zoom, cursor position, and counts" off, the bar is made with its message already in it, so its onChange never fired and the message (and the bar) stayed; the same closure held the window's controller for 4 s after the window closed | `session-check` "a status message goes again" (FAIL before) |
| Set the notes pane's character count after SwiftUI's update, not in it | the notes view set the pane's @State from makeNSView: **audit item 4**, the one "Modifying state during view update" in the owner's log (it comes whenever Notes opens on a circuit with notes) | `session-check.sh` counts SwiftUI warnings (1 before, 0 after) |
| Put a sheet that can't be seen away for good, not straight back up | "never stuck" and Make This Window Editable used `endSheet`; for SwiftUI sheets the flag stayed set and SwiftUI re-attached the same (still invisible) sheet at once, so the window stayed blocked | `session-check` "a sheet that can't be seen goes" (2 FAIL before) |
| Redraw only the oscilloscope as it records, not the whole window | the scope's counter was @Published on the window's controller: the whole window re-rendered ~13 times a second with the scope open on a running circuit | `session-check` (26 change notices in 2 s before, under 6 after) |
| Don't close a tab, or the window, under a sheet with Cmd-W | the menu's Close Tab reached the controller with a sheet up | `session-check` "Cmd-W with a sheet up" (FAIL before); the menu path: hand test 3 |
| Save a gate's setting changed in its Settings sheet | the sheet changed the setting (an engine undo step) but never called `edited()`: not mirrored into undo, no autosave, so a setting changed last was lost on quit or close | `session-check` "a setting changed: undo steps match" (FAIL before) |
| Don't bring back an empty window after a replaced circuit's question | the hidden new window was taken into Your Circuits (closed, its copy opened) while the old one asked "save?"; the answer then showed the dead window | by code; hand test 4 |
| Autosave after Undo and Redo, as after any edit | only edits scheduled the 2-s autosave: an undone or redone circuit reached its file (and Your Circuits, Sync, versions) only with the next edit or the window closing | `session-check`'s pause after its undo walk (seeds 1 and 4 at 2000: unsaved / stale before) |
| Check the updater and crash reports start quietly with no network | (a check, not a fix) `CL_UPDATE_FEED`/`CL_SENTRY_DSN`, honoured only by a test copy | `STATES_ONLY=offline launch-selftest.sh`: PASS, Sparkle's scheduled check ends with SUSparkleErrorDomain 2001, no window or alert |

**The leak seen after the round-2 merge** (`session-check.sh 300 1` on
v4/mac 998c132: controller and circuit kept): the status bar's old 4-s
clear-the-message closure (first row) held the window's controller
strongly; a session that ends within 4 s of a status message (more likely
in a short run) found it still alive 1.5 s after closing. Not a lasting
leak (it went after 4 s), and gone with the first fix: on v4/mac-app merged with v4/mac a169112,
`session-check.sh` at 300, 1000 and 2000 actions, seeds 1-4, frees the
controller and the circuit every time (results below).

### Ruled out

- **Item 5, RenderBox "precondition failure" near Share** (15 Oct): Share
  is a plain `NSSharingServicePicker` over a PNG file
  (`ExportImageView.share`): no SwiftUI state of the app's changes as it
  opens, so the line comes from the system's own drawing of the picker
  (RenderBox is SwiftUI's renderer, which the share menu uses), and the
  session went on and ended normally. Not reproducible headlessly (the
  picker needs a window on screen); nothing in the app to change.
- **Extra do-nothing Undo entries at the full 500-step history**: measured
  by the new undo walk at the end of each 2000-action session: of 517-655
  Undo presses, 61-187 did nothing -- all of them only after the 500 real
  steps were undone (each entry undoes the engine's latest step, so they
  pile up at the far end). A student would have to press Undo 500 times
  in a row to meet one; Redo is unaffected (the walk's Redo restores the
  circuit). Left alone: telling a do-nothing click from a real step at the
  limit needs the engine (a change counter), and the cost is one menu item
  staying enabled.
- **Cmd-Q / Shift-Cmd-W under a sheet** (old item 7): KeyMonitor only acts
  when the circuit window itself is key, and with a sheet up the sheet is;
  Close Circuit then goes to the sheet, which has no close button. Cmd-Q
  under an app-modal question was fixed in round 2.

### Found, not fixed (for the owner or the engine)

- **Version History can keep a stale "before the break" version** (app,
  CanvasController.noticeSaves): Your Circuits notes a save only at its
  2-second look while the window is unedited. In steady work the window is
  edited again before the look, so most autosaves aren't noted and
  `.pending.cdl` keeps an older circuit; after a break that older circuit
  is the version kept -- or none, if it equals the newest version. The
  circuit itself is always saved. Noting every save whose date changed
  (dropping `!d.isDocumentEdited`) would fix it, but also makes Sync hear
  of every autosave; left for a decision. (The session check now saves
  and lets the look see it before its break.)
### Session check, round 3

New scripted part (App/SessionGestures.swift, through the canvas's own
mouse, key and scroll handlers, in the hidden real window): wires pin to
pin and pin to wire; C while dragging, and Escape taking it back; Escape
mid-drag; a double-click opening a part's settings, and a setting
changed; the arrows (and Shift); Cmd-D and the click that places the
copy, or Escape; a part from the palette dropped or let go off the canvas;
trackpad scroll and pinch; the find bar; Straighten; Tidy Up's preview put
back (Escape), switched (Tab), kept (Return). Then the window's own
checks (status message, scope, notes, hidden sheet, Cmd-W under a sheet).
A quarter of the random actions are now such gestures (495-516 per 2000),
with two more invariants (Escape and Put Back change nothing: 0 times),
and each session ends by undoing all the way back and redoing all the way.

Results on v4/mac-app with v4/mac a169112 merged in (the current engine),
`session-check.sh N 1 2 3 4`: **all 12 PASS** at N = 300, 1000 and 2000
(every change left the window edited or already saved, every pause saved
and the file on disk the circuit, undo steps in step, pages in range, a
version after the break, no SwiftUI warnings, the walk back and forth
giving the circuit back, and the closed window's controller and circuit
freed every time). At 2000: 24-30 tabs and 25-34 splits per seed, memory
80-85 MB at the start, about 150 MB a quarter in, 89-102 MB at the end.
The undo walk at 2000 (before the redo fix, same numbers): 515-655 Undo
presses, 60-187 of them doing nothing (see "Ruled out"), 31-48 taking back
more than one step (several engine steps made in one turn of the main
loop are one Undo, as on screen). `launch-selftest.sh 3`: 33 of 33 PASS,
the offline launches included. `--undo-check` 8 PASS, `--leak-check` 4
PASS. `session-check.sh 0 1` (scripted part only, about a minute): PASS.

The checks found three of their own mistakes on the way, now fixed in the
check: an autosave can land between an edit and the look; a break taken
before the last save was noticed; the redo walk going past its start.

### Hand tests for round 3 (only the owner can confirm)

1. **Status message**: Settings > uncheck "Show zoom, cursor position, and
   counts"; press Cmd-S: the message shows, and is gone 4 s later.
2. **Setting saved**: double-click a label, change its text, Done, then
   Cmd-Q at once: reopen, the new text is there.
3. **Cmd-W under a sheet**: two tabs; Simulation > Truth Table (T); press
   Cmd-W: a beep, both tabs stay. Same with File > Export as Image.
4. **Replace, no ghost**: Settings > Opening a circuit: replace. A new
   circuit with a part on it; File > Import File... a .cdl imported once
   before; wait 5 s before answering "save?" (Don't Save): one window, the
   imported circuit's copy, taking edits.
5. **Scope**: open the scope (Cmd-G) on a running circuit with a clock:
   the window stays as quick as without it (Activity Monitor: CPU lower
   than before this round).

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

## Suspected after round 1 (see Round 2 above for 6, 8, 9)

4. **"Modifying state during view update"** -- found and fixed in round 3
   (the notes pane). Originally: (once, 15 Oct 09:51:40, source
   unknown). To find it: run `mac/Tools/launch-check.sh` style under `lldb`
   with a breakpoint on `write` with `$x0 == 2` (that's how the launch
   warnings were traced) while doing what you were doing then. By hand: if
   something looked wrong around a sheet closing or a toolbar toggle, note it.
5. **RenderBox "precondition failure" near the Share menu** -- ruled out
   in round 3. To try: Export
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
