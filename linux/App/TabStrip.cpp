// The tabs over the canvas (see TabStrip.h).

#include "TabStrip.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {
const float kCardH = 26, kMinW = 72, kMaxW = 220, kGap = 4;
}

TabStrip::TabStrip(CircuitWindow* window) : win(window) {
	create();
	gtk_widget_set_size_request(area, -1, (int)stripHeight());
}

void TabStrip::layout(float w) {
	const int n = win->tabCount();
	cards.assign((size_t)n, RectF{});
	std::vector<float> want((size_t)n);
	float total = 0;
	for (int i = 0; i < n; i++) {
		want[i] = std::min(kMaxW, std::max(kMinW + 20, textWidth(win->tabName(i), 12, true) + 50));
		total += want[i] + kGap;
	}
	// When they don't all fit, the others are squeezed; the one in front
	// keeps its name readable.
	const float room = w - 8 - 40;
	const int front = win->currentTab();
	float others = 0;
	for (int i = 0; i < n; i++) if (i != front) others += want[i] + kGap;
	const float frontW = front >= 0 && front < n ? want[front] + kGap : 0;
	const float k = total > room && others > 0 ? std::max(kMinW / kMaxW, (room - frontW) / others) : 1.0f;
	const float y = std::floor((stripHeight() - kCardH) / 2) + 1;
	float x = 8;
	for (int i = 0; i < n; i++) {
		const float cw = i == front ? want[i] : std::max(kMinW, std::floor(want[i] * k));
		cards[i] = rectF(x, y, x + cw, y + kCardH);
		x += cw + kGap;
	}
	plus = rectF(x + 2, y, x + 2 + kCardH, y + kCardH);
}

RectF TabStrip::closeRect(int i) const {
	const RectF& c = cards[i];
	const float cy = (c.top + c.bottom) / 2;
	return rectF(c.right - 22, cy - 8, c.right - 6, cy + 8);
}

int TabStrip::tabAt(float x, float y) const {
	for (int i = 0; i < (int)cards.size(); i++) if (inRect(cards[i], x, y)) return i;
	return -1;
}

void TabStrip::paint(cairo_t* cr, float w, float h) {
	layout(w);
	const Chrome c = chrome();
	const bool dark = c.dark;
	fillRect(cr, rectF(0, 0, w, h), c.tabBar());
	fillRect(cr, rectF(0, h - 1, w, h), c.hairline());
	const Color ink = c.tabInk();
	const int current = win->currentTab();
	const int n = (int)cards.size();
	const Color green = dark ? rgb255(64, 214, 110) : rgb255(36, 168, 76);

	auto drawCard = [&](int i, RectF r) {
		const bool front = i == current;
		if (front) {
			fillRound(cr, rectF(r.left, r.top + 1, r.right, r.bottom + 1), 8, colorF(0, 0, 0, dark ? 0.26f : 0.10f));
			fillRound(cr, r, 8, dark ? rgb255(44, 48, 56) : colorF(1, 1, 1, 1));
			strokeRound(cr, r, 8, withAlpha(ink, dark ? 0.18f : 0.10f));
		} else if (i == hot || (dragging && i == pressed)) {
			fillRound(cr, r, 8, withAlpha(ink, 0.06f));
		}
		const float cy = (r.top + r.bottom) / 2;
		fillCircle(cr, pointF(r.left + 13, cy), 3, front && win->running() ? green : withAlpha(ink, front ? 0.35f : 0.22f));
		const bool showClose = n > 1 && (front || i == hot);
		const float textRight = r.right - (showClose ? 26 : 10);
		drawText(cr, win->tabName(i), rectF(r.left + 23, cy - 8, textRight, cy + 10), 12, withAlpha(ink, front ? 1.0f : 0.62f),
		         TextAlign::Leading, front);
		if (showClose) {
			const RectF x = closeRect(i);
			if (i == hotClose) fillRound(cr, x, 5, withAlpha(ink, 0.16f));
			drawIcon(cr, Icon::Dismiss, x, 10, withAlpha(ink, i == hotClose ? 0.9f : 0.5f));
		}
	};
	for (int i = 0; i < n; i++) {
		if (dragging && i == pressed) continue;
		drawCard(i, cards[i]);
	}
	// The one being dragged, under the pointer.
	if (dragging && pressed >= 0 && pressed < n) {
		const float cw = cards[pressed].right - cards[pressed].left;
		const float left = std::max(4.0f, std::min(w - cw - 4, dragX - grabOffset));
		drawCard(pressed, rectF(left, cards[pressed].top, left + cw, cards[pressed].bottom));
	}
	if (plusHot) fillRound(cr, plus, 9, withAlpha(ink, 0.08f));
	drawIcon(cr, Icon::Add, plus, 14, withAlpha(ink, 0.75f));

	std::vector<Tip> tips;
	tips.push_back({ plus, "New tab (Ctrl+T)" });
	for (int i = 0; i < n; i++) tips.push_back({ cards[i], "Double-click to rename; drag to move" });
	setTips(tips);
}

void TabStrip::mouseMove(float x, float y) {
	if (pressed >= 0 && !dragging && std::fabs(x - pressX) > 5) dragging = true;
	if (dragging) {
		dragX = x;
		// Move it when its middle passes a neighbour's.
		const float cw = cards[pressed].right - cards[pressed].left;
		const float mid = dragX - grabOffset + cw / 2;
		int to = pressed;
		for (int i = 0; i < (int)cards.size(); i++) {
			const float m = (cards[i].left + cards[i].right) / 2;
			if (i < pressed && mid < m) { to = i; break; }
			if (i > pressed && mid > m) to = i;
		}
		if (to != pressed) {
			win->moveTab(pressed, to);
			pressed = to;
			layout(width());
		}
		redraw();
		return;
	}
	const int t = tabAt(x, y);
	const int tc = t >= 0 && inRect(closeRect(t), x, y) ? t : -1;
	const bool ph = inRect(plus, x, y);
	if (t != hot || tc != hotClose || ph != plusHot) {
		hot = t;
		hotClose = tc;
		plusHot = ph;
		redraw();
	}
}

void TabStrip::mouseLeave() {
	if (hot != -1 || hotClose != -1 || plusHot) {
		hot = hotClose = -1;
		plusHot = false;
		redraw();
	}
}

void TabStrip::mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) {
	const int t = tabAt(x, y);
	if (button == 3) {
		win->tabContextMenu(t, (GdkEvent*)e);
		return;
	}
	if (button == 2) {
		if (t >= 0) win->closeTab(t);
		return;
	}
	if (button != 1) return;
	if (inRect(plus, x, y)) { win->runAction("win.new-tab"); return; }
	if (t < 0) return;
	if (doubleClick) {
		win->showTab(t);
		win->renamePage(win->currentPage());
		return;
	}
	if (cards.size() > 1 && inRect(closeRect(t), x, y) && (t == win->currentTab() || t == hot)) {
		win->closeTab(t);
		hot = hotClose = -1;
		return;
	}
	win->showTab(t);
	pressed = t;
	pressX = x;
	dragX = x;
	grabOffset = x - cards[t].left;
	redraw();
}

void TabStrip::mouseUp(int button, float, float) {
	if (button != 1) return;
	pressed = -1;
	dragging = false;
	redraw();
}
