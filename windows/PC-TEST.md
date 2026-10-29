# Windows PC test (real hardware)

Date: 2026-10-29. Branch tested: `ui/windows-menubar` at `54f1236`.

**Result: blocked at step 1. The build fails on this PC because the Windows
SDK isn't installed.** None of the app's checks could run, because there's no
`CedarLogic.exe`. Nothing here is a fault in CedarLogic's code: CMake can't
even identify the compiler, so it never reaches the project's sources.

## PC specs

| | |
|---|---|
| Machine | Lenovo 11T5S2Y200 (desktop) |
| CPU | 12th Gen Intel Core i7-12700 (12 cores / 20 threads) |
| RAM | 31.7 GB |
| GPU | Hybrid: Intel Iris Xe MAX 100 (discrete, 1.9 GB, driver 32.0.101.7084, drives the display) + Intel UHD Graphics 770 (integrated, driver 32.0.101.7079) |
| Windows | Windows 11 Education 10.0.26200 (build 26200) |
| Display | One monitor: Lenovo T34w-30, 3440x1440 |
| Scale | 100% (96 dpi) |
| Refresh | 59.97 Hz (reported as 59/60 Hz) |
| Toolchain | Visual Studio Enterprise 2026 18.10.4, MSVC 14.51.36231, bundled CMake 4.3.1-msvc1. **No Windows 10/11 SDK** (`C:\Program Files (x86)\Windows Kits` has only `NETFXSDK`). `gh` isn't installed (cloned with git + Git Credential Manager instead). |

## Step 1: build

Command, as CI runs it:

```
cmake -S windows -B windows/build -A x64 -DCL_BUILD_TOOLS=ON
```

Error (configure stage):

```
-- Building for: Visual Studio 18 2026
-- The C compiler identification is unknown
-- The CXX compiler identification is unknown
CMake Error at CMakeLists.txt:11 (project):
  No CMAKE_C_COMPILER could be found.
CMake Error at CMakeLists.txt:11 (project):
  No CMAKE_CXX_COMPILER could be found.
-- Configuring incomplete, errors occurred!
```

The cause, from `windows/build/CMakeFiles/CMakeConfigureLog.yaml`:

```
Microsoft.CppBuild.targets(533,5): warning MSB8003: The WindowsSDKDir property is not defined.
LINK : fatal error LNK1181: cannot open input file 'kernel32.lib'
```

So MSVC compiles, but there's no Windows SDK to link against. To fix it, use
the Visual Studio Installer (Modify, then Individual components) to add a
"Windows 11 SDK" (10.0.22621 or newer). That needs admin rights, so this test
didn't do it. CI's `windows-2022` image already has the SDK, which is why the
same commands work there.

## Results

| # | Command | Result | Key numbers |
|---|---|---|---|
| 1 | `cmake -S windows -B windows/build -A x64 -DCL_BUILD_TOOLS=ON` | **FAIL** (environment: no Windows SDK) | LNK1181 `kernel32.lib` |
| 1 | `cmake --build windows/build --config Release --parallel` | NOT RUN | configure failed |
| 2 | `--drag-selftest` (as built / `CEDARLOGIC_DRAG_OLD=1` / `CEDARLOGIC_NO_GEOMETRY_CACHE=1`) | NOT RUN | no exe |
| 3 | `--bench-draw 150 res/samples/practice.cdl` (the only `.cdl` in `res/samples`), and `--sim-view --bench-draw 150` | NOT RUN | no exe |
| 4 | `--session-check 300 <seed>`, seeds 1-4 | NOT RUN | no exe |
| 5 | `--sync-test`, `--start-menu-test` | NOT RUN | no exe |
| 6 | Real-screen drag frame pacing | NOT RUN | no exe |
| 7 | Other displays / scales | NOT RUN | only one display; nothing to measure without the exe anyway |
| 8 | GPU / Direct2D software fallback | PARTIAL (dxdiag only) | see below |

## Step 8: GPU (dxdiag only)

`dxdiag /t` shows both adapters on WDDM 3.2, DDI 12, feature levels up to
12_1, "No problems found" on both Display tabs, and both devices started with
no problem code. Nothing points to a software fallback at the system level.
The app's own view (whether its `ID2D1HwndRenderTarget` ends up as hardware or
WARP) couldn't be checked without the exe.

## Drag jumping

Not measured: the drag self-test needs the app. One thing about this PC is
relevant to `windows/DRAG-DIAGNOSIS.md`, though. It's a **hybrid-GPU machine**
(Iris Xe MAX discrete driving the monitor, plus UHD 770 integrated). If
Direct2D's device picks the other adapter than the one scanning out, each
Present becomes a cross-adapter copy, which can add latency and uneven frame
gaps. That's a hypothesis worth checking in step 6 once the app builds, not a
finding. The display is plain 100% scale at 59.97 Hz, so DPI scaling can't be
the cause on this PC.

## Fixes made

None. Nothing in `windows/` was reached, so there was nothing to fix.

## To finish this test

1. Add the Windows 11 SDK through the Visual Studio Installer (admin).
2. Re-run steps 1-8 from the brief. Everything else (repo, branch, VS 2026,
   MSVC, CMake) is in place on this PC.
