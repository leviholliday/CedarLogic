// --rail-check (see RailCheck.h).

#include "RailCheck.h"
#include "Actions.h"
#include "Canvas.h"
#include "Commands.h"
#include "Rail.h"
#include "Shortcuts.h"
#include "Toolbar.h"
#include "Window.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace railcheck {

namespace {

std::string gHome;
int gFailed = 0, gPassed = 0;

void removeTree(const std::string& dir) {
	std::wstring from = W(dir);
	from.push_back(L'\0');
	SHFILEOPSTRUCTW op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_NO_UI;
	SHFileOperationW(&op);
}
std::function<void(const std::string&)> gOut;

void check(bool ok, const std::string& what) {
	(ok ? gPassed : gFailed)++;
	gOut(std::string(ok ? "PASS " : "FAIL ") + what + "\n");
}

std::string join(const std::vector<std::string>& v) {
	std::string s;
	for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
	return "[" + s + "]";
}

// The messages for a while (the window's clock runs the rail's slide).
void pump(double seconds) {
	const double until = nowSeconds() + seconds;
	while (nowSeconds() < until) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		Sleep(5);
	}
}

std::string writeIni(const std::string& name, const std::string& text) {
	const std::string path = gHome + "\\" + name;
	std::ofstream o(W(path).c_str(), std::ios::binary);
	o << text;
	return path;
}

// Settings read from `text`, as a launch reads them.
void loadText(const std::string& name, const std::string& text) {
	const std::string marker = "normal";
	prefs() = Prefs();
	prefs().loadFrom(writeIni(name, text), &marker);
}

std::vector<std::string> ids(const std::string& query) {
	std::vector<std::string> out;
	for (const actions::Def& d : actions::all()) if (actions::matches(d, query)) out.push_back(d.id);
	return out;
}
bool has(const std::vector<std::string>& v, const std::string& id) { return std::find(v.begin(), v.end(), id) != v.end(); }

void defaultsAndOrder() {
	const Prefs before = prefs();
	// A new install: no settings at all.
	loadText("new.ini", "");
	check(prefs().actionsMode == 0, strf("a new install has the rail on screen (mode %d)", prefs().actionsMode));
	check(!prefs().actionsCollapsed && actions::rail() == actions::defaults(), "open, with the six default actions " + join(actions::rail()));
	check(!prefs().actionsHint, "and no status line about it");
	check(prefs().actionsHoverOpen, "folded, it slides out at the edge (the default)");
	// Someone who had CedarLogic before (settings from before the rail).
	loadText("old.ini", "[CedarLogic]\nhasSeenWelcome=true\ntoolbarStyle=0\n");
	check(prefs().actionsMode == 2, strf("an earlier install keeps its window: no rail (mode %d)", prefs().actionsMode));
	check(prefs().actionsHint, "and the status line tells them once where it is");
	// What was chosen stays, through a save and a launch.
	std::vector<std::string> reversed = actions::defaults();
	std::reverse(reversed.begin(), reversed.end());
	std::string order;
	for (const std::string& id : reversed) order += (order.empty() ? "" : ",") + id;
	loadText("kept.ini", "[CedarLogic]\nactionsMode=0\nactionsCollapsed=true\nactionsHoverOpen=false\nactionsHint=false\nactionsOrder=" + order +
	                         "\nactionsShown=checkCircuit,predict,tidy,shareLink,exportImage,exportReport\n");
	check(prefs().actionsMode == 0 && prefs().actionsCollapsed, "a folded rail stays folded, as it was left");
	check(actions::rail() == reversed, "in the order it was given " + join(actions::rail()));
	check(!prefs().actionsHoverOpen && !prefs().actionsHint, "hover-to-open stays off once turned off; no status line (seen)");
	loadText("again.ini", prefs().text());
	check(prefs().actionsCollapsed && !prefs().actionsHoverOpen && actions::rail() == reversed, "and the same after it's saved and read again");

	// The rail's drag and the Customize list: one order.
	loadText("order.ini", "");
	const std::vector<std::string> b = actions::rail();
	actions::moveRail(b[0], 2);
	const std::vector<std::string> after = actions::rail();
	check(after.size() == b.size() && after[2] == b[0] && after[0] == b[1], "a drag on the rail moves it " + join(after));
	std::vector<std::string> ticked;
	for (const std::string& id : actions::listed()) if (actions::shown(id)) ticked.push_back(id);
	check(ticked == after, "and the Customize list has that order too");
	actions::moveRail(b[0], 0);
	check(actions::rail() == b, "and back to the top again");
	actions::moveRail(b[5], 99);
	check(actions::rail() == b, "a drop past the end leaves the last one last");
	const std::vector<std::string> list = actions::listed();
	const int tidyAt = (int)(std::find(list.begin(), list.end(), "tidy") - list.begin());
	actions::moveListed(tidyAt, 0);
	check(actions::rail().front() == "tidy", "a drag in the Customize list moves it on the rail " + join(actions::rail()));
	actions::setShown("zoomIn", true);
	check(has(actions::rail(), "zoomIn") && actions::rail().size() == 7, "ticking one adds it to the rail");
	actions::setShown("zoomIn", false);
	actions::reset();
	check(actions::isDefault() && actions::rail() == actions::defaults(), "Reset to Default gives back the six, in order");
	prefs() = before;
}

void search() {
	check(has(ids("check"), "checkCircuit"), "search: by name (check)");
	check(has(ids("circuit"), "addGate") && has(ids("circuit"), "tidy"), "search: by menu (circuit)");
	check(has(ids("shift t"), "checkCircuit"), "search: by shortcut in words (shift t)");
	check(has(ids("ctrl e"), "exportImage") && has(ids("control e"), "exportImage") && has(ids("Ctrl+E"), "exportImage"),
	      "search: ctrl e, control e and Ctrl+E find Export as Image");
	check(ids("export lab") == std::vector<std::string>{ "exportReport" }, "search: every word must match (export lab) " + join(ids("export lab")));
	check(ids("zzzz").empty() && ids("").size() == actions::all().size(), "nothing for nonsense, everything for an empty search");
}

void geometry() {
	const RECT c = { 0, 0, 1000, 700 };
	auto in = [&](int x, int y, double s) { return rail::inHotStrip(c, 80, 680, POINT{ x, y }, s); };
	check(in(999, 400, 1) && in(1000, 400, 1), "the hot strip has the window's last column of pixels (and the one past it)");
	check(in(988, 400, 1) && !in(987, 400, 1), "it's 12 points wide");
	check(in(995, 80, 1) && in(995, 679, 1) && !in(995, 79, 1) && !in(995, 680, 1), "and the canvas's height, no more");
	check(in(982, 300, 1.5) && !in(981, 300, 1.5), "at 150%: 18 pixels");
}

void hover() {
	rail::Hover h;
	h.update(0, true, false, true, true);
	check(h.lit() && !h.slidOut, "into the strip: the tab peeks at once, the rail not out yet");
	h.update(0.04, false, false, true, true);
	h.update(0.2, false, false, true, true);
	check(!h.slidOut, "passing through (40 ms) doesn't slide it out");
	h.update(1.0, true, false, true, true);
	h.update(1.05, true, false, true, true);
	const bool early = h.slidOut;
	h.update(1.11, true, false, true, true);
	check(!early && h.slidOut, "100 ms in the strip slides it out");
	h.update(1.5, false, true, true, true);
	h.update(2.0, false, false, true, true);
	h.update(2.2, false, false, true, true);
	const bool stays = h.slidOut;
	h.update(2.41, false, false, true, true);
	check(stays && !h.slidOut, "out over the rail it stays; it goes 400 ms after the pointer leaves");
	h.update(3.0, true, false, true, true);
	h.update(3.11, true, false, true, true);
	h.menuOpen = true;
	h.update(3.2, false, false, true, true);
	h.update(3.9, false, false, true, true);
	const bool menuKept = h.slidOut;
	h.menuOpen = false;
	h.changed(3.9, true, true);
	h.update(4.31, false, false, true, true);
	check(menuKept && !h.slidOut, "its menu open keeps it out; closed, it goes");
	h.update(5.0, true, false, true, true);
	h.update(5.11, true, false, true, true);
	h.dragging = true;
	h.update(5.2, false, false, true, true);
	h.update(6.0, false, false, true, true);
	const bool dragKept = h.slidOut;
	h.dragging = false;
	h.changed(6.0, true, true);
	h.update(6.41, false, false, true, true);
	check(dragKept && !h.slidOut, "a drag keeps it out; done, it goes");
	h.update(7.0, true, false, true, true);
	h.update(7.11, true, false, true, true);
	h.dismissed = true;
	h.slide(false);
	h.update(7.5, true, false, true, true);
	const bool stayedIn = !h.slidOut;
	h.update(7.6, false, false, true, true);
	h.update(7.7, true, false, true, true);
	h.update(7.81, true, false, true, true);
	check(stayedIn && h.slidOut, "Escape puts it back; out again only once the pointer has left and come back");
	rail::Hover off;
	off.update(0, true, false, true, false);
	off.update(1, true, false, true, false);
	check(!off.slidOut, "with the setting off, the edge does nothing");
	rail::Hover open;
	open.update(0, true, false, false, true);
	open.update(1, true, false, false, true);
	check(!open.slidOut, "an open rail doesn't slide out");
}

void window() {
	const Prefs before = prefs();
	CircuitWindow* w = newCircuitWindow();
	if (!w) { check(false, "a circuit window"); return; }
	ActionsRail* r = w->railWidget();
	w->present();
	SetWindowPos(w->window(), nullptr, 20, 20, 1000, 700, SWP_NOZORDER | SWP_NOACTIVATE);
	prefs().actionsMode = 0;
	prefs().actionsCollapsed = false;
	prefs().actionsHoverOpen = true;
	actions::reset();
	actions::changed();
	pump(0.4);
	const double s = dpiOf(w->window()) / 96.0;
	auto railW = [&] { RECT x; GetWindowRect(r->widget(), &x); return (int)(x.right - x.left); };
	auto clientW = [&] { RECT x; GetClientRect(w->window(), &x); return (int)x.right; };
	auto about = [&](int a, double b) { return std::abs(a - (int)std::lround(b)) <= 1; };
	const int open = (int)std::lround(rail::kWidth * s), tab = (int)std::lround(rail::kTabWidth * s);
	check(IsWindowVisible(r->widget()) && about(railW(), open) && about(clientW() - w->canvasRight(), open),
	      strf("open, the rail takes %d px at the right, the canvas the rest (%d)", railW(), clientW() - w->canvasRight()));
	r->fold();
	pump(0.3);
	check(prefs().actionsCollapsed && about(railW(), tab) && about(clientW() - w->canvasRight(), tab), strf("folded, a %d px tab", railW()));
	r->unfold();
	pump(0.3);
	check(!prefs().actionsCollapsed && about(railW(), open), "and open again");

	// Focus mode folds it to its tab (not hidden) and gives it back as it was.
	w->toggleFocusMode();
	pump(0.4);
	check(IsWindowVisible(r->widget()) && about(railW(), tab) && !prefs().actionsCollapsed, "focus mode folds an open rail to its tab, not away");
	r->unfold();
	pump(0.3);
	check(about(railW(), open) && !prefs().actionsCollapsed, "in focus mode its tab opens it");
	w->toggleFocusMode();
	pump(0.4);
	check(about(railW(), open) && !prefs().actionsCollapsed, "out of focus mode it's open, as it was");
	r->fold();
	w->toggleFocusMode();
	pump(0.3);
	r->unfold();
	pump(0.3);
	const bool openInFocus = about(railW(), open) && prefs().actionsCollapsed;
	w->toggleFocusMode();
	pump(0.4);
	check(openInFocus && about(railW(), tab) && prefs().actionsCollapsed, "a folded rail opened in focus mode is folded again after it");
	r->unfold();
	pump(0.2);

	// A narrow window folds it; wide again, it's back.
	SetWindowPos(w->window(), nullptr, 0, 0, (int)std::lround(820 * s), 640, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	pump(0.3);
	check(about(railW(), tab) && !prefs().actionsCollapsed, strf("a window under 900 points folds it by itself (%d px wide)", clientW()));
	SetWindowPos(w->window(), nullptr, 0, 0, 1000, 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	pump(0.3);
	check(about(railW(), open), "and wider again, it's back");

	// Folded, the pointer at the edge slides it out over the canvas.
	r->fold();
	pump(0.3);
	const int canvasFolded = w->canvasRight();
	const double t0 = nowSeconds();
	r->pointerForTest(t0, true, false);
	pump(0.05);
	const int peekW = railW();
	r->pointerForTest(t0 + 0.11, true, false);
	pump(0.45);
	check(about(peekW, rail::kPeekWidth * s), strf("the tab peeks (%d px) as the pointer reaches the edge", peekW));
	check(r->slidOut() && about(railW(), open) && w->canvasRight() == canvasFolded,
	      strf("then slides out over the canvas (%d px), the canvas the same width", railW()));
	r->pointerForTest(t0 + 1.0, false, false);
	r->pointerForTest(t0 + 1.2, false, false);
	const bool stillOut = r->slidOut();
	r->pointerForTest(t0 + 1.45, false, false);
	pump(0.45);
	check(stillOut && !r->slidOut() && about(railW(), tab), "and back 400 ms after the pointer leaves");
	const double t1 = nowSeconds();
	r->pointerForTest(t1, true, false);
	r->pointerForTest(t1 + 0.11, true, false);
	const bool out = r->slidOut();
	check(out && w->railEscape() && !r->slidOut(), "Escape puts a slid-out rail back");
	r->pointerForTest(t1 + 0.5, false, false);
	pump(0.3);

	// View > Show/Hide Actions.
	check(!r->showing(), "folded: View menu says Show Actions");
	w->run(CMD_TOGGLE_ACTIONS);
	pump(0.3);
	check(r->showing() && !prefs().actionsCollapsed && about(railW(), open), "Show Actions opens it");
	w->run(CMD_TOGGLE_ACTIONS);
	pump(0.3);
	check(!r->showing() && prefs().actionsCollapsed, "and Hide Actions folds it again");
	prefs().actionsMode = 2;
	actions::changed();
	pump(0.2);
	check(!IsWindowVisible(r->widget()) && w->canvasRight() == clientW(), "Hidden: no rail, the canvas to the edge");
	check(!w->toolbarWidget()->hasButton(CMD_CHECK_CIRCUIT), "and no actions in the toolbar");
	w->run(CMD_TOGGLE_ACTIONS);
	pump(0.3);
	check(prefs().actionsMode == 0 && r->showing() && about(railW(), open), "with the actions hidden, Show Actions puts the rail on screen, open");
	prefs().actionsMode = 1;
	actions::changed();
	pump(0.2);
	POINT bp;
	check(!IsWindowVisible(r->widget()) && w->canvasRight() == clientW() && w->toolbarWidget()->hasButton(CMD_CHECK_CIRCUIT) &&
	          w->toolbarWidget()->buttonPoint(CMD_CHECK_CIRCUIT, bp),
	      "In the toolbar: no rail, the actions as toolbar buttons (the first one shown at 1000 px)");
	// Minimal has room for all six at this width.
	const int style = prefs().toolbarStyle;
	prefs().toolbarStyle = TSMinimal;
	actions::changed();
	pump(0.2);
	int shownSix = 0;
	for (const std::string& id : actions::defaults())
		if (w->toolbarWidget()->buttonPoint(actions::find(id)->command, bp)) shownSix++;
	check(shownSix == 6, strf("and all six where there's room (%d shown, Minimal)", shownSix));
	prefs().toolbarStyle = style;
	prefs().actionsMode = 0;
	actions::changed();
	pump(0.2);

	// A click on the rail runs it; a drag moves it (and runs nothing).
	prefs().actionsOrder = "gridlines,wireDots,statusBar";
	prefs().actionsShown = "gridlines,wireDots,statusBar";
	prefs().actionsShownSet = true;
	actions::changed();
	pump(0.2);
	const bool grid = prefs().showGrid;
	r->pressForTest(0, 0);
	pump(0.1);
	check(prefs().showGrid != grid, "a click on Show Grid on the rail runs it");
	r->pressForTest(0, 0);
	pump(0.1);
	r->pressForTest(0, 40);
	pump(0.1);
	check(actions::rail() == std::vector<std::string>{ "wireDots", "gridlines", "statusBar" } && prefs().showGrid == grid,
	      "dragged 40 points down it moves one place, and doesn't run " + join(actions::rail()));
	r->pressForTest(1, 5);
	pump(0.1);
	check(actions::rail()[1] == "gridlines" && prefs().showGrid != grid, "moved under 6 points it's still a click, not a drag");
	r->pressForTest(1, 0);
	pump(0.1);
	r->pressForTest(0, 0, 0.5);
	pump(0.1);
	check(prefs().showGrid == grid && actions::rail()[0] == "wireDots", "held 0.45 s and let go without moving: picked up, nothing runs");
	r->pressForTest(1, 2, 0.5);
	pump(0.1);
	check(actions::rail()[1] == "gridlines" && prefs().showGrid == grid, "held, 2 points is a drag (dropped in place), not a click");
	r->pressForTest(1, -36, 0.5);
	pump(0.1);
	check(actions::rail()[0] == "gridlines", "held and moved up a place, it goes up " + join(actions::rail()));
	actions::reset();
	actions::changed();

	// The shortcut.
	const shortcuts::Action* a = shortcuts::find("toggleActions");
	const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
	const shortcuts::Action* m = shortcuts::match(k, false);
	int sharing = 0;
	for (const shortcuts::Action& x : shortcuts::all())
		for (const shortcuts::Key& y : shortcuts::keys(x)) if (y == k) sharing++;
	check(k.valid() && shortcuts::label(k) == "Ctrl+B" && m && m->command == CMD_TOGGLE_ACTIONS && sharing == 1,
	      "View > Show/Hide Actions is Ctrl+B, and nothing else has it (" + (k.valid() ? shortcuts::label(k) : std::string("none")) + ")");
	// Predict from the rail: Simulation View, the lights covered.
	const std::string practice = resourcesDir() + "\\samples\\practice.cdl";
	if (openCircuit(practice, nullptr) && !circuitWindows().empty()) {
		CircuitWindow* p = circuitWindows().back();
		pump(0.3);
		const bool can = p->commandEnabled(CMD_PREDICT);
		p->run(CMD_PREDICT);
		pump(0.3);
		check(can && p->simView() && p->predictCovers(), "Predict from the rail goes into Simulation View with the lights covered");
	} else {
		check(false, "the practice circuit opens (" + practice + ")");
	}
	prefs() = before;
}

}  // namespace

void prepare() {
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	gHome = U(tmp) + strf("cedarlogic-rail-%lu", (unsigned long)GetCurrentProcessId());
	CreateDirectoryW(W(gHome).c_str(), nullptr);
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(gHome).c_str());
	ActionsRail::watchPointer = false;
	ActionsRail::quiet = true;
}

int run(const std::function<void(const std::string&)>& out) {
	gOut = out;
	guarded("the rail check", [] {
		defaultsAndOrder();
		search();
		geometry();
		hover();
		window();
	});
	gOut(strf("rail check: %d passed, %d failed%s\n", gPassed, gFailed, gFailed == 0 ? " -- all passed" : ""));
	removeTree(gHome);
	return gFailed == 0 && gPassed > 0 ? 0 : 1;
}

}  // namespace railcheck
