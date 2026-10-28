// Screenshots and recordings of a window for Send Feedback (see FeedbackCapture.h).

#include "FeedbackCapture.h"
#include "Chrome.h"
#include "Drawn.h"

#include <dwmapi.h>
// The Media Foundation GUIDs this file uses, defined here (so nothing more
// is linked, and Media Foundation itself is only loaded when it's needed).
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace capture {

namespace {

IWICImagingFactory* wic() {
	static IWICImagingFactory* f = [] {
		IWICImagingFactory* made = nullptr;
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&made));
		return made;
	}();
	return f;
}

// A top-down 32-bit bitmap with a DC to draw into it.
struct Dib {
	HDC dc = nullptr;
	HBITMAP bmp = nullptr;
	HGDIOBJ old = nullptr;
	uint32_t* bits = nullptr;
	int w = 0, h = 0;
	~Dib() { release(); }
	void release() {
		if (dc && old) SelectObject(dc, old);
		if (bmp) DeleteObject(bmp);
		if (dc) DeleteDC(dc);
		dc = nullptr;
		bmp = nullptr;
		old = nullptr;
		bits = nullptr;
		w = h = 0;
	}
	bool make(int width, int height) {
		if (dc && w == width && h == height) return true;
		release();
		if (width <= 0 || height <= 0) return false;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof bi.bmiHeader;
		bi.bmiHeader.biWidth = width;
		bi.bmiHeader.biHeight = -height;   // top row first
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* p = nullptr;
		HDC screen = GetDC(nullptr);
		dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, nullptr, 0) : nullptr;
		if (!bmp || !p) { release(); return false; }
		old = SelectObject(dc, bmp);
		bits = static_cast<uint32_t*>(p);
		w = width;
		h = height;
		return true;
	}
};

// Where the window is, and the part of that you see (Windows 10 and 11 put
// an invisible edge round a window, to resize it by), in the window's pixels.
void bounds(HWND h, RECT& window, RECT& visible) {
	GetWindowRect(h, &window);
	RECT v = window;
	if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &v, sizeof v)) || !IntersectRect(&v, &v, &window)) v = window;
	visible = { v.left - window.left, v.top - window.top, v.right - window.left, v.bottom - window.top };
}

// Nothing in it: all one colour (a driver that won't draw for PrintWindow
// gives black, or nothing at all).
bool blank(const Dib& d, const RECT& r) {
	if (!d.bits || r.right <= r.left || r.bottom <= r.top) return true;
	const uint32_t first = d.bits[(size_t)(r.top + (r.bottom - r.top) / 2) * d.w + r.left + (r.right - r.left) / 2] & 0xFFFFFF;
	for (int j = 0; j < 12; j++)
		for (int i = 0; i < 12; i++) {
			const int x = r.left + (int)((r.right - r.left - 1) * (i + 0.5) / 12), y = r.top + (int)((r.bottom - r.top - 1) * (j + 0.5) / 12);
			if ((d.bits[(size_t)y * d.w + x] & 0xFFFFFF) != first) return false;
		}
	return true;
}

// The whole window rectangle into `d`: drawn by Windows (PrintWindow), or
// copied from the screen (`fromScreen`, or when PrintWindow gave nothing:
// then `fromScreen` is set, for a recording's next frames).
bool grab(HWND h, Dib& d, RECT& visible, bool& fromScreen) {
	RECT wr;
	bounds(h, wr, visible);
	if (!IsWindowVisible(h) || IsIconic(h)) return false;
	if (!d.make(wr.right - wr.left, wr.bottom - wr.top)) return false;
	if (!fromScreen) {
		if (PrintWindow(h, d.dc, PW_RENDERFULLCONTENT) && !blank(d, visible)) return true;
		fromScreen = true;
	}
	// From the screen: the window in front first, and the screen up to date.
	if (GetForegroundWindow() != h && GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) != h) SetForegroundWindow(h);
	DwmFlush();
	HDC screen = GetDC(nullptr);
	const BOOL ok = BitBlt(d.dc, 0, 0, d.w, d.h, screen, wr.left, wr.top, SRCCOPY | CAPTUREBLT);
	ReleaseDC(nullptr, screen);
	return ok && !blank(d, visible);
}

// Part of a bitmap as a WIC bitmap, opaque and premultiplied (ready to draw or save).
IWICBitmap* toWic(const Dib& d, const RECT& r) {
	const int w = r.right - r.left, h = r.bottom - r.top;
	if (!wic() || w <= 0 || h <= 0) return nullptr;
	std::vector<uint32_t> px((size_t)w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) px[(size_t)y * w + x] = d.bits[(size_t)(r.top + y) * d.w + r.left + x] | 0xFF000000u;
	IWICBitmap* out = nullptr;
	wic()->CreateBitmapFromMemory((UINT)w, (UINT)h, GUID_WICPixelFormat32bppPBGRA, (UINT)w * 4, (UINT)(px.size() * 4),
	                              reinterpret_cast<BYTE*>(px.data()), &out);
	return out;
}

}  // namespace

IWICBitmap* window(HWND h, std::string* why) {
	Dib d;
	RECT visible;
	bool fromScreen = false;
	if (!IsWindow(h)) {
		if (why) *why = "There's no CedarLogic window to take a picture of.";
		return nullptr;
	}
	if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
	if (!grab(h, d, visible, fromScreen)) {
		if (why) *why = "Windows wouldn't give a picture of the window. Try again, or add a picture you took yourself (Windows+Shift+S).";
		return nullptr;
	}
	IWICBitmap* b = toWic(d, visible);
	if (!b && why) *why = "The picture couldn't be made (Windows Imaging didn't start).";
	return b;
}

IWICBitmap* windowAndPopup(HWND h, HWND popup) {
	Dib main, pop;
	RECT mainVisible, popVisible, mainRect, popRect;
	bool screenA = false, screenB = false;
	if (!grab(h, main, mainVisible, screenA)) return nullptr;
	bounds(h, mainRect, mainVisible);
	const bool hasPopup = popup && IsWindowVisible(popup) && grab(popup, pop, popVisible, screenB);
	if (hasPopup) bounds(popup, popRect, popVisible);
	// Both, on the screen: their union, around the window as you see it.
	RECT a = { mainRect.left + mainVisible.left, mainRect.top + mainVisible.top, mainRect.left + mainVisible.right, mainRect.top + mainVisible.bottom };
	RECT all = a;
	RECT b = {};
	if (hasPopup) {
		b = { popRect.left + popVisible.left, popRect.top + popVisible.top, popRect.left + popVisible.right, popRect.top + popVisible.bottom };
		UnionRect(&all, &a, &b);
	}
	Dib out;
	if (!out.make(all.right - all.left, all.bottom - all.top)) return nullptr;
	RECT whole = { 0, 0, out.w, out.h };
	HBRUSH back = CreateSolidBrush(RGB(128, 128, 128));
	FillRect(out.dc, &whole, back);
	DeleteObject(back);
	BitBlt(out.dc, a.left - all.left, a.top - all.top, a.right - a.left, a.bottom - a.top, main.dc, mainVisible.left, mainVisible.top, SRCCOPY);
	if (hasPopup)
		BitBlt(out.dc, b.left - all.left, b.top - all.top, b.right - b.left, b.bottom - b.top, pop.dc, popVisible.left, popVisible.top, SRCCOPY);
	return toWic(out, whole);
}

bool savePng(IWICBitmapSource* bmp, const std::string& file) {
	if (!bmp || !wic()) return false;
	IWICStream* stream = nullptr;
	IWICBitmapEncoder* enc = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	const bool ok = SUCCEEDED(wic()->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(W(file).c_str(), GENERIC_WRITE)) &&
	                SUCCEEDED(wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
	                SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
	                SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->WriteSource(bmp, nullptr)) && SUCCEEDED(frame->Commit()) &&
	                SUCCEEDED(enc->Commit());
	if (frame) frame->Release();
	if (enc) enc->Release();
	if (stream) stream->Release();
	if (!ok) DeleteFileW(W(file).c_str());
	return ok;
}

// ---- Media Foundation, loaded when it's needed ------------------------------------------

namespace {

struct MF {
	HMODULE plat = nullptr, rw = nullptr;
	HRESULT(WINAPI* startup)(ULONG, DWORD) = nullptr;
	HRESULT(WINAPI* shutdown)() = nullptr;
	HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
	HRESULT(WINAPI* createSample)(IMFSample**) = nullptr;
	HRESULT(WINAPI* createMemoryBuffer)(DWORD, IMFMediaBuffer**) = nullptr;
	HRESULT(WINAPI* createAttributes)(IMFAttributes**, UINT32) = nullptr;
	HRESULT(WINAPI* createSinkWriter)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**) = nullptr;
	HRESULT(WINAPI* createSourceReader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) = nullptr;
	bool ok = false;
};

template <typename F> void bind(HMODULE m, const char* name, F& f) { f = m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr; }

const MF& mf() {
	static MF m = [] {
		MF x;
		x.plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		x.rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		bind(x.plat, "MFStartup", x.startup);
		bind(x.plat, "MFShutdown", x.shutdown);
		bind(x.plat, "MFCreateMediaType", x.createMediaType);
		bind(x.plat, "MFCreateSample", x.createSample);
		bind(x.plat, "MFCreateMemoryBuffer", x.createMemoryBuffer);
		bind(x.plat, "MFCreateAttributes", x.createAttributes);
		bind(x.rw, "MFCreateSinkWriterFromURL", x.createSinkWriter);
		bind(x.rw, "MFCreateSourceReaderFromURL", x.createSourceReader);
		x.ok = x.startup && x.shutdown && x.createMediaType && x.createSample && x.createMemoryBuffer && x.createAttributes &&
		       x.createSinkWriter && x.createSourceReader;
		return x;
	}();
	return m;
}

UINT64 pack(UINT32 hi, UINT32 lo) { return ((UINT64)hi << 32) | lo; }

template <typename T> void release(T*& p) {
	if (p) p->Release();
	p = nullptr;
}

const int kFps = 15;
const UINT32 kBitrate = 2000000;

// An H.264 MP4 of w x h frames, 15 a second, fed top-row-first BGRX pictures.
struct Encoder {
	IMFSinkWriter* writer = nullptr;
	DWORD stream = 0;
	int w = 0, h = 0;
	LONGLONG last = -1;
	int frames = 0;

	bool open(const std::wstring& path, int width, int height, bool hardware, std::string& why) {
		w = width;
		h = height;
		IMFAttributes* attrs = nullptr;
		IMFMediaType* out = nullptr;
		IMFMediaType* in = nullptr;
		HRESULT hr = mf().createAttributes(&attrs, 2);
		if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hardware ? TRUE : FALSE);
		if (SUCCEEDED(hr)) hr = mf().createSinkWriter(path.c_str(), nullptr, attrs, &writer);
		if (SUCCEEDED(hr)) hr = mf().createMediaType(&out);
		if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (SUCCEEDED(hr)) hr = out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_AVG_BITRATE, kBitrate);
		if (SUCCEEDED(hr)) hr = out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_FRAME_SIZE, pack((UINT32)w, (UINT32)h));
		if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_FRAME_RATE, pack(kFps, 1));
		if (SUCCEEDED(hr)) hr = out->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, pack(1, 1));
		if (SUCCEEDED(hr)) hr = writer->AddStream(out, &stream);
		if (SUCCEEDED(hr)) hr = mf().createMediaType(&in);
		if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (SUCCEEDED(hr)) hr = in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_FRAME_SIZE, pack((UINT32)w, (UINT32)h));
		if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_FRAME_RATE, pack(kFps, 1));
		if (SUCCEEDED(hr)) hr = in->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, pack(1, 1));
		// Bottom row first, as RGB is in Media Foundation unless it's told otherwise.
		if (SUCCEEDED(hr)) hr = in->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(-(INT32)(w * 4)));
		if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(stream, in, nullptr);
		if (SUCCEEDED(hr)) hr = writer->BeginWriting();
		release(in);
		release(out);
		release(attrs);
		if (FAILED(hr)) {
			release(writer);
			DeleteFileW(path.c_str());
			why = strf("Windows couldn't start a video (Media Foundation error 0x%08lX).", (unsigned long)hr);
			return false;
		}
		return true;
	}

	// A picture at `t` seconds; false if it couldn't be written.
	bool write(const std::vector<uint32_t>& px, double t) {
		const LONGLONG at = (LONGLONG)std::llround(t * 1e7);
		if (!writer || at <= last) return true;
		const DWORD stride = (DWORD)w * 4, size = stride * (DWORD)h;
		IMFMediaBuffer* buf = nullptr;
		IMFSample* sample = nullptr;
		BYTE* dst = nullptr;
		HRESULT hr = mf().createMemoryBuffer(size, &buf);
		if (SUCCEEDED(hr)) hr = buf->Lock(&dst, nullptr, nullptr);
		if (SUCCEEDED(hr)) {
			const BYTE* src = reinterpret_cast<const BYTE*>(px.data());
			for (int y = 0; y < h; y++) memcpy(dst + (size_t)(h - 1 - y) * stride, src + (size_t)y * stride, stride);
			buf->Unlock();
			hr = buf->SetCurrentLength(size);
		}
		if (SUCCEEDED(hr)) hr = mf().createSample(&sample);
		if (SUCCEEDED(hr)) hr = sample->AddBuffer(buf);
		if (SUCCEEDED(hr)) hr = sample->SetSampleTime(at);
		if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(10000000 / kFps);
		if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample);
		release(sample);
		release(buf);
		if (SUCCEEDED(hr)) {
			last = at;
			frames++;
		}
		return SUCCEEDED(hr);
	}

	bool finish() {
		const bool ok = writer && frames > 0 && SUCCEEDED(writer->Finalize());
		release(writer);
		return ok;
	}
};

// ---- The recording ------------------------------------------------------------------------

struct Frame {
	std::vector<uint32_t> px;
	double t;
};

class RecordingBar;

struct Recording {
	HWND target = nullptr;
	std::string file, firstFrame;
	Finished done;
	double start = 0, seconds = 0;
	Encoder enc;
	bool started = false;   // Media Foundation started (to shut down after)
	Dib full, scaled;
	bool fromScreen = false;
	int outW = 0, outH = 0;
	RecordingBar* bar = nullptr;
	HHOOK mouse = nullptr;
	std::vector<std::pair<POINT, double>> clicks;
	// The encoder's thread and what it's given.
	std::thread worker;
	std::mutex lock;
	std::condition_variable wake;
	std::deque<Frame> queue;
	bool ending = false, failed = false;
	bool stopping = false;   // ending already asked for
	std::string problem;
};

Recording* g_rec = nullptr;
void finishRecording(bool keep, const std::string& problem);

std::string clockText(double t) {
	const int s = (int)t;
	return strf("%d:%02d", s / 60, s % 60);
}

// The bar over the window while it records: a red dot, the time, ✕ and Stop.
// Always dark, as the Mac's; never takes the keys from the window.
class RecordingBar : public Drawn {
public:
	static const int kTimer = 1;
	explicit RecordingBar(HWND owner) {
		create(owner, WS_POPUP);
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
		// Windows 11: rounded, as its own small windows are (Windows 10 keeps them square).
		const DWORD round = 2;   // DWMWCP_ROUND
		DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof round);
		SetTimer(hwnd, kTimer, 1000 / kFps, nullptr);
		place();
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	}
	~RecordingBar() override {
		if (hwnd) KillTimer(hwnd, kTimer);
	}
	// Over the top middle of the window, under its title bar and toolbar.
	void place() {
		if (!g_rec) return;
		RECT wr, vis;
		bounds(g_rec->target, wr, vis);
		const double s = dpiOf(g_rec->target) / 96.0;
		const int bw = (int)std::lround(kW * s), bh = (int)std::lround(kH * s);
		const int x = wr.left + (vis.left + vis.right) / 2 - bw / 2, y = wr.top + vis.top + (int)std::lround(94 * s);
		RECT now;
		GetWindowRect(hwnd, &now);
		if (now.left != x || now.top != y || now.right - now.left != bw || now.bottom - now.top != bh)
			SetWindowPos(hwnd, HWND_TOPMOST, x, y, bw, bh, SWP_NOACTIVATE);
	}

protected:
	static constexpr float kW = 320, kH = 48;
	int hot = 0;   // 1 the ✕, 2 Stop
	D2D1_RECT_F stopRect() const { return D2D1::RectF(kW - 78, 10, kW - 14, kH - 10); }
	D2D1_RECT_F dropRect() const { return D2D1::RectF(kW - 110, 12, kW - 86, kH - 12); }

	void paint(ID2D1RenderTarget* rt, float w, float h) override {
		fillRect(rt, D2D1::RectF(0, 0, w, h), rgb255(36, 38, 42));
		strokeRound(rt, D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), 8, D2D1::ColorF(1, 1, 1, 0.12f));
		const double t = g_rec ? nowSeconds() - g_rec->start : 0;
		const float pulse = (float)(0.35 + 0.65 * (0.5 + 0.5 * std::cos(t * 2 * 3.14159265 / 1.6)));
		fillCircle(rt, D2D1::Point2F(22, h / 2), 5.5f, D2D1::ColorF(0.95f, 0.23f, 0.2f, pulse));
		drawText(rt, "Recording CedarLogic", D2D1::RectF(38, 7, w - 116, 25), 12, D2D1::ColorF(1, 1, 1), TextAlign::Leading, true);
		drawText(rt, clockText(t) + " of " + clockText(kMaxSeconds) + "  ·  only this window", D2D1::RectF(38, 24, w - 116, 41), 11,
		         D2D1::ColorF(1, 1, 1, 0.6f));
		const D2D1_RECT_F x = dropRect();
		if (hot == 1) fillCircle(rt, D2D1::Point2F((x.left + x.right) / 2, (x.top + x.bottom) / 2), 12, D2D1::ColorF(1, 1, 1, 0.12f));
		drawIcon(rt, Icon::Dismiss, x, 10, D2D1::ColorF(1, 1, 1, 0.75f));
		const D2D1_RECT_F s = stopRect();
		fillRound(rt, s, (s.bottom - s.top) / 2, hot == 2 ? rgb255(214, 52, 44) : rgb255(232, 64, 54));
		drawText(rt, "Stop", s, 12, D2D1::ColorF(1, 1, 1), TextAlign::Center, true);
	}
	void mouseMove(float x, float y) override {
		const int h2 = inRect(dropRect(), x, y) ? 1 : inRect(stopRect(), x, y) ? 2 : 0;
		if (h2 != hot) { hot = h2; redraw(); }
	}
	void mouseLeave() override {
		if (hot) { hot = 0; redraw(); }
	}
	void mouseUp(int button, float x, float y) override {
		if (button != 1) return;
		// Ended a moment later, once this bar is done with the click (ending
		// takes the bar away).
		if (inRect(stopRect(), x, y)) SetTimer(nullptr, 0, 0, keepLater);
		else if (inRect(dropRect(), x, y)) SetTimer(nullptr, 0, 0, dropLater);
	}
public:
	static void CALLBACK keepLater(HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); finishRecording(true, ""); }
	static void CALLBACK dropLater(HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); finishRecording(false, ""); }
	LRESULT message(UINT msg, WPARAM wp, LPARAM, bool& handled) override;
};

void tick();

LRESULT RecordingBar::message(UINT msg, WPARAM wp, LPARAM, bool& handled) {
	handled = false;
	if (msg == WM_MOUSEACTIVATE) { handled = true; return MA_NOACTIVATE; }
	if (msg == WM_TIMER && wp == kTimer) {
		handled = true;
		tick();
		return 0;
	}
	if (msg == WM_SETCURSOR) {
		handled = true;
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}
	return 0;
}

// Clicks in the window, for their rings.
LRESULT CALLBACK mouseHook(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION && g_rec &&
	    (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_NCLBUTTONDOWN || wp == WM_NCRBUTTONDOWN)) {
		const MOUSEHOOKSTRUCT* m = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lp);
		if (m && GetAncestor(m->hwnd, GA_ROOT) == g_rec->target) g_rec->clicks.push_back({ m->pt, nowSeconds() });
	}
	return CallNextHookEx(nullptr, code, wp, lp);
}

void encodeLoop(Recording* r) {
	for (;;) {
		Frame f;
		{
			std::unique_lock<std::mutex> g(r->lock);
			r->wake.wait(g, [&] { return r->ending || !r->queue.empty(); });
			if (r->queue.empty()) break;   // ending, and nothing left
			f = std::move(r->queue.front());
			r->queue.pop_front();
		}
		if (!r->enc.write(f.px, f.t)) {
			std::lock_guard<std::mutex> g(r->lock);
			r->failed = true;
			r->problem = "Windows stopped making the video part way. What was recorded is kept if there's enough of it.";
		}
	}
}

// One picture of the window, at the recording's size, with the pointer and
// the clicks' rings drawn in.
bool frame(Recording& r, std::vector<uint32_t>& out) {
	RECT visible;
	if (!grab(r.target, r.full, visible, r.fromScreen)) return false;
	RECT wr;
	GetWindowRect(r.target, &wr);
	const double s = dpiOf(r.target) / 96.0;
	// Clicks: a ring that grows over half a second.
	const double now = nowSeconds();
	r.clicks.erase(std::remove_if(r.clicks.begin(), r.clicks.end(), [&](const std::pair<POINT, double>& c) { return now - c.second > 0.5; }),
	               r.clicks.end());
	if (!r.clicks.empty()) {
		HPEN pen = CreatePen(PS_SOLID, std::max(2, (int)std::lround(3 * s)), RGB(56, 214, 107));
		HGDIOBJ oldPen = SelectObject(r.full.dc, pen), oldBrush = SelectObject(r.full.dc, GetStockObject(NULL_BRUSH));
		for (const auto& c : r.clicks) {
			const double f = (now - c.second) / 0.5;
			const int rad = (int)std::lround((10 + 18 * f) * s);
			const int x = c.first.x - wr.left, y = c.first.y - wr.top;
			Ellipse(r.full.dc, x - rad, y - rad, x + rad, y + rad);
		}
		SelectObject(r.full.dc, oldBrush);
		SelectObject(r.full.dc, oldPen);
		DeleteObject(pen);
	}
	// The pointer, as it is, when it's over the window.
	CURSORINFO ci = { sizeof ci };
	if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && ci.hCursor && WindowFromPoint(ci.ptScreenPos) &&
	    GetAncestor(WindowFromPoint(ci.ptScreenPos), GA_ROOT) == r.target) {
		ICONINFO ii = {};
		if (GetIconInfo(ci.hCursor, &ii)) {
			DrawIconEx(r.full.dc, ci.ptScreenPos.x - wr.left - (int)ii.xHotspot, ci.ptScreenPos.y - wr.top - (int)ii.yHotspot, ci.hCursor, 0, 0, 0,
			           nullptr, DI_NORMAL);
			if (ii.hbmMask) DeleteObject(ii.hbmMask);
			if (ii.hbmColor) DeleteObject(ii.hbmColor);
		}
	}
	// Fitted into the recording's size (a window resized since keeps its shape).
	if (!r.scaled.make(r.outW, r.outH)) return false;
	RECT all = { 0, 0, r.outW, r.outH };
	HBRUSH back = CreateSolidBrush(RGB(20, 20, 20));
	FillRect(r.scaled.dc, &all, back);
	DeleteObject(back);
	const int vw = visible.right - visible.left, vh = visible.bottom - visible.top;
	const double k = std::min((double)r.outW / vw, (double)r.outH / vh);
	const int dw = (int)std::lround(vw * k), dh = (int)std::lround(vh * k);
	SetStretchBltMode(r.scaled.dc, HALFTONE);
	SetBrushOrgEx(r.scaled.dc, 0, 0, nullptr);
	StretchBlt(r.scaled.dc, (r.outW - dw) / 2, (r.outH - dh) / 2, dw, dh, r.full.dc, visible.left, visible.top, vw, vh, SRCCOPY);
	out.assign(r.scaled.bits, r.scaled.bits + (size_t)r.outW * r.outH);
	return true;
}

void tick() {
	Recording* r = g_rec;
	if (!r) return;
	if (r->stopping) return;
	const double t = nowSeconds() - r->start;
	bool failed = false;
	{
		std::lock_guard<std::mutex> g(r->lock);
		failed = r->failed;
	}
	// The window gone, a minute up, or the encoder stopped: kept, a moment
	// later (this runs in the bar's own timer, and ending takes the bar away).
	if (!IsWindow(r->target) || t >= kMaxSeconds || failed) {
		r->stopping = true;
		SetTimer(nullptr, 0, 0, RecordingBar::keepLater);
		return;
	}
	r->bar->place();
	bool busy = false;
	{
		std::lock_guard<std::mutex> g(r->lock);
		busy = r->queue.size() >= 3;   // the encoder is behind: this frame is skipped
	}
	std::vector<uint32_t> px;
	if (!busy && frame(*r, px)) {
		if (r->firstFrame.empty()) {
			// Its first picture, for the thumbnail.
			Dib d;
			d.bits = px.data();
			d.w = r->outW;
			d.h = r->outH;
			const RECT all = { 0, 0, r->outW, r->outH };
			IWICBitmap* b = toWic(d, all);
			d.bits = nullptr;
			d.w = d.h = 0;
			const std::string png = r->file + "-first.png";
			if (b && savePng(b, png)) r->firstFrame = png;
			if (b) b->Release();
		}
		{
			std::lock_guard<std::mutex> g(r->lock);
			r->queue.push_back(Frame{ std::move(px), t });
		}
		r->wake.notify_one();
		r->seconds = t;
	}
	r->bar->redraw();
}

void finishRecording(bool keep, const std::string& givenProblem) {
	Recording* r = g_rec;
	if (!r) return;
	g_rec = nullptr;
	if (r->mouse) UnhookWindowsHookEx(r->mouse);
	delete r->bar;
	r->bar = nullptr;
	{
		std::lock_guard<std::mutex> g(r->lock);
		r->ending = true;
	}
	r->wake.notify_one();
	if (r->worker.joinable()) r->worker.join();
	const bool written = r->enc.finish();
	if (r->started) mf().shutdown();
	const double seconds = nowSeconds() - r->start;
	std::string file = r->file, problem = givenProblem;
	bool kept = keep && written && seconds >= 0.3;
	if (keep && !written) problem = "The recording didn't come out. Try again, or add a screenshot instead.";
	else if (keep && seconds < 0.3) problem = "That recording was too short to keep.";
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (kept && GetFileAttributesExW(W(file).c_str(), GetFileExInfoStandard, &a) &&
	    (((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow) > 55000000LL) {
		kept = false;
		problem = "That recording came out too big to send. Try a shorter one.";
	}
	// Cut short by the encoder: what there is is kept, and that's said.
	if (kept && r->failed) problem = r->problem;
	if (!kept) {
		DeleteFileW(W(file).c_str());
		if (!r->firstFrame.empty()) DeleteFileW(W(r->firstFrame).c_str());
		file.clear();
		if (!keep) problem.clear();   // thrown away on purpose: nothing to say
	}
	Finished done = std::move(r->done);
	const std::string first = kept ? r->firstFrame : std::string();
	delete r;
	if (done) done(file, kept ? std::min(seconds, kMaxSeconds) : 0, first, problem);
}

}  // namespace

bool startRecording(HWND window, const std::string& file, Finished done, std::string& why) {
	if (g_rec) { why = "A recording is already going."; return false; }
	if (!IsWindow(window)) { why = "There's no CedarLogic window to record."; return false; }
	if (!mf().ok) {
		why = "Recording needs Windows' Media Foundation, which this copy of Windows hasn't got (Windows “N” editions: "
		      "add the Media Feature Pack in Settings > Apps > Optional features). A screenshot still works.";
		return false;
	}
	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
	RECT wr, vis;
	bounds(window, wr, vis);
	const int vw = vis.right - vis.left, vh = vis.bottom - vis.top;
	if (vw < 16 || vh < 16) { why = "The window is too small to record."; return false; }
	// As sharp as the screen, but no wider than 1600 pixels (a minute stays
	// a few megabytes), in even numbers (H.264 wants them).
	const double k = std::min(1.0, 1600.0 / vw);
	Recording* r = new Recording();
	r->target = window;
	r->file = file;
	r->done = std::move(done);
	r->outW = std::max(16, (int)(vw * k) & ~1);
	r->outH = std::max(16, (int)(vh * k) & ~1);
	if (FAILED(mf().startup(MF_VERSION, MFSTARTUP_LITE))) {
		delete r;
		why = "Windows' Media Foundation wouldn't start, so it can't record. A screenshot still works.";
		return false;
	}
	r->started = true;
	// The graphics card's encoder if it has one, else Windows' own.
	std::string first, second;
	if (!r->enc.open(W(file), r->outW, r->outH, true, first) && !r->enc.open(W(file), r->outW, r->outH, false, second)) {
		mf().shutdown();
		delete r;
		why = second.empty() ? first : second;
		return false;
	}
	g_rec = r;
	r->start = nowSeconds();
	r->mouse = SetWindowsHookExW(WH_MOUSE, mouseHook, nullptr, GetCurrentThreadId());
	r->worker = std::thread(encodeLoop, r);
	r->bar = new RecordingBar(window);
	tick();
	return true;
}

bool recording() { return g_rec != nullptr; }

double recordingSeconds() { return g_rec ? nowSeconds() - g_rec->start : 0; }

void stopRecording(bool keep) { finishRecording(keep, ""); }

HWND recordingBar() { return g_rec && g_rec->bar ? g_rec->bar->widget() : nullptr; }

double probeVideo(const std::string& file, const std::string& framePng) {
	if (!mf().ok || FAILED(mf().startup(MF_VERSION, MFSTARTUP_LITE))) return 0;
	double seconds = 0;
	IMFAttributes* attrs = nullptr;
	IMFSourceReader* reader = nullptr;
	IMFMediaType* want = nullptr;
	IMFMediaType* got = nullptr;
	HRESULT hr = mf().createAttributes(&attrs, 1);
	if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	if (SUCCEEDED(hr)) hr = mf().createSourceReader(W(file).c_str(), attrs, &reader);
	if (SUCCEEDED(hr)) {
		PROPVARIANT v;
		PropVariantInit(&v);
		if (SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8)
			seconds = (double)v.uhVal.QuadPart / 1e7;
		PropVariantClear(&v);
	}
	if (SUCCEEDED(hr)) hr = mf().createMediaType(&want);
	if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, want);
	if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &got);
	UINT64 size = 0;
	if (SUCCEEDED(hr)) hr = got->GetUINT64(MF_MT_FRAME_SIZE, &size);
	const UINT32 w = (UINT32)(size >> 32), h = (UINT32)size;
	// Which way up the reader hands its rows (RGB is bottom row first unless it says).
	UINT32 strideBits = 0;
	const bool topDown = got && SUCCEEDED(got->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideBits)) && (INT32)strideBits > 0;
	for (int tries = 0; SUCCEEDED(hr) && tries < 30 && !framePng.empty(); tries++) {
		DWORD flags = 0;
		LONGLONG ts = 0;
		IMFSample* sample = nullptr;
		hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
		if (!sample) continue;
		IMFMediaBuffer* buf = nullptr;
		BYTE* data = nullptr;
		DWORD len = 0;
		if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && SUCCEEDED(buf->Lock(&data, nullptr, &len)) && len >= w * h * 4 && w && h) {
			Dib d;
			std::vector<uint32_t> px((size_t)w * h);
			for (UINT32 y = 0; y < h; y++)
				memcpy(&px[(size_t)(topDown ? y : h - 1 - y) * w], data + (size_t)y * w * 4, (size_t)w * 4);
			d.bits = px.data();
			d.w = (int)w;
			d.h = (int)h;
			const RECT all = { 0, 0, (LONG)w, (LONG)h };
			IWICBitmap* b = toWic(d, all);
			d.bits = nullptr;
			d.w = d.h = 0;
			if (b) {
				savePng(b, framePng);
				b->Release();
			}
			buf->Unlock();
			release(buf);
			release(sample);
			break;
		}
		release(buf);
		release(sample);
	}
	release(got);
	release(want);
	release(reader);
	release(attrs);
	mf().shutdown();
	return seconds;
}

}  // namespace capture
