// Drawn (see Drawn.h).

#include "Drawn.h"

namespace {
const wchar_t* kClass = L"CedarLogicDrawn";

void registerClass() {
	static bool done = false;
	if (done) return;
	done = true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = DefWindowProcW;   // replaced per window in create()
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);
}
}  // namespace

Drawn::~Drawn() {
	if (tooltip) DestroyWindow(tooltip);
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

void Drawn::create(HWND parent, DWORD style) {
	registerClass();
	hwnd = CreateWindowExW(0, kClass, L"", style, 0, 0, 10, 10, parent, nullptr, appInstance(), nullptr);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)this);
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
}

double Drawn::width() const {
	RECT rc;
	GetClientRect(hwnd, &rc);
	return rc.right / scale();
}

double Drawn::height() const {
	RECT rc;
	GetClientRect(hwnd, &rc);
	return rc.bottom / scale();
}

void Drawn::cursorPoint(float& x, float& y) const {
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(hwnd, &p);
	x = (float)(p.x / scale());
	y = (float)(p.y / scale());
}

void Drawn::setTips(const std::vector<Tip>& tips) {
	if (tooltip == nullptr) {
		tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, appInstance(), nullptr);
	}
	const int n = (int)SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0);
	for (int i = n - 1; i >= 0; i--) {
		TTTOOLINFOW ti = {};
		ti.cbSize = sizeof ti;
		if (SendMessageW(tooltip, TTM_ENUMTOOLSW, i, (LPARAM)&ti)) SendMessageW(tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
	}
	tipTexts.clear();
	for (const Tip& t : tips) tipTexts.push_back(W(t.text));
	const double s = scale();
	for (size_t i = 0; i < tips.size(); i++) {
		TTTOOLINFOW ti = {};
		ti.cbSize = sizeof ti;
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = hwnd;
		ti.uId = i + 1;
		ti.rect = { (LONG)(tips[i].rect.left * s), (LONG)(tips[i].rect.top * s), (LONG)(tips[i].rect.right * s),
		            (LONG)(tips[i].rect.bottom * s) };
		ti.lpszText = const_cast<wchar_t*>(tipTexts[i].c_str());
		SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	}
}

LRESULT CALLBACK Drawn::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	Drawn* d = reinterpret_cast<Drawn*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (d == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the window", [&] { r = d->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT Drawn::handle(UINT msg, WPARAM wp, LPARAM lp) {
	bool handled = false;
	const LRESULT own = message(msg, wp, lp, handled);
	if (handled) return own;
	const double s = scale();
	auto px = [&] { return (float)(GET_X_LPARAM(lp) / s); };
	auto py = [&] { return (float)(GET_Y_LPARAM(lp) / s); };
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		if (ID2D1HwndRenderTarget* rt = surface.begin(hwnd)) {
			paint(rt, (float)width(), (float)height());
			surface.end();
		}
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
		redraw();
		return 0;
	case WM_NCHITTEST: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ScreenToClient(hwnd, &p);
		return hitTest((float)(p.x / s), (float)(p.y / s));
	}
	case WM_MOUSEMOVE:
		if (!pointerIn) {
			pointerIn = true;
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
		}
		mouseMove(px(), py());
		return 0;
	case WM_MOUSELEAVE:
		pointerIn = false;
		mouseLeave();
		return 0;
	case WM_LBUTTONDOWN: SetCapture(hwnd); mouseDown(1, px(), py(), false); return 0;
	case WM_LBUTTONDBLCLK: SetCapture(hwnd); mouseDown(1, px(), py(), true); return 0;
	case WM_MBUTTONDOWN: mouseDown(2, px(), py(), false); return 0;
	case WM_RBUTTONDOWN: mouseDown(3, px(), py(), false); return 0;
	case WM_LBUTTONUP:
		// Let go of the pointer first (the button may open a dialog), but
		// not as a lost capture: that would forget the button that was
		// pressed before mouseUp could act on it.
		if (GetCapture() == hwnd) {
			releasing = true;
			ReleaseCapture();
			releasing = false;
		}
		mouseUp(1, px(), py());
		return 0;
	case WM_MBUTTONUP: mouseUp(2, px(), py()); return 0;
	case WM_RBUTTONUP: mouseUp(3, px(), py()); return 0;
	case WM_MOUSEWHEEL: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ScreenToClient(hwnd, &p);
		wheel(GET_WHEEL_DELTA_WPARAM(wp), (float)(p.x / s), (float)(p.y / s));
		return 0;
	}
	case WM_CAPTURECHANGED:
		if ((HWND)lp != hwnd && !releasing) captureLost();
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
