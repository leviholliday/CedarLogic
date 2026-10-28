# V4 feature parity matrix (Mac, Windows, Linux, Web)

Made 2026-10-15 by reading code only (nothing built or run). Every cell was
checked by a grep or a read of the files named in the Notes column; where a
cell says "not found" it means a search of that tree found nothing, not that
it was run and failed.

## What was read

| Tag | Tree | Branch / state |
|---|---|---|
| M | `.claude/worktrees/rel-mac-0317/mac` (`App/*.swift`, `CedarCore/`) | rel/mac-0.3.17 (0.3.17 beta) |
| W | `.claude/worktrees/sync-x/windows/App` | rel/win-0.4, 141 commits behind M, 317 own |
| L | `.claude/worktrees/sync-l/linux/App` | rel/linux-0.4, 141 behind M, 292 own |
| S | `cedarlogic-site/public/assets/js` (`sim*.js`), `/app/`, `scripts/build_pages.py` | main at the 0.3.17 web release |
| core | `mac/CedarCore` inside each tree (shared C++ engine and C API) | W and L carry the 2026-10-05 core |

The decisive fact, from diffing `mac/CedarCore/include/CedarCore.h` and the
file lists: **the Windows and Linux trees do not only lack UI, they lack the
core calls.** Missing from their core: `Ink.cpp` (drawing and notes),
`PageLinks.cpp`, `CheckClocked.cpp` + `CheckImpl.h` (clocked checks), all of
`Classroom*.cpp`, and in `CedarCore.h` every `cl_ink_*`, `cl_notes*`,
`cl_pages_*`/`cl_page_*`, `cl_check_clocked*`/`cl_check_step_*`,
`cl_document_clock_step`, `cl_edit_lock_selection`/`cl_edit_unlock_all`/
`cl_gate_is_locked`, `cl_simview_light*`/`draw_predict`,
`cl_document_draw_fitted_ink`, `cl_document_save_text_ex`,
`cl_sync_remove_device`/`cl_sync_profile_name`/`cl_sync_side_records`. They
also lack 46 core commits since the merge base (b73a6b5), including
"Straighten and Tidy Up never lose a connection or leave a pin off its wire"
(5dbb585) and the Full rearrange speed and layout fixes (ad1251d, 958db97,
963e223, 6ff7f2f). `windows/CMakeLists.txt` globs `mac/CedarCore/*.cpp`, so
new core files come in automatically, but `NativeScene.h` (the Scene
abstraction W and L use) exists in their trees and not in M's, and the new
drawing paths (ink, Predict covers, lock badge) need `D2DScene.cpp` /
`CairoScene.cpp` support. Both are reconciliation work for the merge.

Legend: **yes** / **partial** (what is missing) / **no**. "n/a" = does not
apply to that platform.

## 1. The matrix

### Editing

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Select, move, rotate, delete, copy, paste, undo, redo | yes | yes | yes | yes | W `Window.cpp` buildMenus; L `main.cpp` menu model; S `sim.js` |
| Duplicate (Ctrl/Cmd+D) | yes | yes | yes | yes | W `Shortcuts.cpp:151`; L `Shortcuts.cpp`; S `sim.js` duplicateSel |
| Add a Gate / quick add (A) | yes | yes | yes | yes | W `Window.cpp` "Add a Gate... A"; S `sim.js:3318` |
| New and pasted parts follow the pointer | yes | yes | yes | yes | `cl_edit_float_begin` in W and L App; S placing ghost (`clo:placing`) |
| Connect nearby pins (C while dragging, menu) | yes | yes | yes | no | W/L `cl_edit_connect_nearby`; S: not found |
| Lock in Place (single parts; Unlock Part, Unlock All) | yes | no | no | yes | core lacks `cl_edit_lock_selection` (W/L `mac/CedarCore/include/CedarCore.h`); S `sim.js` lockSel |
| Lock the circuit (no edits, switches still work) | yes | yes | yes | yes | W `Window.cpp:322` CMD_LOCK; L `Window.cpp:1056`; S toolbar padlock |
| Part settings (inspector) | yes | yes | yes | yes | W `Dialogs.cpp`; L `Dialogs.cpp`; S `sim-settings.js` |
| RAM/ROM contents editor | yes | yes | yes | yes | W `Dialogs.cpp`; L `RamEditor.cpp`; S `sim-settings.js` |
| Find (label, TO/FROM, part) | yes | partial | partial | no | W/L `FindBar.cpp`: Enter/arrows only; no Find Next / Find Previous commands or keys (M has both: `CedarLogicApp.swift:704-710`, `ShortcutStore.swift`); S has only palette search |
| Zoom in/out/fit/actual size, shortcuts | yes | yes | yes | partial | W `Window.cpp` View menu; S wheel, pinch, Fit (F); no Actual Size or zoom keys found |
| Nudge with arrow keys | yes | yes | yes | yes | `cl_edit_nudge` in W and L; S arrow keys move the picked part |
| Focus mode | yes | yes | yes | no | W `Window.cpp` CMD_FOCUS_MODE; L `TitleButtons.cpp`; S: only a full-screen button on touch layouts (`sim-mobile.js`) |
| Tabs (pages): new, close, rename, reorder, reopen | yes | yes | yes | partial | S: no Reopen Closed Tab command (Undo brings a closed page back, `README.md` "Files and pages") |
| Split view (two pages side by side) | yes | yes | yes | no | W `Window.cpp` CMD_SPLIT_VIEW; S: listed in `APP_ONLY` (`sim.js:590`) |
| Page links (link groups, Connect/Disconnect All Pages, per-tab Connect to...) | yes | no | no | yes | core lacks `cl_pages_*`, `cl_page_connect_to`, `PageLinks.cpp`; S `sim-files.js:519-521` |
| Minimap | yes | yes | yes | yes | W `Palette.cpp` (MiniMap); L `MiniMap.cpp`; S `sim.js` |
| My Parts (save selection as a part) | yes | yes | yes | no | W/L `Collections.cpp`; S: listed under "a few more tools" in `APP_ONLY` |
| Drawing on the circuit (pen, highlighter, eraser) | yes | no | no | yes | core lacks `Ink.cpp` and `cl_ink_*`; M `Drawing.swift`; S `sim-draw.js` |
| Notes for the circuit | yes | no | no | yes | core lacks `cl_notes`; M `Notes.swift`; S `sim-notes.js` |
| Right-click / long-press context menus | yes | yes | yes | yes | W `Window.cpp:2441`; L `Window.cpp` showContextMenu; S `sim-settings.js` partMenu |
| Customizable keyboard shortcuts + searchable list | yes | yes | yes | no | W `Shortcuts.cpp` (58 commands), L `Shortcuts.cpp` (51); M `ShortcutStore.swift` also has Step Clock, Find Next/Previous, draw; S: fixed keys only |

### Wires

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Wire pin to pin | yes | yes | yes | yes | all |
| Drag wire segments / bends | yes | yes | yes | no | S wires are routed between pins; imported bends kept only until a part moves (`README.md`, `pts`); V4 checklist already lists it |
| Hover lights whole wire, value tag on rest | yes | yes | yes | partial | W `Canvas.cpp:222`, L `Canvas.cpp:618`; S colours by value, no rest-on-wire tag found |
| Straighten Wires (S) | yes | partial | partial | no | W/L have it (`cl_edit_straighten`) but on the older core without the connection-loss fix (5dbb585) |
| Tidy Up (Keep layout / Full rearrange, preview, Shift+S) | yes | partial | partial | no | W/L `cl_edit_tidy_*` present; core lacks the Full-rearrange fixes listed above |
| Wire look settings (thickness, low-wire colour, bend dots, dot size) | yes | yes | yes | no | W `Settings.cpp`, L `Settings.cpp:406-418` |

### Simulation

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Run, pause, step, speed | yes | yes | yes | yes | |
| Step Clock (one full cycle of manual clocks) | yes | no | no | yes | core lacks `cl_document_clock_step`; M `CedarLogicApp.swift:629`; S `sim.js:1493` + key K |
| Click a clock part to step it | yes | no | no | yes | same; M commit 3edf77b |
| Step Clock button in toolbar (optional) | yes | no | no | yes | M `SettingsView.swift:499`; S title menu "Show Step Clock in the Top Bar" |
| Simulation View (marching signals, control bar) | yes | yes | yes | yes | W `Window.cpp` CMD_SIM_VIEW; L `win.sim-view`; S `sim.js` |
| Predict + Reveal | yes | no | no | yes | core lacks `cl_simview_light*`; M `Predict.swift`; S `sim.js` |
| Projector mode | yes | no | no | yes | M `Presenter.swift`/`SimBar.swift`; S `sim.js:49` |
| Presenter mode (mirror to a second display) | yes | no | no | no | M `Presenter.swift` ("Mac only" in its header) |
| Oscilloscope + timing diagrams | yes | yes | yes | no | W/L `Scope.cpp`; S `APP_ONLY` first entry |
| Truth table, Karnaugh maps, simplest formulas, CSV | yes | yes | yes | yes | W/L `TruthTableWindow.cpp`; S `sim-truth.js`, `truth-table.js` |
| Build This as a Circuit (from the table) | yes | yes | yes | no | S: locked (`sim.js:593` LOCKED_MENU / truth table sheet) |
| Build from Formula | yes | yes | yes | no | W/L `Formula.cpp`; S locked |
| Check My Circuit: formula, minterms, truth table | yes | yes | yes | yes | W/L `TruthTableWindow.cpp`; S `sim-check.js` |
| Check My Circuit: clocked (count, state table, timing table) | yes | no | no | yes | core lacks `CheckClocked.cpp`, `cl_check_clocked*`, `cl_check_step_*`; M `CircuitCheck.swift`; S `sim-check.js` |

### Files and library

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Your Circuits library (saves as you work) | yes | yes | yes | yes | W `Library.cpp`/`LibraryWindow.cpp` (autosave timer `Window.cpp:689`); L `Library.cpp`; S `sim-library.js` (IndexedDB) |
| Version History | yes | yes | yes | no | S `APP_ONLY` ("Version history") |
| Recently Deleted | yes | yes | yes | yes | S `sim-library.js` (30 days) |
| Open / import .cdl (v1, v2, v3) | yes | yes | yes | yes | S `sim-files.js` |
| Save / export .cdl | yes | yes | yes | yes | S `cdl-export.js` |
| Export for older CedarLogic (v2, v1.x) | yes | yes | yes | partial | S: only "Download for Classic CedarLogic", shown when a circuit has drawing/notes/groups (`sim-draw.js:530`) |
| Templates: New from Template, Save as Template | yes | yes | yes | no | W/L `Collections.cpp`; S has examples only |
| Open Recent | yes | yes | yes | no | W `Window.cpp:261`; L `main.cpp:293` |
| Recovery of unsaved work | yes | yes | yes | yes | W/L `Recovery.cpp`; S IndexedDB |
| Double-click .cdl / cedarlogic:// opens the app | yes | yes | yes | yes | W installer `installer/CedarLogic.iss`; L .deb; S manifest file handler |

### Sharing, sync, export

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Export as Image | yes | partial | partial | partial | M: PNG + PDF, 3 looks (Match Window / Light / B&W), preview, Include drawing, Share (`ExportImageView.swift`). W `Export.cpp`: PNG only, Color/B&W, no Match Window look, no Include drawing, no Share. L `ExportImage.cpp`: PNG/PDF/SVG, same other gaps. S `sim-picture.js`: PNG, light/dark/print, include drawing, no PDF |
| Export Lab Report (PDF) | yes | yes | yes | no | W `LabReport.cpp` + `Pdf.cpp`; L `LabReport.cpp`; S: not found |
| Print | yes | yes | yes | yes | S through the browser |
| Share Link + QR | yes | yes | yes | yes | W `ShareLink.cpp`; L `ShareLink.cpp`; S `sim-share.js`; W/L links cannot carry drawing or notes (no core support) |
| Sync: turn on, code, QR pairing, device list, device name | yes | yes | yes | yes | W `SyncUI.cpp:565-577`; L `SyncUI.cpp`; S `sim-library.js` |
| Sync: Remove device from the list | yes | no | no | yes | core lacks `cl_sync_remove_device`; M `SyncSettingsView.swift:95-103`; S `sim-library.js:837` |
| Sync: your name on every device (profile record) | yes | no | no | yes | core lacks `cl_sync_profile_name`; S `sim-library.js:813` |
| Sync: classroom/side records follow the person | yes | no | no | yes | core lacks `cl_sync_side_records` |
| Classroom (teacher and student) | yes | no | no | yes | no `Classroom*.cpp` in W/L core; S `sim-classroom.js`. Plan says Classroom is OUT of 0.4 |
| Send Feedback (with screenshots) | yes | yes | yes | no | W/L `Feedback.cpp`; S: not found |
| Feedback screen recording | yes | no | no | no | M `FeedbackCapture.swift`; W README "Not ported" |
| In-app updates | yes | yes | yes | yes | M `Updates.swift`; W/L `Updater.cpp`; S service worker "Update ready" (`app.js`) |
| Beta / normal testing group | yes | no | no | n/a | M `SettingsView.swift:326`; not found in W/L |
| Crash reports | yes | no | no | no | M `CrashReports.swift`; not found in W/L |

### Learning and examples

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Built-in examples / templates | yes (~30, 6 groups) | partial (3) | partial (3) | partial (~23) | M `Templates.swift`, `TemplatesExamples.swift` (flip-flops, registers, combinational, counters, Classic Mistakes, two-page gate-built versions). W `Collections.cpp:210` and L `Collections.cpp:215`: Lab Page, 4-Bit Counter, 7-Segment Starter only. S `sim.js` PRESETS + Examples sheet |
| Examples with Step Clock choice (manual / running / pulse) | yes | no | no | yes | M `Templates.swift:312`; needs the Step Clock core call |
| Welcome / first run | yes | yes | yes | partial | S: coach marks on touch layouts only (`sim-mobile.js:389`) |
| Guided tour that builds a circuit | yes | yes | yes | no | W/L `Welcome.cpp`; S: not found |
| What's New | yes | yes | yes | yes | W/L `Welcome.cpp` (0.4 content already written) |
| Help window with search | yes | yes | yes | partial | S has Learn pages (`/learn/`) and a menu link, no in-window help |
| Keyboard shortcuts sheet | yes | yes | yes | partial | S: screen-reader keyboard text (`sim.js:1170`), no sheet |
| Lab Page / starter templates | yes | yes | yes | no | |

### Settings (Settings window rows)

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Settings window | yes (6 pages) | yes (6 pages) | yes (6 pages) | no | pages: General, Appearance, Canvas, Toolbar, Shortcuts, Sync (M `SettingsView.swift:10-20`; W `Settings.cpp:27`; L `Settings.cpp`). S has no app-level settings sheet; only per-part settings, Draw with, projector, Step Clock bar and theme in the title menu |
| Your name, opening behaviour, new-circuit template, quit question, status bar | yes | yes | yes | no | W `Settings.cpp:501+`; L `Settings.cpp:377-382`; S keeps the student name for export only (`cl-online:student-name`) |
| Opening .cdl files default-app row | yes | partial | partial | no | W has a Start menu switch instead (`Settings.cpp`); L: not found |
| Testing group (beta / normal) | yes | no | no | n/a | M `SettingsView.swift:326` |
| Crash reports switch | yes | no | no | n/a | M `SettingsView.swift:337` |
| Theme at launch, app colour | yes | yes | yes | partial | S: Light/Dark toggle only, no accent |
| Tabs: Modern / Classic | yes | no | yes | no | M `prefs.classicTabs`; L `Settings.cpp:404`, `TabStrip.cpp:141`; W: not found |
| Grid style, major lines, gate size, side-panel names/keys | yes | yes | yes | no | |
| Mouse wheel, trackpad, right-click rotates, Duplicate clipboard | yes | yes | yes | no | W `Settings.cpp`, L `Settings.cpp:433-442` |
| New pages share links | yes | no | no | yes | core lacks `cl_set_new_pages_share_links`; S title menu "New Pages Share Links" |
| Tidy Up mode (keep / rearrange) | yes | yes | yes | no | |
| Toolbar style and which groups show | yes | yes | yes | no | three styles (Seamless, Classic, Minimal) in W `Toolbar.cpp:34-60`, L `Toolbar.cpp` |
| Step Clock in toolbar | yes | no | no | yes | M `SettingsView.swift:499` |
| Sync page | yes | yes | yes | yes (sheet) | |

### Window chrome

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Toolbar drawn in the title bar | yes | yes | yes | n/a | W custom frame, snap layouts (`Toolbar.cpp`); L `set_titlebar` + `TitleButtons.cpp` (honours `gtk-decoration-layout`) |
| Tab cards, drag to split | yes | yes | yes | partial | S has a tab bar, no split |
| Side panel (categories, search, drag to canvas) | yes | yes | yes | yes | |
| Status bar (zoom, cursor, counts) | yes | yes | yes | no | W `StatusBar.cpp`, L `StatusBar.cpp`; S toasts only |
| Dark mode, follows system | yes | yes | yes | yes | |
| Launch screen with first-launch sound | yes | yes | yes | no | W `Splash.cpp`, L `Splash.cpp` |
| Quit confirmation | yes | yes | yes | n/a | L `QuitConfirm.cpp` |
| Tab switcher (Ctrl+Tab) | yes | yes | yes | yes (Ctrl+Tab, Alt+Shift+arrows) | W/L `TabSwitcher.cpp` |

### Accessibility

| Feature | Mac | Windows | Linux | Web | Notes / evidence |
|---|---|---|---|---|---|
| Screen-reader labels on controls | partial | no | no | yes | M labels on the sim bar, notes, sync QR (`SimBar.swift`, `Notes.swift`), canvas exposes children only for Predict (`CanvasView.swift:250`). W/L: no `WM_GETOBJECT`/UIA/ATK code found; controls are owner-drawn (`Dialogs.cpp:895`, Cairo) so a screen reader sees little. S aria + sr-only keyboard text (`sim.js:1170`) |
| Keyboard-only wiring and part picking | no | no | no | yes | S `sim.js:1259`, `:2766` (] [ arrows W); not found in M, W, L (L has `contextMenuFromKeyboard`, `Window.cpp:~820`) |
| Reduced motion respected | partial | yes | yes | yes | M splash only (`Splash.swift:94`); W `Welcome.cpp:33` (SPI_GETCLIENTAREAANIMATION); L `Welcome.cpp:28`, `Anim.h`; S `prefers-reduced-motion` |
| High-contrast mode | no | no | no | partial | S one reference in site CSS; not found in the native apps |
| Tab-key focus in dialogs | yes | yes | yes | yes | W `Dialogs.cpp:895` WS_TABSTOP |

## 2. Gap lists by user impact

What a student or teacher meets first, in order.

### Windows (rel/win-0.4)

1. **Step Clock and click-to-step clocks.** Every flip-flop and register lesson uses it; the web and Mac have it, Windows cannot step a manual clock as one cycle. Needs core `cl_document_clock_step`.
2. **Examples.** Three templates against about thirty on Mac; no two-page gate-built examples, no Classic Mistakes, no flip-flop or register sets (`Collections.cpp:210`).
3. **Drawing and notes, Lock in Place, Predict, projector mode.** A teacher's whole projector workflow (lock the starters, draw on the circuit, Predict, projector) is missing. All need core additions.
4. **Straighten / Tidy Up reliability.** Present, but on the core before the "never lose a connection" fix; the Full rearrange fixes are also missing.
5. **Check My Circuit for clocked circuits** (count, state table, timing table); only formula/minterms/table checks work.
6. **Page links** (groups, Connect All Pages); pages share TO/FROM names the old way only.
7. **Export as Image:** no PDF, no Match Window look, no Include drawing, no Share (`Export.cpp`). Lab Report PDF exists.
8. **Sync:** cannot remove an old device from the list; the name is not shared as a profile.
9. **Find Next / Previous** commands and keys.
10. **Settings:** no Tabs Modern/Classic row, no testing group, no crash-report switch, no "New pages share links".
11. **Accessibility:** nothing for screen readers (owner-drawn UI); no high-contrast handling.
12. Feedback screen recording, Presenter mode, Classroom (the last is out of 0.4).
13. Reported by testers (not verified here): object jumping while dragging, drawing layer jumpiness (`docs/V4-CORE-DIAGNOSIS.md` says the core step is under 1 ms, so it is the D2D/input layer).

### Linux (rel/linux-0.4)

Same list as Windows for items 1 to 9 and 11 to 12 (same missing core calls), except:

- Export as Image is better than Windows: PNG, PDF and SVG (`ExportImage.cpp`), but also no Match Window look, no Include drawing, no Share.
- Tabs Modern/Classic exists (`Settings.cpp:404`), so that Settings gap is Windows only.
- Whole scene redraws on the CPU for each pointer event (Cairo); tester reports of unsmooth dragging (V4-CORE-DIAGNOSIS.md); Pi "not as polished as Mac".
- Open-.cdl default-app row: not found in Settings (the .deb sets the association).
- Wayland, sound and feedback-send were never tested by a person (memory `linux-native-app.md`).
- Accessibility: no ATK code found.

### Web (public/app, /online-logic-gate-simulator/)

The web app is closer to the Mac app than Windows/Linux are on teaching features (Step Clock, Predict, projector, Lock in Place, page links, drawing, notes, clocked checks, Classroom, sync device removal all exist). Gaps, by impact:

1. **Build from Formula** (locked; `LOCKED_MENU`, `sim.js:593`) and **Build This as a Circuit**.
2. **Oscilloscope and timing diagrams.**
3. **Straighten and Tidy Up**, and **wire segment dragging** (wires are auto-routed).
4. **Split view.**
5. **Version History.**
6. Templates / My Parts, Find, focus mode, customizable shortcuts, Lab Report PDF, Export PDF, export-for-old-versions menu, connect-nearby, Send Feedback, guided tour/welcome, Settings window with the Mac's pages, status bar, accent colour.
7. Not planned for web: Presenter mode (second display), crash reports, native app updates.

The Settings window and a status readout are the largest "feels like the app" gaps once the V4 checklist items (1 to 5) are done.

### Mac (reference)

Mac is the most complete but not complete:
- Accessibility is partial: the canvas is not exposed to VoiceOver except Predict (`CanvasView.swift:250`); no keyboard-only wiring (the web has it, `sim.js:2766`).
- No `Open the Practice Circuit` item (W and L have it).
- Core bugs open (docs/V4-CORE-DIAGNOSIS.md): wire geometry can drift from pins after dragging a part (about 1 random session in 5; reload repairs it).
- Uncommitted core fixes in this worktree (uninitialised `wireSegment` members, a pin belonging to two wires, `CircuitEdits.cpp`) also have to reach W and L.
- Classroom is built but out of 0.4.

## 3. On Windows / Linux but NOT on Mac (do not lose these)

| Item | Where | Windows | Linux |
|---|---|---|---|
| Open the Practice Circuit (File menu) | `Window.cpp:264` (W), `main.cpp:294` (L); not found in M | yes | yes |
| "Check for new versions" switch in Settings > General | W `Settings.cpp`, L `Settings.cpp:382` (M has none) | yes | yes |
| Gate Settings... as a menu command | `Window.cpp:296` / `main.cpp:334` | yes | yes |
| Rename Tab as a menu command; Next/Previous Tab, Switch Side (F6) in a Tabs menu | W `Window.cpp:330-338` | yes | yes |
| Gate Palette / Side Panel show-hide in the View menu | W `Window.cpp:CMD_PALETTE`, L `win.palette` | yes | yes |
| Dark-mode switch as a toolbar button option | `showThemeToggle` (W, L; M has a menu only) | yes | yes |
| Disconnect in the right-click menu (`CMD_DISCONNECT`) | W `Commands.h` | yes | likely (`cl_edit_disconnect_pin` is used) |
| Tidy Up by Signal Flow as its own menu item | `Window.cpp:303`, `main.cpp:338` | yes | yes |
| Export as Image to PDF and SVG | `ExportImage.cpp` | no | yes (M has PDF only, no SVG) |
| Installer, Start menu, ARM64 build, zip copy offers to add itself | `windows/installer/CedarLogic.iss`, `Integration.cpp` | yes | n/a |
| Alt / F10 opens every menu in one popup | W `Window.cpp:1394`, L `Window.cpp:menusFromKeyboard` | yes | yes |
| .deb + install.sh one-liner, AppImage, pkexec self-update, Raspberry Pi font/theme readability fixes | `linux/package*.sh`, `Style.cpp` | n/a | yes |
| Honours the desktop's window-button layout and "no animations" | `TitleButtons.cpp`, `Anim.h` | n/a | yes |
| Own deflate and PDF writer (no zlib on Windows) | `Deflate.cpp`, `Pdf.cpp` | yes | n/a |
| Own CI helpers: `--screenshot`, `--show <window>`, `--select`, `--place` | `main.cpp` | yes | yes |

## 4. Where Settings and the app menu live now (Windows, Linux)

### Windows (`windows/App`)

- **Menu bar:** none shown. The classic bar is built in `Window.cpp:255 buildMenus()` (File, Edit, View, Simulate, Tabs, Help) and shown only as one popup: the toolbar's ••• button (tooltip "Every menu (Alt)", `Toolbar.cpp:216`) or Alt/F10 (`Window.cpp:1394`). The popup holds the six menus as submenus.
- **Settings:** Edit menu, last item, "Settings...	Ctrl+,", `CMD_PREFERENCES` (`Window.cpp:305`, handler `:1849`, `showPreferencesDialog`). Shortcut registered in `Shortcuts.cpp:187` as Ctrl+Comma (changeable in Settings > Shortcuts). It is not in File, not a gear, not in the title menu. (The only other "Settings" is Gate Settings.)
- **Title bar:** custom frame (`Toolbar.cpp`): circuit name with its own menu (Rename, Duplicate, Version History, Export, Your Circuits; `Window.cpp:1904 titleMenu`), the tool groups in one of three styles (Minimal drops most), the feedback bubble, •••, then drawn Minimize / Maximize / Close with snap layouts.
- **Help menu:** Keyboard Shortcuts, Guided Tour, CedarLogic Help (F1), What's New, Send Feedback, Check for Updates, Add to/Remove from Start Menu (zip copies), About.
- **Convention gap:** Windows users look for a gear in the title bar or Settings/Options at the bottom of the File menu. Suggested: gear button just left of ••• (new `CMD_PREFERENCES` item in `Toolbar::build`, and in Minimal style too), and `Settings...	Ctrl+,` also at the end of File above Exit; keep Edit's entry. Ctrl+, already works.

### Linux (`linux/App`)

- **Menu bar:** none shown (`gtk_application_window_set_show_menubar(..., FALSE)`, `Window.cpp:267`). The same menu model (`main.cpp:288-399`: File, Edit, View, Simulate, Tabs, Help) opens as a popup from the toolbar's ••• (tooltip "Every menu (F10)", `Toolbar.cpp:193`) or F10 (`Window.cpp:menusFromKeyboard`).
- **Settings:** called "Preferences" in the menu, under Edit as the only item of its last section (`main.cpp:344`, action `win.preferences`); Ctrl+, from `Shortcuts.cpp:160` (`<Primary>comma`); Help text also says "Preferences (Ctrl+,)". The window itself is titled Settings (`Settings.cpp`, `settings::show`). Name mismatch with the Mac and Windows ("Settings").
- **Title bar:** the toolbar replaces the GTK header bar (`gtk_window_set_titlebar`, `Window.cpp:266`); window buttons drawn by `TitleButtons.cpp` using `gtk-decoration-layout`. Circuit-name menu: Rename, Duplicate, Version History, Export, Your Circuits (`Window.cpp:834-841`).
- **Help menu:** Keyboard Shortcuts, Guided Tour, Help, What's New, Send Feedback, Check for Updates, About.
- **Convention gap:** GNOME/Adwaita convention is a header-bar primary menu (hamburger, `open-menu-symbolic`) holding Preferences, Keyboard Shortcuts, Help, About, with the document menus elsewhere. Suggested: make the ••• a primary menu whose first section is New Window / Preferences (Ctrl+,) / Keyboard Shortcuts (Ctrl+?) / Help / About, with File/Edit/... kept below as submenus; rename the item to "Settings" or rename the Mac/Windows ones, but be consistent. Ctrl+, already works.

### Mac, for comparison

Settings... (Cmd+,) in the app menu (`CedarLogicApp.swift:61`, `PrefsWindow.shared.show()`), a native menu bar, About and Check for Updates in the app menu, Help menu with Contents, Send Feedback, Keyboard Shortcuts, What's New, Welcome, Guided Tour.

## 5. Notes for planning

- The 25-or-so "no" cells for Windows and Linux are almost all one piece of work: merge the 0.3.17 core (and the uncommitted fixes in this worktree) into the W and L trees, then add UI per feature. Estimated order of value: Step Clock, examples (data only, big payoff, port `TemplatesExamples.swift`'s generator or share the data), Check My Circuit clocked, Straighten/Tidy fixes, Lock in Place, Predict + projector, page links, drawing + notes, sync device removal, export parity.
- Examples are generated in Swift (`Templates.swift`, `TemplatesExamples.swift` via `mac/Tools/make-example-templates.mjs`); Windows and Linux build templates in C++ (`Collections.cpp`). A shared C++ example generator would serve all three.
- Web items to unlock are exactly the V4 checklist's: scope, split, formula, history, Straighten, wire dragging; the `APP_ONLY` lists in `scripts/build_pages.py:213` and `sim.js:590` must shrink in step (checked by `test_sim_files.mjs`).
- Windows and Linux already have 0.4 What's New and tour text that mention Check My Circuit, lab reports, share links and sync (`Welcome.cpp`); update it when the missing features land.

## 6. Windows re-checked, 2026-10-28 (branch ui/windows-menubar)

Sections 1 to 4 above are from 2026-10-15 and are stale for Windows: the
core has since been merged (Ink, PageLinks, CheckClocked, Sync device
removal and profile name are all in `mac/CedarCore`, and 5dbb585 is an
ancestor). Every Windows "no" or "partial" row, read again in
`windows/App`:

| Row | Windows now | Evidence |
|---|---|---|
| Lock in Place (part), Unlock All | yes | `CMD_LOCK_IN_PLACE`, Circuit menu, right-click menu |
| Find Next / Previous | yes | Edit menu, F3 / Shift+F3 (`CMD_FIND_NEXT`) |
| Page links, Connect/Disconnect All Pages | yes | Circuit menu, tab menu (`cl_pages_*` in `Window.cpp`) |
| Drawing on the circuit; Notes | yes | `Ink.h`, `Notes.cpp`, View menu |
| Straighten / Tidy Up on the fixed core | yes | core includes 5dbb585 |
| Step Clock, click a clock, toolbar button | yes | `CMD_STEP_CLOCK`, `cl_document_clock_step`, `showStepClock` |
| Check My Circuit, clocked | yes | `cl_check_clocked*` in `TruthTableWindow.cpp`; CI `check-timing*.png` |
| Sync: remove a device, name on every device | yes | `SyncApp.cpp` removeDevice / profileName |
| New pages share links | yes | `prefs().newPagesShareLinks` |
| Examples | yes (26, the Mac's own files) | `res/templates/templates.txt` |
| Projector mode | **closed on this branch** | View > Projector Mode, `cl_simview_draw_page` |
| Feedback screen recording | **closed on this branch** | `FeedbackCapture.cpp` |
| Menu bar / Settings location | **closed on this branch** | real menu bar, File > Settings (Ctrl+,) |
| Help > Welcome to CedarLogic | **closed on this branch** | `welcome::show` |
| Predict, then Reveal | no | core has it (`cl_simview_draw_predict`, `cl_simview_light_at`); Windows needs the guess/reveal UI of the Mac's `Predict.swift`. No core change needed; a UI round of its own |
| Export as Image: Match Window (dark) look, Share | partial | PNG and PDF, Colour / B&W, drawing included (`Export.cpp`); no dark look, no Windows share sheet |
| Tabs Modern / Classic setting | no | low impact; Windows tabs are the Modern cards |
| Beta / normal testing group | no | planned for the 0.4 release phase (updater channel + Settings choice) |
| Crash reports | no | no reporting service on Windows |
| Screen reader labels, high contrast | no | owner-drawn UI has no UI Automation; a project of its own |
| Classroom, Sync side records | no | Classroom is out of 0.4 |
| Presenter mode | n/a | Mac only |

Nothing found here needs a change to the shared core.
