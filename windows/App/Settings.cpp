// Settings (Ctrl+,), laid out as the Mac app's (SettingsView.swift) and the
// Linux app's: the pages as icons in a bar across the top -- General,
// Appearance, Canvas, Toolbar, Shortcuts -- the window fitting each page as
// you switch, and each setting a label on the left with its control and a
// line of explanation under it. Every change applies at once; Escape or the
// close box closes it.

#include "Dialogs.h"
#include "Chrome.h"
#include "Collections.h"
#include "Shortcuts.h"
#include "Toolbar.h"
#include "Updater.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

enum Page { General, Appearance, CanvasPage, ToolbarPage, ShortcutsPage };

// Every window takes the change.
void apply() {
	prefs().applyWireDots();
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

// The keys an action has now ("Ctrl+N"), or `otherwise` when it has none.
std::string keyOf(const char* id, const std::string& otherwise) {
	const shortcuts::Action* a = shortcuts::find(id);
	const shortcuts::Key k = a ? shortcuts::first(*a) : shortcuts::Key();
	return k.valid() ? shortcuts::label(k) : otherwise;
}

FormField choice(const char* side, std::vector<std::string> items, int active, const std::string& tip = "") {
	FormField x;
	x.kind = FormField::Choice;
	x.label = side;
	x.choices = std::move(items);
	x.value = std::to_string(active);
	x.tip = tip;
	return x;
}

FormField check(const char* side, const char* text, bool on, const std::string& tip = "") {
	FormField x;
	x.kind = FormField::Check;
	x.side = side;
	x.label = text;
	x.value = on ? "1" : "";
	x.tip = tip;
	return x;
}

FormField slider(const char* side, double lo, double hi, double step, double value, const char* format, const std::string& tip) {
	FormField x;
	x.kind = FormField::Slider;
	x.side = side;
	x.lo = lo;
	x.hi = hi;
	x.step = step;
	x.value = strf("%.6g", value);
	x.format = format;
	x.tip = tip;
	return x;
}

FormField picture(const char* side, int height, std::function<void(ID2D1RenderTarget*, float, float)> paint, const std::string& tip = "") {
	FormField x;
	x.kind = FormField::Picture;
	x.side = side;
	x.height = height;
	x.paint = std::move(paint);
	x.tip = tip;
	return x;
}

FormField button(const char* label, bool beside) {
	FormField x;
	x.kind = FormField::Button;
	x.label = label;
	x.beside = beside;
	return x;
}

// ---- The app's colour: a row of swatches ----------------------------------------------

const int kAccentOrder[] = { 6, 0, 1, 2, 3, 4, 5 };   // the icon's green first, as the Mac offers them
const float kSwatchW = 60;

void paintSwatches(ID2D1RenderTarget* rt, float, float h) {
	static const char* names[] = { "Blue", "Purple", "Pink", "Orange", "Green", "Graphite", "CedarLogic" };
	const FormLook look = formLook();
	for (int i = 0; i < 7; i++) {
		const int a = kAccentOrder[i];
		double r = 0, g = 0, b = 0;
		cl_accent_color(a, prefs().dark, &r, &g, &b);
		const D2D1_POINT_2F mid = D2D1::Point2F(kSwatchW / 2 + i * kSwatchW, 17);
		const bool on = prefs().accent == a;
		if (on) strokeRound(rt, D2D1::RectF(mid.x - 15, mid.y - 15, mid.x + 15, mid.y + 15), 15, withAlpha(look.ink, 0.8f), 2);
		fillCircle(rt, mid, 11, D2D1::ColorF((float)r, (float)g, (float)b));
		if (on) fillCircle(rt, mid, 4, D2D1::ColorF(1, 1, 1, 0.95f));
		drawText(rt, names[a], D2D1::RectF(mid.x - kSwatchW / 2, h - 17, mid.x + kSwatchW / 2, h), 10.5f, withAlpha(look.ink, on ? 0.95f : 0.7f),
		         TextAlign::Center, on);
	}
}

// ---- A toolbar style, as a picture of it --------------------------------------------

struct Style { int id; const char* name; const char* blurb; };
const Style kStyles[] = {
	{ 3, "Seamless", "Blends into the canvas like one surface. Tools stay quiet until you point at them." },
	{ 0, "Classic", "Tools in tidy rounded groups, everything in reach." },
	{ 2, "Minimal", "Just the essentials and your circuit's name; the rest is behind the ••• menu." },
};
const float kStyleH = 88;

void paintStyles(HWND owner, ID2D1RenderTarget* rt, float w, float) {
	const FormLook look = formLook();
	const D2D1_COLOR_F accent = chrome().accent();
	Toolbar* bar = nullptr;
	for (CircuitWindow* cw : circuitWindows())
		if (cw->window() == owner || bar == nullptr) bar = cw->toolbarWidget();
	for (int i = 0; i < 3; i++) {
		const Style& st = kStyles[i];
		const float top = i * kStyleH;
		const bool on = prefs().toolbarStyle == st.id;
		const D2D1_POINT_2F dot = D2D1::Point2F(9, top + 11);
		strokeRound(rt, D2D1::RectF(dot.x - 7, dot.y - 7, dot.x + 7, dot.y + 7), 7, on ? accent : withAlpha(look.ink, 0.45f), on ? 2.0f : 1.2f);
		if (on) fillCircle(rt, dot, 3.5f, accent);
		drawText(rt, st.name, D2D1::RectF(24, top + 1, w, top + 21), 13, look.ink, TextAlign::Leading, true);
		drawText(rt, st.blurb, D2D1::RectF(24, top + 21, w, top + 39), 11.5f, look.dim);
		// The bar itself, drawn in that style a size smaller.
		const D2D1_RECT_F pr = D2D1::RectF(24, top + 42, w - 2, top + 80);
		const float k = (pr.bottom - pr.top) / Toolbar::barHeight();
		ID2D1Factory* factory = nullptr;
		rt->GetFactory(&factory);
		ID2D1RoundedRectangleGeometry* clip = nullptr;
		ID2D1Layer* layer = nullptr;
		if (factory && bar && SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(pr, 8, 8), &clip)) &&
		    SUCCEEDED(rt->CreateLayer(&layer))) {
			D2D1_MATRIX_3X2_F base;
			rt->GetTransform(&base);
			rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip), layer);
			rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(pr.left, pr.top) * base);
			bar->paintPicture(rt, (pr.right - pr.left) / k, Toolbar::barHeight(), st.id);
			rt->SetTransform(base);
			rt->PopLayer();
		} else {
			fillRound(rt, pr, 8, withAlpha(look.ink, 0.08f));
		}
		if (layer) layer->Release();
		if (clip) clip->Release();
		if (factory) factory->Release();
		strokeRound(rt, D2D1::RectF(pr.left + 0.5f, pr.top + 0.5f, pr.right - 0.5f, pr.bottom - 0.5f), 8,
		            on ? withAlpha(accent, 0.7f) : withAlpha(look.ink, 0.15f), on ? 1.5f : 1.0f);
	}
}

// ---- Shortcuts: every command, its keys, click to change ------------------------------

const char* const kShortcutsNote = "Click a shortcut, then press the new keys. Keys without Ctrl work while you're not typing in a box.";

struct ShortcutList {
	Form* form = nullptr;
	int search = -1, note = -1, list = -1;
	std::string query;
	float scroll = 0, contentH = 0, viewH = 0;
	int hot = -1;
	const shortcuts::Action* recording = nullptr;
	struct Row { const shortcuts::Action* action; D2D1_RECT_F rect, revert; };
	std::vector<Row> rows;   // as last drawn, for clicks

	HWND hwnd() const { return form->fields[list].hwnd; }
	void refresh() { form->refresh(list); }
	void say(const std::string& text) { form->setText(note, text); }
	void scrollBy(float dy) {
		scroll = std::max(0.0f, std::min(scroll + dy, std::max(0.0f, contentH - viewH)));
		refresh();
	}
	int rowAt(float x, float y) const {
		for (size_t i = 0; i < rows.size(); i++)
			if (inRect(rows[i].rect, x, y)) return (int)i;
		return -1;
	}
};

void paintShortcuts(ShortcutList& L, ID2D1RenderTarget* rt, float w, float h) {
	const FormLook look = formLook();
	const D2D1_COLOR_F accent = chrome().accent();
	L.rows.clear();
	L.viewH = h;
	const D2D1_RECT_F box = D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
	fillRound(rt, box, 8, look.field);
	rt->PushAxisAlignedClip(D2D1::RectF(1, 1, w - 1, h - 1), D2D1_ANTIALIAS_MODE_ALIASED);
	std::string q = lowerCase(L.query);
	while (!q.empty() && q.back() == ' ') q.pop_back();
	while (!q.empty() && q.front() == ' ') q.erase(q.begin());
	const float rowH = 28, right = w - 16;
	float y = 4 - L.scroll;
	std::string section;
	auto heading = [&](const std::string& title) {
		std::string up = title;
		for (char& c : up) c = (char)toupper((unsigned char)c);
		drawText(rt, up, D2D1::RectF(14, y + 8, w - 14, y + 24), 10.5f, accent, TextAlign::Leading, true);
		y += 28;
	};
	for (const shortcuts::Action& a : shortcuts::all()) {
		const shortcuts::Key k = shortcuts::first(a);
		const std::string all = lowerCase(std::string(a.name) + " " + a.section + " " + shortcuts::label(k));
		if (!q.empty() && all.find(q) == std::string::npos) continue;
		if (section != a.section) { section = a.section; heading(section); }
		const D2D1_RECT_F r = D2D1::RectF(6, y, w - 6, y + rowH);
		const bool rec = L.recording == &a, hot = (int)L.rows.size() == L.hot;
		if (rec) fillRound(rt, r, 7, withAlpha(accent, prefs().dark ? 0.24f : 0.14f));
		else if (hot) fillRound(rt, r, 7, withAlpha(look.ink, 0.06f));
		float capsLeft = right;
		if (rec) {
			const float tw = textWidth("Press keys…", 12, true);
			capsLeft = right - tw;
			drawText(rt, "Press keys…", D2D1::RectF(capsLeft, y + 5, right, y + rowH - 5), 12, accent, TextAlign::Trailing, true);
		} else if (!k.valid()) {
			capsLeft = right - textWidth("None", 12);
			drawText(rt, "None", D2D1::RectF(capsLeft, y + 5, right, y + rowH - 5), 12, look.dim, TextAlign::Trailing);
		} else {
			const std::vector<std::string> caps = shortcuts::caps(k);
			capsLeft = right - shortcuts::capsWidth(caps);
			shortcuts::drawCaps(rt, caps, right, y + rowH / 2, look.ink);
		}
		D2D1_RECT_F revert = D2D1::RectF(0, 0, 0, 0);
		if (shortcuts::isCustom(a) && !rec) {
			// Back to how it came.
			revert = D2D1::RectF(capsLeft - 30, y + 3, capsLeft - 6, y + rowH - 3);
			drawIcon(rt, Icon::Undo, revert, 12, withAlpha(look.ink, hot ? 0.75f : 0.45f));
		}
		drawText(rt, a.name, D2D1::RectF(14, y + 5, capsLeft - 36, y + rowH - 5), 12.5f, look.ink);
		L.rows.push_back({ &a, r, revert });
		y += rowH;
	}
	if (q.empty()) {
		heading("Fixed");
		struct Fixed { std::vector<std::string> caps; const char* what; };
		static const std::vector<Fixed> fixed = {
			{ { "Space" }, "Tap: zoom to fit. Hold and drag: move around. In Simulation View: pause" },
			{ { "Esc" }, "Cancel a drag, paste or connection; leave Simulation View" },
			{ { "Del" }, "Delete the selection" },
			{ { "←", "↑", "→", "↓" }, "Nudge the selection, or move around" },
			{ { "Shift", "1-0" }, "Jump to a gate category" },
			{ { "Ctrl", "Tab" }, "Switch tabs: tap for the last one, hold for a picture of each" },
			{ { "Alt" }, "Every menu (F10 too)" },
		};
		for (const Fixed& f : fixed) {
			const float capsLeft = right - shortcuts::capsWidth(f.caps);
			drawText(rt, f.what, D2D1::RectF(14, y + 5, capsLeft - 10, y + rowH - 5), 12.5f, look.dim);
			shortcuts::drawCaps(rt, f.caps, right, y + rowH / 2, look.ink);
			y += rowH;
		}
	}
	L.contentH = y + L.scroll + 6;
	if (L.rows.empty() && !q.empty())
		drawText(rt, "No shortcuts match that.", D2D1::RectF(0, 30, w, 50), 12.5f, look.dim, TextAlign::Center);
	rt->PopAxisAlignedClip();
	// A thin bar on the right, when there's more than fits.
	if (L.contentH > h + 1) {
		const float trackH = h - 12, thumbH = std::max(24.0f, trackH * h / L.contentH);
		const float at = 6 + (trackH - thumbH) * (L.scroll / std::max(1.0f, L.contentH - h));
		fillRound(rt, D2D1::RectF(w - 7, at, w - 4, at + thumbH), 1.5f, withAlpha(look.ink, 0.25f));
	}
	strokeRound(rt, box, 8, look.line);
}

// Every window's menus and toolbar show the new keys.
void shortcutsChanged() {
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

void record(ShortcutList& L, UINT vk) {
	const shortcuts::Action* a = L.recording;
	if (a == nullptr) return;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	auto done = [&] {
		L.recording = nullptr;
		shortcutsChanged();
		L.refresh();
	};
	if (vk == VK_ESCAPE && !ctrl && !shift) {
		L.recording = nullptr;
		L.say(kShortcutsNote);
		L.refresh();
		return;
	}
	if ((vk == VK_BACK || vk == VK_DELETE) && !ctrl && !shift) {
		shortcuts::set(*a, shortcuts::Key());
		L.say(std::string("“") + a->name + "” has no shortcut now.");
		done();
		return;
	}
	if (GetKeyState(VK_MENU) & 0x8000) {
		if (vk != VK_MENU && vk != VK_LMENU && vk != VK_RMENU)
			L.say("Alt is kept for the menus, as everywhere in Windows. Press other keys, or Escape.");
		return;
	}
	const shortcuts::Key k = shortcuts::pressed(vk);
	if (!k.valid()) return;   // Ctrl or Shift on its own: keep waiting
	// The fixed keys would never reach it.
	const std::string kept = shortcuts::reserved(k);
	if (!kept.empty()) {
		L.say(shortcuts::label(k) + " is kept for " + kept + ". Press other keys, or Escape.");
		return;
	}
	const std::string loser = shortcuts::set(*a, k);
	const shortcuts::Action* l = loser.empty() ? nullptr : shortcuts::find(loser);
	if (l) {
		const shortcuts::Key left = shortcuts::first(*l);
		L.say(shortcuts::label(k) + " was “" + l->name + "”; " +
		      (left.valid() ? "that one keeps " + shortcuts::label(left) + "." : std::string("that one has no shortcut now.")));
	} else {
		L.say(kShortcutsNote);
	}
	done();
}

LRESULT CALLBACK shortcutListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	ShortcutList* L = reinterpret_cast<ShortcutList*>(data);
	const float s = dpiOf(h) / 96.0f;
	const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
	switch (msg) {
	case WM_GETDLGCODE:
		// While recording, every key is the new shortcut's (Escape and Tab too).
		if (L->recording) return DLGC_WANTALLKEYS;
		break;
	case WM_MOUSEMOVE: {
		TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
		TrackMouseEvent(&t);
		const int row = L->rowAt(x, y);
		if (row != L->hot) { L->hot = row; L->refresh(); }
		return 0;
	}
	case WM_MOUSELEAVE:
		if (L->hot != -1) { L->hot = -1; L->refresh(); }
		return 0;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const int row = L->rowAt(x, y);
		if (row < 0) return 0;
		const ShortcutList::Row r = L->rows[row];
		if (inRect(r.revert, x, y)) {
			shortcuts::reset(*r.action);
			const shortcuts::Key k = shortcuts::first(*r.action);
			L->say(std::string("“") + r.action->name + "” is back to " + (k.valid() ? shortcuts::label(k) : std::string("no shortcut")) + ".");
			L->recording = nullptr;
			shortcutsChanged();
			L->refresh();
			return 0;
		}
		L->recording = L->recording == r.action ? nullptr : r.action;
		L->say(L->recording ? "Press the new keys. Escape keeps what was there; Backspace removes it." : kShortcutsNote);
		SetFocus(h);
		L->refresh();
		return 0;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (L->recording) {
			guarded("a shortcut", [&] { record(*L, (UINT)wp); });
			return 0;
		}
		break;
	case WM_CHAR:
	case WM_SYSCHAR:
	case WM_SYSKEYUP:
		if (L->recording || msg != WM_SYSKEYUP) return 0;   // no beeps, and no menu from Alt
		break;
	case WM_KILLFOCUS:
		if (L->recording) {
			L->recording = nullptr;
			L->say(kShortcutsNote);
			L->refresh();
		}
		break;
	}
	return DefSubclassProc(h, msg, wp, lp);
}

int g_page = 0;   // the page it opens on (--page, for CI), and the one last shown

}  // namespace

void setPreferencesPage(int page) { g_page = page; }

void showPreferencesDialog(HWND parent) {
	Prefs& p = prefs();
	Form f;
	f.title = "Settings";
	f.width = 620;
	f.okText = "";
	f.cancelText = "";
	f.settingsLayout = true;
	f.pages = { "General", "Appearance", "Canvas", "Toolbar", "Shortcuts" };
	// A gear, a palette, a mouse, ••• (as the Linux app has it), a keyboard.
	f.pageIcons = { 0xE713, 0xE790, 0xE962, Icon::More, 0xE765 };
	f.page = std::max(0, std::min((int)ShortcutsPage, g_page));
	// What each field does when it changes.
	std::map<int, std::function<void(Form&, int)>> on;

	// ---- General
	f.adding = General;
	FormField name;
	name.kind = FormField::Text;
	name.label = "Your name:";
	name.value = p.studentName;
	name.widthPt = 260;
	name.placeholder = "First and last name";
	name.tip = "Printed under your circuit when you export it as an image, and on the Lab Page template.";
	on[f.add(name)] = [](Form& form, int i) { prefs().studentName = form.text(i); prefs().save(); };

	auto openTip = [] {
		return std::string(prefs().openReplaces
			? "New and opened circuits take the place of the one you're in, like classic CedarLogic. It's saved first, as everything is."
			: "New and opened circuits get a window of their own, so you can have several open side by side.");
	};
	FormField opening = choice("Opening a circuit:", { "Replace the current one", "Open in a new window" }, p.openReplaces ? 0 : 1, openTip());
	opening.tipLines = 2;
	on[f.add(opening)] = [openTip](Form& form, int i) {
		prefs().openReplaces = form.choice(i) == 0;
		prefs().save();
		form.setTip(i, openTip());
	};

	std::vector<std::string> templateNames = { "A blank page" }, templateIds = { "" };
	for (const auto& t : templates::list()) { templateIds.push_back(t.first); templateNames.push_back(t.second); }
	int templateAt = 0;
	for (size_t i = 0; i < templateIds.size(); i++) if (templateIds[i] == p.newTemplate) templateAt = (int)i;
	auto newTip = [] {
		const std::string k = keyOf("newCircuit", "New Circuit");
		return prefs().newTemplate.empty() ? k + " starts a blank page. Pick a template to start every new circuit from it instead."
		                                   : k + " starts from this template. (New from Template… in the File menu still offers them all.)";
	};
	FormField newOnes = choice("New circuits:", templateNames, templateAt, newTip());
	newOnes.tipLines = 2;
	on[f.add(newOnes)] = [templateIds, newTip](Form& form, int i) {
		const int at = form.choice(i);
		if (at >= 0 && at < (int)templateIds.size()) prefs().newTemplate = templateIds[at];
		prefs().save();
		form.setTip(i, newTip());
	};
	on[f.add(check("Quitting:", "Ask before quitting", p.confirmQuit,
	               keyOf("quit", "Quitting") + " shows a question first: Enter quits, Escape doesn't."))] = [](Form& form, int i) {
		prefs().confirmQuit = form.checked(i);
		prefs().save();
	};
	on[f.add(check("Status bar:", "Show zoom, cursor position, and counts", p.showStatus, "The readout along the bottom of the window."))] =
		[](Form& form, int i) { prefs().showStatus = form.checked(i); apply(); };
	on[f.add(check("Updates:", "Check for new versions", p.checkUpdates,
	               "New test builds install from inside the app: it asks first, and keeps your circuits."))] = [](Form& form, int i) {
		prefs().checkUpdates = form.checked(i);
		prefs().save();
	};
	on[f.add(button("Check Now", true))] = [](Form& form, int) { updater::checkNow(form.dialog); };

	// ---- Appearance
	f.adding = Appearance;
	on[f.add(choice("Theme at launch:", { "Match Windows", "Light", "Dark", "Same as Last Time" }, p.themeMode,
	                "Which theme the app opens in. The View menu and " + keyOf("darkMode", "the dark mode switch") + " switch it any time."))] =
		[](Form& form, int i) {
			Prefs& q = prefs();
			q.themeMode = form.choice(i);
			if (q.themeMode == 1) q.dark = false;
			else if (q.themeMode == 2) q.dark = true;
			else if (q.themeMode == 0) q.dark = systemPrefersDark();
			q.save();
			applyTheme();
			form.retheme();
		};
	const int swatches = f.add(picture("App colour:", 52, paintSwatches,
	                                   "Selections, highlights and buttons. CedarLogic is the icon's green. Wire colours that show signal state never change."));
	const int showGrid = f.add(check("Canvas:", "Show the grid", p.showGrid, "The background grid gates snap to. Printing never includes it."));
	const int gridStyle = f.add(choice("Grid style:", { "Lines", "Dots" }, p.gridStyle, "Dots are quieter; lines make alignment easier to see."));
	const int major = f.add(check("", "Darker line every 5 squares", p.majorGrid, "Makes distances easy to judge at a glance."));
	on[showGrid] = [gridStyle, major](Form& form, int i) {
		prefs().showGrid = form.checked(i);
		form.enable(gridStyle, prefs().showGrid);
		form.enable(major, prefs().showGrid);
		apply();
	};
	on[gridStyle] = [](Form& form, int i) { prefs().gridStyle = form.choice(i); apply(); };
	on[major] = [](Form& form, int i) { prefs().majorGrid = form.checked(i); apply(); };
	on[f.add(choice("Wire thickness:", { "Thin", "Normal", "Thick" }, p.wireThickness, "On screen only. Printouts always use the standard weight."))] =
		[](Form& form, int i) { prefs().wireThickness = form.choice(i); apply(); };
	on[f.add(choice("Low wires:", { "Silver", "Slate blue", "Soft white", "Classic grey" }, p.lowWire,
	                "The colour of wires carrying a 0 on the dark background, so they stand apart from the grid. Light mode keeps black."))] =
		[](Form& form, int i) { prefs().lowWire = form.choice(i); apply(); };
	on[f.add(check("", "Show dots at wire bends", p.wireDots, "Marks every corner of a wire. Junctions where wires join always get a dot."))] =
		[](Form& form, int i) { prefs().wireDots = form.checked(i); apply(); };
	on[f.add(slider("Wire dot size:", 0.08, 0.4, 0.01, p.wireDotSize, "%.2f", "Radius of the dots on wires, in grid units."))] =
		[](Form& form, int i) { prefs().wireDotSize = form.number(i); apply(); };
	on[f.add(slider("Gate size:", 36, 96, 1, p.gateSize, "%.0f", "How big the gates in the side panel are."))] =
		[](Form& form, int i) { prefs().gateSize = (int)std::lround(form.number(i)); apply(); };
	on[f.add(check("Side panel:", "Show gate names", p.showGateNames))] = [](Form& form, int i) { prefs().showGateNames = form.checked(i); apply(); };
	on[f.add(check("", "Show category shortcuts", p.showCategoryKeys,
	               "The names under the gates, and Shift+1…Shift+0 beside the categories that jump to them."))] =
		[](Form& form, int i) { prefs().showCategoryKeys = form.checked(i); apply(); };

	// ---- Canvas
	f.adding = CanvasPage;
	on[f.add(choice("Mouse wheel:", { "Zooms", "Moves around" }, p.mouseWheel))] = [](Form& form, int i) { prefs().mouseWheel = form.choice(i); apply(); };
	on[f.add(check("", "Reverse zoom direction", p.reverseWheel,
	               "Rolling the wheel away from you zooms in. Turn this on if it zooms out for you instead (some mice and settings reverse the wheel)."))] =
		[](Form& form, int i) { prefs().reverseWheel = form.checked(i); apply(); };
	on[f.add(choice("Touchpad scroll:", { "Zooms", "Moves around" }, p.touchpadScroll, "Pinching always zooms."))] =
		[](Form& form, int i) { prefs().touchpadScroll = form.choice(i); apply(); };
	on[f.add(check("", "Reverse zoom direction", p.reverseTouchpad,
	               "Only matters when touchpad scrolling zooms. Ctrl+scroll always zooms; Shift+scroll always moves sideways."))] =
		[](Form& form, int i) { prefs().reverseTouchpad = form.checked(i); apply(); };
	on[f.add(check("Right-click:", "Rotates the gate", p.rightClickRotate, "Off: right-clicking a gate opens a menu with Rotate, Delete and more."))] =
		[](Form& form, int i) { prefs().rightClickRotate = form.checked(i); apply(); };
	on[f.add(choice("Duplicate:", { "Leaves the clipboard alone", "Copies to the clipboard too" }, p.duplicateUsesClipboard ? 1 : 0,
	                "Second option: the copy stays on the clipboard, so paste makes more of it."))] =
		[](Form& form, int i) { prefs().duplicateUsesClipboard = form.choice(i) == 1; apply(); };
	on[f.add(choice("Tidy Up:", { "Keeps my layout", "Rearranges everything" }, p.tidyMode,
	                "Keeps my layout: lines gates up where they are. Rearranges everything: lays the circuit out by signal flow. "
	                "The Edit menu has both."))] = [](Form& form, int i) { prefs().tidyMode = form.choice(i); apply(); };

	// ---- Toolbar
	f.adding = ToolbarPage;
	const int styles = f.add(picture("Style:", (int)(3 * kStyleH) - 6, [parent](ID2D1RenderTarget* rt, float w, float h) { paintStyles(parent, rt, w, h); }));
	static const int groups[] = { TGFile, TGUndo, TGClipboard, TGZoom, TGSim, TGRun, TGLock, TGTab, TGFeedback };
	bool firstGroup = true;
	for (int g : groups) {
		FormField c = check(firstGroup ? "Show:" : "", toolGroupName(g), (p.toolbarHidden & (1 << g)) == 0);
		c.grid = 2;
		firstGroup = false;
		on[f.add(c)] = [g](Form& form, int i) {
			if (form.checked(i)) prefs().toolbarHidden &= ~(1 << g);
			else prefs().toolbarHidden |= 1 << g;
			apply();
		};
	}
	FormField title = check("", "Circuit name", p.showTitle);
	title.grid = 2;
	on[f.add(title)] = [](Form& form, int i) { prefs().showTitle = form.checked(i); apply(); };
	FormField themeSwitch = check("", "Dark mode switch", p.showThemeToggle,
	                              "Applies to every style. Hidden tools are still in the ••• menu and keep their shortcuts.");
	themeSwitch.grid = 2;
	on[f.add(themeSwitch)] = [](Form& form, int i) { prefs().showThemeToggle = form.checked(i); apply(); };

	// ---- Shortcuts
	f.adding = ShortcutsPage;
	ShortcutList list;
	list.form = &f;
	FormField search;
	search.kind = FormField::Text;
	search.placeholder = "Search shortcuts";
	list.search = f.add(search);
	on[list.search] = [&list](Form& form, int i) {
		list.query = form.text(i);
		list.scroll = 0;
		list.hot = -1;
		list.refresh();
	};
	on[f.add(button("Restore Defaults", true))] = [&list](Form&, int) {
		list.recording = nullptr;
		list.say(shortcuts::anyCustom() ? "Every shortcut is back to how it came." : "Every shortcut is already as it came.");
		shortcuts::resetAll();
		shortcutsChanged();
		list.refresh();
	};
	FormField note;
	note.kind = FormField::Note;
	note.label = kShortcutsNote;
	note.lines = 2;
	list.note = f.add(note);
	list.list = f.add(picture("", 360, [&list](ID2D1RenderTarget* rt, float w, float h) { paintShortcuts(list, rt, w, h); }));

	f.onInit = [&](Form& form) {
		form.enable(gridStyle, prefs().showGrid);
		form.enable(major, prefs().showGrid);
		SetPropW(form.fields[list.note].hwnd, L"clTip", (HANDLE)1);
		SetWindowSubclass(list.hwnd(), shortcutListProc, 1, (DWORD_PTR)&list);
	};
	f.onChange = [&](Form& form, int field) {
		auto it = on.find(field);
		if (it != on.end()) it->second(form, field);
	};
	f.onClick = [&](Form& form, int field, float x, float y) {
		if (field == swatches) {
			const int i = (int)(x / kSwatchW);
			if (i < 0 || i > 6) return;
			prefs().accent = kAccentOrder[i];
			apply();
			form.retheme();
		} else if (field == styles) {
			const int i = (int)(y / kStyleH);
			if (i < 0 || i > 2) return;
			prefs().toolbarStyle = kStyles[i].id;
			apply();
			form.refresh(styles);
		}
	};
	f.onWheel = [&](Form&, int field, int delta) {
		if (field == list.list) list.scrollBy(-delta * 84.0f / WHEEL_DELTA);
	};
	f.run(parent);
	g_page = f.page;
	if (g_page < 0 || g_page > ShortcutsPage) g_page = General;
}
