// The gate palette and the minimap (see Palette.h).

#include "Palette.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Drawn.h"
#include "Collections.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

const wchar_t* kHostClass = L"CedarLogicPalette";
const wchar_t* kTilesClass = L"CedarLogicTiles";
const wchar_t* kMapClass = L"CedarLogicMiniMap";
const int kSearchId = 1;
// In points.
const float kTileW = 78, kArtH = 46, kCaptionH = 16, kGap = 2, kPad = 8, kMapH = 132, kPickerH = 28, kFieldH = 28;

}  // namespace

// The category picker at the top of the panel: the category's name in a
// soft rounded box, with a chevron; a click lists them all.
class CategoryButton : public Drawn {
public:
	CategoryButton(GatePalette* palette, HWND parent) : palette(palette) { create(parent); }

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override {
		const Chrome c = chrome();
		rt->Clear(c.panel());
		const D2D1_RECT_F r = D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
		fillRound(rt, r, 7, c.dark ? D2D1::ColorF(1, 1, 1, hot ? 0.10f : 0.06f) : D2D1::ColorF(0, 0, 0, hot ? 0.07f : 0.045f));
		strokeRound(rt, r, 7, c.hairline());
		const D2D1_COLOR_F ink = c.barInk();
		drawText(rt, palette->categoryTitle(), D2D1::RectF(10, 0, w - 26, h), 12, ink, TextAlign::Center, true);
		drawIcon(rt, Icon::ChevronDown, D2D1::RectF(w - 24, 0, w - 8, h), 9, withAlpha(ink, 0.6f));
	}
	void mouseMove(float, float) override { if (!hot) { hot = true; redraw(); } }
	void mouseLeave() override { hot = false; redraw(); }
	void mouseDown(int button, float, float, bool) override {
		if (button != 1) return;
		if (GetCapture() == hwnd) ReleaseCapture();
		RECT rc;
		GetClientRect(hwnd, &rc);
		POINT p = { 0, rc.bottom + 2 };
		ClientToScreen(hwnd, &p);
		palette->chooseCategory(p);
		hot = false;
		redraw();
	}

private:
	GatePalette* palette;
	bool hot = false;
};

void registerPaletteClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = GatePalette::hostProc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kHostClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = GatePalette::tilesProc;
	wc.lpszClassName = kTilesClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = MiniMap::proc;
	wc.lpszClassName = kMapClass;
	RegisterClassExW(&wc);
}

// ---- The palette -------------------------------------------------------------------

GatePalette::GatePalette(CircuitWindow* window, HWND parent) : win(window) {
	host = CreateWindowExW(WS_EX_CONTROLPARENT, kHostClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10,
	                       parent, nullptr, appInstance(), this);
	search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
	                         host, (HMENU)(INT_PTR)kSearchId, appInstance(), nullptr);
	SendMessageW(search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Find a gate");
	picker = new CategoryButton(this, host);
	tiles = CreateWindowExW(0, kTilesClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, host, nullptr, appInstance(), this);
	map = new MiniMap(window, host);

	loadCategories();
	dpiChanged();
	fill();
}

GatePalette::~GatePalette() {
	delete map;
	map = nullptr;
	delete picker;
	picker = nullptr;
	if (fieldBrush) DeleteObject(fieldBrush);
	if (host) {
		SetWindowLongPtrW(tiles, GWLP_USERDATA, 0);
		SetWindowLongPtrW(host, GWLP_USERDATA, 0);
		DestroyWindow(host);
	}
}

void GatePalette::dpiChanged() {
	setFontTree(host, uiFont(dpiOf(host)));
	layout();
}

void GatePalette::loadCategories() {
	categories.clear();
	for (int c = 0; c < cl_library_category_count(); c++) {
		Category cat;
		std::string name = cl_library_category(c);
		const size_t dash = name.find(" - ");
		cat.title = dash == std::string::npos ? name : name.substr(dash + 3);
		for (int i = 0; i < cl_library_gate_count(c); i++) {
			Gate g;
			g.name = cl_library_gate(c, i);
			g.caption = cl_library_gate_caption(g.name.c_str());
			if (g.caption.empty()) g.caption = g.name;
			cat.gates.push_back(g);
		}
		if (!cat.gates.empty()) categories.push_back(cat);
	}
	// Your own parts, last (Edit > Save as Part).
	Category mine;
	mine.title = "My Parts";
	for (const parts::Part& p : parts::all()) mine.gates.push_back({ p.gate(), p.name });
	if (!mine.gates.empty()) categories.push_back(mine);
}

void GatePalette::partsChanged() {
	const std::string was = category >= 0 && category < (int)categories.size() ? categories[category].title : std::string();
	loadCategories();
	category = 0;
	for (int i = 0; i < (int)categories.size(); i++) if (categories[i].title == was) category = i;
	fill();
}

std::string GatePalette::categoryTitle() const {
	if (GetWindowTextLengthW(search) > 0) return "Search";
	return category >= 0 && category < (int)categories.size() ? categories[category].title : std::string();
}

void GatePalette::chooseCategory(POINT screen) {
	HMENU m = CreatePopupMenu();
	for (int i = 0; i < (int)categories.size(); i++) {
		std::string label = categories[i].title;
		if (i < 10) label += strf("\tShift+%d", (i + 1) % 10);
		AppendMenuW(m, MF_STRING | (i == category ? MF_CHECKED : 0), i + 1, W(label).c_str());
	}
	const int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, host, nullptr);
	DestroyMenu(m);
	if (r > 0) showCategory(r - 1);
}

void GatePalette::showCategory(int index) {
	if (index < 0 || index >= (int)categories.size()) return;
	category = index;
	if (GetWindowTextLengthW(search) > 0) SetWindowTextW(search, L"");   // fills
	else fill();
	picker->redraw();
}

void GatePalette::focusSearch() { SetFocus(search); }

void GatePalette::themeChanged() {
	if (fieldBrush) { DeleteObject(fieldBrush); fieldBrush = nullptr; }
	InvalidateRect(host, nullptr, TRUE);
	InvalidateRect(search, nullptr, TRUE);
	InvalidateRect(tiles, nullptr, FALSE);
	if (picker) picker->redraw();
	if (map) map->themeChanged();
}

void GatePalette::fill() {
	shown.clear();
	const std::string query = lowerCase(windowText(search));
	if (!query.empty()) {
		// Searching looks through every category.
		for (const Category& c : categories)
			for (const Gate& g : c.gates)
				if (lowerCase(g.caption).find(query) != std::string::npos || lowerCase(g.name).find(query) != std::string::npos)
					shown.push_back(g);
	} else if (category >= 0 && category < (int)categories.size()) {
		shown = categories[category].gates;
	}
	scrollY = 0;
	hover = pressed = -1;
	if (picker) picker->redraw();
	InvalidateRect(tiles, nullptr, FALSE);
}

RECT GatePalette::searchFrame() const {
	RECT rc;
	GetClientRect(host, &rc);
	const UINT dpi = dpiOf(host);
	const int pad = scaled((int)kPad, dpi);
	const int top = pad + scaled((int)kPickerH, dpi) + scaled(6, dpi);
	return RECT{ pad, top, rc.right - pad, top + scaled((int)kFieldH, dpi) };
}

void GatePalette::layout() {
	if (tiles == nullptr || search == nullptr || picker == nullptr) return;   // still being made
	RECT rc;
	GetClientRect(host, &rc);
	const UINT dpi = dpiOf(host);
	const int w = rc.right, h = rc.bottom, pad = scaled((int)kPad, dpi);
	MoveWindow(picker->widget(), pad, pad, std::max(10, w - 2 * pad), scaled((int)kPickerH, dpi), TRUE);
	// The edit sits inside the drawn field, its text centred on the line.
	const RECT f = searchFrame();
	HDC dc = GetDC(search);
	HGDIOBJ old = SelectObject(dc, uiFont(dpi));
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	SelectObject(dc, old);
	ReleaseDC(search, dc);
	const int lineH = tm.tmHeight;
	const int iconW = scaled(26, dpi);
	MoveWindow(search, f.left + iconW, f.top + (f.bottom - f.top - lineH) / 2, std::max(10, (int)(f.right - f.left) - iconW - scaled(8, dpi)), lineH, TRUE);
	const int top = f.bottom + scaled(6, dpi);
	const int mapH = scaled((int)kMapH, dpi);
	MoveWindow(tiles, 0, top, w, std::max(10, h - top - mapH), TRUE);
	if (map) MoveWindow(map->widget(), 0, h - mapH, w, mapH, TRUE);
	clampScroll();
	InvalidateRect(host, nullptr, TRUE);
}

void GatePalette::paintHost(HDC dc) {
	const Chrome c = chrome();
	RECT rc;
	GetClientRect(host, &rc);
	HBRUSH bg = CreateSolidBrush(c.gdi(c.panel()));
	FillRect(dc, &rc, bg);
	DeleteObject(bg);
	// The search field: a rounded box a shade off the panel, a hairline round
	// it, the magnifier in front.
	const RECT f = searchFrame();
	const UINT dpi = dpiOf(host);
	const D2D1_COLOR_F field = c.dark ? D2D1::ColorF(0.16f, 0.17f, 0.20f) : D2D1::ColorF(1, 1, 1);
	HBRUSH fb = CreateSolidBrush(c.gdi(field));
	HPEN pen = CreatePen(PS_SOLID, 1, c.dark ? RGB(58, 62, 72) : RGB(214, 216, 222));
	HGDIOBJ ob = SelectObject(dc, fb), op = SelectObject(dc, pen);
	const int r = scaled(14, dpi);
	RoundRect(dc, f.left, f.top, f.right, f.bottom, r, r);
	SelectObject(dc, ob);
	SelectObject(dc, op);
	DeleteObject(fb);
	DeleteObject(pen);
	LOGFONTW lf = {};
	lf.lfHeight = -scaled(12, dpi);
	wcscpy(lf.lfFaceName, L"Segoe MDL2 Assets");
	HFONT icons = CreateFontIndirectW(&lf);
	HGDIOBJ of = SelectObject(dc, icons);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, c.dark ? RGB(140, 146, 158) : RGB(120, 124, 132));
	RECT ir = { f.left + scaled(4, dpi), f.top, f.left + scaled(26, dpi), f.bottom };
	const wchar_t glass = Icon::Search;
	DrawTextW(dc, &glass, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SelectObject(dc, of);
	DeleteObject(icons);
}

// In points.
void GatePalette::layoutTiles(int& columns, float& tileW, float& tileH) const {
	RECT rc;
	GetClientRect(tiles, &rc);
	const double s = dpiOf(tiles) / 96.0;
	const float width = (float)(rc.right / s);
	tileH = kArtH + (prefs().showGateNames ? kCaptionH : 0);
	columns = std::max(1, (int)((width - 2 * kPad + kGap) / (kTileW + kGap)));
	// Spread whatever width is left over the columns.
	tileW = std::max(kTileW, (width - 2 * kPad - (columns - 1) * kGap) / columns);
}

float GatePalette::contentHeight() const {
	int columns;
	float tileW, tileH;
	layoutTiles(columns, tileW, tileH);
	const int rows = ((int)shown.size() + columns - 1) / columns;
	return 2 * kPad + rows * tileH + std::max(0, rows - 1) * kGap;
}

float GatePalette::viewHeight() const {
	RECT rc;
	GetClientRect(tiles, &rc);
	return (float)(rc.bottom / (dpiOf(tiles) / 96.0));
}

void GatePalette::clampScroll() {
	if (tiles == nullptr) return;
	scrollY = std::max(0.0f, std::min(scrollY, contentHeight() - viewHeight()));
}

// The overlay scrollbar's thumb (points), or an empty rect when everything fits.
D2D1_RECT_F GatePalette::scrollThumb() const {
	const float view = viewHeight(), total = contentHeight();
	if (total <= view + 1) return D2D1::RectF(0, 0, 0, 0);
	RECT rc;
	GetClientRect(tiles, &rc);
	const float w = (float)(rc.right / (dpiOf(tiles) / 96.0));
	const float th = std::max(28.0f, view * view / total);
	const float ty = (view - th) * scrollY / (total - view);
	return D2D1::RectF(w - 9, ty + 2, w - 3, ty + th - 2);
}

int GatePalette::tileAt(float px, float py) const {
	py += scrollY;
	int columns;
	float tileW, tileH;
	layoutTiles(columns, tileW, tileH);
	if (px < kPad || py < kPad) return -1;
	const int col = (int)((px - kPad) / (tileW + kGap)), row = (int)((py - kPad) / (tileH + kGap));
	if (col >= columns) return -1;
	if (std::fmod(px - kPad, tileW + kGap) >= tileW || std::fmod(py - kPad, tileH + kGap) >= tileH) return -1;
	const int i = row * columns + col;
	return i >= 0 && i < (int)shown.size() ? i : -1;
}

void GatePalette::paintTiles() {
	PAINTSTRUCT ps;
	BeginPaint(tiles, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(tiles);
	if (rt) {
		const Chrome c = chrome();
		const bool dark = c.dark;
		const double s = surface.scale();
		rt->Clear(c.panel());
		RECT rc;
		GetClientRect(tiles, &rc);
		const float viewW = (float)(rc.right / s), viewH = (float)(rc.bottom / s);
		int columns;
		float tileW, tileH;
		layoutTiles(columns, tileW, tileH);
		const D2D1_COLOR_F ink = c.barInk();
		for (int i = 0; i < (int)shown.size(); i++) {
			const int row = i / columns, col = i % columns;
			const float x = kPad + col * (tileW + kGap);
			const float y = kPad + row * (tileH + kGap) - scrollY;
			if (y + tileH < 0 || y > viewH) continue;
			if (i == hover || i == pressed)
				fillRound(rt, D2D1::RectF(x, y, x + tileW, y + tileH), 8, withAlpha(ink, i == pressed ? 0.14f : 0.07f));
			// The engine draws the gate in the tile's own points.
			rt->SetTransform(D2D1::Matrix3x2F::Translation(x, y + 2) * D2D1::Matrix3x2F::Scale((float)s, (float)s));
			guarded("drawing the palette", [&] {
				if (parts::isPart(shown[i].name)) parts::draw(shown[i].name, rt, tileW, kArtH - 4, s, dark);
				else cl_library_draw_gate(shown[i].name.c_str(), rt, tileW, kArtH - 4, s, dark);
			});
			rt->SetTransform(D2D1::Matrix3x2F::Scale((float)s, (float)s));
			if (prefs().showGateNames)
				drawText(rt, shown[i].caption, D2D1::RectF(x + 2, y + kArtH - 2, x + tileW - 2, y + tileH - 2), 10,
				         withAlpha(ink, 0.55f), TextAlign::Center);
		}
		if (shown.empty())
			drawText(rt, "No gates match.", D2D1::RectF(0, 8, viewW, 40), 12, withAlpha(ink, 0.5f), TextAlign::Center);
		// The overlay scrollbar, quiet until the pointer's over the panel.
		const D2D1_RECT_F thumb = scrollThumb();
		if (thumb.bottom > thumb.top)
			fillRound(rt, thumb, 3, withAlpha(ink, scrollDrag || scrollHot ? 0.45f : (hover >= 0 ? 0.22f : 0.14f)));
		surface.end();
	}
	EndPaint(tiles, &ps);
}

void GatePalette::drop() {
	const std::string name = shown[pressed].name;
	POINT p;
	GetCursorPos(&p);
	// Onto either side of a split: that side's page takes it.
	Canvas* c = nullptr;
	for (int pane = 0; pane < 2; pane++)
		if (Canvas* pc = win->paneCanvas(pane)) if (WindowFromPoint(p) == pc->widget()) c = pc;
	if (c && c != win->currentCanvas()) win->activatePane(win->paneOf(c));
	if (c && c == win->currentCanvas()) {
		// Same as clicking the tile: the gate lands floating, still following
		// the pointer until a click drops it -- so C-to-connect, Escape, and
		// every other in-flight key work exactly as they do after a click.
		double vx, vy, wx, wy;
		c->screenToView(p, vx, vy);
		c->worldPoint(vx, vy, wx, wy);
		if (win->addGateFloating(name, wx, wy)) c->focus();
	}
}

LRESULT CALLBACK GatePalette::hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		auto* self = static_cast<GatePalette*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		self->host = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
	}
	auto* self = reinterpret_cast<GatePalette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the palette", [&] { r = self->hostMessage(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT GatePalette::hostMessage(UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_SIZE:
		layout();
		return 0;
	case WM_ERASEBKGND:
		paintHost((HDC)wp);
		return 1;
	case WM_CTLCOLOREDIT: {
		// The search box in the field's colours.
		const Chrome c = chrome();
		const D2D1_COLOR_F field = c.dark ? D2D1::ColorF(0.16f, 0.17f, 0.20f) : D2D1::ColorF(1, 1, 1);
		if (fieldBrush == nullptr) fieldBrush = CreateSolidBrush(c.gdi(field));
		SetBkColor((HDC)wp, c.gdi(field));
		SetTextColor((HDC)wp, c.gdi(c.barInk()));
		return (LRESULT)fieldBrush;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == kSearchId && HIWORD(wp) == EN_CHANGE) { fill(); return 0; }
		break;
	}
	return DefWindowProcW(host, msg, wp, lp);
}

LRESULT CALLBACK GatePalette::tilesProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		auto* self = static_cast<GatePalette*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		self->tiles = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
	}
	auto* self = reinterpret_cast<GatePalette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the palette", [&] { r = self->tilesMessage(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT GatePalette::tilesMessage(UINT msg, WPARAM wp, LPARAM lp) {
	const double s = dpiOf(tiles) / 96.0;
	const float x = (float)(GET_X_LPARAM(lp) / s), y = (float)(GET_Y_LPARAM(lp) / s);
	switch (msg) {
	case WM_PAINT:
		paintTiles();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		clampScroll();
		InvalidateRect(tiles, nullptr, FALSE);
		return 0;
	case WM_MOUSEWHEEL:
		scrollY -= GET_WHEEL_DELTA_WPARAM(wp) * 60.0f / WHEEL_DELTA;
		clampScroll();
		InvalidateRect(tiles, nullptr, FALSE);
		return 0;
	case WM_MOUSEMOVE: {
		TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, tiles, 0 };
		TrackMouseEvent(&t);
		if (scrollDrag) {
			const float view = viewHeight(), total = contentHeight();
			const D2D1_RECT_F th = scrollThumb();
			const float travel = std::max(1.0f, view - (th.bottom - th.top + 4));
			scrollY = (y - scrollGrab) / travel * (total - view);
			clampScroll();
			InvalidateRect(tiles, nullptr, FALSE);
			return 0;
		}
		if (pressed >= 0) {
			const int slop = scaled(4, dpiOf(tiles));
			if (!dragging && (std::abs(GET_X_LPARAM(lp) - pressAt.x) > slop || std::abs(GET_Y_LPARAM(lp) - pressAt.y) > slop)) {
				dragging = true;
				SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
			}
			return 0;
		}
		const D2D1_RECT_F th = scrollThumb();
		const bool overBar = th.bottom > th.top && x >= th.left - 4;
		const int i = overBar ? -1 : tileAt(x, y);
		if (i != hover || overBar != scrollHot) {
			hover = i;
			scrollHot = overBar;
			InvalidateRect(tiles, nullptr, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (hover >= 0 || scrollHot) { hover = -1; scrollHot = false; InvalidateRect(tiles, nullptr, FALSE); }
		return 0;
	case WM_LBUTTONDOWN: {
		const D2D1_RECT_F th = scrollThumb();
		if (th.bottom > th.top && x >= th.left - 4) {
			// On the bar: grab the thumb (a click above or below it jumps there).
			if (y < th.top || y > th.bottom) {
				const float view = viewHeight(), total = contentHeight();
				scrollY = (y - (th.bottom - th.top) / 2) / std::max(1.0f, view - (th.bottom - th.top)) * (total - view);
				clampScroll();
			}
			scrollDrag = true;
			const D2D1_RECT_F now = scrollThumb();
			scrollGrab = y - (now.top - 2);
			SetCapture(tiles);
			InvalidateRect(tiles, nullptr, FALSE);
			return 0;
		}
		pressed = tileAt(x, y);
		if (pressed >= 0) {
			pressAt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			dragging = false;
			SetCapture(tiles);
			InvalidateRect(tiles, nullptr, FALSE);
		}
		return 0;
	}
	case WM_RBUTTONUP: {
		// A part of yours: rename or delete it.
		const int i = tileAt(x, y);
		if (i >= 0 && parts::isPart(shown[i].name)) {
			POINT p;
			GetCursorPos(&p);
			parts::tileMenu(tiles, shown[i].name, p);
		}
		return 0;
	}
	case WM_LBUTTONUP:
		if (scrollDrag) {
			scrollDrag = false;
			if (GetCapture() == tiles) ReleaseCapture();
			InvalidateRect(tiles, nullptr, FALSE);
			return 0;
		}
		if (pressed >= 0) {
			const int was = pressed;
			const bool dragged = dragging;
			if (GetCapture() == tiles) ReleaseCapture();
			pressed = was;
			if (dragged) drop();
			else win->addGateOnNextMove(shown[was].name);
			pressed = -1;
			dragging = false;
			InvalidateRect(tiles, nullptr, FALSE);
		}
		return 0;
	case WM_CAPTURECHANGED:
		if ((HWND)lp != tiles) {
			pressed = -1;
			dragging = scrollDrag = false;
			InvalidateRect(tiles, nullptr, FALSE);
		}
		return 0;
	}
	return DefWindowProcW(tiles, msg, wp, lp);
}

// ---- The minimap ---------------------------------------------------------------------

MiniMap::MiniMap(CircuitWindow* window, HWND parent) : win(window) {
	hwnd = CreateWindowExW(0, kMapClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, appInstance(), this);
}

MiniMap::~MiniMap() {
	if (cache) cache->Release();
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

void MiniMap::queueDraw() { InvalidateRect(hwnd, nullptr, FALSE); }

bool MiniMap::fit(double w, double h, double& left, double& bottom, double& right, double& top, double& upp) const {
	Canvas* c = win->currentCanvas();
	if (c == nullptr || w < 4 || h < 4) return false;
	const int page = c->page();
	double ox, oy;
	c->origin(ox, oy);
	const double cw = c->width(), ch = c->height(), cupp = c->unitsPerPoint();
	// The visible area (world space), unioned with the page's own bounds so
	// panning past the drawn content never runs the outline off the picture.
	double l = ox, t = oy, r = ox + cw * cupp, b = oy - ch * cupp;
	double pl, pb, pr, pt;
	if (page >= 0 && cl_document_page_bounds(win->document(), page, &pl, &pb, &pr, &pt)) {
		l = std::min(l, pl); b = std::min(b, pb); r = std::max(r, pr); t = std::max(t, pt);
	}
	l -= 2; b -= 2; r += 2; t += 2;
	upp = std::max((r - l) / w, (t - b) / h);
	if (!(upp > 0)) return false;
	const double cx = (l + r) / 2, cy = (b + t) / 2;
	left = cx - w / 2 * upp;
	top = cy + h / 2 * upp;
	bottom = top - h * upp;
	right = left + w * upp;
	return true;
}

void MiniMap::paint() {
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
	if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
	const double s = surface.scale();
	RECT rc;
	GetClientRect(hwnd, &rc);
	const double w = rc.right / s, h = rc.bottom / s;
	const bool dark = prefs().dark;
	const RGBA bg = Palette{ dark, false }.canvas();
	rt->Clear(d2dColor(bg));

	Canvas* c = win->currentCanvas();
	double left = 0, bottom = 0, right = 0, top = 0, upp = 1;
	if (c && fit(w, h, left, bottom, right, top, upp)) {
		const int page = c->page();
		// The circuit, drawn once into a layer and kept while nothing it
		// shows has changed: the canvas redraws often while a circuit runs.
		const std::string key = strf("%d|%d|%d|%d|%.3f|%.4f|%.4f|%u|%u", page, dark ? 1 : 0, (int)rc.right, (int)rc.bottom, s,
		                             left, top, win->editStamp(), (unsigned)surface.generation());
		if (cache == nullptr || key != cacheKey) {
			if (cache) { cache->Release(); cache = nullptr; }
			if (SUCCEEDED(rt->CreateCompatibleRenderTarget(&cache))) {
				cache->BeginDraw();
				cache->SetTransform(D2D1::Matrix3x2F::Scale((float)s, (float)s));
				cache->Clear(d2dColor(bg));
				if (page >= 0) {
					CLDrawOptions o{};
					o.dark = dark;
					o.accent = prefs().accent;
					o.wireScale = 1.0;
					o.simView = false;
					o.thumbnail = true;
					o.showSelection = false;
					o.selectionFade = 1;
					cl_document_draw_ex(win->document(), page, cache, s, left, top, upp, &o);
				}
				if (FAILED(cache->EndDraw())) { cache->Release(); cache = nullptr; }
				cacheKey = key;
			}
		}
		if (cache) {
			ID2D1Bitmap* bmp = nullptr;
			if (SUCCEEDED(cache->GetBitmap(&bmp))) {
				rt->DrawBitmap(bmp, D2D1::RectF(0, 0, (float)w, (float)h));
				bmp->Release();
			}
		}
		// The visible area, outlined.
		double ox, oy;
		c->origin(ox, oy);
		const double cw = c->width(), ch = c->height(), cupp = c->unitsPerPoint();
		const float x = (float)((ox - left) / upp), y = (float)((top - oy) / upp);
		const float vw = (float)std::max(3.0, cw * cupp / upp), vh = (float)std::max(3.0, ch * cupp / upp);
		const RGBA accent = accentColor(dark);
		ID2D1SolidColorBrush* brush = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF((float)accent.r, (float)accent.g, (float)accent.b, 0.9f), &brush))) {
			const float px = (float)(1 / s);
			rt->DrawRectangle(D2D1::RectF(x + px / 2, y + px / 2, x + vw, y + vh), brush, px);
			brush->Release();
		}
	}
	surface.end();
	EndPaint(hwnd, &ps);
}

void MiniMap::goTo(double vx, double vy) {
	Canvas* c = win->currentCanvas();
	RECT rc;
	GetClientRect(hwnd, &rc);
	const double s = dpiOf(hwnd) / 96.0;
	double left = 0, bottom = 0, right = 0, top = 0, upp = 1;
	if (c == nullptr || !fit(rc.right / s, rc.bottom / s, left, bottom, right, top, upp)) return;
	c->panTo(left + vx * upp, top - vy * upp);
}

LRESULT CALLBACK MiniMap::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		auto* self = static_cast<MiniMap*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		self->hwnd = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
	}
	auto* self = reinterpret_cast<MiniMap*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the minimap", [&] { r = self->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT MiniMap::handle(UINT msg, WPARAM wp, LPARAM lp) {
	const double s = dpiOf(hwnd) / 96.0;
	switch (msg) {
	case WM_PAINT:
		paint();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONDOWN:
		dragging = true;
		SetCapture(hwnd);
		goTo(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
		return 0;
	case WM_MOUSEMOVE:
		if (dragging) goTo(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s);
		return 0;
	case WM_LBUTTONUP:
		dragging = false;
		if (GetCapture() == hwnd) ReleaseCapture();
		return 0;
	case WM_CAPTURECHANGED:
		dragging = false;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
