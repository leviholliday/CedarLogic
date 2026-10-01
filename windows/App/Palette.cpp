// The gate palette and the minimap (see Palette.h).

#include "Palette.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>

namespace {

const wchar_t* kHostClass = L"CedarLogicPalette";
const wchar_t* kTilesClass = L"CedarLogicTiles";
const wchar_t* kMapClass = L"CedarLogicMiniMap";
const int kSearchId = 1, kComboId = 2;
// In points.
const int kTileW = 78, kArtH = 50, kCaptionH = 16, kGap = 2, kPad = 6, kMapH = 120;

RGBA panelColor(bool dark) { return dark ? RGBA{ 0.125, 0.13, 0.145, 1 } : RGBA{ 0.965, 0.968, 0.975, 1 }; }

}  // namespace

void registerPaletteClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = GatePalette::hostProc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = kHostClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = GatePalette::tilesProc;
	wc.hbrBackground = nullptr;
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
	search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
	                         host, (HMENU)(INT_PTR)kSearchId, appInstance(), nullptr);
	SendMessageW(search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Find a gate");
	combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 10, 300,
	                        host, (HMENU)(INT_PTR)kComboId, appInstance(), nullptr);
	tiles = CreateWindowExW(0, kTilesClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, host, nullptr,
	                        appInstance(), this);
	map = new MiniMap(window, host);

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
		if (cat.gates.empty()) continue;
		const int n = (int)categories.size() + 1;
		const std::string label = n <= 10 ? strf("%s   (Shift+%d)", cat.title.c_str(), n % 10) : cat.title;
		SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)W(label).c_str());
		categories.push_back(cat);
	}
	if (!categories.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);
	dpiChanged();
	fill();
}

GatePalette::~GatePalette() {
	delete map;
	map = nullptr;
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

void GatePalette::showCategory(int index) {
	if (index < 0 || index >= (int)categories.size()) return;
	SetWindowTextW(search, L"");
	SendMessageW(combo, CB_SETCURSEL, index, 0);
	fill();
}

void GatePalette::focusSearch() { SetFocus(search); }

void GatePalette::themeChanged() {
	InvalidateRect(tiles, nullptr, FALSE);
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
	} else {
		const int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
		if (i >= 0 && i < (int)categories.size()) shown = categories[i].gates;
	}
	scrollY = 0;
	hover = pressed = -1;
	updateScroll();
	InvalidateRect(tiles, nullptr, FALSE);
}

void GatePalette::layout() {
	if (tiles == nullptr || search == nullptr || combo == nullptr) return;   // still being made
	RECT rc;
	GetClientRect(host, &rc);
	const UINT dpi = dpiOf(host);
	const int w = rc.right, h = rc.bottom, pad = scaled(kPad, dpi);
	const int editH = scaled(24, dpi);
	MoveWindow(search, pad, pad, std::max(10, w - 2 * pad), editH, TRUE);
	MoveWindow(combo, pad, pad + editH + pad, std::max(10, w - 2 * pad), scaled(320, dpi), TRUE);
	RECT cr;
	GetWindowRect(combo, &cr);
	const int comboH = cr.bottom - cr.top;
	const int top = pad + editH + pad + comboH + pad;
	const int mapH = scaled(kMapH, dpi);
	MoveWindow(tiles, 0, top, w, std::max(10, h - top - mapH - scaled(1, dpi)), TRUE);
	if (map) MoveWindow(map->widget(), 0, h - mapH, w, mapH, TRUE);
	updateScroll();
}

// In points.
void GatePalette::layoutTiles(int& columns, int& tileW, int& tileH, int& gap, int& pad) const {
	RECT rc;
	GetClientRect(tiles, &rc);
	const double s = dpiOf(tiles) / 96.0;
	const int width = (int)(rc.right / s);
	tileW = kTileW;
	tileH = kArtH + (prefs().showGateNames ? kCaptionH : 0);
	gap = kGap;
	pad = kPad;
	columns = std::max(1, (width - 2 * pad + gap) / (tileW + gap));
	// Spread whatever width is left over the columns.
	tileW = std::max(kTileW, (width - 2 * pad - (columns - 1) * gap) / columns);
}

int GatePalette::contentHeight() const {
	int columns, tileW, tileH, gap, pad;
	layoutTiles(columns, tileW, tileH, gap, pad);
	const int rows = ((int)shown.size() + columns - 1) / columns;
	return 2 * pad + rows * tileH + std::max(0, rows - 1) * gap;
}

void GatePalette::updateScroll() {
	if (tiles == nullptr) return;
	RECT rc;
	GetClientRect(tiles, &rc);
	const double s = dpiOf(tiles) / 96.0;
	const int page = (int)(rc.bottom / s);
	const int total = contentHeight();
	scrollY = std::max(0, std::min(scrollY, total - page));
	SCROLLINFO si = { sizeof si, SIF_ALL | SIF_DISABLENOSCROLL, 0, std::max(0, total - 1), (UINT)std::max(1, page), scrollY, 0 };
	SetScrollInfo(tiles, SB_VERT, &si, TRUE);
}

int GatePalette::tileAt(int x, int y) const {
	const double s = dpiOf(tiles) / 96.0;
	const int px = (int)(x / s), py = (int)(y / s) + scrollY;
	int columns, tileW, tileH, gap, pad;
	layoutTiles(columns, tileW, tileH, gap, pad);
	if (px < pad || py < pad) return -1;
	const int col = (px - pad) / (tileW + gap), row = (py - pad) / (tileH + gap);
	if (col >= columns) return -1;
	if ((px - pad) % (tileW + gap) >= tileW || (py - pad) % (tileH + gap) >= tileH) return -1;
	const int i = row * columns + col;
	return i >= 0 && i < (int)shown.size() ? i : -1;
}

void GatePalette::paintTiles() {
	PAINTSTRUCT ps;
	BeginPaint(tiles, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(tiles);
	if (rt) {
		const bool dark = prefs().dark;
		const double s = surface.scale();
		rt->Clear(d2dColor(panelColor(dark)));
		RECT rc;
		GetClientRect(tiles, &rc);
		const double viewH = rc.bottom / s;
		int columns, tileW, tileH, gap, pad;
		layoutTiles(columns, tileW, tileH, gap, pad);
		const double ink = dark ? 1 : 0;
		ID2D1SolidColorBrush* brush = nullptr;
		rt->CreateSolidColorBrush(D2D1::ColorF((float)ink, (float)ink, (float)ink, 0.08f), &brush);
		for (int i = 0; i < (int)shown.size(); i++) {
			const int row = i / columns, col = i % columns;
			const float x = (float)(pad + col * (tileW + gap));
			const float y = (float)(pad + row * (tileH + gap) - scrollY);
			if (y + tileH < 0 || y > viewH) continue;
			if ((i == hover || i == pressed) && brush) {
				brush->SetColor(D2D1::ColorF((float)ink, (float)ink, (float)ink, i == pressed ? 0.15f : 0.08f));
				rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + tileW, y + tileH), 6, 6), brush);
			}
			// The engine draws the gate in the tile's own points.
			rt->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * D2D1::Matrix3x2F::Scale((float)s, (float)s));
			guarded("drawing the palette", [&] {
				cl_library_draw_gate(shown[i].name.c_str(), rt, tileW, kArtH, s, dark);
			});
			rt->SetTransform(D2D1::Matrix3x2F::Scale((float)s, (float)s));
			if (prefs().showGateNames) {
				const float c = dark ? 0.72f : 0.35f;
				drawText(rt, shown[i].caption, D2D1::RectF(x + 2, y + kArtH, x + tileW - 2, y + tileH), 10.5f,
				         D2D1::ColorF(c, c, c, 1), TextAlign::Center);
			}
		}
		if (shown.empty()) {
			const float c = dark ? 0.6f : 0.45f;
			drawText(rt, "No gates match.", D2D1::RectF(0, 8, (float)(rc.right / s), 40), 12, D2D1::ColorF(c, c, c, 1),
			         TextAlign::Center);
		}
		if (brush) brush->Release();
		surface.end();
	}
	EndPaint(tiles, &ps);
}

void GatePalette::drop() {
	const std::string name = shown[pressed].name;
	POINT p;
	GetCursorPos(&p);
	Canvas* c = win->currentCanvas();
	if (c && WindowFromPoint(p) == c->widget()) {
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
	case WM_COMMAND:
		if (LOWORD(wp) == kSearchId && HIWORD(wp) == EN_CHANGE) { fill(); return 0; }
		if (LOWORD(wp) == kComboId && HIWORD(wp) == CBN_SELCHANGE) {
			if (GetWindowTextLengthW(search) > 0) SetWindowTextW(search, L"");   // fills
			else fill();
			return 0;
		}
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
	switch (msg) {
	case WM_PAINT:
		paintTiles();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		updateScroll();
		InvalidateRect(tiles, nullptr, FALSE);
		return 0;
	case WM_VSCROLL: {
		SCROLLINFO si = { sizeof si, SIF_ALL };
		GetScrollInfo(tiles, SB_VERT, &si);
		int y = si.nPos;
		switch (LOWORD(wp)) {
		case SB_LINEUP: y -= 30; break;
		case SB_LINEDOWN: y += 30; break;
		case SB_PAGEUP: y -= (int)si.nPage; break;
		case SB_PAGEDOWN: y += (int)si.nPage; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = si.nMax; break;
		}
		scrollY = y;
		updateScroll();
		InvalidateRect(tiles, nullptr, FALSE);
		return 0;
	}
	case WM_MOUSEWHEEL:
		scrollY -= GET_WHEEL_DELTA_WPARAM(wp) * 60 / WHEEL_DELTA;
		updateScroll();
		InvalidateRect(tiles, nullptr, FALSE);
		return 0;
	case WM_MOUSEMOVE: {
		const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
		if (pressed >= 0) {
			const int slop = scaled(4, dpiOf(tiles));
			if (!dragging && (std::abs(x - pressAt.x) > slop || std::abs(y - pressAt.y) > slop)) {
				dragging = true;
				SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
			}
			return 0;
		}
		const int i = tileAt(x, y);
		if (i != hover) {
			hover = i;
			InvalidateRect(tiles, nullptr, FALSE);
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, tiles, 0 };
			TrackMouseEvent(&t);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (hover >= 0) { hover = -1; InvalidateRect(tiles, nullptr, FALSE); }
		return 0;
	case WM_LBUTTONDOWN:
		pressed = tileAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		if (pressed >= 0) {
			pressAt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			dragging = false;
			SetCapture(tiles);
			InvalidateRect(tiles, nullptr, FALSE);
		}
		return 0;
	case WM_LBUTTONUP:
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
		if ((HWND)lp != tiles) { pressed = -1; dragging = false; InvalidateRect(tiles, nullptr, FALSE); }
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
		const std::string key = strf("%d|%d|%d|%d|%.3f|%.4f|%.4f|%u|%p", page, dark ? 1 : 0, (int)rc.right, (int)rc.bottom, s,
		                             left, top, win->editStamp(), (void*)rt);
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
