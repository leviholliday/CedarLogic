// The app's dialogs and extra windows (see Dialogs.h).

#include "Dialogs.h"
#include "Alert.h"
#include "Anim.h"
#include "Collections.h"
#include "Formula.h"
#include "Picker.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// ---- A line of text ----------------------------------------------------------------

bool askText(GtkWindow* parent, const std::string& title, const std::string& prompt, std::string& value) {
	Alert a;
	a.title = title;
	a.heading = title;
	a.text = prompt;
	a.field = true;
	a.value = value;
	const bool naming = title.find("Rename") != std::string::npos || title.find("Save") != std::string::npos;
	a.buttons = { { naming ? (title.find("Rename") != std::string::npos ? "Rename" : "Save") : "OK", 1, 1 }, { "Cancel", 0, 0 } };
	if (runAlert(parent, a) != 1) return false;
	value = a.value;
	return true;
}

// ---- A gate's settings ------------------------------------------------------------
// The Mac's GateSettingsSheet: the part's picture and name, its settings in
// a card -- each its name on the left, the control its type calls for on the
// right, the allowed range or a problem under it -- applied as they change
// (one undo step each), and Rotate, Delete and Done along the bottom.

namespace {

std::string numberText(double v) {
	if (v == std::floor(v) && std::fabs(v) < 1e15) return format("%.0f", v);
	return format("%g", v);
}

struct SettingRow {
	CircuitWindow* w;
	long gate;
	std::string name, type, value;
	double min, max;
	GtkWidget* control = nullptr;
	GtkWidget* note = nullptr;
	std::string hint;
	GtkWidget* dialog = nullptr;
};

// Numbers are checked against the library's range, as the wx dialog does.
bool commitRow(SettingRow* r, bool quietly = false) {
	std::string v;
	if (r->type == "BOOL") {
		v = gtk_switch_get_active(GTK_SWITCH(r->control)) ? "true" : "false";
	} else if (r->type == "FILE_IN" || r->type == "FILE_OUT") {
		v = r->value;
	} else {
		gchar* t = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(r->control))));
		v = t;
		g_free(t);
	}
	if (r->type == "INT" || r->type == "FLOAT") {
		char* end = nullptr;
		const double x = strtod(v.c_str(), &end);
		std::string bad;
		if (v.empty() || end == nullptr || *end != 0 || !std::isfinite(x) || (r->type == "INT" && x != std::floor(x)))
			bad = r->type == "INT" ? "Enter a whole number." : "Enter a number.";
		else if (x < r->min || x > r->max)
			bad = "Must be between " + numberText(r->min) + " and " + numberText(r->max) + ".";
		GtkStyleContext* sc = gtk_widget_get_style_context(r->control);
		if (!bad.empty()) {
			if (!quietly) {
				gtk_label_set_text(GTK_LABEL(r->note), bad.c_str());
				gtk_style_context_add_class(gtk_widget_get_style_context(r->note), "problem");
				gtk_style_context_add_class(sc, "problem");
			}
			return false;
		}
		gtk_label_set_text(GTK_LABEL(r->note), r->hint.c_str());
		gtk_style_context_remove_class(gtk_widget_get_style_context(r->note), "problem");
		gtk_style_context_remove_class(sc, "problem");
	}
	if (v != r->value) {
		cl_gate_set_setting(r->w->document(), r->gate, r->name.c_str(), v.c_str());
		r->value = v;
		r->w->edited();
	}
	return true;
}

void chooseFileCb(GtkButton* b, gpointer data) {
	SettingRow* r = static_cast<SettingRow*>(data);
	GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(b)));
	const bool in = r->type == "FILE_IN";
	GtkFileChooserNative* c = gtk_file_chooser_native_new(in ? "Choose a File" : "Save to File", parent,
		in ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE, in ? "_Open" : "_Save", "_Cancel");
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		if (gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) {
			r->value.clear();
			cl_gate_set_setting(r->w->document(), r->gate, r->name.c_str(), file);
			r->value = file;
			gchar* base = g_path_get_basename(file);
			gtk_label_set_text(GTK_LABEL(r->note), base);
			g_free(base);
			g_free(file);
			r->w->edited();
		}
	}
	g_object_unref(c);
}

}  // namespace

void showGateSettings(CircuitWindow* w, long gate) {
	CLDocument* doc = w->document();
	const std::string caption = cl_gate_caption(doc, gate);
	const std::string libName = cl_gate_library_name(doc, gate) ? cl_gate_library_name(doc, gate) : "";
	GtkWidget* d = gtk_dialog_new();
	gtk_widget_set_name(d, "gate-settings");
	gtk_window_set_title(GTK_WINDOW(d), caption.c_str());
	gtk_window_set_transient_for(GTK_WINDOW(d), w->window());
	gtk_window_set_modal(GTK_WINDOW(d), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
	gtk_widget_set_size_request(d, 420, -1);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 0);
	gtk_box_set_spacing(GTK_BOX(box), 0);

	// The part, drawn, and its name.
	GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
	gtk_container_set_border_width(GTK_CONTAINER(head), 20);
	GtkWidget* pic = gtk_drawing_area_new();
	gtk_widget_set_size_request(pic, 56, 48);
	std::string* nameKeep = new std::string(libName);
	g_signal_connect_data(pic, "draw", CL_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer data) -> gboolean {
		const std::string* n = static_cast<std::string*>(data);
		const Chrome c = chrome();
		const float W = gtk_widget_get_allocated_width(a), H = gtk_widget_get_allocated_height(a);
		fillRound(cr, rectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), 10, c.dark ? rgb255(36, 40, 47) : colorF(1, 1, 1));
		strokeRound(cr, rectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), 10, c.hairline());
		cairo_save(cr);
		cairo_translate(cr, 6, 6);
		cl_library_draw_gate(n->c_str(), cr, W - 12, H - 12, std::max(1, gtk_widget_get_scale_factor(a)), c.dark);
		cairo_restore(cr);
		return TRUE;
	}), nameKeep, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);
	gtk_box_pack_start(GTK_BOX(head), pic, FALSE, FALSE, 0);
	GtkWidget* titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_valign(titles, GTK_ALIGN_CENTER);
	GtkWidget* t = gtk_label_new(caption.c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(t), "sheet-title");
	gtk_label_set_xalign(GTK_LABEL(t), 0);
	gtk_label_set_line_wrap(GTK_LABEL(t), TRUE);
	GtkWidget* sub = gtk_label_new("Changes apply as you make them · Ctrl+Z undoes");
	gtk_style_context_add_class(gtk_widget_get_style_context(sub), "hint");
	gtk_label_set_xalign(GTK_LABEL(sub), 0);
	gtk_box_pack_start(GTK_BOX(titles), t, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(titles), sub, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head), titles, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);

	// The settings, in a card.
	GtkWidget* cardBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(cardBox, "settings-card");
	gtk_widget_set_margin_start(cardBox, 20);
	gtk_widget_set_margin_end(cardBox, 20);
	std::vector<std::unique_ptr<SettingRow>> rows;
	const int n = cl_gate_setting_count(doc, gate);
	for (int i = 0; i < n; i++) {
		CLGateSetting st;
		if (!cl_gate_setting(doc, gate, i, &st)) continue;
		std::unique_ptr<SettingRow> r(new SettingRow{ w, gate, st.name, st.type, st.value ? st.value : "", st.min, st.max });
		r->dialog = d;
		if (i > 0) {
			GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_widget_set_margin_start(sep, 14);
			gtk_box_pack_start(GTK_BOX(cardBox), sep, FALSE, FALSE, 0);
		}
		GtkWidget* rowBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_container_set_border_width(GTK_CONTAINER(rowBox), 10);
		gtk_widget_set_margin_start(rowBox, 4);
		gtk_widget_set_margin_end(rowBox, 4);
		GtkWidget* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		GtkWidget* label = gtk_label_new(st.label ? st.label : st.name);
		gtk_label_set_xalign(GTK_LABEL(label), 0);
		gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
		gtk_box_pack_start(GTK_BOX(line), label, TRUE, TRUE, 0);
		const bool number = r->type == "INT" || r->type == "FLOAT";
		r->note = gtk_label_new("");
		gtk_style_context_add_class(gtk_widget_get_style_context(r->note), "hint");
		gtk_label_set_xalign(GTK_LABEL(r->note), 0);
		SettingRow* rp = r.get();
		if (r->type == "BOOL") {
			r->control = gtk_switch_new();
			gtk_switch_set_active(GTK_SWITCH(r->control), r->value == "true");
			gtk_widget_set_valign(r->control, GTK_ALIGN_CENTER);
			g_signal_connect(r->control, "notify::active", CL_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) { commitRow(static_cast<SettingRow*>(data)); }), rp);
			gtk_box_pack_end(GTK_BOX(line), r->control, FALSE, FALSE, 0);
		} else if (r->type == "FILE_IN" || r->type == "FILE_OUT") {
			GtkWidget* choose = gtk_button_new_with_label("Choose…");
			g_signal_connect(choose, "clicked", G_CALLBACK(chooseFileCb), rp);
			gtk_box_pack_end(GTK_BOX(line), choose, FALSE, FALSE, 0);
			gchar* base = r->value.empty() ? g_strdup("None") : g_path_get_basename(r->value.c_str());
			r->hint = base;
			g_free(base);
		} else {
			r->control = gtk_entry_new();
			gtk_entry_set_text(GTK_ENTRY(r->control), r->value.c_str());
			gtk_entry_set_width_chars(GTK_ENTRY(r->control), number ? 8 : 18);
			if (number) gtk_entry_set_alignment(GTK_ENTRY(r->control), 1);
			gtk_entry_set_activates_default(GTK_ENTRY(r->control), TRUE);
			g_signal_connect(r->control, "focus-out-event", CL_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
				commitRow(static_cast<SettingRow*>(data));
				return FALSE;
			}), rp);
			if (r->type == "INT" && r->max < 1e30) {
				GtkWidget* steps = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
				gtk_style_context_add_class(gtk_widget_get_style_context(steps), "linked");
				for (int dir : { -1, 1 }) {
					GtkWidget* b = gtk_button_new_from_icon_name(dir < 0 ? "list-remove-symbolic" : "list-add-symbolic", GTK_ICON_SIZE_MENU);
					g_object_set_data(G_OBJECT(b), "dir", GINT_TO_POINTER(dir));
					g_signal_connect(b, "clicked", CL_CALLBACK(+[](GtkButton* btn, gpointer data) {
						SettingRow* r = static_cast<SettingRow*>(data);
						const int d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "dir"));
						const double x = std::min(r->max, std::max(r->min, std::floor(strtod(gtk_entry_get_text(GTK_ENTRY(r->control)), nullptr)) + d));
						gtk_entry_set_text(GTK_ENTRY(r->control), numberText(x).c_str());
						commitRow(r);
					}), rp);
					gtk_box_pack_start(GTK_BOX(steps), b, FALSE, FALSE, 0);
				}
				gtk_box_pack_end(GTK_BOX(line), steps, FALSE, FALSE, 0);
			}
			gtk_box_pack_end(GTK_BOX(line), r->control, FALSE, FALSE, 0);
			if (number && r->max < 1e30) r->hint = numberText(r->min) + " to " + numberText(r->max);
		}
		gtk_label_set_text(GTK_LABEL(r->note), r->hint.c_str());
		gtk_box_pack_start(GTK_BOX(rowBox), line, FALSE, FALSE, 0);
		if (!r->hint.empty()) gtk_box_pack_start(GTK_BOX(rowBox), r->note, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(cardBox), rowBox, FALSE, FALSE, 0);
		rows.push_back(std::move(r));
	}
	if (rows.empty()) {
		GtkWidget* l = gtk_label_new("This part has no settings.");
		gtk_style_context_add_class(gtk_widget_get_style_context(l), "hint");
		gtk_label_set_xalign(GTK_LABEL(l), 0);
		gtk_container_set_border_width(GTK_CONTAINER(cardBox), 12);
		gtk_box_pack_start(GTK_BOX(cardBox), l, FALSE, FALSE, 0);
	}
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
	gtk_container_add(GTK_CONTAINER(scroll), cardBox);
	gtk_box_pack_start(GTK_BOX(box), scroll, FALSE, FALSE, 0);

	// Rotate and Delete; Done.
	GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start(foot, 20);
	gtk_widget_set_margin_end(foot, 20);
	gtk_widget_set_margin_top(foot, 16);
	gtk_widget_set_margin_bottom(foot, 18);
	GtkWidget* rotate = gtk_button_new_with_label("Rotate");
	gtk_widget_set_tooltip_text(rotate, "Turn a quarter turn (R). Parts with wires attached stay put.");
	GtkWidget* del = gtk_button_new_with_label("Delete");
	gtk_style_context_add_class(gtk_widget_get_style_context(del), "destructive-action");
	GtkWidget* done = gtk_button_new_with_label("Done  ↩");
	gtk_style_context_add_class(gtk_widget_get_style_context(done), "suggested-action");
	gtk_widget_set_can_default(done, TRUE);
	g_signal_connect_swapped(rotate, "clicked", CL_CALLBACK(+[](CircuitWindow* cw) { cw->rotate(); }), w);
	g_signal_connect(del, "clicked", CL_CALLBACK(+[](GtkButton*, gpointer dlg) { gtk_dialog_response(GTK_DIALOG(dlg), 2); }), d);
	g_signal_connect(done, "clicked", CL_CALLBACK(+[](GtkButton*, gpointer dlg) { gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK); }), d);
	gtk_box_pack_start(GTK_BOX(foot), rotate, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(foot), del, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(foot), done, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), foot, FALSE, FALSE, 0);
	gtk_window_set_default(GTK_WINDOW(d), done);
	gtk_widget_show_all(d);
	anim::fadeIn(d);
	for (auto& r : rows) if (r->hint.empty()) gtk_widget_hide(r->note);
	const int answer = gtk_dialog_run(GTK_DIALOG(d));
	// Return keeps a value and closes, like Done; a value that isn't valid
	// is left as it was.
	for (auto& r : rows) if (r->control && r->type != "BOOL") commitRow(r.get(), true);
	gtk_widget_destroy(d);
	if (answer == 2) w->deleteSelection();
}

// ---- Add a Gate (A) -----------------------------------------------------------
// The Mac's QuickAddView: type part of a name, arrow to the one you want,
// Return. Each result is drawn with the gate's own picture; the gate then
// follows the pointer until a click drops it. Escape closes.

namespace {

// QuickAddDialog::fuzzyScore: a substring beats letters in order; a match at
// the start beats one in the middle; -1 for no match.
int fuzzyScore(const std::string& query, const std::string& target) {
	gchar* ql = g_utf8_strdown(query.c_str(), -1);
	gchar* tl = g_utf8_strdown(target.c_str(), -1);
	const std::string q = ql ? ql : "", t = tl ? tl : "";
	g_free(ql);
	g_free(tl);
	if (q.empty()) return 0;
	const size_t at = t.find(q);
	if (at != std::string::npos) return at == 0 ? 100 : 80;
	size_t qi = 0;
	int score = 0, last = -2;
	for (size_t ti = 0; ti < t.size() && qi < q.size(); ti++) {
		if (t[ti] != q[qi]) continue;
		score += 10;
		if (last == (int)ti - 1) score += 5;
		if (ti == 0 || t[ti - 1] == ' ' || t[ti - 1] == '-' || t[ti - 1] == '_') score += 5;
		last = (int)ti;
		qi++;
	}
	return qi < q.size() ? -1 : score;
}

}  // namespace

void showQuickAdd(CircuitWindow* w) {
	struct Entry { std::string name, caption, category; };
	std::vector<Entry> all;
	for (int c = 0; c < cl_library_category_count(); c++) {
		std::string cat = cl_library_category(c);
		const size_t dash = cat.find(" - ");
		if (dash != std::string::npos) cat = cat.substr(dash + 3);
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			const std::string name = cl_library_gate(c, i);
			const std::string caption = cl_library_gate_caption(name.c_str());
			all.push_back({ name, caption.empty() ? name : caption, cat });
		}
	}
	for (const parts::Part& p : parts::all()) all.push_back({ p.gate(), p.name, "My Parts" });
	std::string chosen;
	picker::Picker p;
	p.title = "Add a Gate";
	p.line = "Type to search, then press Enter. The gate follows your mouse onto the canvas.";
	p.width = 520;
	p.height = 560;
	p.emptyText = "No gates match that.";
	p.rightButtons = { "Cancel", "Add" };
	p.rows = [&](const std::string& query) {
		std::vector<std::pair<int, size_t>> scored;
		for (size_t i = 0; i < all.size(); i++) {
			const int sc = query.empty() ? 1 : std::max(fuzzyScore(query, all[i].caption), fuzzyScore(query, all[i].name));
			if (sc > 0) scored.push_back({ sc, i });
		}
		std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
		std::vector<picker::Row> out;
		for (auto& sc : scored) {
			const Entry& e = all[sc.second];
			picker::Row r;
			r.id = e.name;
			r.title = e.caption;
			r.subtitle = e.caption != e.name && !parts::isPart(e.name) ? e.name + "  ·  " + e.category : e.category;
			out.push_back(r);
			if (out.size() >= 400) break;
		}
		return out;
	};
	p.drawTile = [](cairo_t* cr, const picker::Row& row, const RectF& r) {
		cairo_save(cr);
		cairo_translate(cr, r.left, r.top);
		if (parts::isPart(row.id)) parts::draw(row.id, cr, r.right - r.left, r.bottom - r.top, 2, prefs().dark);
		else cl_library_draw_gate(row.id.c_str(), cr, r.right - r.left, r.bottom - r.top, 2, prefs().dark);
		cairo_restore(cr);
	};
	p.onButton = [&](picker::Picker& pk, int b) -> bool {
		if (b == 100) return true;
		if (b == 101) {
			const picker::Row* r = pk.selected();
			if (r) chosen = r->id;
			return r != nullptr;
		}
		return false;
	};
	p.run(w->window());
	// As in wx, it appears on the pointer at the next move over the canvas.
	if (!chosen.empty()) w->addGateOnNextMove(chosen);
}

// The truth table: TruthTableWindow.cpp.

// ---- Memory (RAM and ROM contents): RamEditor.cpp ----

// ---- Preferences: Settings.cpp ----

// ---- Every shortcut ----------------------------------------------------------------

// Every shortcut: ShortcutsSheet.cpp.

// ---- The oscilloscope: Scope.cpp ----

// ---- Build from Formula ----------------------------------------------------------
// The Mac app's FormulaCircuit: type a formula (or several, one an output),
// see what it'll make, and it's built -- switches for the variables, gates,
// a light for each output, labelled and wired.

namespace {

struct BuildForm {
	GtkWidget* text;
	GtkWidget* preview;
	GtkWidget* shape;
	GtkWidget* style;
	GtkWidget* two;
	GtkWidget* ok;
	formula::Parsed parsed;
	bool valid = false;
};

std::string textOf(GtkWidget* view) {
	GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter s, e;
	gtk_text_buffer_get_bounds(b, &s, &e);
	gchar* t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
	std::string out = t ? t : "";
	g_free(t);
	return out;
}

void buildUpdate(BuildForm* f) {
	std::string error;
	const std::string src = textOf(f->text);
	bool blank = true;
	for (char c : src) if (!g_ascii_isspace(c)) blank = false;
	f->valid = false;
	if (blank) {
		gtk_label_set_text(GTK_LABEL(f->preview), "");
	} else if (!formula::parse(src, f->parsed, error)) {
		gtk_label_set_text(GTK_LABEL(f->preview), ("⚠  " + error).c_str());
	} else {
		f->valid = true;
		std::string s = f->parsed.variables.empty() ? std::string("No variables")
		                                            : format("%d variable%s: ", (int)f->parsed.variables.size(),
		                                                     f->parsed.variables.size() == 1 ? "" : "s");
		for (size_t i = 0; i < f->parsed.variables.size(); i++) s += (i ? ", " : "") + f->parsed.variables[i];
		for (const formula::Function& fn : f->parsed.functions)
			s += "\nSimplest:  " + fn.name + " = " + formula::simplest(true, (int)f->parsed.variables.size(), fn.values).text(f->parsed.variables);
		const formula::Plan plan = formula::plan(f->parsed, (formula::Shape)gtk_combo_box_get_active(GTK_COMBO_BOX(f->shape)),
		                                         (formula::Style)gtk_combo_box_get_active(GTK_COMBO_BOX(f->style)),
		                                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f->two)));
		s += "\n" + plan.summary();
		gtk_label_set_text(GTK_LABEL(f->preview), s.c_str());
	}
	gtk_widget_set_sensitive(f->ok, f->valid);
}

GtkWidget* comboOf(const std::vector<const char*>& items, int active) {
	GtkWidget* c = gtk_combo_box_text_new();
	for (const char* i : items) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), i);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
	return c;
}

}  // namespace

void showBuildFormula(CircuitWindow* w) {
	Prefs& pr = prefs();
	GtkWidget* d = gtk_dialog_new_with_buttons("Build from Formula", w->window(),
	                                           (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                           "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
	BuildForm f;
	f.ok = gtk_dialog_add_button(GTK_DIALOG(d), "_Build", GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(f.ok), "suggested-action");
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
	gtk_window_set_default_size(GTK_WINDOW(d), 560, -1);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_box_set_spacing(GTK_BOX(box), 10);
	GtkWidget* intro = gtk_label_new("Switches for the variables, gates for the formula, and a light for each output, labelled and wired.");
	gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
	gtk_label_set_xalign(GTK_LABEL(intro), 0);
	gtk_box_pack_start(GTK_BOX(box), intro, FALSE, FALSE, 0);

	f.text = gtk_text_view_new();
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(f.text), TRUE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(f.text), 8);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(f.text), 6);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(f.text)), pr.lastFormula.c_str(), -1);
	gtk_widget_set_size_request(f.text, -1, 76);
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_container_add(GTK_CONTAINER(frame), f.text);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	GtkWidget* help = gtk_label_new("One output per line. NOT: A' or ~A · AND: AB, A·B or A*B · OR: A + B · XOR: A ^ B "
	                                "· or minterms: F(A,B,C) = m(1,3,5) + d(7)");
	gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
	gtk_label_set_xalign(GTK_LABEL(help), 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(help), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 0);

	f.preview = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(f.preview), 0);
	gtk_label_set_line_wrap(GTK_LABEL(f.preview), TRUE);
	gtk_label_set_selectable(GTK_LABEL(f.preview), TRUE);
	gtk_widget_set_size_request(f.preview, -1, 80);
	gtk_label_set_yalign(GTK_LABEL(f.preview), 0);
	gtk_box_pack_start(GTK_BOX(box), f.preview, FALSE, FALSE, 0);

	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	f.shape = comboOf({ "As written", "Simplest sum of products", "Simplest product of sums" }, pr.buildShape);
	f.style = comboOf({ "Any gates", "NAND only", "NOR only" }, pr.buildStyle);
	f.two = gtk_check_button_new_with_label("Only 2-input gates");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f.two), pr.buildTwoInput);
	GtkWidget* where = comboOf({ "On a new page", "Beside this page's circuit" }, pr.buildNewPage ? 0 : 1);
	const char* labels[] = { "Build it", "With", "", "Put it" };
	GtkWidget* fields[] = { f.shape, f.style, f.two, where };
	for (int i = 0; i < 4; i++) {
		GtkWidget* l = gtk_label_new(labels[i]);
		gtk_label_set_xalign(GTK_LABEL(l), 1);
		gtk_grid_attach(GTK_GRID(grid), l, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 4);

	g_signal_connect_swapped(gtk_text_view_get_buffer(GTK_TEXT_VIEW(f.text)), "changed", G_CALLBACK(buildUpdate), &f);
	g_signal_connect_swapped(f.shape, "changed", G_CALLBACK(buildUpdate), &f);
	g_signal_connect_swapped(f.style, "changed", G_CALLBACK(buildUpdate), &f);
	g_signal_connect_swapped(f.two, "toggled", G_CALLBACK(buildUpdate), &f);
	buildUpdate(&f);
	gtk_widget_show_all(d);
	gtk_widget_grab_focus(f.text);
	const bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK && f.valid;
	if (ok) {
		pr.lastFormula = textOf(f.text);
		pr.buildShape = gtk_combo_box_get_active(GTK_COMBO_BOX(f.shape));
		pr.buildStyle = gtk_combo_box_get_active(GTK_COMBO_BOX(f.style));
		pr.buildTwoInput = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f.two));
		pr.buildNewPage = gtk_combo_box_get_active(GTK_COMBO_BOX(where)) == 0;
		pr.save();
	}
	gtk_widget_destroy(d);
	if (!ok) return;
	const formula::Plan plan = formula::plan(f.parsed, (formula::Shape)pr.buildShape, (formula::Style)pr.buildStyle, pr.buildTwoInput);
	std::string name;
	for (size_t i = 0; i < f.parsed.functions.size(); i++) name += (i ? ", " : "") + f.parsed.functions[i].name;
	if (!w->buildPlan(plan, pr.buildNewPage, name)) gtk_widget_error_bell(GTK_WIDGET(w->window()));
}
