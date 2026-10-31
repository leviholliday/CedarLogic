// Short fades (see Fade.h).

#include "Fade.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fade {

namespace {

const wchar_t* kClass = L"CedarLogicFade";
const UINT_PTR kPictureTimer = 1, kFadeInTimer = 0xFAD1, kFadeOutTimer = 0xFAD2;

// A cross-fade's picture: kept until it's gone, so clipTo can lay a smaller
// part of it again.
struct Picture {
	HDC dc = nullptr;
	HBITMAP bitmap = nullptr;
	HGDIOBJ old = nullptr;
	RECT at{};            // where it is on the screen now
	POINT from{};         // the part of the bitmap showing (after a clip)
	double start = -1;    // the first frame (-1: not shown yet)
	double seconds = 0.17;
	BYTE alpha = 255;
};

double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

void layAgain(HWND h, Picture* p) {
	POINT dst = { p->at.left, p->at.top };
	SIZE size = { p->at.right - p->at.left, p->at.bottom - p->at.top };
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, p->alpha, 0 };
	UpdateLayeredWindow(h, nullptr, &dst, &size, p->dc, &p->from, 0, &blend, ULW_ALPHA);
}

void CALLBACK pictureTick(HWND h, UINT, UINT_PTR, DWORD) {
	Picture* p = reinterpret_cast<Picture*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (p == nullptr) { KillTimer(h, kPictureTimer); DestroyWindow(h); return; }
	const double now = nowSeconds();
	// The first frame stays whole: the window under it has had a moment to
	// draw what changed.
	if (p->start < 0) { p->start = now; return; }
	const double t = clamp01((now - p->start) / p->seconds);
	if (t >= 1) {
		KillTimer(h, kPictureTimer);
		DestroyWindow(h);
		return;
	}
	// The new look comes in easing out, as the Mac's .easeOut: the old one
	// goes (1 - t)^2.
	p->alpha = (BYTE)std::lround(255 * (1 - t) * (1 - t));
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, p->alpha, 0 };
	UpdateLayeredWindow(h, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
}

LRESULT CALLBACK pictureProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_ERASEBKGND: return 1;
	case WM_NCDESTROY: {
		Picture* p = reinterpret_cast<Picture*>(GetWindowLongPtrW(h, GWLP_USERDATA));
		SetWindowLongPtrW(h, GWLP_USERDATA, 0);
		if (p) {
			if (p->dc) { SelectObject(p->dc, p->old); DeleteDC(p->dc); }
			if (p->bitmap) DeleteObject(p->bitmap);
			delete p;
		}
		break;
	}
	}
	return DefWindowProcW(h, msg, wp, lp);
}

void registerClass() {
	static bool done = false;
	if (done) return;
	done = true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = pictureProc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);
}

// Numbers kept on a window as properties (no memory to free if the window
// goes mid-fade).
void setNumber(HWND h, const wchar_t* name, intptr_t v) { SetPropW(h, name, (HANDLE)v); }
intptr_t number(HWND h, const wchar_t* name) { return (intptr_t)GetPropW(h, name); }
long long nowMs() { return (long long)(nowSeconds() * 1000); }

void CALLBACK fadeInTick(HWND h, UINT, UINT_PTR, DWORD) {
	if (!IsWindowVisible(h)) return;   // a dialog shows after WM_INITDIALOG
	if (number(h, L"clFadeAt") == 0) setNumber(h, L"clFadeAt", (intptr_t)std::max(1LL, nowMs()));
	const double ms = (double)std::max<intptr_t>(1, number(h, L"clFadeMs"));
	const double t = clamp01((nowMs() - number(h, L"clFadeAt")) / ms);
	if (t >= 1) {
		KillTimer(h, kFadeInTimer);
		RemovePropW(h, L"clFadeAt");
		RemovePropW(h, L"clFadeMs");
		SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
		// Opaque again for good (the system can drop the layer), drawn at
		// once so there's no frame without it.
		SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
		RedrawWindow(h, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
		return;
	}
	const double e = 1 - (1 - t) * (1 - t);
	SetLayeredWindowAttributes(h, 0, (BYTE)std::lround(255 * e), LWA_ALPHA);
}

void CALLBACK fadeOutTick(HWND h, UINT, UINT_PTR, DWORD) {
	if (number(h, L"clFadeAt") == 0) setNumber(h, L"clFadeAt", (intptr_t)std::max(1LL, nowMs()));
	const double ms = (double)std::max<intptr_t>(1, number(h, L"clFadeMs"));
	const double t = clamp01((nowMs() - number(h, L"clFadeAt")) / ms);
	const BYTE from = (BYTE)number(h, L"clFadeFrom");
	if (t >= 1) {
		KillTimer(h, kFadeOutTimer);
		RemovePropW(h, L"clFadeAt");
		RemovePropW(h, L"clFadeMs");
		RemovePropW(h, L"clFadeFrom");
		DestroyWindow(h);
		return;
	}
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE)std::lround(from * (1 - t)), AC_SRC_ALPHA };
	UpdateLayeredWindow(h, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &blend, ULW_ALPHA);
}

}  // namespace

bool animations() {
	BOOL on = TRUE;
	SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
	return on != FALSE;
}

HWND crossfade(HWND w, const RECT* area, int ms) {
	if (!animations() || w == nullptr || !IsWindowVisible(w)) return nullptr;
	HWND root = GetAncestor(w, GA_ROOT);
	if (root == nullptr || IsIconic(root) || !IsWindowVisible(root)) return nullptr;
	RECT r;
	if (area) r = *area;
	else GetClientRect(w, &r);
	MapWindowPoints(w, nullptr, (POINT*)&r, 2);
	// Only what's on a screen can be copied from it.
	const RECT screens = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
	                       GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
	                       GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
	if (!IntersectRect(&r, &r, &screens)) return nullptr;
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	if (pw < 2 || ph < 2) return nullptr;

	registerClass();
	Picture* p = new Picture();
	p->at = r;
	p->seconds = std::max(1, ms) / 1000.0;
	HDC screen = GetDC(nullptr);
	p->dc = CreateCompatibleDC(screen);
	p->bitmap = CreateCompatibleBitmap(screen, pw, ph);
	bool copied = false;
	if (p->dc && p->bitmap) {
		p->old = SelectObject(p->dc, p->bitmap);
		// CAPTUREBLT: layered windows too (a fade still going, a toast).
		copied = BitBlt(p->dc, 0, 0, pw, ph, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
	}
	if (!copied) {
		ReleaseDC(nullptr, screen);
		if (p->dc) { if (p->old) SelectObject(p->dc, p->old); DeleteDC(p->dc); }
		if (p->bitmap) DeleteObject(p->bitmap);
		delete p;
		return nullptr;
	}
	HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP, r.left, r.top, pw,
	                         ph, root, nullptr, appInstance(), nullptr);
	if (h == nullptr) {
		ReleaseDC(nullptr, screen);
		SelectObject(p->dc, p->old);
		DeleteDC(p->dc);
		DeleteObject(p->bitmap);
		delete p;
		return nullptr;
	}
	SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
	POINT dst = { r.left, r.top };
	SIZE size = { pw, ph };
	BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
	UpdateLayeredWindow(h, screen, &dst, &size, p->dc, &p->from, 0, &blend, ULW_ALPHA);
	ReleaseDC(nullptr, screen);
	// Over the window it copies (it belongs to it, so it stays just above it).
	SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	// On the screen before anything changes under it.
	DwmFlush();
	SetTimer(h, kPictureTimer, 10, pictureTick);
	return h;
}

void clipTo(HWND picture, HWND w) {
	if (picture == nullptr || !IsWindow(picture) || w == nullptr) return;
	Picture* p = reinterpret_cast<Picture*>(GetWindowLongPtrW(picture, GWLP_USERDATA));
	if (p == nullptr) return;
	RECT c;
	GetClientRect(w, &c);
	MapWindowPoints(w, nullptr, (POINT*)&c, 2);
	RECT keep;
	if (!IntersectRect(&keep, &p->at, &c)) { DestroyWindow(picture); return; }
	if (EqualRect(&keep, &p->at)) return;
	p->from.x += keep.left - p->at.left;
	p->from.y += keep.top - p->at.top;
	p->at = keep;
	layAgain(picture, p);
}

void fadeIn(HWND top, int ms) {
	if (!animations() || top == nullptr) return;
	const LONG ex = GetWindowLongW(top, GWL_EXSTYLE);
	if (ex & WS_EX_LAYERED) return;
	SetWindowLongW(top, GWL_EXSTYLE, ex | WS_EX_LAYERED);
	SetLayeredWindowAttributes(top, 0, 0, LWA_ALPHA);
	setNumber(top, L"clFadeMs", std::max(1, ms));
	setNumber(top, L"clFadeAt", 0);
	SetTimer(top, kFadeInTimer, 10, fadeInTick);
}

void fadeOutAndDestroy(HWND layered, BYTE alpha, int ms) {
	if (layered == nullptr) return;
	if (!animations() || alpha == 0) { DestroyWindow(layered); return; }
	setNumber(layered, L"clFadeMs", std::max(1, ms));
	setNumber(layered, L"clFadeAt", 0);
	setNumber(layered, L"clFadeFrom", alpha);
	SetTimer(layered, kFadeOutTimer, 10, fadeOutTick);
}

}  // namespace fade
