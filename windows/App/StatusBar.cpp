// The status bar (see StatusBar.h).

#include "StatusBar.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

const double kNoteSeconds = 4;

// Windows' "Show animations" off: no easing, only the end of it.
bool calm() {
	BOOL animations = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
	return !animations;
}

}  // namespace

double StatusBar::Tween::value() const {
	const double t = time > 0 ? std::min(1.0, std::max(0.0, (nowSeconds() - at) / time)) : 1;
	return from + (to - from) * (1 - std::pow(1 - t, 3));
}

bool StatusBar::Tween::active() const { return nowSeconds() - at < time; }

void StatusBar::Tween::go(double target, double seconds) {
	from = value();
	to = target;
	at = nowSeconds();
	time = calm() ? 0 : seconds;
}

StatusBar::StatusBar(CircuitWindow* window, HWND parent) : win(window) {
	create(parent);
	slide.set(prefs().showStatus ? 1 : 0);
}

double StatusBar::shown() const { return prefs().showStatus ? 1 : slide.value(); }

void StatusBar::note(const std::string& text) {
	message = text;
	messageAt = nowSeconds();
	if (!text.empty()) {
		shownMessage = text;
		fade.set(0);
		fade.go(1, 0.2);
		if (slide.to < 1) slide.go(1, 0.2);
	}
	redraw();
}

bool StatusBar::tick() {
	if (!message.empty() && nowSeconds() - messageAt >= kNoteSeconds) {
		message.clear();
		fade.go(0, 0.3);
	}
	// Off, it goes once the note has faded.
	if (message.empty() && !fade.active() && slide.to > 0 && !prefs().showStatus) slide.go(0, 0.2);
	if (prefs().showStatus) slide.set(1);
	if (fade.active()) redraw();
	// Laid out each step, and once more at the end.
	const bool was = sliding;
	sliding = slide.active();
	return sliding || was;
}

void StatusBar::paint(ID2D1RenderTarget* rt, float w, float h) {
	const Chrome c = chrome();
	fillRect(rt, D2D1::RectF(0, 0, w, h), c.tabBar());
	// Drawn whole (sliding up, its top shows first).
	const float hh = barHeight();
	fillRect(rt, D2D1::RectF(0, 0, w, 1 / (float)scale()), withAlpha(c.tabInk(), 0.10f));
	const D2D1_COLOR_F ink = withAlpha(c.tabInk(), 0.6f);
	float right = w - 12;
	if (prefs().showStatus) {
		CLDocument* doc = win->document();
		const int p = win->currentPage();
		Canvas* cv = win->currentCanvas();
		const int gates = cl_document_gate_count(doc, p);
		const int sel = cl_edit_selected_gate_count(doc, p) + cl_edit_selected_wire_count(doc, p);
		std::string counts = strf("%d gate%s", gates, gates == 1 ? "" : "s");
		if (sel > 0) counts += strf(" · %d selected", sel);
		double px = 0, py = 0;
		win->pointer(px, py);
		const std::string items[] = { counts, strf("x %.1f   y %.1f", px, py), strf("%d%%", cv ? cv->zoomPercent() : 100) };
		for (const std::string& s : items) {
			const float tw = textWidth(s, 11);
			drawText(rt, s, D2D1::RectF(right - tw - 1, 0, right + 1, hh), 11, ink);
			right -= tw + 18;
		}
	}
	// The note rises a little as it fades in.
	const float a = (float)fade.value();
	if (a > 0.01f && !shownMessage.empty()) {
		const float rise = fade.to > 0 ? (1 - a) * 5 : 0;
		fillCircle(rt, D2D1::Point2F(16, hh / 2 + rise), 3, withAlpha(c.accent(), a));
		drawText(rt, shownMessage, D2D1::RectF(26, rise, std::max(26.0f, right - 12), hh + rise), 11, withAlpha(c.tabInk(), 0.75f * a));
	}
}
