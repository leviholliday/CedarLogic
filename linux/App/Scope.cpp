// The oscilloscope and its timing diagrams (see ScopeWindow in Dialogs.h,
// and the Mac app's ScopeView.swift and TimingDiagram.swift).

#include "Chrome.h"
#include "Dialogs.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {

const float kHeader = 40, kNameWidth = 130, kLane = 30, kRuler = 22;
enum { kBtnHidden = 1, kBtnShare, kBtnOut, kBtnIn, kBtnLive, kBtnClear, kBtnClose };

const int kTickEvery[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };

int tickEvery(float pps, float minGap) {
	for (int e : kTickEvery) if (e * pps >= minGap) return e;
	return 10000;
}

const char* valueLabel(unsigned char v) {
	switch (v) { case 0: return "0"; case 1: return "1"; case 2: return "Z"; case 3: return "!"; case 4: return "?"; default: return "–"; }
}

Color valueColor(unsigned char v, Color dim) {
	switch (v) {
	case 1: return colorF(0.2f, 0.75f, 0.35f);
	case 2: return colorF(0.24f, 0.48f, 0.98f);
	case 3: return colorF(0.92f, 0.26f, 0.24f);
	case 4: return colorF(0.96f, 0.58f, 0.13f);
	default: return dim;
	}
}

void line(cairo_t* cr, float x0, float y0, float x1, float y1, const Color& c, float width, bool dashed = false) {
	cairo_save(cr);
	if (dashed) {
		const double d[] = { 2, 3 };
		cairo_set_dash(cr, d, 2, 0);
	}
	setColor(cr, c);
	cairo_set_line_width(cr, width);
	cairo_move_to(cr, x0, y0);
	cairo_line_to(cr, x1, y1);
	cairo_stroke(cr);
	cairo_restore(cr);
}

// ---- Timing diagrams ---------------------------------------------------------------
// The oscilloscope for a lab report: white paper, black traces, each
// signal's name, the step numbers along the bottom, and a title (with your
// name, as image exports have).

const float kTMargin = 28, kTName = 110, kTLane = 38, kTTitle = 46, kTAxis = 34;

void timingSize(int steps, int signals, float& w, float& h, float& pps) {
	const int n = std::max(1, steps);
	pps = std::min(24.0f, std::max(0.5f, 1400.0f / n));
	w = std::max(520.0f, kTMargin * 2 + kTName + n * pps);
	h = kTMargin * 2 + kTTitle + std::max(1, signals) * kTLane + kTAxis;
}

void drawTiming(cairo_t* cr, CLDocument* doc, const std::vector<int>& sigs, const std::vector<std::string>& names, int from, int count,
                const std::string& title, bool color) {
	float w, h, pps;
	timingSize(count, (int)sigs.size(), w, h, pps);
	const Color black = colorF(0, 0, 0), gray = colorF(0.33f, 0.33f, 0.33f), light = colorF(0.82f, 0.82f, 0.82f);
	drawText(cr, title, rectF(kTMargin, kTMargin, w - kTMargin, kTMargin + 22), 17, black, TextAlign::Leading, true);
	std::string byline = "Timing diagram";
	if (!prefs().studentName.empty()) byline += " · " + prefs().studentName;
	char date[64];
	const time_t t = time(nullptr);
	struct tm lt;
	localtime_r(&t, &lt);
	strftime(date, sizeof date, "%b %d, %Y", &lt);
	byline += std::string(" · ") + date;
	drawText(cr, byline, rectF(kTMargin, kTMargin + 25, w - kTMargin, kTMargin + 40), 11, gray);
	const float left = kTMargin + kTName, top = kTMargin + kTTitle, bottom = top + sigs.size() * kTLane;
	auto x = [&](int i) { return left + (i - from) * pps; };
	const int first = (int)cl_scope_first_step(doc);
	const int every = tickEvery(pps, 44);
	for (int i = from - ((from + first) % every); i <= from + count; i += every) {
		if (i < from) continue;
		line(cr, x(i), top, x(i), bottom, light, 0.5f, true);
		line(cr, x(i), bottom, x(i), bottom + 4, black, 0.5f);
		drawText(cr, format("%d", i + first), rectF(x(i) - 30, bottom + 6, x(i) + 30, bottom + 20), 9.5f, gray, TextAlign::Center);
	}
	line(cr, left, bottom, x(from + count), bottom, black, 1);
	drawText(cr, "step", rectF(left, bottom + 20, x(from + count), bottom + 34), 9.5f, gray, TextAlign::Center);
	std::vector<unsigned char> buf((size_t)std::max(1, count));
	const Color high = color ? colorF(0.85f, 0.95f, 0.87f) : colorF(0.9f, 0.9f, 0.9f);
	for (size_t row = 0; row < sigs.size(); row++) {
		const float laneTop = top + row * kTLane, hi = laneTop + 9, lo = laneTop + kTLane - 9;
		drawTextMid(cr, names[sigs[row]], rectF(kTMargin, (hi + lo) / 2 - 9, left - 12, (hi + lo) / 2 + 9), 12, black, TextAlign::Trailing, true);
		const int n = cl_scope_samples(doc, sigs[row], from, count, buf.data());
		float lastY = -1;
		for (int s = 0; s < n;) {
			const unsigned char v = buf[s];
			int e = s + 1;
			while (e < n && buf[e] == v) e++;
			const float a = x(from + s), c = x(from + e);
			if (v == 0 || v == 1) {
				const float y = v == 1 ? hi : lo;
				if (v == 1) fillRect(cr, rectF(a, hi, c, lo), high);
				if (lastY >= 0 && lastY != y) line(cr, a, lastY, a, y, black, 1.5f);
				line(cr, a, y, c, y, black, 1.5f);
				lastY = y;
			} else if (v == 2) {
				line(cr, a, (hi + lo) / 2, c, (hi + lo) / 2, color ? colorF(0.24f, 0.48f, 0.98f) : black, 1, true);
				lastY = -1;
			} else if (v == 3 || v == 4) {
				const Color hc = color ? (v == 3 ? colorF(0.92f, 0.26f, 0.24f) : colorF(0.96f, 0.58f, 0.13f)) : colorF(0.45f, 0.45f, 0.45f);
				cairo_save(cr);
				cairo_rectangle(cr, a, hi, c - a, lo - hi);
				cairo_clip(cr);
				for (float hx = a - (lo - hi); hx < c; hx += 5) line(cr, hx, lo, hx + (lo - hi), hi, hc, 0.6f);
				cairo_restore(cr);
				cairo_rectangle(cr, a, hi, c - a, lo - hi);
				setColor(cr, color ? hc : black);
				cairo_set_line_width(cr, 0.8);
				cairo_stroke(cr);
				lastY = -1;
			} else {
				lastY = -1;
			}
			s = e;
		}
	}
}

}  // namespace

ScopeWindow::ScopeWindow(CircuitWindow* o) : owner(o) {
	// Docked under the canvas, as on the Mac (the window puts it there).
	win = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_ref_sink(win);
	gtk_widget_set_size_request(win, -1, 140);
	area = gtk_drawing_area_new();
	gtk_widget_set_can_focus(area, TRUE);
	gtk_widget_add_events(area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
	                                GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(win), area);
	g_signal_connect(area, "draw", CL_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer self) -> gboolean {
		guarded("the oscilloscope", [&] {
			static_cast<ScopeWindow*>(self)->paint(cr, (float)gtk_widget_get_allocated_width(a), (float)gtk_widget_get_allocated_height(a));
		});
		return TRUE;
	}), this);
	g_signal_connect(area, "motion-notify-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventMotion* e, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		if (s->dragging) { s->setCursorAt((float)e->x, (float)gtk_widget_get_allocated_width(a)); return TRUE; }
		int i = -1;
		for (size_t k = 0; k < s->buttons.size(); k++) if (inRect(s->buttons[k].r, (float)e->x, (float)e->y)) i = (int)k;
		if (i != s->hot) { s->hot = i; s->update(); }
		return TRUE;
	}), this);
	g_signal_connect(area, "leave-notify-event", CL_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		s->hot = -1;
		s->update();
		return FALSE;
	}), this);
	g_signal_connect(area, "button-press-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventButton* e, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		gtk_widget_grab_focus(a);
		if (e->type != GDK_BUTTON_PRESS || e->button != 1) return TRUE;
		const float x = (float)e->x, y = (float)e->y;
		guarded("the oscilloscope", [&] {
			for (const Button& b : s->buttons) if (inRect(b.r, x, y)) { s->press(b.id, (GdkEvent*)e); return; }
			if (y < kHeader) return;
			// A name chooses its signal; the traces move the cursor.
			if (x < kNameWidth) {
				const std::vector<int> shown = s->shownSignals();
				const int row = (int)((y - kHeader - kRuler + s->scrollY) / kLane);
				if (row >= 0 && row < (int)shown.size()) s->chosen = shown[row];
				s->update();
				return;
			}
			s->dragging = true;
			s->setCursorAt(x, (float)gtk_widget_get_allocated_width(a));
		});
		return TRUE;
	}), this);
	g_signal_connect(area, "button-release-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer self) -> gboolean {
		static_cast<ScopeWindow*>(self)->dragging = false;
		return TRUE;
	}), this);
	g_signal_connect(area, "scroll-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventScroll* e, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		double d = 0;
		if (e->direction == GDK_SCROLL_SMOOTH) { double dx = 0; gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &d); }
		else if (e->direction == GDK_SCROLL_UP) d = -1;
		else if (e->direction == GDK_SCROLL_DOWN) d = 1;
		if (e->state & GDK_CONTROL_MASK) {
			// A wheel's click is a quarter; a touchpad zooms as far as it moves
			// (and its lift-off, which moves nothing, not at all).
			if (d == 0) return TRUE;
			s->pointsPerStep = std::min(48.0f, std::max(0.25f, s->pointsPerStep * (float)std::pow(1.25, -d)));
		} else {
			const float lanes = s->shownSignals().size() * kLane, view = gtk_widget_get_allocated_height(a) - kHeader - kRuler;
			s->scrollY = std::max(0.0f, std::min(s->scrollY + (float)d * 40, std::max(0.0f, lanes - view)));
		}
		s->update();
		return TRUE;
	}), this);
	g_signal_connect(area, "key-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer self) -> gboolean {
		ScopeWindow* s = static_cast<ScopeWindow*>(self);
		return guarded("the oscilloscope", [&] { return s->key(e->keyval, e->state); }) ? TRUE : FALSE;
	}), this);
	// Closing hides it, kept for next time.
}

ScopeWindow::~ScopeWindow() {
	g_signal_handlers_disconnect_by_data(area, this);
	if (GtkWidget* parent = gtk_widget_get_parent(win)) gtk_container_remove(GTK_CONTAINER(parent), win);
	g_object_unref(win);
}

void ScopeWindow::present() {
	gtk_widget_show_all(win);
	gtk_widget_grab_focus(area);
	update();
}

void ScopeWindow::close() { gtk_widget_hide(win); }
bool ScopeWindow::visible() const { return gtk_widget_get_visible(win); }
void ScopeWindow::update() { gtk_widget_queue_draw(area); }

std::vector<std::string> ScopeWindow::signals() const {
	std::vector<std::string> out;
	CLDocument* doc = owner->document();
	for (int i = 0; i < cl_scope_signal_count(doc); i++) out.push_back(cl_scope_signal(doc, i));
	return out;
}

std::vector<int> ScopeWindow::shownSignals() const {
	std::vector<int> out;
	const std::vector<std::string> all = signals();
	for (int i = 0; i < (int)all.size(); i++)
		if (std::find(hidden.begin(), hidden.end(), all[i]) == hidden.end()) out.push_back(i);
	return out;
}

void ScopeWindow::window(float width, int length, int& start, int& count) const {
	count = std::max(1, (int)((width - kNameWidth) / pointsPerStep));
	int end = length;
	if (cursor >= 0 && cursor < length - count / 2) end = std::min(length, std::max(cursor + count / 2, count));
	start = std::max(0, end - count);
}

void ScopeWindow::setCursorAt(float x, float width) {
	const int length = (int)cl_scope_length(owner->document());
	if (length <= 0 || x <= kNameWidth) return;
	int start, count;
	window(width, length, start, count);
	cursor = std::min(length - 1, std::max(0, start + (int)((x - kNameWidth) / pointsPerStep)));
	update();
}

void ScopeWindow::paint(cairo_t* cr, float w, float h) {
	const Chrome c = chrome();
	const bool dark = c.dark;
	const Color ink = c.barInk(), dim = withAlpha(ink, 0.55f), accent = c.accent();
	const Color green = colorF(0.2f, 0.75f, 0.35f);
	CLDocument* doc = owner->document();
	const std::vector<std::string> names = signals();
	const std::vector<int> shown = shownSignals();
	const int length = (int)cl_scope_length(doc);
	fillRect(cr, rectF(0, 0, w, h), dark ? c.canvas() : colorF(1, 1, 1));

	// The header: the name, where the cursor is, and the tools.
	fillRect(cr, rectF(0, 0, w, kHeader), c.bar());
	fillRect(cr, rectF(0, kHeader - 1, w, kHeader), c.hairline());
	drawTextMid(cr, "Oscilloscope", rectF(14, 0, 140, kHeader), 13, ink, TextAlign::Leading, true);
	const std::string where = cursor >= 0 && cursor < length ? format("step %lld", cl_scope_first_step(doc) + cursor) : std::string("live");
	drawTextMid(cr, where, rectF(124, 0, 260, kHeader), 12, dim);
	buttons.clear();
	float bx = w - 10;
	auto button = [&](int id, const char* icon, const std::string& label) {
		const float bw = !label.empty() ? textWidth(label, 12) + 22 : 32;
		bx -= bw;
		const RectF r = rectF(bx, 7, bx + bw, kHeader - 7);
		if (hot == (int)buttons.size()) fillRound(cr, r, 7, withAlpha(ink, 0.08f));
		if (!label.empty()) drawTextMid(cr, label, r, 12, ink, TextAlign::Center);
		else if (id == kBtnIn || id == kBtnOut) drawZoomIcon(cr, r, withAlpha(ink, 0.85f), id == kBtnIn);
		else drawIcon(cr, icon, r, 14, withAlpha(ink, 0.85f));
		buttons.push_back({ r, id });
		bx -= 4;
	};
	button(kBtnClose, Icon::Dismiss, "");
	button(kBtnClear, "user-trash-symbolic", "");
	button(kBtnLive, "go-last-symbolic", "");
	button(kBtnIn, Icon::ZoomIn, "");
	button(kBtnOut, Icon::ZoomOut, "");
	button(kBtnShare, "document-send-symbolic", "");
	if (!hidden.empty()) button(kBtnHidden, nullptr, format("%d hidden", (int)hidden.size()));

	if (names.empty()) {
		drawTextMid(cr, "Add a TO label to a wire, and its signal shows up here.", rectF(0, kHeader, w, h), 12.5f, dim, TextAlign::Center);
		return;
	}

	int start, count;
	window(w, length, start, count);
	shownStart = start;
	shownCount = std::min(count, std::max(0, length - start));
	const float pps = pointsPerStep, x0 = kNameWidth;
	auto x = [&](int i) { return x0 + (i - start) * pps; };
	const float top0 = kHeader;
	cairo_save(cr);
	cairo_rectangle(cr, 0, top0, w, h - top0);
	cairo_clip(cr);

	const int first = (int)cl_scope_first_step(doc);
	const int every = tickEvery(pps, 60);
	for (int i = start - ((start + first) % every); i <= start + count; i += every) {
		if (i < start) continue;
		fillRect(cr, rectF(x(i), top0 + kRuler - 6, x(i) + 1, h), withAlpha(ink, 0.08f));
		drawText(cr, format("%d", i + first), rectF(x(i) + 3, top0 + 4, x(i) + 70, top0 + 18), 10, dim);
	}

	std::vector<unsigned char> buf((size_t)count + 1);
	const int cursorIndex = cursor >= 0 ? std::min(cursor, length - 1) : length - 1;
	for (size_t row = 0; row < shown.size(); row++) {
		const int sig = shown[row];
		const float top = top0 + kRuler + row * kLane - scrollY;
		if (top + kLane < top0 + kRuler || top > h) continue;
		const float hi = top + 7, lo = top + kLane - 7;
		const bool isChosen = sig == chosen;
		if (isChosen) fillRect(cr, rectF(0, top, w, top + kLane), withAlpha(accent, 0.08f));
		unsigned char value = 255;
		if (length > 0) cl_scope_samples(doc, sig, cursorIndex, 1, &value);
		drawTextMid(cr, names[sig], rectF(10, top, kNameWidth - 30, top + kLane), 12.5f, ink, TextAlign::Leading, isChosen);
		drawTextMid(cr, valueLabel(value), rectF(kNameWidth - 30, top, kNameWidth - 10, top + kLane), 12.5f, valueColor(value, dim),
		            TextAlign::Trailing, true);
		const int n = length > 0 ? cl_scope_samples(doc, sig, start, count, buf.data()) : 0;
		float lastY = -1;
		for (int s = 0; s < n;) {
			const unsigned char v = buf[s];
			int e = s + 1;
			while (e < n && buf[e] == v) e++;
			const float a = x(start + s), b = x(start + e);
			if (v == 0 || v == 1) {
				const float y = v == 1 ? hi : lo;
				if (v == 1) fillRect(cr, rectF(a, hi, b, lo), withAlpha(green, 0.10f));
				if (lastY >= 0 && lastY != y) line(cr, a, lastY, a, y, green, 1.6f);
				line(cr, a, y, b, y, green, 1.6f);
				lastY = y;
			} else if (v == 2) {
				line(cr, a, (hi + lo) / 2, b, (hi + lo) / 2, valueColor(2, dim), 1.5f);
				lastY = -1;
			} else if (v == 3 || v == 4) {
				fillRect(cr, rectF(a, hi, b, lo), withAlpha(valueColor(v, dim), 0.25f));
				lastY = -1;
			} else {
				lastY = -1;
			}
			s = e;
		}
		fillRect(cr, rectF(0, top + kLane - 1, w, top + kLane), withAlpha(ink, 0.07f));
	}
	fillRect(cr, rectF(kNameWidth, top0, kNameWidth + 1, h), withAlpha(ink, 0.15f));
	if (cursor >= start && cursor <= start + count) {
		const float cx = x(cursor) + pps / 2;
		fillRect(cr, rectF(cx - 0.75f, top0, cx + 0.75f, h), accent);
	}
	cairo_restore(cr);
}

void ScopeWindow::press(int id, GdkEvent* e) {
	CLDocument* doc = owner->document();
	switch (id) {
	case kBtnOut: pointsPerStep = std::max(0.25f, pointsPerStep / 1.5f); break;
	case kBtnIn: pointsPerStep = std::min(48.0f, pointsPerStep * 1.5f); break;
	case kBtnLive: cursor = -1; break;
	case kBtnClear: cl_scope_clear(doc); cursor = -1; break;
	case kBtnClose: close(); return;
	case kBtnShare: exportMenu(e); break;
	case kBtnHidden: hiddenMenu(e); break;
	default: break;
	}
	update();
}

namespace {

// A popup menu of labelled items; the index chosen, or -1.
int popup(const std::vector<std::string>& items, GdkEvent* e) {
	GtkWidget* menu = gtk_menu_new();
	int chosen = -1;
	GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
	for (size_t i = 0; i < items.size(); i++) {
		GtkWidget* it;
		if (items[i].empty()) it = gtk_separator_menu_item_new();
		else if (items[i][0] == '\x01' || items[i][0] == '\x02') {
			it = gtk_check_menu_item_new_with_label(items[i].c_str() + 1);
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), items[i][0] == '\x02');
		} else it = gtk_menu_item_new_with_label(items[i].c_str());
		g_object_set_data(G_OBJECT(it), "index", GINT_TO_POINTER((int)i));
		g_signal_connect(it, "activate", CL_CALLBACK(+[](GtkMenuItem* mi, gpointer out) {
			*static_cast<int*>(out) = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "index"));
		}), &chosen);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
	}
	g_signal_connect_swapped(menu, "deactivate", G_CALLBACK(+[](GMainLoop* l) {
		g_idle_add([](gpointer lp) -> gboolean { g_main_loop_quit(static_cast<GMainLoop*>(lp)); return FALSE; }, l);
	}), loop);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), e);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	gtk_widget_destroy(menu);
	return chosen;
}

}  // namespace

void ScopeWindow::hiddenMenu(GdkEvent* e) {
	std::vector<std::string> items;
	for (const std::string& name : hidden) items.push_back("Show " + name);
	items.push_back("");
	items.push_back("Show All");
	const int i = popup(items, e);
	if (i == (int)items.size() - 1) hidden.clear();
	else if (i >= 0 && i < (int)hidden.size()) hidden.erase(hidden.begin() + i);
}

cairo_surface_t* ScopeWindow::timingImage() {
	CLDocument* doc = owner->document();
	const int length = (int)cl_scope_length(doc);
	int from = prefs().timingWhole ? 0 : shownStart, count = prefs().timingWhole ? length : shownCount;
	if (count <= 0) { from = 0; count = length; }
	const std::vector<int> sigs = shownSignals();
	if (sigs.empty() || count <= 0) return nullptr;
	const std::vector<std::string> names = signals();
	float tw, th, pps;
	timingSize(count, (int)sigs.size(), tw, th, pps);
	// Sharp at 2x, but a long recording only as large as an image can be
	// (32767 pixels a side).
	const double scale = std::min(2.0, 32000.0 / std::max(tw, th));
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)std::ceil(tw * scale), (int)std::ceil(th * scale));
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return nullptr;
	}
	cairo_t* cr = cairo_create(s);
	cairo_scale(cr, scale, scale);
	fillRect(cr, rectF(0, 0, tw, th), colorF(1, 1, 1));
	drawTiming(cr, doc, sigs, names, from, count, owner->titleText(), prefs().timingInColor);
	cairo_destroy(cr);
	return s;
}

// A timing diagram for a lab report: copied, or saved as a PNG, of what's on
// screen or the whole recording, in colour or black and white.
void ScopeWindow::exportMenu(GdkEvent* e) {
	for (;;) {
		const std::vector<std::string> items = { "Copy as Image", "Save as PNG…", "",
		                                         std::string(1, prefs().timingWhole ? '\x02' : '\x01') + "Whole recording",
		                                         std::string(1, prefs().timingInColor ? '\x02' : '\x01') + "In color" };
		const int i = popup(items, e);
		// The two options flip and the menu comes back, as the Mac's panel stays.
		if (i == 3) { prefs().timingWhole = !prefs().timingWhole; prefs().save(); continue; }
		if (i == 4) { prefs().timingInColor = !prefs().timingInColor; prefs().save(); continue; }
		if (i != 0 && i != 1) return;
		cairo_surface_t* img = timingImage();
		if (img == nullptr) { gtk_widget_error_bell(area); return; }
		if (i == 0) {
			GdkPixbuf* pb = gdk_pixbuf_get_from_surface(img, 0, 0, cairo_image_surface_get_width(img), cairo_image_surface_get_height(img));
			if (pb) {
				gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pb);
				g_object_unref(pb);
				owner->note("Timing diagram copied. Paste it into your report.");
			}
		} else {
			const std::string file = chooseImageFile(GTK_WINDOW(gtk_widget_get_toplevel(area)), "Save Timing Diagram", "_Save",
			                                         safeFileName(owner->titleText() + " timing") + ".png", false);
			if (!file.empty()) {
				if (cairo_surface_write_to_png(img, file.c_str()) != CAIRO_STATUS_SUCCESS)
					showMessage(GTK_WINDOW(gtk_widget_get_toplevel(area)), GTK_MESSAGE_WARNING, "Couldn't save it there", "Try another folder.");
			}
		}
		cairo_surface_destroy(img);
		return;
	}
}

bool ScopeWindow::key(guint k, guint state) {
	CLDocument* doc = owner->document();
	const int length = (int)cl_scope_length(doc);
	const bool shift = (state & GDK_SHIFT_MASK) != 0, alt = (state & GDK_MOD1_MASK) != 0, ctrl = (state & GDK_CONTROL_MASK) != 0;
	const std::vector<int> shown = shownSignals();
	auto moveCursor = [&](int delta) {
		if (length <= 0) return;
		const int at = cursor >= 0 ? cursor : length - 1;
		cursor = std::min(length - 1, std::max(0, at + delta));
	};
	auto jump = [&](int dir) {
		if (length <= 0) return;
		const int at = cursor >= 0 ? cursor : length - 1;
		unsigned char here = 255;
		cl_scope_samples(doc, chosen, at, 1, &here);
		for (int i = at + dir; i >= 0 && i < length; i += dir) {
			unsigned char v = 255;
			cl_scope_samples(doc, chosen, i, 1, &v);
			if (v != here) { cursor = i; return; }
		}
		cursor = dir < 0 ? 0 : length - 1;
	};
	switch (gdk_keyval_to_lower(k)) {
	case GDK_KEY_Escape: close(); return true;
	case GDK_KEY_g: if (ctrl) { close(); gtk_window_present(owner->window()); return true; } return false;
	case GDK_KEY_Left: if (alt) jump(-1); else moveCursor(shift ? -10 : -1); break;
	case GDK_KEY_Right: if (alt) jump(1); else moveCursor(shift ? 10 : 1); break;
	case GDK_KEY_Up: case GDK_KEY_Down: {
		if (shown.empty()) return true;
		auto it = std::find(shown.begin(), shown.end(), chosen);
		int i = it == shown.end() ? 0 : (int)(it - shown.begin());
		i = std::max(0, std::min((int)shown.size() - 1, i + (k == GDK_KEY_Up ? -1 : 1)));
		chosen = shown[i];
		break;
	}
	case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add: pointsPerStep = std::min(48.0f, pointsPerStep * 1.5f); break;
	case GDK_KEY_minus: case GDK_KEY_KP_Subtract: pointsPerStep = std::max(0.25f, pointsPerStep / 1.5f); break;
	case GDK_KEY_Home: cursor = length > 0 ? 0 : -1; break;
	case GDK_KEY_End: cursor = -1; break;
	case GDK_KEY_h: {
		const std::vector<std::string> names = signals();
		if (chosen < 0 || chosen >= (int)names.size()) return true;
		auto it = std::find(hidden.begin(), hidden.end(), names[chosen]);
		if (it != hidden.end()) hidden.erase(it);
		else {
			hidden.push_back(names[chosen]);
			const std::vector<int> left = shownSignals();
			if (!left.empty()) chosen = left.front();
		}
		break;
	}
	case GDK_KEY_c: if (ctrl) return false; cl_scope_clear(doc); cursor = -1; break;
	case GDK_KEY_space: owner->toggleRunning(); break;
	default: return false;
	}
	update();
	return true;
}
