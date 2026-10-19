# Core diagnosis for v4 (2026-10-14)

Question: how sound is the shared C++ core (`src/gui`, `logic`, `format`, `mac/CedarCore`)
that every app and the web build run on? Method: a random-edit fuzzer through the C
interface (`mac/Tools/fuzz_edit.cpp`, run with `mac/Tools/fuzz-check.sh`), built with
AddressSanitizer + UndefinedBehaviorSanitizer. Each seed is a new document and N random
operations (place a part, drag a part or box, draw a wire between two pins, delete,
rotate, nudge, copy/paste, undo/redo, add/delete/close page, Straighten, Tidy, lock,
simulate, click), then the document is saved, reloaded and saved again.

Environment variables for the fuzzer: `CL_FUZZ_TRACE=1` (print every operation),
`CL_FUZZ_INVARIANT=1` (stop at the first operation that leaves a wire holding a gate that
is gone), `CL_FUZZ_BOTH=1` (also: wire and gate must agree on who owns a pin),
`CL_FUZZ_REACH=1` (also: every wire must reach each pin it holds), `CL_FUZZ_DUMP=<dir>`
(keep both saved texts when save/reload/save differs), `CL_FUZZ_DUP=1` (no pin held or
recorded twice on a wire, no crossing recorded twice), `CL_FUZZ_WATCH=<wire>:<gate>:<pin>`
(print that wire after every operation), `CL_FUZZ_NOSIM=1` (the save comparison leaves out
`CURRENT_VALUE` and `PAUSE_SIM`, which the simulation writes and a reload recomputes).

Round 3 (2026-10-16) widened it to what a student does in a ten-minute session:
`CL_FUZZ_WIDE=1` adds gate settings (in-range numbers, labels and TO/FROM names
with quotes, brackets, unicode, long text), wire segment drags, C while dragging
and Escape, parts and pastes that follow the pointer (dropped, Escaped, or left
floating while other operations happen), Straighten and Tidy with and without a
selection (keep, discard, settle by another edit or Undo), page links and TO/FROM
pairs across pages, add/close/move/delete pages in the middle of a drag, copy on
one page and paste on another, Delete while the simulation runs, clicks on
switches, keypads, clocks and pulses, runs of 5-35 undos, right-click Disconnect,
click-started wires, save-and-reopen mid-session, memory contents, Step Clock and
truth tables. `CL_FUZZ_SIM=1` runs the simulation through odd seeds (16.7 ms
ticks between pointer events and between operations). `CL_FUZZ_ODD=1` also
sends out-of-range settings and two-line text. Two invariants:
`CL_FUZZ_REOPEN=k` (every k operations the saved text reopens with no notices
and saves the same) and `CL_FUZZ_UNDOALL=k` (every k operations and at the end,
undo everything then redo it: the same circuit; segment numbering, float noise
under 0.001 and what clicks and the memory editor set are left out, since those
aren't undo steps). Several start files may be given (seed s starts from file
s % count); `CL_FUZZ_CHAIN=1` starts each seed from the previous seed's save;
`CL_FUZZ_SAYSEED=1` prints each seed as it starts. `CL_FUZZ_DUP=1` now also
checks that each gate and wire is on one page only.

## Found and fixed, round 2 (2026-10-15)

Before: seeds 1-150 x 250 ops with REACH+BOTH+INVARIANT: 20 failed (7 paste reach,
13 save/reload/save). After: 3 failed, all simulation state (0 with `CL_FUZZ_NOSIM=1`);
seeds 151-300: 0; Lab5 as the start document, 60 seeds: 0 (with NOSIM).

3. **A part whose library name has a space could not be pasted**
   (`src/gui/command/cmdSerialize.cpp`, `parse(CreateGate)`). "3x8 Decoder Chip" was read
   as type "3x8", so nothing was created, and the next pasted part got the same id: its
   wires then named pins it doesn't have (read as (0,0)). The last two words are now x
   and y and the rest is the name; the text written is unchanged.
4. **A wire on a rotated part fell into two dots** (`src/gui/guiWire.cpp`,
   `mergeSegments`). Rotation leaves pins a few millionths off the grid; the jog
   between two such runs was removed as zero length (EQUALRANGE) but the runs were only
   joined when exactly in line. Same-channel is now judged within EQUALRANGE too.
5. **Zero-length segments left by trimming** (`mergeSegments`): cleared only at the
   start, so the reload's merge removed them and the file changed. One more pass when
   one is left. Also crossings deduplicated after ids are merged (a doubled `(cross)`).
6. **Pins sharing a spot recorded twice** (a RAM's DATA_IN_n / DATA_OUT_n: connecting
   one connects both). Copy wrote a connectwire for each (`klsClipboard.cpp`), deleting a
   wire disconnected each (`cmdDeleteWire.cpp`, so Undo connected each twice), and a wire
   could be drawn from one to the other (`CircuitEdits.cpp`). Each now happens once.

## Found and fixed (uncommitted on this branch)

1. **Uninitialised read** (`include/gui/wireSegment.h`): a default-constructed
   `wireSegment` left `verticalSeg` and `id` uninitialised (UBSan: "load of value 190,
   which is not a valid value for type 'bool'"). Now `= false` / `= 0`.
2. **A pin could belong to two wires** (`src/gui/CircuitEdits.cpp`, `gateWireConnection`).
   Joining a pin that was already on a *different* wire (drawing a wire from one wired
   pin to another, or dropping a pin on another wire) connected it anyway, so two wires
   held the same pin and the gate knew only one of them. Deleting the gate then detached
   one wire and left the other holding a gate that no longer existed; the next move
   hit `assert(gate != nullptr && "wire holds a connection to a gate that no longer
   exists")` in `guiWire::gateOf` and the app aborted. (Asserts are live in the Mac
   build: no `-DNDEBUG`.) Now such a connection is refused.
   Reached from the app by: wire between two already-wired pins, delete one of the gates,
   then move anything attached to that wire.

After both fixes: 175 seeds x 250 operations, no crash, no sanitizer report, no pin/wire
ownership disagreement.

## Not fixed: wire geometry drifts from the pins

About 1 random session in 5 (37 of 175) ends with save -> reload -> save giving different
text, and with `CL_FUZZ_REACH=1` the cause shows up live: after a drag that starts on a
part, a wire's segments no longer reach one of its pins (7 of 9 failing seeds checked).
Reloading repairs it (the loader snaps ends to pins), so the user sees a wire that was
detached on screen come back after Save + Reopen. The code is the original wx-era
segment-map model in `src/gui/guiWire.cpp` (segments, intersection maps, merging,
`updateSegDrag`, `mergeSegments`), which is intricate and keeps connection records in
three places (wire, segment, gate).

Caveat: the fuzzer is far rougher than a person (parts dropped on top of wires and each
other, 8-pin and bus parts, select-all then drag). It overstates the rate, but it proves
the model can drift, and a drifting wire is the "glitch" you can see.

## What this says about the core

- Memory safety: one real defect found and fixed in about an hour of fuzzing; after that
  nothing in 175 long sessions. The core is not rotten.
- Logic: the wire model is the weak part. Ownership of a connection lives in three
  places and has no single source of truth, which is how both the double-owned pin and
  the drifting wire arise.
- A rewrite of the whole core is not justified by this. A rewrite of the wire geometry
  model (one source of truth: nets of pin ids; geometry derived and re-derived from
  pins, never edited in place) probably is, and the fuzzer is the test that would prove
  it: the target is zero reports with `CL_FUZZ_BOTH=1 CL_FUZZ_REACH=1`.

## Next

- Decide: repair the drift in place (find the reroute step after a part drag that skips
  a wire) or replace the model.
- Run longer sessions (2000 operations) and the other fixtures as start documents:
  `fuzz-check.sh <seed> <count> <ops> format/tests/fixtures/Lab5.cdl`.
