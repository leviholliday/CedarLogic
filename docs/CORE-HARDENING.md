# Core hardening (2026-10-28)

The shared engine (`src/gui`, `include/gui`, `logic`, `format`, `mac/CedarCore`) is
what the Mac, Windows and Linux apps all run on. This round set out to make it
never crash, hang or quietly change a circuit, whatever file is opened, whatever
is pasted, and whatever a student does. It follows on from
`docs/V4-CORE-DIAGNOSIS.md` (rounds 1 to 5), which explains the edit fuzzer.

In short: three new kinds of check (a file fuzzer, a self-check of the circuit,
and a set of regression cases) and much longer fuzz runs found 39 bugs. All are
fixed; nearly all have a case in `core_check` or the clipboard tests, and the
rest (a few that need a long session's history to happen) are covered by the
fuzz runs. Everything runs in CI on every push, including ThreadSanitizer on the
engine's threads. The engine is now also deterministic: the same edits give the
same circuit every run (before, which of two close pins or two meeting runs a
click took depended on memory addresses), which is what made a few fuzz
failures impossible to reproduce.

## The wire question: repair or replace?

A wire is stored three ways: the list of pins the wire holds, each gate's record
of which wire is on each of its pins, and the wire's drawing (segments, each
noting the pins it ends on and the segments it crosses). A save writes the
drawing, and opening rebuilds the other two from it. The old trouble, a wire
that stops reaching its pin until Save and Reopen, is the drawing drifting away
from the pins.

**Decision: repair in place, with the pins as the one source of truth and a
safety net that redraws a wire from its pins whenever its drawing is wrong.**
A full replacement (geometry always derived from pins) was not done, because:

- The drawing is not only derived: students drag segments, and Straighten,
  Tidy Up and the page router all produce shapes worth keeping. A model that
  re-derives everything would throw that away, or need a second store for
  hand-made shapes, which is the same problem again.
- Every front end, the file format, undo/redo and copy/paste use the segment
  model. Replacing it means rewriting and re-testing all of them on three
  platforms; repairing it changes nothing outside the engine.
- The fuzzer shows the incremental model is right almost always. What was
  missing was a precise statement of "right" and a fallback when it isn't.

So the work was:

1. **One definition of a sound drawing** (`guiWire::isSoundShape`): every pin
   the wire holds is recorded once, on a segment that reaches it, and nothing
   else is recorded; each segment runs along its own direction; each crossing is
   on both segments, once, and names a segment that exists; no segment leads
   nowhere or has no length; the segments touch as one piece; two runs along
   one line that meet are one run. This is exactly what a reopen would produce,
   so a sound drawing saves and reopens the same.
2. **One self-check of the whole circuit** (`cl_document_check`): the three
   stores agree (a wire's pins, the gates' records, the drawing), every wire's
   drawing is sound, nothing is on two pages, every position is a number. The
   fuzzers run it after every step.
3. **A safety net at every door**: a drawing that isn't sound is redrawn from the
   pins when a file is opened, when text is pasted, and when a part move ends.
   The user sees a tidy wire where they would have seen a broken one.
4. **Fixes where the fuzzers found the incremental model going wrong** (below).

Result: zero reports from the edit fuzzer with every check on, over the runs in
the numbers section.

## Bugs found and fixed

"Seen as" is what a student would notice.

### Files (found by the new file fuzzer)

| # | Seen as | Cause | Fix |
|---|---|---|---|
| 1 | Opening a crafted .cdl overwrites any file on the computer, or reads one into the circuit | A memory part's `READ_FILE`/`WRITE_FILE` settings in a file were carried out on open | They are actions the user takes, never taken from a file or paste (`CircuitParse.cpp`, `cmdSetParams.cpp`) |
| 2 | App hangs, then runs out of memory, opening a file | A bus width of -1 or millions declared that many pins one by one | Widths held to 0..32 (`logic/include/logic_params.h`) |
| 3 | Same, for a decoder | 2^n outputs: 32 inputs is four billion | Decoders held to 12 inputs |
| 4 | Crash opening a file (undefined behaviour) | Positions of `nan`/`inf`, or huge ones, reached the collision grid and float maths | NaN and infinity refused with a message; parts far off are brought to the edge with a notice; the grid saturates |
| 5 | A file that saved then would not open ("number out of range") | A tiny angle (1e-320) was written in a form the reader refuses | Underflow reads as 0; angles kept in [0, 360) in the form a save writes |
| 6 | Save after opening a damaged file is unreadable | A NUL byte in a name cut every later save short | NUL bytes are dropped on reading |
| 7 | Two parts with one id: one silently replaced, wires to the wrong part | Duplicate and out-of-range ids were merged | Left out with a notice; ids normalised ("007" is gate 7) |
| 8 | Crash on open | A segment id of LONG_MAX overflowed the next id | Segment ids out of range are renumbered |
| 9 | Crash (version or page number too big for an int) | Unchecked conversions | Range-checked |
| 10 | A wire drawn at the origin, saved for ever | A file named a pin the part doesn't have | Not connected |
| 11 | A connection lost on the next save | A pin claimed by two wires, or a pin no segment recorded | Refused / wire redrawn from its pins |
| 12 | A wire that reaches nothing until reopened | The file's drawing didn't match its pins | Redrawn from its pins on open, with a notice |
| 13 | Taking back a paste of thousands of parts takes minutes | Each removal re-ran the whole collision update | Removal updates only what it touches |

### Copy and paste (clipboard text can come from anywhere)

| # | Seen as | Cause | Fix |
|---|---|---|---|
| 14 | **App quits when pasting a label with two lines** (the backslash/newline report) | The clipboard format is one line per command; a newline in a value cut it, and the half-line threw `out_of_range` | Values escape `\`, tab and newline; a plain backslash reads as itself |
| 15 | App hangs on a damaged paste | A line cut after "connection" looped for ever; counts past the line's end read on | Reads stop at the line's end |
| 16 | App quits pasting a TO/FROM named e.g. `bus99999999999999999999` | Counting the name on used `stoi` | Counts on as text |
| 17 | Damaged paste wires a part that wasn't pasted (gate 0) | Unknown ids were looked up with `[]`, giving 0 | They name nothing |
| 18 | Part pasted over another; wire left behind when the paste moves | A doubled id reused the first's | Every pasted part and wire gets a new id; everything pasted is selected |
| 19 | A wire with no ends, or a crash, after a damaged paste | `createwire` with no ids | Refused |
| 20 | Pasted wires drawn wrongly | The pasted drawing wasn't checked | Unsound pasted wires are redrawn, as a step of the paste |

### Editing and wires

| # | Seen as | Cause | Fix |
|---|---|---|---|
| 21 | **A wire falls into two pieces after dragging a part** (the known drift) | Several pins of one part on one wire stretch one at a time; the merge then dropped the run joining them | A move's end redraws any wire left unsound (`Editor.cpp commitMove`) |
| 22 | A zero-length bit of wire after a drag, gone after reopening | As above | As above |
| 23 | Disconnect on a RAM's data pin leaves a wire to nothing | DATA_IN_n and DATA_OUT_n share a spot; both came off a 3-pin wire | The whole wire goes when fewer than two ends remain |
| 24 | Wire saved twice over the same crossing, different after reopening | Float noise kept one crossing under two positions | Crossings kept once, where the crossed run lies |
| 25 | Straighten makes a wire whose middle vanishes | The straight-trunk route could give a branch the trunk's id 0 | Branches skip the trunk's id |
| 26 | Same file saved twice, different bytes | Pin order in a segment wasn't fixed | Written in a fixed order |
| 27 | Save, reopen, save gives different numbers (18 vs 17.99999428; -20.5357 vs -20.5358) | A rotated part's pins sit a few millionths off the grid; a reopen trims wire ends to them; the part and the wire's end were rounded apart | Positions kept, taken in and saved to a ten-thousandth of a grid unit (`savedPrecision`) |
| 28 | A pasted wire left stretched and broken | A wire on a pasted part locked in place isn't carried by the paste's move | Redrawn from its pins, in the paste's undo step |
| 36 | A click on a pin where two pins are close picks one or the other, differently each time the app runs | The pin was the first hit in a set ordered by memory address | The nearest pin (ties: by name) (`guiGate::checkHotspots`) |
| 37 | Dragging a wire at a corner drags one run or the other, differently each run | Same: the first segment hit, by address | The nearest segment (ties: the lowest id) (`guiWire::startSegDrag`) |
| 38 | App stops responding for minutes on Connect while moving, or Connect Nearby, with a few thousand parts | Every free pin of the moving parts against every free pin on the page, each looked up by name | The page's free pins placed once in a grid; each moving pin looks only at the cells near it. Same result, in the same order (checked against the old search at every call in 79 fuzz sessions) |

### Simulation

| # | Seen as | Cause | Fix |
|---|---|---|---|
| 29 | App quits when a counter with MAX_COUNT at the largest number counts up | `maxCount + 1` wrapped to 0, then divided by it | Handled |
| 30 | Undefined behaviour on every step of a priority encoder with no inputs | Output width worked out as log(0) | Uses the width already set |
| 31 | RAM memory grows without bound; huge saves | Reading an address made an entry for it | Reads leave memory alone |
| 32 | App quits showing a wide register | More than 8 hex digits asked of an 8-digit string | Digits held to 8 |
| 33 | File differs after a Pauseulator fires | Its pause request was saved as a setting | Not saved |
| 34 | Crash on opening a RAM part with a short file name set | `substr` on a name under 3 characters | Checked |
| 35 | Reads past a bus (undefined behaviour, can crash) for an adder, register or equivalence gate with 0 or 1 inputs from a file | Code assumed at least the library's width | Guarded |

### Sync (found on a Windows PC)

| # | Seen as | Cause | Fix |
|---|---|---|---|
| 39 | `--sync-test` fails 1 run in 3 on a Windows PC (s47, line 2014) | Moving a circuit's folder to `.Trash` failed for a moment: Windows can't rename a folder while any file in it is open, and Defender or the indexer opens newly written files. The test ignored the failure; the circuit stayed and was sent again after the reset. (Skipping the trash on a Mac gives the same failure at the same line.) | `files::rename` and `writeAtomic` retry for about a second while the source is there; the test's `remove()` fails at once if the trash does. 50 runs of s47 and 5 of the whole suite pass |

### Already fixed before this round

The two fixes in the hand-over patch (`wireSegment` fields left uninitialised,
and a pin able to belong to two wires, with its Escape companion in
`Editor.cpp`) were already on v4/mac when this round started, as were the
fuzzer and `bench_drag` it described, so there was nothing to apply.

## How it is checked

All of these build from one CMake project with no graphics at all
(`mac/Tools/headless`, using `CL_HEADLESS`: a drawing scene that draws nothing):

```bash
cmake -S mac/Tools/headless -B build/headless -DCL_SANITIZE=ON
cmake --build build/headless
ctest --test-dir build/headless --output-on-failure
```

- **`core_check`**: a regression case for every bug above. Fast (seconds).
- **`thread_check`**: Check My Circuit's clock-by-clock check, the engine's one
  off-main-thread path, as the apps run it: many jobs at once on worker threads
  while the main thread keeps their circuits running, opens, edits, saves and
  closes other documents, and cancels some jobs mid-run. Each result must match
  the same check run alone. CI runs it under ThreadSanitizer (`-DCL_TSAN=ON`),
  with the sync engine's self-test (`TSAN=1 mac/Tools/sync-selftest.sh`).
- **`test_cmdserialize`**: the clipboard line format, including escapes and
  damaged lines.
- **`fuzz_edit`** (from round 1): random editing sessions. New:
  `CL_FUZZ_CHECK=1` runs `cl_document_check` after every operation.
- **`fuzz_load`** (new): takes real files from `format/tests/fixtures` and
  damages them thousands of ways (flipped bytes, cuts, repeats, odd numbers,
  duplicate ids, deep nesting, hostile text, enormous files), then opens,
  checks, simulates, draws, copies, pastes a damaged copy of the clipboard,
  undoes, saves, reopens and saves again. `fuzz_load gatedefs 0 0 0 file.cdl`
  reruns one kept file.
- **`fuzz-sweep.py`**: runs either fuzzer over many seeds in parallel with a
  time cap, keeps the output of any seed that fails:

```bash
CL_FUZZ_CHECK=1 CL_FUZZ_BOTH=1 CL_FUZZ_REACH=1 CL_FUZZ_INVARIANT=1 \
  python3 mac/Tools/fuzz-sweep.py build/headless/fuzz_edit 1 200 2000
CL_FUZZ_DUMP=/tmp/kept python3 mac/Tools/fuzz-sweep.py build/headless/fuzz_load 1 300 100 \
  format/tests/fixtures/*.cdl format/tests/fixtures/pagelinks/*.cdl tests/fixtures/*.cdl
```

**CI** (`.github/workflows/core-hardening.yml`, every push, Linux, about 8
minutes): the logic/ and format/ suites, the headless build under ASan+UBSan,
`ctest`, a few minutes of each fuzzer, two minutes of libFuzzer on the loader,
the checks again with asserts compiled out (as a release build has them), and
a ThreadSanitizer job (`thread_check`, the sync self-test).
Anything that fails is uploaded as an artifact to rerun.

## Numbers

All on this Mac (10 cores), ASan+UBSan build, every check on
(`CL_FUZZ_CHECK BOTH REACH INVARIANT DUP TREE NOSIM`; "wide" adds `WIDE SIM
UNDOSTEP REOPEN=25 UNDOALL=50`). "Unfinished" is a seed past its time cap.

| Run | 2026-10-28 (tip 6229520) | 2026-10-31 (this round's end) |
|---|---|---|
| fuzz_load, files | 1000 seeds x 100: all passed | 300 seeds x 100: all passed |
| fuzz_edit basic, new circuit | 300 x 2000: 296 passed, 4 unfinished | (not rerun; the 4 pass without sanitizers in 9 to 15 min) |
| fuzz_edit basic, fixtures | 150 x 2000: 148 passed, 2 unfinished | (not rerun) |
| fuzz_edit wide, new circuit | 200 x 300: 198 passed, 2 unfinished | 200 x 300: 198 passed, 2 unfinished (74, 183) |
| fuzz_edit wide, fixtures | 1000 ops: seed 21 failed, cut short | seeds 1-55 x 500: 43 passed, **1 failed (21)**, 11 unfinished |
| thread_check under TSan | (didn't exist) | 474 jobs on 6 threads, 31 cancelled mid-run: clean |
| sync self-test under TSan | (not run) | 265 of 265, clean |
| Same session run 5 times | sessions 16 and 38 differed run to run | 60 wide sessions: identical every time |
| Mac app | | `mac/build.sh` (clean), `session-check.sh`, `launch-selftest.sh`: pass |

The unfinished seeds are slow, not stuck: those profiled spend their time
pasting a page onto itself (copy, paste, collision update on undo) until it
holds thousands of overlapping parts; before this round wide seeds 74 and
183 also spent minutes in each Connect while moving (bug 38, fixed). See
Known limits.

## Still open

- **fuzz_edit wide seed 21** (fixtures, 500 operations; reproducible now the
  engine is deterministic): after op 292, Redo of Straighten Wires gives a
  wire end at -28.4989 where the step itself left -28.5 (a pin of a rotated
  part, a ten-thousandth off). Probably a route or a redo path that doesn't
  take its ends in to `savedPrecision`. Rerun:
  `CL_FUZZ_CHECK=1 CL_FUZZ_BOTH=1 CL_FUZZ_REACH=1 CL_FUZZ_INVARIANT=1 CL_FUZZ_DUP=1 CL_FUZZ_TREE=1 CL_FUZZ_NOSIM=1 CL_FUZZ_WIDE=1 CL_FUZZ_SIM=1 CL_FUZZ_UNDOSTEP=1 CL_FUZZ_REOPEN=25 CL_FUZZ_UNDOALL=50 build/headless/fuzz_edit res/cl_gatedefs.xml 21 1 500 format/tests/fixtures/{Lab5,Broadcast,lab6,jmSGTZM,web-online2}.cdl "format/tests/fixtures/Routing Backwards.cdl"`
- Bug 28 (a paste's stretched wire on a locked part) has no `core_check`
  case: a hand-made paste with a locked part didn't break the wire on the
  engine before the fix, so only the fuzz runs cover it.
- No check in CI yet that a fuzz session gives the same circuit twice (the
  determinism above was measured by hand).

## Known limits (left as they are)

- **Opening a circuit runs the simulation until it settles.** A counter on a
  free-running clock therefore moves on when a file is opened, so its saved
  value (`CURRENT_VALUE`) can differ between a save and a save after reopening.
  This is the design (the first frame shows real states), not damage; the
  fuzzers leave simulation values out of the comparison (`CL_FUZZ_NOSIM`).
- **Thousands of TO/FROM parts with one name are slow to join and take
  apart**: every join walks the whole group. A crafted file can make opening
  take seconds. Not a crash; worth a rewrite of junction groups if it ever
  matters.
- Coordinates are held to +/-1,000,000 on the way in (files, pastes). A part
  dragged past that by hand (impossible in practice) still works.
- **Undo of a huge paste onto a crowded page is slow.** The collision update
  after it rechecks every part the paste touched against its neighbours; a
  fuzz session that pastes a page onto itself over and over, until thousands
  of parts sit on top of each other, spends most of its time there (2,000
  operations take 10 to 20 minutes without sanitizers, hence the long sweeps'
  "unfinished" seeds). No real circuit is that dense.

## For the front ends

No changes are needed in the Mac, Windows or Linux apps. Added, not changed:
`cl_document_check` in `CedarCore.h` (for tests; apps need not call it), and
`CL_HEADLESS` builds, which the apps don't use. New engine files
(`Consistency.cpp`, `NullScene.cpp`, `NullScene.h`, `logic_params.h`) are
picked up by each app's existing globs; `NullScene.cpp` is empty unless
`CL_HEADLESS` is set.
