// Every command's keys (see Shortcuts.h).

#include "Shortcuts.h"
#include "Chrome.h"
#include "Commands.h"
#include "Integration.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <map>
#include <sstream>

namespace shortcuts {

namespace {

std::map<std::string, std::string>& overrides() {
	static std::map<std::string, std::string> m = [] {
		std::map<std::string, std::string> out;
		std::istringstream in(prefs().shortcuts);
		std::string item;
		while (std::getline(in, item, ';')) {
			const size_t eq = item.find('=');
			if (eq != std::string::npos && eq > 0) out[item.substr(0, eq)] = item.substr(eq + 1);
		}
		return out;
	}();
	return m;
}

void save() {
	std::string s;
	for (const auto& kv : overrides()) s += kv.first + "=" + kv.second + ";";
	prefs().shortcuts = s;
	prefs().save();
}

std::vector<Key> split(const std::string& s) {
	std::vector<Key> out;
	std::istringstream in(s);
	std::string part;
	while (std::getline(in, part, '|')) {
		const Key k = parse(part);
		if (k.valid()) out.push_back(k);
	}
	return out;
}

std::string join(const std::vector<Key>& keys) {
	std::string out;
	for (const Key& k : keys) out += (out.empty() ? "" : "|") + text(k);
	return out;
}

// A changed action's keys are kept like the defaults, separated by '|'.
std::vector<Key> keyList(const Action& a) {
	auto it = overrides().find(a.id);
	if (it != overrides().end()) return split(it->second);
	return split(a.keys);
}

// The keys with a name of their own (letters, digits, F1... and the number
// pad's digits are worked out); `shown` is what a US keyboard has on it,
// for when the keyboard in use can't say.
struct Name { UINT vk; const char* name; const char* shown; };
const Name kNames[] = {
	{ VK_OEM_PLUS, "Equals", "=" }, { VK_OEM_MINUS, "Minus", "-" }, { VK_OEM_COMMA, "Comma", "," }, { VK_OEM_PERIOD, "Period", "." },
	{ VK_OEM_2, "Slash", "/" }, { VK_OEM_1, "Semicolon", ";" }, { VK_OEM_7, "Quote", "'" }, { VK_OEM_4, "LeftBracket", "[" },
	{ VK_OEM_6, "RightBracket", "]" }, { VK_OEM_5, "Backslash", "\\" }, { VK_OEM_3, "Backquote", "`" }, { VK_OEM_102, "Oem102", "\\" },
	{ VK_PRIOR, "PageUp", "PgUp" }, { VK_NEXT, "PageDown", "PgDn" }, { VK_HOME, "Home", "Home" }, { VK_END, "End", "End" },
	{ VK_INSERT, "Insert", "Ins" }, { VK_DELETE, "Delete", "Del" }, { VK_BACK, "Backspace", "Backspace" }, { VK_RETURN, "Enter", "Enter" },
	{ VK_TAB, "Tab", "Tab" }, { VK_SPACE, "Space", "Space" }, { VK_ESCAPE, "Escape", "Esc" }, { VK_LEFT, "Left", "Left" },
	{ VK_RIGHT, "Right", "Right" }, { VK_UP, "Up", "Up" }, { VK_DOWN, "Down", "Down" }, { VK_ADD, "NumPlus", "Num +" },
	{ VK_SUBTRACT, "NumMinus", "Num -" }, { VK_MULTIPLY, "NumStar", "Num *" }, { VK_DIVIDE, "NumSlash", "Num /" },
	{ VK_DECIMAL, "NumDot", "Num ." }, { VK_PAUSE, "Pause", "Pause" },
};

std::string keyName(UINT vk) {
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
	if (vk >= VK_F1 && vk <= VK_F24) return strf("F%u", vk - VK_F1 + 1);
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return strf("Num%u", vk - VK_NUMPAD0);
	for (const Name& n : kNames) if (n.vk == vk) return n.name;
	return std::string();
}

UINT keyFromName(const std::string& name) {
	if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return (UINT)name[0];
	if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return (UINT)(name[0] - 'a' + 'A');
	if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f') && isdigit((unsigned char)name[1])) {
		const int n = atoi(name.c_str() + 1);
		if (n >= 1 && n <= 24) return VK_F1 + (UINT)(n - 1);
	}
	if (name.size() == 4 && name.compare(0, 3, "Num") == 0 && isdigit((unsigned char)name[3])) return VK_NUMPAD0 + (UINT)(name[3] - '0');
	for (const Name& n : kNames) if (lowerCase(n.name) == lowerCase(name)) return n.vk;
	return 0;
}

// What the key shows: the character the keyboard in use has on it (an
// OEM key's differs from layout to layout), else the US one.
std::string keyShown(UINT vk) {
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
	if (vk >= VK_F1 && vk <= VK_F24) return strf("F%u", vk - VK_F1 + 1);
	if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return strf("Num %u", vk - VK_NUMPAD0);
	for (const Name& n : kNames) {
		if (n.vk != vk) continue;
		const bool oem = vk == VK_OEM_PLUS || vk == VK_OEM_MINUS || vk == VK_OEM_COMMA || vk == VK_OEM_PERIOD || (vk >= VK_OEM_1 && vk <= VK_OEM_3) ||
		                 (vk >= VK_OEM_4 && vk <= VK_OEM_7) || vk == VK_OEM_102;
		if (oem) {
			// A dead key (an accent waiting for its letter) has the top bit set.
			const UINT ch = MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFF;
			if (ch > L' ') return U(std::wstring(1, (wchar_t)towupper((wint_t)ch)));
		}
		return n.shown;
	}
	return "?";
}

bool isModifier(UINT vk) {
	switch (vk) {
	case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
	case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
		return true;
	default:
		return false;
	}
}

bool down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

}  // namespace

const std::vector<Action>& all() {
	static const std::vector<Action> list = {
		{ "newCircuit", "Circuits", "New circuit", CMD_NEW, false, "Ctrl+N" },
		{ "openLibrary", "Circuits", "Open one of your circuits", CMD_OPEN, false, "Ctrl+O" },
		{ "importFile", "Circuits", "Import a .cdl file", CMD_IMPORT, false, "Ctrl+I" },
		{ "save", "Circuits", "Save now and keep a version", CMD_SAVE, false, "Ctrl+S" },
		{ "exportImage", "Circuits", "Export as an image", CMD_EXPORT_IMAGE, false, "Ctrl+E" },
		{ "exportFile", "Circuits", "Export as a CedarLogic file", CMD_SAVE_AS, false, "Ctrl+Shift+S" },
		{ "print", "Circuits", "Print", CMD_PRINT, false, "Ctrl+P" },
		{ "closeWindow", "Circuits", "Close the window", CMD_CLOSE_WINDOW, false, "Ctrl+Shift+W" },
		{ "quit", "Circuits", "Quit CedarLogic", CMD_QUIT, false, "Ctrl+Q" },
		{ "undo", "Editing", "Undo", CMD_UNDO, false, "Ctrl+Z" },
		{ "redo", "Editing", "Redo", CMD_REDO, false, "Ctrl+Y|Ctrl+Shift+Z" },
		{ "cut", "Editing", "Cut", CMD_CUT, false, "Ctrl+X" },
		{ "copy", "Editing", "Copy", CMD_COPY, false, "Ctrl+C" },
		{ "paste", "Editing", "Paste (it follows your mouse)", CMD_PASTE, false, "Ctrl+V" },
		{ "duplicate", "Editing", "Duplicate the selection", CMD_DUPLICATE, false, "Ctrl+D" },
		{ "selectAll", "Editing", "Select everything on the page", CMD_SELECT_ALL, false, "Ctrl+A" },
		{ "find", "Editing", "Find a label, a TO/FROM name or a part", CMD_FIND, false, "Ctrl+F" },
		{ "addGate", "Building", "Add a gate by name", CMD_ADD_GATE, true, "A" },
		{ "rotate", "Building", "Rotate the selection", CMD_ROTATE, true, "R" },
		{ "straighten", "Building", "Straighten the selected wires", CMD_STRAIGHTEN, true, "S" },
		{ "tidy", "Building", "Tidy up (preview first)", CMD_TIDY, true, "Shift+S" },
		{ "quickCopy", "Building", "Copy, or connect while moving (quick key)", CMD_COPY, true, "C" },
		{ "quickPaste", "Building", "Paste (quick key)", CMD_PASTE, true, "V" },
		{ "quickCut", "Building", "Cut (quick key)", CMD_CUT, true, "X" },
		{ "quickDuplicate", "Building", "Duplicate (quick key)", CMD_DUPLICATE, true, "D" },
		{ "buildFormula", "Building", "Build a circuit from a formula", CMD_BUILD_FORMULA, false, "" },
		{ "zoomIn", "Moving around", "Zoom in", CMD_ZOOM_IN, false, "Ctrl+Equals|Ctrl+Shift+Equals|Ctrl+NumPlus" },
		{ "zoomOut", "Moving around", "Zoom out", CMD_ZOOM_OUT, false, "Ctrl+Minus|Ctrl+NumMinus" },
		{ "zoomFit", "Moving around", "Zoom to fit", CMD_ZOOM_FIT, false, "Ctrl+0|Ctrl+Num0" },
		{ "zoomActual", "Moving around", "Actual size", CMD_ZOOM_ACTUAL, false, "Ctrl+1|Ctrl+Num1" },
		{ "focusMode", "Moving around", "Focus mode: hide the toolbar and side panel", CMD_FOCUS_MODE, false, "Ctrl+Period" },
		{ "palette", "Moving around", "Show or hide the side panel", CMD_PALETTE, false, "" },
		{ "simView", "Simulation", "Simulation view", CMD_SIM_VIEW, false, "Ctrl+R" },
		{ "step", "Simulation", "Step once", CMD_STEP, false, "Ctrl+Shift+R" },
		{ "truthTable", "Simulation", "Truth table", CMD_TRUTH_TABLE, true, "T" },
		{ "scope", "Simulation", "Oscilloscope", CMD_SCOPE, false, "Ctrl+G" },
		{ "lock", "Simulation", "Lock the circuit", CMD_LOCK, false, "" },
		{ "newTab", "Tabs and split view", "New tab", CMD_NEW_TAB, false, "Ctrl+T" },
		{ "closeTab", "Tabs and split view", "Close tab", CMD_CLOSE_TAB, false, "Ctrl+W" },
		{ "reopenTab", "Tabs and split view", "Reopen the tab you closed", CMD_REOPEN_TAB, false, "Ctrl+Shift+T" },
		{ "renameTab", "Tabs and split view", "Rename the tab (in place)", CMD_RENAME_TAB, false, "" },
		{ "splitView", "Tabs and split view", "Split view", CMD_SPLIT_VIEW, false, "Ctrl+Alt+S" },
		{ "switchPane", "Tabs and split view", "Switch side (in a split view)", CMD_SWITCH_PANE, false, "F6|Ctrl+Alt+Right|Ctrl+Alt+Left" },
		{ "closeSplit", "Tabs and split view", "Close split view", CMD_CLOSE_SPLIT, false, "Ctrl+Alt+W" },
		{ "nextTab", "Tabs and split view", "Next tab", CMD_NEXT_TAB, false, "Ctrl+PageDown" },
		{ "previousTab", "Tabs and split view", "Previous tab", CMD_PREVIOUS_TAB, false, "Ctrl+PageUp" },
		{ "shortcuts", "App", "Every shortcut (this list)", CMD_SHORTCUTS, false, "Ctrl+Slash" },
		{ "darkMode", "App", "Dark mode", CMD_DARK, false, "Ctrl+Shift+D" },
		{ "statusBar", "App", "Show or hide the status bar", CMD_STATUS_BAR, false, "" },
		{ "preferences", "App", "Settings", CMD_PREFERENCES, false, "Ctrl+Comma" },
		{ "help", "App", "CedarLogic Help", CMD_HELP, false, "F1" },
		{ "checkUpdates", "App", "Check for updates", CMD_CHECK_UPDATES, false, "" },
		{ "startMenu", "App", "Add to or remove from the Start menu", CMD_START_MENU, false, "" },
		{ "about", "App", "About CedarLogic", CMD_ABOUT, false, "" },
		{ "feedback", "App", "Send feedback", CMD_FEEDBACK, false, "" },
	};
	return list;
}

bool available(const Action& a) { return a.command != CMD_START_MENU || integration::offered(); }

const Action* find(const std::string& id) {
	for (const Action& a : all()) if (id == a.id) return &a;
	return nullptr;
}

const Action* forCommand(int command) {
	const Action* canvasOne = nullptr;
	for (const Action& a : all()) {
		if (a.command != command) continue;
		if (!a.canvas) return &a;
		if (!canvasOne) canvasOne = &a;
	}
	return canvasOne;
}

std::vector<Key> keys(const Action& a) { return keyList(a); }

Key first(const Action& a) {
	const std::vector<Key> k = keyList(a);
	return k.empty() ? Key() : k.front();
}

bool isCustom(const Action& a) { return overrides().count(a.id) > 0; }
bool anyCustom() { return !overrides().empty(); }

std::string set(const Action& a, const Key& key) {
	std::string loser;
	if (key.valid()) {
		// Whoever had the key loses just that one, and keeps any others.
		for (const Action& o : all()) {
			if (&o == &a) continue;
			std::vector<Key> kept;
			bool had = false;
			for (const Key& k : keyList(o)) {
				if (k == key) had = true;
				else kept.push_back(k);
			}
			if (!had) continue;
			overrides()[o.id] = join(kept);
			loser = o.id;
		}
	}
	// One of the action's own keys: it has all its keys back (bar any another
	// action has taken), the one pressed first.
	const std::vector<Key> defaults = split(a.keys);
	bool own = false;
	for (const Key& k : defaults) own = own || k == key;
	if (own) {
		std::vector<Key> list = { key };
		for (const Key& k : defaults) {
			if (k == key) continue;
			bool taken = false;
			for (const Action& o : all())
				if (&o != &a)
					for (const Key& ok : keyList(o)) taken = taken || ok == k;
			if (!taken) list.push_back(k);
		}
		if (join(list) == join(defaults)) overrides().erase(a.id);
		else overrides()[a.id] = join(list);
	} else {
		overrides()[a.id] = key.valid() ? text(key) : std::string();
	}
	save();
	return loser;
}

void reset(const Action& a) {
	overrides().erase(a.id);
	save();
}

void resetAll() {
	overrides().clear();
	save();
}

Key pressed(UINT vk) {
	Key k;
	// Alt alone belongs to the menus. (AltGr is Ctrl+Alt to Windows: the
	// callers leave it to a text box, where it types.)
	const bool alt = down(VK_MENU), ctrl = down(VK_CONTROL);
	if (isModifier(vk) || (alt && !ctrl) || down(VK_LWIN) || down(VK_RWIN)) return k;
	k.vk = vk;
	k.ctrl = ctrl;
	k.alt = alt;
	k.shift = down(VK_SHIFT);
	return k;
}

const Action* match(const Key& k, bool canvas) {
	if (!k.valid()) return nullptr;
	for (const Action& a : all()) {
		if (a.canvas != canvas || !available(a)) continue;
		for (const Key& own : keyList(a))
			if (own == k) return &a;
	}
	return nullptr;
}

std::string reserved(const Key& k) {
	const UINT v = k.vk;
	// Windows' own, and the window's, before any shortcut.
	if (k.alt && (v == VK_DELETE || v == VK_TAB)) return "Windows itself";
	if (v == VK_TAB && k.ctrl) return "switching tabs";
	if (v == VK_F10 || v == VK_APPS) return "the menus";
	if (v == VK_ESCAPE) return "letting go of what you're doing";
	if (k.ctrl) return "";
	// The canvas's, with or without Shift, before its single keys.
	if (v == VK_SPACE) return "moving around and zooming to fit";
	if (v == VK_LEFT || v == VK_RIGHT || v == VK_UP || v == VK_DOWN) return "nudging and moving around";
	if (v == VK_RETURN || v == VK_TAB) return "Tidy Up's preview";
	if (v == VK_DELETE || v == VK_BACK) return "deleting";
	if (k.shift && (MapVirtualKeyW(v, MAPVK_VK_TO_CHAR) & 0x7FFF) == L'/') return "the list of shortcuts";
	if (k.shift && v >= '0' && v <= '9') return "the gate categories";
	return "";
}

std::string label(const Key& k) {
	if (!k.valid()) return "";
	return std::string(k.ctrl ? "Ctrl+" : "") + (k.alt ? "Alt+" : "") + (k.shift ? "Shift+" : "") + keyShown(k.vk);
}

std::vector<std::string> caps(const Key& k) {
	std::vector<std::string> out;
	if (!k.valid()) return out;
	if (k.ctrl) out.push_back("Ctrl");
	if (k.alt) out.push_back("Alt");
	if (k.shift) out.push_back("Shift");
	switch (k.vk) {
	case VK_LEFT: out.push_back("←"); break;
	case VK_RIGHT: out.push_back("→"); break;
	case VK_UP: out.push_back("↑"); break;
	case VK_DOWN: out.push_back("↓"); break;
	default: out.push_back(keyShown(k.vk)); break;
	}
	return out;
}

std::string tipKeys(int command) {
	const Action* a = forCommand(command);
	const Key k = a ? first(*a) : Key();
	return k.valid() ? " (" + label(k) + ")" : std::string();
}

std::string menuKeys(int command) {
	const Action* a = forCommand(command);
	const Key k = a ? first(*a) : Key();
	return k.valid() ? "\t" + label(k) : std::string();
}

std::string keysFor(int command, const std::string& otherwise) {
	const Action* a = forCommand(command);
	const Key k = a ? first(*a) : Key();
	return k.valid() ? label(k) : otherwise;
}

void relabel(HMENU menu) {
	const int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		MENUITEMINFOW mi = {};
		mi.cbSize = sizeof mi;
		mi.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
		if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &mi)) continue;
		if (mi.hSubMenu) { relabel(mi.hSubMenu); continue; }
		if (mi.fType & (MFT_SEPARATOR | MFT_OWNERDRAW)) continue;
		if (forCommand((int)mi.wID) == nullptr) continue;
		wchar_t buf[256] = L"";
		MENUITEMINFOW get = {};
		get.cbSize = sizeof get;
		get.fMask = MIIM_STRING;
		get.dwTypeData = buf;
		get.cch = 255;
		if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &get)) continue;
		std::wstring t = buf;
		const size_t tab = t.find(L'\t');
		if (tab != std::wstring::npos) t.resize(tab);
		t += W(menuKeys((int)mi.wID));
		MENUITEMINFOW put = {};
		put.cbSize = sizeof put;
		put.fMask = MIIM_STRING;
		put.dwTypeData = &t[0];
		SetMenuItemInfoW(menu, (UINT)i, TRUE, &put);
	}
}

std::string text(const Key& k) {
	const std::string name = k.valid() ? keyName(k.vk) : std::string();
	if (name.empty()) return "";
	return std::string(k.ctrl ? "Ctrl+" : "") + (k.alt ? "Alt+" : "") + (k.shift ? "Shift+" : "") + name;
}

Key parse(const std::string& s) {
	Key k;
	std::istringstream in(s);
	std::string part, last;
	std::vector<std::string> parts;
	while (std::getline(in, part, '+')) parts.push_back(part);
	if (parts.empty()) return Key();
	for (size_t i = 0; i + 1 < parts.size(); i++) {
		const std::string m = lowerCase(parts[i]);
		if (m == "ctrl") k.ctrl = true;
		else if (m == "shift") k.shift = true;
		else if (m == "alt") k.alt = true;
		else return Key();   // nonsense
	}
	if (k.alt && !k.ctrl) return Key();   // Alt alone is the menus'
	k.vk = keyFromName(parts.back());
	return k.vk ? k : Key();
}

namespace {

// "drag", "click a pin" and "none" are said, not pressed; "PgDn" is a key.
bool isWord(const std::string& k) { return k.size() > 1 && islower((unsigned char)k[0]); }
float capWidth(const std::string& k) { return isWord(k) ? textWidth(k, 11.5f) + 4 : std::max(22.0f, textWidth(k, 11, true) + 14); }

}  // namespace

float capsWidth(const std::vector<std::string>& caps) {
	float w = 0;
	for (const std::string& k : caps) w += capWidth(k) + 4;
	return caps.empty() ? 0 : w - 4;
}

void drawCaps(ID2D1RenderTarget* rt, const std::vector<std::string>& caps, float right, float midY, const D2D1_COLOR_F& ink) {
	float x = right - capsWidth(caps);
	for (const std::string& k : caps) {
		const float kw = capWidth(k);
		const D2D1_RECT_F r = D2D1::RectF(x, midY - 11, x + kw, midY + 11);
		if (isWord(k)) {
			drawText(rt, k, r, 11.5f, withAlpha(ink, 0.55f), TextAlign::Center);
		} else {
			fillRound(rt, D2D1::RectF(r.left, r.top + 1, r.right, r.bottom + 1), 5, withAlpha(ink, 0.10f));
			fillRound(rt, r, 5, withAlpha(ink, 0.06f));
			strokeRound(rt, D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 4.5f, withAlpha(ink, 0.18f));
			drawText(rt, k, D2D1::RectF(r.left, r.top + 2, r.right, r.bottom - 2), 11, withAlpha(ink, 0.88f), TextAlign::Center, true);
		}
		x += kw + 4;
	}
}

}  // namespace shortcuts
