# Native Mac app — next round of fixes

Reference for "how it should behave" is always the **wx app** (src/gui/*).
Build `mac/build.sh`; checks `Tools/build-tools.sh` then cl_check / edit_check /
save_check / asan-check.sh (with `res/cl_gatedefs.xml`); look without screen
capture via `CL_SNAPSHOT=<dir>`. Reinstall to /Applications + DMG at the end.
Don't commit unless asked.

Groups are ordered cheapest model first. Run each group as one long session:
paste "do group N from mac/NEXT-ROUND.md" and let it finish, because every
extra message costs more than letting the model keep going.

---

## Next round: ideas from looking through the app (not started)

### Release (whenever you say)
- [ ] 0.2.1 to beta testers with the toolbar fix (64d5969): they're on 0.2,
      whose toolbar misses clicks. Version 0.2.1, publish-update.sh beta.
- [ ] Later: offer it to everyone (`publish-update.sh promote`).

### Group 5a (done): Sonnet 5, medium thinking (small, well-defined)
- [x] Help > Set Up CedarLogic… as in wx: opens Welcome at its setup page.
- [x] Split view keys as in wx: Switch Pane (Cmd-Option-Right) and Close
      Split (Cmd-Option-W), in the menu and Settings > Shortcuts.
- [x] Focus mode's tab "+" goes through BarTip like every other toolbar
      control, so it lights up and shows its tooltip up in the title row.
- [ ] Not done: the Minimal toolbar's "•••" overflow menu still has no
      tooltip in the title row. It's a real SwiftUI `Menu`, and wrapping
      it without breaking its own click handling needs more than a small
      change -- moved to group 5c.

### Group 5b: Opus 5.5, medium thinking
- [ ] Battery: while a clock runs, the canvas redraws 60 times a second
      (12–26% CPU with the circuit in Your Circuits), with no check for
      whether the window can be seen. Keep simulating, but skip drawing
      while it's covered, on another Space or minimized (occlusion
      state), and let the 60 Hz timer rest when nothing runs. Measure
      before and after.
- [ ] Export for older CedarLogic, as in wx's File menu: "Export as V2
      (legacy XML)…" and "Export as V1.x Compatible…" (school computers
      with an old CedarLogic). wx's code for it (CircuitParse's
      saveCircuit / saveCircuitLegacy) is already in the native build;
      it needs C functions in CedarCore and two menu items.

### Group 5c: Opus 5.5, high thinking
- [ ] The Minimal toolbar's "•••" overflow menu: no tooltip in the title
      row (from 5a). Needs a hover-only overlay that finds it for BarHover
      without swallowing the Menu's own clicks (a BarControl subclass that
      only intercepts hitTest while BarHover is looking, not for real
      clicks -- see the reasoning left in this session).
- [ ] Crash reports from beta testers: the wx app sends them (Sentry,
      crashpad); the native app sends nothing, so a crash on a tester's
      Mac goes unseen. The same Sentry project, or at least: after a crash,
      offer to send macOS's report on the next launch.
- [ ] Full screen: check the drawn bar there (macOS slides its own title
      bar over the window's top when the pointer reaches the screen edge)
      and make it behave. Untested so far.
- [ ] A permanent toolbar test: this round's real-mouse test (guarded HID
      clicks, a hidden test switch in the app) as a script, so a change
      to the bar is checked in a minute, not by hand.

---

## Group 1 (done): Sonnet 5, medium thinking (small, well-defined edits)
- [x] RAM editor: "Click a word to change it" becomes "Click a value to change it"
      (and any other "word" wording).
- [x] Tabs: make them solid (keep the glass gradient look, but gates must not
      show through).
- [x] Split view opens and closes as an instant cut (remove the 0.22s
      animations in SplitState / pane transition).
- [x] Welcome: ←/→ arrow keys go back and forward on every page.
- [x] Settings > General: "Opening a circuit" choice, **Replace the current
      one (like classic CedarLogic)** by default, or **Open in a new window**,
      with a one-line explanation under it. (Group 3 wires up the behaviour.)
- [x] Settings > Canvas: a low-wire colour option.
- [x] Dark canvas: low wires become a cooler, brighter slate-blue, clearly
      different from the grid; the dark major grid lines get quieter. Light
      mode unchanged.

## Group 2 (done): Opus 5.5, medium thinking (porting straightforward wx behaviour)
- [x] Welcome: port the wx page slide exactly (0.32s easeOut, name hidden
      until the slide settles; src/gui/Welcome.cpp `slide()`).
- [x] Settings window tab switch: match wx PreferencesWindow's transition
      (height and content together, no stutter).
- [x] ⌘O Your Circuits and Browse Versions: restyle to match the wx
      LibraryDialogs look (src/gui/LibraryDialogs.cpp).
- [x] Wiring: bring back the little square on the target pin while dragging a
      wire (shows it will connect).
- [x] A (add gate): the gate appears only after the mouse moves, like wx.
- [x] Tab dragging: much clearer drop spot (brighter, taller line, the other
      tabs slide apart to open a gap where it will land).

## Group 3 (done): Opus 5.5, high thinking (bugs that need real digging)
- [x] **Split: the right side behaves as if it has the whole window.** Its
      clicks and gate placement are measured against the full width, so a
      gate put on the right lands on the left. Check CircuitCanvasNSView
      coordinate conversion, CLCanvasHost frames, palette drop
      (CLSidePanel `canvasPoint`) and zoom/fit bounds for the second pane.
- [x] Top bar sometimes faded when switching back to the app until you click
      the grid: redraw the toolbar and title-bar pieces when the window
      becomes key or the app becomes active (TitlebarManager; the hidden
      _NSTitlebarDecorationView may reappear).
- [x] C shortcut (connect nearby) doesn't work: trace ShortcutStore →
      clKeyDown → connectNearby.
- [x] Ctrl+Tab: the wx tab switcher (src/gui/TabSwitcherMac.mm): a list of
      tabs, most recently used first; hold Ctrl and tap Tab to move through
      it, let go to jump there; Ctrl+Shift+Tab goes backwards. Works across
      both sides of a split.
- [x] Opening a circuit replaces the current one (wx), asking to save first
      if there are unsaved changes; follows the Settings choice from group 1.
- [x] Full debug pass: every shortcut in ShortcutStore, drag/drop, wiring,
      copy/paste, undo/redo, tabs + split, Simulation View, lock, export.
      Fix what's broken; list anything left.

## Group 4 (done): Opus 5.5, extra thinking (new, design-heavy pieces)
- [x] **Launch screen** (fresh launch only, **can't be skipped**, about 2.4s,
      icon colours, liquid glass): the icon rises and sharpens out of blur on
      a glass panel with a soft light sweep; "CedarLogic" draws in; a short
      status line ("Loading 212 gates…"); a thin glowing bar along the bottom
      that fills with the real loading work; then it dissolves into the
      window.
- [x] **Opening a circuit** (about 0.7s): a smaller glass card with the
      circuit's name and a quick sweep, then the circuit fades up.
- [x] **Help**: a new Mac Help window (sidebar, search, pages on the real
      shortcuts and features, reading the user's custom shortcuts) plus a
      **"Classic Help"** section keeping the original Contents pages.
      Help > Contents… and F1 open it.

---

**Model to start with: Opus 5.5 high, all in one session.** That's the best
credit balance for most of this. Group 1 is fine on Sonnet if you want to
split it off, but switching models means another message and the model has to
re-read everything, so it only saves credits if you do group 1 on its own
first. Use extra only for group 4 (the design work benefits from it), and only
move to max if something in group 3 stays broken after one try.

---

## Group 3b (done): Opus 5.5, high thinking (fixes from testing groups 1–3)
- [x] Palette drag: go back to the old behaviour. The gate should visibly
      leave the palette and follow the pointer from the side, not pop onto
      the canvas.
- [x] C with a palette-dragged gate: it jumps back to the palette and
      doesn't connect. (C works for gates already on the canvas and after A.)
- [x] Ctrl+Tab: match the wx one exactly (liquid-glass panel, the zoom-cut
      into the tab). Reread TabSwitcherMac.mm and MainFrame's tab switch.
- [x] Top bar still goes faded after switching back to the app; find the real
      cause (it's not only _NSTitlebarDecorationView).
- [x] ⌘O / Version History: always open centred on the circuit window you're
      in (right monitor, full screen), not where they were last.
- [x] Versions like Google Docs: save continuously, but keep a version only
      every so often (idle gaps / meaningful changes), not every few minutes.
- [x] Closing a tab with gates on it asks first ("All work on this tab will
      be lost…"), like wx CloseTabCanvas.
- [x] Settings: fade between pages again, gentler than the old custom shell
      (keep the native window).
- [x] Split drop hint once showed on the wrong half (couldn't reproduce):
      check paneFrames staleness in hint(at:).

## Group 3c (done): Opus 5.5, high thinking (from testing 3b)
- [x] Title at top left says "circuit.cdl" for Your Circuits circuits:
      show the library name (name.txt), same as ⌘O. Rename from the title
      renames the library entry.
- [x] Title menu hit area is flaky (only clicks in one spot): make the
      whole name + chevron one reliable button.
- [x] Palette drag: the carried gate should come out from *behind* the
      palette (under it), not draw on top of it. Drop the overlay; instead
      let the gate appear as it crosses the palette's edge, under the panel.
- [x] C while a gate is floating (palette drag or A): connect, but keep the
      gate on the pointer (wx pendingConnects: connections made, gate still
      moving; Escape takes back just those connections).
- [x] Settings: redesign to match wx PreferencesWindow's layout
      (src/gui/PreferencesWindow.cpp): read how each page is laid out.

## Group 3d (done): Opus 5.5, high thinking
- [x] Settings page switch like wx (the system preferences animation): the
      page empties, the window eases to the new height, then the page fades
      in. Ours jumps. Drive the window resize ourselves if SwiftUI's
      TabView won't animate it.
- [x] Toolbar buttons (right side too) sometimes miss clicks: the same
      window-drag area behind the bar (WindowDragArea's DragView) is taking
      them. Replace it with something that only drags on empty bar
      (mouseDownCanMoveWindow / hit-test pass-through), and check every
      toolbar button and the title.

## Group 3e (done, needs the user's real clicks to confirm): the top bar
- [x] Settings page switch a little slower (resize ~0.32s, fade ~0.28s).
- [x] Undid 3d's window-drag change (normal Mac dragging is back; the
      occasional missed toolbar click is back too, until this group).
- [x] Window dragging from the bar must behave like every Mac app:
      dragging a zoomed/tiled window restores its old size, double-click
      zooms, drag works every time. The 3d approach (isMovable false +
      performDrag + polling pressedMouseButtons) breaks this: the server-side
      drag doesn't send mouseUp, so the poll can switch movable off mid-drag.
      Likely fix: leave isMovable true and instead stop the bar's *buttons*
      from being drag regions (each control reports mouseDownCanMoveWindow
      false, e.g. an NSView under each that forwards clicks), so empty bar is
      a normal title bar again.
- [x] Toolbar buttons that sometimes miss clicks: reproduce reliably first
      (log mouseDown/hit view/whether a window drag started), then fix.

## Group 3f (done; checked with real mouse input, needs the user's hands too): the top bar, found and fixed
Opus 5.5, max thinking. 3e didn't fix it (buttons finicky, lit only in one
spot that moved; double-click/drag sometimes dead when buttons worked).
Measured, not guessed, with real HID mouse events (see memory note
title-bar-clicks): three separate causes, all from the bar living in
SwiftUI under AppKit's 32pt title bar.
- [x] Clicks: the title bar container stretched full width took every
      press in its top 32pt (only a button's bottom edge clicked). Now it
      covers just the red/yellow/green buttons, and snaps back whenever
      AppKit resets it (frame observers).
- [x] Window nudging on sloppy clicks: the window server's drag region is
      the whole top 32pt (SwiftUI can't exclude our buttons). isMovable is
      now off for the CedarLogic window; the bar's background drags it
      (performDrag: tiling, other screens and restore-from-Fill all work).
- [x] Hover and tooltips: macOS gives views under the title bar no
      enter/exit up there. Hover now comes from the mouse's moves (BarHover),
      tooltips are the bar's own (BarTip).
- [x] Double-click on the empty bar: handed straight to AppKit's title bar
      (sent through the window it went back to the drag view): Fill works.
- [x] Focus mode: the tab strip, as the top row, drags the window, its
      double-click makes a tab (wx), and its hover works up there too.

## Group 4b (done): Opus 5.5, high thinking (polish from testing group 4)
- [x] Launch bar: glide continuously instead of jumping step to step
      (ease the shown value toward its target every frame; never stall).
- [x] Opening card: no flash of the circuit before the card. The cover has
      to be there on the window's very first frame, then the card eases in.
      Smoother timing overall (a touch longer, softer curves).
- [x] Launch opens the circuit you had open last, every time: remember it
      ourselves (last circuit window to close, or last opened), don't rely
      on the recent-documents order or window restoration.
- [x] Any other polish that makes launch and opening feel seamless.

## Released
- [x] 0.2 (build 737) to beta testers, 2026-09-26: release v0.2 in
      leviholliday/CedarLogic-Releases (zip for updates, DMG for installs),
      beta native feed only. `publish-update.sh promote 0.2` offers it to
      everyone later.
