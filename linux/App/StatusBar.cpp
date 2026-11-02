// The status bar (see StatusBar.h).

#include "StatusBar.h"
#include "Canvas.h"
#include "Window.h"

namespace {
const double kNoteSeconds = 4;
}

StatusBar::StatusBar(CircuitWindow* window) : win(window) {
	create();
	gtk_widget_set_size_request(area, -1, (int)barHeight());
	revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
	gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 200);
	gtk_container_add(GTK_CONTAINER(revealer), area);
	gtk_widget_show(area);
	gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), prefs().showStatus);
}

void StatusBar::note(const std::string& text) {
	message = text;
	messageAt = anim::now();
	if (!text.empty()) {
		shownMessage = text;
		fade.set(0);
		fade.go(1, 0.2);
		animate();
	}
	showOrHide();
	redraw();
}

void StatusBar::update() { redraw(); }

void StatusBar::settingsChanged() { showOrHide(); redraw(); }

void StatusBar::tick() {
	if (message.empty() || anim::now() - messageAt < kNoteSeconds) return;
	message.clear();
	fade.go(0, 0.3);
	animate();
	showOrHide();
}

void StatusBar::showOrHide() {
	const bool want = prefs().showStatus || !message.empty();
	if (gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)) != want) gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), want);
}

void StatusBar::paint(cairo_t* cr, float w, float h) {
	const Chrome c = chrome();
	fillRect(cr, rectF(0, 0, w, h), c.tabBar());
	fillRect(cr, rectF(0, 0, w, 1), withAlpha(c.tabInk(), 0.10f));
	const Color ink = withAlpha(c.tabInk(), 0.6f);
	float right = w - 12;
	if (prefs().showStatus) {
		CLDocument* doc = win->document();
		const int p = win->currentPage();
		Canvas* cv = win->currentCanvas();
		const int gates = cl_document_gate_count(doc, p);
		const int sel = cl_edit_selected_gate_count(doc, p) + cl_edit_selected_wire_count(doc, p);
		std::string counts = format("%d gate%s", gates, gates == 1 ? "" : "s");
		if (sel > 0) counts += format(" · %d selected", sel);
		const std::string items[] = { counts, format("x %.1f   y %.1f", win->pointerX, win->pointerY),
		                              format("%d%%", cv ? cv->zoomPercent() : 100) };
		for (const std::string& s : items) {
			const float tw = textWidth(s, 11);
			drawTextMid(cr, s, rectF(right - tw, 0, right, h), 11, ink);
			right -= tw + 18;
		}
	}
	const float a = (float)fade.value();
	if (a > 0.01f && !shownMessage.empty()) {
		fillCircle(cr, pointF(16, h / 2), 3, withAlpha(c.accent(), a));
		drawTextMid(cr, shownMessage, rectF(26, 0, right - 12, h), 11, withAlpha(c.tabInk(), 0.75f * a));
	}
}
