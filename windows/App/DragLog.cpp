// Measuring a drag on the canvas (see DragLog.h and windows/DRAG-DIAGNOSIS.md).

#include "DragLog.h"
#include "App.h"
#include "Canvas.h"
#include "Window.h"

#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace draglog {
namespace {

FILE* gFile = nullptr;
double gStart = 0;

// The drag being measured.
struct Track {
	bool active = false;
	long gate = -1;              // the part followed (the one selected), -1 for none
	double offX = 0, offY = 0;   // part - pointer at the grab (world units)
	double lastMotion = 0, lastPaint = 0;
	int motionsSincePaint = 0;
	// For the summary line (and the self-test's verdict).
	int motions = 0, paints = 0;
	double maxErr = 0;           // worst |part - (pointer + grab offset)| at a move, world units
	std::vector<double> frameGaps, lagPx, drawMs, presentMs, engineMs;
};
Track gTrack;

double ms(double seconds) { return seconds * 1000.0; }

void line(const std::string& s) {
	if (gFile == nullptr) return;
	fputs(s.c_str(), gFile);
	fputc('\n', gFile);
}

void console(const std::string& text) {
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (out == nullptr || out == INVALID_HANDLE_VALUE) {
		if (AttachConsole(ATTACH_PARENT_PROCESS)) out = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	DWORD wrote = 0;
	if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), (DWORD)text.size(), &wrote, nullptr);
}

int refreshHz(HWND h) {
	HDC dc = GetDC(h);
	const int hz = dc ? GetDeviceCaps(dc, VREFRESH) : 0;
	if (dc) ReleaseDC(h, dc);
	return hz;
}

// The window's geometry: DPI, the canvas's client size and where it is on
// the screen, and the top-level window's rectangle (physical pixels).
std::string geometry(HWND canvas) {
	RECT cr = {}, wr = {};
	GetClientRect(canvas, &cr);
	POINT o = { 0, 0 };
	ClientToScreen(canvas, &o);
	GetWindowRect(GetAncestor(canvas, GA_ROOT), &wr);
	return strf("%u\t%ldx%ld\t%ld,%ld\t%ld,%ld,%ld,%ld", dpiOf(canvas), cr.right, cr.bottom, o.x, o.y, wr.left, wr.top, wr.right, wr.bottom);
}

bool partAt(const View& v, double& x, double& y) {
	return gTrack.gate >= 0 && cl_gate_position(v.doc, gTrack.gate, &x, &y);
}

double percentile(std::vector<double> v, double p) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	const size_t i = std::min(v.size() - 1, (size_t)std::lround(p * (v.size() - 1)));
	return v[i];
}

double mean(const std::vector<double>& v) {
	double s = 0;
	for (double x : v) s += x;
	return v.empty() ? 0 : s / v.size();
}

std::string summary() {
	const Track& t = gTrack;
	return strf("moves=%d paints=%d  frame gap ms p50=%.1f p95=%.1f max=%.1f  lag px p50=%.1f p95=%.1f max=%.1f  "
	            "draw ms avg=%.2f max=%.2f  present ms avg=%.2f max=%.2f  engine ms max=%.3f  worst move error=%.3f units",
	            t.motions, t.paints, percentile(t.frameGaps, 0.5), percentile(t.frameGaps, 0.95), percentile(t.frameGaps, 1),
	            percentile(t.lagPx, 0.5), percentile(t.lagPx, 0.95), percentile(t.lagPx, 1), mean(t.drawMs),
	            percentile(t.drawMs, 1), mean(t.presentMs), percentile(t.presentMs, 1), percentile(t.engineMs, 1), t.maxErr);
}

}  // namespace

bool legacy() {
	static const bool old = [] {
		const char* e = getenv("CEDARLOGIC_DRAG_OLD");
		return e && *e && *e != '0';
	}();
	return old;
}

bool on() { return gFile != nullptr; }

double now() { return nowSeconds(); }

void open(const std::string& path) {
	close();
	gFile = _wfopen(W(path).c_str(), L"w");
	if (gFile == nullptr) { console("drag log: couldn't open " + path + "\n"); return; }
	gStart = now();
	OSVERSIONINFOW os = {};
	os.dwOSVersionInfoSize = sizeof os;
	// (GetVersionEx lies without a manifest entry; RtlGetVersion doesn't.)
	if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
		typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
		if (auto f = (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion")) f(&os);
	}
	int speed = 0;
	SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
	int accel[3] = {};
	SystemParametersInfoW(SPI_GETMOUSE, 0, accel, 0);
	line(strf("# CedarLogic drag log  version %s  Windows %lu.%lu.%lu  drawing: %s  mouse speed %d, enhance pointer precision %s  swapped buttons %s",
	          CL_VERSION, os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber,
	          legacy() ? "OLD (CEDARLOGIC_DRAG_OLD: vsync present, paint when idle)" : "new (present at once, a frame per refresh while dragging)",
	          speed, accel[2] ? "on" : "off", GetSystemMetrics(SM_SWAPBUTTON) ? "yes" : "no"));
	line("# times are ms since the log opened (QueryPerformanceCounter); screen and client positions are physical pixels; view is points (1/96 in); world is circuit units, y up; 'err' is part - (pointer + grab offset), world units (0.5 is one snap step)");
	line("# D\tt\tvx\tvy\twx\twy\tgate\tpartX\tpartY\tdpi\tclient\tclientAtScreen\twindowRect");
	line("# M\tt\tkind\tlpX\tlpY\tmsgPosX\tmsgPosY\tcursorX\tcursorY\tvx\tvy\twx\twy\tpartX\tpartY\terrX\terrY\tengineMs\tsinceMoveMs\tsincePaintMs\toriginX\toriginY\tupp\tdpi\tclient\tclientAtScreen\twindowRect");
	line("# K\tt\twhat (a double-click that ended the drag, the pointer taken away mid-drag...)");
	line("# P\tt\tdrawMs\tpresentMs\tframeGapMs\tmovesSincePaint\tsinceMoveMs\tcursorX\tcursorY\tpartX\tpartY\tlagX_px\tlagY_px\tlag_px\tupp");
	line("# U\tt\twx\twy\tpartX\tpartY ; S = that drag's summary");
	fflush(gFile);
}

void close() {
	if (gFile) fclose(gFile);
	gFile = nullptr;
}

void press(const View& v, double vx, double vy, double wx, double wy) {
	// A gate that was on the pointer is put down by this press: its summary first.
	if (gTrack.active && on()) line("S\t" + summary());
	gTrack = Track();
	gTrack.active = true;
	gTrack.gate = cl_edit_single_gate(v.doc, v.page);
	double px = 0, py = 0;
	if (partAt(v, px, py)) { gTrack.offX = px - wx; gTrack.offY = py - wy; }
	gTrack.lastMotion = gTrack.lastPaint = now();
	if (!on()) return;
	line(strf("D\t%.3f\t%.2f\t%.2f\t%.4f\t%.4f\t%ld\t%.4f\t%.4f\t%s", ms(now() - gStart), vx, vy, wx, wy, gTrack.gate, px, py,
	          geometry(v.canvas).c_str()));
}

void motion(const View& v, const char* kind, LPARAM lp, bool hasLp, double vx, double vy, double wx, double wy, double engineMs) {
	const double t = now();
	// A gate on the pointer (from the side panel, or pasted) has no press:
	// it's followed from its first move.
	if (!gTrack.active) {
		gTrack = Track();
		gTrack.active = true;
		gTrack.gate = cl_edit_single_gate(v.doc, v.page);
		double px, py;
		if (partAt(v, px, py)) { gTrack.offX = px - wx; gTrack.offY = py - wy; }
		gTrack.lastMotion = gTrack.lastPaint = t;
	}
	double px = 0, py = 0, ex = 0, ey = 0;
	if (partAt(v, px, py)) {
		ex = px - (wx + gTrack.offX);
		ey = py - (wy + gTrack.offY);
		gTrack.maxErr = std::max(gTrack.maxErr, std::max(std::fabs(ex), std::fabs(ey)));
	}
	gTrack.motions++;
	gTrack.motionsSincePaint++;
	gTrack.engineMs.push_back(engineMs);
	const double sinceMove = ms(t - gTrack.lastMotion), sincePaint = ms(t - gTrack.lastPaint);
	gTrack.lastMotion = t;
	if (!on()) return;
	POINT cur;
	GetCursorPos(&cur);
	const DWORD mp = GetMessagePos();
	line(strf("M\t%.3f\t%s\t%d\t%d\t%d\t%d\t%ld\t%ld\t%.2f\t%.2f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.3f\t%.2f\t%.2f\t%.4f\t%.4f\t%.5f\t%s",
	          ms(t - gStart), kind, hasLp ? GET_X_LPARAM(lp) : -1, hasLp ? GET_Y_LPARAM(lp) : -1, GET_X_LPARAM(mp), GET_Y_LPARAM(mp), cur.x,
	          cur.y, vx, vy, wx, wy, px, py, ex, ey, engineMs, sinceMove, sincePaint, v.originX, v.originY, v.upp,
	          geometry(v.canvas).c_str()));
}

void note(const std::string& what) {
	if (!on()) return;
	line(strf("K\t%.3f\t", ms(now() - gStart)) + what);
	fflush(gFile);
}

void paint(const View& v, double drawMs, double presentMs) {
	if (!gTrack.active) return;
	const double t = now();
	const double gap = ms(t - gTrack.lastPaint);
	gTrack.lastPaint = t;
	gTrack.paints++;
	gTrack.frameGaps.push_back(gap);
	gTrack.drawMs.push_back(drawMs);
	gTrack.presentMs.push_back(presentMs);
	// Where the part is drawn against where the pointer is now: how far
	// behind (or off) what's on screen is.
	POINT cur;
	GetCursorPos(&cur);
	POINT c = cur;
	ScreenToClient(v.canvas, &c);
	const double s = dpiOf(v.canvas) / 96.0;
	const double cwx = v.originX + c.x / s * v.upp, cwy = v.originY - c.y / s * v.upp;
	double px = 0, py = 0, lx = 0, ly = 0, lag = -1;
	if (partAt(v, px, py)) {
		lx = (px - (cwx + gTrack.offX)) / v.upp * s;
		ly = -(py - (cwy + gTrack.offY)) / v.upp * s;
		lag = std::hypot(lx, ly);
		gTrack.lagPx.push_back(lag);
	}
	const int moves = gTrack.motionsSincePaint;
	gTrack.motionsSincePaint = 0;
	if (!on()) return;
	line(strf("P\t%.3f\t%.3f\t%.3f\t%.2f\t%d\t%.2f\t%ld\t%ld\t%.4f\t%.4f\t%.1f\t%.1f\t%.1f\t%.5f", ms(t - gStart), drawMs, presentMs, gap, moves,
	          ms(t - gTrack.lastMotion), cur.x, cur.y, px, py, lx, ly, lag, v.upp));
}

void release(const View& v, double wx, double wy) {
	if (!gTrack.active) return;
	double px = 0, py = 0;
	partAt(v, px, py);
	if (on()) {
		line(strf("U\t%.3f\t%.4f\t%.4f\t%.4f\t%.4f", ms(now() - gStart), wx, wy, px, py));
		line("S\t" + summary());
		fflush(gFile);
	}
	gTrack.active = false;
}

// ---- --drag-selftest -----------------------------------------------------------------
//
// A gate in the middle of a new circuit, pressed with the real (injected)
// left button and dragged round a circle twice, slowly then quickly, with a
// move every 2 ms as a 500 Hz mouse sends them, from a thread of its own so
// the window takes them as it would a hand's. Every move is checked: the
// gate must stay under the point it was grabbed by (within a grid unit),
// and after the drop it must be where the pointer left it.

namespace {

struct SelfTest {
	CircuitWindow* win = nullptr;
	HWND root = nullptr, canvas = nullptr;
	long gate = -1;
	double gateX = 0, gateY = 0;     // where it started
	POINT grab = {}, end = {};       // screen pixels
	POINT before = {};
	std::atomic<int> state{ 0 };     // 0 idle, 1 dragging, 2 done, 3 aborted
	std::thread input;
	int stage = 0;
};
SelfTest* gTest = nullptr;

void quit(int code) {
	if (gTest && gTest->input.joinable()) gTest->input.join();
	if (gTest) SetCursorPos(gTest->before.x, gTest->before.y);
	close();
	for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) w->destroy();
	PostQuitMessage(code);
}

void sendMove(POINT p) {
	const int vl = GetSystemMetrics(SM_XVIRTUALSCREEN), vt = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int vw = std::max(2, GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh = std::max(2, GetSystemMetrics(SM_CYVIRTUALSCREEN));
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dx = (LONG)std::lround((p.x - vl) * 65535.0 / (vw - 1));
	in.mi.dy = (LONG)std::lround((p.y - vt) * 65535.0 / (vh - 1));
	in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
	SendInput(1, &in, sizeof in);
}

void sendButton(bool downNow) {
	// The primary button: the right one when they're swapped.
	const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dwFlags = downNow ? (swapped ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN) : (swapped ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
	SendInput(1, &in, sizeof in);
}

// The input thread: press, the path, a rest, release. It stops (letting go
// of the button) if CedarLogic stops being the window in front.
void drive(std::vector<POINT> path, HWND root) {
	timeBeginPeriod(1);
	auto front = [&] { return GetForegroundWindow() == root; };
	bool ok = front();
	if (ok) {
		sendMove(path.front());
		Sleep(120);
		sendButton(true);
		Sleep(60);
		for (size_t i = 1; i < path.size() && (ok = front()); i++) {
			sendMove(path[i]);
			Sleep(2);
		}
		Sleep(250);   // still, so the last frames are drawn
		sendButton(false);
	}
	timeEndPeriod(1);
	gTest->state = ok ? 2 : 3;
}

void CALLBACK step(HWND, UINT, UINT_PTR id, DWORD);

void next(UINT afterMs) { SetTimer(nullptr, 0, afterMs, step); }

// The test's own window in front, even when it was started from a shell in
// the background: Windows' foreground lock refuses SetForegroundWindow to a
// process that hasn't had the last input, so the window would only flash on
// the taskbar and the test would skip. First by joining the front window's
// input for a moment (allowed to take the focus from it), then, if that
// wasn't enough, with a tap of Alt sent from here (the "last input" is then
// ours). The tap goes to the window in front before ours comes up, and the
// queue is let drain before ours does, so it never opens CedarLogic's menu bar.
bool bringToFront(HWND root) {
	if (GetForegroundWindow() == root) return true;
	if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
	HWND front = GetForegroundWindow();
	const DWORD mine = GetCurrentThreadId(), theirs = front ? GetWindowThreadProcessId(front, nullptr) : 0;
	const bool joined = theirs && theirs != mine && AttachThreadInput(mine, theirs, TRUE);
	SetForegroundWindow(root);
	BringWindowToTop(root);
	SetActiveWindow(root);
	if (joined) AttachThreadInput(mine, theirs, FALSE);
	if (GetForegroundWindow() == root) return true;
	INPUT alt[2] = {};
	alt[0].type = alt[1].type = INPUT_KEYBOARD;
	alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
	alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(2, alt, sizeof(INPUT));
	Sleep(60);
	SetForegroundWindow(root);
	BringWindowToTop(root);
	return GetForegroundWindow() == root;
}

bool setUp(std::string& why) {
	SelfTest& t = *gTest;
	t.win = circuitWindows().empty() ? nullptr : circuitWindows().front();
	Canvas* c = t.win ? t.win->currentCanvas() : nullptr;
	if (c == nullptr) { why = "no canvas"; return false; }
	t.root = t.win->window();
	t.canvas = c->widget();
	bringToFront(t.root);
	// 100%, the world's origin in the middle of the canvas.
	const double upp = 0.1;
	c->setCamera(-c->width() / 2 * upp, c->height() / 2 * upp, upp);
	CLDocument* doc = t.win->document();
	const int page = t.win->currentPage();
	if (!cl_edit_add_gate(doc, page, "AA_AND2", 0, 0)) { why = "couldn't add a gate"; return false; }
	t.gate = cl_edit_single_gate(doc, page);
	if (t.gate < 0 || !cl_gate_position(doc, t.gate, &t.gateX, &t.gateY)) { why = "the new gate isn't selected"; return false; }
	cl_edit_select_none(doc, page);
	t.win->redraw();
	UpdateWindow(t.canvas);
	// The gate's own point on the screen: inside its body, clear of its pins.
	double ox, oy;
	c->origin(ox, oy);
	const double s = dpiOf(t.canvas) / 96.0;
	t.grab = { (LONG)std::lround((t.gateX - ox) / upp * s), (LONG)std::lround((oy - t.gateY) / upp * s) };
	ClientToScreen(t.canvas, &t.grab);
	GetCursorPos(&t.before);
	if (GetForegroundWindow() != t.root) { why = "CedarLogic isn't the window in front (something else took the focus)"; return false; }
	if (WindowFromPoint(t.grab) != t.canvas) { why = "something covers the canvas where the gate is"; return false; }
	return true;
}

std::vector<POINT> makePath() {
	SelfTest& t = *gTest;
	RECT cr;
	GetClientRect(t.canvas, &cr);
	const double s = dpiOf(t.canvas) / 96.0;
	// A circle starting at the grab point and going right and round: twice
	// round, the first time in 2 s, the second in 0.5 s (2 ms a step).
	const double r = std::max(20.0, std::min({ 140.0 * s, cr.right / 5.0, cr.bottom / 3.0 }));
	const double cx = t.grab.x + r, cy = t.grab.y;
	std::vector<POINT> path;
	auto lap = [&](int steps) {
		for (int i = 0; i <= steps; i++) {
			const double a = 2 * 3.14159265358979 * i / steps;
			path.push_back({ (LONG)std::lround(cx - r * std::cos(a)), (LONG)std::lround(cy + r * std::sin(a)) });
		}
	};
	lap(1000);
	lap(250);
	// Then away to the right, so the drop is somewhere new.
	for (int i = 1; i <= 100; i++) path.push_back({ (LONG)std::lround(t.grab.x + r * i / 100.0), t.grab.y + (LONG)std::lround(r * 0.5 * i / 100.0) });
	return path;
}

void finish() {
	SelfTest& t = *gTest;
	CLDocument* doc = t.win->document();
	Canvas* c = t.win->currentCanvas();
	double gx = 0, gy = 0;
	const bool have = cl_gate_position(doc, t.gate, &gx, &gy);
	// Where the pointer let go, in the world; the gate should be its grab
	// offset (zero: it was grabbed by its own point) from there.
	POINT p;
	GetCursorPos(&p);
	double vx = 0, vy = 0, wx = 0, wy = 0;
	if (c) { c->screenToView(p, vx, vy); c->worldPoint(vx, vy, wx, wy); }
	const double dropErr = std::max(std::fabs(gx - wx), std::fabs(gy - wy));
	const bool moved = have && (std::fabs(gx - t.gateX) > 0.25 || std::fabs(gy - t.gateY) > 0.25);
	const bool tracked = gTrack.motions > 0;
	const bool pass = have && moved && tracked && dropErr <= 1.0 + 1e-6 && gTrack.maxErr <= 1.0 + 1e-6;
	const std::string stats = summary();
	std::string verdict = strf("%s  drag self-test: %d moves seen, gate moved %s, drop error %.3f units, worst error while moving %.3f units (tolerance 1)",
	                           pass ? "PASS" : "FAIL", gTrack.motions, moved ? "yes" : "NO", dropErr, gTrack.maxErr);
	if (!tracked) verdict += " -- the canvas never saw the drag (no moves with the button down)";
	line("S\t" + stats);
	line(verdict);
	console(verdict + "\n" + "      " + stats + "\n");
	// Smoothness, as a note: the frames (P lines) during the drag.
	const double p95 = percentile(gTrack.frameGaps, 0.95), worst = percentile(gTrack.frameGaps, 1);
	const std::string smooth = strf("%s  frames while dragging: %d over %d moves, gap p95 %.1f ms, worst %.1f ms, drawn %.1f px behind the pointer at p95",
	                                (gTrack.paints > 0 && p95 <= 34) ? "SMOOTH" : "CHOPPY", gTrack.paints, gTrack.motions, p95, worst,
	                                percentile(gTrack.lagPx, 0.95));
	line(smooth);
	console(smooth + "\n");
	quit(pass ? 0 : 1);
}

void CALLBACK step(HWND, UINT, UINT_PTR id, DWORD) {
	if (id) KillTimer(nullptr, id);
	guarded("the drag self-test", [&] {
		SelfTest& t = *gTest;
		if (t.stage == 0) {
			std::string why;
			if (!setUp(why)) {
				const std::string s = "SKIP  drag self-test: " + why;
				line(s);
				console(s + "\n");
				quit(2);
				return;
			}
			t.stage = 1;
			next(400);   // the gate drawn and settled
			return;
		}
		if (t.stage == 1) {
			// Checked again just before pressing: nothing must have come over it.
			if (GetForegroundWindow() != t.root || WindowFromPoint(t.grab) != t.canvas) {
				const std::string s = "SKIP  drag self-test: the canvas was covered or lost the focus before the press";
				line(s);
				console(s + "\n");
				quit(2);
				return;
			}
			t.stage = 2;
			t.state = 1;
			t.input = std::thread(drive, makePath(), t.root);
			next(100);
			return;
		}
		if (t.stage == 2) {
			if (t.state == 1) { next(100); return; }
			t.input.join();
			if (t.state == 3) {
				const std::string s = "FAIL  drag self-test: CedarLogic stopped being the window in front mid-drag (input stopped, button let go)";
				line(s);
				console(s + "\n");
				quit(1);
				return;
			}
			t.stage = 3;
			next(400);   // the drop handled
			return;
		}
		finish();
	});
}

}  // namespace

void startSelfTest() {
	if (gTest) return;
	gTest = new SelfTest();
	next(1500);   // the window up, its opening card gone
}

bool selfTestRunning() { return gTest != nullptr; }

}  // namespace draglog
