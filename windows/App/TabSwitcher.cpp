// Ctrl+Tab (see TabSwitcher.h).

#include "TabSwitcher.h"
#include "Canvas.h"
#include "Chrome.h"
#include "Fade.h"
#include "Images.h"
#include "Window.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>

namespace {
// In points, as the Mac's (TabSwitcher.swift); the corner a little rounder
// than Windows' own 8 px, the Mac's being 28.
const float kThumbW = 208, kThumbH = 130, kLabelH = 18, kLabelGap = 8, kCardPad = 10, kGap = 6, kPad = 14;
const float kRadius = 20, kCardRadius = 14, kThumbRadius = 9;
const float kMargin = 26;   // round the panel, for its shadow
const int kPerRow = 5;
const double kFadeIn = 0.12;
const wchar_t* kClass = L"CedarLogicTabSwitcher";
}

TabSwitcher::~TabSwitcher() {
	hide();
	release();
}

bool TabSwitcher::key(bool backwards) {
	if (isActive) {
		const int n = (int)tabs.size();
		if (n > 0) selected = (selected + (backwards ? n - 1 : 1)) % n;
		if (panel) { render(); present(); }
		return true;
	}
	tabs = win->recentTabs();
	if (tabs.size() > 10) tabs.resize(10);
	if (tabs.size() < 2) return false;
	isActive = true;
	selected = backwards ? (int)tabs.size() - 1 : 1;
	started = nowSeconds();
	return true;
}

void TabSwitcher::tick() {
	if (!isActive) return;
	if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) { commit(selected); return; }
	// Only if Ctrl is still down after a moment: a quick Ctrl+Tab just flips
	// to the previous tab.
	if (panel == nullptr && nowSeconds() - started >= 0.18) show();
	// Fading in (ease out).
	if (panel && alpha < 255) {
		const double t = fade::animations() ? std::min(1.0, (nowSeconds() - shownTime) / kFadeIn) : 1;
		alpha = (BYTE)std::lround(255 * (1 - (1 - t) * (1 - t)));
		present();
	}
}

bool TabSwitcher::showForTest() {
	tabs = win->recentTabs();
	if (tabs.size() > 10) tabs.resize(10);
	if (tabs.size() < 2) return false;
	isActive = false;   // nothing watches Ctrl
	selected = 1;
	show();
	if (panel == nullptr) return false;
	alpha = 255;
	present();
	return true;
}

bool TabSwitcher::saveFrame(const std::string& file) {
	if (panel == nullptr || dib == nullptr) return false;
	DIBSECTION ds = {};
	if (GetObjectW(dib, sizeof ds, &ds) == 0 || ds.dsBm.bmBits == nullptr) return false;
	GdiFlush();
	IWICImagingFactory* wic = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
	IWICBitmap* b = nullptr;
	if (wic == nullptr) return false;
	if (FAILED(wic->CreateBitmapFromMemory((UINT)pixelW, (UINT)pixelH, GUID_WICPixelFormat32bppPBGRA, (UINT)ds.dsBm.bmWidthBytes,
	                                                       (UINT)(ds.dsBm.bmWidthBytes * pixelH), (BYTE*)ds.dsBm.bmBits, &b)))
	{
		wic->Release();
		return false;
	}
	const bool ok = images::savePng(b, file);
	b->Release();
	wic->Release();
	return ok;
}

void TabSwitcher::commit(int index) {
	if (!isActive) return;
	const int target = index >= 0 && index < (int)tabs.size() ? tabs[index] : -1;
	cancel();
	if (target >= 0 && index != 0) win->showPage(target);
}

void TabSwitcher::cancel() {
	isActive = false;
	hide();
}

void TabSwitcher::show() {
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = proc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = kClass;
		RegisterClassExW(&wc);
	}
	HWND owner = win->window();
	scale = dpiOf(owner) / 96.0f;
	const int n = (int)tabs.size(), perRow = std::min(n, kPerRow), rows = (n + kPerRow - 1) / kPerRow;
	const float cellW = kThumbW + 2 * kCardPad, cellH = kCardPad + kThumbH + kLabelGap + kLabelH + kCardPad;
	panelW = perRow * cellW + (perRow - 1) * kGap + 2 * kPad;
	panelH = rows * cellH + (rows - 1) * kGap + 2 * kPad;
	cards.clear();
	for (int i = 0; i < n; i++) {
		const float x = kPad + (i % kPerRow) * (cellW + kGap), y = kPad + (i / kPerRow) * (cellH + kGap);
		cards.push_back(D2D1::RectF(x, y, x + cellW, y + cellH));
	}
	pixelW = (int)std::ceil((panelW + 2 * kMargin) * scale);
	pixelH = (int)std::ceil((panelH + 2 * kMargin) * scale);

	// The bitmap it's drawn in, and a Direct2D target on it.
	release();
	HDC screen = GetDC(nullptr);
	memDC = CreateCompatibleDC(screen);
	ReleaseDC(nullptr, screen);
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof bi.bmiHeader;
	bi.bmiHeader.biWidth = pixelW;
	bi.bmiHeader.biHeight = -pixelH;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	dib = memDC ? CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
	if (dib == nullptr) { release(); return; }
	oldBitmap = SelectObject(memDC, dib);
	// At 96 dpi, scaled by a transform (as the canvas's surface is): the
	// engine's drawing expects that.
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	if (FAILED(d2dFactory()->CreateDCRenderTarget(&props, &rt))) { release(); return; }

	RECT o;
	GetWindowRect(owner, &o);
	panel = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kClass, L"", WS_POPUP,
	                        (o.left + o.right - pixelW) / 2, (o.top + o.bottom - pixelH) / 2, pixelW, pixelH, owner, nullptr, appInstance(), this);
	if (panel == nullptr) { release(); return; }
	GetCursorPos(&shownAt);
	alpha = fade::animations() ? 0 : 255;
	shownTime = nowSeconds();
	render();
	present();
	ShowWindow(panel, SW_SHOWNA);
}

void TabSwitcher::hide() {
	if (panel == nullptr) return;
	SetWindowLongPtrW(panel, GWLP_USERDATA, 0);
	// It fades away on its own (the picture it last had stays with it).
	const HWND going = panel;
	panel = nullptr;
	release();
	fade::fadeOutAndDestroy(going, alpha, 100);
}

void TabSwitcher::release() {
	for (ID2D1Bitmap* b : thumbs) if (b) b->Release();
	thumbs.clear();
	if (rt) { rt->Release(); rt = nullptr; }
	if (memDC) {
		if (oldBitmap) SelectObject(memDC, oldBitmap);
		DeleteDC(memDC);
		memDC = nullptr;
	}
	oldBitmap = nullptr;
	if (dib) { DeleteObject(dib); dib = nullptr; }
}

// Each tab's page, fitted, on the canvas's colour: drawn once a showing (a
// big circuit would make every Tab press slow otherwise).
void TabSwitcher::makeThumbs() {
	for (ID2D1Bitmap* b : thumbs) if (b) b->Release();
	thumbs.assign(tabs.size(), nullptr);
	const Chrome c = chrome();
	const bool dark = c.dark;
	const float tw = std::ceil(kThumbW * scale), th = std::ceil(kThumbH * scale);
	for (size_t i = 0; i < tabs.size(); i++) {
		ID2D1BitmapRenderTarget* b = nullptr;
		if (FAILED(rt->CreateCompatibleRenderTarget(D2D1::SizeF(tw, th), &b)) || b == nullptr) continue;
		b->BeginDraw();
		b->Clear(c.canvas());
		b->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
		b->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		const int page = win->pageOfTab(tabs[i]);
		bool drew = false;
		if (page >= 0) drew = cl_document_draw_fitted(win->document(), page, b, kThumbW, kThumbH, 10, scale, dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
		if (!drew)
			drawText(b, "Empty", D2D1::RectF(0, kThumbH / 2 - 9, kThumbW, kThumbH / 2 + 9), 12,
			         dark ? D2D1::ColorF(1, 1, 1, 0.35f) : D2D1::ColorF(0, 0, 0, 0.32f), TextAlign::Center);
		b->SetTransform(D2D1::Matrix3x2F::Identity());
		if (SUCCEEDED(b->EndDraw())) b->GetBitmap(&thumbs[i]);
		b->Release();
	}
}

void TabSwitcher::render() {
	if (rt == nullptr || memDC == nullptr) return;
	RECT r = { 0, 0, pixelW, pixelH };
	if (FAILED(rt->BindDC(memDC, &r))) return;
	rt->BeginDraw();
	if (thumbs.size() != tabs.size()) makeThumbs();
	rt->SetTransform(D2D1::Matrix3x2F::Identity());
	rt->Clear(D2D1::ColorF(0, 0, 0, 0));
	rt->SetTransform(D2D1::Matrix3x2F::Translation(kMargin, kMargin) * D2D1::Matrix3x2F::Scale(scale, scale));
	// Text on a target that's see-through in places: greyscale, not ClearType.
	rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

	const Chrome c = chrome();
	const bool dark = c.dark;
	const D2D1_COLOR_F ink = dark ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 1);
	const D2D1_COLOR_F accent = c.accent();
	const D2D1_RECT_F body = D2D1::RectF(0, 0, panelW, panelH);

	// A soft shadow, a little below (rings of falling alpha stand in for a blur).
	for (int i = 12; i >= 1; i--) {
		const float g = i * 1.8f;
		const D2D1_RECT_F s = D2D1::RectF(body.left - g, body.top - g + 6, body.right + g, body.bottom + g + 6);
		fillRound(rt, s, kRadius + g, D2D1::ColorF(0, 0, 0, (dark ? 0.05f : 0.028f) * (1 - i / 13.0f)));
	}
	// The HUD: nearly opaque, as the Mac's glass reads over a busy circuit.
	fillRound(rt, body, kRadius, dark ? rgb255(38, 41, 48, 0.97f) : rgb255(250, 250, 252, 0.97f));
	strokeRound(rt, D2D1::RectF(body.left + 0.5f, body.top + 0.5f, body.right - 0.5f, body.bottom - 0.5f), kRadius,
	            withAlpha(ink, dark ? 0.14f : 0.10f), 1);

	for (size_t i = 0; i < cards.size() && i < tabs.size(); i++) {
		const D2D1_RECT_F& card = cards[i];
		const bool on = (int)i == selected;
		if (on) {
			fillRound(rt, card, kCardRadius, withAlpha(accent, dark ? 0.22f : 0.13f));
			strokeRound(rt, card, kCardRadius, withAlpha(accent, dark ? 0.45f : 0.35f), 1);
		}
		const D2D1_RECT_F thumb =
			D2D1::RectF(card.left + kCardPad, card.top + kCardPad, card.left + kCardPad + kThumbW, card.top + kCardPad + kThumbH);
		// The picture, in its rounded corners (through a brush of it).
		ID2D1BitmapBrush* brush = nullptr;
		if (i < thumbs.size() && thumbs[i] && SUCCEEDED(rt->CreateBitmapBrush(thumbs[i], &brush))) {
			brush->SetTransform(D2D1::Matrix3x2F::Scale(1 / scale, 1 / scale) * D2D1::Matrix3x2F::Translation(thumb.left, thumb.top));
			rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, kThumbRadius, kThumbRadius), brush);
			brush->Release();
		} else {
			fillRound(rt, thumb, kThumbRadius, c.canvas());
		}
		if (on) strokeRound(rt, D2D1::RectF(thumb.left - 1, thumb.top - 1, thumb.right + 1, thumb.bottom + 1), kThumbRadius + 1, accent, 2);
		else strokeRound(rt, thumb, kThumbRadius, withAlpha(ink, dark ? 0.14f : 0.12f), 0.75f);
		drawText(rt, win->tabName(tabs[i]), D2D1::RectF(thumb.left, thumb.bottom + kLabelGap, thumb.right, thumb.bottom + kLabelGap + kLabelH),
		         12, withAlpha(ink, on ? 0.95f : 0.62f), TextAlign::Center, on);
	}
	if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
		// The display took the target away: made again next time.
		for (ID2D1Bitmap* b : thumbs) if (b) b->Release();
		thumbs.clear();
		rt->Release();
		rt = nullptr;
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
		d2dFactory()->CreateDCRenderTarget(&props, &rt);
	}
}

void TabSwitcher::present() {
	if (panel == nullptr || memDC == nullptr) return;
	RECT at;
	GetWindowRect(panel, &at);
	POINT dst = { at.left, at.top }, src = { 0, 0 };
	SIZE size = { pixelW, pixelH };
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
	UpdateLayeredWindow(panel, nullptr, &dst, &size, memDC, &src, 0, &blend, ULW_ALPHA);
}

LRESULT CALLBACK TabSwitcher::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
	TabSwitcher* t = reinterpret_cast<TabSwitcher*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
	if (t == nullptr) return DefWindowProcW(h, msg, wp, lp);
	if (msg == WM_NCCREATE) t->panel = h;
	// In the panel's points (inside the shadow's margin).
	const float x = GET_X_LPARAM(lp) / t->scale - kMargin, y = GET_Y_LPARAM(lp) / t->scale - kMargin;
	switch (msg) {
	case WM_DESTROY:
		// Its window closed under it (the circuit window went).
		if (t->panel == h) t->panel = nullptr;
		break;
	case WM_MOUSEMOVE: {
		POINT p;
		GetCursorPos(&p);
		if (p.x == t->shownAt.x && p.y == t->shownAt.y) return 0;   // it opened under the pointer
		for (size_t i = 0; i < t->cards.size(); i++)
			if (inRect(t->cards[i], x, y) && (int)i != t->selected) {
				t->selected = (int)i;
				guarded("the tab switcher", [&] { t->render(); t->present(); });
			}
		return 0;
	}
	case WM_LBUTTONUP:
		for (size_t i = 0; i < t->cards.size(); i++)
			if (inRect(t->cards[i], x, y)) { const int idx = (int)i; PostMessageW(h, WM_APP, idx, 0); }
		return 0;
	case WM_APP: t->commit((int)wp); return 0;
	}
	return DefWindowProcW(h, msg, wp, lp);
}
