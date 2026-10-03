// Export as Image, as the Mac app's (ExportImageView.swift, after the wx
// app's) and the Windows app's: a preview, the grid if wanted, and a strip
// under the circuit with your name and whether it works -- what a grader
// looks for first -- in colour or black and white, at 2x, 4x or 6x. Copied,
// or saved as a PNG (or a PDF or SVG, which stay sharp at any size).

#include "Alert.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Window.h"

#include <cairo-pdf.h>
#include <cairo-svg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace {

const float kPointsPerUnit = 12, kMargin = 24;
const float kPad = 22, kBody = 17, kSmall = 11, kGap = 8;

struct ExportInfo {
	bool enabled = false;
	std::string name, why, fileName;
	bool works = true;
};

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<std::string> stripLines(const ExportInfo& info, float width, float& height) {
	std::string statement = info.works ? "My circuit works properly." : "My circuit does not work because " + trimmed(info.why);
	for (char& c : statement) if (c == '\r' || c == '\n') c = ' ';
	std::vector<std::string> lines;
	std::string line;
	size_t i = 0;
	while (i < statement.size()) {
		const size_t j = statement.find(' ', i);
		const std::string word = statement.substr(i, j == std::string::npos ? std::string::npos : j - i);
		i = j == std::string::npos ? statement.size() : j + 1;
		if (word.empty()) continue;
		const std::string trial = line.empty() ? word : line + " " + word;
		if (!line.empty() && textWidth(trial, kBody) > width - 2 * kPad) { lines.push_back(line); line = word; }
		else line = trial;
	}
	if (!line.empty()) lines.push_back(line);
	height = kPad + kBody + kGap * 1.6f + lines.size() * (kBody + kGap) + kPad * 0.5f;
	return lines;
}

bool imageSize(CLDocument* doc, int page, const ExportInfo& info, float& w, float& h) {
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return false;
	w = std::max((float)(r - l) * kPointsPerUnit + 2 * kMargin, info.enabled ? 420.0f : 0.0f);
	h = (float)(t - b) * kPointsPerUnit + 2 * kMargin;
	if (info.enabled) {
		float sh = 0;
		stripLines(info, w, sh);
		h += sh;
	}
	w = std::ceil(w);
	h = std::ceil(h);
	return true;
}

void drawImage(cairo_t* cr, CLDocument* doc, int page, float w, float h, double scale, bool color, bool grid, const ExportInfo& info) {
	float stripH = 0;
	std::vector<std::string> lines;
	if (info.enabled) lines = stripLines(info, w, stripH);
	const float circuitH = h - stripH;
	fillRect(cr, rectF(0, 0, w, h), colorF(1, 1, 1));
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return;
	const double midX = (l + r) / 2, midY = (b + t) / 2;
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, circuitH);
	cairo_clip(cr);
	if (grid) {
		const double upp = 1 / kPointsPerUnit;
		const double ox = midX - w / 2 * upp, oy = midY + circuitH / 2 * upp;
		cairo_set_source_rgba(cr, 0, 0, 0.2, 0.2);
		cairo_set_line_width(cr, 0.5);
		for (double x = std::ceil(ox); x < ox + w * upp; x += 1) {
			const double sx = (x - ox) / upp;
			cairo_move_to(cr, sx, 0);
			cairo_line_to(cr, sx, circuitH);
		}
		for (double y = std::floor(oy); y > oy - circuitH * upp; y -= 1) {
			const double sy = (oy - y) / upp;
			cairo_move_to(cr, 0, sy);
			cairo_line_to(cr, w, sy);
		}
		cairo_stroke(cr);
	}
	if (!color) {
		cl_document_draw_fitted(doc, page, cr, w, circuitH, kMargin, scale, CL_STYLE_PRINT);
	} else {
		const double upp = std::max((r - l) / (w - 2 * kMargin), (t - b) / (circuitH - 2 * kMargin));
		CLDrawOptions o = {};
		o.dark = false;
		o.accent = prefs().accent;
		o.wireScale = 1;
		o.simView = false;
		o.thumbnail = false;
		o.showSelection = false;
		o.selectionFade = 1;
		cl_document_draw_ex(doc, page, cr, scale, midX - w / 2 * upp, midY + circuitH / 2 * upp, upp, &o);
	}
	cairo_restore(cr);
	if (!info.enabled) return;

	// The name-and-result strip, black on white so it prints.
	const Color ink = colorF(0, 0, 0), gray = colorF(0.4f, 0.4f, 0.4f);
	fillRect(cr, rectF(kPad, circuitH, w - kPad, circuitH + 1), ink);
	float y = circuitH + kPad;
	const std::string label = "Name:";
	drawText(cr, label, rectF(kPad, y, w, y + kBody * 1.6f), kBody, gray);
	const std::string name = trimmed(info.name);
	drawText(cr, name.empty() ? "________________" : name, rectF(kPad + textWidth(label, kBody) + 8, y, w, y + kBody * 1.6f), kBody, ink);
	char date[64];
	const time_t now = time(nullptr);
	struct tm lt;
	localtime_r(&now, &lt);
	strftime(date, sizeof date, "%b %d, %Y", &lt);
	const std::string meta = info.fileName.empty() ? std::string(date) : info.fileName + "   " + date;
	drawText(cr, meta, rectF(kPad, y + kBody - kSmall + 2, w - kPad, y + kBody * 1.6f), kSmall, gray, TextAlign::Trailing);
	y += kBody + kGap * 1.6f;
	for (const std::string& line : lines) {
		drawText(cr, line, rectF(kPad, y, w, y + kBody * 1.6f), kBody, ink);
		y += kBody + kGap;
	}
}

cairo_surface_t* makeImage(CLDocument* doc, int page, double multiplier, bool color, bool grid, const ExportInfo& info) {
	float w, h;
	if (!imageSize(doc, page, info, w, h)) return nullptr;
	double scale = multiplier;
	if (std::max(w, h) * scale > 12000) scale = 12000 / std::max(w, h);
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)std::ceil(w * scale), (int)std::ceil(h * scale));
	cairo_t* cr = cairo_create(s);
	cairo_scale(cr, scale, scale);
	drawImage(cr, doc, page, w, h, scale, color, grid, info);
	cairo_destroy(cr);
	return s;
}

struct Form {
	CircuitWindow* win;
	CLDocument* doc;
	int page;
	std::string fileName;
	GtkWidget* dialog;
	GtkWidget* preview;
	GtkWidget* grid;
	GtkWidget* info;
	GtkWidget* name;
	GtkWidget* works;
	GtkWidget* why;
	GtkWidget* style;
	GtkWidget* res;
	GtkWidget* problem;

	struct State { ExportInfo info; bool color, grid; int multiplier; };
	State state() const {
		State s;
		s.grid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grid));
		s.info.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(info));
		s.info.name = gtk_entry_get_text(GTK_ENTRY(name));
		s.info.works = gtk_combo_box_get_active(GTK_COMBO_BOX(works)) == 0;
		s.info.why = gtk_entry_get_text(GTK_ENTRY(why));
		s.info.fileName = fileName;
		s.color = gtk_combo_box_get_active(GTK_COMBO_BOX(style)) == 0;
		const int r = gtk_combo_box_get_active(GTK_COMBO_BOX(res));
		s.multiplier = r == 0 ? 2 : r == 2 ? 6 : 4;
		return s;
	}
	void keep(const State& s) const {
		Prefs& p = prefs();
		p.exportGrid = s.grid;
		p.exportInfo = s.info.enabled;
		p.studentName = trimmed(s.info.name);
		p.exportWorks = s.info.works;
		p.exportProblem = s.info.why;
		p.exportColor = s.color;
		p.exportScale = s.multiplier;
		p.save();
	}
	std::string bad(const State& s) const {
		if (s.info.enabled && !s.info.works && trimmed(s.info.why).empty()) return "Say what doesn't work, or choose “works properly”.";
		return "";
	}
	void changed() {
		const bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(info));
		gtk_widget_set_sensitive(name, on);
		gtk_widget_set_sensitive(works, on);
		gtk_widget_set_sensitive(why, on && gtk_combo_box_get_active(GTK_COMBO_BOX(works)) == 1);
		gtk_label_set_text(GTK_LABEL(problem), "");
		gtk_widget_queue_draw(preview);
	}
};

GtkWidget* comboOf(const std::vector<const char*>& items, int active) {
	GtkWidget* c = gtk_combo_box_text_new();
	for (const char* i : items) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), i);
	gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
	return c;
}

}  // namespace

std::string safeFileName(const std::string& name) {
	std::string out = name;
	for (char& c : out)
		if ((unsigned char)c < 0x20 || strchr("/\\:*?\"<>|", c)) c = '-';
	// Not hidden (a leading dot), and nothing a USB stick drops at the end.
	const size_t start = out.find_first_not_of(". ");
	out = start == std::string::npos ? std::string() : out.substr(start);
	while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
	return out.empty() ? "Untitled" : out;
}

std::string chooseImageFile(GtkWindow* parent, const char* title, const char* accept, const std::string& suggested, bool vectors) {
	std::string name = suggested, folder = prefs().lastFolder;
	for (;;) {
		GtkFileChooserNative* c = gtk_file_chooser_native_new(title, parent, GTK_FILE_CHOOSER_ACTION_SAVE, accept, "_Cancel");
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(c), TRUE);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(c), name.c_str());
		if (!folder.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(c), folder.c_str());
		GtkFileFilter* filter = gtk_file_filter_new();
		gtk_file_filter_set_name(filter, vectors ? "Pictures (PNG, or PDF and SVG)" : "PNG pictures");
		gtk_file_filter_add_pattern(filter, "*.png");
		gtk_file_filter_add_pattern(filter, "*.PNG");
		if (vectors) {
			gtk_file_filter_add_pattern(filter, "*.pdf");
			gtk_file_filter_add_pattern(filter, "*.svg");
		}
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(c), filter);
		std::string file;
		if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(c)) == GTK_RESPONSE_ACCEPT)
			if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(c))) { file = f; g_free(f); }
		g_object_unref(c);
		if (file.empty()) return file;
		auto endsWith = [&](const char* x) {
			const size_t n = strlen(x);
			return file.size() >= n && g_ascii_strcasecmp(file.c_str() + file.size() - n, x) == 0;
		};
		if (endsWith(".png") || (vectors && (endsWith(".pdf") || endsWith(".svg")))) return file;
		file += ".png";
		if (!g_file_test(file.c_str(), G_FILE_TEST_EXISTS)) return file;
		// The chooser asked only about the name as it was typed.
		gchar* base = g_path_get_basename(file.c_str());
		gchar* dir = g_path_get_dirname(file.c_str());
		name = base;
		folder = dir;
		g_free(base);
		g_free(dir);
		if (askConfirm(parent, "Replace “" + name + "”?", "There's a file with that name there already. Replacing it writes over it.",
		               "Replace", "Cancel", true))
			return file;
	}
}

void showExportImage(CircuitWindow* win, int page) {
	CLDocument* doc = win->document();
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) { win->note("This page is empty: nothing to export."); return; }
	Prefs& p = prefs();
	Form f;
	f.win = win;
	f.doc = doc;
	f.page = page;
	f.fileName = win->titleText();
	if (cl_document_page_count(doc) > 1) f.fileName += " - " + win->tabName(page);

	f.dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(f.dialog), "Export as Image");
	gtk_window_set_transient_for(GTK_WINDOW(f.dialog), win->window());
	gtk_window_set_modal(GTK_WINDOW(f.dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(f.dialog), 580, -1);
	gtk_dialog_add_button(GTK_DIALOG(f.dialog), "_Cancel", GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(f.dialog), "Copy to Clipboard", 100);
	GtkWidget* save = gtk_dialog_add_button(GTK_DIALOG(f.dialog), "Export to File…", GTK_RESPONSE_OK);
	gtk_style_context_add_class(gtk_widget_get_style_context(save), "suggested-action");
	gtk_dialog_set_default_response(GTK_DIALOG(f.dialog), GTK_RESPONSE_OK);
	GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(f.dialog));
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_box_set_spacing(GTK_BOX(box), 10);

	f.preview = gtk_drawing_area_new();
	gtk_widget_set_size_request(f.preview, -1, 240);
	gtk_box_pack_start(GTK_BOX(box), f.preview, FALSE, FALSE, 0);
	f.grid = gtk_check_button_new_with_label("Include grid lines");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f.grid), p.exportGrid);
	gtk_box_pack_start(GTK_BOX(box), f.grid, FALSE, FALSE, 0);
	f.info = gtk_check_button_new_with_label("Add my name and whether the circuit works");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(f.info), p.exportInfo);
	gtk_box_pack_start(GTK_BOX(box), f.info, FALSE, FALSE, 0);

	GtkWidget* grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	f.name = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(f.name), p.studentName.c_str());
	gtk_widget_set_hexpand(f.name, TRUE);
	f.works = comboOf({ "My circuit works properly", "My circuit does not work because…" }, p.exportWorks ? 0 : 1);
	f.why = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(f.why), p.exportProblem.c_str());
	gtk_entry_set_placeholder_text(GTK_ENTRY(f.why), "Explain what doesn't work (required)");
	f.style = comboOf({ "Color", "Black & White" }, p.exportColor ? 0 : 1);
	f.res = comboOf({ "Screen (2×)", "Print (4×)", "High Quality (6×)" }, p.exportScale <= 2 ? 0 : p.exportScale >= 6 ? 2 : 1);
	const char* labels[] = { "Your name", "Result", "Because", "Output style", "Resolution" };
	GtkWidget* fields[] = { f.name, f.works, f.why, f.style, f.res };
	for (int i = 0; i < 5; i++) {
		GtkWidget* lbl = gtk_label_new(labels[i]);
		gtk_label_set_xalign(GTK_LABEL(lbl), 1);
		gtk_grid_attach(GTK_GRID(grid), lbl, 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	f.problem = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(f.problem), 0);
	gtk_box_pack_start(GTK_BOX(box), f.problem, FALSE, FALSE, 0);

	g_signal_connect(f.preview, "draw", CL_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer data) -> gboolean {
		Form* form = static_cast<Form*>(data);
		guarded("the export preview", [&] {
			const Form::State s = form->state();
			float w, h;
			if (!imageSize(form->doc, form->page, s.info, w, h)) return;
			const float pw = gtk_widget_get_allocated_width(a), ph = gtk_widget_get_allocated_height(a);
			const float inset = 8, fit = std::min({ 1.0f, (pw - 2 * inset) / w, (ph - 2 * inset) / h });
			const float x = (pw - w * fit) / 2, y = (ph - h * fit) / 2;
			fillRound(cr, rectF(0, 0, pw, ph), 8, withAlpha(chrome().barInk(), 0.08f));
			cairo_save(cr);
			cairo_translate(cr, x, y);
			cairo_scale(cr, fit, fit);
			drawImage(cr, form->doc, form->page, w, h, fit * std::max(1, gtk_widget_get_scale_factor(a)), s.color, s.grid, s.info);
			cairo_restore(cr);
			strokeRound(cr, rectF(x, y, x + w * fit, y + h * fit), 1, colorF(0, 0, 0, 0.15f), 0.5f);
		});
		return TRUE;
	}), &f);
	for (GtkWidget* wdg : { f.grid, f.info }) g_signal_connect_swapped(wdg, "toggled", G_CALLBACK(+[](Form* form) { form->changed(); }), &f);
	for (GtkWidget* wdg : { f.works, f.style, f.res }) g_signal_connect_swapped(wdg, "changed", G_CALLBACK(+[](Form* form) { form->changed(); }), &f);
	for (GtkWidget* wdg : { f.name, f.why }) g_signal_connect_swapped(wdg, "changed", G_CALLBACK(+[](Form* form) { form->changed(); }), &f);
	f.changed();
	gtk_widget_show_all(f.dialog);

	std::string file;
	Form::State s;
	for (;;) {
		const int rsp = gtk_dialog_run(GTK_DIALOG(f.dialog));
		if (rsp != 100 && rsp != GTK_RESPONSE_OK) break;
		s = f.state();
		const std::string why = f.bad(s);
		if (!why.empty()) { gtk_label_set_text(GTK_LABEL(f.problem), why.c_str()); gtk_widget_error_bell(f.dialog); continue; }
		f.keep(s);
		if (rsp == 100) {
			cairo_surface_t* img = makeImage(doc, page, s.multiplier, s.color, s.grid, s.info);
			GdkPixbuf* pb = img ? gdk_pixbuf_get_from_surface(img, 0, 0, cairo_image_surface_get_width(img), cairo_image_surface_get_height(img))
			                    : nullptr;
			if (pb) {
				gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pb);
				g_object_unref(pb);
				win->note("Image copied.");
			}
			if (img) cairo_surface_destroy(img);
			if (!pb) { gtk_label_set_text(GTK_LABEL(f.problem), "The image couldn't be copied."); continue; }
			break;
		}
		file = chooseImageFile(GTK_WINDOW(f.dialog), "Export as Image", "_Export", safeFileName(f.fileName) + ".png", true);
		if (file.empty()) continue;
		break;
	}
	gtk_widget_destroy(f.dialog);
	if (file.empty()) return;
	gchar* lowerName = g_ascii_strdown(file.c_str(), -1);
	const std::string lf = lowerName;
	g_free(lowerName);
	auto endsWith = [&](const char* x) { const size_t n = strlen(x); return lf.size() >= n && lf.compare(lf.size() - n, n, x) == 0; };
	cairo_status_t st = CAIRO_STATUS_SUCCESS;
	if (endsWith(".pdf") || endsWith(".svg")) {
		float w, h;
		imageSize(doc, page, s.info, w, h);
		cairo_surface_t* vs = endsWith(".pdf") ? cairo_pdf_surface_create(file.c_str(), w, h) : cairo_svg_surface_create(file.c_str(), w, h);
		cairo_t* cr = cairo_create(vs);
		drawImage(cr, doc, page, w, h, 1, s.color, s.grid, s.info);
		cairo_destroy(cr);
		cairo_surface_finish(vs);
		st = cairo_surface_status(vs);
		cairo_surface_destroy(vs);
	} else {
		cairo_surface_t* img = makeImage(doc, page, s.multiplier, s.color, s.grid, s.info);
		st = img ? cairo_surface_write_to_png(img, file.c_str()) : CAIRO_STATUS_NO_MEMORY;
		if (img) cairo_surface_destroy(img);
	}
	if (st != CAIRO_STATUS_SUCCESS) {
		showMessage(win->window(), GTK_MESSAGE_ERROR, "The image couldn't be saved", cairo_status_to_string(st));
		return;
	}
	gchar* dir = g_path_get_dirname(file.c_str());
	prefs().lastFolder = dir;
	g_free(dir);
	win->note("Exported " + baseName(file) + ".");
}
