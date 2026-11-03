// The actions (see Actions.h).

#include "Actions.h"
#include "Commands.h"
#include "Shortcuts.h"
#include "Window.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace actions {

namespace {

std::vector<std::string> splitIds(const std::string& s) {
	std::vector<std::string> out;
	std::istringstream in(s);
	std::string id;
	while (std::getline(in, id, ',')) if (!id.empty() && find(id)) out.push_back(id);
	return out;
}

std::string joinIds(const std::vector<std::string>& ids) {
	std::string out;
	for (const std::string& id : ids) out += (out.empty() ? "" : ",") + id;
	return out;
}

// A saved order with any actions it doesn't know (new ones) after it; none
// saved: the default six first, then the rest in menu order.
std::vector<std::string> completeOrder(const std::vector<std::string>& saved) {
	std::vector<std::string> out;
	auto add = [&](const std::string& id) { if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id); };
	for (const std::string& id : saved.empty() ? defaults() : saved) add(id);
	for (const Def& d : all()) add(d.id);
	return out;
}

std::vector<std::string> order() { return completeOrder(splitIds(prefs().actionsOrder)); }
void setOrder(const std::vector<std::string>& ids) { prefs().actionsOrder = joinIds(ids); }

std::vector<std::string> shownIds() {
	return prefs().actionsShownSet ? splitIds(prefs().actionsShown) : defaults();
}

}  // namespace

const std::vector<Def>& all() {
	static const std::vector<Def> list = {
		// File
		{ "newCircuit", "New Circuit", "File", 0, CMD_NEW },
		{ "openLibrary", "Your Circuits…", "File", 0, CMD_OPEN },
		{ "importFile", "Import a File…", "File", 0xE8B5, CMD_IMPORT },
		{ "newFromTemplate", "New from Template…", "File", 0xE8A9, CMD_NEW_TEMPLATE },
		{ "practice", "Open the Practice Circuit", "File", 0xE7BE, CMD_OPEN_SAMPLE },
		{ "save", "Save a Version", "File", 0xE74E, CMD_SAVE },
		{ "versionHistory", "Version History…", "File", 0xE81C, CMD_VERSIONS },
		{ "saveAsTemplate", "Save as Template…", "File", 0xECA5, CMD_SAVE_TEMPLATE },
		{ "exportImage", "Export as Image…", "File", 0xE91B, CMD_EXPORT_IMAGE },
		{ "exportReport", "Export Lab Report…", "File", 0xE9F9, CMD_EXPORT_REPORT },
		{ "exportFile", "Export as CedarLogic File…", "File", 0xEDE1, CMD_SAVE_AS },
		{ "exportV2", "Export for CedarLogic 2…", "File", 0xE8A5, CMD_EXPORT_V2 },
		{ "exportV1", "Export for CedarLogic 1.x…", "File", 0xE7C3, CMD_EXPORT_V1 },
		{ "shareLink", "Share Link…", "File", 0xE71B, CMD_SHARE_LINK },
		{ "print", "Print…", "File", 0xE749, CMD_PRINT },
		{ "settings", "Settings…", "File", 0xE713, CMD_PREFERENCES },
		// Edit
		{ "undo", "Undo", "Edit", 0xE7A7, CMD_UNDO },
		{ "redo", "Redo", "Edit", 0xE7A6, CMD_REDO },
		{ "cut", "Cut", "Edit", 0xE8C6, CMD_CUT },
		{ "copy", "Copy", "Edit", 0xE8C8, CMD_COPY },
		{ "paste", "Paste", "Edit", 0xE77F, CMD_PASTE },
		{ "duplicate", "Duplicate", "Edit", 0xE7C4, CMD_DUPLICATE },
		{ "delete", "Delete", "Edit", 0xE74D, CMD_DELETE },
		{ "selectAll", "Select All", "Edit", 0xE8B3, CMD_SELECT_ALL },
		{ "find", "Find…", "Edit", 0xE721, CMD_FIND },
		{ "findNext", "Find Next", "Edit", 0xE74B, CMD_FIND_NEXT },
		{ "findPrevious", "Find Previous", "Edit", 0xE74A, CMD_FIND_PREVIOUS },
		// Circuit (the Mac keeps these in Edit)
		{ "addGate", "Add a Gate…", "Circuit", 0xE710, CMD_ADD_GATE },
		{ "rotate", "Rotate", "Circuit", 0xE7AD, CMD_ROTATE },
		{ "lockInPlace", "Lock in Place", "Circuit", 0xE718, CMD_LOCK_IN_PLACE },
		{ "unlockAllParts", "Unlock All Parts", "Circuit", 0xE77A, CMD_UNLOCK_ALL_PARTS },
		{ "straighten", "Straighten Wires", "Circuit", 0xED5E, CMD_STRAIGHTEN },
		{ "tidy", "Tidy Up", "Circuit", 0xEA99, CMD_TIDY },
		{ "tidyFlow", "Tidy Up by Signal Flow", "Circuit", 0xE8B1, CMD_TIDY_FLOW },
		{ "connectNearby", "Connect Nearby Pins", "Circuit", 0xE703, CMD_CONNECT_NEARBY },
		{ "buildFormula", "Build from Formula…", "Circuit", 0xE943, CMD_BUILD_FORMULA },
		{ "saveAsPart", "Save as Part…", "Circuit", 0xE7B8, CMD_SAVE_PART },
		{ "connectAllPages", "Connect All Pages", "Circuit", 0xE8A1, CMD_CONNECT_ALL_PAGES },
		{ "disconnectAllPages", "Disconnect All Pages", "Circuit", 0xE738, CMD_DISCONNECT_ALL_PAGES },
		// View
		{ "zoomIn", "Zoom In", "View", 0xE8A3, CMD_ZOOM_IN },
		{ "zoomOut", "Zoom Out", "View", 0xE71F, CMD_ZOOM_OUT },
		{ "zoomFit", "Zoom to Fit", "View", 0xE9A6, CMD_ZOOM_FIT },
		{ "zoomActual", "Actual Size", "View", 0xE71E, CMD_ZOOM_ACTUAL },
		{ "darkMode", "Dark Mode", "View", 0xE708, CMD_DARK },
		{ "focusMode", "Focus Mode", "View", 0xE740, CMD_FOCUS_MODE },
		{ "palette", "Gate Palette", "View", 0xE90C, CMD_PALETTE },
		{ "statusBar", "Status Bar", "View", 0xE8FD, CMD_STATUS_BAR },
		{ "gridlines", "Show Grid", "View", 0xF0E2, CMD_SHOW_GRID },
		{ "wireDots", "Dots at Wire Bends", "View", 0xEA3A, CMD_WIRE_DOTS },
		{ "projector", "Projector Mode", "View", 0xE95D, CMD_PROJECTOR },
		{ "draw", "Draw on the Circuit", "View", 0xE70F, CMD_DRAW },
		{ "showDrawing", "Show or Hide Drawing", "View", 0xE890, CMD_SHOW_DRAWING },
		{ "clearDrawing", "Clear Drawing on This Page", "View", 0xE75C, CMD_CLEAR_DRAWING },
		{ "notes", "Notes", "View", 0xE70B, CMD_NOTES },
		// Simulate
		{ "pause", "Pause or Resume", "Simulate", 0xE769, CMD_RUNNING },
		{ "step", "Step Once", "Simulate", 0xE893, CMD_STEP },
		{ "stepClock", "Step Clock", "Simulate", 0xE823, CMD_STEP_CLOCK },
		{ "simView", "Simulation View", "Simulate", 0xE768, CMD_SIM_VIEW },
		{ "lock", "Lock the Circuit", "Simulate", 0xE72E, CMD_LOCK },
		{ "predict", "Predict, Then Reveal", "Simulate", 0xE9CE, CMD_PREDICT },
		{ "reveal", "Reveal", "Simulate", 0xE73E, CMD_REVEAL },
		{ "truthTable", "Truth Table", "Simulate", 0xE80A, CMD_TRUTH_TABLE },
		{ "checkCircuit", "Check My Circuit…", "Simulate", 0xE930, CMD_CHECK_CIRCUIT },
		{ "scope", "Oscilloscope", "Simulate", 0xE9D9, CMD_SCOPE },
		// Window
		{ "newTab", "New Tab", "Window", 0, CMD_NEW_TAB },
		{ "closeTab", "Close Tab", "Window", 0xE8BB, CMD_CLOSE_TAB },
		{ "reopenTab", "Reopen Closed Tab", "Window", 0xE72C, CMD_REOPEN_TAB },
		{ "nextTab", "Next Tab", "Window", 0xE72A, CMD_NEXT_TAB },
		{ "previousTab", "Previous Tab", "Window", 0xE72B, CMD_PREVIOUS_TAB },
		{ "splitView", "Split View", "Window", 0xE8A0, CMD_SPLIT_VIEW },
		{ "switchPane", "Switch Side", "Window", 0xE8AB, CMD_SWITCH_PANE },
		{ "closeSplit", "Close Split View", "Window", 0xE89F, CMD_CLOSE_SPLIT },
		// Help
		{ "help", "CedarLogic Help", "Help", 0xE897, CMD_HELP },
		{ "shortcuts", "Keyboard Shortcuts", "Help", 0xE765, CMD_SHORTCUTS },
		{ "tour", "Guided Tour", "Help", 0xE7C1, CMD_TOUR },
		{ "welcome", "Welcome to CedarLogic…", "Help", 0xE734, CMD_WELCOME },
		{ "whatsNew", "What's New in CedarLogic", "Help", 0xE789, CMD_WHATS_NEW },
		{ "feedback", "Send Feedback…", "Help", 0xED15, CMD_FEEDBACK },
		{ "checkForUpdates", "Check for Updates…", "Help", 0xE896, CMD_CHECK_UPDATES },
		{ "about", "About CedarLogic", "Help", 0xE946, CMD_ABOUT },
	};
	return list;
}

const Def* find(const std::string& id) {
	for (const Def& d : all()) if (id == d.id) return &d;
	return nullptr;
}

const Def* forCommand(int command) {
	for (const Def& d : all()) if (d.command == command) return &d;
	return nullptr;
}

const std::vector<std::string>& defaults() {
	static const std::vector<std::string> six = { "checkCircuit", "predict", "tidy", "shareLink", "exportImage", "exportReport" };
	return six;
}

std::string plainTitle(const Def& d) {
	std::string t = d.title;
	const std::string dots = "…";
	const size_t at = t.find(dots);
	if (at != std::string::npos) t.erase(at, dots.size());
	return t;
}

std::string keyLabel(const Def& d) {
	// The fixed ones the menus show.
	if (d.command == CMD_DELETE) return "Del";
	if (d.command == CMD_REVEAL) return "Enter";
	const shortcuts::Action* a = shortcuts::forCommand(d.command);
	const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
	return k.valid() ? shortcuts::label(k) : std::string();
}

std::string titleWithKey(const Def& d) {
	const std::string k = keyLabel(d);
	return plainTitle(d) + (k.empty() ? "" : " (" + k + ")");
}

std::vector<std::string> listed() { return order(); }

std::vector<std::string> rail() {
	const std::vector<std::string> on = shownIds();
	std::vector<std::string> out;
	for (const std::string& id : order())
		if (std::find(on.begin(), on.end(), id) != on.end()) out.push_back(id);
	return out;
}

bool shown(const std::string& id) {
	const std::vector<std::string> on = shownIds();
	return std::find(on.begin(), on.end(), id) != on.end();
}

void setShown(const std::string& id, bool on) {
	std::vector<std::string> ids = shownIds();
	ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
	if (on && find(id)) ids.push_back(id);
	prefs().actionsShown = joinIds(ids);
	prefs().actionsShownSet = true;
}

void moveListed(int from, int to) {
	std::vector<std::string> ids = order();
	if (from < 0 || from >= (int)ids.size()) return;
	to = std::max(0, std::min(to, (int)ids.size() - 1));
	if (to == from) return;
	const std::string id = ids[from];
	ids.erase(ids.begin() + from);
	ids.insert(ids.begin() + to, id);
	setOrder(ids);
}

void moveRail(const std::string& id, int index) {
	std::vector<std::string> items = rail();
	auto f = std::find(items.begin(), items.end(), id);
	if (f == items.end()) return;
	const int from = (int)(f - items.begin());
	items.erase(f);
	const int to = std::max(0, std::min(index, (int)items.size()));
	if (to == from) return;
	std::vector<std::string> ids = order();
	ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
	if (to < (int)items.size()) {
		ids.insert(std::find(ids.begin(), ids.end(), items[to]), id);
	} else if (!items.empty()) {
		auto last = std::find(ids.begin(), ids.end(), items.back());
		ids.insert(last == ids.end() ? last : last + 1, id);
	} else {
		ids.insert(ids.begin(), id);
	}
	setOrder(ids);
}

void reset() {
	prefs().actionsOrder.clear();
	prefs().actionsShown.clear();
	prefs().actionsShownSet = false;
}

bool isDefault() {
	std::vector<std::string> on = shownIds(), six = defaults();
	std::sort(on.begin(), on.end());
	std::sort(six.begin(), six.end());
	return on == six && order() == completeOrder({});
}

std::string words(const Def& d) {
	std::string w = plainTitle(d) + " " + d.menu;
	const std::string k = keyLabel(d);
	if (!k.empty()) {
		w += " " + k;
		std::istringstream in(k);
		std::string part;
		while (std::getline(in, part, '+')) {
			const std::string p = lowerCase(part);
			w += " " + (p == "ctrl" ? std::string("ctrl control") : p == "del" ? std::string("del delete") : p);
		}
	}
	return lowerCase(w);
}

bool matches(const Def& d, const std::string& query) {
	const std::string hay = words(d);
	// Its words on their own, for a key typed alone ("ctrl e": the E key,
	// not every name with an e in it).
	std::vector<std::string> tokens;
	std::string t;
	for (char c : hay + " ") {
		if (std::isalnum((unsigned char)c) || (unsigned char)c >= 0x80) t += c;
		else if (!t.empty()) { tokens.push_back(t); t.clear(); }
	}
	std::istringstream in(lowerCase(query));
	std::string word;
	while (in >> word) {
		if (word.size() == 1) {
			if (std::find(tokens.begin(), tokens.end(), word) == tokens.end() && hay.find(" " + word + " ") == std::string::npos) return false;
		} else if (hay.find(word) == std::string::npos) {
			return false;
		}
	}
	return true;
}

void changed() {
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->actionsChanged();
}

bool enabled(const CircuitWindow* w, const Def& d) {
	if (!w) return false;
	if (d.command == CMD_STEP_CLOCK) return w->hasManualClock();
	return w->commandEnabled(d.command);
}

bool isOn(const CircuitWindow* w, const Def& d) {
	if (!w) return false;
	switch (d.command) {
	case CMD_RUNNING: return !w->running();   // lit while paused, as the toolbar's
	case CMD_DRAW: return w->drawing();
	case CMD_NOTES: return w->notesShown();
	case CMD_SCOPE: return w->scopeShown();
	default: return w->commandChecked(d.command) == 1;
	}
}

}  // namespace actions

namespace rail {

bool inHotStrip(const RECT& client, int top, int bottom, POINT p, double scale) {
	const int w = (int)std::lround(kHotWidth * scale);
	return p.x >= client.right - w && p.x <= client.right && p.y >= top && p.y < bottom;
}

bool Hover::update(double now, bool strip, bool over, bool folded, bool hoverOn) {
	const bool was = slidOut;
	if (!folded) {
		// Open (or not on screen): nothing to slide out.
		overStrip = overRail = slidOut = dismissed = false;
		pending = 0;
		return was != slidOut;
	}
	if (strip != overStrip || over != overRail) {
		overStrip = strip;
		overRail = over;
		changed(now, folded, hoverOn);
	}
	// Turned off in Settings while it was out: back in.
	if (!hoverOn && slidOut) slide(false);
	if (pending && now >= due) {
		const bool inside = overStrip || overRail;
		if (pending == 1 && hoverOn && inside && !dismissed) slide(true);
		else if (pending == -1 && !inside && !menuOpen && !dragging) slide(false);
		pending = 0;
	}
	return was != slidOut;
}

void Hover::changed(double now, bool folded, bool hoverOn) {
	pending = 0;
	const bool inside = overStrip || overRail;
	if (!inside) dismissed = false;
	if (!folded || !hoverOn || dismissed) return;
	if (inside && !slidOut) {
		pending = 1;
		due = now + kOpenDelay;
	} else if (!inside && slidOut && !menuOpen && !dragging) {
		pending = -1;
		due = now + kCloseDelay;
	}
}

void Hover::slide(bool out) {
	pending = 0;
	slidOut = out;
	if (!out) overRail = false;
}

}  // namespace rail
