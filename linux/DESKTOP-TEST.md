# Linux desktop test: Zorin OS

Branch `linux/native-v4-desktop`, from `linux/native-v4-session` at 8a8ab21.
Tested by Claude (Sonnet 5.5) on the owner's laptop, driving it from a
terminal. Read the "What this did not cover" section: nobody sat and used the
app by hand for ten minutes.

## Machine

- Zorin OS (Ubuntu-based), GNOME on **Wayland**, GTK 3.24.41, cairo 1.18.0
- Lenovo laptop, 2 cores, 7 GB RAM, built-in 1366x768 display at **scale 1.0**
  (no HiDPI, no fractional scaling; scale 2 appears only in the benchmark's
  own simulated rows)
- Release build with Ninja: 6 min 56 s on those 2 cores, 158 steps, no errors

## Session check (`linux/Tools/session-check.sh`, 300 actions, seeds 1-8)

Each seed runs three ways: plain, with `G_DEBUG=fatal-criticals`, and (seed 1
only) with the canvas always drawn through its frame and whole. 17 runs.

**Real display.** `xvfb-run` does not give a virtual display on this machine:
GTK prefers `WAYLAND_DISPLAY` over the Xvfb `DISPLAY`, so my first "Xvfb"
runs (and the first bench) went to the real Wayland compositor
(`GdkWaylandDisplay` in the bench header). I'm reporting them as the
real-display runs, because that is what they were.

| | before the fixes | after |
|---|---|---|
| Wayland, 17 runs | 8 pass, 9 fail | seeds 1, 2, 4, 5, 7, 8 plain and fatal-criticals, and seed 1 frame-whole (13 runs) pass; seeds 3 and 6 had passed before |
| Xvfb, forced X11 (`env -u WAYLAND_DISPLAY GDK_BACKEND=x11`), 17 runs | 15 pass; seed 8 plain and fatal-criticals fail | seed 8 plain passes (the fatal-criticals run was not repeated) |

No crash, no hang, no GTK or GLib warning, and no memory failure in any run
(seed 8: 113 MB at start, 177 MB at end, under the 150 MB growth limit). Every
failure was one of the two check problems below.

Wall time for 17 runs: about 18 min on Wayland, 20 min on Xvfb.

## Drag benchmark (`--bench-drag 60 Lab5.cdl`: 53 gates, 40 wires)

ms per frame, scale 1, step by step, mean / p95 (draw plus the engine's drag
step; 16.7 is 60 frames a second):

| | Wayland ("straight") | Xvfb X11 ("through frame") | XWayland ("through frame") |
|---|---|---|---|
| one gate | 1.30 / 1.46 | 4.07 / 8.14 | 2.04 / 2.12 |
| one gate, whole page (no kept layer) | 7.49 / 8.00 | 10.53 / 13.23 | not recorded |
| select all | 11.65 / 12.10 | 16.45 / 18.77 | 12.59 / 13.08 |
| pan | 8.30 / 8.78 | 12.36 / 15.66 | 8.95 / 9.08 |

Scale 2 (simulated, Wayland): one gate 4.03 / 3.98, select all 20.68 / 21.29,
pan 15.94 / 16.33. The engine's own handler cost is 0.1 ms for one gate and
about 0.7 ms for select all, so the cost is drawing, as `SMOOTHNESS.md` says.

Live, on the frame clock at scale 1 (real GTK drawing):

| | frames a second, one gate | draw p95, one gate | frames a second, select all | draw p95, select all |
|---|---|---|---|---|
| Wayland | 60 | 9.15 ms | 31 | 13.60 ms |
| XWayland | 60 | 10.51 ms | 47 | 14.73 ms |
| Xvfb | 51 | 14.01 ms | 37 | 25.27 ms |

Reading it: dragging one gate on this laptop holds 60 frames a second on
Wayland and XWayland, and the kept static layer is worth about 6 ms a frame
(7.49 ms drawn whole vs 1.30 ms). Moving everything on a 53-gate circuit
takes 12 ms a frame, which is over the 8 ms the notes ask for and drops to
about 31-47 frames a second live. Xvfb numbers are slower than real X11
because it is software and has no compositor; don't use them as a verdict. The
static layer and the whole-page frame differ by 24 of 720,410 pixels (0.003%).
Each row is one 60-step run, so treat "worst" as noisy.

## What I saw

I could not see the screen live. I looked at the app's own `--screenshot`
renders (main window with Lab5, split view, focus mode, Simulation View
scope dock, the hamburger menu, the toolbar's More menu, Settings pages 0-4,
Add a Gate, gate settings) at 1366x768, and launched the real app on the
Wayland display for 12 s (clean exit by timeout, nothing on stderr).

- The drawing is sharp and aligned at scale 1. Toolbar, tabs, side panel,
  minimap and status bar line up. Settings is clean. Dark mode follows the
  desktop. I found nothing blurry or misaligned.
- Gate names in the side panel are cut off at the default width ("2-input A...",
  "3-input N...") for most gates. It looks deliberate (ellipsis) but a long
  label is hard to tell apart. Not changed.
- The Simulation View scope dock rendered as an empty dark panel in the
  `scope` screenshot, on Lab5 and on the SR latch template, with no
  "Oscilloscope" header or close button, although `Scope.cpp` draws both.
  I tried to see it through a real X framebuffer grab (xdotool Ctrl+G, `xwd`)
  but that grab was unusable (a black rectangle where a window hadn't painted).
  So I can't say whether the dock is blank in a real window or only in
  `--screenshot`'s capture. Worth a look by eye.
- `--show truth` on Lab5 prints "nothing to picture": Lab5's first page has
  no switch-to-light table, so the app shows its "A truth table couldn't be
  made" message instead of the Truth Table window. That is expected. On
  `res/templates/template-builtin-ff-sr.cdl` the window renders cleanly
  (2 switches, 2 lights, 4 rows, tabs for Karnaugh Map, Formulas and Check).
- The PNG captures have black pixels in the window's rounded corners. That is
  the capture, not the window.

## Fixed (one commit each)

1. `1e89d06` Session check: Ctrl+Tab isn't wrong when Ctrl+W's question
   dismissed it. Ctrl+W on a tab with work on it asks "Close this tab?"; the
   card takes the window's focus, which dismisses the switcher, and the check
   cancels the question, so no tab was picked. The check counted the switch
   as wrong. Seeds 1, 4, 5, 7, 8 and fatal-criticals 1, 2, 8 failed on this
   only. Proved by tracing the commit and cancel calls: the switcher was
   cancelled before Ctrl came up and the tab count stayed at 3. The app's
   behaviour is unchanged.
2. `7c3c76b` Session check: close only the copy's window after Duplicate. The
   check closed every other circuit window, including one that was already
   open, and called that a leak ("left 1 windows alive (2 before)"). Seed 8
   on X11. Proved by printing the counts: 2 before the copy, 1 after
   waiting 1.5 s.

Both are changes to `SessionCheck.cpp` only. I found no app bug.

## What this did not cover

- No ten-minute hands-on session. I can't drive the real screen or feel
  latency from here, so placing parts, drawing wires, undo/redo, tabs, split,
  the Simulation View and Settings were exercised by the session check's
  scripted actions (about 18 min of them on the real compositor) and by
  looking at screenshots, not by a person.
- No HiDPI or fractional scaling: this screen is scale 1.0. Scale 2 numbers
  are the benchmark's simulation.
- Seed 8 with `G_DEBUG=fatal-criticals` on Xvfb X11 is intermittent after the
  Duplicate fix: 2 of 4 runs through `session-check.sh` failed the last check
  ("the window closed, and was freed with its circuit (1 circuit windows alive,
  1 before)", 0 expected) and 4 of 4 direct runs passed, including one with
  debug output that would have listed the window left. No GTK warning. I could
  not capture a failing run with output, so I don't know whether a second
  window is left over or the close takes longer than the check waits (1.5 s).
- The scope dock being blank in `--screenshot` is unresolved (above).

## Left to look at

- Select-all drag (12 ms a frame at scale 1, 21 ms at scale 2) is the slowest
  case; `SMOOTHNESS.md` step 3 (labels as cached glyphs) is where it would
  come from.
- A person on this laptop should try Wayland dragging at 1366x768 by hand.

## Found by hand, then fixed (owner testing, same laptop)

- A on the keyboard put the gate in the top left corner until the mouse moved
  (GDK reports 0, 0 for the pointer on Wayland after the picker closes). It
  uses the canvas's own last pointer position now.
- Dragging a gate in from the side panel, C did nothing until the gate was
  dropped and picked up again (focus stayed on the panel). The canvas takes the
  focus when the gate lands on it.
- Ctrl+Tab cards: works; the owner would like them to look more like the Mac
  app's. Not changed; to be talked through first.

The first fixes for A and C (71d3b78, 1a552bb, a8e2e8b) didn't fix them on
Wayland. Debug output from the owner's run showed why: after the picker
closed, the canvas thought the pointer was outside and GDK's answer was stale
(A), and C's key did reach the canvas but needed the canvas to have seen the
pointer come in, which a drag from the panel never gives it. The fix that
worked is in the commit after the Ctrl+Tab one; the owner confirmed both on
Wayland. Neither is covered by an automated check.

Ctrl+Tab (c3b338d): rounded, shadowed, slightly see-through panel that fades,
cards drawn at each tab's own zoom, an accent ring, and a fix for a resting
pointer picking a card. Session check passes on Xvfb and on Wayland; the look
is for the owner to judge.
