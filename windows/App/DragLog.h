// Measuring a drag on the canvas (windows/DRAG-DIAGNOSIS.md). Off unless the
// app is started with --drag-log <file> (or CEDARLOGIC_DRAG_LOG=<file>), or
// --drag-selftest [file], which drags a gate by itself with real input
// (SendInput) and prints PASS or FAIL.
//
// The log is plain text, one line per event, tab-separated:
//   M  a pointer move during a drag (or with a gate on the pointer)
//   P  a paint of the canvas while dragging
//   D  a press that starts a drag, U the release
// Each kind's columns are named in the header the file starts with.

#pragma once

#include <windows.h>
#include <string>

struct CLDocument;
class CircuitWindow;

namespace draglog {

// CEDARLOGIC_DRAG_OLD=1: the drawing as it was before the fix (each frame
// held for the display's refresh, painted only once the input queue is
// empty, the minimap with every move), so the two can be measured side by side.
bool legacy();

bool on();
void open(const std::string& path);
void close();
double now();   // seconds (QueryPerformanceCounter)

// What the canvas knows at a moment, for a log line.
struct View {
	HWND canvas;
	CLDocument* doc;
	int page;
	double originX, originY, upp;   // the camera (world = origin + view * upp, y up)
};

// The press that starts a drag of a part (after cl_edit_press).
void press(const View& v, double vx, double vy, double wx, double wy);
// A WM_MOUSEMOVE taken during a drag: its lParam (or none, -1, when the move
// came from elsewhere, as the side panel's drag), where it went in view
// points and the world, and how long the engine took with it.
void motion(const View& v, const char* kind, LPARAM lp, bool hasLp, double vx, double vy, double wx, double wy, double engineMs);
// A canvas paint while dragging: drawing, then EndDraw (the present).
void paint(const View& v, double drawMs, double presentMs);
void release(const View& v, double wx, double wy);
// Something that ended or disturbed a drag (a K line).
void note(const std::string& what);

// --drag-selftest: start once the window is up; it quits the app when done.
void startSelfTest();
bool selfTestRunning();

}  // namespace draglog
