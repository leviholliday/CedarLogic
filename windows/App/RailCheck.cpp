// --rail-check (see RailCheck.h).

#include "RailCheck.h"
#include "Actions.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Commands.h"
#include "Intro.h"
#include "Rail.h"
#include "Dialogs.h"
#include "Shortcuts.h"
#include "ShowMeAround.h"
#include "Toolbar.h"
#include "Welcome.h"
#include "Window.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <climits>
#include <cwchar>
#include <fstream>
#include <functional>
#include <map>

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

bool hasLine(const std::string& text, const std::string& key) { return text.find("\n" + key + "=") != std::string::npos; }

void defaultsAndOrder() {
	const Prefs before = prefs();
	// A new install: no settings at all.
	loadText("new.ini", "");
	check(prefs().actionsOn, "a new install has the panel on");
	check(!prefs().actionsCollapsed && actions::rail() == actions::defaults(), "open, with the six default actions " + join(actions::rail()));
	check(prefs().actionsHoverOpen, "folded, it slides out at the edge (Open on hover, the default)");
	check(!prefs().showMeAroundSeen, "and Show Me Around hasn't been seen");
	// 0.4's one-time migrations, from every earlier setting: on, and open.
	struct Old { const char* name; const char* ini; };
	const Old olds[] = {
		{ "a build from before the panel", "[CedarLogic]\nhasSeenWelcome=true\ntoolbarStyle=0\n" },
		{ "the panel Hidden (and the status line pending)", "[CedarLogic]\nhasSeenWelcome=true\nactionsMode=2\nactionsHint=true\n" },
		{ "In the toolbar", "[CedarLogic]\nhasSeenWelcome=true\nactionsMode=1\nactionsHint=false\n" },
		{ "On screen, folded", "[CedarLogic]\nhasSeenWelcome=true\nactionsMode=0\nactionsCollapsed=true\n" },
		{ "Hidden and folded", "[CedarLogic]\nhasSeenWelcome=true\nactionsMode=2\nactionsCollapsed=true\nactionsHoverOpen=false\n" },
	};
	for (const Old& o : olds) {
		loadText("old.ini", o.ini);
		const std::string saved = prefs().text();
		check(prefs().actionsOn && !prefs().actionsCollapsed, std::string("migration: ") + o.name + " -> on and open");
		check(!hasLine(saved, "actionsMode") && !hasLine(saved, "actionsHint") && hasLine(saved, "actionsOn04") && hasLine(saved, "actionsOpen04"),
		      std::string("migration: ") + o.name + " -> saved without the old mode or status line, both steps marked done");
	}
	loadText("old-hover.ini", "[CedarLogic]\nactionsMode=2\nactionsHoverOpen=false\n");
	check(!prefs().actionsHoverOpen, "migration: Open on hover stays as it was chosen");
	// After the migration the choice sticks: off stays off, folded stays folded.
	loadText("migrated.ini", "[CedarLogic]\nhasSeenWelcome=true\nactionsMode=1\n");
	prefs().actionsOn = false;
	prefs().actionsCollapsed = true;
	loadText("after.ini", prefs().text());
	check(!prefs().actionsOn && prefs().actionsCollapsed, "after the migration: turned off and folded, it stays so at the next launch");
	prefs().actionsOn = true;
	loadText("after2.ini", prefs().text());
	check(prefs().actionsOn && prefs().actionsCollapsed, "and turned on again, it's on and still folded (never unfolded again)");
	// What was chosen stays, through a save and a launch.
	std::vector<std::string> reversed = actions::defaults();
	std::reverse(reversed.begin(), reversed.end());
	std::string order;
	for (const std::string& id : reversed) order += (order.empty() ? "" : ",") + id;
	loadText("kept.ini", "[CedarLogic]\nactionsOn04=1\nactionsOpen04=1\nactionsOn=true\nactionsCollapsed=true\nactionsHoverOpen=false\nactionsOrder=" + order +
	                         "\nactionsShown=checkCircuit,predict,tidy,shareLink,exportImage,exportReport\n");
	check(prefs().actionsOn && prefs().actionsCollapsed, "a folded panel stays folded, as it was left");
	check(actions::rail() == reversed, "in the order it was given " + join(actions::rail()));
	check(!prefs().actionsHoverOpen, "Open on hover stays off once turned off");
	loadText("again.ini", prefs().text());
	check(prefs().actionsCollapsed && !prefs().actionsHoverOpen && actions::rail() == reversed, "and the same after it's saved and read again");
	loadText("seen.ini", "[CedarLogic]\nshowMeAroundSeen=true\n");
	check(prefs().showMeAroundSeen, "Show Me Around seen stays seen");

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
	check(ids("ctrl e") == std::vector<std::string>{ "exportImage" }, "search: a key alone is that key (ctrl e) " + join(ids("ctrl e")));
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
	prefs().actionsOn = true;
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
	prefs().actionsOn = false;
	actions::changed();
	pump(0.2);
	check(!IsWindowVisible(r->widget()) && w->canvasRight() == clientW(), "Show the Actions panel off: no panel, the canvas to the edge");
	// No toolbar mode: none of the actions are toolbar buttons, in any style.
	const int style = prefs().toolbarStyle;
	int inToolbar = 0;
	for (int st : { TSSeamless, TSClassic, TSMinimal }) {
		prefs().toolbarStyle = st;
		actions::changed();
		pump(0.1);
		for (const std::string& id : actions::defaults())
			if (w->toolbarWidget()->hasButton(actions::find(id)->command)) inToolbar++;
	}
	prefs().toolbarStyle = style;
	actions::changed();
	check(inToolbar == 0, strf("no toolbar mode: with the panel off, none of its actions are in the toolbar, in any style (%d)", inToolbar));
	w->run(CMD_TOGGLE_ACTIONS);
	pump(0.3);
	check(prefs().actionsOn && r->showing() && about(railW(), open), "with the panel off, Show Actions turns it on, open");
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

// The intro's rules (Intro.h), the Windows twin of the Mac launch-selftest's
// intro checks: the timelines' lengths (the full intro and the short one),
// which launches play which intro and with the sound or not, "When
// CedarLogic opens" (Short intro for everyone to begin with, settled once
// from the old checkbox, each choice sticking), and Help > Play the Intro and
// Learn Logic Online in the actions.
void introRules() {
	const Prefs before = prefs();
	check(std::fabs(intro::kTotal - 6.07) < 0.005 && std::fabs(intro::kDissolveAt - 5.52) < 0.005,
	      strf("intro: 6.07 s in all, the panel held to %.2f s (%.2f s)", intro::kDissolveAt, intro::kTotal));
	check(std::lround(intro::kTotal * 60) + 1 == 365, "intro: --render-intro draws 365 frames");
	check(std::fabs(intro::kShortTotal - 3.55) < 0.005 && std::fabs(intro::kShortDissolveAt - 3.15) < 0.005 &&
	          std::fabs(intro::lengthFor(intro::Trigger::Short) - 3.55) < 0.005 && std::fabs(intro::lengthFor(intro::Trigger::Every) - 6.07) < 0.005,
	      strf("intro: the short intro is %.2f s, its bloom crossfading into the window from %.2f s (the LED and \"0.4\" at %.2f, the beat to %.2f)",
	           intro::kShortTotal, intro::kShortDissolveAt, intro::kLedAt, intro::kBloomAt));
	check(std::lround(intro::kShortTotal * 60) + 1 == 214, "intro: --render-intro short draws 214 frames (0 to 213)");

	// "When CedarLogic opens", and the one-time settling from the old checkbox.
	const char* names[] = { "Short intro", "Full intro", "Classic launch screen", "None" };
	bool titles = true;
	for (int c = intro::AtShort; c <= intro::AtNone; c++) titles = titles && std::string(intro::atLaunchTitle(c)) == names[c];
	check(titles, "intro: \"When CedarLogic opens\" offers Short intro, Full intro, Classic launch screen and None");
	loadText("intro-new.ini", "");
	check(prefs().introAtLaunch == intro::AtShort, "intro: a new install opens with the short intro (When CedarLogic opens: Short intro)");
	struct Old { const char* name; const char* ini; int want; };
	const Old olds[] = {
		{ "0.3 (no checkbox yet)", "[CedarLogic]\nhasSeenWelcome=true\n", intro::AtShort },
		{ "the checkbox never touched (off to begin with)", "[CedarLogic]\nhasSeenWelcome=true\nintroEveryLaunch=false\n", intro::AtShort },
		{ "the checkbox turned on (when it began off)", "[CedarLogic]\nhasSeenWelcome=true\nintroEveryLaunch=true\n", intro::AtFull },
		{ "the checkbox left on (when it began on)", "[CedarLogic]\nhasSeenWelcome=true\nintroEveryLaunch=true\nintroEveryLaunchSet=1\n", intro::AtShort },
		{ "the checkbox turned off (when it began on)", "[CedarLogic]\nhasSeenWelcome=true\nintroEveryLaunch=false\nintroEveryLaunchSet=1\n", intro::AtClassic },
	};
	for (const Old& o : olds) {
		loadText("intro-old.ini", o.ini);
		const int got = prefs().introAtLaunch;
		const std::string saved = prefs().text();
		loadText("intro-old-again.ini", saved);
		check(got == o.want && prefs().introAtLaunch == o.want && hasLine(saved, "introAtLaunch") && !hasLine(saved, "introEveryLaunch") &&
		          !hasLine(saved, "introEveryLaunchSet"),
		      strf("intro: settled once from %s: %s, and so after a save and a launch", o.name, intro::atLaunchTitle(o.want)));
	}
	bool sticks = true;
	for (int c = intro::AtShort; c <= intro::AtNone; c++) {
		loadText("intro-pick.ini", "[CedarLogic]\nintroEveryLaunch=true\n");
		prefs().introAtLaunch = c;
		loadText("intro-picked.ini", prefs().text());
		sticks = sticks && prefs().introAtLaunch == c;
	}
	check(sticks, "intro: each of the four choices sticks through a save and a launch");
	check(intro::hasSound(intro::Trigger::First) && intro::hasSound(intro::Trigger::Update) && !intro::hasSound(intro::Trigger::Every) &&
	          !intro::hasSound(intro::Trigger::Short),
	      "intro: the sound is for the first launch and updates only");

	// Which launch plays which, for each choice.
	Prefs first;
	bool firstAlways = true, updateAlways = true, noneOwed = true;
	for (int c = intro::AtShort; c <= intro::AtNone; c++) {
		first.introAtLaunch = c;
		firstAlways = firstAlways && intro::forLaunch(first) == intro::Trigger::First && intro::hasSound(intro::forLaunch(first));
		noneOwed = noneOwed && !intro::straightInFor(first);
	}
	check(firstAlways, "intro: a first launch plays the full intro, with the sound, whatever the choice");
	Prefs later;
	later.hasSeenWelcome = later.firstLaunchPlayed = later.introFirstLaunchPlayed = true;
	later.seenWhatsNew = whatsnew::kVersion;
	check(intro::forLaunch(later) == intro::Trigger::Short && !intro::hasSound(intro::forLaunch(later)) && !intro::straightInFor(later),
	      "intro: by default a later launch plays the short intro, silently");
	later.introAtLaunch = intro::AtFull;
	check(intro::forLaunch(later) == intro::Trigger::Every && !intro::hasSound(intro::forLaunch(later)) && !intro::straightInFor(later),
	      "intro: Full intro: a later launch plays the full intro, with no sound");
	later.introAtLaunch = intro::AtClassic;
	check(intro::forLaunch(later) == intro::Trigger::None && !intro::straightInFor(later),
	      "intro: Classic launch screen: a later launch plays no intro, just the launch screen");
	later.introAtLaunch = intro::AtNone;
	check(intro::forLaunch(later) == intro::Trigger::None && intro::straightInFor(later),
	      "intro: None: a later launch has no launch screen at all, straight into the window");
	Prefs updated = later;
	updated.seenWhatsNew = "an older version";
	for (int c = intro::AtShort; c <= intro::AtNone; c++) {
		updated.introAtLaunch = c;
		updateAlways = updateAlways && intro::forLaunch(updated) == intro::Trigger::Update && intro::hasSound(intro::forLaunch(updated));
		noneOwed = noneOwed && !intro::straightInFor(updated);
	}
	check(updateAlways, "intro: the first launch after an update plays the update's intro, with the sound, whatever the choice");
	check(noneOwed, "intro: None never skips an intro that's owed (the first launch, an update)");
	updated.introUpdatePlayed = whatsnew::kVersion;
	updated.introAtLaunch = intro::AtShort;
	check(intro::forLaunch(updated) == intro::Trigger::Short, "intro: once played, the update's is the short intro again");
	check(!intro::straightIn(), "intro: never straight in for a test run (--rail-check)");
	check(intro::atLaunchHint(intro::AtShort).find("three and a half seconds") != std::string::npos &&
	          intro::atLaunchHint(intro::AtNone).find("no launch screen") != std::string::npos,
	      "intro: Settings says what each choice does under it");
	const actions::Def* play = actions::find("playIntro");
	check(play && play->command == CMD_PLAY_INTRO && std::string(play->menu) == "Help" && std::string(play->title) == "Play the Intro",
	      "intro: Play the Intro is among the actions, in Help");
	const actions::Def* learn = actions::find("learnOnline");
	check(learn && learn->command == CMD_LEARN_ONLINE && std::string(learn->menu) == "Help" && std::string(learn->title) == "Learn Logic Online\u2026" &&
	          has(ids("learn"), "learnOnline") && has(ids("online"), "learnOnline"),
	      "learn online: Learn Logic Online\u2026 is among the actions, in Help (so it can go on the panel), and found by its name");
	check(std::string(kLearnOnlineUrl) == "https://cedarlogic.netlify.app/learn/", std::string("learn online: it opens ") + kLearnOnlineUrl);
	prefs() = before;
}

bool menuHas(HMENU m, UINT command) {
	for (int i = 0; m && i < GetMenuItemCount(m); i++) {
		if (GetMenuItemID(m, i) == command) return true;
		if (HMENU sub = GetSubMenu(m, i)) if (menuHas(sub, command)) return true;
	}
	return false;
}

void introReplay() {
	const Prefs before = prefs();
	CircuitWindow* w = newCircuitWindow();
	if (!w) { check(false, "intro: a circuit window"); return; }
	w->present();
	SetWindowPos(w->window(), nullptr, 20, 20, 1000, 700, SWP_NOZORDER | SWP_NOACTIVATE);
	pump(0.3);
	check(menuHas(w->menuBarMenus(), CMD_PLAY_INTRO), "intro: Help > Play the Intro is in the menu bar");
	prefs().introFirstLaunchPlayed = false;
	prefs().introUpdatePlayed = "";
	const bool could = w->commandEnabled(CMD_PLAY_INTRO);
	const double soundBefore = intro::lastSoundFrom();
	w->run(CMD_PLAY_INTRO);
	pump(0.4);
	check(could && splash::active() && !w->commandEnabled(CMD_PLAY_INTRO),
	      strf("intro: Play the Intro plays (the launch screen up, the intro %s), and can't again while it does", intro::active() ? "up" : "skipped: animations off"));
	check(intro::lastSoundFrom() >= 0 && intro::lastSoundFrom() != soundBefore, strf("intro: with the sound (%.2f s into it)", intro::lastSoundFrom()));
	const double until = nowSeconds() + intro::kTotal + 1.5;
	while ((splash::active() || intro::active()) && nowSeconds() < until) pump(0.1);
	check(!splash::active() && !intro::active() && IsWindowVisible(w->window()) && w->commandEnabled(CMD_PLAY_INTRO),
	      "intro: then gone, back into the window, and can be played again");
	check(!prefs().introFirstLaunchPlayed && prefs().introUpdatePlayed.empty(), "intro: the once-only keys are left as they were");

	// The short intro, played over the window the same way: silent, no
	// panel, about 3.55 s (with Windows' animations off: the still, briefly).
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	const double expect = animations ? intro::kShortTotal : intro::kCalmHold + intro::kCalmFade;
	const double soundShort = intro::lastSoundFrom();
	const double began = nowSeconds();
	splash::playIntroNow(w, intro::Trigger::Short);
	pump(0.2);
	const bool upShort = splash::active();
	double gone = -1;
	while (nowSeconds() < began + expect + 2.0) {
		if (!splash::active() && !intro::active()) { gone = nowSeconds() - began; break; }
		pump(0.02);
	}
	check(upShort && gone > 0 && gone >= expect - 0.15 && gone <= expect + 0.6 && intro::lastSoundFrom() == soundShort,
	      strf("intro: the short intro plays (%s), silent, and is gone into the window after %.2f s (%.2f s expected)",
	           animations ? "the drawing, no panel" : "animations off: the still", gone, expect));

	// Help > Learn Logic Online…, next to Learn Logic: Guided Tour.
	HMENU helpMenu = nullptr;
	for (int i = 0; i < GetMenuItemCount(w->menuBarMenus()); i++)
		if (HMENU sub = GetSubMenu(w->menuBarMenus(), i)) if (menuHas(sub, CMD_TOUR)) helpMenu = sub;
	int tourAt = -1, learnAt = -1;
	for (int i = 0; helpMenu && i < GetMenuItemCount(helpMenu); i++) {
		if (GetMenuItemID(helpMenu, i) == CMD_TOUR) tourAt = i;
		if (GetMenuItemID(helpMenu, i) == CMD_LEARN_ONLINE) learnAt = i;
	}
	wchar_t label[128] = L"";
	if (helpMenu && learnAt >= 0) GetMenuStringW(helpMenu, (UINT)learnAt, label, 127, MF_BYPOSITION);
	check(learnAt >= 0 && learnAt == tourAt + 1 && U(label) == "Learn Logic &Online\u2026" && w->commandEnabled(CMD_LEARN_ONLINE),
	      "learn online: Help > Learn Logic Online\u2026 is in the menu bar, right under Learn Logic: Guided Tour (" + U(label) + ")");
	prefs() = before;
}


// Show Me Around (ShowMeAround.h), the Windows twin of the Mac
// launch-selftest's: its stops in a real window, the keys, the panel's stop,
// Escape, starting by itself after each of the welcome's ways out and after
// What's New (never over them), and never again once seen.
MSG keyMsg(CircuitWindow* w, UINT vk) {
	MSG m = {};
	m.hwnd = w->window();
	m.message = WM_KEYDOWN;
	m.wParam = vk;
	return m;
}

// Until it's on (or `seconds` pass).
bool waitActive(double seconds) {
	const double until = nowSeconds() + seconds;
	while (!showme::active() && nowSeconds() < until) pump(0.05);
	pump(0.1);
	return showme::active();
}

std::string menuText(HMENU m, UINT command) {
	for (int i = 0; m && i < GetMenuItemCount(m); i++) {
		if (GetMenuItemID(m, i) == command) {
			wchar_t buf[128] = L"";
			GetMenuStringW(m, (UINT)i, buf, 127, MF_BYPOSITION);
			return U(buf);
		}
		if (HMENU sub = GetSubMenu(m, i)) {
			const std::string t = menuText(sub, command);
			if (!t.empty()) return t;
		}
	}
	return "";
}

// The hole is round the place (give or take the window's edge, where it's
// kept 2 points in).
bool holeRound(CircuitWindow* w, int spot) {
	RECT hole = showme::holeOnScreen(), place = w->tourSpot(spot);
	if (IsRectEmpty(&place) || IsRectEmpty(&hole)) return false;
	const int slack = (int)std::lround(4 * dpiOf(w->window()) / 96.0);
	InflateRect(&hole, slack, slack);
	RECT both;
	UnionRect(&both, &hole, &place);
	return EqualRect(&both, &hole) != FALSE;
}

void showMeAround() {
	const Prefs before = prefs();
	const char* P = "show me around: ";
	CircuitWindow* w = newCircuitWindow();
	if (!w) { check(false, std::string(P) + "a circuit window"); return; }
	w->present();
	SetWindowPos(w->window(), nullptr, 20, 20, 1100, 760, SWP_NOZORDER | SWP_NOACTIVATE);
	prefs().actionsOn = true;
	prefs().actionsCollapsed = false;
	actions::reset();
	actions::changed();
	pump(0.4);
	const std::string smaLabel = menuText(w->menuBarMenus(), CMD_SHOW_ME_AROUND), tourLabel = menuText(w->menuBarMenus(), CMD_TOUR);
	check(smaLabel.find("Show Me &Around") == 0 && tourLabel.find("Learn Logic: Guided &Tour") == 0,
	      std::string(P) + "Help > Show Me Around, and the lesson is Learn Logic: Guided Tour (" + smaLabel + ", " + tourLabel + ")");
	const actions::Def* sd = actions::find("showMeAround");
	const actions::Def* td = actions::find("tour");
	check(sd && sd->command == CMD_SHOW_ME_AROUND && td && std::string(td->title) == "Learn Logic: Guided Tour",
	      std::string(P) + "both are among the actions, by those names");

	// On a launch before it's been seen: by itself, once the window is up.
	showme::quiet = false;
	showme::assumeFront = true;
	prefs().showMeAroundSeen = false;
	showme::startWhenReady();
	const bool started = waitActive(4);
	check(started && showme::target() == w && showme::overlayShown(), std::string(P) + "starts by itself on a launch before it's been seen");
	check(showme::shown().size() == 7 && showme::step() == 0 && showme::position() == 1,
	      strf("%sseven stops, on the side panel first (%zu, stop %d)", P, showme::shown().size(), showme::step() + 1));
	check(holeRound(w, showme::Palette), std::string(P) + "its hole is round the side panel");
	bool allPlaces = true;
	std::string missing;
	for (int i = 0; i < showme::SpotCount; i++) {
		const RECT r = w->tourSpot(i);
		if (IsRectEmpty(&r)) { allPlaces = false; missing += showme::stepTitle(i) + "; "; }
	}
	check(allPlaces, std::string(P) + "it finds every place in the window " + missing);
	check(showme::stepBody(6).find("menus at the top of the window") != std::string::npos && showme::stepBody(6).find("File (Ctrl+,)") != std::string::npos,
	      std::string(P) + "the last stop points at the menus in the window, Settings under File (Ctrl+,)");
	// The keys.
	MSG m = keyMsg(w, VK_RIGHT);
	const bool took = showme::key(m);
	pump(0.35);
	check(took && showme::step() == 1, strf("%sRight steps on (stop %d)", P, showme::step() + 1));
	m = keyMsg(w, VK_LEFT);
	showme::key(m);
	pump(0.35);
	check(showme::step() == 0, std::string(P) + "Left steps back");
	m = keyMsg(w, VK_RETURN);
	showme::key(m);
	pump(0.35);
	m = keyMsg(w, VK_SPACE);
	showme::key(m);
	pump(0.35);
	check(showme::step() == 2, strf("%sEnter and Space step on (stop %d)", P, showme::step() + 1));
	m = keyMsg(w, 'A');
	check(showme::key(m) && showme::active(), std::string(P) + "a plain key doesn't reach the canvas meanwhile");
	// The panel's stop opens a folded panel, and it folds back after.
	ActionsRail* r = w->railWidget();
	prefs().actionsCollapsed = true;
	actions::changed();
	pump(0.3);
	auto railW = [&] { RECT x; GetWindowRect(r->widget(), &x); return (int)(x.right - x.left); };
	const double s = dpiOf(w->window()) / 96.0;
	const int open = (int)std::lround(rail::kWidth * s), tab = (int)std::lround(rail::kTabWidth * s);
	const bool foldedBefore = std::abs(railW() - tab) <= 1;
	m = keyMsg(w, VK_RIGHT);
	showme::key(m);
	pump(0.45);
	check(foldedBefore && showme::stepSpot(showme::step()) == showme::Actions && std::abs(railW() - open) <= 1 && prefs().actionsCollapsed,
	      strf("%sat the panel's stop a folded panel opens (%d px), still folded in Settings", P, railW()));
	check(showme::stepBody(showme::step()).find("folded") != std::string::npos, std::string(P) + "and its sentence says it's kept folded");
	check(holeRound(w, showme::Actions), std::string(P) + "its hole is round the open panel");
	showme::key(m);
	pump(0.45);
	check(showme::step() == 4 && std::abs(railW() - tab) <= 1, strf("%sand at the next stop it folds back (%d px)", P, railW()));
	prefs().actionsCollapsed = false;
	actions::changed();
	// Escape quits, and counts as seen.
	m = keyMsg(w, VK_ESCAPE);
	showme::key(m);
	pump(0.4);
	check(!showme::active() && !showme::overlayShown() && prefs().showMeAroundSeen, std::string(P) + "Escape stops it, and it counts as seen");
	showme::startWhenReady();
	pump(1.6);
	check(!showme::active() && !showme::watching(), std::string(P) + "once seen it never starts by itself again");
	// The panel off: its stop is passed over.
	prefs().actionsOn = false;
	actions::changed();
	pump(0.2);
	showme::start(w, 0);
	pump(0.2);
	const std::vector<int> six = showme::shown();
	std::vector<int> visited;
	std::string notRound;
	for (int i = 0; i < 6 && showme::active(); i++) {
		visited.push_back(showme::step());
		if (!holeRound(w, showme::stepSpot(showme::step()))) notRound += showme::stepTitle(showme::step()) + "; ";
		showme::next();
		pump(0.3);
	}
	check(six.size() == 6 && std::find(six.begin(), six.end(), 3) == six.end() && visited == six && !showme::active(),
	      strf("%swith the panel off its stop is passed over (%zu stops), and Done at the last ends it", P, six.size()));
	check(notRound.empty(), std::string(P) + "at every stop the hole is round its place " + notRound);
	prefs().actionsOn = true;
	actions::changed();
	pump(0.2);

	// After the first-run welcome, whichever way it closes; the lesson after it if chosen.
	struct Exit { showme::WelcomeExit e; const char* name; };
	const Exit exits[] = { { showme::WelcomeExit::GuidedTour, "Learn Logic" }, { showme::WelcomeExit::Template, "a template" },
	                       { showme::WelcomeExit::Blank, "Just Start / a blank canvas" }, { showme::WelcomeExit::Shortcuts, "the shortcuts" },
	                       { showme::WelcomeExit::Skip, "Skip / Escape" } };
	for (const Exit& x : exits) {
		prefs().showMeAroundSeen = false;
		prefs().hasSeenWelcome = false;
		welcome::show(w);
		showme::startWhenReady();
		pump(1.4);
		const bool waited = !showme::active() && welcome::isOpen();
		welcome::finishForTest(x.e);
		const bool on = waitActive(4);
		check(waited && on && showme::target() == w,
		      std::string(P) + "never over the welcome; after it closes by " + x.name + " it starts");
		if (x.e == showme::WelcomeExit::GuidedTour) {
			check(showme::hasThen(), std::string(P) + "the lesson waits for it");
			showme::end();
			pump(0.6);
			check(welcome::tourWindow() != nullptr && prefs().showMeAroundSeen, std::string(P) + "and starts right after it");
			welcome::stopTour();
		} else {
			check(!showme::hasThen(), std::string(P) + "nothing after it (" + std::string(x.name) + ")");
			showme::end();
			pump(0.3);
		}
	}
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows()))
		if (c != w) c->destroy();
	pump(0.2);
	// After What's New (an update's first launch), never over it.
	prefs().showMeAroundSeen = false;
	prefs().hasSeenWelcome = true;
	prefs().seenWhatsNew = "an older version";
	whatsnew::show(w);
	showme::startWhenReady();
	pump(1.4);
	const bool waitedNews = !showme::active() && whatsnew::isOpen();
	whatsnew::closeForTest();
	check(waitedNews && waitActive(4), std::string(P) + "never over What's New; after it closes it starts");
	// A dialog over the window hides it.
	EnableWindow(w->window(), FALSE);
	pump(0.2);
	const bool hid = !showme::overlayShown();
	EnableWindow(w->window(), TRUE);
	pump(0.2);
	check(hid && showme::overlayShown(), std::string(P) + "under a dialog it's out of sight, and back after");
	// The last stop's "Next, learn logic" link: the lesson after it.
	showme::stop();
	showme::preview(w, 6);
	check(showme::isLast() && !showme::hasThen(), std::string(P) + "the last stop (Help and Settings)");
	showme::end();
	pump(0.3);
	showme::quiet = true;
	showme::assumeFront = false;
	w->destroy();
	pump(0.2);
	prefs() = before;
}

// About's address, and the wheel over a choice.
void aboutAndWheel() {
	CircuitWindow* w = newCircuitWindow();
	if (!w) { check(false, "about: a circuit window"); return; }
	w->present();
	pump(0.2);
	about::show(w);
	pump(0.4);
	check(about::isOpen() && about::mailLinkDrawn() && about::mailLink() == "mailto:cedarlogic@proton.me",
	      "about: \"Questions? cedarlogic@proton.me\", a mailto link (" + about::mailLink() + ")");
	about::closeNow();
	pump(0.2);
	std::string detail;
	const bool wheelOk = wheelOverChoiceCheck(detail);
	check(wheelOk, "settings: the wheel over a closed choice leaves its value and scrolls the page (" + detail + ")");
	w->destroy();
	pump(0.2);
}


// ---- The canvas's right edge: the hot strip only watches --------------------------

// The real pointer (SendInput), at `at` (screen pixels), with a button's
// flags or none (a move).
void pointerTo(POINT at, DWORD flags = 0) {
	const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
	const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1), vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
	SetCursorPos(at.x, at.y);
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dx = (LONG)std::lround((at.x - vx) * 65535.0 / vw);
	in.mi.dy = (LONG)std::lround((at.y - vy) * 65535.0 / vh);
	in.mi.dwFlags = flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
	SendInput(1, &in, sizeof in);
}

// The Linux bug, checked on Windows: there the panel's 12-point hot strip
// (folded, Open on hover) took the mouse's presses, so a drag couldn't start
// at the canvas's right edge. Here the strip is no window (the clock watches
// the pointer), the peeking tab passes presses over the canvas through, and
// the rail doesn't come out under a drag the canvas has. A real press in the
// strip's 12 points (over the canvas), held past the slide-out's 100 ms, and
// a drag left: the canvas draws its selection box and selects the gate it
// went round, and the rail never came out. Folded and open, in focus mode
// and not; and the wheel there.
void edgeDrags() {
	const Prefs before = prefs();
	const char* P = "right edge: ";
	CircuitWindow* w = newCircuitWindow();
	if (!w) { check(false, std::string(P) + "a circuit window"); return; }
	ActionsRail* r = w->railWidget();
	w->present();
	MONITORINFO mi = { sizeof mi };
	GetMonitorInfoW(MonitorFromWindow(w->window(), MONITOR_DEFAULTTOPRIMARY), &mi);
	const RECT& work = mi.rcWork;
	const int ww = std::min<int>(1000, work.right - work.left - 16), wh = std::min<int>(700, work.bottom - work.top - 16);
	SetWindowPos(w->window(), HWND_TOPMOST, work.left + 8, work.top + 8, ww, wh, SWP_SHOWWINDOW);
	prefs().actionsOn = true;
	prefs().actionsHoverOpen = true;
	prefs().actionsCollapsed = false;
	actions::reset();
	actions::changed();
	pump(1.2);   // (the opening card gone)
	const double s = dpiOf(w->window()) / 96.0;
	const POINT parked = { work.left + 4, work.bottom - 4 };
	// In front, with the keys: a click on an empty part of the canvas.
	{
		RECT cr;
		GetWindowRect(w->currentCanvas()->widget(), &cr);
		const POINT mid = { (cr.left + cr.right) / 2, (cr.top + cr.bottom) / 2 };
		pointerTo(mid);
		pump(0.05);
		pointerTo(mid, MOUSEEVENTF_LEFTDOWN);
		pump(0.05);
		pointerTo(mid, MOUSEEVENTF_LEFTUP);
		pump(0.2);
	}
	ActionsRail::watchPointer = true;
	const bool front = GetForegroundWindow() == w->window();
	auto toScreen = [&](int x, int y) { POINT p = { x, y }; ClientToScreen(w->window(), &p); return p; };

	struct Case { bool focus, folded; const char* name; };
	const Case cases[] = { { false, true, "folded" }, { false, false, "open" }, { true, true, "folded, in focus mode" },
	                       { true, false, "open, in focus mode" } };
	for (const Case& k : cases) {
		if (w->focusMode() != k.focus) w->toggleFocusMode();
		prefs().actionsCollapsed = k.folded;
		actions::changed();
		pump(0.4);
		// In focus mode it folds by itself: its tab opens it.
		if (k.focus && !k.folded) { r->unfold(); pump(0.4); }
		if (k.focus && k.folded && !r->folded()) { r->fold(); pump(0.3); }
		RECT client;
		GetClientRect(w->window(), &client);
		const int canvasRight = w->canvasRight();
		RECT cr;
		GetWindowRect(w->currentCanvas()->widget(), &cr);
		const int midY = (cr.top + cr.bottom) / 2 - (int)(60 * s);
		// Inside the strip's 12 points and on the canvas: folded, under the
		// peeking tab and just left of it; open, the canvas's last 12 points.
		std::vector<std::pair<int, const char*>> starts;
		if (k.folded) {
			starts.push_back({ canvasRight - (int)std::lround(2 * s), "under the peeking tab" });
			starts.push_back({ (int)client.right - (int)std::lround(11 * s), "11 points in" });
		} else {
			starts.push_back({ canvasRight - (int)std::lround(3 * s), "3 points in" });
			starts.push_back({ canvasRight - (int)std::lround(10 * s), "10 points in" });
		}
		for (const auto& st : starts) {
			CLDocument* doc = w->document();
			const int pg = w->currentPage();
			Canvas* c = w->currentCanvas();
			const POINT start = toScreen(st.first, 0);
			const POINT from = { start.x, midY };
			// (Open: within 12 points of the canvas's edge, the rail beside it.)
			const bool inStrip = k.folded ? rail::inHotStrip(client, 0, client.bottom, POINT{ st.first, client.bottom / 2 }, s)
			                              : canvasRight - st.first <= (int)std::lround(rail::kHotWidth * s);
			// A gate to the left, inside where the box will go.
			POINT v = from;
			ScreenToClient(c->widget(), &v);
			double gx = 0, gy = 0;
			c->worldPoint(v.x / s - 110, v.y / s + 45, gx, gy);
			cl_edit_select_none(doc, pg);
			cl_edit_add_gate(doc, pg, "AA_AND2", gx, gy);
			cl_edit_select_none(doc, pg);
			c->redraw();
			pump(0.1);
			pointerTo(from);
			pump(0.05);   // (the tab lights at once; the rail would come out at 100 ms)
			const bool peeked = !k.folded || r->hover().lit();
			const bool under = WindowFromPoint(from) == c->widget();
			pointerTo(from, MOUSEEVENTF_LEFTDOWN);
			pump(0.25);   // past the slide-out's 100 ms
			const bool held = GetCapture() == c->widget() && c->isDragging();
			bool slid = r->slidOut();
			const POINT to = { from.x - (int)std::lround(200 * s), from.y + (int)std::lround(90 * s) };
			bool box = false;
			for (int i = 1; i <= 10; i++) {
				pointerTo(POINT{ from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10 });
				pump(0.03);
				double l, b, rr, t;
				box = box || cl_edit_box(doc, &l, &b, &rr, &t);
				slid = slid || r->slidOut();
			}
			pointerTo(to, MOUSEEVENTF_LEFTUP);
			pump(0.15);
			const int selected = cl_edit_selected_gate_count(doc, pg);
			check(front && inStrip && peeked && under && held && box && selected == 1 && !slid,
			      strf("%s%s, a drag started %s (in the 12-point strip) is the canvas's: it took the press%s, drew its box and selected the gate "
			           "(%d); the panel stayed in (front %d, strip %d, peek %d, canvas under %d, held %d, box %d, slid %d)",
			           P, k.name, st.second, k.folded ? " under the tab" : "", selected, front, inStrip, peeked, under, held, box, slid));
			cl_edit_select_all(doc, pg);
			cl_edit_delete(doc, pg);
			pointerTo(parked);
			pump(0.6);
		}
		// The wheel there, folded: the canvas's.
		if (k.folded && !k.focus) {
			Canvas* c = w->currentCanvas();
			const POINT at = toScreen(canvasRight - (int)std::lround(2 * s), 0);
			const POINT p = { at.x, midY };
			pointerTo(p);
			pump(0.08);
			double ox0, oy0, u0, ox1, oy1, u1;
			c->camera(ox0, oy0, u0);
			INPUT in = {};
			in.type = INPUT_MOUSE;
			in.mi.dwFlags = MOUSEEVENTF_WHEEL;
			in.mi.mouseData = (DWORD)(-WHEEL_DELTA);
			SendInput(1, &in, sizeof in);
			pump(0.4);
			c->camera(ox1, oy1, u1);
			check(ox0 != ox1 || oy0 != oy1 || u0 != u1, std::string(P) + "folded, the wheel in the strip moves the canvas, not the panel");
			pointerTo(parked);
			pump(0.6);
		}
	}
	if (w->focusMode()) w->toggleFocusMode();
	ActionsRail::watchPointer = false;
	SetWindowPos(w->window(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	w->destroy();
	pump(0.2);
	prefs() = before;
}

// ---- Text on the accent --------------------------------------------------------------

// The Linux bug, checked on Windows: there, highlighted menu items and
// Settings' selected rows drew white on the green accent (unreadable). Here
// the menus are Windows' own (none owner-drawn: Windows draws their
// highlight), and every owner-drawn state with text on the accent, or in
// it, must read at 4.5:1 (WCAG AA), in both themes and with every accent;
// on CedarLogic's green the text is dark ink.
void accentContrast() {
	const Prefs before = prefs();
	const char* P = "contrast: ";
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	int ownerDrawn = 0, items = 0;
	std::function<void(HMENU)> walk = [&](HMENU m) {
		for (int i = 0; m && i < GetMenuItemCount(m); i++) {
			MENUITEMINFOW mii = { sizeof mii };
			mii.fMask = MIIM_FTYPE | MIIM_SUBMENU;
			if (!GetMenuItemInfoW(m, (UINT)i, TRUE, &mii)) continue;
			items++;
			if (mii.fType & MFT_OWNERDRAW) ownerDrawn++;
			if (mii.hSubMenu) walk(mii.hSubMenu);
		}
	};
	if (w) walk(w->menuBarMenus());
	check(w && items > 50 && ownerDrawn == 0,
	      strf("%sthe menus (%d items) are Windows' own, none owner-drawn: Windows draws their highlight and its text", P, items));

	struct Worst { double ratio = 99; std::string where; };
	std::map<std::string, Worst> worst;
	const char* accentNames[] = { "blue", "purple", "pink", "orange", "green", "graphite", "CedarLogic green" };
	auto note = [&](const std::string& what, D2D1_COLOR_F fg, D2D1_COLOR_F bg, const std::string& where) {
		const double r = Chrome::contrast(fg, Chrome::over(bg, D2D1::ColorF(0, 0, 0)));
		Worst& x = worst[what];
		if (r < x.ratio) { x.ratio = r; x.where = where; }
	};
	for (int dark = 0; dark < 2; dark++) {
		for (int a = 0; a < 7; a++) {
			prefs().dark = dark != 0;
			prefs().accent = a;
			const Chrome ch = chrome();
			const D2D1_COLOR_F accent = ch.accent();
			const std::string where = std::string(accentNames[a]) + (dark ? ", dark" : ", light");
			// The dialogs' paper and fields (Dialogs.cpp), Settings' page bar.
			const D2D1_COLOR_F paper = dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252);
			const D2D1_COLOR_F field = dark ? rgb255(41, 45, 53) : rgb255(239, 240, 243);
			const D2D1_COLOR_F ink = dark ? rgb255(226, 230, 238) : rgb255(30, 33, 40);
			const D2D1_COLOR_F barBack = dark ? D2D1::ColorF(0.141f, 0.153f, 0.180f) : D2D1::ColorF(0.941f, 0.945f, 0.957f);
			// Filled with the accent, its text on it: Settings' and every dialog's
			// default button (still, hovered, pressed), a picker's, the canvas
			// bar's first button, the truth table's Done, the segments.
			for (float alpha : { 1.0f, 0.92f, 0.8f }) {
				const D2D1_COLOR_F fill = Chrome::over(withAlpha(accent, alpha), paper);
				note("a default button's text (Settings, dialogs, pickers): still, hovered, pressed", Chrome::inkOn(fill), fill, where);
			}
			for (float alpha : { 1.0f, 0.9f }) {
				const D2D1_COLOR_F fill = Chrome::over(withAlpha(accent, alpha), dark ? rgb255(40, 43, 50) : D2D1::ColorF(1, 1, 1));
				note("the canvas bar's first button (Keep), still and hovered", Chrome::inkOn(fill), fill, where);
			}
			const D2D1_COLOR_F ttPaper = dark ? D2D1::ColorF(0.075f, 0.085f, 0.1f) : D2D1::ColorF(0.965f, 0.97f, 0.975f);
			for (float alpha : { 1.0f, 0.92f }) {
				const D2D1_COLOR_F fill = Chrome::over(withAlpha(accent, alpha), ttPaper);
				note("the truth table's Done", Chrome::inkOn(fill), fill, where);
			}
			note("a segment chosen (Hex / Decimal, 1s / 0s)", ch.onAccent(), accent, where);
			// In the accent, on a tint of it: Settings' page bar (the page
			// showing), the shortcut being recorded, the section headings.
			note("Settings' page bar: the page showing, in the accent on its tint",
			     ch.accentText(Chrome::over(withAlpha(accent, dark ? 0.22f : 0.15f), barBack)),
			     Chrome::over(withAlpha(accent, dark ? 0.22f : 0.15f), barBack), where);
			note("Settings > Shortcuts: \"Press keys…\" on the row being recorded",
			     ch.accentText(Chrome::over(withAlpha(accent, dark ? 0.24f : 0.14f), field)),
			     Chrome::over(withAlpha(accent, dark ? 0.24f : 0.14f), field), where);
			note("Settings > Shortcuts: section headings", ch.accentText(field), field, where);
			note("the Keyboard Shortcuts sheet: section headings", ch.accentText(paper), paper, where);
			note("a picker's badge, and Send Feedback's tags chosen", ch.accentText(Chrome::over(withAlpha(accent, 0.18f), paper)),
			     Chrome::over(withAlpha(accent, 0.18f), paper), where);
			// The ink on a selected or hovered row's tint: Settings > Actions
			// and Customize (the keyboard's row), Settings > Shortcuts, the
			// Keyboard Shortcuts sheet, Help's list, Add a Gate.
			for (float t : { 0.09f, 0.14f, 0.16f, 0.24f, 0.26f, 0.28f }) {
				const D2D1_COLOR_F row = Chrome::over(withAlpha(accent, t), field);
				note("the ink on a selected row (Settings, Customize, Help, Add a Gate)", ink, row, where);
				const D2D1_COLOR_F row2 = Chrome::over(withAlpha(accent, t), paper);
				note("the ink on a selected row (Settings, Customize, Help, Add a Gate)", ink, row2, where);
			}
			// Not text (a tick in a box, a toggle's knob): 3:1.
			note("~a tick on the accent (Customize, Settings > Actions), a toggle's knob", ch.onAccent(), accent, where);
		}
	}
	for (const auto& kv : worst) {
		const bool symbol = kv.first[0] == '~';
		const std::string what = symbol ? kv.first.substr(1) : kv.first;
		check(kv.second.ratio >= (symbol ? 3.0 : 4.5),
		      strf("%s%s: at least %.1f:1 in both themes, every accent (the lowest %.2f:1, %s)", P, what.c_str(), symbol ? 3.0 : 4.5,
		           kv.second.ratio, kv.second.where.c_str()));
	}
	// On CedarLogic's green (the default), the text is dark ink.
	for (int dark = 0; dark < 2; dark++) {
		prefs().dark = dark != 0;
		prefs().accent = 6;
		const Chrome ch = chrome();
		const D2D1_COLOR_F on = ch.onAccent(), dk = Chrome::darkInk();
		const bool isDark = on.r == dk.r && on.g == dk.g && on.b == dk.b;
		const D2D1_COLOR_F paper = dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252);
		const D2D1_COLOR_F hovered = Chrome::over(withAlpha(ch.accent(), 0.92f), paper), pressed = Chrome::over(withAlpha(ch.accent(), 0.8f), paper);
		const D2D1_COLOR_F onHot = Chrome::inkOn(hovered), onDown = Chrome::inkOn(pressed);
		check(isDark && onHot.r == dk.r && onDown.r == dk.r,
		      strf("%son CedarLogic's green (%s) the text is dark ink: %.2f:1 (white was %.2f:1), hovered %.2f:1, pressed %.2f:1", P,
		           dark ? "dark" : "light", Chrome::contrast(on, ch.accent()), Chrome::contrast(D2D1::ColorF(1, 1, 1), ch.accent()),
		           Chrome::contrast(onHot, hovered), Chrome::contrast(onDown, pressed)));
	}
	prefs() = before;
}

// ---- Settings on a small screen -------------------------------------------------------

struct SettingsSeen {
	bool open = false, onScreen = true;
	RECT window{}, work{};
	UINT dpi = 96;
	int width = 0, pages = 0;
	std::vector<int> contentH;
	std::string off;
};
SettingsSeen gSeen;

void CALLBACK settingsLook(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	Form* f = openPagedForm();
	if (f == nullptr || f->dialog == nullptr) return;
	gSeen.open = true;
	MONITORINFO mi = { sizeof mi };
	GetMonitorInfoW(MonitorFromWindow(f->dialog, MONITOR_DEFAULTTONEAREST), &mi);
	gSeen.work = mi.rcWork;
	gSeen.dpi = dpiOf(f->dialog);
	gSeen.width = f->width;
	gSeen.pages = (int)f->pages.size();
	for (size_t p = 0; p < f->pages.size(); p++) {
		f->showPage((int)p);
		RECT r;
		GetWindowRect(f->dialog, &r);
		if (p == 0) gSeen.window = r;
		if (r.left < mi.rcWork.left || r.top < mi.rcWork.top || r.right > mi.rcWork.right || r.bottom > mi.rcWork.bottom) {
			gSeen.onScreen = false;
			gSeen.off += f->pages[p] + "; ";
		}
		if (p < f->pageBottoms.size()) gSeen.contentH.push_back(f->pageBottoms[p] + f->footerGap + f->footerButtonsAt + f->footerBelow);
	}
	f->showPage(0);
	PostMessageW(f->dialog, WM_COMMAND, IDCANCEL, 0);
}

BOOL CALLBACK endDialogs(HWND h, LPARAM) {
	wchar_t cls[16] = L"";
	GetClassNameW(h, cls, 15);
	if (wcscmp(cls, L"#32770") == 0 && IsWindowVisible(h)) EndDialog(h, IDCANCEL);
	return TRUE;
}

void CALLBACK settingsWatchdog(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	// (Whatever's open, closed: the check never waits on a dialog.)
	EnumThreadWindows(GetCurrentThreadId(), endDialogs, 0);
}

// Settings at the screens a laptop or a classroom PC has: 1024 x 600 and
// 1280 x 720, at Windows' 125% and 150% (and this one, as it is). Each page
// starts on the screen and stays on it: no taller than the room above the
// taskbar (the page scrolls), never wider, its top left never off it. The
// small screens are worked out with the code that places it (formFit, as
// Settings uses it) from what Settings' pages hold here, since CI can't
// change Windows' scaling.
void settingsFit() {
	const char* P = "settings: ";
	CircuitWindow* w = circuitWindows().empty() ? newCircuitWindow() : circuitWindows().front();
	if (!w) { check(false, std::string(P) + "a circuit window"); return; }
	w->present();
	pump(0.2);
	gSeen = SettingsSeen();
	SetTimer(nullptr, 0, 700, settingsLook);
	const UINT_PTR watchdog = SetTimer(nullptr, 0, 8000, settingsWatchdog);
	showPreferencesDialog(w->window());
	KillTimer(nullptr, watchdog);
	pump(0.2);
	check(gSeen.open && gSeen.onScreen && gSeen.pages >= 4 && (int)gSeen.contentH.size() == gSeen.pages,
	      strf("%son this screen (%ld x %ld at %u%%) every one of its %d pages is on the screen %s", P, gSeen.work.right - gSeen.work.left,
	           gSeen.work.bottom - gSeen.work.top, gSeen.dpi * 100 / 96, gSeen.pages, gSeen.off.c_str()));
	if (!gSeen.open || gSeen.contentH.empty()) return;
	const double here = gSeen.dpi / 96.0;
	struct Screen { int w, h; };
	for (const Screen sc : { Screen{ 1024, 600 }, Screen{ 1280, 720 } }) {
		for (const int percent : { 125, 150 }) {
			const double k = percent / 100.0;
			const UINT dpi = (UINT)std::lround(96 * k);
			// Windows 11's taskbar, 48 points (Windows 10's is 40).
			const RECT work = { 0, 0, sc.w, sc.h - (int)std::lround(48 * k) };
			const RECT frame = formFrame(dpi);
			const int clientW = (int)std::lround(gSeen.width * k);
			const int scrollW = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
			// Opened centred on its circuit window (filling the screen here),
			// as tall as its first page, kept on the screen; then each page fits.
			const int first = (int)std::ceil(gSeen.contentH[0] / here * k);
			const int ww0 = clientW + (frame.right - frame.left), wh0 = first + (frame.bottom - frame.top);
			POINT at = { std::max<int>(work.left, std::min<int>((work.left + work.right - ww0) / 2, work.right - ww0)),
			             std::max<int>(work.top, std::min<int>((work.top + work.bottom - wh0) / 2, work.bottom - wh0)) };
			bool ok = true;
			int scrolling = 0, leastRoom = INT_MAX;
			std::string why;
			for (size_t p = 0; p < gSeen.contentH.size(); p++) {
				const int contentH = (int)std::ceil(gSeen.contentH[p] / here * k);
				const FormFit fit = formFit(work, at, clientW, contentH, frame, scrollW, (int)std::lround(120 * k));
				const RECT& r = fit.window;
				if (r.left < work.left || r.top < work.top || r.right > work.right || r.bottom > work.bottom) {
					ok = false;
					why += strf(" page %zu at %ld,%ld-%ld,%ld;", p + 1, r.left, r.top, r.right, r.bottom);
				}
				if (fit.scrolls) scrolling++;
				leastRoom = std::min(leastRoom, fit.clientH);
				at = POINT{ r.left, r.top };
			}
			check(ok && leastRoom >= (int)std::lround(200 * k),
			      strf("%sfits a %d x %d screen at %d%%: every page on it, %d scrolling, at least %d px of page to see%s", P, sc.w, sc.h, percent,
			           scrolling, leastRoom, why.c_str()));
		}
	}
}

}  // namespace

void prepare() {
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	gHome = U(tmp) + strf("cedarlogic-rail-%lu", (unsigned long)GetCurrentProcessId());
	CreateDirectoryW(W(gHome).c_str(), nullptr);
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(gHome).c_str());
	ActionsRail::watchPointer = false;
	showme::quiet = true;
}

int run(const std::function<void(const std::string&)>& out) {
	gOut = out;
	guarded("the rail check", [] {
		defaultsAndOrder();
		search();
		geometry();
		hover();
		window();
		showMeAround();
		aboutAndWheel();
		introRules();
		introReplay();
		edgeDrags();
		accentContrast();
		settingsFit();
	});
	gOut(strf("rail check: %d passed, %d failed%s\n", gPassed, gFailed, gFailed == 0 ? " -- all passed" : ""));
	removeTree(gHome);
	return gFailed == 0 && gPassed > 0 ? 0 : 1;
}

}  // namespace railcheck
