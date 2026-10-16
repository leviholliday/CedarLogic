// Notes for the circuit (see Notes.h).

#include "Notes.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <cstring>

namespace {

const char* kPlaceholder = "Notes for this circuit. They're saved with it.";

std::string withCommas(int n) {
	std::string s = std::to_string(n);
	for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
	return s;
}

}  // namespace

NotesPane::NotesPane(CircuitWindow* window) : win(window) {
	revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
	gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 200);
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(revealer), row);
	GtkWidget* sash = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(sash, "sash");
	gtk_widget_set_size_request(sash, 1, -1);
	gtk_box_pack_start(GTK_BOX(row), sash, FALSE, FALSE, 0);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(box, "notes");
	gtk_widget_set_size_request(box, 260, -1);
	gtk_box_pack_start(GTK_BOX(row), box, TRUE, TRUE, 0);

	// "Notes", the count near the limit, and a button that hides the pane.
	GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(head, 12);
	gtk_widget_set_margin_end(head, 8);
	gtk_widget_set_margin_top(head, 8);
	gtk_widget_set_margin_bottom(head, 4);
	GtkWidget* title = gtk_label_new("Notes");
	gtk_style_context_add_class(gtk_widget_get_style_context(title), "notes-title");
	gtk_box_pack_start(GTK_BOX(head), title, FALSE, FALSE, 0);
	counter = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(counter), "notes-count");
	gtk_box_pack_end(GTK_BOX(head), counter, FALSE, FALSE, 0);
	GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(close, "Hide Notes");
	gtk_widget_set_name(close, "notes-close");
	g_signal_connect(close, "clicked", CL_CALLBACK(+[](GtkButton*, gpointer self) {
		static_cast<NotesPane*>(self)->win->runAction("win.notes");
	}), this);
	gtk_box_pack_end(GTK_BOX(head), close, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);

	// The text, with its placeholder over it while it's empty.
	GtkWidget* over = gtk_overlay_new();
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	view = gtk_text_view_new();
	gtk_widget_set_name(view, "notes-text");
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 12);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 10);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 10);
	gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE);
	AtkObject* acc = gtk_widget_get_accessible(view);
	if (acc) atk_object_set_name(acc, "Notes for this circuit");
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_container_add(GTK_CONTAINER(over), scroll);
	placeholder = gtk_label_new(kPlaceholder);
	gtk_label_set_line_wrap(GTK_LABEL(placeholder), TRUE);
	gtk_label_set_xalign(GTK_LABEL(placeholder), 0);
	gtk_widget_set_halign(placeholder, GTK_ALIGN_START);
	gtk_widget_set_valign(placeholder, GTK_ALIGN_START);
	gtk_widget_set_margin_start(placeholder, 12);
	gtk_widget_set_margin_end(placeholder, 10);
	gtk_widget_set_margin_top(placeholder, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(placeholder), "notes-placeholder");
	gtk_overlay_add_overlay(GTK_OVERLAY(over), placeholder);
	gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(over), placeholder, TRUE);
	gtk_box_pack_start(GTK_BOX(box), over, TRUE, TRUE, 0);

	GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	g_signal_connect(buf, "changed", CL_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
		guarded("the notes", [&] { static_cast<NotesPane*>(self)->changed(); });
	}), this);
	// 20,000 characters: typing past that is refused (deleting works); a
	// paste is cut to fit, with a note.
	g_signal_connect(buf, "insert-text", CL_CALLBACK(+[](GtkTextBuffer* b, GtkTextIter* at, gchar* t, gint len, gpointer self) {
		NotesPane* n = static_cast<NotesPane*>(self);
		if (n->loading) return;
		const glong adding = g_utf8_strlen(t, len);
		const glong have = gtk_text_buffer_get_char_count(b);
		if (have + adding <= kMax) return;
		g_signal_stop_emission_by_name(b, "insert-text");
		const glong room = kMax - have;
		if (adding > 1 && room > 0) {
			const gchar* end = g_utf8_offset_to_pointer(t, room);
			n->loading = true;   // the cut text goes in without coming back here
			gtk_text_buffer_insert(b, at, t, (gint)(end - t));
			n->loading = false;
			n->changed();
			n->win->note("The notes hold " + withCommas(kMax) + " characters, so the paste was cut to fit.");
		} else {
			gtk_widget_error_bell(n->view);
			n->win->note("The notes are full (" + withCommas(kMax) + " characters).");
		}
	}), this);
	// The notes' own undo, before the window's (Ctrl+Z there is the circuit's).
	g_signal_connect(view, "key-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer self) -> gboolean {
		return static_cast<NotesPane*>(self)->key(e);
	}), this);
	gtk_widget_show_all(row);
	load();
}

bool NotesPane::shown() const { return gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)); }

void NotesPane::setShown(bool on) {
	gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), on);
	if (on) gtk_widget_grab_focus(view);
}

std::string NotesPane::text() const {
	GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter a, b;
	gtk_text_buffer_get_bounds(buf, &a, &b);
	gchar* s = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
	std::string out = s ? s : "";
	g_free(s);
	return out;
}

void NotesPane::setText(const std::string& s) {
	loading = true;
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), s.c_str(), (gint)s.size());
	loading = false;
	lastText = s;
	refresh();
}

void NotesPane::load() {
	CLDocument* doc = win->document();
	setText(doc ? cl_notes(doc) : "");
	undoStack.clear();
	redoStack.clear();
}

void NotesPane::setSample(const std::string& s) { setText(s); }

void NotesPane::refresh() {
	GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	const int n = gtk_text_buffer_get_char_count(buf);
	gtk_widget_set_visible(placeholder, n == 0);
	gtk_label_set_text(GTK_LABEL(counter), n > kCounterFrom ? (withCommas(n) + " / " + withCommas(kMax)).c_str() : "");
	GtkStyleContext* sc = gtk_widget_get_style_context(counter);
	if (n >= kMax) gtk_style_context_add_class(sc, "full");
	else gtk_style_context_remove_class(sc, "full");
}

void NotesPane::changed() {
	if (loading) return;
	const std::string now = text();
	if (now == lastText) { refresh(); return; }
	// A burst of typing is one undo step: a new one after a pause.
	const gint64 t = g_get_monotonic_time();
	if (undoStack.empty() || t - lastEdit > 1000000) undoStack.push_back(lastText);
	if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
	redoStack.clear();
	lastEdit = t;
	lastText = now;
	refresh();
	// Not an undo step of the circuit's: the document is marked edited and
	// saves itself (and syncs) as after any change.
	if (CLDocument* doc = win->document()) cl_notes_set(doc, now.c_str());
	win->markEdited();
}

bool NotesPane::key(GdkEventKey* e) {
	const guint mods = e->state & gtk_accelerator_get_default_mod_mask();
	const guint k = gdk_keyval_to_lower(e->keyval);
	const bool undo = mods == GDK_CONTROL_MASK && k == GDK_KEY_z;
	const bool redo = (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && k == GDK_KEY_z) || (mods == GDK_CONTROL_MASK && k == GDK_KEY_y);
	if (e->keyval == GDK_KEY_Escape && mods == 0) {
		// Back to the canvas.
		if (Canvas* c = win->currentCanvas()) gtk_widget_grab_focus(c->widget());
		return TRUE;
	}
	if (!undo && !redo) return FALSE;
	std::vector<std::string>& from = undo ? undoStack : redoStack;
	std::vector<std::string>& to = undo ? redoStack : undoStack;
	if (from.empty()) { gtk_widget_error_bell(view); return TRUE; }
	to.push_back(lastText);
	const std::string s = from.back();
	from.pop_back();
	setText(s);
	lastEdit = 0;
	if (CLDocument* doc = win->document()) cl_notes_set(doc, s.c_str());
	win->markEdited();
	return TRUE;
}

bool NotesPane::select(const std::string& q) {
	if (q.empty()) return false;
	GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkTextIter from, start, end, a, b;
	if (gtk_text_buffer_get_selection_bounds(buf, &a, &b)) from = b;
	else gtk_text_buffer_get_iter_at_mark(buf, &from, gtk_text_buffer_get_insert(buf));
	const GtkTextSearchFlags flags = (GtkTextSearchFlags)(GTK_TEXT_SEARCH_CASE_INSENSITIVE | GTK_TEXT_SEARCH_TEXT_ONLY);
	bool found = gtk_text_iter_forward_search(&from, q.c_str(), flags, &start, &end, nullptr);
	if (!found) {
		GtkTextIter top;
		gtk_text_buffer_get_start_iter(buf, &top);
		found = gtk_text_iter_forward_search(&top, q.c_str(), flags, &start, &end, nullptr);
	}
	if (!found) return false;
	gtk_text_buffer_select_range(buf, &start, &end);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view), &start, 0.1, FALSE, 0, 0);
	return true;
}

void NotesPane::themeChanged() { gtk_widget_queue_draw(revealer); }
