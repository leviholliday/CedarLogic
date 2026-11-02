// A side's tabs (see TabStrip.h).

#include "TabStrip.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {
const float kTabH = 30, kGap = 2, kEdge = 8, kPlusW = 26;

void linear(cairo_t* cr, const RectF& r, float radius, const Color& top, const Color& bottom) {
	cairo_pattern_t* g = cairo_pattern_create_linear(0, r.top, 0, r.bottom);
	cairo_pattern_add_color_stop_rgba(g, 0, top.r, top.g, top.b, top.a);
	cairo_pattern_add_color_stop_rgba(g, 1, bottom.r, bottom.g, bottom.b, bottom.a);
	cairo_save(cr);
	if (radius > 0) roundedPath(cr, r, radius);
	else cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_restore(cr);
	cairo_pattern_destroy(g);
}
}  // namespace

TabStrip::TabStrip(CircuitWindow* window, int paneIndex) : win(window), pane(paneIndex) {
	create();
	gtk_widget_set_size_request(area, -1, (int)stripHeight());
	overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(overlay), area);
	// The name, renamed in place.
	entry = gtk_entry_new();
	gtk_widget_set_name(entry, "tab-rename");
	gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
	gtk_widget_set_halign(entry, GTK_ALIGN_START);
	gtk_widget_set_valign(entry, GTK_ALIGN_START);
	gtk_widget_set_no_show_all(entry, TRUE);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), entry);
	g_signal_connect(entry, "activate", CL_CALLBACK(+[](GtkEntry*, gpointer self) { static_cast<TabStrip*>(self)->commitRename(true); }), this);
	g_signal_connect(entry, "key-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer self) -> gboolean {
		if (e->keyval != GDK_KEY_Escape) return FALSE;
		static_cast<TabStrip*>(self)->commitRename(false);
		return TRUE;
	}), this);
	g_signal_connect(entry, "focus-out-event", CL_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer self) -> gboolean {
		static_cast<TabStrip*>(self)->commitRename(true);
		return FALSE;
	}), this);
	hover.in = 0.14;
	hover.out = 0.14;
	card.in = card.out = 0.14;
	stepBack.set(0);
	gtk_widget_show(area);
}

void TabStrip::setTitleRow(bool on) {
	titleRow = on;
	buttons.load();
	hover.setHot(-1);
	redraw();
}

bool TabStrip::activeSide() const { return !win->splitOpen() || win->focusedPane() == pane; }

void TabStrip::layout(float w) {
	pages = win->panePages(pane);
	const bool showButtons = titleRow;
	leftInset = showButtons && win->stripIsLeftmost(pane) && buttons.leftRoom() > 0 ? buttons.leftRoom() : kEdge;
	rightInset = showButtons && win->stripIsRightmost(pane) ? buttons.rightRoom() : 0;
	const float n = (float)std::max<size_t>(1, pages.size());
	const float room = w - leftInset - kEdge - kPlusW - kGap - rightInset;
	tw = (room - kGap * (n - 1)) / n;
	tw = std::max(86.0f, std::min(220.0f, tw));
	if (n * (220 + kGap) < room) tw = std::min(tw, 160.0f);
	if (showButtons) buttons.layout(w, stripHeight());
	// Each tab eases to its place (a new one appears there).
	for (int k = 0; k < (int)pages.size(); k++) {
		const uint64_t key = cl_document_page_id(win->document(), pages[k]);
		auto it = xs.find(key);
		const float target = xAt(k) + makeRoom(k);
		if (it == xs.end()) {
			anim::Spring s;
			s.response = 0.26;
			s.damping = 0.86;
			s.snap(target);
			xs[key] = s;
		} else {
			it->second.target = target;
		}
	}
}

int TabStrip::heldIndex() const {
	if (heldKey == 0) return -1;
	for (int k = 0; k < (int)pages.size(); k++)
		if (cl_document_page_id(win->document(), pages[k]) == heldKey) return k;
	return -1;
}

// Where the held tab would land in this strip: the slot under its middle.
int TabStrip::dropSlot() const {
	const int d = heldIndex();
	if (d < 0 || !moving) return -1;
	const float cx = xAt(d) + dragDX + tw / 2;
	for (int j = 0; j < (int)pages.size(); j++) if (cx < xAt(j) + tw) return j;
	return (int)pages.size();
}

// While a tab is dragged along the strip, the tabs it passes slide aside.
float TabStrip::makeRoom(int k) const {
	const int at = dropSlot(), d = heldIndex();
	if (at < 0 || d < 0 || k == d) return 0;
	if (at > d + 1 && k > d && k < at) return -(tw + kGap);
	if (at < d && k >= at && k < d) return tw + kGap;
	return 0;
}

int TabStrip::tabAt(float x, float y) const {
	const float top = (stripHeight() - kTabH) / 2;
	if (y < top || y > top + kTabH) return -1;
	for (int k = 0; k < (int)pages.size(); k++) {
		const float x0 = xAt(k) + makeRoom(k);
		if (x >= x0 && x < x0 + tw) return k;
	}
	return -1;
}

bool TabStrip::onClose(int k, float x) const {
	const float x0 = xAt(k) + makeRoom(k);
	return cl_document_page_count(win->document()) > 1 && x > x0 + tw - 28 && x < x0 + tw - 4;
}

bool TabStrip::animating() {
	bool any = hover.active() || closeHover.active() || card.active() || stepBack.active() || buttons.animating();
	for (auto& kv : xs) any = kv.second.step() || any;
	return any;
}

void TabStrip::paint(cairo_t* cr, float w, float h) {
	if (prefs().classicTabs) { paintClassic(cr, w, h); return; }
	layout(w);
	const Chrome c = chrome();
	const bool dark = c.dark;
	const Color ink = c.tabInk();
	const Color accent = c.accent();
	CLDocument* doc = win->document();
	const int shown = win->shownPage(pane);
	const bool activePane = activeSide();
	stepBack.go(win->splitOpen() && !activePane ? 1 : 0, 0.18);
	if (stepBack.active()) animate();

	// Glass: a gentle gradient with a bright hairline along the top.
	linear(cr, rectF(0, 0, w, h), 0, c.tabBarTop(), c.tabBar());
	fillRect(cr, rectF(0, 0, w, 1), colorF(1, 1, 1, dark ? 0.07f : 0.9f));

	const int held = heldIndex();
	const int slot = dropSlot();
	const float top = (h - kTabH) / 2;
	// The gap the held tab would drop into: an accent outline.
	if (slot >= 0 && held >= 0 && slot != held && slot != held + 1) {
		const int landing = slot > held ? slot - 1 : slot;
		const RectF g = rectF(xAt(landing), top, xAt(landing) + tw, top + kTabH);
		fillRound(cr, g, 8, withAlpha(accent, 0.14f));
		cairo_save(cr);
		const double dash[] = { 5, 3 };
		cairo_set_dash(cr, dash, 2, 0);
		strokeRound(cr, g, 8, withAlpha(accent, 0.9f), 1.5f);
		cairo_restore(cr);
	}

	card.setHot(shown >= 0 && activePane ? shown : -1);
	bool springs = false;
	auto drawTab = [&](int k, float x, bool lifted) {
		const int p = pages[k];
		const bool active = p == shown && activePane;
		const float cardA = (float)card.amount(p);
		const float hot = (float)hover.amount(k);
		RectF r = rectF(x, top, x + tw, top + kTabH);
		cairo_save(cr);
		if (lifted) {
			// The held tab: lifted a little, with a shadow.
			const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
			cairo_translate(cr, cx, cy);
			cairo_scale(cr, 1.03, 1.03);
			cairo_translate(cr, -cx, -cy);
			for (int i = 3; i >= 1; i--)
				fillRound(cr, rectF(r.left - i, r.top + 2 - i + 2, r.right + i, r.bottom + i + 2), 8 + i, colorF(0, 0, 0, 0.06f));
		}
		if (cardA > 0.01f) {
			fillRound(cr, rectF(r.left, r.top + 1, r.right, r.bottom + 1), 8, colorF(0, 0, 0, (dark ? 0.26f : 0.10f) * cardA));
			linear(cr, r, 8, withAlpha(c.tabCardLit(), cardA), withAlpha(c.tabCard(), cardA));
			strokeRound(cr, r, 8, withAlpha(ink, (dark ? 0.18f : 0.10f) * cardA));
			fillRect(cr, rectF(r.left + 8, r.top + 1, r.right - 8, r.top + 2), colorF(1, 1, 1, (dark ? 0.10f : 0.85f) * cardA));
		}
		if (!active) fillRound(cr, r, 8, withAlpha(ink, 0.06f * std::max(hot, lifted ? 1.0f : 0.0f) * (1 - cardA)));
		const float cy = (r.top + r.bottom) / 2;
		fillCircle(cr, pointF(r.left + 12, cy), 3, active ? accent : withAlpha(ink, 0.28f));
		if (renamingKey != cl_document_page_id(doc, p)) {
			const bool showClose = cl_document_page_count(doc) > 1 && (active || hot > 0.5f);
			drawTextMid(cr, win->pageName(p), rectF(r.left + 24, r.top, r.right - (showClose ? 28 : 10), r.bottom), 11.5f,
			            withAlpha(ink, active ? 1.0f : 0.7f), TextAlign::Leading, active);
			if (showClose) {
				const RectF x = rectF(r.right - 26, cy - 10, r.right - 6, cy + 10);
				fillRound(cr, x, 6, withAlpha(ink, 0.16f * (float)closeHover.amount(k)));
				drawIcon(cr, Icon::Dismiss, x, 9, withAlpha(ink, 0.7f));
			}
		}
		cairo_restore(cr);
	};
	// Separators between quiet tabs.
	for (int k = 0; k + 1 < (int)pages.size(); k++) {
		const bool loud = (pages[k] == shown && activePane) || (pages[k + 1] == shown && activePane) || hover.hot == k || hover.hot == k + 1 || k == held || k + 1 == held;
		if (loud) continue;
		const uint64_t key = cl_document_page_id(doc, pages[k]);
		const float x = xs.count(key) ? (float)xs[key].x : xAt(k);
		fillRect(cr, rectF(x + tw + kGap / 2 - 0.5f, top + 6.5f, x + tw + kGap / 2 + 0.5f, top + kTabH - 6.5f), withAlpha(ink, 0.14f));
	}
	for (int k = 0; k < (int)pages.size(); k++) {
		if (k == held && moving) continue;
		const uint64_t key = cl_document_page_id(doc, pages[k]);
		anim::Spring& s = xs[key];
		if (!s.settled()) springs = true;
		drawTab(k, (float)s.x, false);
	}
	if (held >= 0 && moving) {
		const float x = std::max(leftInset - 4, std::min(w - rightInset - tw - 4, xAt(held) + dragDX));
		drawTab(held, x, true);
		xs[heldKey].snap(x);
	}
	// +, after the tabs.
	const RectF plus = rectF(xAt((int)pages.size()), top, xAt((int)pages.size()) + kPlusW, top + kTabH);
	fillRound(cr, plus, 9, withAlpha(ink, 0.08f * (float)hover.amount(kPlus)));
	drawIcon(cr, Icon::Add, plus, 13, withAlpha(ink, 0.75f));

	// A hairline under the strip -- or, in a split, an accent rail under the
	// side in use, and the other side stepping back.
	if (win->splitOpen() && activePane) fillRect(cr, rectF(0, h - 2, w, h), accent);
	else fillRect(cr, rectF(0, h - 1, w, h), withAlpha(ink, 0.12f));
	const float sb = (float)stepBack.value();
	if (sb > 0.01f) fillRect(cr, rectF(0, 0, w, h - 1), withAlpha(c.tabBar(), 0.45f * sb));

	if (titleRow) buttons.paint(cr, win->window(), c.barInk());
	if (springs || hover.active() || closeHover.active() || card.active()) animate();

	std::vector<Tip> tips;
	tips.push_back({ plus, "New tab (Ctrl+T)" });
	for (int k = 0; k < (int)pages.size(); k++)
		tips.push_back({ rectF(xAt(k), top, xAt(k) + tw, top + kTabH), "Double-click to rename; drag to move, or down onto the canvas to split the view" });
	setTips(tips);
}

// ---- Classic tabs: plain segments in the middle -------------------------------------

std::vector<RectF> TabStrip::classicSegments(float w) const {
	std::vector<RectF> out;
	float total = 0;
	std::vector<float> ws;
	for (int p : pages) { ws.push_back(textWidth(win->pageName(p), 11.5f) + 26); total += ws.back(); }
	float x = std::max(leftInset, (w - total) / 2);
	const float top = (stripHeight() - 24) / 2;
	for (float sw : ws) { out.push_back(rectF(x, top, x + sw, top + 24)); x += sw; }
	return out;
}

void TabStrip::paintClassic(cairo_t* cr, float w, float h) {
	pages = win->panePages(pane);
	leftInset = titleRow && buttons.leftRoom() > 0 ? buttons.leftRoom() : kEdge;
	rightInset = titleRow ? buttons.rightRoom() : 0;
	if (titleRow) buttons.layout(w, h);
	const Chrome c = chrome();
	const Color ink = c.tabInk();
	fillRect(cr, rectF(0, 0, w, h), c.tabBar());
	const std::vector<RectF> segs = classicSegments(w);
	const int shown = win->shownPage(pane);
	if (!segs.empty()) {
		const RectF all = rectF(segs.front().left, segs.front().top, segs.back().right, segs.back().bottom);
		fillRound(cr, all, 7, withAlpha(ink, 0.06f));
		strokeRound(cr, all, 7, withAlpha(ink, 0.10f));
	}
	for (size_t i = 0; i < segs.size(); i++) {
		const bool on = pages[i] == shown;
		if (on) {
			fillRound(cr, rectF(segs[i].left + 2, segs[i].top + 2, segs[i].right - 2, segs[i].bottom - 2), 5, c.tabCard());
			strokeRound(cr, rectF(segs[i].left + 2, segs[i].top + 2, segs[i].right - 2, segs[i].bottom - 2), 5, withAlpha(ink, 0.12f));
		}
		drawTextMid(cr, win->pageName(pages[i]), segs[i], 11.5f, withAlpha(ink, on ? 1.0f : 0.7f), TextAlign::Center, on);
	}
	const RectF plus = rectF(w - rightInset - 8 - kPlusW, (h - kTabH) / 2, w - rightInset - 8, (h + kTabH) / 2);
	drawIcon(cr, Icon::Add, plus, 13, withAlpha(ink, 0.75f));
	fillRect(cr, rectF(0, h - 1, w, h), withAlpha(ink, 0.12f));
	if (titleRow) buttons.paint(cr, win->window(), c.barInk());
}

// ---- The pointer -------------------------------------------------------------------

void TabStrip::mouseMove(float x, float y) {
	if (heldKey != 0) {
		dragDX = x - pressX;
		if (!moving && (std::fabs(x - pressX) > 6 || std::fabs(y - pressY) > 8)) moving = true;
		if (moving && !prefs().classicTabs) win->showDropHint(win->dropHintAt(pane, area, x, y));
		animate();
		return;
	}
	if (titleRow) {
		const int b = buttons.at(x, y);
		buttons.setHot(b);
		if (b >= 0) { hover.setHot(-1); closeHover.setHot(-1); animate(); return; }
	}
	if (prefs().classicTabs) return;
	const int k = tabAt(x, y);
	const float top = (stripHeight() - kTabH) / 2;
	const float px = xAt((int)pages.size());
	const bool onPlus = k < 0 && x >= px && x < px + kPlusW && y >= top && y < top + kTabH;
	hover.setHot(onPlus ? kPlus : k);
	closeHover.setHot(k >= 0 && onClose(k, x) ? k : -1);
	animate();
}

void TabStrip::mouseLeave() {
	if (heldKey != 0) return;
	hover.setHot(-1);
	closeHover.setHot(-1);
	buttons.setHot(-1);
	animate();
}

void TabStrip::mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) {
	CLDocument* doc = win->document();
	if (titleRow) {
		const int b = buttons.at(x, y);
		if (b >= 0) { if (button == 1) { buttons.pressed = b; redraw(); } return; }
	}
	if (prefs().classicTabs) {
		const std::vector<RectF> segs = classicSegments(width());
		for (size_t i = 0; i < segs.size(); i++)
			if (inRect(segs[i], x, y)) {
				if (button == 1 && doubleClick) beginRename(pages[i]);
				else if (button == 1) win->showPage(pages[i]);
				else if (button == 3) win->tabContextMenu(pages[i], (GdkEvent*)e);
				return;
			}
		if (button == 1 && x > width() - rightInset - 8 - kPlusW && x < width() - rightInset - 8) { win->activatePane(pane); win->newPage(); return; }
		if (titleRow && button == 1 && !doubleClick) titleRowPress(win->window(), e, false);
		return;
	}
	const int k = tabAt(x, y);
	if (button == 3) { win->tabContextMenu(k >= 0 ? pages[k] : -1, (GdkEvent*)e); return; }
	if (button == 2) { if (k >= 0) win->closePage(pages[k]); return; }
	if (button != 1) return;
	const float top = (stripHeight() - kTabH) / 2;
	const float px = xAt((int)pages.size());
	if (k < 0 && x >= px && x < px + kPlusW && y >= top && y < top + kTabH) {
		win->activatePane(pane);
		win->newPage();
		return;
	}
	if (k < 0) {
		// The empty strip: work in this side; twice, a new tab. In focus
		// mode it's the window's top row too.
		if (doubleClick) { win->activatePane(pane); win->newPage(); return; }
		win->activatePane(pane);
		if (Canvas* cv = win->paneCanvas(pane)) gtk_widget_grab_focus(cv->widget());
		if (titleRow) titleRowPress(win->window(), e, false);
		return;
	}
	const int p = pages[k];
	if (doubleClick) { beginRename(p); return; }
	if (onClose(k, x)) { win->closePage(p); hover.setHot(-1); closeHover.setHot(-1); return; }
	win->showPage(p);
	heldKey = cl_document_page_id(doc, p);
	pressX = x;
	pressY = y;
	dragDX = 0;
	moving = false;
	redraw();
}

void TabStrip::mouseUp(int button, float x, float y) {
	if (button != 1) return;
	if (titleRow && buttons.pressed >= 0) {
		const int b = buttons.pressed;
		buttons.pressed = -1;
		if (buttons.at(x, y) == b) buttons.activate(b, win->window());
		redraw();
		return;
	}
	if (heldKey == 0) return;
	const uint64_t key = heldKey;
	const bool wasMoving = moving;
	const int slot = dropSlot(), held = heldIndex();
	const CircuitWindow::DropHint h = wasMoving ? win->dropHintAt(pane, area, x, y) : CircuitWindow::DropHint();
	heldKey = 0;
	moving = false;
	dragDX = 0;
	win->showDropHint(CircuitWindow::DropHint());
	const int page = cl_document_page_index(win->document(), key);
	if (page < 0 || !wasMoving) { animate(); return; }
	if (h.kind != 0) { win->tabDropped(page, h); animate(); return; }
	if (slot >= 0 && held >= 0 && slot != held && slot != held + 1) {
		// Before the tab now in that slot, or after the one before it.
		const int to = slot < held ? pages[slot] : pages[slot - 1];
		win->movePage(page, to);
	}
	animate();
}

// ---- Renaming in place -----------------------------------------------------------

void TabStrip::beginRename(int page) {
	if (page < 0 || page >= cl_document_page_count(win->document())) return;
	win->showPage(page);
	layout(width());
	int k = -1;
	for (int i = 0; i < (int)pages.size(); i++) if (pages[i] == page) k = i;
	if (k < 0) return;
	renamingKey = cl_document_page_id(win->document(), page);
	gtk_entry_set_text(GTK_ENTRY(entry), win->pageName(page).c_str());
	const float top = (stripHeight() - kTabH) / 2;
	gtk_widget_set_margin_start(entry, (int)(xAt(k) + 20));
	gtk_widget_set_margin_top(entry, (int)(top + 3));
	gtk_widget_set_size_request(entry, (int)(tw - 30), (int)(kTabH - 6));
	gtk_widget_show(entry);
	gtk_widget_grab_focus(entry);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
	redraw();
}

void TabStrip::commitRename(bool keep) {
	if (renamingKey == 0) return;
	const uint64_t key = renamingKey;
	renamingKey = 0;
	const std::string text = gtk_entry_get_text(GTK_ENTRY(entry));
	gtk_widget_hide(entry);
	const int page = cl_document_page_index(win->document(), key);
	std::string name = text;
	const size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
	name = a == std::string::npos ? std::string() : name.substr(a, b - a + 1);
	if (keep && page >= 0 && !name.empty() && name != win->pageName(page)) {
		cl_document_rename_page(win->document(), page, name.c_str());
		win->edited();
	}
	if (Canvas* c = win->paneCanvas(pane)) gtk_widget_grab_focus(c->widget());
	redraw();
}
