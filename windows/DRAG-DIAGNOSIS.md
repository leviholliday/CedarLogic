# Dragging on Windows: diagnosis

The report: on a real Windows PC, dragging a part isn't smooth. The part
"jumps around the screen" as the mouse moves, so badly you can't tell you're
dragging. The same engine is smooth on the Mac, and its drag step was measured
at under 1 ms a move (`mac/Tools/bench-drag.sh`), so the problem is in the
Windows layer (`windows/App`), not the engine.

None of this has been run on Windows yet (no Windows machine or VM was
available). The analysis comes from reading the code. The fixes and the
diagnostic compile with MinGW-w64, but **nothing here has been seen working**.
Run the steps under "Running it" on a Windows PC to confirm or rule out each
cause.

## How a drag works on Windows

1. `WM_LBUTTONDOWN` goes to the canvas (`Canvas::proc` -> `handle`). The
   lParam (client pixels) is divided by `dpi/96` to get view points (`viewPoint`).
   `worldPoint` then gives world units: `wx = originX + vx*upp`,
   `wy = originY - vy*upp`. `onPress` calls `cl_edit_press(..., upp)` and
   `SetCapture`.
2. Each `WM_MOUSEMOVE` is converted the same way. `onMotion` calls
   `win->pointerMoved` (marks the status bar dirty), then `cl_edit_drag`, then
   `redraw()`. `redraw()` only *invalidates* the canvas and the minimap.
3. `WM_PAINT` goes to `Canvas::paint`: `WindowSurface::begin`, which uses a
   96-dpi `ID2D1HwndRenderTarget` with a `Scale(dpi/96)` transform, then
   `drawInto` -> `cl_document_draw_ex(rt, scale, origin, upp)`. The engine
   undoes the scale (`DevicePixels`) and draws in device pixels with
   `scale/upp`. Then `EndDraw` presents.
4. `WM_LBUTTONUP` goes to `cl_edit_release`.

A gate dragged from the side panel follows the same path, through
`GatePalette::dragTo`. It reads `GetCursorPos` + `ScreenToClient` and calls
`cl_edit_hover` on a floating selection. A gate clicked in the panel and then
moved onto the canvas goes through `Canvas::onMotion` (`Drag::None`, floating).

## Hypotheses, ranked

### 1. Frames arrive late and in bursts because WM_PAINT waits for an empty input queue. Likely; fixed.

Windows hands out `WM_PAINT` only after all waiting input has been handled.
The order is: sent messages, posted messages, input, then `WM_PAINT`, then
`WM_TIMER`. A drag move only invalidated the canvas. While the mouse keeps
moving, input keeps arriving, and so the frame keeps getting put off. A
500-1000 Hz mouse sends a move every 1-2 ms. Any time handling a move plus the
present takes longer than that, the canvas can go many moves without a frame.
Then it jumps to wherever the pointer is. That is a part that "jumps around".

The Mac and Linux don't hit this: AppKit and GTK draw once per display
refresh with the latest position.

Evidence: `Canvas::onMotion`, `case Drag::Edit` called `redraw()`, which is
`InvalidateRect` only. Nothing painted synchronously. `GatePalette::dragTo`
did the same.

**Fix:** `Canvas::redrawWhileMoving()`. During a drag, or while a gate is on
the pointer, it invalidates the canvas and then calls `UpdateWindow` once a
display refresh has passed since the last paint. The refresh rate is read
with `GetDeviceCaps(VREFRESH)` as the drag starts. So a frame is drawn at the
display's pace, always with the latest pointer position. Moves in between
only update the engine. Panning gets the same pacing (`paceFrame`).

### 2. Each present waits for the display, once for every window that redraws. Likely; fixed.

`WindowSurface::begin` made every `ID2D1HwndRenderTarget` with the default
present option, `D2D1_PRESENT_OPTIONS_NONE`. With that option, `EndDraw`
blocks until the next vertical blank, and every drawn part of the window
(canvas, minimap, status bar, toolbar, tab strips) has its own target.

During a drag:

- Every move invalidated the canvas **and** the minimap (`Canvas::redraw` ->
  `win->redrawMiniMap()`). That is two vblank waits per frame, so 30 fps at
  most on a 60 Hz display.
- Every 100 ms, `pointerMoved` -> `statusDirty` -> `updateStatus()` redrew the
  status bar and the toolbar (for the zoom readout). That adds two more waits,
  about 67 ms without a canvas frame.

The result is a stutter that repeats regularly, on top of #1.

**Fix:** targets present with `D2D1_PRESENT_OPTIONS_IMMEDIATELY`
(`App/Util.cpp`). On Windows 10/11 the compositor (DWM) shows the last frame
handed over at each refresh, so nothing tears. The minimap is no longer
redrawn on each drag move. It shows nothing new until the drop, and the drop's
`redraw()` updates it.

### 3. Drawing the scene costs more than a frame. Possible; measured, not fixed.

`D2DScene` (`windows/CedarCore/D2DScene.cpp`) creates a new
`ID2D1PathGeometry` or `ID2D1TransformedGeometry` for every filled or stroked
shape and every circle, on every frame. Every label is drawn as DirectWrite
glyph outlines turned into a geometry and filled, which means tessellating
them on the CPU again on every frame. In a medium-sized circuit this can take
tens of milliseconds a frame, while CoreGraphics on the Mac is cheap.

With #1 and #2 fixed, a slow scene still caps the frame rate. The motion
stays steady, though: each frame is drawn with the latest position.

The dot grid (`gridStyle` 1) can also issue up to 60,000 `FillEllipse` calls
a frame.

The P lines' `drawMs` column measures this. If it's high, the next step is
to cache geometry per gate type/label (or use `ID2D1GeometryRealization` on an
`ID2D1DeviceContext`) and draw labels with `DrawGlyphRun`.

### 4. A quick second press is taken as a double-click and ends the drag. Possible on touchpads; logged, not changed.

The canvas class has `CS_DBLCLKS`. When a press arrives as `WM_LBUTTONDBLCLK`
(within the double-click time, 500 ms by default, and 4 px of the last
click), `onPress` ends the gesture. It then opens the gate's settings, or, if
the press isn't on exactly one gate, **animates a zoom to fit**. During that
animation the whole circuit glides, so it looks like the parts jump.

This happens with:

- a touchpad's "double-tap and drag",
- clicking a part and grabbing it again at once.

The Mac does the same on `clickCount == 2`, but Mac trackpads aren't used that
way. Whether to change it is a design choice (for example: act on a
double-click only if it's released without moving). The log writes a
`K ... WM_LBUTTONDBLCLK` line whenever this happens.

### 5. Timers stall during a continuous drag. Side effect, not the jump.

`WM_TIMER` comes after input and paint. While the mouse keeps moving, the
window's 15 ms clock (`CircuitWindow::tick`) can be delayed. That delays the
simulation, fades and status updates. It doesn't move the part. Worth
remembering if the simulation seems to pause while you drag.

### 6. The camera moves during a drag. Unlikely; logged.

`Canvas::onSize` keeps the view's middle in place, so a canvas that resizes
mid-drag shifts the view by half the change. Things that resize it:

- the status bar sliding in for a note when it's set to hidden,
- focus mode sliding in or out,
- the oscilloscope,
- a DPI change.

`needsFit` / `zoomToFit` and the eased zoom are both cleared at the press.

The M lines record `originX/originY/upp`, and a `K ... WM_SIZE mid-drag` line
is written whenever the canvas resizes during a drag. Constant origin values
rule this out.

### 7. Coordinates don't match. Ruled out by reading the code; the log checks it.

- The press, the drag and the drawing use the same mapping: view points =
  pixels / (dpi/96), and `upp` is world units per view point. That matches the
  Mac's `unitsPerPoint`, with y flipped in `worldPoint` and in the engine's
  `Camera`.
- The render target's `Scale(dpi/96)` is undone by `DevicePixels` before the
  engine draws, so the drawing is in pixels at `scale/upp`, the same as the
  pointer mapping.
- Captured `WM_MOUSEMOVE` lParams are client pixels of the canvas.
  `GET_X_LPARAM` keeps the sign. The custom title bar (`WM_NCHITTEST`,
  `WM_NCCALCSIZE`) belongs to the top-level window and doesn't change a
  child's client coordinates.
- The side panel's drag uses `GetCursorPos` + `ScreenToClient` on the canvas,
  which gives the same coordinates.
- The process is per-monitor-v2 DPI aware (manifest and
  `SetProcessDpiAwarenessContext`), and `scale()` re-reads the DPI each time.
- The published `windows-native-testing` build (branch `windows/native`) has
  the same `Canvas.cpp`, `Util.cpp`, `Palette.cpp`, `Window.cpp` and
  `D2DScene` as `a621793`.

The engine moves a selection in half-unit snaps (`kGrid` 0.5, 5 points at
100%), the same as on the Mac. So the part trails the pointer by up to half a
unit, and by up to the 6-point drag threshold before the drag begins. The log
checks all of this on every move: `errX/errY` is part - (pointer + grab
offset).

### 8. The engine is slow. Ruled out on the Mac; checked by the log.

`engineMs` on every M line is the time spent in `cl_edit_drag` /
`cl_edit_hover` on Windows.

## What changed (all uncommitted)

| File | Change |
| --- | --- |
| `windows/App/Util.cpp` | Render targets present at once (`D2D1_PRESENT_OPTIONS_IMMEDIATELY`) unless `CEDARLOGIC_DRAG_OLD=1`. |
| `windows/App/Canvas.cpp/.h` | `redrawWhileMoving` / `paceFrame`: one synchronous frame per display refresh during an edit drag, a pan, or a floating gate. The minimap is skipped during a drag. Paints are timed. Log hooks for press, move, paint, release, double-click, lost capture and mid-drag resize. |
| `windows/App/Palette.cpp` | The side-panel drag uses `redrawWhileMoving` and is logged (kind `palette`). |
| `windows/App/DragLog.cpp/.h` (new) | The log and the self-test. |
| `windows/App/main.cpp` | `--drag-log <file>`, `--drag-selftest [file]`, `CEDARLOGIC_DRAG_LOG`. |
| `windows/CMakeLists.txt` | A comment. `App/*.cpp` is globbed, so `DragLog.cpp` is built automatically. |
| `mac/CedarCore/Editor.cpp`, `include/CedarCore.h` | `cl_gate_position(doc, gate, &x, &y)`, read-only, so the log can follow the dragged part. |

`CEDARLOGIC_DRAG_OLD=1` brings back the old drawing (vsync present, paint
only when idle, minimap on every move), so the old and new behaviour can be
measured on the same machine.

## Running it on a Windows PC

You need a build of this worktree. Either push it through CI (the
`windows-native.yml` zip), or build it on the PC with Visual Studio and CMake
(`cmake -S windows -B build -A x64 && cmake --build build --config Release`).

Open **cmd** in the folder with `CedarLogic.exe` (and its `res` folder).

### Self-test (about 6 seconds, no hands)

```
start /wait "" CedarLogic.exe --drag-selftest "%USERPROFILE%\Desktop\drag-new.log"
echo exit %errorlevel%
```

- **Don't touch the mouse or keyboard while it runs.** It moves the real
  pointer and presses the real primary button (the right one if the buttons
  are swapped).
- It opens a new, empty circuit at 100% and puts a 2-input AND gate in the
  middle.
- It presses on the gate and drags it twice round a circle (once in 2 s, once
  in 0.5 s), with a move every 2 ms, like a 500 Hz mouse. Then it drags it off
  to the side and lets go.
- The input comes from a separate thread, so the window receives it the way
  it would from a real mouse.
- It stops, and lets go of the button, if CedarLogic stops being the window
  in front.
- It won't press at all if the gate is covered or the window isn't in front.

Exit codes: 0 = PASS, 1 = FAIL, 2 = SKIP (couldn't safely press). The last
lines of the log, also printed to the console, are:

```
PASS  drag self-test: 1400 moves seen, gate moved yes, drop error 0.300 units, worst error while moving 0.500 units (tolerance 1)
SMOOTH  frames while dragging: 210 over 1400 moves, gap p95 17.2 ms, worst 25.0 ms, drawn 9.0 px behind the pointer at p95
```

- **PASS/FAIL** checks position. At every move, the gate must stay within one
  grid unit of the point it was grabbed by, and it must land under the
  pointer at the drop. A FAIL means a real coordinate bug (hypothesis 7).
- **SMOOTH/CHOPPY** checks frames. It's SMOOTH when 95% of the gaps between
  frames are 34 ms or less (about 30 fps).

Then run the old behaviour for comparison:

```
set CEDARLOGIC_DRAG_OLD=1
start /wait "" CedarLogic.exe --drag-selftest "%USERPROFILE%\Desktop\drag-old.log"
set CEDARLOGIC_DRAG_OLD=
```

If no file is given, the log goes to `%TEMP%\cedarlogic-drag-selftest.log`.

### Your own drags

```
start "" CedarLogic.exe --drag-log "%USERPROFILE%\Desktop\drag-manual.log"
```

Or set `CEDARLOGIC_DRAG_LOG` to a file path. Then drag parts as usual (on the
canvas, and from the side panel) and quit. Do it once with
`CEDARLOGIC_DRAG_OLD=1` and once without.

### Reading the log

It's tab-separated text, so it opens in Excel. The first column is the line
type, and the `#` lines at the top name every column.

| Line | What it is |
| --- | --- |
| `D` | A press that starts a drag: the grabbed point, and the part being followed (the one selected gate). |
| `M` | A pointer move during a drag (`kind` is edit, float or palette). |
| `P` | A canvas frame during a drag. |
| `U` | The release. |
| `S` | That drag's summary. |
| `K` | Something that ended or disturbed a drag. |

Columns in each M line:

- `lpX/lpY`: the message's lParam (client pixels).
- `msgPosX/Y`: `GetMessagePos`, the screen position when the move happened.
- `cursorX/Y`: `GetCursorPos`, where the pointer is now.
- `vx/vy`: view points.
- `wx/wy`: world units.
- `partX/partY`: the dragged gate's position, from the engine.
- `errX/errY`: part - (pointer + grab offset).
- `engineMs`: time in `cl_edit_drag` / `cl_edit_hover`.
- `sinceMoveMs`, `sincePaintMs`.
- `originX/originY/upp`: the camera.
- DPI, client size, the client area's screen position, the window rectangle.

Columns in each P line:

- `drawMs`: time drawing.
- `presentMs`: time in `EndDraw`.
- `frameGapMs`: time since the last frame.
- `movesSincePaint`.
- `lag_px`: how far from the pointer the part was drawn, in pixels.

What to look for:

| You see | It means |
| --- | --- |
| `err` beyond 1 unit; `lpX` not equal to `cursorX - clientAtScreen.x` while the mouse is still; DPI or `clientAtScreen` changing | A coordinate bug (7) |
| `originX/originY/upp` changing during one drag, or `K WM_SIZE` | The camera moved (6) |
| `K WM_LBUTTONDBLCLK` | The drag was ended by a double-click (4) |
| `K WM_CAPTURECHANGED` | Something took the pointer |
| Old log: big `frameGapMs` with high `movesSincePaint` | Paint starvation (1) |
| Old log: `presentMs` near 16 (60 Hz) | vsync blocking (2) |
| `drawMs` large in both logs | The scene is too slow to draw (3) |
| `engineMs` large | The engine (8) |

If the new log is SMOOTH and the old one CHOPPY, fixes 1 and 2 were the cause.
