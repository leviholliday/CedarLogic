// `CedarLogic.exe --session-check [actions] [seed]`: a long working session
// in one circuit window, the Windows twin of the Mac's (mac/App/SessionCheck.swift).
// A real window on the desktop is driven through the app's own code -- the
// canvas's mouse and key messages, the tab strip's, the window's commands --
// by seeded random actions, a few hundredths of a second apart: parts placed
// from the side panel, wires drawn pin to pin and pin to wire, parts and
// selections dragged (and Escape mid-drag), rotate, delete, cut, copy,
// paste, duplicate, nudge, undo and redo (past the 500-step history too),
// tabs added, closed, reopened and moved, split view, Simulation View with
// run, pause, step and Step Clock, the oscilloscope, the truth table, Find,
// gate settings, Settings, notes, drawing, Straighten and Tidy Up's preview,
// locking, saving and reopening.
//
// Checked all along: no crash (a crash prints the action it happened in, a
// stack and the minidump it wrote) and no hang, nothing thrown into the
// app's guard, no Direct2D frame failing, the window's edited state (its
// title's *) following the circuit, autosave saving it, the file on disk
// reopening to the same circuit, the undo history's counts in range, every
// tab and side showing a page that exists, GDI and USER objects and memory
// levelling off, and -- once the window closes -- its timers stopped, its
// windows gone and every document freed.
//
// It works in a home folder of its own (CEDARLOGIC_HOME, a new folder in
// %TEMP%): your settings and Your Circuits are never touched. One PASS or
// FAIL line per check, then a summary; the exit code is 0 when all passed.

#ifndef CL_WINDOWS_SESSIONCHECK_H
#define CL_WINDOWS_SESSIONCHECK_H

#include <cstdint>
#include <string>

namespace sessioncheck {

// Called first thing, when the arguments ask for it: the home folder of its
// own, and the crash and hang handlers.
void prepare(uint64_t seed);
// Once the gate library is loaded: the session starts when the message loop does.
void start(int actions, uint64_t seed);
// After the message loop: 0 all passed, 1 a check failed.
int exitCode();

}  // namespace sessioncheck

// What the app notices going wrong, counted for the session check (and
// harmless otherwise).
namespace health {
extern long d2dFailures;       // Direct2D frames or targets that failed
extern long lastD2dError;      // the last one's HRESULT
extern long exceptions;        // thrown into guarded() (App.h)
extern std::string firstException;
extern int liveWindows;        // CircuitWindow objects
extern int liveCanvases;       // Canvas objects
void noteD2dFailure(long hr, const char* where);
}  // namespace health

#endif  // CL_WINDOWS_SESSIONCHECK_H
