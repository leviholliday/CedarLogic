// The Customize list and its sheet (see ActionsList.h, Rail.h).

#include "ActionsList.h"
#include "Actions.h"
#include "Chrome.h"
#include "Rail.h"
#include "Window.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>

namespace {

const float kRowH = 26;

LRESULT CALLBACK listProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	ActionsList* L = reinterpret_cast<ActionsList*>(data);
	const float s = dpiOf(h) / 96.0f;
	const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
	LRESULT handled = 0;
	bool took = false;
	guarded("the actions list", [&] {
		switch (msg) {
		case WM_GETDLGCODE:
			handled = DLGC_WANTARROWS | DLGC_WANTCHARS;
			took = true;
			break;
		case WM_SETFOCUS:
			if (L->hot < 0 && !L->rows.empty()) L->hot = 0;
			L->refresh();
			break;
		case WM_KILLFOCUS:
			L->refresh();
			break;
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
			TrackMouseEvent(&t);
			if (L->pressRow >= 0 && !L->searching() && !L->disabled) {
				if (!L->dragging && std::fabs(y - L->pressY) >= 4) L->dragging = true;
				if (L->dragging) {
					L->dragY = y;
					// Near the ends, it scrolls.
					if (y < 8) L->scroll = std::max(0.0f, L->scroll - 6);
					else if (y > L->viewH - 8) L->scroll = std::min(std::max(0.0f, L->contentH - L->viewH), L->scroll + 6);
					L->refresh();
				}
				took = true;
				break;
			}
			const int row = L->rowAt(x, y);
			if (row != L->hot && row >= 0) { L->hot = row; L->refresh(); }
			took = true;
			break;
		}
		case WM_MOUSELEAVE:
			if (L->hot != -1 && GetFocus() != h && !L->dragging) { L->hot = -1; L->refresh(); }
			took = true;
			break;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK: {
			took = true;
			SetFocus(h);
			if (L->disabled) break;
			const int row = L->rowAt(x, y);
			if (row < 0) break;
			L->hot = row;
			// The tick (or a double-click anywhere on the row) ticks it.
			if (x < L->rows[row].box.right + 6 || msg == WM_LBUTTONDBLCLK) {
				L->toggle(row);
				break;
			}
			if (!L->searching()) {
				L->pressRow = row;
				L->pressY = y;
				L->dragY = y;
				SetCapture(h);
			}
			L->refresh();
			break;
		}
		case WM_LBUTTONUP:
			took = true;
			if (L->pressRow >= 0) {
				if (L->dragging) {
					const int to = L->dropIndex();
					actions::moveListed(L->pressRow, to);
					L->hot = to;
					L->after();
				}
				L->pressRow = -1;
				L->dragging = false;
				if (GetCapture() == h) ReleaseCapture();
				L->refresh();
			}
			break;
		case WM_CAPTURECHANGED:
			if (L->pressRow >= 0) { L->pressRow = -1; L->dragging = false; L->refresh(); }
			break;
		case WM_KEYDOWN: {
			if (L->disabled) break;
			const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
			switch (wp) {
			case VK_DOWN: if (ctrl) L->moveBy(L->hot, 1); else L->choose(L->hot + 1); took = true; break;
			case VK_UP: if (ctrl) L->moveBy(L->hot, -1); else L->choose(L->hot - 1); took = true; break;
			case VK_HOME: L->choose(0); took = true; break;
			case VK_END: L->choose((int)L->rows.size() - 1); took = true; break;
			case VK_SPACE: if (L->hot >= 0) L->toggle(L->hot); took = true; break;
			default: break;
			}
			break;
		}
		case WM_CHAR:
			if (wp == L' ') took = true;   // no beep
			break;
		}
	});
	return took ? handled : DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

bool ActionsList::searching() const {
	for (char c : query) if (c != ' ' && c != '\t') return true;
	return false;
}

std::vector<std::string> ActionsList::visible() const {
	std::vector<std::string> out;
	for (const std::string& id : actions::listed()) {
		const actions::Def* d = actions::find(id);
		if (d && (!searching() || actions::matches(*d, query))) out.push_back(id);
	}
	return out;
}

int ActionsList::rowAt(float x, float y) const {
	for (size_t i = 0; i < rows.size(); i++)
		if (inRect(rows[i].rect, x, y)) return (int)i;
	(void)x;
	return -1;
}

// Where the row being dragged goes: the row its middle is over.
int ActionsList::dropIndex() const {
	const int n = (int)rows.size();
	if (pressRow < 0 || n == 0) return pressRow;
	const float moved = dragY - pressY;
	return std::min(std::max(pressRow + (int)std::lround(moved / kRowH), 0), n - 1);
}

void ActionsList::toggle(int row) {
	if (row < 0 || row >= (int)rows.size()) return;
	const std::string& id = rows[row].id;
	actions::setShown(id, !actions::shown(id));
	after();
	refresh();
}

void ActionsList::moveBy(int row, int delta) {
	if (searching() || row < 0 || row >= (int)rows.size()) return;
	const int to = std::max(0, std::min((int)rows.size() - 1, row + delta));
	if (to == row) return;
	actions::moveListed(row, to);
	after();
	choose(to);
}

void ActionsList::choose(int row) {
	if (rows.empty()) return;
	hot = std::max(0, std::min((int)rows.size() - 1, row));
	const D2D1_RECT_F r = rows[hot].rect;
	if (r.top < 4) scroll = std::max(0.0f, scroll - (4 - r.top));
	else if (r.bottom > viewH - 4) scroll += r.bottom - viewH + 4;
	refresh();
}

void ActionsList::after() {
	actions::changed();
	if (!form) return;
	const int all = (int)actions::listed().size();
	if (footer >= 0)
		form->setText(footer, searching() ? strf("%d of %d match. Clear the search to drag them into order.", (int)visible().size(), all)
		                                  : strf("%d shown. Drag to change the order, here or on the panel.", (int)actions::rail().size()));
	if (reset >= 0) form->enable(reset, !actions::isDefault() && !disabled);
}

void ActionsList::refresh() {
	if (form && list >= 0) form->refresh(list);
}

void ActionsList::setDisabled(bool off) {
	disabled = off;
	if (form) {
		if (search >= 0) form->enable(search, !off);
		if (reset >= 0) form->enable(reset, !off && !actions::isDefault());
	}
	refresh();
}

void ActionsList::add(Form& f, int height) {
	form = &f;
	FormField s;
	s.kind = FormField::Text;
	s.placeholder = "Search actions";
	s.widthPt = 300;
	search = f.add(s);
	FormField r;
	r.kind = FormField::Button;
	r.label = "Reset to Default";
	r.beside = true;
	reset = f.add(r);
	FormField pic;
	pic.kind = FormField::Picture;
	pic.height = height;
	pic.paint = [this](ID2D1RenderTarget* rt, float w, float h) { paint(rt, w, h); };
	list = f.add(pic);
	FormField note;
	note.kind = FormField::Note;
	note.label = strf("%d shown. Drag to change the order, here or on the panel.", (int)actions::rail().size());
	footer = f.add(note);
}

void ActionsList::init(Form& f) {
	form = &f;
	HWND h = f.fields[list].hwnd;
	SetWindowSubclass(h, listProc, 7, (DWORD_PTR)this);
	SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | WS_TABSTOP);
	if (!query.empty()) f.setText(search, query);
	f.enable(reset, !actions::isDefault() && !disabled);
	if (disabled) setDisabled(true);
	after();
}

bool ActionsList::changed(Form& f, int field) {
	if (field == search) {
		query = f.text(search);
		scroll = 0;
		hot = -1;
		after();
		refresh();
		return true;
	}
	if (field == reset) {
		actions::reset();
		scroll = 0;
		after();
		refresh();
		return true;
	}
	return false;
}

bool ActionsList::wheel(int field, int delta) {
	if (field != list) return false;
	scroll = std::max(0.0f, std::min(scroll - delta * 3 * kRowH / WHEEL_DELTA, std::max(0.0f, contentH - viewH)));
	refresh();
	return true;
}

bool ActionsList::clearSearch(Form& f) {
	if (query.empty()) return false;
	query.clear();
	f.setText(search, "");
	scroll = 0;
	after();
	refresh();
	return true;
}

void ActionsList::paint(ID2D1RenderTarget* rt, float w, float h) {
	const FormLook look = formLook();
	const D2D1_COLOR_F accent = chrome().accent();
	viewH = h;
	rows.clear();
	const D2D1_RECT_F frame = D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
	fillRound(rt, frame, 8, look.field);
	rt->PushAxisAlignedClip(D2D1::RectF(1, 1, w - 1, h - 1), D2D1_ANTIALIAS_MODE_ALIASED);
	const std::vector<std::string> ids = visible();
	const bool focused = form && list >= 0 && GetFocus() == form->fields[list].hwnd;
	const float dim = disabled ? 0.45f : 1.0f;
	const int n = (int)ids.size();
	const int drop = dragging ? dropIndex() : -1;
	auto rowTop = [&](int i) { return 4 + i * kRowH - scroll; };
	auto drawRow = [&](int i, float top, bool lifted) {
		const actions::Def* d = actions::find(ids[i]);
		if (!d) return;
		const D2D1_RECT_F r = D2D1::RectF(4, top, w - 4, top + kRowH);
		if (lifted) {
			fillRound(rt, D2D1::RectF(r.left, r.top + 2, r.right, r.bottom + 2), 6, D2D1::ColorF(0, 0, 0, prefs().dark ? 0.35f : 0.10f));
			fillRound(rt, r, 6, chrome().tabCard());
			strokeRound(rt, r, 6, withAlpha(accent, 0.6f), 1.2f);
		} else if (i == hot && !disabled) {
			fillRound(rt, r, 6, focused ? withAlpha(accent, prefs().dark ? 0.16f : 0.09f) : withAlpha(look.ink, 0.06f));
		} else if (i % 2 == 1) {
			fillRect(rt, D2D1::RectF(r.left, r.top, r.right, r.bottom), withAlpha(look.ink, 0.025f));
		}
		const float my = (r.top + r.bottom) / 2;
		const D2D1_RECT_F box = D2D1::RectF(14, my - 7, 28, my + 7);
		const bool on = actions::shown(d->id);
		if (on) {
			fillRound(rt, box, 3, withAlpha(accent, dim));
			drawIcon(rt, 0xE73E, box, 10, withAlpha(chrome().onAccent(), dim));
		} else {
			strokeRound(rt, D2D1::RectF(box.left + 0.5f, box.top + 0.5f, box.right - 0.5f, box.bottom - 0.5f), 3, withAlpha(look.ink, 0.45f * dim), 1.2f);
		}
		const D2D1_RECT_F icon = D2D1::RectF(36, r.top, 58, r.bottom);
		const D2D1_COLOR_F ic = withAlpha(accent, dim);
		if (d->command == CMD_NEW) drawNewDocIcon(rt, icon, ic);
		else if (d->command == CMD_NEW_TAB) drawNewTabIcon(rt, icon, ic);
		else if (d->command == CMD_OPEN) drawFolderIcon(rt, icon, ic);
		else drawIcon(rt, d->glyph, icon, 13, ic);
		float right = w - 14;
		if (!searching()) drawIcon(rt, 0xE700, D2D1::RectF(right - 14, r.top, right, r.bottom), 10, withAlpha(look.ink, 0.3f * dim));
		right -= 22;
		drawText(rt, d->menu, D2D1::RectF(right - 64, r.top + 5, right, r.bottom - 5), 11, withAlpha(look.dim, 0.8f * dim), TextAlign::Trailing);
		right -= 72;
		const std::string key = actions::keyLabel(*d);
		if (!key.empty()) {
			const float kw = textWidth(key, 11.5f);
			drawText(rt, key, D2D1::RectF(right - kw - 2, r.top + 5, right, r.bottom - 5), 11.5f, withAlpha(look.dim, dim), TextAlign::Trailing);
			right -= kw + 12;
		}
		drawText(rt, d->title, D2D1::RectF(64, r.top + 5, right, r.bottom - 5), 12.5f, withAlpha(look.ink, dim));
		if (!lifted) rows.push_back({ d->id, r, box });
	};
	for (int i = 0; i < n; i++) {
		if (dragging && i == pressRow) { rows.push_back({ ids[i], D2D1::RectF(4, rowTop(i), w - 4, rowTop(i) + kRowH), D2D1::RectF(14, 0, 28, 0) }); continue; }
		float top = rowTop(i);
		// The others make room where it would go.
		if (dragging) {
			if (pressRow < drop && i > pressRow && i <= drop) top -= kRowH;
			else if (drop < pressRow && i >= drop && i < pressRow) top += kRowH;
		}
		drawRow(i, top, false);
	}
	if (dragging && pressRow >= 0 && pressRow < n) {
		const float top = std::min(std::max(rowTop(pressRow) + dragY - pressY, 2.0f - kRowH / 2), h - kRowH / 2);
		drawRow(pressRow, top, true);
	}
	if (n == 0) drawText(rt, "No actions match “" + query + "”.", D2D1::RectF(0, 30, w, 50), 12.5f, look.dim, TextAlign::Center);
	contentH = 8 + n * kRowH;
	rt->PopAxisAlignedClip();
	if (contentH > h + 1) {
		const float trackH = h - 12, thumbH = std::max(24.0f, trackH * h / contentH);
		const float at = 6 + (trackH - thumbH) * (scroll / std::max(1.0f, contentH - h));
		fillRound(rt, D2D1::RectF(w - 7, at, w - 4, at + thumbH), 1.5f, withAlpha(look.ink, 0.25f));
	}
	if (focused) strokeRound(rt, frame, 8, withAlpha(accent, 0.8f), 1.5f);
	else strokeRound(rt, frame, 8, look.line);
}

// ---- The sheet -------------------------------------------------------------------

void showCustomizeActions(CircuitWindow* w, const std::string& searchFor) {
	if (!w) return;
	Form f;
	f.title = "Customize Actions";
	f.width = 520;
	f.okText = "Done";
	f.cancelText = "";
	FormField intro;
	intro.kind = FormField::Note;
	intro.label = "Tick what goes on the Actions panel, anything from any menu. They show in this order, top to bottom. "
	              "You can also drag them up or down on the panel itself.";
	intro.lines = 2;
	f.add(intro);
	ActionsList list;
	list.query = searchFor.empty() ? actionsSearch() : searchFor;
	list.add(f, 360);
	// The panel's two switches (the Mac's sheet has them beside Done).
	FormField show;
	show.kind = FormField::Check;
	show.label = actions::kShowSwitch;
	show.value = prefs().actionsOn ? "1" : "";
	show.grid = 2;
	const int showField = f.add(show);
	FormField hover;
	hover.kind = FormField::Check;
	hover.label = actions::kHoverSwitch;
	hover.value = prefs().actionsHoverOpen ? "1" : "";
	hover.grid = 2;
	hover.tip = actions::hoverHint();
	const int hoverField = f.add(hover);
	f.onInit = [&](Form& form) {
		list.init(form);
		form.enable(hoverField, prefs().actionsOn);
	};
	f.onChange = [&](Form& form, int field) {
		if (list.changed(form, field)) return;
		if (field == showField) {
			prefs().actionsOn = form.checked(field);
			form.enable(hoverField, prefs().actionsOn);
			actions::changed();
		} else if (field == hoverField) {
			prefs().actionsHoverOpen = form.checked(field);
			actions::changed();
		}
	};
	f.onWheel = [&](Form&, int field, int delta) { list.wheel(field, delta); };
	// Escape clears a search first, then closes.
	f.onCancel = [&](Form& form) { return !list.clearSearch(form); };
	f.run(w->window());
}
