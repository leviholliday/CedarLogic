// Every command's keys (see Shortcuts.h).

#include "Shortcuts.h"

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

std::vector<std::string> split(const std::string& s) {
	std::vector<std::string> out;
	std::istringstream in(s);
	std::string part;
	while (std::getline(in, part, '|')) if (!part.empty()) out.push_back(part);
	return out;
}

std::string join(const std::vector<std::string>& keys) {
	std::string out;
	for (const std::string& k : keys) out += (out.empty() ? "" : "|") + k;
	return out;
}

// Both the same key, whatever the case and spelling.
bool same(const std::string& a, const std::string& b) {
	guint ka = 0, kb = 0;
	GdkModifierType ma, mb;
	gtk_accelerator_parse(a.c_str(), &ka, &ma);
	gtk_accelerator_parse(b.c_str(), &kb, &mb);
	return ka != 0 && gdk_keyval_to_lower(ka) == gdk_keyval_to_lower(kb) && ma == mb;
}

// A changed action's keys are kept like the defaults, separated by '|'.
std::vector<std::string> keyList(const Action& a) {
	auto it = overrides().find(a.id);
	if (it != overrides().end()) return split(it->second);
	return split(a.keys);
}

std::string accelName(guint keyval, guint state) {
	const GdkModifierType mods = (GdkModifierType)(state & gtk_accelerator_get_default_mod_mask());
	gchar* name = gtk_accelerator_name(gdk_keyval_to_lower(keyval), mods);
	std::string out = name ? name : "";
	g_free(name);
	return out;
}

// The same physical key on a Latin layout, when the one in use writes
// another script (Russian, Greek, Hebrew...), so R is still Rotate there.
// 0 when the key is Latin already (é and ß too), isn't a character, or no
// layout the desktop has gives it a Latin letter.
guint latinKeyval(const GdkEventKey* e) {
	if (gdk_keyval_to_unicode(e->keyval) < 0x370) return 0;   // Greek is the first script after Latin's
	GdkKeymap* keymap = gdk_keymap_get_for_display(e->window ? gdk_window_get_display(e->window) : gdk_display_get_default());
	GdkKeymapKey* entries = nullptr;
	guint* keyvals = nullptr;
	gint n = 0;
	if (keymap == nullptr || !gdk_keymap_get_entries_for_keycode(keymap, e->hardware_keycode, &entries, &keyvals, &n)) return 0;
	guint out = 0;
	for (gint i = 0; i < n && out == 0; i++) {
		if (keyvals[i] <= 0x20 || keyvals[i] >= 0x7f) continue;   // Latin keyvals are their ASCII codes
		// That layout, with the same Shift as now.
		guint kv = 0;
		if (gdk_keymap_translate_keyboard_state(keymap, e->hardware_keycode, (GdkModifierType)e->state, entries[i].group, &kv,
		                                        nullptr, nullptr, nullptr) &&
		    kv > 0x20 && kv < 0x7f)
			out = kv;
	}
	g_free(entries);
	g_free(keyvals);
	return out;
}

bool isModifier(guint keyval) {
	switch (keyval) {
	case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: case GDK_KEY_Control_L: case GDK_KEY_Control_R:
	case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: case GDK_KEY_Super_L: case GDK_KEY_Super_R:
	case GDK_KEY_Meta_L: case GDK_KEY_Meta_R: case GDK_KEY_ISO_Level3_Shift:
		return true;
	default:
		return false;
	}
}

}  // namespace

const std::vector<Action>& all() {
	static const std::vector<Action> list = {
		{ "newCircuit", "Circuits", "New circuit", "app.new", "<Primary>n" },
		{ "openLibrary", "Circuits", "Open one of your circuits", "app.open", "<Primary>o" },
		{ "importFile", "Circuits", "Import a .cdl file", "app.import", "<Primary>i" },
		{ "save", "Circuits", "Save now and keep a version", "win.save", "<Primary>s" },
		{ "exportImage", "Circuits", "Export as an image", "win.export-image", "<Primary>e" },
		{ "exportFile", "Circuits", "Export as a CedarLogic file", "win.save-as", "<Primary><Shift>s" },
		{ "print", "Circuits", "Print", "win.print", "<Primary>p" },
		{ "closeWindow", "Circuits", "Close the window", "win.close", "<Primary><Shift>w" },
		{ "quit", "Circuits", "Quit CedarLogic", "app.quit", "<Primary>q" },
		{ "undo", "Editing", "Undo", "win.undo", "<Primary>z" },
		{ "redo", "Editing", "Redo", "win.redo", "<Primary><Shift>z|<Primary>y" },
		{ "cut", "Editing", "Cut", "win.cut", "<Primary>x" },
		{ "copy", "Editing", "Copy", "win.copy", "<Primary>c" },
		{ "paste", "Editing", "Paste (it follows your mouse)", "win.paste", "<Primary>v" },
		{ "duplicate", "Editing", "Duplicate the selection", "win.duplicate", "<Primary>d" },
		{ "selectAll", "Editing", "Select everything on the page", "win.select-all", "<Primary>a" },
		{ "find", "Editing", "Find a label, a TO/FROM name or a part", "win.find", "<Primary>f" },
		{ "addGate", "Building", "Add a gate by name", nullptr, "a" },
		{ "rotate", "Building", "Rotate the selection", nullptr, "r" },
		{ "straighten", "Building", "Straighten the selected wires", nullptr, "s" },
		{ "tidy", "Building", "Tidy up (preview first)", nullptr, "<Shift>s" },
		{ "quickCopy", "Building", "Copy, or connect while moving (quick key)", nullptr, "c" },
		{ "quickPaste", "Building", "Paste (quick key)", nullptr, "v" },
		{ "quickCut", "Building", "Cut (quick key)", nullptr, "x" },
		{ "quickDuplicate", "Building", "Duplicate (quick key)", nullptr, "d" },
		{ "buildFormula", "Building", "Build a circuit from a formula", "win.build-formula", "" },
		{ "zoomIn", "Moving around", "Zoom in", "win.zoom-in", "<Primary>equal|<Primary>plus|<Primary>KP_Add" },
		{ "zoomOut", "Moving around", "Zoom out", "win.zoom-out", "<Primary>minus|<Primary>KP_Subtract" },
		{ "zoomFit", "Moving around", "Zoom to fit", "win.zoom-fit", "<Primary>0|<Primary>KP_0" },
		{ "zoomActual", "Moving around", "Actual size", "win.zoom-actual", "<Primary>1|<Primary>KP_1" },
		{ "focusMode", "Moving around", "Focus mode: hide the toolbar and side panel", "win.focus-mode", "<Primary>period" },
		{ "simView", "Simulation", "Simulation view", "win.sim-view", "<Primary>r" },
		{ "step", "Simulation", "Step once", "win.step", "<Primary><Shift>r" },
		{ "truthTable", "Simulation", "Truth table", nullptr, "t" },
		{ "checkCircuit", "Simulation", "Check my circuit against a formula or table", nullptr, "<Shift>t" },
		{ "scope", "Simulation", "Oscilloscope", "win.scope", "<Primary>g" },
		{ "lock", "Simulation", "Lock the circuit", "win.lock", "" },
		{ "newTab", "Tabs and split view", "New tab", "win.new-tab", "<Primary>t" },
		{ "closeTab", "Tabs and split view", "Close tab", "win.close-tab", "<Primary>w" },
		{ "reopenTab", "Tabs and split view", "Reopen the tab you closed", "win.reopen-tab", "<Primary><Shift>t" },
		{ "splitView", "Tabs and split view", "Split view", "win.split-view", "<Primary><Alt>s" },
		{ "switchPane", "Tabs and split view", "Switch side (in a split view)", "win.switch-pane", "F6|<Primary><Alt>Right|<Primary><Alt>Left" },
		{ "closeSplit", "Tabs and split view", "Close split view", "win.close-split", "<Primary><Alt>w" },
		{ "nextTab", "Tabs and split view", "Next tab", "win.next-tab", "<Primary>Page_Down" },
		{ "previousTab", "Tabs and split view", "Previous tab", "win.previous-tab", "<Primary>Page_Up" },
		{ "shortcuts", "App", "Every shortcut (this list)", "win.shortcuts", "<Primary>question|<Primary>slash" },
		{ "darkMode", "App", "Dark mode", "win.dark", "<Primary><Shift>d" },
		{ "preferences", "App", "Settings", "win.preferences", "<Primary>comma" },
		{ "help", "App", "CedarLogic Help", "win.help", "F1" },
		{ "feedback", "App", "Send feedback", "win.feedback", "" },
	};
	return list;
}

const Action* find(const std::string& id) {
	for (const Action& a : all()) if (id == a.id) return &a;
	return nullptr;
}

std::string keys(const Action& a) {
	const std::vector<std::string> k = keyList(a);
	return k.empty() ? std::string() : k.front();
}

bool isCustom(const Action& a) { return overrides().count(a.id) > 0; }
bool anyCustom() { return !overrides().empty(); }

std::string set(const Action& a, const std::string& accel) {
	std::string loser;
	if (!accel.empty()) {
		// Whoever had the key loses just that one, and keeps any others.
		for (const Action& o : all()) {
			if (&o == &a) continue;
			std::vector<std::string> kept;
			bool had = false;
			for (const std::string& k : keyList(o)) {
				if (same(k, accel)) had = true;
				else kept.push_back(k);
			}
			if (!had) continue;
			overrides()[o.id] = join(kept);
			loser = o.id;
		}
	}
	// One of the action's own keys: it has all its keys back (bar any another
	// action has taken), the one pressed first.
	const std::vector<std::string> defaults = split(a.keys);
	bool own = false;
	for (const std::string& k : defaults) own = own || same(k, accel);
	if (own) {
		std::vector<std::string> keys = { accel };
		for (const std::string& k : defaults) {
			if (same(k, accel)) continue;
			bool taken = false;
			for (const Action& o : all())
				if (&o != &a)
					for (const std::string& ok : keyList(o)) taken = taken || same(ok, k);
			if (!taken) keys.push_back(k);
		}
		if (join(keys) == join(defaults)) overrides().erase(a.id);
		else overrides()[a.id] = join(keys);
	} else {
		overrides()[a.id] = accel;
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

void apply(GtkApplication* app) {
	for (const Action& a : all()) {
		if (a.gaction == nullptr) continue;
		const std::vector<std::string> k = keyList(a);
		std::vector<const char*> list;
		for (const std::string& s : k) list.push_back(s.c_str());
		list.push_back(nullptr);
		gtk_application_set_accels_for_action(app, a.gaction, list.data());
	}
}

std::string fromEvent(const GdkEventKey* e) {
	if (isModifier(e->keyval)) return "";
	const guint latin = latinKeyval(e);
	return accelName(latin ? latin : e->keyval, e->state);
}

std::string canvasAction(const GdkEventKey* e) {
	if (isModifier(e->keyval)) return "";
	// The key as a Latin layout has it, then as it is (a key recorded on
	// another script before).
	const std::string tries[] = { fromEvent(e), accelName(e->keyval, e->state) };
	for (const std::string& pressed : tries) {
		if (pressed.empty()) continue;
		for (const Action& a : all()) {
			if (a.gaction) continue;
			for (const std::string& k : keyList(a))
				if (same(k, pressed)) return a.id;
		}
	}
	return "";
}

std::string reserved(const GdkEventKey* e) {
	const guint k = e->keyval;
	const bool bare = (e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) == 0;
	const bool tab = k == GDK_KEY_Tab || k == GDK_KEY_ISO_Left_Tab || k == GDK_KEY_KP_Tab;
	// The window's, before any shortcut.
	if (tab && (e->state & GDK_CONTROL_MASK)) return "switching tabs";
	// The canvas's, before its single keys (and a menu's shortcut would take
	// them from it).
	if (!bare) return "";
	if (k == GDK_KEY_space) return "moving around and zooming to fit";
	if (k == GDK_KEY_Left || k == GDK_KEY_Right || k == GDK_KEY_Up || k == GDK_KEY_Down || k == GDK_KEY_KP_Left ||
	    k == GDK_KEY_KP_Right || k == GDK_KEY_KP_Up || k == GDK_KEY_KP_Down)
		return "nudging and moving around";
	if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter || tab) return "Tidy Up's preview";
	if (k == GDK_KEY_KP_Delete) return "deleting";
	if (k == GDK_KEY_question || (gdk_keyval_to_lower(k) == GDK_KEY_slash && (e->state & GDK_SHIFT_MASK))) return "the list of shortcuts";
	if (e->state & GDK_SHIFT_MASK) {
		guint plain = 0;
		GdkKeymap* keymap = gdk_keymap_get_for_display(e->window ? gdk_window_get_display(e->window) : gdk_display_get_default());
		if (keymap && gdk_keymap_translate_keyboard_state(keymap, e->hardware_keycode, (GdkModifierType)0, e->group, &plain, nullptr,
		                                                  nullptr, nullptr) &&
		    plain >= GDK_KEY_0 && plain <= GDK_KEY_9)
			return "the gate categories";
	}
	return "";
}

std::string label(const std::string& accel) {
	guint key = 0;
	GdkModifierType mods;
	gtk_accelerator_parse(accel.c_str(), &key, &mods);
	if (key == 0) return "";
	gchar* l = gtk_accelerator_get_label(key, mods);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

std::vector<std::string> caps(const std::string& accel) {
	std::vector<std::string> out;
	guint key = 0;
	GdkModifierType mods;
	gtk_accelerator_parse(accel.c_str(), &key, &mods);
	if (key == 0) return out;
	if (mods & GDK_CONTROL_MASK) out.push_back("Ctrl");
	if (mods & GDK_MOD1_MASK) out.push_back("Alt");
	if (mods & GDK_SHIFT_MASK) out.push_back("Shift");
	if (mods & GDK_SUPER_MASK) out.push_back("Super");
	gchar* l = gtk_accelerator_get_label(key, (GdkModifierType)0);
	std::string k = l ? l : "";
	g_free(l);
	if (k.size() == 1) k[0] = (char)toupper((unsigned char)k[0]);
	out.push_back(k);
	return out;
}

}  // namespace shortcuts
