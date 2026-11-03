# Tester report, 2026-11-02: wires on three or more parts

A tester on the normal Mac release reported, in his words: "issues with
CedarLogic not loading connections properly and crashing when three or more
elements were connected by one wire, and with it unexpectedly not saving."
The owner saw a related glitch copying and pasting a complex circuit that had
one wire connected to several parts.

**Short answer: every one of these reproduces on 0.3.4, and none of them
reproduces on main.** All the fixes are already in the 0.4 beta. The normal
channel is still 0.3.4, so normal testers have none of them yet.

## Which build the tester has

The normal channel's newest release is **0.3.4** (`v0.3.4` in
CedarLogic-Releases, published 2026-10-04, commit `a5a30c97`, "Build from
Formula's NOR only builds negated products right"). Every release since
(0.3.5 to 0.3.19, and 0.4 on 2026-11-01) is a beta (pre-release), so a normal
tester can only have 0.3.4 or an older build. None of the commits below is in
0.3.4. All of them are in the 0.4 beta (`f5583f09`) and on main.

## How it was checked

- **0.3.4:** the engine at `a5a30c97` (`src/gui`, `logic`, `format`,
  `mac/CedarCore`), built the way 0.3.4's own `asan-check.sh` built it, with
  asserts on as in the Mac app, plus ASan and UBSan. On top of that, a small
  driver that uses only C API calls 0.3.4 and main share (`cl_edit_press`,
  `drag` and `release` to draw wires and drag parts, `cl_edit_copy`, then
  `cl_edit_paste` + `cl_edit_float_begin` + hover + click, which is exactly
  the app's Cmd-V, plus Save, reopen, Straighten and Tidy Up). After each step
  it checks that wires and gates agree on every connection, that a save
  reopens with the same nets, and that save, reopen, save gives the same text
  (simulation values left out). The app half of the saving bugs was read from
  0.3.4's Swift source.
- **main** (`1b239423`): the same driver against the headless engine
  (`mac/Tools/headless`, ASan+UBSan), `ctest` (5/5), `core_check` with the new
  cases below, and fuzz_edit sweeps from the fixtures with wires of three or
  more ends (results under "Evidence").

The circuits used:
- One switch wired to four AND inputs, joined in each of the four ways the app
  joins a pin to a wire (from the switch, onto its wired pin, from its wired
  pin, onto the wire itself): one wire with five ends.
- Two separate wires joined at a pin.
- The fixtures with wires of three or more ends: Lab5, lab6, jmSGTZM,
  Broadcast, Routing Backwards, web-online2, tidy-adder, tidy-inverters,
  practice, and drag-leaves-a-dot, which has RAMs. On each: open, save,
  reopen; select all, copy, paste and drop to the right; box-select half,
  copy, paste below; undo and redo; nudge; Straighten.

## The table

| # | Symptom (what a student sees) | On 0.3.4? | On main? | Fixed by |
|---|---|---|---|---|
| 1 | **Crash with a wire on three or more parts.** Draw a wire from a pin that is already wired to a pin (or onto a wire) that belongs to a *different* wire. 0.3.4 puts that pin on both wires, which makes a wire with three ends. Delete one of the gates, then drag anything attached: the app quits (`assert` in `guiWire::gateOf`, "wire holds a connection to a gate that no longer exists"). | **Yes**: SIGABRT at the first drag after the delete. | No: the second connection is refused. Delete and drag are fine. | `5c98f918` (2026-10-16) |
| 1b | **The same circuit crashes again after reopening.** 0.3.4 saves the pin as claimed by both wires and reads it back the same way, so the next delete and drag crashes again, every session. | **Yes**: opened the 0.3.4 save, deleted gate 1, dragged: SIGABRT. | No: the file opens with one claim refused and a notice ("Wire 2 was removed, because it connected fewer than two pins"). Deleting any gate and dragging the rest is safe. | `8032916a` (2026-10-28, CORE-HARDENING #11) |
| 2 | **Crash after a paste.** Cmd-V, C while the paste floats (connect while moving), Cmd-Z, click, then drag the part C connected to: the app quits (same assert). | **Yes**: SIGABRT. | No: Undo first puts the floating paste back, then takes it away. | `7ccd12e4` (2026-10-16) |
| 3 | **Connections lost after Save and reopen** on a wire with three or more ends, after Straighten, Tidy Up or Build from Formula. When the page router finds no way through, these fall back to the simple router. Its L-bend case (the wire's first two pins facing different ways) routed only those two pins, so the other pins stayed connected on screen but the save didn't write them. | **Yes**: a four-end wire went through the fallback router, then save and reopen: 4 connections went down to 2. In the app this needs the page router to fail first: 15 of 160,843 runs in the straighten lab. Straighten and Tidy on the small circuits and the fixtures didn't hit it. | No: same case, 4 and 4. | `5dbb5853` (2026-10-08) |
| 4 | **Paste glitch: a pasted wire splits in two.** Float a copy where one wire holds two pins of one part and one of another, then drop it: the copy's wire comes out in two pieces and is saved that way. | **Yes**: "wire is in 2 pieces (3 ends)" after the drop (fuzz seed 31's circuit). | No: one piece. | `f99c6941` (2026-10-20) |
| 5 | **Paste glitch on RAM data pins.** A copied wire on a RAM's DATA_IN_n/DATA_OUT_n spot (which always has three or more ends) is pasted with the pin recorded twice. The connections then change on Save and reopen. | **Yes**: drag-leaves-a-dot, copy all, paste: 138 connections became 280 (should be 276). After save and reopen the doubles were gone (276), and save, reopen, save differed. | No: 276, and the save is stable. | `5c98f918` (2026-10-16) |
| 6 | Paste dropped exactly where it was copied. | Partly: UBSan flags uninitialised wire-segment fields, and save, reopen, save differs. It didn't crash here. | No: clean. | `5c98f918`, `bb63022b` |
| 7 | **Crash opening a damaged file** (a wire whose `(ids` lost a byte). The app reopens the last circuit at launch, so it crashed at every launch. | **Yes**: null reference, then SEGV, while opening. | No: it opens with a notice, and the other parts and wires load. | `dd878333` (2026-10-19) |
| 8 | **Not saving after a long session.** The engine keeps 500 undo steps. 0.3.4's app marks the window edited only when the step count grows, and at 500 it stops growing. From then on the window is never marked edited, so autosave, closing the window and quitting all skip the circuit, and every change after that point is lost. About ten minutes of work fills the history. | **Yes**: in the engine, the count stays at 500 for the last 21 of 521 edits. The 0.3.4 app source (`CanvasView.mirrorNewSteps`, and `scheduleAutosave`, which needs `isDocumentEdited`) shows the rest. | No: the app counts the steps made (`cl_edit_steps_made`), not the steps held. | `bafd06fa` (2026-10-16), `54667b83` (2026-10-20) |
| 9 | **Not saving: a setting changed last.** In 0.3.4 a change made in a part's settings never called `edited()`, so if it was the last edit before closing or quitting, it was lost. | **Yes**, from the 0.3.4 source: `InspectorView` calls `setSetting`, then only redraws. | No | `84ecba37` (2026-10-20) |
| 10 | **Not saving: Undo or Redo made last.** In 0.3.4 Undo and Redo didn't schedule an autosave. | **Yes**, from the 0.3.4 source: `refreshAfterHistory` has no `scheduleAutosave`. | No | `b97fbca0` (2026-10-20) |
| 11 | A crash (1, 1b, 2, 7) also loses whatever autosave hadn't yet written, up to about 2 s of work. In case 1b, reopening the circuit and editing crashes again. | Yes, follows from 1 to 7 | No (the crashes are gone) | as above |
| 12 | Closed circuit windows kept simulating: slower, warmer, more battery. This isn't data loss. | Yes (the retain cycle) | No | `1576fa3c` (2026-10-19) |
| 13 | Mac "window undo out of step" (fixed yesterday) | **Can't happen on 0.3.4.** It needs Draw mode (`b2f4f22`, first in the 0.3.11 beta) and a Tidy Up preview. | No | `3fc821bb` (2026-10-31; not in the 0.4 beta build, on main) |

Not reproduced on either build, so they don't explain the report:
- Simple fan-out circuits: one switch to four or more gates, joined each way,
  dragged, copied, pasted (also pasted on top of the original), undone and
  redone, Straightened, Tidied (both modes). Every connection survives save
  and reopen on 0.3.4 as well as on main.
- Opening ordinary files. For every legacy (wx XML) fixture, the connections
  each gate and each wire segment declares are exactly the connections loaded
  on both builds. 88 to 1,157 connections a file, nets identical.
- Export for older CedarLogic (v2 and v1.x XML) with two five-end wires, read
  back: every connection on both builds.
- The (connect) line order in a save. 0.3.4 can write a segment's `(connect)`
  lines in a different order after a reopen (seen on Lab5). The connections
  are the same, so it is cosmetic. Main writes them in a fixed order
  (CORE-HARDENING #26).

## What the tester should be told

> Thanks: this was real, and it's fixed. In 0.3.4 a wire joining three or more
> parts could end up recording one pin on two wires (most often when you draw a
> wire to a pin that already has a different wire on it). Deleting a part on it
> and then moving something made the app quit, and because the file saved that
> way, the same circuit could quit again after reopening. Separately, 0.3.4 could
> stop marking a circuit as changed after a long session (about 500 edits), so
> autosave and quitting didn't save it, which is likely the "unexpectedly not
> saving". Copy and paste could also split a pasted wire or double up a RAM
> data connection, and in rare cases Straighten or Tidy Up dropped a pin from a
> wire at the next save.
>
> All of that is fixed in the 0.4 beta: Settings > Testing group > Beta to get
> it now, or wait for 0.4 on the normal channel. A circuit saved by 0.3.4 that
> had the bad connection opens safely in 0.4. One of the two wires on that pin
> is removed, with a note, so check that pin and redraw the wire if it's
> missing. If you can, please send the .cdl that crashed: it will tell us which
> of these it was.

## Two more safety nets (branch `fix/multi-wire-read-2026-11-02`)

A parallel code read of the same report found two places where a drifted
drawing (one that no longer reaches a pin it holds) could still lose a
connection on main. No fuzz run on main has produced such a drawing, so these
guard against it rather than fix a path anyone has seen. That branch adds them
as CORE-HARDENING #41 and #42, each with a core_check case:

| # | Symptom | 0.3.4 | main | Fix |
|---|---|---|---|---|
| 41 | A wire connected on screen comes back without one of its pins after Save and reopen. Save writes the drawing, and the loader joins only the pins that the drawing records. | Yes, whenever the drawing drifted (the drift was common on 0.3.4: V4-CORE-DIAGNOSIS, about 1 random session in 5) | Not seen, but possible if a drawing ever drifts | `fe294600`: save writes an unsound wire routed from its pins (on a copy) |
| 42 | Disconnect on such a wire left it drifted, or trimmed an unrelated run | Yes (same code) | Not seen | `7d7afb29`: trim only from the segment that holds the pin; Disconnect redraws a wire it leaves unsound, and Undo restores it |

Those two are not merged. This branch (`fix/multi-wire-2026-11-02`) has this
report and the core_check cases only, and changes no engine code.

## What's still open on main

- **Joining two existing wires is refused without a word.** Drawing from a
  pin on one wire to a pin on another wire (or dropping it onto the other
  wire) used to put the pin on both wires: the crash in row 1. Since
  `5c98f918` main refuses it. That is safe, but nothing happens and nothing is
  said, so a student who builds a three-part net from two wires they already
  drew finds that it "won't connect". The way that works is to draw from a
  free pin onto the wire. **Suggested:** merge the two wires as one undo step
  (move the second wire's pins onto the first, keep the first's shape and
  route the rest, check bus widths match), or at least show a notice ("That
  pin is already on another wire: delete one wire first"). Not changed here:
  it is a behaviour decision for all three apps.
- **The note shown when a 0.3.4 file's double claim is repaired** says only
  "Wire 2 was removed, because it connected fewer than two pins". It could name
  the pin that was claimed twice, so the student knows which connection to
  redraw.
- **The normal channel still ships 0.3.4.** Every fix above reaches normal
  testers only with the next normal release (0.4).
- Not something we can test: the tester's own file. If it arrives, open it on
  main and run `fuzz_load gatedefs 0 0 0 file.cdl` on it to check.

## Evidence

0.3.4 (`a5a30c97`), ASan+UBSan, asserts on:
- Row 1: `wire 2 holds 4.IN_0 but the gate says 1`, then at the first drag
  after deleting gate 4: `Assertion failed: (gate != nullptr && "wire holds a
  connection to a gate that no longer exists"), function gateOf,
  guiWire.cpp:94`, exit 134.
- Row 1b: the save of that circuit, reopened, gate 1 deleted, the rest
  dragged: the same assertion.
- Row 2: `C connected 1`, then Cmd-Z left the paste floating
  (`floating 1, 6 gates, 2 wires`), then the drag: the same assertion.
- Row 3: `every connection survives save and reopen ... 4 connections;
  reopened ... 2 connections` (live `1.IN_0,2.OUT_0,3.IN_0,4.IN_1`,
  reopened `1.IN_0,2.OUT_0`).
- Row 4: `wire 1 is in 2 pieces (3 ends)` after the drop.
- Row 5: `live only: 34.IN_0,41.DATA_IN_5,41.DATA_IN_5,41.DATA_OUT_5,41.DATA_OUT_5`,
  `reopened only: 34.IN_0,41.DATA_IN_5,41.DATA_OUT_5`.
- Row 7: `reference binding to null pointer` (vector.h:408), then SEGV.
- Row 8: `after 521 edits: undo count 500, 21 edits left the count unchanged`.

main (`1b239423`) plus this branch's `core_check` cases:
- Every scenario above passes: no crash, wires and gates agree, every
  connection survives save and reopen, save, reopen, save stable apart from
  the free-running counters' `CURRENT_VALUE`, which is a known limit.
- `ctest` (headless, ASan+UBSan): 5/5. `core_check`: all passed, including the
  new "one wire on three or more parts (the 2026-11-02 report)" section, with
  14 checks: the five-end fan-out drawn four ways, then dragged, pasted,
  pasted on top, undone and redone; a join between two wired pins refused, and
  delete plus drag safe; the file 0.3.4 saved with the double claim; Cmd-V, C,
  Cmd-Z, click, drag; Lab5 copied, pasted, dropped and reopened.
- fuzz_edit, every check on (CHECK BOTH REACH INVARIANT DUP TREE NOSIM WIDE SIM
  UNDOSTEP REOPEN=25 UNDOALL=50), from Lab5, lab6, jmSGTZM, tidy-adder,
  tidy-inverters and web-online2: seeds 1001-1060 x 300 ops, 150 s cap:
  38 passed, 0 failed, 0 crashed, 22 unfinished (the two 1,100-connection
  files, which are slow under ASan). The big two again, seeds 2001-2016 x 120
  ops, 400 s cap: 16 passed, 0 failed, 0 crashed, 0 unfinished.
- Mac app: `mac/build.sh` builds at this branch. launch-selftest, --undo-check and
  session-check were not run this round (dropped to save disk). The app-side
  fixes in rows 8 to 10 carry their own checks from when they landed.
