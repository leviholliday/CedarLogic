// The oscilloscope and its timing diagrams (see ScopeWindow in Dialogs.h, and
// the Mac app's ScopeView.swift and TimingDiagram.swift).

#include "Chrome.h"
#include "Dialogs.h"
#include "Images.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {

const wchar_t* kScopeClass = L"CedarLogicScope";
const float kHeader = 40, kNameWidth = 130, kLane = 30, kRuler = 22;
enum { kBtnHidden = 1, kBtnShare, kBtnOut, kBtnIn, kBtnLive, kBtnClear, kBtnClose };

const int kTickEvery[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };

int tickEvery(float pps, float minGap) {
	for (int e : kTickEvery) if (e * pps >= minGap) return e;
	return 10000;
}

const char* valueLabel(unsigned char v) {
	switch (v) { case 0: return "0"; case 1: return "1"; case 2: return "Z"; case 3: return "!"; case 4: return "?"; default: return "–"; }
}

D2D1_COLOR_F valueColor(unsigned char v, D2D1_COLOR_F dim) {
	switch (v) {
	case 1: return D2D1::ColorF(0.2f, 0.75f, 0.35f);
	case 2: return D2D1::ColorF(0.24f, 0.48f, 0.98f);
	case 3: return D2D1::ColorF(0.92f, 0.26f, 0.24f);
	case 4: return D2D1::ColorF(0.96f, 0.58f, 0.13f);
	default: return dim;
	}
}

// ---- Timing diagrams ---------------------------------------------------------------
// The oscilloscope for a lab report: white paper, black traces, each
// signal's name, the step numbers along the bottom, and a title (with your
// name, as image exports have).

const float kTMargin = 28, kTName = 110, kTLane = 38, kTTitle = 46, kTAxis = 34;

void timingSize(int steps, int signals, float& w, float& h, float& pps) {
	const int n = std::max(1, steps);
	// Readable steps where there's room; squeezed to fit a wide page otherwise.
	pps = std::min(24.0f, std::max(0.5f, 1400.0f / n));
	w = std::max(520.0f, kTMargin * 2 + kTName + n * pps);
	h = kTMargin * 2 + kTTitle + std::max(1, signals) * kTLane + kTAxis;
}

void drawTiming(ID2D1RenderTarget* rt, CLDocument* doc, const std::vector<int>& sigs, const std::vector<std::string>& names, int from,
                int count, const std::string& title, bool color) {
	float w, h, pps;
	timingSize(count, (int)sigs.size(), w, h, pps);
	const D2D1_COLOR_F black = D2D1::ColorF(0, 0, 0), gray = D2D1::ColorF(0.33f, 0.33f, 0.33f), light = D2D1::ColorF(0.82f, 0.82f, 0.82f);
	drawText(rt, title, D2D1::RectF(kTMargin, kTMargin, w - kTMargin, kTMargin + 22), 17, black, TextAlign::Leading, true);
	std::string byline = "Timing diagram";
	if (!prefs().studentName.empty()) byline += " · " + prefs().studentName;
	char date[64];
	const time_t t = time(nullptr);
	struct tm lt;
	localtime_s(&lt, &t);
	strftime(date, sizeof date, "%b %d, %Y", &lt);
	byline += std::string(" · ") + date;
	drawText(rt, byline, D2D1::RectF(kTMargin, kTMargin + 23, w - kTMargin, kTMargin + 38), 11, gray);
	const float left = kTMargin + kTName, top = kTMargin + kTTitle, bottom = top + sigs.size() * kTLane;
	auto x = [&](int i) { return left + (i - from) * pps; };
	ID2D1SolidColorBrush* b = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(black, &b))) return;
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	ID2D1StrokeStyle* dash = nullptr;
	const float dashes[] = { 2, 3 };
	if (f) f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
	                                                         D2D1_LINE_JOIN_MITER, 10, D2D1_DASH_STYLE_CUSTOM, 0), dashes, 2, &dash);
	// Time grid and axis: a tick every so many steps.
	const int first = (int)cl_scope_first_step(doc);
	const int every = tickEvery(pps, 44);
	for (int i = from - ((from + first) % every); i <= from + count; i += every) {
		if (i < from) continue;
		b->SetColor(light);
		rt->DrawLine(D2D1::Point2F(x(i), top), D2D1::Point2F(x(i), bottom), b, 0.5f, dash);
		b->SetColor(black);
		rt->DrawLine(D2D1::Point2F(x(i), bottom), D2D1::Point2F(x(i), bottom + 4), b, 0.5f);
		drawText(rt, strf("%d", i + first), D2D1::RectF(x(i) - 30, bottom + 5, x(i) + 30, bottom + 19), 9.5f, gray, TextAlign::Center);
	}
	rt->DrawLine(D2D1::Point2F(left, bottom), D2D1::Point2F(x(from + count), bottom), b, 1);
	drawText(rt, "step", D2D1::RectF(left, bottom + 19, x(from + count), bottom + 33), 9.5f, gray, TextAlign::Center);
	// The traces.
	std::vector<unsigned char> buf((size_t)std::max(1, count));
	const D2D1_COLOR_F high = color ? D2D1::ColorF(0.85f, 0.95f, 0.87f) : D2D1::ColorF(0.9f, 0.9f, 0.9f);
	for (size_t row = 0; row < sigs.size(); row++) {
		const float laneTop = top + row * kTLane, hi = laneTop + 9, lo = laneTop + kTLane - 9;
		drawText(rt, names[sigs[row]], D2D1::RectF(kTMargin, (hi + lo) / 2 - 9, left - 12, (hi + lo) / 2 + 9), 12, black, TextAlign::Trailing, true);
		const int n = cl_scope_samples(doc, sigs[row], from, count, buf.data());
		float lastY = -1;
		for (int s = 0; s < n;) {
			const unsigned char v = buf[s];
			int e = s + 1;
			while (e < n && buf[e] == v) e++;
			const float a = x(from + s), c = x(from + e);
			b->SetColor(black);
			if (v == 0 || v == 1) {
				const float y = v == 1 ? hi : lo;
				if (v == 1) fillRect(rt, D2D1::RectF(a, hi, c, lo), high);
				if (lastY >= 0 && lastY != y) rt->DrawLine(D2D1::Point2F(a, lastY), D2D1::Point2F(a, y), b, 1.5f);
				rt->DrawLine(D2D1::Point2F(a, y), D2D1::Point2F(c, y), b, 1.5f);
				lastY = y;
			} else if (v == 2) {   // floating: a dashed line in the middle
				b->SetColor(color ? D2D1::ColorF(0.24f, 0.48f, 0.98f) : black);
				rt->DrawLine(D2D1::Point2F(a, (hi + lo) / 2), D2D1::Point2F(c, (hi + lo) / 2), b, 1, dash);
				lastY = -1;
			} else if (v == 3 || v == 4) {   // conflict or unknown: a hatched band
				const D2D1_COLOR_F hc = color ? (v == 3 ? D2D1::ColorF(0.92f, 0.26f, 0.24f) : D2D1::ColorF(0.96f, 0.58f, 0.13f))
				                              : D2D1::ColorF(0.45f, 0.45f, 0.45f);
				b->SetColor(hc);
				rt->PushAxisAlignedClip(D2D1::RectF(a, hi, c, lo), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
				for (float hx = a - (lo - hi); hx < c; hx += 5) rt->DrawLine(D2D1::Point2F(hx, lo), D2D1::Point2F(hx + (lo - hi), hi), b, 0.6f);
				rt->PopAxisAlignedClip();
				if (!color) b->SetColor(black);
				rt->DrawRectangle(D2D1::RectF(a, hi, c, lo), b, 0.8f);
				lastY = -1;
			} else {
				lastY = -1;
			}
			s = e;
		}
	}
	if (dash) dash->Release();
	if (f) f->Release();
	b->Release();
}

}  // namespace

void registerDialogClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = ScopeWindow::proc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
	wc.lpszClassName = kScopeClass;
	RegisterClassExW(&wc);
}

// Docked under the canvases, as on the Mac (the window puts it there).
ScopeWindow::ScopeWindow(CircuitWindow* o) : owner(o) {
	hwnd = CreateWindowExW(0, kScopeClass, L"Oscilloscope", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, owner->window(), nullptr,
	                       appInstance(), this);
}

ScopeWindow::~ScopeWindow() {
	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

void ScopeWindow::present() {
	ShowWindow(hwnd, SW_SHOW);
	SetFocus(hwnd);
	update();
}

void ScopeWindow::close() { ShowWindow(hwnd, SW_HIDE); }
bool ScopeWindow::visible() const { return IsWindowVisible(hwnd) != FALSE; }
void ScopeWindow::update() { InvalidateRect(hwnd, nullptr, FALSE); }

std::vector<std::string> ScopeWindow::signals() const {
	std::vector<std::string> out;
	CLDocument* doc = owner->document();
	for (int i = 0; i < cl_scope_signal_count(doc); i++) out.push_back(cl_scope_signal(doc, i));
	return out;
}

std::vector<int> ScopeWindow::shownSignals() const {
	std::vector<int> out;
	const std::vector<std::string> all = signals();
	for (int i = 0; i < (int)all.size(); i++)
		if (std::find(hidden.begin(), hidden.end(), all[i]) == hidden.end()) out.push_back(i);
	return out;
}

// The samples on screen: ending at the cursor's page, or live.
void ScopeWindow::window(float width, int length, int& start, int& count) const {
	count = std::max(1, (int)((width - kNameWidth) / pointsPerStep));
	int end = length;
	if (cursor >= 0 && cursor < length - count / 2) end = std::min(length, std::max(cursor + count / 2, count));
	start = std::max(0, end - count);
}

void ScopeWindow::setCursorAt(float x, float width) {
	const int length = (int)cl_scope_length(owner->document());
	if (length <= 0 || x <= kNameWidth) return;
	int start, count;
	window(width, length, start, count);
	cursor = std::min(length - 1, std::max(0, start + (int)((x - kNameWidth) / pointsPerStep)));
	update();
}

void ScopeWindow::paint() {
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
	if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
	const Chrome c = chrome();
	const bool dark = c.dark;
	const D2D1_COLOR_F ink = c.barInk(), dim = withAlpha(ink, 0.55f), accent = c.accent();
	const D2D1_COLOR_F green = D2D1::ColorF(0.2f, 0.75f, 0.35f);
	RECT rc;
	GetClientRect(hwnd, &rc);
	const float w = (float)(rc.right / surface.scale()), h = (float)(rc.bottom / surface.scale());
	CLDocument* doc = owner->document();
	const std::vector<std::string> names = signals();
	const std::vector<int> shown = shownSignals();
	const int length = (int)cl_scope_length(doc);
	rt->Clear(dark ? c.canvas() : D2D1::ColorF(1, 1, 1));

	// The header: the name, where the cursor is, and the tools.
	fillRect(rt, D2D1::RectF(0, 0, w, kHeader), c.bar());
	fillRect(rt, D2D1::RectF(0, kHeader - 1, w, kHeader), c.hairline());
	drawText(rt, "Oscilloscope", D2D1::RectF(14, 0, 140, kHeader), 13, ink, TextAlign::Leading, true);
	const std::string where = cursor >= 0 && cursor < length ? strf("step %lld", cl_scope_first_step(doc) + cursor) : std::string("live");
	drawText(rt, where, D2D1::RectF(124, 0, 260, kHeader), 12, dim);
	buttons.clear();
	float bx = w - 10;
	auto button = [&](int id, wchar_t glyph, const char* label) {
		const float bw = label ? textWidth(label, 12) + 22 : 32;
		bx -= bw;
		const D2D1_RECT_F r = D2D1::RectF(bx, 7, bx + bw, kHeader - 7);
		if (hot == (int)buttons.size()) fillRound(rt, r, 7, withAlpha(ink, 0.08f));
		if (label) drawText(rt, label, r, 12, ink, TextAlign::Center);
		else drawIcon(rt, glyph, r, 13, withAlpha(ink, 0.85f));
		buttons.push_back({ r, id });
		bx -= 4;
	};
	button(kBtnClose, Icon::Dismiss, nullptr);
	button(kBtnClear, 0xE74D, nullptr);         // delete
	button(kBtnLive, 0xE893, nullptr);          // to the end
	button(kBtnIn, Icon::ZoomIn, nullptr);
	button(kBtnOut, Icon::ZoomOut, nullptr);
	button(kBtnShare, 0xE72D, nullptr);         // share
	if (!hidden.empty()) button(kBtnHidden, 0, strf("%d hidden", (int)hidden.size()).c_str());

	if (names.empty()) {
		drawText(rt, "Add a TO label to a wire, and its signal shows up here.", D2D1::RectF(0, kHeader, w, h), 12.5f, dim, TextAlign::Center);
		surface.end();
		EndPaint(hwnd, &ps);
		return;
	}

	int start, count;
	window(w, length, start, count);
	shownStart = start;
	shownCount = std::min(count, std::max(0, length - start));
	const float pps = pointsPerStep, x0 = kNameWidth;
	auto x = [&](int i) { return x0 + (i - start) * pps; };
	const float top0 = kHeader;
	rt->PushAxisAlignedClip(D2D1::RectF(0, top0, w, h), D2D1_ANTIALIAS_MODE_ALIASED);

	// The ruler: a tick every so many steps, with the step number.
	const int first = (int)cl_scope_first_step(doc);
	const int every = tickEvery(pps, 60);
	for (int i = start - ((start + first) % every); i <= start + count; i += every) {
		if (i < start) continue;
		fillRect(rt, D2D1::RectF(x(i), top0 + kRuler - 6, x(i) + 1, h), withAlpha(ink, 0.08f));
		drawText(rt, strf("%d", i + first), D2D1::RectF(x(i) + 3, top0 + 3, x(i) + 70, top0 + 17), 10, dim);
	}

	std::vector<unsigned char> buf((size_t)count + 1);
	const int cursorIndex = cursor >= 0 ? std::min(cursor, length - 1) : length - 1;
	ID2D1SolidColorBrush* br = nullptr;
	rt->CreateSolidColorBrush(green, &br);
	for (size_t row = 0; row < shown.size(); row++) {
		const int sig = shown[row];
		const float top = top0 + kRuler + row * kLane - scrollY;
		if (top + kLane < top0 + kRuler || top > h) continue;
		const float hi = top + 7, lo = top + kLane - 7;
		const bool isChosen = sig == chosen;
		if (isChosen) fillRect(rt, D2D1::RectF(0, top, w, top + kLane), withAlpha(accent, 0.08f));
		// The name, and the value under the cursor.
		unsigned char value = 255;
		if (length > 0) cl_scope_samples(doc, sig, cursorIndex, 1, &value);
		drawText(rt, names[sig], D2D1::RectF(10, top, kNameWidth - 30, top + kLane), 12.5f, ink, TextAlign::Leading, isChosen);
		drawText(rt, valueLabel(value), D2D1::RectF(kNameWidth - 30, top, kNameWidth - 10, top + kLane), 12.5f, valueColor(value, dim),
		         TextAlign::Trailing, true);
		// The trace, as runs of equal samples.
		const int n = length > 0 ? cl_scope_samples(doc, sig, start, count, buf.data()) : 0;
		float lastY = -1;
		for (int s = 0; s < n && br;) {
			const unsigned char v = buf[s];
			int e = s + 1;
			while (e < n && buf[e] == v) e++;
			const float a = x(start + s), b = x(start + e);
			if (v == 0 || v == 1) {
				const float y = v == 1 ? hi : lo;
				if (v == 1) fillRect(rt, D2D1::RectF(a, hi, b, lo), withAlpha(green, 0.10f));
				br->SetColor(green);
				if (lastY >= 0 && lastY != y) rt->DrawLine(D2D1::Point2F(a, lastY), D2D1::Point2F(a, y), br, 1.6f);
				rt->DrawLine(D2D1::Point2F(a, y), D2D1::Point2F(b, y), br, 1.6f);
				lastY = y;
			} else if (v == 2) {
				br->SetColor(valueColor(2, dim));
				rt->DrawLine(D2D1::Point2F(a, (hi + lo) / 2), D2D1::Point2F(b, (hi + lo) / 2), br, 1.5f);
				lastY = -1;
			} else if (v == 3 || v == 4) {
				fillRect(rt, D2D1::RectF(a, hi, b, lo), withAlpha(valueColor(v, dim), 0.25f));
				lastY = -1;
			} else {
				lastY = -1;
			}
			s = e;
		}
		fillRect(rt, D2D1::RectF(0, top + kLane - 1, w, top + kLane), withAlpha(ink, 0.07f));
	}
	if (br) br->Release();
	// The names column's edge, and the cursor.
	fillRect(rt, D2D1::RectF(kNameWidth, top0, kNameWidth + 1, h), withAlpha(ink, 0.15f));
	if (cursor >= start && cursor <= start + count) {
		const float cx = x(cursor) + pps / 2;
		fillRect(rt, D2D1::RectF(cx - 0.75f, top0, cx + 0.75f, h), accent);
	}
	rt->PopAxisAlignedClip();
	surface.end();
	EndPaint(hwnd, &ps);
}

void ScopeWindow::press(int id) {
	CLDocument* doc = owner->document();
	switch (id) {
	case kBtnOut: pointsPerStep = std::max(0.25f, pointsPerStep / 1.5f); break;
	case kBtnIn: pointsPerStep = std::min(48.0f, pointsPerStep * 1.5f); break;
	case kBtnLive: cursor = -1; break;
	case kBtnClear: cl_scope_clear(doc); cursor = -1; break;
	case kBtnClose: hot = -1; owner->toggleScope(); return;
	case kBtnShare: case kBtnHidden: {
		for (const Button& b : buttons) {
			if (b.id != id) continue;
			const float s = dpiOf(hwnd) / 96.0f;
			POINT p = { (LONG)(b.r.left * s), (LONG)(b.r.bottom * s + 2) };
			ClientToScreen(hwnd, &p);
			if (id == kBtnShare) exportMenu(p);
			else hiddenMenu(p);
		}
		break;
	}
	default: break;
	}
	update();
}

void ScopeWindow::hiddenMenu(POINT at) {
	// Its own numbers, below the window's commands (the window labels and
	// greys those as its menus open: 1000 is New's, Ctrl+N).
	const UINT showAll = 1;
	HMENU m = CreatePopupMenu();
	for (size_t i = 0; i < hidden.size(); i++) AppendMenuW(m, MF_STRING, i + 2, W("Show " + hidden[i]).c_str());
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, showAll, L"Show All");
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD, at.x, at.y, 0, owner->window(), nullptr);
	DestroyMenu(m);
	if (cmd == (int)showAll) hidden.clear();
	else if (cmd >= 2 && cmd - 2 < (int)hidden.size()) hidden.erase(hidden.begin() + (cmd - 2));
}

// What's on screen or the whole recording, as the menu says. Null when
// there's nothing recorded.
IWICBitmap* ScopeWindow::timingImage() {
	CLDocument* doc = owner->document();
	const int length = (int)cl_scope_length(doc);
	int from = prefs().timingWhole ? 0 : shownStart, count = prefs().timingWhole ? length : shownCount;
	if (count <= 0) { from = 0; count = length; }
	const std::vector<int> sigs = shownSignals();
	if (sigs.empty() || count <= 0) return nullptr;
	const std::vector<std::string> names = signals();
	const std::string title = owner->titleText();
	float tw, th, pps;
	timingSize(count, (int)sigs.size(), tw, th, pps);
	return images::render(tw, th, 2, true, [&](ID2D1RenderTarget* rt) {
		drawTiming(rt, doc, sigs, names, from, count, title, prefs().timingInColor);
	});
}

bool ScopeWindow::saveTimingDiagram(const std::string& file) {
	IWICBitmap* bmp = timingImage();
	if (bmp == nullptr) return false;
	const bool ok = images::savePng(bmp, file);
	bmp->Release();
	return ok;
}

// A timing diagram for a lab report: copied, or saved as a PNG, of what's on
// screen or the whole recording, in colour or black and white.
void ScopeWindow::exportMenu(POINT at) {
	enum { COPY = 1, PNG, WHOLE, COLOR };
	for (;;) {
		HMENU m = CreatePopupMenu();
		AppendMenuW(m, MF_STRING, COPY, L"&Copy as Image");
		AppendMenuW(m, MF_STRING, PNG, L"Save as &PNG…");
		AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(m, MF_STRING | (prefs().timingWhole ? MF_CHECKED : 0), WHOLE, L"&Whole recording");
		AppendMenuW(m, MF_STRING | (prefs().timingInColor ? MF_CHECKED : 0), COLOR, L"In c&olor");
		const int cmd = TrackPopupMenu(m, TPM_RETURNCMD, at.x, at.y, 0, owner->window(), nullptr);
		DestroyMenu(m);
		// The two options flip and the menu comes back, as the Mac's panel stays.
		if (cmd == WHOLE) { prefs().timingWhole = !prefs().timingWhole; prefs().save(); continue; }
		if (cmd == COLOR) { prefs().timingInColor = !prefs().timingInColor; prefs().save(); continue; }
		if (cmd != COPY && cmd != PNG) return;
		IWICBitmap* bmp = timingImage();
		if (bmp == nullptr) { MessageBeep(MB_OK); return; }
		const std::string title = owner->titleText();
		if (cmd == COPY) {
			if (images::copyToClipboard(hwnd, bmp)) owner->note("Timing diagram copied. Paste it into your report.");
		} else {
			const HWND top = owner->window();
			const std::string file = chooseSaveFile(top, "Save Timing Diagram", title + " timing.png", { { "PNG pictures (*.png)", "*.png" } }, ".png");
			if (!file.empty() && !images::savePng(bmp, file)) showMessage(top, Tone::Warning, "Couldn't save it there", "Try another folder.");
		}
		bmp->Release();
		return;
	}
}

bool ScopeWindow::key(UINT vk) {
	CLDocument* doc = owner->document();
	const int length = (int)cl_scope_length(doc);
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const std::vector<int> shown = shownSignals();
	auto moveCursor = [&](int delta) {
		if (length <= 0) return;
		const int at = cursor >= 0 ? cursor : length - 1;
		cursor = std::min(length - 1, std::max(0, at + delta));
	};
	// The next change on the chosen signal, either way.
	auto jump = [&](int dir) {
		if (length <= 0) return;
		const int at = cursor >= 0 ? cursor : length - 1;
		unsigned char here = 255;
		cl_scope_samples(doc, chosen, at, 1, &here);
		for (int i = at + dir; i >= 0 && i < length; i += dir) {
			unsigned char v = 255;
			cl_scope_samples(doc, chosen, i, 1, &v);
			if (v != here) { cursor = i; return; }
		}
		cursor = dir < 0 ? 0 : length - 1;
	};
	switch (vk) {
	case VK_ESCAPE: owner->toggleScope(); return true;   // put away; back to the canvas
	case VK_LEFT: if (alt) jump(-1); else moveCursor(shift ? -10 : -1); break;
	case VK_RIGHT: if (alt) jump(1); else moveCursor(shift ? 10 : 1); break;
	case VK_UP: case VK_DOWN: {
		if (shown.empty()) return true;
		auto it = std::find(shown.begin(), shown.end(), chosen);
		int i = it == shown.end() ? 0 : (int)(it - shown.begin());
		i = std::max(0, std::min((int)shown.size() - 1, i + (vk == VK_UP ? -1 : 1)));
		chosen = shown[i];
		break;
	}
	case VK_OEM_PLUS: case VK_ADD: pointsPerStep = std::min(48.0f, pointsPerStep * 1.5f); break;
	case VK_OEM_MINUS: case VK_SUBTRACT: pointsPerStep = std::max(0.25f, pointsPerStep / 1.5f); break;
	case VK_HOME: cursor = length > 0 ? 0 : -1; break;
	case VK_END: cursor = -1; break;
	case 'H': {
		if (ctrl || alt) return false;
		const std::vector<std::string> names = signals();
		if (chosen < 0 || chosen >= (int)names.size()) return true;
		auto it = std::find(hidden.begin(), hidden.end(), names[chosen]);
		if (it != hidden.end()) hidden.erase(it);
		else {
			hidden.push_back(names[chosen]);
			const std::vector<int> left = shownSignals();
			if (!left.empty()) chosen = left.front();
		}
		break;
	}
	case 'C': {
		// Ctrl+C copies the timing diagram, as the share menu does; C alone clears.
		if (ctrl && !alt) {
			IWICBitmap* bmp = timingImage();
			if (bmp == nullptr) { MessageBeep(MB_OK); return true; }
			if (images::copyToClipboard(hwnd, bmp)) owner->note("Timing diagram copied. Paste it into your report.");
			bmp->Release();
			return true;
		}
		if (ctrl || alt) return false;
		cl_scope_clear(doc);
		cursor = -1;
		break;
	}
	case VK_SPACE: owner->toggleRunning(); break;
	default: return false;
	}
	update();
	return true;
}

LRESULT CALLBACK ScopeWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		auto* self = static_cast<ScopeWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		self->hwnd = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
	}
	auto* self = reinterpret_cast<ScopeWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the oscilloscope", [&] { r = self->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ScopeWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
	const float s = dpiOf(hwnd) / 96.0f;
	const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
	RECT rc;
	GetClientRect(hwnd, &rc);
	const float w = rc.right / s;
	switch (msg) {
	case WM_PAINT: paint(); return 0;
	case WM_ERASEBKGND: return 1;
	case WM_SIZE: update(); return 0;
	case WM_NCHITTEST: {
		// The line over it is the window's, to drag.
		const POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		if (owner->onDivider(p)) return HTTRANSPARENT;
		break;
	}
	case WM_MOUSEMOVE: {
		TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&t);
		if (dragging) { setCursorAt(x, w); return 0; }
		int i = -1;
		for (size_t k = 0; k < buttons.size(); k++) if (inRect(buttons[k].r, x, y)) i = (int)k;
		if (i != hot) { hot = i; update(); }
		return 0;
	}
	case WM_MOUSELEAVE: hot = -1; update(); return 0;
	case WM_LBUTTONDOWN: {
		SetFocus(hwnd);
		for (const Button& b : buttons) if (inRect(b.r, x, y)) { press(b.id); return 0; }
		if (y < kHeader) return 0;
		// A name chooses its signal; the traces move the cursor.
		if (x < kNameWidth) {
			const std::vector<int> shown = shownSignals();
			const int row = (int)((y - kHeader - kRuler + scrollY) / kLane);
			if (row >= 0 && row < (int)shown.size()) chosen = shown[row];
			update();
			return 0;
		}
		dragging = true;
		SetCapture(hwnd);
		setCursorAt(x, w);
		return 0;
	}
	case WM_LBUTTONUP:
		dragging = false;
		if (GetCapture() == hwnd) ReleaseCapture();
		return 0;
	case WM_CAPTURECHANGED: dragging = false; return 0;
	case WM_MOUSEWHEEL: {
		const int delta = GET_WHEEL_DELTA_WPARAM(wp);
		if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
			pointsPerStep = delta > 0 ? std::min(48.0f, pointsPerStep * 1.25f) : std::max(0.25f, pointsPerStep / 1.25f);
		} else {
			const float lanes = shownSignals().size() * kLane, view = rc.bottom / s - kHeader - kRuler;
			scrollY = std::max(0.0f, std::min(scrollY - delta * 40.0f / WHEEL_DELTA, std::max(0.0f, lanes - view)));
		}
		update();
		return 0;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (key((UINT)wp)) return 0;
		break;
	case WM_SYSCHAR:
		return 0;   // Alt+arrows: no menu beep
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}
