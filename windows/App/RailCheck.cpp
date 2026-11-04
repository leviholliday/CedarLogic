// --rail-check (see RailCheck.h).

#include "RailCheck.h"
#include "Actions.h"
#include "Canvas.h"
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
// intro checks: the timeline's length, which launches play which intro, the
// sound (the first launch's and an update's, never an every-launch one),
// "Play the intro every time CedarLogic opens" (off unless chosen), and Help
// > Play the Intro (in the actions, in the menu bar, and played: sound and
// all, the once-only keys left alone, gone again by the end).
void introRules() {
	const Prefs before = prefs();
	check(std::fabs(intro::kTotal - 6.07) < 0.005 && std::fabs(intro::kDissolveAt - 5.52) < 0.005,
	      strf("intro: 6.07 s in all, the panel held to %.2f s (%.2f s)", intro::kDissolveAt, intro::kTotal));
	check(std::lround(intro::kTotal * 60) + 1 == 365, "intro: --render-intro draws 365 frames");
	loadText("intro-new.ini", "");
	check(!prefs().introEveryLaunch, "intro: \"Play the intro every time CedarLogic opens\" is off by default");
	loadText("intro-every.ini", "[CedarLogic]\nintroEveryLaunch=true\n");
	check(prefs().introEveryLaunch, "intro: and stays on once chosen");
	check(intro::hasSound(intro::Trigger::First) && intro::hasSound(intro::Trigger::Update) && !intro::hasSound(intro::Trigger::Every),
	      "intro: the sound is for the first launch and updates only");
	Prefs first;
	check(intro::forLaunch(first) == intro::Trigger::First && intro::hasSound(intro::forLaunch(first)),
	      "intro: a first launch plays the first launch's intro, with the sound");
	Prefs later;
	later.hasSeenWelcome = later.firstLaunchPlayed = later.introFirstLaunchPlayed = true;
	later.seenWhatsNew = whatsnew::kVersion;
	check(intro::forLaunch(later) == intro::Trigger::None, "intro: a later launch plays none with the setting off");
	later.introEveryLaunch = true;
	check(intro::forLaunch(later) == intro::Trigger::Every && !intro::hasSound(intro::forLaunch(later)),
	      "intro: with the setting on, a later launch plays it, with no sound");
	Prefs updated = later;
	updated.introEveryLaunch = false;
	updated.seenWhatsNew = "an older version";
	check(intro::forLaunch(updated) == intro::Trigger::Update && intro::hasSound(intro::forLaunch(updated)),
	      "intro: the first launch after an update plays the update's intro, with the sound");
	updated.introEveryLaunch = true;
	check(intro::forLaunch(updated) == intro::Trigger::Update, "intro: the update's intro (and its sound), setting or not");
	updated.introUpdatePlayed = whatsnew::kVersion;
	check(intro::forLaunch(updated) == intro::Trigger::Every, "intro: once played, the update's is the every-launch one again");
	const actions::Def* play = actions::find("playIntro");
	check(play && play->command == CMD_PLAY_INTRO && std::string(play->menu) == "Help" && std::string(play->title) == "Play the Intro",
	      "intro: Play the Intro is among the actions, in Help");
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
	});
	gOut(strf("rail check: %d passed, %d failed%s\n", gPassed, gFailed, gFailed == 0 ? " -- all passed" : ""));
	removeTree(gHome);
	return gFailed == 0 && gPassed > 0 ? 0 : 1;
}

}  // namespace railcheck
