# Smooth dragging on Linux

A tester on Fedora said dragging in the Linux app felt bad. On a Raspberry Pi
it works, but it doesn't feel as polished as the Mac app. The shared engine's
drag step takes under 1 ms per pointer move on real circuits
(`mac/Tools/bench-drag.sh`), so the engine's editing code is not the
problem. The slow part is how the Linux front end draws. This note covers
what a drag costs, what changed, how to measure it, and what to do next.

## What one pointer move costs

Path: `motion-notify-event` → `Canvas::motionCb` → `Canvas::onMotion` →
`cl_edit_drag` (the engine moves the parts) → `Canvas::redraw()` →
`gtk_widget_queue_draw` (plus the minimap) → on the next frame-clock tick,
`Canvas::draw`.

**Batching already works.** GTK 3 draws at most once per frame-clock tick
however many times `queue_draw` is called, and it merges queued motion events
(event compression is on by default). A burst of 1000 Hz mouse events costs
one draw per frame plus one cheap `onMotion` per event. Batching motion
events further would not help. The cost is the draw itself.

**Each draw (before this change), every frame, for the whole canvas:**

1. Copy the grid layer (`gridLayer`, a full-size RGB24 image) into `frame`.
   The grid is cached and redrawn only when the camera moves. While panning
   or zooming, it was redrawn into its layer and then copied, every frame.
2. The engine (`cl_document_draw_ex` → `clDrawPage`) draws every wire and
   every gate on the page, in id order. It skips nothing that is off screen.
   Each shape gets its own `cairo_save`/`stroke`/`restore`, and every label
   is drawn as glyph outlines filled as a path (`CairoScene::text`), which is
   far slower than Cairo's cached glyphs (numbers below).
3. `cl_edit_draw_overlay` (pins and connection lines) and the selection box.
4. Copy `frame` onto the window.
5. Overlays. The banner and the Simulation View bar also re-blur the part of
   `frame` under them (`frosted()`: a 10× shrink-and-grow, small).
6. Outside the canvas: the minimap is queued on every move. Its picture of
   the whole page is cached, but the cache key includes the visible area and
   the page bounds, so panning, or dragging a part past the page's edge,
   redraws the whole page again as a thumbnail on every move.

After that the windowing system adds one more copy. On X11, `frame` goes to
the X server (Xvfb in CI). On Wayland, the shared-memory buffer is committed
and the compositor uploads the damaged area to the GPU.

**HiDPI.** `gtk_widget_get_scale_factor` returns an integer. GTK 3 only
renders at integer scales. With fractional scaling (125%, 150%, common on
Fedora laptops), Mutter asks GTK 3 apps for scale 2 and shrinks the result,
so the canvas is drawn at 2× in each direction: **4× the pixels** of scale 1,
even on a 1080p panel at 150%. A 4K screen at 200% is the same 2×. Every
full-size copy and every anti-aliased edge costs about 4× as much.

### How big each piece is

Measured with Cairo 1.18.6 on an Apple M4 (one core), using the same Cairo
calls the canvas makes. These are micro-benchmarks of the pieces, not the
engine itself. The window is 1280×760 points, about a maximised 1080p window
minus the side panel.

| per frame | scale 1 (1280×760 px) | scale 2 (2560×1520 px) |
|---|---|---|
| one full-size copy (grid → frame) | 0.19 ms | 0.88 ms |
| frame → window (RGB24 → ARGB32) | 0.19 ms | 1.37 ms |
| 600 curved gate bodies, filled and outlined | 13.8 ms | 25.2 ms |
| 600 labels as glyph outlines (what `CairoScene::text` does) | 10.3 ms | 17.4 ms |
| 600 labels as cached glyphs (`cairo_show_glyphs`) | 0.84 ms | 1.19 ms |
| 3000 two-segment wires, horizontal/vertical (CedarLogic's) | 2.6 ms | 3.1 ms |
| 3000 diagonal wires | 16.7 ms | 28.6 ms |
| the same 3000 diagonal, `CAIRO_ANTIALIAS_FAST` | 14.4 ms | 27.7 ms |
| the same 3000 diagonal, merged into one stroke | 53 ms | 102 ms |

Estimates for other machines. These are not measurements; run the benchmark
below for real numbers.

- **Mid-range Fedora laptop (x86-64):** roughly 1.5–2.5× the M4's time for
  drawing shapes, and 2–4× for full-size copies.
- **Raspberry Pi 4:** roughly 6–10× for shapes, and about 10× for copies
  (2–4 GB/s memory). At scale 1 each full-size copy is about 2–3 ms. A copy
  at scale 2 is about 10–12 ms.
- **Raspberry Pi 5:** about 2–3× faster than a Pi 4.

What this means: **gates and labels dominate, then pixel count.** The
circuit is drawn again from nothing every frame. A circuit with a few hundred
gates and labels takes 15–25 ms on an M4 at scale 1, about twice that at
scale 2, and several times more on a laptop at scale 2. That is 10–20 frames
a second on the Fedora machine: "terrible". The copies matter at scale 2 and
on the Pi (three full-size copies there cost about 30 ms on their own). They
don't matter on a fast PC at scale 1.

Two other results from the same micro-benchmarks:

- **Coarser anti-aliasing barely helps.** `ANTIALIAS_FAST` saves under 15%.
  Only `ANTIALIAS_NONE` is much faster, and it visibly changes the look.
- **Merging strokes is a trap.** One big stroke is 3× slower than many small
  ones.

## What changed (linux/App/Canvas.cpp, Canvas.h)

These are small, guarded changes. None of them changes what you see.

1. **Wayland: draw straight onto the window.** When the window's surface is
   an image in memory at the same scale (Wayland's shared-memory buffers,
   GTK's own offscreen drawing), the canvas skips `frame` and draws the grid
   layer and the scene directly into it. That is one full-size copy fewer
   per frame. GTK's clip limits it to what needs drawing. The frame is still
   used:
   - on X11, where one image sent to the server beats thousands of shapes
     sent one by one;
   - while a glass panel (banner, Simulation View bar) needs `frame` to blur;
   - when `CEDARLOGIC_CANVAS_FRAME=1` is set.

   If a glass panel appears on a frame that was drawn directly, `frosted()`
   asks for one more frame through `frame`. Nothing is drawn blurred from a
   stale frame.
2. **The grid while the camera moves.** During a pan or zoom, the grid is
   drawn straight into the frame instead of into its layer and then copied
   out. The layer is rebuilt once the camera has stayed put for one frame.
   This saves one full-size fill and one copy per pan or zoom frame.
3. **Only the damaged part, on X11.** When GTK asks for part of the canvas
   (another widget over it changed) and `frame` still holds the whole canvas
   as it is on screen, only that part is drawn again. Anything that changes
   the circuit or the camera still redraws everything.
4. **Minimap during drags.** While a drag is in progress, the minimap is
   redrawn at most 10 times a second (it was redrawn on every move). The
   release redraws it.
5. **`CEDARLOGIC_FAST_DRAG=1` (off by default):** `CAIRO_ANTIALIAS_FAST`
   while parts are being dragged. The micro-benchmark says it barely helps.
   It stays in so the benchmark can show the numbers on a Pi.

Things that were already right and are left alone: the wire value tag, hover
hit-tests and pin highlights only run when no drag is in progress. The status
bar updates at most 10 times a second (`CircuitWindow::tick`). Simulation
redraws only happen when a step changed something on screen.

**What to expect.** On Wayland at scale 2, these changes take one copy out of
three, plus the grid redraw while panning and the minimap's whole-page
redraw. That is a few milliseconds on a laptop and about 10 ms on a Pi. It is
a real improvement, but **it will not make a large circuit smooth at 2× on a
weak CPU.** Redrawing every gate and label every frame is still the dominant
cost. See "Next steps".

## The benchmark: `--bench-drag`

    cedarlogic --bench-drag [steps] <circuit.cdl>        # steps: 60 by default

It opens the circuit at a fixed window size (1440×900, so results from
different machines compare), zooms to fit, and finds the gate nearest the
middle. Then it runs these drags with the canvas's own handlers (`onPress`,
`onMotion`), on a figure-of-eight path:

- **Step by step**, at scale 1 and scale 2. Each step is one pointer move
  followed by one `Canvas::draw` into a window-sized image. Drags: one gate,
  "select all" (everything moves), and a pan. Draw modes: "through frame"
  (how X11 draws, and how every display drew before this change),
  "straight" (Wayland now), and "straight + fast AA". The first draw of each
  run is not counted.
- **Live**: one pointer move per frame-clock tick, drawn by GTK as a real
  drag is, at the screen's own scale. It reports the canvas's draw times and
  the time between frames.

For each it prints mean, median, p95 and worst in ms per frame, plus the
handler's mean (`onMotion`, which includes the engine's `cl_edit_drag`).
Everything dragged is put back with `cl_edit_cancel`, and nothing is saved.
The run keeps its settings and Your Circuits in a temporary folder
(`XDG_DATA_HOME` and `XDG_CONFIG_HOME`), deleted at the end, so your own are
never touched. It exits 0. It exits 1 only when no circuit or gate was found.

**Reading the numbers:** 16.7 ms per frame is 60 frames a second, and 33 ms
is 30. Under about 8 ms leaves room for GTK and the compositor. Watch the
**p95** column, not the mean: a drag stutters whenever single frames miss.
At scale 2, "through frame" vs "straight" shows what the Wayland change
saves on that machine. "select all" vs "one gate" shows how much of the time
is the engine moving parts rather than drawing them. If "pan" is much slower
than "one gate", the grid is expensive (try the dot grid vs lines in
Settings > Canvas).

### Running it

On a CI runner or any headless machine (Xvfb, X11 path, scale 1 live):

    sudo apt-get install -y xvfb                       # Ubuntu, Debian, Raspberry Pi OS
    sudo dnf install -y xorg-x11-server-Xvfb           # Fedora
    xvfb-run -a -s "-screen 0 1600x1000x24" \
      cedarlogic --bench-drag 120 format/tests/fixtures/lab6.cdl

With the AppImage: `APPIMAGE_EXTRACT_AND_RUN=1 ./CedarLogic-Native-*.AppImage
--bench-drag ...`. From a source build: `linux/build/cedarlogic ...`. The
fixtures `lab6.cdl` and `jmSGTZM.cdl` are the large ones; `Lab5.cdl` is
medium.

On the desktop itself (Fedora on Wayland, a Pi), run the same command
without `xvfb-run`. The live part then shows the real path. The window
appears for a few seconds; leave the mouse alone while it runs.
`GDK_BACKEND=x11 cedarlogic --bench-drag ...` measures the X11 (XWayland)
path on the same machine for comparison.

A CI step could run this and keep the output. It is not added to
`.github/workflows/linux-native.yml`. If you want it there, a separate step
after "Smoke test the AppImage" is enough:

    xvfb-run -a -s "-screen 0 1600x1000x24" timeout 300 "$APPIMAGE" \
      --bench-drag 60 format/tests/fixtures/lab6.cdl | tee renders/bench-drag.txt || true

GitHub's runners are shared and noisy. Compare runs on the same machine, and
use the Pi or the Fedora laptop for verdicts.

## Next steps, in order

1. **Measure** with `--bench-drag` on the Fedora laptop (Wayland and
   `GDK_BACKEND=x11`) and on the Pi. If "straight" at the screen's scale is
   already under about 10 ms p95 for the circuits people use, stop here.
2. **Don't redraw what isn't moving (needs a small engine API).** At drag
   start, draw the page without the moving parts into a cached layer once.
   The moving parts are the gesture's `preMove` gates and `preMoveWire`
   wires (`EditGesture` in `mac/CedarCore/Editor.cpp`). Each frame then costs
   one copy plus the moving parts. This is the big win: it makes a drag cost
   scale with what moves, not with the circuit. It needs a new C function,
   e.g. `cl_document_draw_layer(..., CL_LAYER_STATIC | CL_LAYER_MOVING)`.
   Don't add a new field to `CLDrawOptions`: the Mac's Swift code builds
   that struct. The cache has to be thrown away when:
   - the simulation changes a wire's colour (`CL_TICK_SHOWN`);
   - a static gate's pin lights up under the moving part (collision
     highlight);
   - the camera, theme or selection fade changes.

   The moving parts are then painted over static gates, which differs from
   id order where they overlap during the drag.

   **Done (2026-10-16).** The engine has `cl_document_draw_layer` and
   `cl_document_moving_signature` (`mac/CedarCore/Extras.cpp`; the moving
   set is the gesture's gates, its selected wires, the wires reshaping with
   the parts, and any wire on a moving pin). `Canvas::drawLayered` keeps the
   static layer (background, grid, every other part, the drawing) from the
   drag's first moving frame and draws the moving parts over it. It's made
   again when its key changes (camera, size, scale, theme, grid, accent,
   wire weight, what's moving, an edit) or when anything calls
   `Canvas::redraw()` (the simulation's `CL_TICK_SHOWN`, every command);
   fades and notes over the circuit use `redrawOver()` and keep it. Made
   again three frames running, the rest of that drag is drawn whole.
   No static gate changes look during a drag (the pin box and connection
   line are `cl_edit_draw_overlay`'s, drawn every frame), and a halo fading
   in is on the selection, which is what moves. `CEDARLOGIC_DRAW_ALL=1`
   turns it off; `--bench-drag` prints "whole page" rows beside the others
   and compares a frame drawn both ways.
3. **Labels as cached glyphs.** `CairoScene::text` fills glyph outlines so
   labels measure and sit exactly as the Skia and Mac apps draw them.
   `cairo_show_glyphs` with the same font matrix, unhinted, is about 12×
   faster (table above). The rasterisation differs slightly (glyph masks,
   pixel-rounded positions). Check it against CI's screenshots before
   switching. This is Linux-only code (`linux/CedarCore/CairoScene.cpp`).

   **Done (2026-10-27): cached glyphs are the default.** CI (run
   40451913845) took every `--show` picture, the Pi themes' and the
   practice window twice in a row, and `render_png` drew Lab5, lab6,
   jmSGTZM and practice (light at 2x, the dark and print exports) both
   ways. Windows without a circuit are identical. In the rest, only label
   pixels differ: 0.3-1.4% of the picture, every one on a glyph's edge.
   The largest difference is 193 of 255 in the light pictures and under
   20 in the dark ones. The same glyphs sit in the same places; only the
   edge shading differs, and side by side at 6x zoom the letters look the
   same. Lab5 select-all drag on CI went from 5.27 to 3.47 ms a frame at
   scale 1 and from 9.41 to 6.38 at scale 2. `CEDARLOGIC_GLYPH_TEXT=0`
   fills the outlines as before, and CI keeps taking the pictures that
   way into `renders/outlines/` to compare.
4. **A GPU canvas, only if 2 and 3 aren't enough.** Inside GTK 3: a
   `GtkGLArea` with Skia's GL backend. The repo already draws the scene with
   Skia for the wx app (`src/gui/render/SkiaScene.cpp`). Keep Cairo as the
   fallback: llvmpipe (no GPU driver) is slower than Cairo, the Pi 3 has
   only GLES 2, and some Wayland/NVIDIA setups misbehave. It is a large
   dependency for the AppImage and .deb.

   **Not needed for dragging (2026-10-27): the moving parts are kept as
   an image.** After steps 2 and 3, a select-all drag still drew every
   moving gate, wire and label each frame. The gates and their labels are
   most of that, and a moving gate only slides (`CL_LAYER_MOVING_RIGID`).
   `Canvas::drawMovingImage` draws the moving gates once into an image when
   the static layer is made and copies it each frame, moved by the drag's
   offset (`cl_document_moving_rigid`) in whole device pixels. The moving
   wires (`CL_LAYER_MOVING_LIVE`) are drawn under it every frame, as the
   page is drawn: even a selected wire with both ends on moving gates
   reshapes, because the drag moves it whole and then the pins pull its
   ends. (A first version kept those wires in the image too; CI's compared
   frames showed their ends a step off.) It's made again with the static
   layer (its key, every `redraw()`). It isn't used while a new
   selection's halo fades in, for fewer than 8 selected gates (cheap to
   draw anyway, and then exact), when it would be over 8192 pixels a side
   or 16 megapixels, or with `CEDARLOGIC_DRAG_IMAGE=0`.

   The cost: whole pixels put the moving gates up to half a device pixel
   off until the drop draws them exactly. `--bench-drag` compares a
   select-all frame with the page drawn whole and prints how many pixels
   differ, and `CEDARLOGIC_BENCH_PICTURES=<folder>` saves both frames.

   Lab5 select all, `--bench-drag` on CI (x86_64, "straight"), mean ms a
   frame:

   | | scale 1 | scale 2 |
   |---|---|---|
   | before (outlines, drawn every frame) | 5.27 | 9.41 |
   | cached glyphs | 3.47 | 6.38 |
   | + gates and wires from an image (the first version, wrong) | 0.61 | 2.63 |
   | + the moving gates from an image (now) | 1.38 | 4.18 |

   On aarch64 the same rows are 4.88, 3.68 and 2.40 ms at scale 1. At
   scale 2 there the image doesn't help (7.06 ms against 7.04 drawn): copying
   a large image costs what drawing the gates does. Lab5's select-all frame
   against the page drawn whole: 1.07% of pixels differ by more than 24 of
   255, at most 112, all on gate edges (wires and the static layer exact);
   one gate (not from the image) 16 pixels, as before.

   The Zorin OS laptop was about 2.2 times slower than CI on the same
   rows (11.65 ms against 5.27).

   Left as it was: a running simulation that changes what's on screen
   calls `redraw()`, which makes the static layer and the image again.
   When that happens three frames running, the rest of the drag is drawn
   whole. **Measured (2026-10-27, run 40523841149):** `--bench-drag` now
   has "sim" rows that tick the simulation between moves (25 ms a step at
   60 frames a second, as `CircuitWindow::tick` does) and count the layers
   rebuilt. Lab5 and lab6 both have a running clock. On CI (x86_64,
   "straight"), mean ms a frame, Lab5, stopped / running:

   | | scale 1 | scale 2 |
   |---|---|---|
   | one gate | 0.40 / 0.84 | 1.38 / 2.34 |
   | select all | 1.45 / 2.43 | 4.48 / 6.31 |

   The static layer is made again on 16 of 60 frames at 25 ms a step (9 of
   60 on lab6): the frames where a step changed a wire. Those frames cost
   about what the page drawn whole does (select all at scale 2: p95 10.5 ms,
   against 6.3 whole). At 1 ms a step something changes every frame, the
   three-in-a-row rule turns the layer off after two builds, and the frames
   are the whole-page ones (3.5 and 6.4 ms for select all). Live, one gate
   on Lab5 tripped the rule at the start and drew the rest whole (2.4 ms);
   the sticky rule matters for a big circuit, where whole is the expensive
   draw. Nothing at scale 1 was over 8.4 ms.

## GTK 3 or GTK 4?

The owner asked why GTK 3 when GTK 4 exists. Nobody recorded a reason. The
README gives one after the fact: every Linux desktop has GTK 3 (Raspberry Pi
OS, Ubuntu 22.04 — which CI builds on — Fedora, Mint, Kali), it speaks
Wayland and X11, and the AppImage uses the system's copy so the app gets the
desktop's theme and fonts. In practice it was the safe default. GTK 4 is
current, and there is no GTK 5.

**Porting to GTK 4 would not fix dragging.** GTK 4 draws widgets and CSS on
the GPU (GSK). But a custom canvas like this one is either:

- a `GtkDrawingArea`, which still runs Cairo on the CPU into an image that
  is uploaded as a texture every frame (the same cost as now); or
- `GtkSnapshot` paths (`GskPath`, GTK 4.14+), which current renderers still
  largely rasterise on the CPU.

GTK 4 does have real advantages:

- true fractional scaling on Wayland (4.12–4.14+), so 150% would draw 1.5×
  instead of 2× (about 44% fewer pixels than now);
- GPU-composited window chrome.

The costs:

- About 24,000 lines of GTK 3 code in `linux/App` would change. Menus become
  popovers, signal handlers become event controllers, drag and drop is new,
  and `gtk_widget_draw`, which every CI screenshot relies on, is gone.
- Ubuntu 22.04 ships GTK 4.6 and Raspberry Pi OS Bookworm ships 4.8. Both
  are too old for fractional scaling and `GskPath`. The AppImage uses the
  system's GTK, so the oldest supported distro sets the feature level.

**Recommendation: stay on GTK 3 now.** Take the canvas from
"redraw everything" to "redraw what moves" (step 2) and cheaper labels
(step 3). Those fix the actual cost on every machine, Pi included, with no
new dependencies. Reach for a GPU canvas (step 4) only if the benchmark
still misses 16 ms on target hardware. Revisit GTK 4 for the chrome and
fractional scaling once the oldest distros you support ship 4.14 or later
(Ubuntu 24.04 has 4.14; Debian 13 and the Raspberry Pi OS built on it have
4.18). Port then, as its own project, not as a fix for dragging.
