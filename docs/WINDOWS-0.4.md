# CedarLogic for Windows 0.4: the menu bar, recording, and what changed

Branch `ui/windows-menubar` (not published to testers yet). Written for
the owner, not for programmers.

## The top of the window

Testers said two things felt wrong on Windows: there was no normal menu,
and Settings was hard to find (only a gear and a "•••" button). Now the
top of the window works like other Windows programs:

- **First row (the title bar):** the CedarLogic icon, then the menus
  **File, Edit, Circuit, View, Simulate, Window, Help**, and at the right
  Windows' own minimize, maximize and close buttons. Dragging an empty part
  of this row moves the window; double-clicking it maximizes; hovering the
  maximize button shows Windows 11's snap layouts, as before.
- **Second row:** the toolbar, exactly as before (the circuit's name, New,
  Open, Save, Undo, Zoom, the simulation controls, Draw, Lock, the gear for
  Settings). Settings > Toolbar still picks Seamless, Classic or Minimal.
- The **"•••" button is gone**. Everything it had is in the menus.

It follows light and dark mode (and Simulation View's dark look) and looks
right at every display scale from 100% to 200%.

### Using the menus with the keyboard

- **Alt** (or **F10**) highlights File and underlines a letter in each menu
  name. Left and Right move along; Enter or Down opens the menu.
- **Alt + a letter** opens that menu at once: Alt+F File, Alt+E Edit, Alt+C
  Circuit, Alt+V View, Alt+S Simulate, Alt+W Window, Alt+H Help.
- Inside a menu, the underlined letter picks the item; Left and Right go to
  the next menu; Escape steps back, a second Escape leaves the menus.
- The keyboard shortcut for each command is written beside it (Ctrl+S,
  Ctrl+E...). If you change a shortcut in Settings > Shortcuts, the menu
  shows the new one.
- In focus mode (Ctrl+.) the menu bar slides away with the toolbar; Alt
  still opens File, just under the tabs.

### What is in each menu

| Menu | What's in it |
|---|---|
| **File** | New, New from Template, Your Circuits (Ctrl+O), Open Recent, Import a File, Open the Practice Circuit · Save a Version, Version History, Rename, Duplicate, Save as Template · Export as Image, Export Lab Report, Export as CedarLogic File, Export for Older CedarLogic, Share Link · Print · **Settings (Ctrl+,)** · Close Window, **Exit** |
| **Edit** | Undo, Redo · Cut, Copy, Paste, Duplicate, Delete, Select All · Find, Find Next (F3), Find Previous (Shift+F3) |
| **Circuit** | Add a Gate (A), Gate Settings, Rotate (R) · Lock in Place, Unlock All Parts · Straighten Wires (S), Tidy Up, Tidy Up by Signal Flow, Connect Nearby Pins · Build from Formula, Save as Part · Connect All Pages, Disconnect All Pages |
| **View** | Zoom In, Zoom Out, Zoom to Fit, Actual Size · Dark Mode, Focus Mode, Gate Palette, Status Bar, Show Grid, Dots at Wire Bends, **Projector Mode** · Draw on the Circuit, Drawing Tools, Hide Drawing, Show Notes |
| **Simulate** | Running, Step Once, Step Clock (K), Simulation View (Ctrl+R), Lock the Circuit · Truth Table (T), Check My Circuit, Oscilloscope |
| **Window** | New Tab, Close Tab, Reopen Closed Tab, Rename Tab, Next Tab, Previous Tab · Split View, Switch Side, Close Split View · Minimize · a list of your open circuits (click one to bring it forward) |
| **Help** | CedarLogic Help (F1), Keyboard Shortcuts, Guided Tour, **Welcome to CedarLogic**, What's New · Send Feedback, Check for Updates, Add to / Remove from Start Menu (zip copies only) · **About CedarLogic** |

New in the menus (they used to be only behind the circuit's name or in
Settings): File > Rename and Duplicate, View > Show Grid and Dots at Wire
Bends, View > Projector Mode, Help > Welcome to CedarLogic, and the Window
menu's list of open circuits. The circuit's name in the toolbar still has
its own little menu too.

## Recording the screen for Send Feedback

Help > Send Feedback now has three tiles: **Screenshot** (as before),
**Record**, and **Add image**.

- **Record** hides the feedback form and starts recording **only the
  CedarLogic window** (never the rest of your screen). The window works
  normally while it records: show the problem.
- A small dark bar over the top of the window says **Recording
  CedarLogic**, counts the time, and has **Stop** and an **×** (throw it
  away). The bar itself is not in the video.
- It stops by itself after **one minute**. Closing the window, Exit, or
  choosing Send Feedback again also stops it.
- The form comes back with the recording attached (a picture of its first
  moment with a play sign and its length). **Record again** replaces it.
  One recording and up to three pictures can be sent.
- The video is an MP4, 15 frames a second, at most 1600 pixels wide: about
  1 to 4 MB a minute. The mouse pointer and a green ring where you click are
  drawn into it, so the developer can see what was pressed.
- It works on Windows 10 (version 1903 or newer) and Windows 11, on normal
  PCs and ARM laptops. It uses Windows' built-in video encoder (the graphics
  card's if there is one). On "N" editions of Windows without the Media
  Feature Pack, Record says so in plain words; screenshots still work.

Screenshots work exactly as they did.

## Other changes

- **Projector Mode** (View menu): Simulation View with very thick wires
  and big lights and labels, for the back of a classroom, as on the Mac.
- Help texts, the Settings > Toolbar notes and the Start-menu question no
  longer mention the old "•••" menu.
- Send Feedback's attachments have their own row, with a short note when
  nothing is attached yet.

## How it was checked (no Windows PC needed)

GitHub builds it for Intel/AMD and ARM on every run, and on Intel/AMD it
also:

- takes pictures of each menu open, in light and dark, in Simulation View,
  in focus mode and in the Classic toolbar style (`renders/menu-*.png`);
- drives the menu bar with the keyboard (Alt, arrows, Escape, Alt+V, a
  letter in a menu, Alt+W) and checks each step (`--menu-test`);
- opens Send Feedback, takes a screenshot, records three seconds while the
  window zooms, reads the MP4 back (its length and first frame), and throws
  a second recording away (`--feedback-capture-test`; the video and
  pictures are in the renders).

## What to try by hand on a Windows PC

1. Press Alt: File is highlighted and letters are underlined. Press Right
   twice, then Down: the Circuit menu opens. Press Escape twice.
2. Press Alt+F: File opens; Settings is near the bottom; Ctrl+, opens it too.
3. With a menu open, move the mouse across the other menu names: each opens
   in turn.
4. Change a shortcut in Settings > Shortcuts, then open that menu: the new
   keys show.
5. Switch dark mode (Ctrl+Shift+D): the menu bar and its menus turn dark.
6. Send Feedback > Record: click around for a few seconds, press Stop, then
   click the recording's picture to play it.
7. Try at 150% or 200% display scale and with a narrow window.
