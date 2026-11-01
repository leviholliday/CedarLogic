# Changelog

What changed in each version of CedarLogic. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Downloads and the full notes for each release are on [GitHub](https://github.com/leviholliday/CedarLogic-Releases/releases).

Versions 0.2 to 0.3.19 were Mac only. 0.4 is the first version for Mac, Windows and Linux together. Releases marked "beta" went to Beta testers first.

## [0.4] - Unreleased

### Added

- **Recently Deleted** in Your Circuits. A deleted circuit can be restored for 30 days, then it goes. Delete Now removes it for good, and asks first. Deleted circuits stay on that computer and aren't synced. (Mac, Windows, Linux)
- **Open the Practice Circuit** in the File menu (and the Help menu on the Mac). It opens as a new circuit of your own. (Mac, Linux)
- **A menu bar** on Windows and Linux: File, Edit, View, Simulate and the rest, with Alt and F10. Settings moved into File, and the ••• button is gone.
- **Record your window** for Send Feedback on Windows and Linux, for up to a minute, with your pointer and clicks.
- **Predict, Then Reveal** in Simulation View on Windows and Linux.
- **Match Window** in Export as Image on Windows and Linux, so a dark circuit exports dark. Linux also gets **Share**.
- **Projector Mode** in the View menu on Windows.
- **Testing group** in Settings > General on Windows and Linux (Normal tester or Beta tester, with Check Now), as the Mac has.
- On Windows, Settings has **Tabs** (Modern or Classic) and **Opening files**.
- **One-line installs:** Homebrew on the Mac, `install.ps1` (and winget, coming) on Windows, `install.sh` on Linux. Each checks what it downloads.

### Changed

- **Clear Drawing asks first**, and says Undo brings it back. (Mac, Windows, Linux)
- **Send Feedback** needs a short summary, and allows 10 reports a day. "Request more" gives 10 more, once a day. (Mac, Windows, Linux)
- Clicking where two pins sit close together, or dragging a wire at a corner, picks the same one every time.
- Holding C while moving parts, and Connect Nearby Pins, are much quicker on pages with thousands of parts.
- Part positions are saved to a ten-thousandth of a grid square, so save, reopen and save again gives the same file.
- On Windows and Linux, opening a circuit replaces the one you have open, as on the Mac. Settings can change it.
- On Windows and Linux, Ctrl+Tab shows a panel of your tabs like the Mac's, and panels fade in and out (unless the system's animations are off).
- On Windows, Send Feedback shows its progress, then a thank-you. If sending fails, what you wrote stays.
- On Linux, the launch screen no longer flashes the window, and Settings fits a short screen.

### Fixed

- A long round of engine checks found 40 problems, all fixed. Among them:
  - Pasting a label with two lines no longer quits the app.
  - A damaged `.cdl` file or paste no longer crashes or freezes the app, or connects a part to the wrong thing.
  - A `.cdl` file can no longer make a memory part read or overwrite a file on your computer.
  - Wires no longer fall into pieces or stretch after a drag or a paste, and disconnecting a RAM's data pin no longer leaves a wire to nothing.
  - Redo of Straighten Wires, Tidy Up or Build gives the same wires as the step itself.
  - Several parts with unusual sizes (a counter at its largest number, a wide register, gates with 0 or 1 inputs) no longer crash the app.
  - A RAM no longer grows without limit when you only read it, and a huge bus width or decoder size in a file no longer freezes the app.
  - Saving the same circuit twice gives the same file.
  - Cancelling Check My Circuit part way through is safe.
- Mac: after 500 undo steps, later changes are saved again. Changes in a gate's Settings sheet, and Undo and Redo, are saved.
- Mac: Undo and Redo of a move, Delete or Disconnect give back the wire bends you drew.
- Mac: Escape on a pasted copy or new part no longer brings it back on Redo, and a copy put on the pointer while drawing can be placed.
- Mac: Cmd-W with a sheet open no longer closes the tab behind it, and Option-Left in the oscilloscope no longer quits after a long recording.
- Mac: a closed circuit window lets go of its circuit, and a damaged file whose wire lost its id opens with a notice.
- Mac: the first-launch sound no longer holds up the launch.
- Windows: C now connects a gate you dragged from the side panel, and dragging draws one frame per screen refresh.
- Windows: Sync no longer fails when a security scanner holds a file for a moment.
- Windows: key hints say Esc, and pressing Alt again to leave the menu bar works.
- Linux: C connects a moving part wherever the keyboard focus is, Settings no longer prints a GTK warning, and a button that is off looks off.

### Security

- Windows: Share Link and the circuit attached to Send Feedback no longer carry your private Notes.
- Mac: your sync code and class keys are kept in the Keychain, not in a file that backups carried along.
- Mac: crash reports say where the app crashed and no more: no trail of clicks, no web addresses, no user name in file paths.
- Linux: your circuits' folders can only be read by you, and the sync code goes into the desktop's keyring when one is unlocked.

## [0.3.19] - 2026-10-27 (beta)

### Fixed

- One Cmd-Z takes back one thing, however long you have worked.
- A paste, Cmd-D copy or new part put away with Escape no longer comes back on Redo.
- A copy or new part put on the pointer while drawing can be placed.

## [0.3.18] - 2026-10-20 (beta)

### Fixed

- A closed circuit window no longer keeps simulating in the background.
- A file with one damaged wire opens with a note instead of crashing.
- Many wire fixes: wires left off their pins, a pin on two wires, wires on rotated parts splitting, pasted wires coming apart.
- Changes in a part's Settings sheet are saved and can be undone; Undo and Redo autosave.
- The oscilloscope no longer redraws the whole window while recording; Option-Left no longer quits.

## [0.3.17] - 2026-10-09 (beta)

### Added

- Two-page examples in New from Template: the part on page 1, built from gates on page 2.
- Click the clock part to step it when "Only on Step Clock" is on.
- Sync: remove old devices, and your name syncs.
- Classroom: share or post circuits from Your Circuits, and schedule class examples.

## [0.3.16] - 2026-10-08 (beta)

### Added

- Page links: choose which pages' TO/FROM labels connect.
- Export Image has a preview, Copy Image, Save and Share.
- Class pass for students.

### Fixed

- Straighten and Tidy Up no longer drop a connection on save, and big pages straighten about twice as fast.

## [0.3.15] - 2026-10-08 (beta)

### Changed

- Classroom reworked: share a circuit with the class, class examples, a simpler hand-in, and checking against a solution circuit.

## [0.3.14] - 2026-10-08 (beta)

### Added

- Classroom (File > Classroom): classes with a join code, assignments, hand-ins, and a live class. No accounts.

## [0.3.13] - 2026-10-07 (beta)

### Added

- Presenter mode shows your pointer, selection and what you're moving.
- A pulse button can be a template's clock.
- A Width control for drawing.

### Fixed

- Never stuck after opening a circuit; View > Make This Window Editable clears any stuck state.

## [0.3.12] - 2026-10-07 (beta)

### Added

- Draw on the circuit with a pen, highlighter or eraser, saved with it.
- Presenter mode, Notes, Step Clock (K), Predict and Projector mode, Lock in Place.
- Check My Circuit for clocked circuits, timing diagram worksheets, and new templates.

## [0.3.11] - 2026-10-06 (beta)

### Added

- A template for every flip-flop and latch.

## [0.3.10] - 2026-10-06 (beta)

### Fixed

- Quitting works again with Sync on.

## [0.3.9] - 2026-10-05 (beta)

### Fixed

- After the synced copy is reset, every device shows up again right away.

## [0.3.8] - 2026-10-05 (beta)

### Added

- Link a Mac to your sync by scanning a QR code.

## [0.3.7] - 2026-10-05 (beta)

### Added

- Sync Your Circuits across your devices, end-to-end encrypted.

## [0.3.6] - 2026-10-05 (beta)

### Fixed

- Full Rearrange is fast again on big pages; lab report and truth table fixes.

## [0.3.5] - 2026-10-04 (beta)

### Added

- Check My Circuit, Export Lab Report, Share Link, and a smarter Full Rearrange.

## [0.3.4] - 2026-10-04

### Fixed

- Build from Formula, NOR only: a negated product is built correctly.

## [0.3.3] - 2026-09-30 (beta)

### Changed

- Click outside a panel to close it.

### Fixed

- A crash with some saved parts.

## [0.3.2] - 2026-09-30

### Fixed

- The welcome comes in on top of your circuit; the disk image always shows Applications.

## [0.3.1] - 2026-09-30

### Added

- A new disk image, a first-launch sound, and credits in About CedarLogic.

## [0.3] - 2026-09-30

### Added

- A new name (just CedarLogic), a new icon, and a What's New tour.
- Your Circuits with autosave and version history, templates and My Parts.
- Truth tables with Karnaugh maps and the simplest SOP and POS; Build from Formula; Find.
- Timing diagrams from the oscilloscope; wires that light up when you point at them.

## [0.2.6] - 2026-09-30 (beta)

### Added

- Karnaugh maps, Build from Formula, timing diagrams, wire highlighting and Find.

## [0.2.5] - 2026-09-29 (beta)

### Changed

- The Simple interface is gone; `.cdl` files get their own icon.

## [0.2.4] - 2026-09-29 (beta)

### Added

- Send Feedback, with a screenshot or a short recording.

## [0.2.3] - 2026-09-29 (beta)

### Fixed

- The green window button could start out of place.

## [0.2.2] - 2026-09-29 (beta)

### Added

- Opt-in crash reports.

## [0.2.1] - 2026-09-27 (beta)

### Added

- Export for older CedarLogic, split view keys, Help > Set Up CedarLogic.

### Fixed

- Toolbar buttons take every click.

## [0.2] - 2026-09-27 (beta)

### Added

- The full CedarLogic interface, rebuilt natively for the Mac: tabs and split view, autosave with versions, Your Circuits, Settings, and Help.

## CedarLogic Classic (the legacy wx app)

The cross-platform wxWidgets app. It is retired; 4.0.2 was its last version.

- [4.0.2] - 2026-09-29 (beta): Send Feedback.
- [4.0.1] - 2026-09-25 (beta): updates from inside the app, and a Testing group setting.
- [4.0.0-beta.1] - 2026-09-24: the first CedarLogic 4 beta, for Mac and Windows.

[0.4]: https://github.com/leviholliday/CedarLogic/blob/main/docs/RELEASE-0.4-notes.md
[0.3.19]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.19
[0.3.18]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.18
[0.3.17]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.17
[0.3.16]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.16
[0.3.15]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.15
[0.3.14]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.14
[0.3.13]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.13
[0.3.12]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.12
[0.3.11]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.11
[0.3.10]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.10
[0.3.9]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.9
[0.3.8]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.8
[0.3.7]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.7
[0.3.6]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.6
[0.3.5]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.5
[0.3.4]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.4
[0.3.3]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.3
[0.3.2]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.2
[0.3.1]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3.1
[0.3]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.3
[0.2.6]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.6
[0.2.5]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.5
[0.2.4]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.4
[0.2.3]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.3
[0.2.2]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.2
[0.2.1]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2.1
[0.2]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v0.2
[4.0.2]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v4.0.2
[4.0.1]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v4.0.1
[4.0.0-beta.1]: https://github.com/leviholliday/CedarLogic-Releases/releases/tag/v4.0.0-beta.1
