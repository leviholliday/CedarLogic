// The app's dialogs and extra windows (see Dialogs.h).

#include "Dialogs.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// ---- A line of text ----------------------------------------------------------------

bool askText(GtkWindow* parent, const std::string& title, const std::string& prompt, std::string& value) {
	GtkWidget* d = gtk_dialog_new_with_buttons(title.c_str(), parent,
	                                           (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                           "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_box_set_spacing(GTK_BOX(box), 8);
	GtkWidget* label = gtk_label_new(prompt.c_str());
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	GtkWidget* entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 30);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_widget_show_all(d);
	const bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
	if (ok) {
		gchar* t = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))));
		value = t;
		g_free(t);
	}
	gtk_widget_destroy(d);
	return ok;
}

// ---- A gate's settings -----------------------------------------------------------
// The same settings the wx app's parameters dialog lists, each with a control
// that suits its type. OK applies what changed (one undo step each).

namespace {

struct SettingField {
	std::string name, type, value;
	double min, max;
	GtkWidget* widget;   // check button, entry, or the file row's entry
};

std::string numberText(double v) {
	if (v == std::floor(v) && std::fabs(v) < 1e15) return format("%.0f", v);
	return format("%g", v);
}

void chooseFileCb(GtkButton* b, gpointer data) {
	SettingField* f = static_cast<SettingField*>(data);
	GtkWindow* parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(b)));
	const bool in = f->type == "FILE_IN";
	GtkFileChooserNative* c = gtk_file_chooser_native_new(in ? "Choose a File" : "Save to File", parent,
		in ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE, in ? "_Open" : "_Save", "_Cancel");
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
		if (gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) {
			gtk_entry_set_text(GTK_ENTRY(f->widget), file);
			g_free(file);
		}
	}
	g_object_unref(c);
}

}  // namespace

void showGateSettings(CircuitWindow* w, long gate) {
	CLDocument* doc = w->document();
	const std::string caption = cl_gate_caption(doc, gate);
	GtkWidget* d = gtk_dialog_new_with_buttons(caption.c_str(), w->window(),
	                                           (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                           "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);

	std::vector<std::unique_ptr<SettingField>> fields;
	const int n = cl_gate_setting_count(doc, gate);
	for (int i = 0; i < n; i++) {
		CLGateSetting s;
		if (!cl_gate_setting(doc, gate, i, &s)) continue;
		std::unique_ptr<SettingField> f(new SettingField{ s.name, s.type, s.value ? s.value : "", s.min, s.max, nullptr });
		const std::string label = s.label ? s.label : s.name;
		if (f->type == "BOOL") {
			f->widget = gtk_check_button_new_with_label(label.c_str());
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f->widget), f->value == "true");
			gtk_grid_attach(GTK_GRID(grid), f->widget, 0, i, 2, 1);
		} else {
			GtkWidget* l = gtk_label_new(label.c_str());
			gtk_label_set_xalign(GTK_LABEL(l), 1);
			gtk_grid_attach(GTK_GRID(grid), l, 0, i, 1, 1);
			f->widget = gtk_entry_new();
			gtk_entry_set_text(GTK_ENTRY(f->widget), f->value.c_str());
			gtk_entry_set_activates_default(GTK_ENTRY(f->widget), TRUE);
			gtk_widget_set_hexpand(f->widget, TRUE);
			gtk_entry_set_width_chars(GTK_ENTRY(f->widget), 24);
			if ((f->type == "INT" || f->type == "FLOAT") && f->max < 1e30)
				gtk_widget_set_tooltip_text(f->widget, format("%s to %s", numberText(f->min).c_str(), numberText(f->max).c_str()).c_str());
			if (f->type == "FILE_IN" || f->type == "FILE_OUT") {
				GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
				GtkWidget* choose = gtk_button_new_with_mnemonic("_Choose…");
				g_signal_connect(choose, "clicked", G_CALLBACK(chooseFileCb), f.get());
				gtk_box_pack_start(GTK_BOX(row), f->widget, TRUE, TRUE, 0);
				gtk_box_pack_start(GTK_BOX(row), choose, FALSE, FALSE, 0);
				gtk_grid_attach(GTK_GRID(grid), row, 1, i, 1, 1);
			} else {
				gtk_grid_attach(GTK_GRID(grid), f->widget, 1, i, 1, 1);
			}
		}
		fields.push_back(std::move(f));
	}
	if (fields.empty()) {
		GtkWidget* l = gtk_label_new("This part has no settings.");
		gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
		gtk_grid_attach(GTK_GRID(grid), l, 0, 0, 2, 1);
	}
	GtkWidget* problem = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(problem), 0);
	gtk_box_pack_start(GTK_BOX(box), problem, FALSE, FALSE, 6);
	gtk_widget_show_all(d);
	gtk_widget_hide(problem);

	while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
		// Numbers are checked against the library's range, as the wx dialog does.
		std::string bad;
		std::vector<std::pair<std::string, std::string>> changes;
		for (auto& f : fields) {
			std::string v;
			if (f->type == "BOOL") {
				v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f->widget)) ? "true" : "false";
			} else {
				gchar* t = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(f->widget))));
				v = t;
				g_free(t);
			}
			if ((f->type == "INT" || f->type == "FLOAT") && bad.empty()) {
				char* end = nullptr;
				const double x = strtod(v.c_str(), &end);
				if (v.empty() || end == nullptr || *end != 0 || !std::isfinite(x) || (f->type == "INT" && x != std::floor(x)))
					bad = format("%s: enter %s.", f->name.c_str(), f->type == "INT" ? "a whole number" : "a number");
				else if (x < f->min || x > f->max)
					bad = format("%s must be between %s and %s.", f->name.c_str(), numberText(f->min).c_str(), numberText(f->max).c_str());
			}
			if (v != f->value) changes.push_back({ f->name, v });
		}
		if (!bad.empty()) {
			gtk_label_set_text(GTK_LABEL(problem), bad.c_str());
			gtk_widget_show(problem);
			continue;
		}
		for (auto& c : changes) cl_gate_set_setting(doc, gate, c.first.c_str(), c.second.c_str());
		if (!changes.empty()) w->edited();
		break;
	}
	gtk_widget_destroy(d);
}

// ---- Quick add (A) -------------------------------------------------------------
// Type part of a gate's name; Return puts the first (or chosen) match on the
// pointer.

namespace {

struct QuickAdd {
	GtkWidget* dialog;
	GtkWidget* entry;
	GtkListStore* store;
	GtkWidget* view;
	std::vector<std::pair<std::string, std::string>> all;   // name, caption
};

std::string lowered(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

// How well a gate matches what's typed (lower is better; -1 not at all):
// the whole name, then the start of it, then the start of a word, then
// anywhere.
int matchRank(const std::string& caption, const std::string& name, const std::string& text) {
	if (text.empty()) return 0;
	const std::string c = lowered(caption), n = lowered(name);
	if (c == text || n == text) return 0;
	if (c.compare(0, text.size(), text) == 0) return 1;
	for (size_t at = c.find(text); at != std::string::npos; at = c.find(text, at + 1))
		if (at > 0 && !g_ascii_isalnum(c[at - 1])) return 2;
	if (c.find(text) != std::string::npos || n.find(text) != std::string::npos) return 3;
	return -1;
}

void quickFilter(QuickAdd* q) {
	const std::string text = lowered(gtk_entry_get_text(GTK_ENTRY(q->entry)));
	gtk_list_store_clear(q->store);
	std::vector<std::pair<int, size_t>> hits;
	for (size_t i = 0; i < q->all.size(); i++) {
		const int r = matchRank(q->all[i].second, q->all[i].first, text);
		if (r >= 0) hits.push_back({ r, i });
	}
	std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first < b.first; });
	int shown = 0;
	for (auto& h : hits) {
		const auto& g = q->all[h.second];
		GtkTreeIter it;
		gtk_list_store_append(q->store, &it);
		gtk_list_store_set(q->store, &it, 0, g.second.c_str(), 1, g.first.c_str(), -1);
		if (++shown >= 300) break;
	}
	GtkTreePath* first = gtk_tree_path_new_first();
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(q->view), first, nullptr, FALSE);
	gtk_tree_path_free(first);
}

void quickChangedCb(GtkEditable*, gpointer data) { quickFilter(static_cast<QuickAdd*>(data)); }

gboolean quickKeyCb(GtkWidget*, GdkEventKey* e, gpointer data) {
	QuickAdd* q = static_cast<QuickAdd*>(data);
	// Up and Down move through the list while typing.
	if (e->keyval != GDK_KEY_Up && e->keyval != GDK_KEY_Down) return FALSE;
	GtkTreePath* path = nullptr;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(q->view), &path, nullptr);
	if (path == nullptr) return TRUE;
	if (e->keyval == GDK_KEY_Up) gtk_tree_path_prev(path);
	else gtk_tree_path_next(path);
	GtkTreeIter it;
	if (gtk_tree_model_get_iter(GTK_TREE_MODEL(q->store), &it, path))
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(q->view), path, nullptr, FALSE);
	gtk_tree_path_free(path);
	return TRUE;
}

void quickRowCb(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) {
	gtk_dialog_response(GTK_DIALOG(static_cast<QuickAdd*>(data)->dialog), GTK_RESPONSE_OK);
}

}  // namespace

void showQuickAdd(CircuitWindow* w) {
	QuickAdd q;
	for (int c = 0; c < cl_library_category_count(); c++)
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			std::string name = cl_library_gate(c, i);
			std::string caption = cl_library_gate_caption(name.c_str());
			q.all.push_back({ name, caption.empty() ? name : caption });
		}
	std::sort(q.all.begin(), q.all.end(), [](auto& a, auto& b) { return lowered(a.second) < lowered(b.second); });
	q.all.erase(std::unique(q.all.begin(), q.all.end()), q.all.end());

	q.dialog = gtk_dialog_new_with_buttons("Add a Gate", w->window(),
	                                       (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                       "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_OK, nullptr);
	gtk_dialog_set_default_response(GTK_DIALOG(q.dialog), GTK_RESPONSE_OK);
	gtk_window_set_default_size(GTK_WINDOW(q.dialog), 380, 420);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(q.dialog));
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_box_set_spacing(GTK_BOX(box), 8);
	q.entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(q.entry), "Type part of a gate's name");
	gtk_entry_set_activates_default(GTK_ENTRY(q.entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), q.entry, FALSE, FALSE, 0);
	q.store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	q.view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(q.store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(q.view), FALSE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(q.view),
		gtk_tree_view_column_new_with_attributes("Gate", gtk_cell_renderer_text_new(), "text", 0, nullptr));
	g_signal_connect(q.view, "row-activated", G_CALLBACK(quickRowCb), &q);
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_container_add(GTK_CONTAINER(scroll), q.view);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	g_signal_connect(q.entry, "changed", G_CALLBACK(quickChangedCb), &q);
	g_signal_connect(q.entry, "key-press-event", G_CALLBACK(quickKeyCb), &q);
	quickFilter(&q);
	gtk_widget_show_all(q.dialog);
	gtk_widget_grab_focus(q.entry);

	std::string chosen;
	if (gtk_dialog_run(GTK_DIALOG(q.dialog)) == GTK_RESPONSE_OK) {
		GtkTreePath* path = nullptr;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(q.view), &path, nullptr);
		GtkTreeIter it;
		if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(q.store), &it, path)) {
			gchar* name = nullptr;
			gtk_tree_model_get(GTK_TREE_MODEL(q.store), &it, 1, &name, -1);
			if (name) { chosen = name; g_free(name); }
		}
		if (path) gtk_tree_path_free(path);
	}
	gtk_widget_destroy(q.dialog);
	g_object_unref(q.store);
	// As in wx, it appears on the pointer at the next move over the canvas.
	if (!chosen.empty()) w->addGateOnNextMove(chosen);
}

// ---- Truth tables ----------------------------------------------------------------

void showTruthTable(CircuitWindow* w, int page) {
	char err[512] = "";
	CLTruthTable* tt = cl_truth_table(w->document(), page, err, sizeof err);
	if (tt == nullptr) {
		showMessage(w->window(), GTK_MESSAGE_INFO, "A truth table couldn't be made for this page",
		            *err ? err : "Add switches (inputs) and lights (outputs) to the page first.");
		return;
	}
	const int cols = cl_tt_columns(tt), rows = cl_tt_rows(tt), inputs = cl_tt_inputs(tt);
	GtkWidget* d = gtk_dialog_new_with_buttons("Truth Table", w->window(), GTK_DIALOG_DESTROY_WITH_PARENT,
	                                           "_Close", GTK_RESPONSE_CLOSE, nullptr);
	gtk_window_set_default_size(GTK_WINDOW(d), std::min(900, 120 + cols * 60), std::min(700, 140 + rows * 26));
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_box_set_spacing(GTK_BOX(box), 8);

	std::string about = format("%d input%s, %d output%s, %d row%s.", inputs, inputs == 1 ? "" : "s",
	                           cols - inputs, cols - inputs == 1 ? "" : "s", rows, rows == 1 ? "" : "s");
	if (cl_tt_sequential(tt)) about += " This page has clocks or flip-flops, so outputs can depend on what came before.";
	if (cl_tt_unsettled(tt) > 0) about += format(" %d row%s never settled.", cl_tt_unsettled(tt), cl_tt_unsettled(tt) == 1 ? "" : "s");
	GtkWidget* info = gtk_label_new(about.c_str());
	gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
	gtk_label_set_xalign(GTK_LABEL(info), 0);
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);

	std::vector<GType> types((size_t)cols, G_TYPE_STRING);
	GtkListStore* store = gtk_list_store_newv(cols, types.data());
	for (int r = 0; r < rows; r++) {
		GtkTreeIter it;
		gtk_list_store_append(store, &it);
		for (int c = 0; c < cols; c++) {
			const char cell[2] = { cl_tt_cell(tt, r, c), 0 };
			gtk_list_store_set(store, &it, c, cell, -1);
		}
	}
	GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);
	gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(view), GTK_TREE_VIEW_GRID_LINES_BOTH);
	for (int c = 0; c < cols; c++) {
		GtkCellRenderer* cell = gtk_cell_renderer_text_new();
		g_object_set(cell, "xalign", 0.5, "family", "monospace", nullptr);
		if (c >= inputs) g_object_set(cell, "weight", PANGO_WEIGHT_BOLD, nullptr);
		GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(cl_tt_name(tt, c), cell, "text", c, nullptr);
		gtk_tree_view_column_set_alignment(col, 0.5);
		gtk_tree_view_column_set_min_width(col, 44);
		gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
		if (c == inputs - 1 && c + 1 < cols) {
			// A gap between the inputs and the outputs.
			GtkTreeViewColumn* gap = gtk_tree_view_column_new();
			gtk_tree_view_column_set_min_width(gap, 10);
			gtk_tree_view_append_column(GTK_TREE_VIEW(view), gap);
		}
	}
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	GtkWidget* legend = gtk_label_new("X unknown · Z floating · ! conflict · - not connected");
	gtk_style_context_add_class(gtk_widget_get_style_context(legend), "dim-label");
	gtk_label_set_xalign(GTK_LABEL(legend), 0);
	gtk_box_pack_start(GTK_BOX(box), legend, FALSE, FALSE, 0);
	cl_tt_free(tt);
	g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
	gtk_widget_show_all(d);
}

// ---- Memory (RAM and ROM contents) -----------------------------------------------

namespace {

struct RamEditor {
	CircuitWindow* w;
	long gate;
	int addressBits, dataBits;
	GtkListStore* store;
	GtkWidget* dialog;
	guint timer;
	long lastRead = -2, lastWritten = -2;
};

std::string wordText(unsigned long v, int bits) {
	const int digits = std::max(1, (bits + 3) / 4);
	return format("%0*lX", digits, v);
}

void ramFill(RamEditor* r) {
	gtk_list_store_clear(r->store);
	const unsigned long words = 1UL << std::min(std::max(r->addressBits, 0), 20);
	const int aDigits = std::max(1, (r->addressBits + 3) / 4);
	const long lr = cl_ram_last_read(r->w->document(), r->gate), lw = cl_ram_last_written(r->w->document(), r->gate);
	for (unsigned long a = 0; a < words; a++) {
		GtkTreeIter it;
		gtk_list_store_append(r->store, &it);
		const unsigned long v = cl_ram_value(r->w->document(), r->gate, a);
		const char* mark = (long)a == lw ? "written last" : (long)a == lr ? "read last" : "";
		gtk_list_store_set(r->store, &it, 0, format("%0*lX", aDigits, a).c_str(), 1, wordText(v, r->dataBits).c_str(),
		                   2, format("%lu", v).c_str(), 3, mark, -1);
	}
	r->lastRead = lr;
	r->lastWritten = lw;
}

void ramEditedCb(GtkCellRendererText*, gchar* pathText, gchar* text, gpointer data) {
	RamEditor* r = static_cast<RamEditor*>(data);
	char* end = nullptr;
	const unsigned long v = strtoul(text, &end, 16);
	if (end == text || *end != 0) { gtk_widget_error_bell(r->dialog); return; }
	const unsigned long address = strtoul(pathText, nullptr, 10);
	cl_ram_set(r->w->document(), r->gate, address, v);
	GtkTreeIter it;
	if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(r->store), &it, pathText)) {
		const unsigned long now = cl_ram_value(r->w->document(), r->gate, address);
		gtk_list_store_set(r->store, &it, 1, wordText(now, r->dataBits).c_str(), 2, format("%lu", now).c_str(), -1);
	}
	r->w->edited();
}

gboolean ramTickCb(gpointer data) {
	// The words last read and written change as the circuit runs.
	RamEditor* r = static_cast<RamEditor*>(data);
	std::vector<CircuitWindow*>& all = circuitWindows();
	if (std::find(all.begin(), all.end(), r->w) == all.end()) { r->timer = 0; return G_SOURCE_REMOVE; }
	const long lr = cl_ram_last_read(r->w->document(), r->gate), lw = cl_ram_last_written(r->w->document(), r->gate);
	if (lw != r->lastWritten) ramFill(r);
	else r->lastRead = lr;
	return G_SOURCE_CONTINUE;
}

}  // namespace

void showRamEditor(CircuitWindow* w, long gate) {
	RamEditor r{ w, gate, 0, 0, nullptr, nullptr, 0 };
	if (!cl_ram_info(w->document(), gate, &r.addressBits, &r.dataBits)) return;
	r.dialog = gtk_dialog_new_with_buttons("Memory", w->window(),
	                                       (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	                                       "_Load File…", 1, "_Save File…", 2, "_Settings…", 3, "_Close", GTK_RESPONSE_CLOSE, nullptr);
	gtk_window_set_default_size(GTK_WINDOW(r.dialog), 460, 560);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(r.dialog));
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_box_set_spacing(GTK_BOX(box), 8);
	const unsigned long words = 1UL << std::min(std::max(r.addressBits, 0), 20);
	GtkWidget* info = gtk_label_new(format("%lu addresses × %d bits. Double-click a value to change it (in hex).",
	                                       words, r.dataBits).c_str());
	gtk_label_set_xalign(GTK_LABEL(info), 0);
	gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
	r.store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	ramFill(&r);
	GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(r.store));
	gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(view), FALSE);
	const char* titles[] = { "Address", "Value (hex)", "Decimal", "" };
	for (int c = 0; c < 4; c++) {
		GtkCellRenderer* cell = gtk_cell_renderer_text_new();
		g_object_set(cell, "family", "monospace", nullptr);
		if (c == 1) {
			g_object_set(cell, "editable", TRUE, nullptr);
			g_signal_connect(cell, "edited", G_CALLBACK(ramEditedCb), &r);
		}
		if (c == 3) g_object_set(cell, "style", PANGO_STYLE_ITALIC, nullptr);
		gtk_tree_view_append_column(GTK_TREE_VIEW(view),
			gtk_tree_view_column_new_with_attributes(titles[c], cell, "text", c, nullptr));
	}
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	gtk_widget_show_all(r.dialog);
	r.timer = g_timeout_add(250, ramTickCb, &r);

	for (;;) {
		const int resp = gtk_dialog_run(GTK_DIALOG(r.dialog));
		if (resp == 1 || resp == 2) {
			const bool load = resp == 1;
			GtkFileChooserNative* c = gtk_file_chooser_native_new(load ? "Load Memory" : "Save Memory",
				GTK_WINDOW(r.dialog), load ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE,
				load ? "_Load" : "_Save", "_Cancel");
			gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(c), TRUE);
			if (!load) gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c), "memory.cdm");
			if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT) {
				if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) {
					if (load) { cl_ram_load_file(w->document(), gate, f); ramFill(&r); w->edited(); }
					else cl_ram_save_file(w->document(), gate, f);
					g_free(f);
				}
			}
			g_object_unref(c);
			continue;
		}
		if (resp == 3) {
			gtk_widget_hide(r.dialog);
			showGateSettings(w, gate);
			break;
		}
		break;
	}
	if (r.timer) g_source_remove(r.timer);
	gtk_widget_destroy(r.dialog);
	g_object_unref(r.store);
}

// ---- Preferences ---------------------------------------------------------------

namespace {

void prefsApply() {
	prefs().applyWireDots();
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

GtkWidget* combo(const std::vector<const char*>& items, int active, void (*changed)(GtkComboBox*, gpointer)) {
	GtkWidget* c = gtk_combo_box_text_new();
	for (const char* i : items) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), i);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
	g_signal_connect(c, "changed", G_CALLBACK(changed), nullptr);
	return c;
}

GtkWidget* check(const char* label, bool on, void (*toggled)(GtkToggleButton*, gpointer)) {
	GtkWidget* c = gtk_check_button_new_with_label(label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), on);
	g_signal_connect(c, "toggled", G_CALLBACK(toggled), nullptr);
	return c;
}

int row = 0;
void addRow(GtkWidget* grid, const char* label, GtkWidget* w) {
	if (label) {
		GtkWidget* l = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(l), 1);
		gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
	} else {
		gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
	}
	row++;
}

void addHeading(GtkWidget* grid, const char* text) {
	GtkWidget* l = gtk_label_new(nullptr);
	gchar* m = g_markup_printf_escaped("<b>%s</b>", text);
	gtk_label_set_markup(GTK_LABEL(l), m);
	g_free(m);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_widget_set_margin_top(l, row ? 10 : 0);
	gtk_grid_attach(GTK_GRID(grid), l, 0, row++, 2, 1);
}

}  // namespace

void showPreferencesDialog(GtkWindow* parent) {
	GtkWidget* d = gtk_dialog_new_with_buttons("Preferences", parent, GTK_DIALOG_DESTROY_WITH_PARENT,
	                                           "_Close", GTK_RESPONSE_CLOSE, nullptr);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 14);
	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
	row = 0;
	Prefs& p = prefs();

	addHeading(grid, "Appearance");
	addRow(grid, "Theme", combo({ "Match the system", "Light", "Dark", "As I left it" }, p.themeMode,
		[](GtkComboBox* c, gpointer) {
			prefs().themeMode = gtk_combo_box_get_active(c);
			if (prefs().themeMode == 1) prefs().dark = false;
			else if (prefs().themeMode == 2) prefs().dark = true;
			else if (prefs().themeMode == 0) prefs().dark = systemPrefersDark();
			prefs().save();
			applyTheme();
		}));
	addRow(grid, "Accent", combo({ "Blue", "Purple", "Pink", "Orange", "Green", "Graphite" }, p.accent,
		[](GtkComboBox* c, gpointer) { prefs().accent = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, "Grid", combo({ "Lines", "Dots" }, p.gridStyle,
		[](GtkComboBox* c, gpointer) { prefs().gridStyle = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, nullptr, check("Show the grid", p.showGrid,
		[](GtkToggleButton* t, gpointer) { prefs().showGrid = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, nullptr, check("Every fifth line darker", p.majorGrid,
		[](GtkToggleButton* t, gpointer) { prefs().majorGrid = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, "Wires", combo({ "Thin", "Normal", "Thick" }, p.wireThickness,
		[](GtkComboBox* c, gpointer) { prefs().wireThickness = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, nullptr, check("Dots at every bend (off: only where wires join)", p.wireDots,
		[](GtkToggleButton* t, gpointer) { prefs().wireDots = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, "Low wires when dark", combo({ "Silver", "Slate blue", "Soft white", "Classic grey" }, p.lowWire,
		[](GtkComboBox* c, gpointer) { prefs().lowWire = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, nullptr, check("Names under the palette's gates", p.showGateNames,
		[](GtkToggleButton* t, gpointer) {
			prefs().showGateNames = gtk_toggle_button_get_active(t);
			prefs().save();
			showMessage(nullptr, GTK_MESSAGE_INFO, "Changes the next window", "New windows show the palette this way.");
		}));

	addHeading(grid, "Canvas");
	addRow(grid, "Mouse wheel", combo({ "Zooms", "Moves around" }, p.mouseWheel,
		[](GtkComboBox* c, gpointer) { prefs().mouseWheel = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, "Touchpad scrolling", combo({ "Zooms", "Moves around" }, p.touchpadScroll,
		[](GtkComboBox* c, gpointer) { prefs().touchpadScroll = gtk_combo_box_get_active(c); prefsApply(); }));
	addRow(grid, nullptr, check("Reverse the wheel's zoom", p.reverseWheel,
		[](GtkToggleButton* t, gpointer) { prefs().reverseWheel = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, nullptr, check("Right-click a gate to rotate it", p.rightClickRotate,
		[](GtkToggleButton* t, gpointer) { prefs().rightClickRotate = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, nullptr, check("Duplicate (D) also copies to the clipboard", p.duplicateUsesClipboard,
		[](GtkToggleButton* t, gpointer) { prefs().duplicateUsesClipboard = gtk_toggle_button_get_active(t); prefsApply(); }));
	addRow(grid, "Tidy Up (Shift+S)", combo({ "Keeps the layout's shape", "Arranges by signal flow" }, p.tidyMode,
		[](GtkComboBox* c, gpointer) { prefs().tidyMode = gtk_combo_box_get_active(c); prefsApply(); }));

	g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
	gtk_widget_show_all(d);
}

// ---- Every shortcut ----------------------------------------------------------------

void showShortcutsWindow(GtkWindow* parent) {
	struct Key { const char* keys; const char* what; };
	struct Group { const char* title; std::vector<Key> keys; };
	const std::vector<Group> groups = {
		{ "Circuits", { { "<Primary>n", "New circuit" }, { "<Primary>o", "Open" }, { "<Primary>s", "Save" },
		                { "<Primary><Shift>s", "Save as" }, { "<Primary>e", "Export as an image" },
		                { "<Primary>p", "Print" }, { "<Primary>q", "Quit" } } },
		{ "Editing", { { "<Primary>z", "Undo" }, { "<Primary><Shift>z", "Redo" }, { "<Primary>x", "Cut" },
		               { "<Primary>c", "Copy" }, { "<Primary>v", "Paste (it follows the pointer)" },
		               { "<Primary>d", "Duplicate" }, { "<Primary>a", "Select all" }, { "Delete", "Delete" },
		               { "Escape", "Let go, or drop the selection" } } },
		{ "Building (on the canvas)", { { "a", "Add a gate by name" }, { "r", "Rotate" }, { "s", "Straighten wires" },
		               { "<Shift>s", "Tidy up (preview first)" }, { "c", "Copy; while moving, connect nearby pins" },
		               { "v", "Paste" }, { "x", "Cut" }, { "d", "Duplicate" }, { "Left Right Up Down", "Nudge the selection" },
		               { "<Shift>1", "Palette category 1 (…9, 0)" } } },
		{ "Moving around", { { "<Primary>equal", "Zoom in" }, { "<Primary>minus", "Zoom out" }, { "<Primary>0", "Zoom to fit" },
		               { "space", "Tap: zoom to fit. Hold and drag: move around" }, { "<Primary>1", "Actual size" },
		               { "<Primary>period", "Show or hide the palette" } } },
		{ "Simulation", { { "<Primary>r", "Simulation View" }, { "<Primary><Shift>r", "Step once" }, { "t", "Truth table" },
		               { "<Primary>g", "Oscilloscope" } } },
		{ "Tabs", { { "<Primary>t", "New tab" }, { "<Primary>w", "Close tab" }, { "<Primary><Shift>t", "Reopen the tab you closed" },
		            { "<Primary>Tab", "Next tab" }, { "<Primary><Shift>Tab", "Previous tab" } } },
		{ "App", { { "<Shift>question", "Every shortcut (this list)" }, { "<Primary><Shift>d", "Dark mode" },
		           { "<Primary>comma", "Preferences" }, { "F1", "Help" } } },
	};
	GtkWidget* win = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_WINDOW, "modal", TRUE, nullptr));
	gtk_window_set_transient_for(GTK_WINDOW(win), parent);
	GtkWidget* section = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_SECTION, "section-name", "shortcuts",
	                                             "max-height", 12, "visible", TRUE, nullptr));
	for (const Group& g : groups) {
		GtkWidget* group = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", g.title, "visible", TRUE, nullptr));
		for (const Key& k : g.keys) {
			GtkWidget* s = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT, "accelerator", k.keys,
			                                       "title", k.what, "visible", TRUE, nullptr));
			gtk_container_add(GTK_CONTAINER(group), s);
		}
		gtk_container_add(GTK_CONTAINER(section), group);
	}
	gtk_container_add(GTK_CONTAINER(win), section);
	gtk_widget_show_all(win);
}

// ---- The oscilloscope --------------------------------------------------------------

ScopeWindow::ScopeWindow(CircuitWindow* o) : owner(o) {
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Oscilloscope");
	gtk_window_set_transient_for(GTK_WINDOW(win), owner->window());
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(win), 820, 360);
	g_signal_connect(win, "delete-event", G_CALLBACK(deleteCb), this);
	// Escape or Ctrl+G puts it away; Space runs and pauses the circuit.
	g_signal_connect(win, "key-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		const bool ctrl = e->state & GDK_CONTROL_MASK;
		if (e->keyval == GDK_KEY_Escape || (ctrl && gdk_keyval_to_lower(e->keyval) == GDK_KEY_g)) {
			gtk_widget_hide(w);
			gtk_window_present(s->owner->window());
			return TRUE;
		}
		if (e->keyval == GDK_KEY_space && !ctrl) { s->owner->toggleRunning(); return TRUE; }
		return FALSE;
	}), this);
	GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(bar), 6);
	GtkWidget* clear = gtk_button_new_with_mnemonic("_Clear");
	GtkWidget* in = gtk_button_new_from_icon_name("zoom-in-symbolic", GTK_ICON_SIZE_BUTTON);
	GtkWidget* out = gtk_button_new_from_icon_name("zoom-out-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(clear, "clicked", G_CALLBACK(clearCb), this);
	g_signal_connect(in, "clicked", G_CALLBACK(zoomInCb), this);
	g_signal_connect(out, "clicked", G_CALLBACK(zoomOutCb), this);
	info = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(info), 0);
	gtk_box_pack_start(GTK_BOX(bar), clear, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), out, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), in, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), info, TRUE, TRUE, 8);
	gtk_box_pack_start(GTK_BOX(v), bar, FALSE, FALSE, 0);
	area = gtk_drawing_area_new();
	gtk_widget_set_vexpand(area, TRUE);
	g_signal_connect(area, "draw", G_CALLBACK(drawCb), this);
	gtk_box_pack_start(GTK_BOX(v), area, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(win), v);
}

ScopeWindow::~ScopeWindow() {
	if (win) {
		g_signal_handlers_disconnect_by_data(win, this);
		g_signal_handlers_disconnect_by_data(area, this);
		gtk_widget_destroy(win);
	}
}

void ScopeWindow::present() {
	gtk_widget_show_all(win);
	gtk_window_present(GTK_WINDOW(win));
	update();
}

void ScopeWindow::close() { gtk_widget_hide(win); }
bool ScopeWindow::visible() const { return gtk_widget_get_visible(win); }

void ScopeWindow::update() {
	CLDocument* doc = owner->document();
	const long long len = cl_scope_length(doc);
	gtk_label_set_text(GTK_LABEL(info), format("%d signal%s · %lld steps recorded · %d ms a step",
		cl_scope_signal_count(doc), cl_scope_signal_count(doc) == 1 ? "" : "s", len, cl_document_step_ms(doc)).c_str());
	gtk_widget_queue_draw(area);
}

gboolean ScopeWindow::deleteCb(GtkWidget* w, GdkEvent*, gpointer) {
	gtk_widget_hide(w);
	return TRUE;   // kept for next time
}

void ScopeWindow::clearCb(GtkButton*, gpointer self) {
	ScopeWindow* s = static_cast<ScopeWindow*>(self);
	cl_scope_clear(s->owner->document());
	s->update();
}

void ScopeWindow::zoomInCb(GtkButton*, gpointer self) {
	ScopeWindow* s = static_cast<ScopeWindow*>(self);
	s->zoom = std::min(48, s->zoom * 2);
	s->update();
}

void ScopeWindow::zoomOutCb(GtkButton*, gpointer self) {
	ScopeWindow* s = static_cast<ScopeWindow*>(self);
	s->zoom = std::max(1, s->zoom / 2);
	s->update();
}

gboolean ScopeWindow::drawCb(GtkWidget*, cairo_t* cr, gpointer self) {
	static_cast<ScopeWindow*>(self)->draw(cr);
	return TRUE;
}

// One lane per signal: low and high as a line at the bottom or the top,
// floating in the middle, unknown and conflict as shaded blocks. The newest
// step is at the right edge.
void ScopeWindow::draw(cairo_t* cr) {
	CLDocument* doc = owner->document();
	const double w = gtk_widget_get_allocated_width(area), h = gtk_widget_get_allocated_height(area);
	const bool dark = prefs().dark;
	const Palette pal{ dark, false };
	const RGBA bg = pal.canvas();
	cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
	cairo_paint(cr);
	const int n = cl_scope_signal_count(doc);
	const double ink = dark ? 0.85 : 0.15;
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12);
	if (n == 0) {
		cairo_set_source_rgb(cr, ink, ink, ink);
		const char* msg = "Add a TO label to a wire, and its signal shows up here.";
		cairo_text_extents_t e;
		cairo_text_extents(cr, msg, &e);
		cairo_move_to(cr, (w - e.width) / 2, h / 2);
		cairo_show_text(cr, msg);
		return;
	}
	const double nameW = 130, lane = std::min(34.0, std::max(18.0, (h - 8) / n));
	const long long len = cl_scope_length(doc);
	const int visible = std::max(1, (int)((w - nameW - 8) / zoom));
	const long long from = std::max(0LL, len - visible);
	const int count = (int)std::min<long long>(visible, len - from);
	std::vector<unsigned char> buf((size_t)std::max(count, 1));
	const RGBA accent = accentColor(dark);
	for (int s = 0; s < n; s++) {
		const double top = 4 + s * lane, hi = top + 4, lo = top + lane - 6, mid = (hi + lo) / 2;
		// Lane separator and name.
		cairo_set_source_rgba(cr, ink, ink, ink, 0.12);
		cairo_rectangle(cr, 0, top + lane - 1, w, 1);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, ink, ink, ink);
		cairo_move_to(cr, 8, mid + 4);
		cairo_show_text(cr, cl_scope_signal(doc, s));
		if (count <= 0) continue;
		cl_scope_samples(doc, s, from, count, buf.data());
		cairo_set_line_width(cr, 1.5);
		cairo_set_source_rgb(cr, accent.r, accent.g, accent.b);
		double prevY = -1;
		for (int i = 0; i < count; i++) {
			const double x0 = nameW + i * zoom, x1 = x0 + zoom;
			const unsigned char v = buf[i];
			if (v == 0 || v == 1 || v == 2) {
				const double y = v == 1 ? hi : v == 0 ? lo : mid;
				cairo_set_source_rgb(cr, v == 2 ? 0.0 : accent.r, v == 2 ? 0.7 : accent.g, v == 2 ? 0.0 : accent.b);
				if (prevY >= 0 && prevY != y) { cairo_move_to(cr, x0, prevY); cairo_line_to(cr, x0, y); }
				cairo_move_to(cr, x0, y);
				cairo_line_to(cr, x1, y);
				cairo_stroke(cr);
				prevY = y;
			} else if (v == 3 || v == 4) {
				// Conflict red, unknown blue, as the canvas colours them.
				if (v == 3) cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.5);
				else cairo_set_source_rgba(cr, 0.3, 0.3, 1.0, 0.4);
				cairo_rectangle(cr, x0, hi, zoom, lo - hi);
				cairo_fill(cr);
				prevY = -1;
			} else {
				prevY = -1;
			}
		}
	}
}
