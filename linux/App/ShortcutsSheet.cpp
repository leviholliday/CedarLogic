// Every shortcut (Ctrl+?), as the Mac app's ShortcutsSheet: sections in two
// columns, keys drawn as keycaps, a search across the top. Arrow to a row
// and press Enter (or click it) to do it. The keys shown are yours: change
// them in Settings > Shortcuts.

#include "Dialogs.h"
#include "Shortcuts.h"
#include "Sheet.h"
#include "Window.h"

#include <algorithm>
#include <functional>

namespace {

struct ShortRow {
	std::string section, what;
	std::vector<std::string> keys;
	const char* gaction = nullptr;   // a command to run, if any
	std::string canvasAction;        // or a canvas key's
};

std::vector<ShortRow> allRows() {
	std::vector<ShortRow> out;
	for (const shortcuts::Action& a : shortcuts::all()) {
		ShortRow r;
		r.section = a.section;
		r.what = a.name;
		const std::string k = shortcuts::keys(a);
		r.keys = k.empty() ? std::vector<std::string>{ "none" } : shortcuts::caps(k);
		r.gaction = a.gaction;
		if (!a.gaction) r.canvasAction = a.id;
		out.push_back(r);
		const std::string id = a.id;
		auto extra = [&](const char* section, std::vector<std::string> keys, const char* what) {
			out.push_back({ section, what, std::move(keys), nullptr, "" });
		};
		if (id == "selectAll") {
			extra("Editing", { "Delete" }, "Delete the selection");
			extra("Editing", { "Esc" }, "Cancel a drag, paste or connection");
			extra("Editing", { "Shift", "click" }, "Add to or remove from the selection");
		}
		if (id == "tidy") {
			extra("Building", { "Shift", "1-0" }, "Jump to a gate category");
			extra("Building", { "↑", "↓", "←", "→" }, "Nudge the selection (Shift: 5 squares)");
			extra("Building", { "click a pin, then another" }, "Connect them");
			extra("Building", { "C", "while dragging" }, "Drop it and connect to pins nearby");
			extra("Building", { "double-click a gate" }, "Change its settings");
		}
		if (id == "focusMode") {
			extra("Moving around", { "Space" }, "Zoom to fit (tap)");
			extra("Moving around", { "Space", "drag" }, "Move around");
			extra("Moving around", { "Ctrl", "scroll" }, "Zoom (or pinch)");
			extra("Moving around", { "Shift", "scroll" }, "Move sideways");
		}
		if (id == "lock") {
			extra("Simulation", { "Space" }, "Pause or resume (in Simulation View)");
			extra("Simulation", { "Esc" }, "Leave Simulation View");
		}
		if (id == "previousTab") {
			extra("Tabs and split view", { "double-click a tab" }, "Rename it");
			extra("Tabs and split view", { "drag a tab down" }, "Split the view");
			extra("Tabs and split view", { "Ctrl", "Tab" }, "Switch tabs (hold for pictures)");
		}
	}
	return out;
}

std::string lower(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

float capsWidth(const std::vector<std::string>& keys) {
	float w = 0;
	for (const std::string& k : keys) w += std::max(22.0f, textWidth(k, 11, true) + 14) + 4;
	return w;
}

void drawCaps(cairo_t* cr, const std::vector<std::string>& keys, float right, float cy, const Color& ink) {
	float x = right - capsWidth(keys);
	for (const std::string& k : keys) {
		const float kw = std::max(22.0f, textWidth(k, 11, true) + 14);
		// "drag", "click a pin" and "none" are said, not pressed; "Page Down" is a key.
		const bool word = !k.empty() && g_ascii_islower(k[0]) && k.size() > 1;
		const RectF r = rectF(x, cy - 11, x + kw, cy + 11);
		if (!word) {
			fillRound(cr, rectF(r.left, r.top + 1, r.right, r.bottom + 1), 5, withAlpha(ink, 0.10f));
			fillRound(cr, r, 5, withAlpha(ink, 0.06f));
			strokeRound(cr, r, 5, withAlpha(ink, 0.16f));
		}
		drawTextMid(cr, k, r, word ? 11 : 11, withAlpha(ink, word ? 0.55f : 0.85f), TextAlign::Center, !word);
		x += kw + 4;
	}
}

}  // namespace

void showShortcutsWindow(GtkWindow* parent) {
	CircuitWindow* owner = nullptr;
	for (CircuitWindow* w : circuitWindows()) if (w->window() == parent) owner = w;
	const std::vector<ShortRow> rows = allRows();
	std::string query;
	int selected = 0;
	float scroll = 0, contentH = 0;
	std::vector<int> shown;
	ShortRow runIt;
	bool run = false;
	auto refilter = [&] {
		shown.clear();
		const std::string q = lower(query);
		for (int i = 0; i < (int)rows.size(); i++) {
			const ShortRow& r = rows[i];
			std::string all = r.what + " " + r.section;
			for (const std::string& k : r.keys) all += " " + k;
			if (q.empty() || lower(all).find(q) != std::string::npos) shown.push_back(i);
		}
		selected = std::min(selected, std::max(0, (int)shown.size() - 1));
		scroll = 0;
	};
	refilter();
	Sheet s;
	s.title = "Keyboard Shortcuts";
	s.width = 880;
	s.height = 620;
	s.minWidth = 720;
	s.minHeight = 420;
	GtkWidget* entry = nullptr;
	s.paint = [&](Sheet& sh, cairo_t* cr, float w, float h) {
		const bool dark = prefs().dark;
		const Color paper = dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252);
		const Color ink = dark ? rgb255(226, 230, 238) : rgb255(30, 33, 40);
		const Color accent = chrome().accent();
		fillRect(cr, rectF(0, 0, w, h), paper);
		drawTextMid(cr, "Keyboard Shortcuts", rectF(22, 14, w - 300, 46), 19, ink, TextAlign::Leading, true);
		const RectF field = rectF(w - 22 - 260, 16, w - 22, 44);
		fillRound(cr, field, 8, withAlpha(ink, 0.06f));
		strokeRound(cr, field, 8, entry && gtk_widget_has_focus(entry) ? withAlpha(accent, 0.7f) : withAlpha(ink, 0.1f), entry && gtk_widget_has_focus(entry) ? 2 : 1);
		drawIcon(cr, "edit-find-symbolic", rectF(field.left + 6, field.top, field.left + 26, field.bottom), 13, withAlpha(ink, 0.45f));
		// Sections in two columns.
		std::vector<std::string> sections;
		for (int i : shown) if (std::find(sections.begin(), sections.end(), rows[i].section) == sections.end()) sections.push_back(rows[i].section);
		const size_t half = (sections.size() + 1) / 2;
		const float top = 62, colW = (w - 44 - 24) / 2;
		cairo_save(cr);
		cairo_rectangle(cr, 0, top, w, h - top);
		cairo_clip(cr);
		float tallest = 0;
		for (int col = 0; col < 2; col++) {
			float y = top - scroll;
			const float x0 = 22 + col * (colW + 24);
			for (size_t si = col == 0 ? 0 : half; si < (col == 0 ? half : sections.size()); si++) {
				gchar* up = g_utf8_strup(sections[si].c_str(), -1);
				drawTextMid(cr, up, rectF(x0, y, x0 + colW, y + 22), 10.5f, accent, TextAlign::Leading, true);
				g_free(up);
				y += 26;
				for (int k = 0; k < (int)shown.size(); k++) {
					const ShortRow& r = rows[shown[k]];
					if (r.section != sections[si]) continue;
					const RectF rr = rectF(x0 - 8, y, x0 + colW + 8, y + 30);
					// Rows scrolled up under the title can't be clicked there.
					const bool visible = rr.bottom > top && rr.top < h;
					const bool sel = k == selected;
					if (sel) fillRound(cr, rr, 8, withAlpha(accent, dark ? 0.24f : 0.14f));
					else if (visible && sh.hotNext()) fillRound(cr, rr, 8, withAlpha(ink, 0.05f));
					drawTextMid(cr, r.what, rectF(x0, y, x0 + colW - capsWidth(r.keys) - 10, y + 30), 12.5f, withAlpha(ink, r.gaction || !r.canvasAction.empty() ? 0.95f : 0.65f));
					drawCaps(cr, r.keys, x0 + colW, y + 15, ink);
					const int kk = k;
					if (visible)
						sh.hit(rr, [&, kk] {
							if (selected == kk) { runIt = rows[shown[kk]]; run = true; sh.close(); }
							else selected = kk;
						});
					y += 30;
				}
				y += 12;
			}
			tallest = std::max(tallest, y + scroll - top);
		}
		contentH = tallest;
		if (shown.empty()) drawTextMid(cr, "No shortcuts match that.", rectF(0, top + 30, w, top + 60), 13, withAlpha(ink, 0.55f), TextAlign::Center);
		cairo_restore(cr);
	};
	s.onScroll = [&](Sheet& sh, float dy) {
		const float view = (float)gtk_widget_get_allocated_height(sh.area) - 62;
		scroll = std::max(0.0f, std::min(scroll + dy, std::max(0.0f, contentH - view)));
	};
	s.onKey = [&](Sheet& sh, guint k, guint) -> bool {
		if (k == GDK_KEY_Escape) {
			if (!query.empty()) { gtk_entry_set_text(GTK_ENTRY(entry), ""); return true; }
			sh.close();
			return true;
		}
		if (k == GDK_KEY_Down) { selected = std::min((int)shown.size() - 1, selected + 1); return true; }
		if (k == GDK_KEY_Up) { selected = std::max(0, selected - 1); return true; }
		if ((k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) && selected >= 0 && selected < (int)shown.size()) {
			runIt = rows[shown[selected]];
			run = true;
			sh.close();
			return true;
		}
		return false;
	};
	// The search field over the drawn one.
	struct Ctx { std::string* query; std::function<void()> refilter; Sheet* sheet; };
	Ctx ctx{ &query, refilter, &s };
	s.onTick = nullptr;
	// Built once run() has made the window: added from a zero-delay idle.
	g_idle_add([](gpointer data) -> gboolean {
		auto* c = static_cast<Ctx*>(data);
		Sheet* sh = c->sheet;
		if (sh->overlay == nullptr) return G_SOURCE_CONTINUE;
		GtkWidget* e = gtk_entry_new();
		gtk_widget_set_name(e, "picker-search");
		gtk_entry_set_placeholder_text(GTK_ENTRY(e), "Search");
		gtk_widget_set_halign(e, GTK_ALIGN_END);
		gtk_widget_set_valign(e, GTK_ALIGN_START);
		gtk_widget_set_margin_end(e, 30);
		gtk_widget_set_margin_top(e, 18);
		gtk_widget_set_size_request(e, 226, 24);
		gtk_overlay_add_overlay(GTK_OVERLAY(sh->overlay), e);
		g_object_set_data(G_OBJECT(sh->window), "cl-entry", e);
		g_signal_connect(e, "changed", CL_CALLBACK(+[](GtkEditable* ed, gpointer data) {
			auto* c = static_cast<Ctx*>(data);
			*c->query = gtk_entry_get_text(GTK_ENTRY(ed));
			c->refilter();
			c->sheet->redraw();
		}), c);
		g_signal_connect(e, "key-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventKey* ev, gpointer data) -> gboolean {
			auto* c = static_cast<Ctx*>(data);
			const guint k = ev->keyval;
			if (k == GDK_KEY_Up || k == GDK_KEY_Down || k == GDK_KEY_Return || k == GDK_KEY_KP_Enter || k == GDK_KEY_Escape)
				return c->sheet->onKey && c->sheet->onKey(*c->sheet, k, ev->state) ? (c->sheet->redraw(), TRUE) : FALSE;
			return FALSE;
		}), c);
		gtk_widget_show(e);
		gtk_widget_grab_focus(e);
		return G_SOURCE_REMOVE;
	}, &ctx);
	s.paint = [&, inner = s.paint](Sheet& sh, cairo_t* cr, float w, float h) {
		if (!entry && sh.window) entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(sh.window), "cl-entry"));
		inner(sh, cr, w, h);
	};
	s.run(parent);
	if (!run || owner == nullptr) return;
	// Do it, once the sheet has gone.
	if (runIt.gaction) owner->runAction(runIt.gaction);
	else if (runIt.canvasAction == "addGate") owner->quickAdd();
	else if (runIt.canvasAction == "rotate") owner->rotate();
	else if (runIt.canvasAction == "straighten") owner->straighten();
	else if (runIt.canvasAction == "tidy") owner->tidy();
	else if (runIt.canvasAction == "truthTable") owner->makeTruthTable();
	else if (runIt.canvasAction == "checkCircuit") owner->checkCircuit();
	else if (runIt.canvasAction == "quickCopy") owner->copy();
	else if (runIt.canvasAction == "quickPaste") owner->paste();
	else if (runIt.canvasAction == "quickCut") owner->cut();
	else if (runIt.canvasAction == "quickDuplicate") owner->duplicate();
}
