# CedarLogic for Linux: what changed for 0.4

This covers the Linux app (Ubuntu, Debian, Raspberry Pi OS) in 0.4: the new
menu bar, recording for Send Feedback, Predict and Reveal, and what still
differs from the Mac app.

## The menu bar

Testers found the old ••• and ☰ buttons odd on Linux. Linux programs have a
menu bar, so CedarLogic now has one too. It sits in a thin row above the
toolbar, in the toolbar's colour, with the window's minimize, maximize and
close buttons at its ends. The ••• and ☰ buttons are gone.
Everything they held is in the menus.

| Menu | What's in it |
|---|---|
| **File** | New Circuit (Ctrl+N), New from Template…, Your Circuits… (Ctrl+O), Import a File… (Ctrl+I), Open Recent, Open the Practice Circuit · Save a Version (Ctrl+S), Rename…, Duplicate Circuit, Version History… · Export as Image… (Ctrl+E), Export Lab Report…, Export as CedarLogic File… (Ctrl+Shift+S), Export for Older CedarLogic, Share Link…, Save as Template… · Print… (Ctrl+P) · Close Window (Ctrl+Shift+W), Quit (Ctrl+Q) |
| **Edit** | Undo, Redo · Cut, Copy, Paste, Duplicate, Delete, Select All · Add a Gate… (A), Rotate (R), Lock in Place (Ctrl+L), Unlock All Parts, Gate Settings… · Find ▸ (Find…, Find Next, Find Previous) · Save as Part…, Build from Formula…, Straighten Wires (S), Tidy Up: Keep My Layout, Tidy Up: Full Rearrange, Connect Nearby Pins · **Settings… (Ctrl+,)** |
| **View** | Zoom In, Zoom Out, Zoom to Fit, Actual Size · Focus Mode (Ctrl+.), Full Screen (F11), Side Panel, Status Bar, Gridlines, Wire Connection Dots, Dark Mode (Ctrl+Shift+D) · Simulation View, Projector Mode, Oscilloscope · Draw on the Circuit, Drawing ▸, Show Drawing, Show Notes |
| **Simulation** | Simulation View (Ctrl+R), Running, Step Once, Step Clock (K) · Predict, Then Reveal, Reveal · Truth Table… (T), Check My Circuit… (Shift+T), Oscilloscope · Lock the Circuit |
| **Window** | New Tab (Ctrl+T), Close Tab (Ctrl+W), Reopen Closed Tab, Rename Tab…, Next Tab, Previous Tab · Split View, Switch Side, Close Split View · Connect All Pages, Disconnect All Pages · Minimize · the open circuits, to bring one to the front |
| **Help** | CedarLogic Help (F1), Keyboard Shortcuts (Ctrl+?), Guided Tour · What's New in CedarLogic, Welcome to CedarLogic · Send Feedback…, Check for Updates… · About CedarLogic |

The menus and their order follow the Mac app (File, Edit, View, Simulation,
Window, Help). The Mac's own CedarLogic menu has no Linux equivalent, so its
items moved to where Linux programs keep them: Quit in File, About and Check
for Updates in Help, and Settings in Edit.

**Why Settings is Edit > Settings…:** that's where most Linux programs with a
menu bar keep it (Firefox and Thunderbird call it Edit > Settings; Inkscape
and GIMP call it Edit > Preferences). The app already calls the window
"Settings", as the Mac app does, so the menu uses that name too. Ctrl+,
opens it, as before.

How it works:

- **Keys.** Each item shows its shortcut at the right. Change a shortcut
  in Settings > Shortcuts and the menu updates. That includes the canvas's
  one-letter keys (A, R, S, T, K).
- **Keyboard.** Alt plus the underlined letter opens a menu (Alt+F for
  File). F10 opens File.
- **Focus mode and full screen.** The menu row slides away with the
  toolbar. F10 still opens every menu, in one list at the top right.
- **Title bar.** The empty part of the menu row works as the title bar:
  drag it to move the window, double-click it to maximize, right-click it
  for the window menu.
- **Themes.** The row uses the toolbar's colours, so it looks the same in
  Adwaita, Yaru, and the Pi's PiXflat and PiXnoir, in light and dark.
  The Pi's grey text in windows that aren't in front doesn't affect it.

Other changes that came with the menus: Tidy Up's two kinds are named as on
the Mac, and Shift+S shows on whichever kind Settings picks. Tab commands
moved from the old Tabs menu to Window.

## Recording for Send Feedback

Send Feedback now has a **Record** tile, between Screenshot and Add image….
Screenshots work as before.

- Clicking **Record** hides the form and starts recording the CedarLogic
  window. A dark bar over the canvas shows the time, a **Stop** button and
  an **✕** to throw the recording away. When you stop, the form comes back
  with the recording attached. Its thumbnail has a play mark and shows how
  long it is. Clicking **Record again** replaces it.
- Only CedarLogic's own window is recorded. The app draws its own window
  a few times a second, so it works the same on X11 and on Wayland (where
  apps can't read the screen) and asks for no permission. The pointer is
  drawn in, with a red ring wherever you click. The bar over the canvas
  doesn't appear in the recording.
- **Limits:** recording stops by itself after 1 minute, or when the file
  gets to about 40 MB (MP4) or 11 MB (animated picture). Those are under
  the feedback site's limits.
- **What gets sent:**
  - **If the computer has ffmpeg:** an MP4 video, which plays in any
    browser. The Pi usually has ffmpeg. You can install it on any system
    with `sudo apt install ffmpeg`.
  - **If it doesn't** (Ubuntu doesn't by default): an animated PNG
    picture, which plays in any browser. The
    feedback site already accepts both types.
- **On a Pi:** when a circuit is big and frames take longer to draw,
  CedarLogic records fewer frames per second so the app stays usable. A
  window where nothing moves costs almost nothing to record.

## Predict, then reveal

Predict and Reveal are in the Simulation menu, as on the Mac. They work in
Simulation View:

1. Turn on **Predict, Then Reveal**. Every light and display is covered.
2. Make your guesses. Click a light to cycle ?, 0 and 1. On a display, click
   a digit to cycle it. You can also type 0–9 and A–F, use Tab to move
   between lights, and press Backspace to clear a guess.
3. Choose **Reveal** (or press Return). Each guess gets a ring: green if
   right, red if wrong, grey if you didn't guess. The status line shows the
   score, such as "3 of 4 right".
4. Once revealed, flipping a switch, Step or Step Clock starts a new round.
   So does **Cover the Lights Again**.

## Mac vs Linux (V4-PARITY-MATRIX, checked again 2026-10-28)

**Now on Linux** (missing when the matrix was written on 2026-10-15):

- Lock in Place
- Find Next and Find Previous
- Page links
- Drawing on the circuit
- Notes
- Step Clock, clicking a clock to step it, and its toolbar button
- Projector Mode
- Clocked Check My Circuit
- Sync: removing a device, and your name on every device
- New pages share links
- The full set of templates, including the Step Clock choice
- Screen recording for feedback (new in this round)
- Predict, then reveal (new in this round)

**Still different, on purpose or for later:**

- Classroom: comes after 0.4.
- Presenter mode on a second display: Mac only.
- Testing group (beta or normal) and crash reports: Linux has only the
  preview channel and no crash reporter.
- Export as Image's "Match Window" look: Linux exports Color or Black &
  White, plus PDF and SVG, which the Mac doesn't have.
- Making CedarLogic the default app for .cdl files (a Settings row on the
  Mac): on Linux, the .deb and the AppImage register .cdl files when
  they're installed.
- Screen-reader labels: partial on every platform.
