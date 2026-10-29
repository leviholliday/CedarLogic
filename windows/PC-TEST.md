# Windows PC test (real hardware)

Date: 2026-10-29. Branch tested: `ui/windows-menubar` at `54f1236`.
Raw logs (drag logs, session logs) stayed on the test PC in `codingprojects/pc-out`.

**Summary:** with a Windows SDK in place it builds, and every check passes on
real hardware. The one exception is a sync scenario that failed once in three
runs. Drag frame pacing is perfect on the real display at 100%, 125% and 150%:
a new frame at every refresh, with none missed. The old drawing path
(`CEDARLOGIC_DRAG_OLD=1`) reproduces the "jumps around" report exactly: 5
frames in 4.3 s of dragging.

## PC specs

| | |
|---|---|
| Machine | Lenovo 11T5S2Y200 (desktop) |
| CPU | 12th Gen Intel Core i7-12700 (12 cores / 20 threads) |
| RAM | 31.7 GB |
| GPU | Hybrid: Intel Iris Xe MAX 100 (discrete, 1.9 GB, driver 32.0.101.7084, drives the display) + Intel UHD Graphics 770 (integrated, driver 32.0.101.7079) |
| Windows | Windows 11 Education 10.0.26200 (build 26200) |
| Display | One monitor: Lenovo T34w-30, 3440x1440 |
| Scale | 100% (96 dpi); 125% and 150% also tested (step 7) |
| Refresh | 59.97 Hz |
| Toolchain | Visual Studio Enterprise 2026 18.10.4, MSVC 14.51.36231, CMake 4.3.1-msvc1 (bundled with VS), Ninja (bundled with VS) |

## Step 1: build

As CI does it (`cmake -S windows -B windows/build -A x64 -DCL_BUILD_TOOLS=ON`),
configuring **failed on this PC** because no Windows SDK is installed. VS 2026
was installed without one.

```
CMake Error at CMakeLists.txt:11 (project):
  No CMAKE_C_COMPILER could be found.
-- from CMakeConfigureLog.yaml:
warning MSB8003: The WindowsSDKDir property is not defined.
LINK : fatal error LNK1181: cannot open input file 'kernel32.lib'
```

Installing the SDK with winget (`Microsoft.WindowsSDK.10.0.26100`) needs
admin, and its UAC prompt went unanswered (0x800704C7, cancelled). So this test
used the SDK from Microsoft's NuGet packages (`Microsoft.Windows.SDK.CPP` and
`.x64`, 10.0.26100.9457), unpacked locally, with `INCLUDE`/`LIB`/`PATH` set by
hand. The VS generator can't use an SDK that isn't registered, so it built with
the Ninja Multi-Config generator, which writes the same `windows/build/Release/`:

```
cmake -S windows -B windows/build -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
      -DCMAKE_RC_COMPILER=rc -DCMAKE_MT=mt -DCL_BUILD_TOOLS=ON
cmake --build windows/build --config Release --parallel
```

All 166 targets built with no errors. **For a normal setup, add "Windows 11
SDK" in the Visual Studio Installer and CI's commands work as they are.**

### Running the GUI checks from a shell

`--drag-selftest` SKIPs ("CedarLogic isn't the window in front") when it's
started from a background shell. Windows' foreground lock stops the new window
from taking focus. The test PC started the app through a small launcher that
waits for the window and brings it to the front (an Alt key press, then
`SetForegroundWindow`) within the test's 1.5 s start delay. A person
double-clicking it wouldn't hit this. Every other app check below went through
the same launcher.

## Results

| # | Command | Result | Key numbers |
|---|---|---|---|
| 1 | build (Ninja + NuGet SDK, see above) | PASS | 166/166 targets |
| 2 | `--drag-selftest` | PASS, SMOOTH | 1222 moves, worst error 0.50 units, drop error 0.05; paint gap p50 3.0 / p95 6.0 / max 16.2 ms; draw 0.19 ms avg, present 0.25 ms avg; lag p95 3.6 px |
| 2 | `CEDARLOGIC_DRAG_OLD=1 --drag-selftest` | PASS, **CHOPPY** | only **269** moves reached the canvas, **5 paints**, one gap of **4037.6 ms** |
| 2 | `CEDARLOGIC_NO_GEOMETRY_CACHE=1 --drag-selftest` | PASS, SMOOTH | 1222 moves, paint gap p50 3.0 / p95 6.2 / max 15.6 ms; draw 0.21 ms avg |
| 3 | `--bench-draw 150 res/samples/practice.cdl` (24 gates, 3229x1276 px) | PASS | built every frame avg 1.97 / p95 3.10 ms; kept geometry avg 1.36 / p95 2.28 ms |
| 3 | `--bench-draw 150 format/tests/fixtures/Lab5.cdl` (53 gates) | PASS | built avg 1.79 / p95 2.08; kept avg 1.57 / p95 1.69 ms |
| 3 | `--bench-draw 150 format/tests/fixtures/lab6.cdl` (54 gates) | PASS | built avg 1.94 / p95 2.86; kept avg 1.51 / p95 1.58 ms |
| 3 | `--sim-view --bench-draw 150 res/samples/practice.cdl` | PASS | built avg 1.61 / p95 2.61; kept avg 1.51 / p95 2.55 ms |
| 4 | `--session-check 300 1` | PASS | all passed, 143 s |
| 4 | `--session-check 300 2` | PASS | all passed, 141 s |
| 4 | `--session-check 300 3` | PASS | all passed, 141 s |
| 4 | `--session-check 300 4` | PASS | all passed, 139 s |
| 5 | `--sync-test` | **FLAKY** (1 of 3 runs failed) | run 1: 264 passed, 1 failed: `s47_damaged_records_that_are_gone_stop_counting -- b.state().unreadable.empty() (line 2014)`; runs 2 and 3: 265 passed, 0 failed |
| 5 | `--start-menu-test` | PASS | 13/13; added, checked and removed again |
| 6 | Drag frame pacing on the real screen, 100% | PASS | see below |
| 7 | Same at 125% and 150% | PASS | see below |
| 8 | GPU / Direct2D software fallback | PASS (hardware) | see below |

`res/samples` has only `practice.cdl`, so step 3 also used CI's two fixture
circuits (Lab5, lab6). `practice.cdl` was the "largest .cdl in res/samples"
for step 6. The drag self-test always builds its own one-gate circuit, though,
and at ~2 ms a frame the scene size doesn't change the pacing.

The `s47` sync failure is in the engine's scenario code
(`mac/CedarCore/SyncTestScenarios.cpp:2014`, after `w.tamper(a, "resetSpace")`).
Out of scope here (no engine changes), and it didn't happen again. It looks
like a timing dependency on the in-process server and is worth a look before
the sync test's `continue-on-error` comes off in CI.

## Steps 6 and 7: frame pacing on the real display

The app's own "frame gap" counts *paints*, not frames on screen. With
`D2D1_PRESENT_OPTIONS_IMMEDIATELY`, the compositor (DWM) shows only the newest
present at each vblank. So the test PC bucketed each drag log's P lines (paint
times) between the press (D) and the drop (U) into 59.97 Hz refresh intervals.
A refresh with no paint shows a repeated ("doubled") frame. Paints after the
first in the same refresh are superseded and never seen.

| Run | dpi | Canvas px | Refreshes | New frame shown | Repeated | Paints never shown | On-screen gap p50 / p95 / max | Gaps > 1 refresh |
|---|---|---|---|---|---|---|---|---|
| 100%, new drawing | 96 | 3229x1276 | 255 | 240 | 15 | 989 | 16.7 / 16.7 / 16.7 ms | 0 |
| 100%, no kept geometry | 96 | 3229x1276 | 257 | 241 | 16 | 989 | 16.7 / 16.7 / 16.7 ms | 0 |
| 100%, **old drawing** | 96 | 3229x1276 | 259 | **5** | **254** | 0 | 16.7 / 16.7 / **4035** ms | 1 |
| 100% (rerun, GPU watched) | 96 | 3229x1276 | 243 | 228 | 15 | 1002 | 16.7 / 16.7 / 16.7 ms | 0 |
| 125% | 120 | 3176x1235 | 243 | 227 | 16 | 1127 | 16.7 / 16.7 / 16.7 ms | 0 |
| 150% | 144 | 3124x1194 | 244 | 229 | 15 | 1128 | 16.7 / 16.7 / 16.7 ms | 0 |

The ~15 repeated refreshes in each new-drawing run are the test's own pauses:
60 ms still after the press and 250 ms still before the release. During
movement, no refresh was missed at any scale. Paint gap p95 at 125% and 150%
was 3.6 ms, with worst 16.8 and 22.5 ms. The grab stayed under the pointer
(worst error 0.56 units against a tolerance of 1), and the drop landed within
0.08 units. The scale was changed live through the same DisplayConfig call
Settings uses, and put back to 100% afterwards.

### What makes parts "jump around"

**Evidence that it's WM_PAINT starvation, and that it's fixed:** with the old
drawing (`CEDARLOGIC_DRAG_OLD=1`, which only invalidates on each move), the
real display got **5 frames in 4.3 s**. One gap was 4.04 s long, so the part
sat still and then jumped to the pointer. Only 269 of ~1,222 moves were even
handled before the queue drained. That's hypothesis #1 in
`windows/DRAG-DIAGNOSIS.md`. Windows hands out WM_PAINT only when no input is
waiting, and a 500 Hz mouse keeps input waiting. The current drawing (commit
`89d397b`, 2026-10-16: `Canvas::redrawWhileMoving`/`paceFrame` plus
present-immediately) puts a new frame on screen at every refresh. **Any
tester's build from before 2026-10-16 would show the jumping. Builds since
then shouldn't.**

Causes ruled out on this PC:

- **DPI scaling / grab offset:** identical results at 96/120/144 dpi; worst
  grab error 0.50-0.56 units (under one snap step) at every scale.
- **Mouse-event coalescing:** the new path saw 1222-1350 moves from the 1351
  injected points (the missing ones at 100% are steps that round to the same
  pixel, which Windows drops). Coalescing only bites in the old path, where the
  canvas never got time to handle them.
- **vsync / Present timing:** present 0.25-0.42 ms avg, max 1.57 ms. No present
  waits for vblank (present-immediately). The scene draws in 0.05-0.2 ms.
- **Hybrid GPU:** the app renders on the Iris Xe MAX, the adapter driving
  the monitor (step 8), so frames don't cross between adapters.

Two notes:

1. The self-test uses injected input (`SendInput` every 2 ms). A physical
   high-rate mouse wasn't moved by hand in this test (unattended).
2. During a drag the app paints about once per move (~330 paints/s on a 60 Hz
   screen), and about 80% of those are never shown. `paceFrame` only limits
   the *forced* `UpdateWindow` paints. Ordinary WM_PAINTs still come whenever
   the queue empties between moves, and with 0.2 ms draws it empties after
   nearly every move. That's harmless here (about 0.5 ms of CPU per paint) but
   wasted work on a laptop battery or a slow GPU. It wasn't changed: it isn't a
   small, obvious fix, since it would mean deferring paints by timer or
   validating the rect.

## Step 8: GPU / Direct2D

- `dxdiag /t`: both adapters on WDDM 3.2, DDI 12, feature levels up to 12_1,
  "No problems found" on both Display tabs, devices started with no problem
  code.
- The app's process: while the drag self-test ran, `\GPU Engine(pid_<CedarLogic>_*)`
  counter instances existed only on adapter LUID `0xc9da`. That's the only
  adapter with dedicated memory in use (about 1.2 GB), the Iris Xe MAX that
  drives the display. A WARP (software) target would have no GPU engine
  instances. 3D utilization read 0.0%, which fits draws of about 0.2 ms.
  **No software fallback.** The app doesn't log its render target type itself
  (`D2D1_RENDER_TARGET_TYPE_DEFAULT` in `App/Util.cpp`), so this is inferred
  from the counters.
- The session checks' "no Direct2D frame or target failed" passed on all 4
  seeds.

## Fixes made

None in `windows/`. Nothing failed there, and the one thing worth changing
(extra paints during a drag, above) isn't a small fix. The build problem was
this PC's missing Windows SDK, not the project.
