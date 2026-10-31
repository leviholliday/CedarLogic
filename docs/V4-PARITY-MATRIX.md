# V4 feature parity matrix (Mac, Windows, Linux, Web)

Rewritten 2026-10-31 from the code, not from the 2026-10-15 version of this
file. Nothing was built or run: every cell is a read of the source named in
the last column. "Yes" means the feature is wired from a menu, key or control
down to a core call or its own implementation. It does not mean it was tried
on a screen, so behaviour bugs are out of scope here.

## What was read

| Tag | Tree | Branch / commit |
|---|---|---|
| Mac | `mac/App/*.swift`, `mac/CedarCore` | `private/v4/mac` @ 8855470 |
| Windows | `windows/App/*.cpp`, `windows/res`, `docs/WINDOWS-0.4.md` | `private/ui/windows-menubar` @ 54f1236 |
| Linux | `linux/App/*.cpp`, `linux/res`, `docs/LINUX-0.4.md` | `private/ui/linux-menubar` @ 46d32ba |
| Web | `public/assets/js/sim*.js`, `public/app/`, `scripts/build_pages.py` in `cedarlogic-site` | `main` @ d900041 |

The Windows and Linux branches are being added to by other agents while this
was written. Anything on that list is marked **In progress** (never "No")
and collected in section 12.

The biggest change since the 10-15 version: **`mac/CedarCore` is identical
on all three native branches** (`diff -rq` of the directory against each of
the Windows and Linux branches prints nothing). Windows and Linux no longer
lack any core call, so every "No" or "Partial" for them below is app code
(menus, windows, drawing), not a missing engine feature. The web app now has
the oscilloscope, Build from Formula, Version History, split view, Straighten
and Tidy Up (the router compiled to WebAssembly, `sim-route.js`), dragging a
wire's runs, and a Settings window; `LOCKED_MENU` in `sim.js:612` is empty.

Legend: **Yes**, **Partial** (what is missing is in the Notes), **No**,
**In progress** (Windows/Linux only: another agent is on it now), **n/a**
(does not apply to that platform). Paths are relative to the tree named above.

## 1. Menus and commands

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| A menu bar | Yes | Yes | Yes | n/a | Mac `CedarLogicApp.swift` (commands from line 54). Windows File, Edit, Circuit, View, Simulate, Window, Help in the title bar (`Window.cpp:270 buildMenus`). Linux File, Edit, View, Simulation, Window, Help (`main.cpp:544 buildMenubar`, `MenuBar.cpp`). Web has one title menu (`sim.js:1620`) |
| Menus show the user's own (changed) shortcuts | Yes | Yes | Yes | n/a | Mac `keys.menu(...)`; Windows `shortcuts::menuKeys`; Linux `canvasKeys` in `main.cpp` |
| Alt / F10 opens the menus; Alt+letter opens one | n/a | Yes | Yes | n/a | Windows 0.4 doc (Alt, arrows, Escape, tested by `--menu-test`); Linux `MenuBar.cpp`. macOS has the system menu bar |
| Settings opens from a menu and Ctrl/Cmd+, | Yes | Yes | Yes | Yes | Mac app menu (`CedarLogicApp.swift:64`). Windows File > Settings (`Window.cpp`) plus the toolbar gear (`Toolbar.cpp:67`). Linux Edit > Settings (`main.cpp:605`), **moving to File: In progress**. Web title menu, `sim-prefs.js` |
| Open Recent | Yes | Yes | Yes | n/a | Mac: the system's, from `DocumentGroup` (`CedarLogicApp.swift:42`). Windows `Window.cpp` recentMenu. Linux `main.cpp` gRecentMenu. Web: Your Circuits |
| Window menu lists the open circuits | Yes | Yes | Yes | n/a | Windows `rebuildWindowList`; Linux `gWindowMenu`; Mac the system's |
| Open the Practice Circuit | No | Yes | Yes | n/a | Windows/Linux `main.cpp` (`open-sample`). Not on the Mac (see section 13) |
| Right-click / long-press context menus | Yes | Yes | Yes | Yes | Windows `Window.cpp`; Linux `showContextMenu`; Web `sim-settings.js` partMenu |
| Customizable shortcuts, searchable list, Restore Defaults | Yes | Yes | Yes | No | Mac `ShortcutStore.swift` (about 45 actions). Windows `Shortcuts.cpp` (67 entries), Linux `Shortcuts.cpp` (59). Web Settings > Shortcuts is a read-only list built from `sim-keys.js` |
| Keyboard Shortcuts sheet | Yes | Yes | Yes | Partial | Mac `ShortcutsSheet.swift`; Windows/Linux `ShortcutsSheet.cpp`. Web only inside Settings, and the `?` key does not open it |

## 2. Editing

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Select, move, rotate, copy, cut, paste, delete, undo, redo | Yes | Yes | Yes | Yes | |
| Duplicate (Cmd/Ctrl+D) and its clipboard setting | Yes | Yes | Yes | Partial | Web duplicates but has no "Duplicate uses the clipboard" setting |
| Add a Gate (A), quick add | Yes | Yes | Yes | Yes | Mac `QuickAddView.swift`; Web `sim.js:925` fuzzy search |
| Nudge with arrow keys | Yes | Yes | Yes | Yes | `cl_edit_nudge` |
| Connect Nearby Pins (menu) | Yes | Yes | Yes | No | Mac `CanvasView.swift:1177`; Windows/Linux `Window.cpp` `connectNearby`. Web: listed as not online in `build_pages.py` |
| C while dragging connects nearby pins | Yes | Yes | Yes | No | Mac `CLCanvas.swift:336`; Windows `Canvas.cpp:1522`; Linux `Canvas.cpp:1473` (`cl_edit_connect_while_moving`). Range tuning on Windows/Linux: **In progress** |
| Lock in Place, Unlock Part, Unlock All Parts | Yes | Yes | Yes | Yes | Mac `CedarLogicApp.swift:708`; Windows `CMD_LOCK_IN_PLACE`; Linux `win.lock-in-place`; Web `lockSel` (`sim.js`) |
| Lock the circuit (nothing editable, switches still work) | Yes | Yes | Yes | Yes | |
| Part settings (inspector) and RAM/ROM editor | Yes | Yes | Yes | Yes | Mac `InspectorView.swift`, `RamEditorView.swift`; Windows `Dialogs.cpp`; Linux `Dialogs.cpp`, `RamEditor.cpp`; Web `sim-settings.js` |
| Find, Find Next, Find Previous | Yes | Yes | Yes | No | Mac `FindBar.swift` + Find menu; Windows `FindBar.cpp`, Edit menu F3 / Shift+F3; Linux `FindBar.cpp`, Edit > Find. Web: palette search only |
| Zoom in/out/fit, Actual Size | Yes | Yes | Yes | Partial | Web has zoom, wheel/pinch and Fit; no Actual Size (`build_pages.py` KEY_GROUPS) |
| Focus mode | Yes | Yes | Yes | No | Mac `CLWindow.swift`; Windows/Linux `Window.cpp` |
| Minimap | Yes | Yes | Yes | Yes | Mac `CLSidePanel.swift`; Windows `Palette.cpp`; Linux `MiniMap.cpp`; Web `sim.js` |
| Side panel: categories, search, drag to canvas, Shift+1..0 jumps | Yes | Yes | Yes | Partial | Web has no category-jump keys |
| My Parts (save a selection as a part) | Yes | Yes | Yes | No | Mac `MyParts.swift`; Windows/Linux `Collections.cpp` + `Palette.cpp`. Web: "Templates and My Parts" are listed app-only in `build_pages.py:232` |
| Tabs: new, close, rename, reorder, Reopen Closed Tab | Yes | Yes | Yes | Partial | Web has new, close, rename (double-click), reorder; no Reopen command and no Cmd+T/W (Undo brings a closed page back) |
| Split view, Switch Side, Close Split | Yes | Yes | Yes | Yes | Mac `CLWindow.swift` SplitState; Windows `CMD_SPLIT_VIEW`; Linux `win.split-view`; Web `sim-split.js` |
| Ctrl/Cmd+Tab switcher (panel of tabs, most recent first) | Yes | Yes | Yes | Partial | Mac `TabSwitcher.swift`; Windows/Linux `TabSwitcher.cpp` (Ctrl+Tab look: **In progress**). Web steps tabs with Ctrl+Tab / Alt+Shift+arrows (`sim-files.js:417`), no panel |
| Page links (TO/FROM across pages), per-tab Connect to, Connect/Disconnect All Pages | Yes | Yes | Yes | Yes | Mac `CedarLogicApp.swift:391`; Windows `Window.cpp:833`; Linux `main.cpp:667`; Web `sim-files.js:521` |
| "New pages share links" setting | Yes | Yes | Yes | Yes | Mac `SettingsView.swift:464`; Windows `Settings.cpp:627`; Linux `Prefs.cpp`; Web `sim-prefs.js` links |
| Wire value tag when resting on a wire | Yes | Yes | Yes | Partial | Mac `SettingsView.swift:437`; Windows `Canvas.cpp:306`. Web highlights the hovered wire and sets a tooltip; no value tag found |

## 3. Wires and routing

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Draw wire pin to pin | Yes | Yes | Yes | Yes | |
| Drag a wire's runs / bends | Yes | Yes | Yes | Yes | Web `sim-wire.js`, `sim.js:3548` (run cursors); the old file said No |
| Straighten Wires (S) | Yes | Yes | Yes | Yes | `cl_edit_straighten` (Windows `Window.cpp:2573`); Web `sim-route.js` (WebAssembly router) |
| Tidy Up, Keep My Layout and Full Rearrange, preview, Return/Esc/Tab | Yes | Yes | Yes | Yes | `cl_edit_tidy_*`; Windows `Canvas.cpp:1494`, `Window.cpp:2613 switchTidyMode`; Linux `Canvas.cpp:1402`; Web `sim-route.js:490` |
| Tidy Up mode setting (keep / rearrange) | Yes | Yes | Yes | Yes | Mac `SettingsView.swift:471`; Web Settings > Editing |
| Wire look settings (thickness, low-wire colour, bend dots, dot size) | Yes | Yes | Yes | Partial | Web: thickness, dots, dot size; no low-wire colour choice |

## 4. Simulation, checking, learning tools

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Run, pause, step, speed | Yes | Yes | Yes | Yes | |
| Step Clock (K), toolbar button, click a manual clock to step it | Yes | Yes | Yes | Yes | Mac `CanvasView.swift:472, 1848`; Windows `Window.cpp:2770`, `Canvas.cpp:1211`, toolbar `Canvas.cpp:512`; Linux `Window.cpp:2406`, `Canvas.cpp:1306`; Web "Step Clock" in the title menu and `K` |
| Simulation View | Yes | Yes | Yes | Yes | Windows `CMD_SIM_VIEW`; Linux `win.sim-view`; Web `sim.js` |
| Projector mode | Yes | Yes | Yes | Yes | Windows `CMD_PROJECTOR`; Linux `win.projector`; Web `sim-prefs.js` |
| Predict, Then Reveal | Yes | **No** | Yes | Yes | Mac `Predict.swift`; Linux `Predict.cpp`, Simulation menu; Web `sim.js` predict. Windows: no code, no menu item (the word appears only in `Canvas.cpp:242` as a state reset) |
| Presenter mode (mirror to a second display) | Yes | No | No | No | Mac `Presenter.swift`, `RenderPresenter.swift`. Mac only by design |
| Oscilloscope and timing diagrams | Yes | Yes | Yes | Yes | Mac `ScopeView.swift`, `TimingDiagram.swift`; Windows/Linux `Scope.cpp`; Web `sim-scope.js` |
| Truth table, Karnaugh maps, simplest formulas, CSV | Yes | Yes | Yes | Yes | Mac `TruthTableView.swift`; Windows/Linux `TruthTableWindow.cpp`; Web `sim-truth.js`, `truth-table.js` |
| Build This as a Circuit (from the table) | Yes | Yes | Yes | Yes | Web `sim-truth.js:589` hands the table to `sim-formula.js` |
| Build from Formula | Yes | Yes | Yes | Yes | Mac `BuildFormulaView.swift`; Windows/Linux `Dialogs.cpp` + `Formula.cpp`; Web `sim-formula.js` |
| Check My Circuit: formula, minterms, table | Yes | Yes | Yes | Yes | Mac `CircuitCheck.swift`; Windows/Linux `TruthTableWindow.cpp` (`cl_check_expected`, `cl_check_table`); Web `sim-check.js` |
| Check My Circuit: clocked (count, state table, timing table) | Yes | Yes | Yes | Yes | Windows/Linux `cl_check_clocked*`, `cl_check_step_count` in `TruthTableWindow.cpp`; Web `sim-check.js:1026` |
| Templates: New from Template, Save as Template, Clock choice | Yes | Yes | Yes | Partial | Mac `Templates.swift`, `TemplatesExamples.swift`. Windows `Collections.cpp` reads `res/templates` (27 files plus the Lab Page built in code: Flip-Flops and Latches 10, Combinational 5, Registers 5, Classic Mistakes 4, Counters 1, Built In 2). Linux `Collections.cpp` + `res/templates`. Web has no New/Save as Template; it has Examples (below) |
| Built-in examples | Yes | Yes | Yes | Yes | Same 28 on Mac, Windows and Linux (Lab Page plus the 27 above; Windows and Linux are generated from the Mac's, `Templates.swift:141-429`). Web: about 33 examples in six groups incl. Classic Mistakes (`sim.js:553 EX_CATS`) |
| Lab Page starter | Yes | Yes | Yes | No | |

## 5. Drawing and notes

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Draw on the circuit: pen, highlighter, eraser, three widths, colours | Yes | Yes | Yes | Yes | Mac `Drawing.swift`; Windows `Canvas.cpp:566` (drawing bar), `Ink.h`; Linux `Ink.cpp`, `Canvas.cpp`; Web `sim-draw.js`. Windows takes pen input (`WM_POINTER*`, `Canvas.cpp:1011`); Linux handles a tablet eraser (`Canvas.cpp:1274`) |
| Show / Hide Drawing, Done Drawing | Yes | Yes | Yes | Yes | |
| Clear Drawing on This Page | Yes (no prompt, one Undo step) | Yes | Yes | Yes | Mac `Drawing.swift:137` clears "without asking". A confirmation on Windows/Linux: **In progress**. Mac has none, so decide which behaviour is right and make all three match |
| Drawing carried in the .cdl, sync, share link (when shown and it fits) | Yes | Partial | Yes | Yes | Linux `main.cpp:801-815` as the Mac. Windows `Window.cpp:2307` calls plain `cl_document_save_text`, so the link always carries the drawing and does not drop it when too long (see the privacy row in section 7) |
| Notes for the circuit | Yes | Yes | Yes | Yes | Mac `Notes.swift`; Windows `Notes.cpp`; Linux `Notes.cpp`; Web `sim-notes.js`. Notes never go in a share link (Mac, Linux, Web; see Windows in section 7) |

## 6. Files and library

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Your Circuits library, saves as you work | Yes | Yes | Yes | Yes | Mac `LibraryView.swift`; Windows `Library.cpp`, `LibraryWindow.cpp`; Linux same names; Web `sim-library.js` (IndexedDB) |
| Version History (restore a version) | Yes | Yes | Yes | Yes | Mac `LibraryView.swift:722`; Windows/Linux `LibraryWindow.cpp`; Web `sim-history.js` |
| Recently Deleted (a list you can restore from) | No | No | No | Yes | **Native apps only move a deleted circuit to a hidden `.Trash` folder inside the library** (Mac `LibraryView.swift:136`, Windows `Library.cpp:243`, Linux `Library.cpp:232`); no window lists it. Web `sim-library.js` has the sheet (30 days). The old file said Yes for Mac, Windows and Linux; that was wrong |
| Open / import .cdl v1, v2, v3 | Yes | Yes | Yes | Yes | |
| Save / export .cdl; Export for older CedarLogic (v2, v1.x) | Yes | Yes | Yes | Partial | Web only offers "Download for Classic CedarLogic" when needed (`sim-draw.js`). Windows/Linux submenu "For CedarLogic 2 / 1.x" |
| Recovery of unsaved work | Yes | Yes | Yes | Yes | Windows/Linux `Recovery.cpp`; Web IndexedDB |
| Double-click .cdl / `cedarlogic://` link opens the app | Yes | Yes | Yes | Yes | Mac `Info.plist`; Windows installer + `Integration.cpp`; Linux `res/cedarlogic.desktop`; Web manifest file handler |
| "Open .cdl files with CedarLogic" row in Settings | Yes | Partial | Yes | n/a | Windows has a Start menu switch (`Settings.cpp:537`) but no default-app row. Linux `Settings.cpp:365` (`xdg-mime`) |

## 7. Export, print and sharing

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Export as Image: formats | PNG, PDF | PNG, PDF | PNG, PDF, SVG | PNG | Mac `ExportImageView.swift`; Windows `Export.cpp:346`; Linux `ExportImage.cpp:5`; Web `sim-picture.js` (no PDF) |
| Export as Image: looks | Match Window, Light, Black & White | Colour, Black & White | Colour, Black & White | Screen, Light, Dark, Print | **No "Match Window" on Windows/Linux**: a dark-mode circuit cannot be exported as it looks. Mac `ExportImageView.swift:209` |
| Export as Image: preview, grid lines, Include drawing, name and result strip, Copy | Yes | Yes | Yes | Yes | Windows `Export.cpp:211-253`; Linux `ExportImage.cpp:316-345` |
| Export as Image: Share (system share sheet) | Yes | No | No | n/a | Mac `ExportImageView.swift:381` (macOS share picker). Web has a separate "Share…" (`sim.js`, Web Share) |
| Print | Yes | Yes | Yes | Yes | Mac `ExportPrint.swift`; Windows/Linux File > Print; Web through the browser |
| Export Lab Report (PDF) | Yes | Yes | Yes | No | Mac `LabReport.swift`; Windows `LabReport.cpp` + `Pdf.cpp`; Linux `LabReport.cpp`. Web can save only a timing-diagram picture (`sim-scope.js:76`) |
| Share Link (copy a link; website opens it) | Yes | Yes | Yes | Yes | Mac `ShareLink.swift`; Windows `ShareLink.cpp`, `Window.cpp:2302`; Linux `main.cpp:795`; Web `sim-share.js` |
| Share Link leaves out the student's Notes | Yes | **No** | Yes | Yes | Mac `ShareLink.swift:26` (`CL_SAVE_NO_NOTES`); Linux `main.cpp:810`; Windows `Window.cpp:2307` uses `cl_document_save_text(doc)`, which writes notes and drawing (`CedarCore.h:716` says the `_ex` call exists to leave them out). **Pasting a Windows link into a chat shares the student's notes** |
| Send Feedback > "send this circuit too" leaves out Notes | Yes | **No** | Yes | n/a | Mac `Feedback.swift:268`; Linux `Feedback.cpp:885`; Windows `Feedback.cpp:1132` uses plain `cl_document_save_text` |

## 8. Sync and Classroom

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Sync: turn on, show code, "I have a code", QR to scan from a phone | Yes | Yes | Yes | Yes | Mac `Sync.swift`, `SyncSettingsView.swift`; Windows `SyncApp.cpp`, `SyncUI.cpp`; Linux same; Web `sim-library.js`, `sync-core.js`, `qrscan.js` |
| Sync: device list, name, Remove a device | Yes | Yes | Yes | Yes | Mac `Sync.swift:388`; Windows `SyncUI.cpp:549-669`; Linux `SyncUI.cpp:786-957`; Web `sim-library.js:840`. Old file said No for Windows/Linux |
| Sync: your name on every device (profile record) | Yes | Yes | Yes | Yes | Mac `Sync.swift:361`; Windows `SyncApp.cpp:346`; Linux `SyncApp.cpp:50` |
| Sync: Turn Off with "also remove synced circuits" | Yes | Yes | Yes | Yes | Windows `SyncUI.cpp:463` |
| Classroom (teacher and student) | Yes | No | No | Yes | Mac `Classroom*.swift`, `RenderClassroom.swift`; Web `sim-classroom.js`, `classroom-core.js`. The core compiles on Windows/Linux (`CMakeLists.txt`) but nothing in `App/` calls it. Out of 0.4 per the plan |
| Make This Window Editable / Hand In | Yes | No | No | Yes | Classroom only |

## 9. Send Feedback

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Send Feedback window (summary, details, tags, priority, email, contact OK) | Yes | Yes | Yes | No | Mac `FeedbackView.swift`; Windows/Linux `Feedback.cpp`. Web has no feedback form in the simulator |
| Summary required before Send | Yes | Yes | Yes | n/a | Mac `Feedback.swift:259`; Windows `Feedback.cpp:1122`; Linux `Feedback.cpp:872` ("Write a short summary first."). Already on both branch tips; also on the in-progress list |
| Screenshot of the circuit window | Yes | Yes | Yes | n/a | Mac `FeedbackCapture.swift`; Windows `Feedback.cpp:793`; Linux `Capture.cpp` |
| Record the window (1 minute, only the app window, pointer and click ring) | Yes | Yes | Yes | n/a | Mac `AVAssetWriter` MP4 (`FeedbackCapture.swift`); Windows Media Foundation MP4 (`FeedbackCapture.cpp:229`, needs the Media Feature Pack on N editions); Linux MP4 through ffmpeg, else animated PNG (`Capture.cpp`) |
| Add image (up to three) | Yes | Yes | Yes | n/a | |
| Send the circuit too | Yes | Yes | Yes | n/a | Windows version leaks notes, see section 7 |
| Sending progress, and failure keeps the draft | Yes | Yes | Yes | n/a | Mac progress bar with MB (`Feedback.swift:298`); Windows/Linux text in the form and a "wasn't sent" note |
| Confirmation screen after sending ("Thank you", Send Another, Done) | Yes | Partial | Partial | n/a | Mac `FeedbackView.swift:379`. Windows/Linux: a one-line note "Feedback sent. Thank you!" and the window closes. **Confirmation screen: In progress** |
| "Request more" | Not found | In progress | In progress | n/a | The words do not appear in the Mac code or on the site; the nearest thing on the Mac is the sent screen's Send Another. Ask the agent on this what it is meant to match |
| Fades on the feedback and other windows | Yes | In progress | In progress | n/a | |

## 10. Updates, testing groups, diagnostics

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| In-app update check and install | Yes | Yes | Yes | Partial | Mac Sparkle (`Updates.swift`); Windows `Updater.cpp` and Linux `Updater.cpp` (signed manifest, SHA-256, `windows/linux-native-testing` tag). Web `/app/` shows an "Update ready" bar (`app.js:307`); the online page just loads the new site |
| "Check for new versions" switch | No | Yes | Yes | n/a | Windows `Settings.cpp:555`; Linux `Settings.cpp:382` |
| Beta / normal testing group (two update feeds) | Yes | No | No | n/a | Mac `Updates.swift:10,52`, `SettingsView.swift:326`. Windows/Linux have one feed (the `*-native-testing` tag) |
| Crash reports (opt-in switch, Sentry) | Yes | No | No | No | Mac `CrashReports.swift`, `SettingsView.swift:336`. Windows/Linux `SessionCheck.cpp` is the CI `--session-check`, not a reporter |
| What's New window | Yes | Yes | Yes | Partial | Mac `WhatsNew.swift`; Windows/Linux `Welcome.cpp`. Web's menu opens the website page `/whats-new/` (`sim.js:1678`) |
| Welcome / first run, Guided Tour | Yes | Yes | Yes | Partial | Web: first-run toast and coach marks in the `/app/` phone shell only (`sim-mobile.js:1211`); no tour |
| Help window with search | Yes | Yes | Yes | Partial | Mac `Help.swift`; Windows/Linux `Help.cpp`. Web links to the Learn pages |
| Launch screen with first-launch sound | Yes | Yes | Yes | No | Mac `Splash.swift`; Windows/Linux `Splash.cpp` |
| Quit confirmation | Yes | Yes | Yes | n/a | Mac `QuitConfirm.swift`; Linux `QuitConfirm.cpp`; Windows `Window.cpp:2035` |

## 11. Settings window, row by row

Mac pages: General, Appearance, Canvas, Toolbar, Shortcuts, Sync
(`SettingsView.swift:10`). Windows (`Settings.cpp:486`) and Linux have the same
six. Web has five (`sim-prefs.js:26`): Appearance, Canvas, Editing, Shortcuts,
About.

| Setting | Mac | Windows | Linux | Web | Notes |
|---|---|---|---|---|---|
| Your name (printed on exports) | Yes | Yes | Yes | Yes | Web: Editing |
| Opening a circuit (replace / new window) | Yes | Yes | Yes | n/a | |
| New circuits start from (template) | Yes | Yes | Yes | No | |
| Opening .cdl files default-app row | Yes | Partial | Yes | n/a | see section 6 |
| Ask before quitting | Yes | Yes | Yes | n/a | |
| Status bar | Yes | Yes | Yes | No | |
| Testing group (beta / normal) | Yes | No | No | n/a | |
| Crash reports | Yes | No | No | No | |
| Theme at launch | Yes | Yes | Yes | Partial | Web: Light or Dark only, no follow-system or last-used |
| App colour (accent) | Yes | Yes | Yes | Yes | Web 7 colours, same names |
| Tabs: Modern / Classic | Yes | **No** | Yes | No | Mac `Prefs.classicTabs`; Linux `Settings.cpp:404`; no `classicTabs` anywhere in the Windows tree |
| Grid on/off, style, darker every 5 | Yes | Yes | Yes | Yes | |
| Wire thickness, low-wire colour, bend dots, dot size | Yes | Yes | Yes | Partial | Web has no low-wire colour |
| Gate size, side-panel names and category keys | Yes | Yes | Yes | No | |
| Wire value tag; mouse wheel; trackpad; reverse zoom; right-click rotates; Duplicate clipboard | Yes | Yes | Yes | No | Web only has Tidy, New pages, name, haptics |
| New pages share links; Tidy Up mode | Yes | Yes | Yes | Yes | |
| Toolbar: three styles (Seamless, Classic, Minimal) with pictures, groups, circuit name, Step Clock | Yes | Yes | Yes | No | Web has only "Show Step Clock in the top bar" |
| Dark-mode switch as a toolbar option | No | Yes | Yes | n/a | `showThemeToggle` |
| Shortcuts page: record, conflicts, Restore Defaults | Yes | Yes | Yes | No | |
| Sync page | Yes | Yes | Yes | Yes (sheet) | |

## 12. In progress on Windows and Linux (do not count as gaps)

Being added by other agents at the time of writing. The Mac column says what
the target is.

| Item | Mac today | Windows | Linux |
|---|---|---|---|
| Feedback confirmation screen | `FeedbackView.swift:379` | In progress | In progress |
| Required summary | yes | on branch tip already | on branch tip already |
| "Request more" | not found in Mac code | In progress | In progress |
| Fades | yes | In progress | In progress |
| Ctrl+Tab look | `TabSwitcher.swift` | In progress | In progress |
| C-connect range | `CLCanvas.swift:336` | In progress | In progress |
| Clear-drawing confirmation | **none on Mac** ("without asking", one Undo) | In progress | In progress |
| Settings under File | app menu | already under File | In progress (now Edit > Settings) |

## 13. On Windows / Linux but not on the Mac (keep when merging)

Open the Practice Circuit (File); Gate Settings as a menu command; Rename Tab
as a menu command; Next/Previous Tab in the menu; Gate Palette / Status Bar
toggles in View; Tidy Up by Signal Flow as its own item; Windows Projector,
Show Grid and Dots at Bends in the menus; "Check for new versions" switch;
dark-mode toolbar button option; Alt/F10 menu navigation; Linux Full Screen
(F11), PNG/PDF/SVG export, ffmpeg-or-APNG recordings; Windows installer,
Start menu offer, ARM64 build; the CI helpers (`--screenshot`, `--show`,
`--menu-test`, `--feedback-capture-test`, `--session-check`).

## 14. Accessibility and keyboard-only use

| Feature | Mac | Windows | Linux | Web | Notes / where |
|---|---|---|---|---|---|
| Screen-reader labels on controls | Partial | No | No | Yes | Mac labels on the drawing tools, sim bar, notes, sync QR, Karnaugh map (`Drawing.swift`, `SimBar.swift`, `TruthTableView.swift:657`); the canvas is exposed only for Predict (`CanvasView.swift:272`). Windows: no UI Automation / `WM_GETOBJECT` code (owner-drawn Direct2D). Linux: one accessible name on the Notes box (`Notes.cpp:71`). Web `aria-*` throughout `sim*.js` |
| Pick and wire parts from the keyboard | No | No | No | Yes | Web `sim.js:2945 pickNext`, W to wire, arrows. The native canvases have arrow-key nudge but no way to pick a part or make a wire without the mouse |
| Menus reachable by keyboard | Yes | Yes | Yes | Yes | Windows Alt/F10 + letters; Linux F10 / Alt; Linux also context menu from the keyboard (`Window.cpp:909`, Menu key, Shift+F10) |
| Reduced motion respected | Partial | Yes | Yes | Yes | Mac only the splash (`Splash.swift:93`); Windows `Welcome.cpp`, `Window.cpp`; Linux `Anim.h`; Web `prefers-reduced-motion` |
| High-contrast | No | No | No | Partial | One reference in `sim.js`; nothing in the native apps |

## 15. Gaps to close before 0.4

Ordered by how soon a student or teacher would run into it. "(IP)" is already
being worked on.

1. **Windows has no Predict, Then Reveal.** Mac, Linux and Web have it, and it
   is the main Simulation View classroom activity. Windows needs the
   `Predict.cpp` equivalent, the Simulation menu items and the cover/ring
   drawing. (`linux/App/Predict.cpp` is the model.)
2. **Windows leaks the student's Notes.** Share Link (`Window.cpp:2307`) and
   Send Feedback's "send this circuit" (`Feedback.cpp:1132`) use
   `cl_document_save_text`. Switch to `cl_document_save_text_ex` with
   `CL_SAVE_NO_NOTES` (and the Mac/Linux drawing rules) as `linux/App/main.cpp:795-815`
   does. A student pasting a link into a chat shares their notes; this is the
   one gap that can hurt someone, and it is a few lines.
3. **Export as Image on Windows and Linux has no Match Window look** (dark
   mode circuits export light only) and no Share. Add the third look to
   `Export.cpp` / `ExportImage.cpp`.
4. **Deleting a circuit has no way back inside any native app.** Windows,
   Linux and Mac move it to a hidden `.Trash` folder in the library; the web
   has a Recently Deleted sheet. Add a Recently Deleted list to Your Circuits
   (the sync dialogs on all three already promise "Recently Deleted").
5. **The web app is missing the teaching-day basics**: Find, New from Template
   / Save as Template / My Parts, a Lab Report PDF and PDF export, focus mode,
   Reopen Closed Tab, Connect Nearby (and C while dragging), Actual Size, a
   welcome/tour, and Send Feedback. These are exactly what `build_pages.py`
   `APP_ONLY` lists as "a few more tools".
6. **Polish a student will see on Windows/Linux (IP):** feedback confirmation
   screen and "Request more", fades, the Ctrl+Tab panel look, the C-connect
   range, Settings under File on Linux, and the clear-drawing confirmation
   (decide whether the Mac should confirm too, since it currently does not).
7. **Windows Settings is missing Tabs Modern/Classic** and the "Opening .cdl
   files" row; Linux and Mac have both. Web Settings lacks most of the Mac's
   rows (toolbar, gate size, mouse/trackpad, status bar, template for new
   circuits, shortcut editing).
8. **Accessibility.** Windows and Linux are close to nothing for a screen
   reader (owner-drawn UI); Mac covers controls but not the canvas; nobody on
   a native app can pick or wire a part from the keyboard (the web can).
   Neither native app follows high-contrast settings.
9. **Update channel and diagnostics.** Windows and Linux have one update feed
   and no beta/normal setting, and no crash reporting. Only testers and the
   developer notice, but a beta channel is the way to ship a bad build to
   few people first.
10. **Classroom on Windows and Linux, Presenter mode on everything but the
    Mac.** Out of 0.4 per the plan; listed so they are not forgotten.
11. **Mac small items:** no Open the Practice Circuit (Windows and Linux have
    it); the sent-feedback and clear-drawing behaviours above should be
    decided once and copied to all three.

## 16. How to re-check

- Core in step: `diff -rq mac/CedarCore <other tree>/mac/CedarCore` (empty on
  all three branches today).
- Menus: Windows `Window.cpp:270 buildMenus`; Linux `main.cpp:544 buildMenubar`;
  Mac `CedarLogicApp.swift` commands; Web `sim.js:1620`.
- A feature's presence on a native app is usually one `cl_*` call:
  `cl_edit_lock_selection`, `cl_document_clock_step`, `cl_pages_*`, `cl_ink_*`,
  `cl_notes*`, `cl_check_clocked*`, `cl_edit_connect_while_moving`,
  `cl_document_save_text_ex`. Grep the other trees for the call the Mac makes.
- Web: `scripts/build_pages.py` `APP_ONLY`, `BOTH` and `KEY_GROUPS` are the
  site's own statement of what it has; this file agrees with them.
