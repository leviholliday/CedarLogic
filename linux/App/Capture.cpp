// Screenshots and recordings of the window (see Capture.h).

#include "Capture.h"
#include "App.h"
#include "Chrome.h"
#include "Window.h"

#include <glib/gstdio.h>
#include <signal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace capture {

namespace {

const double kMaxSeconds = 60;
const long long kMaxBytes = 40000000;   // the site takes a 60 MB movie; well under it
const long long kMaxPngBytes = 11000000;   // and a 12 MB picture
const int kMaxWidth = 1280;             // pixels: sharp enough to read, small enough for a minute
const int kMaxPngWidth = 1024;          // an animated PNG is bigger per frame
const int kFastestMs = 125;             // 8 frames a second at most

// The window's own frame in its GdkWindow: the title bar and the content,
// without the shadow a desktop draws round a client-side decorated window.
GdkRectangle frameOf(GtkWindow* w) {
	GtkAllocation all;
	gtk_widget_get_allocation(GTK_WIDGET(w), &all);
	GdkRectangle r = { 0, 0, 0, 0 };
	bool any = false;
	auto unite = [&](GtkWidget* part) {
		if (part == nullptr || !gtk_widget_get_visible(part)) return;
		GtkAllocation a;
		gtk_widget_get_allocation(part, &a);
		if (a.width <= 1 || a.height <= 0) return;
		if (!any) { r = a; any = true; return; }
		gdk_rectangle_union(&r, &a, &r);
	};
	unite(gtk_window_get_titlebar(w));
	unite(gtk_bin_get_child(GTK_BIN(w)));
	if (!any) r = { 0, 0, all.width, all.height };
	return r;
}

}  // namespace

cairo_surface_t* window(GtkWindow* w, int maxWidth) {
	if (w == nullptr || !gtk_widget_get_realized(GTK_WIDGET(w))) return nullptr;
	// A change just made (Simulation View, a panel) is laid out first: GTK
	// draws only a window whose sizes are settled.
	gtk_container_check_resize(GTK_CONTAINER(w));
	const GdkRectangle f = frameOf(w);
	if (f.width <= 0 || f.height <= 0) return nullptr;
	double scale = std::max(1, gtk_widget_get_scale_factor(GTK_WIDGET(w)));
	if (maxWidth > 0) scale = std::min(scale, (double)maxWidth / f.width);
	const int pw = std::max(1, (int)std::lround(f.width * scale)), ph = std::max(1, (int)std::lround(f.height * scale));
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	cairo_t* cr = cairo_create(s);
	cairo_scale(cr, (double)pw / f.width, (double)ph / f.height);
	cairo_translate(cr, -f.x, -f.y);
	gtk_widget_draw(GTK_WIDGET(w), cr);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	return s;
}

GdkPixbuf* pixbuf(cairo_surface_t* s, int maxW, int maxH) {
	if (s == nullptr) return nullptr;
	const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
	GdkPixbuf* full = gdk_pixbuf_get_from_surface(s, 0, 0, w, h);
	if (full == nullptr || maxW <= 0 || maxH <= 0) return full;
	const double k = std::min(1.0, std::min((double)maxW / w, (double)maxH / h));
	GdkPixbuf* small = gdk_pixbuf_scale_simple(full, std::max(1, (int)(w * k)), std::max(1, (int)(h * k)), GDK_INTERP_BILINEAR);
	g_object_unref(full);
	return small;
}

namespace {

// ---- A QuickTime movie of still pictures ------------------------------------------
// The pictures as they come (in its 'mdat'), then the index that says where
// each is and how long it shows (its 'moov'), written at the end.
class MovWriter {
public:
	bool open(const std::string& file, int width, int height, const char codec[4]) {
		f = g_fopen(file.c_str(), "wb");
		if (f == nullptr) return false;
		w = width;
		h = height;
		memcpy(fourcc, codec, 4);
		std::string head;
		be32(head, 20); head += "ftyp"; head += "qt  "; be32(head, 0x200); head += "qt  ";
		mdatAt = (long)head.size();
		be32(head, 0); head += "mdat";   // its size goes in at the end
		bytes = (long long)head.size();
		return fwrite(head.data(), 1, head.size(), f) == head.size();
	}
	// A picture, shown until the next one (or the end).
	bool add(const char* data, size_t size, long long atMs) {
		if (f == nullptr) return false;
		if (!starts.empty() && atMs <= starts.back()) atMs = starts.back() + 1;
		offsets.push_back(bytes);
		sizes.push_back((unsigned)size);
		starts.push_back(atMs);
		bytes += (long long)size;
		return fwrite(data, 1, size, f) == size;
	}
	// The last picture shows until `endMs`. True if the file is whole.
	bool finish(long long endMs) {
		if (f == nullptr) return false;
		bool ok = !sizes.empty();
		if (ok) {
			std::vector<unsigned> durations;
			for (size_t i = 0; i < starts.size(); i++) {
				const long long next = i + 1 < starts.size() ? starts[i + 1] : std::max(endMs, starts[i] + 100);
				durations.push_back((unsigned)std::max(1LL, next - starts[i]));
			}
			long long total = 0;
			for (unsigned d : durations) total += d;
			const std::string moov = movie(durations, (unsigned)total);
			ok = fwrite(moov.data(), 1, moov.size(), f) == moov.size();
			// The pictures' box, now its size is known.
			std::string size;
			be32(size, (unsigned)(bytes - mdatAt));
			ok = ok && fseek(f, mdatAt, SEEK_SET) == 0 && fwrite(size.data(), 1, 4, f) == 4;
		}
		ok = fclose(f) == 0 && ok;
		f = nullptr;
		return ok;
	}
	void abandon() { if (f) fclose(f); f = nullptr; }
	long long size() const { return bytes; }
	int count() const { return (int)sizes.size(); }

private:
	FILE* f = nullptr;
	int w = 0, h = 0;
	char fourcc[4];
	long mdatAt = 0;
	long long bytes = 0;
	std::vector<long long> offsets, starts;
	std::vector<unsigned> sizes;

	static void be32(std::string& s, unsigned v) {
		const char b[4] = { (char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v };
		s.append(b, 4);
	}
	static void be16(std::string& s, unsigned v) {
		const char b[2] = { (char)(v >> 8), (char)v };
		s.append(b, 2);
	}
	static void zeros(std::string& s, int n) { s.append((size_t)n, '\0'); }
	static std::string box(const char* type, const std::string& body) {
		std::string s;
		be32(s, (unsigned)(body.size() + 8));
		s.append(type, 4);
		return s + body;
	}
	static std::string fullBox(const char* type, unsigned flags, const std::string& body) {
		std::string s;
		be32(s, flags & 0xFFFFFF);
		return box(type, s + body);
	}
	static void matrix(std::string& s) {
		const unsigned m[9] = { 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000 };
		for (unsigned v : m) be32(s, v);
	}
	static std::string counted(const std::string& text, int width) {
		std::string s(1, (char)text.size());
		s += text;
		if (width > 0) s.resize((size_t)width, '\0');
		return s;
	}

	std::string movie(const std::vector<unsigned>& durations, unsigned total) const {
		std::string mvhd;
		be32(mvhd, 0); be32(mvhd, 0); be32(mvhd, 1000); be32(mvhd, total);
		be32(mvhd, 0x10000); be16(mvhd, 0x100); zeros(mvhd, 10);
		matrix(mvhd);
		zeros(mvhd, 24);
		be32(mvhd, 2);

		std::string tkhd;
		be32(tkhd, 0); be32(tkhd, 0); be32(tkhd, 1); be32(tkhd, 0); be32(tkhd, total);
		zeros(tkhd, 8); be16(tkhd, 0); be16(tkhd, 0); be16(tkhd, 0); be16(tkhd, 0);
		matrix(tkhd);
		be32(tkhd, (unsigned)w << 16); be32(tkhd, (unsigned)h << 16);

		std::string mdhd;
		be32(mdhd, 0); be32(mdhd, 0); be32(mdhd, 1000); be32(mdhd, total); be16(mdhd, 0); be16(mdhd, 0);

		std::string hdlr = "mhlrvide";
		zeros(hdlr, 12);
		hdlr += counted("VideoHandler", 0);
		std::string vmhd;
		be16(vmhd, 0x40); be16(vmhd, 0x8000); be16(vmhd, 0x8000); be16(vmhd, 0x8000);
		std::string dhlr = "dhlralis";
		zeros(dhlr, 12);
		dhlr += counted("DataHandler", 0);
		std::string dref;
		be32(dref, 1);
		be32(dref, 12); dref += "alis"; be32(dref, 1);

		std::string entry;
		zeros(entry, 6); be16(entry, 1);
		be16(entry, 0); be16(entry, 0); entry += "appl";
		be32(entry, 0); be32(entry, 512);
		be16(entry, (unsigned)w); be16(entry, (unsigned)h);
		be32(entry, 0x480000); be32(entry, 0x480000); be32(entry, 0); be16(entry, 1);
		entry += counted(memcmp(fourcc, "jpeg", 4) == 0 ? "Photo - JPEG" : "PNG", 32);
		be16(entry, 24); be16(entry, 0xFFFF);
		std::string stsd;
		be32(stsd, 1);
		be32(stsd, (unsigned)entry.size() + 8);
		stsd.append(fourcc, 4);
		stsd += entry;

		// Runs of equal durations.
		std::vector<std::pair<unsigned, unsigned>> runs;
		for (unsigned d : durations) {
			if (!runs.empty() && runs.back().second == d) runs.back().first++;
			else runs.push_back({ 1, d });
		}
		std::string stts;
		be32(stts, (unsigned)runs.size());
		for (const auto& r : runs) { be32(stts, r.first); be32(stts, r.second); }
		std::string stsc;
		be32(stsc, 1); be32(stsc, 1); be32(stsc, 1); be32(stsc, 1);
		std::string stsz;
		be32(stsz, 0); be32(stsz, (unsigned)sizes.size());
		for (unsigned s : sizes) be32(stsz, s);
		std::string stco;
		be32(stco, (unsigned)offsets.size());
		for (long long o : offsets) be32(stco, (unsigned)o);

		const std::string stbl = box("stbl", fullBox("stsd", 0, stsd) + fullBox("stts", 0, stts) + fullBox("stsc", 0, stsc) +
		                                         fullBox("stsz", 0, stsz) + fullBox("stco", 0, stco));
		const std::string minf = box("minf", fullBox("vmhd", 1, vmhd) + fullBox("hdlr", 0, dhlr) + box("dinf", fullBox("dref", 0, dref)) + stbl);
		const std::string mdia = box("mdia", fullBox("mdhd", 0, mdhd) + fullBox("hdlr", 0, hdlr) + minf);
		const std::string trak = box("trak", fullBox("tkhd", 3, tkhd) + mdia);
		return box("moov", fullBox("mvhd", 0, mvhd) + trak);
	}
};

// ---- An animated PNG -------------------------------------------------------------------
// Where there's no ffmpeg to make an MP4: a picture that moves in every
// browser (the site takes it as a PNG). Each frame after the first is only
// the rectangle that changed, so a pointer moving over a still circuit costs
// a few kilobytes.
class ApngWriter {
public:
	bool open(const std::string& file, int width, int height) {
		f = g_fopen(file.c_str(), "wb");
		if (f == nullptr) return false;
		w = width;
		h = height;
		std::string out("\x89PNG\r\n\x1a\n", 8);
		std::string ihdr;
		be32(ihdr, (unsigned)w); be32(ihdr, (unsigned)h);
		ihdr += std::string("\x08\x02\x00\x00\x00", 5);   // 8 bits, RGB
		out += chunk("IHDR", ihdr);
		actlAt = (long)out.size();
		out += actl(0);   // the frame count goes in at the end
		bytes = (long long)out.size();
		return fwrite(out.data(), 1, out.size(), f) == out.size();
	}
	// A frame (xRGB pixels, as Cairo's RGB24) at `atMs`. Nothing is written
	// when it's the same as the frame before.
	bool add(const unsigned char* px, int stride, long long atMs) {
		if (f == nullptr) return false;
		int x0 = 0, y0 = 0, x1 = w, y1 = h;
		if (!prev.empty()) {
			// The rectangle that changed.
			x0 = w; y0 = h; x1 = 0; y1 = 0;
			for (int y = 0; y < h; y++) {
				const guint32* a = reinterpret_cast<const guint32*>(px + (size_t)y * stride);
				const guint32* b = reinterpret_cast<const guint32*>(prev.data() + (size_t)y * stride);
				if (memcmp(a, b, (size_t)w * 4) == 0) continue;
				int l = 0, r = w - 1;
				while (l < w && ((a[l] ^ b[l]) & 0xFFFFFF) == 0) l++;
				while (r > l && ((a[r] ^ b[r]) & 0xFFFFFF) == 0) r--;
				if (l >= w) continue;
				x0 = std::min(x0, l); x1 = std::max(x1, r + 1);
				y0 = std::min(y0, y); y1 = std::max(y1, y + 1);
			}
			if (x1 <= x0 || y1 <= y0) return true;
		}
		if (!flush(atMs)) return false;
		pending = Pending{ x0, y0, x1 - x0, y1 - y0, atMs, deflate(px, stride, x0, y0, x1, y1), true };
		prev.assign(px, px + (size_t)stride * h);
		frames++;
		return true;
	}
	bool finish(long long endMs) {
		if (f == nullptr) return false;
		bool ok = frames > 0 && flush(std::max(endMs, pending.atMs + 100));
		const std::string end = chunk("IEND", "");
		ok = ok && fwrite(end.data(), 1, end.size(), f) == end.size();
		const std::string count = actl((unsigned)frames);
		ok = ok && fseek(f, actlAt, SEEK_SET) == 0 && fwrite(count.data(), 1, count.size(), f) == count.size();
		ok = fclose(f) == 0 && ok;
		f = nullptr;
		return ok;
	}
	void abandon() { if (f) fclose(f); f = nullptr; }
	long long size() const { return bytes + (long long)pending.data.size(); }
	int count() const { return frames; }

private:
	struct Pending {
		int x = 0, y = 0, w = 0, h = 0;
		long long atMs = 0;
		std::string data;
		bool any = false;
	};
	FILE* f = nullptr;
	int w = 0, h = 0, frames = 0;
	unsigned seq = 0;
	long actlAt = 0;
	long long bytes = 0;
	std::vector<unsigned char> prev;
	Pending pending;

	static void be32(std::string& s, unsigned v) {
		const char b[4] = { (char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v };
		s.append(b, 4);
	}
	static unsigned crc(const std::string& s) {
		static unsigned table[256];
		static bool made = false;
		if (!made) {
			for (unsigned n = 0; n < 256; n++) {
				unsigned c = n;
				for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				table[n] = c;
			}
			made = true;
		}
		unsigned c = 0xFFFFFFFFu;
		for (unsigned char b : s) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
		return c ^ 0xFFFFFFFFu;
	}
	static std::string chunk(const char* type, const std::string& data) {
		std::string out;
		be32(out, (unsigned)data.size());
		const std::string body = std::string(type, 4) + data;
		out += body;
		be32(out, crc(body));
		return out;
	}
	static std::string actl(unsigned count) {
		std::string d;
		be32(d, count); be32(d, 0);   // and it repeats
		return chunk("acTL", d);
	}
	// The rectangle's rows, each filtered by its left neighbour (flat UI
	// becomes runs of zeros), compressed as PNG's zlib stream.
	static std::string deflate(const unsigned char* px, int stride, int x0, int y0, int x1, int y1) {
		std::string raw;
		raw.reserve((size_t)(y1 - y0) * ((x1 - x0) * 3 + 1));
		for (int y = y0; y < y1; y++) {
			raw += '\x01';
			const guint32* row = reinterpret_cast<const guint32*>(px + (size_t)y * stride);
			unsigned pr = 0, pg = 0, pb = 0;
			for (int x = x0; x < x1; x++) {
				const unsigned r = (row[x] >> 16) & 0xFF, g = (row[x] >> 8) & 0xFF, b = row[x] & 0xFF;
				raw += (char)(r - pr); raw += (char)(g - pg); raw += (char)(b - pb);
				pr = r; pg = g; pb = b;
			}
		}
		GZlibCompressor* z = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 4);
		std::string out;
		std::vector<char> buf(1 << 16);
		size_t used = 0;
		for (;;) {
			gsize read = 0, written = 0;
			const GConverterResult r = g_converter_convert(G_CONVERTER(z), raw.data() + used, raw.size() - used, buf.data(), buf.size(),
			                                               G_CONVERTER_INPUT_AT_END, &read, &written, nullptr);
			used += read;
			out.append(buf.data(), written);
			if (r == G_CONVERTER_FINISHED || r == G_CONVERTER_ERROR) break;
		}
		g_object_unref(z);
		return out;
	}
	// The frame waiting for its length, written now it's known.
	bool flush(long long nextMs) {
		if (!pending.any) return true;
		const long long ms = std::max(1LL, std::min(60000LL, nextMs - pending.atMs));
		std::string fc;
		be32(fc, seq++);
		be32(fc, (unsigned)pending.w); be32(fc, (unsigned)pending.h); be32(fc, (unsigned)pending.x); be32(fc, (unsigned)pending.y);
		fc += (char)(ms >> 8); fc += (char)ms; fc += (char)(1000 >> 8); fc += (char)(1000 & 0xFF);
		fc += '\0'; fc += '\0';   // nothing disposed; the rectangle replaces what's under it
		std::string out = chunk("fcTL", fc);
		if (frames == 1 && seq == 1) {
			out += chunk("IDAT", pending.data);   // the first frame is the still picture too
		} else {
			std::string fd;
			be32(fd, seq++);
			out += chunk("fdAT", fd + pending.data);
		}
		pending = Pending();
		bytes += (long long)out.size();
		return fwrite(out.data(), 1, out.size(), f) == out.size();
	}
};

// ---- The recording ---------------------------------------------------------------------

struct Click { double x, y; gint64 at; };

struct Recorder {
	CircuitWindow* win = nullptr;
	GtkWindow* gw = nullptr;
	std::string folder, movFile;
	std::function<void(const Recording&)> done;
	MovWriter mov;
	int width = 0, height = 0;   // the movie's, in pixels
	bool jpeg = true;
	bool apng = false;          // no ffmpeg: an animated PNG instead of a movie
	ApngWriter png;
	std::string ffmpegPath;
	int ffmpegTry = 0;
	gint64 started = 0;
	guint timer = 0;
	std::vector<unsigned char> last;   // the frame before, to skip a repeat
	GdkPixbuf* first = nullptr;
	std::vector<Click> clicks;
	bool clickPending = false;
	bool stopping = false;
	bool gone = false;          // the window closed
	gulong destroyId = 0;
	double seconds = 0;
	// The bar over the canvas.
	GtkWidget* hud = nullptr;
	GtkWidget* dot = nullptr;
	GtkWidget* timeLabel = nullptr;
	GtkWidget* title = nullptr;
	GtkWidget* buttons = nullptr;
	// ffmpeg, making the MP4.
	GPid ffmpeg = 0;
	guint ffmpegTimeout = 0;
	std::string mp4File;
};

Recorder* gRec = nullptr;

double elapsed(const Recorder* r) { return (g_get_monotonic_time() - r->started) / 1e6; }

std::string mmss(double t) {
	const int s = (int)t;
	return format("%d:%02d", s / 60, s % 60);
}

// Every event still goes to GTK; a press in the recorded window leaves a ring.
void eventHandler(GdkEvent* e, gpointer) {
	if (gRec && !gRec->stopping && e->type == GDK_BUTTON_PRESS && e->button.window && gRec->gw &&
	    gtk_widget_get_window(GTK_WIDGET(gRec->gw)) && gdk_window_get_toplevel(e->button.window) == gtk_widget_get_window(GTK_WIDGET(gRec->gw)))
		gRec->clickPending = true;
	gtk_main_do_event(e);
}

// The pointer, in the window's GdkWindow.
bool pointerAt(GtkWindow* w, double& x, double& y) {
	GdkWindow* gdkw = gtk_widget_get_window(GTK_WIDGET(w));
	GdkDisplay* d = gdkw ? gdk_window_get_display(gdkw) : nullptr;
	GdkSeat* seat = d ? gdk_display_get_default_seat(d) : nullptr;
	GdkDevice* p = seat ? gdk_seat_get_pointer(seat) : nullptr;
	if (p == nullptr) return false;
	gdk_window_get_device_position_double(gdkw, p, &x, &y, nullptr);
	return true;
}

// An arrow pointer at (x, y), in the frame's points.
void drawPointer(cairo_t* cr, double x, double y) {
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, 0, 17);
	cairo_line_to(cr, 4.2, 13);
	cairo_line_to(cr, 7.2, 19.5);
	cairo_line_to(cr, 10, 18.3);
	cairo_line_to(cr, 7.1, 12);
	cairo_line_to(cr, 12.5, 12);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 1.2);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void finishRecording(bool keep);

// One frame: the window drawn at the movie's size (the bar over the canvas
// left out), the pointer and the clicks drawn in, stored unless it's the
// same as the one before.
void frame(Recorder* r) {
	const double t = elapsed(r);
	r->seconds = t;
	if (r->hud) gtk_widget_set_opacity(r->hud, 0);
	cairo_surface_t* shot = window(r->gw, r->width);
	if (r->hud) gtk_widget_set_opacity(r->hud, 1);
	if (shot == nullptr) return;
	const GdkRectangle f = frameOf(r->gw);
	cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, r->width, r->height);
	cairo_t* cr = cairo_create(s);
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.12);
	cairo_paint(cr);
	// The window as big as fits (it may have changed size since the start).
	const double k = std::min((double)r->width / f.width, (double)r->height / f.height);
	const double ox = (r->width - f.width * k) / 2, oy = (r->height - f.height * k) / 2;
	cairo_save(cr);
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, k * f.width / cairo_image_surface_get_width(shot), k * f.height / cairo_image_surface_get_height(shot));
	cairo_set_source_surface(cr, shot, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_surface_destroy(shot);
	double px = 0, py = 0;
	const bool havePointer = pointerAt(r->gw, px, py);
	px -= f.x;
	py -= f.y;
	const gint64 now = g_get_monotonic_time();
	if (r->clickPending && havePointer) r->clicks.push_back({ px, py, now });
	r->clickPending = false;
	cairo_translate(cr, ox, oy);
	cairo_scale(cr, k, k);
	// Clicks: a ring that grows and fades over half a second.
	r->clicks.erase(std::remove_if(r->clicks.begin(), r->clicks.end(), [&](const Click& c) { return now - c.at > 600000; }), r->clicks.end());
	for (const Click& c : r->clicks) {
		const double a = std::min(1.0, (now - c.at) / 500000.0);
		cairo_arc(cr, c.x, c.y, 8 + 18 * a, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.95, 0.27, 0.24, 0.85 * (1 - a));
		cairo_set_line_width(cr, 3);
		cairo_stroke(cr);
	}
	if (havePointer && px >= 0 && py >= 0 && px < f.width && py < f.height) drawPointer(cr, px, py);
	cairo_destroy(cr);
	cairo_surface_flush(s);

	const unsigned char* data = cairo_image_surface_get_data(s);
	const size_t n = (size_t)cairo_image_surface_get_stride(s) * r->height;
	if (r->last.size() == n && memcmp(r->last.data(), data, n) == 0) {
		cairo_surface_destroy(s);
		return;   // nothing moved: the frame before shows longer
	}
	r->last.assign(data, data + n);
	if (r->apng) {
		if (r->first == nullptr) r->first = pixbuf(s, r->width * 168 / r->height, 168);
		r->png.add(data, cairo_image_surface_get_stride(s), (long long)(t * 1000));
		cairo_surface_destroy(s);
		return;
	}
	GdkPixbuf* pb = gdk_pixbuf_get_from_surface(s, 0, 0, r->width, r->height);
	cairo_surface_destroy(s);
	if (pb == nullptr) return;
	gchar* buf = nullptr;
	gsize len = 0;
	const bool saved = r->jpeg ? gdk_pixbuf_save_to_buffer(pb, &buf, &len, "jpeg", nullptr, "quality", "72", nullptr)
	                           : gdk_pixbuf_save_to_buffer(pb, &buf, &len, "png", nullptr, "compression", "3", nullptr);
	if (r->first == nullptr) r->first = gdk_pixbuf_scale_simple(pb, std::max(1, r->width * 168 / r->height), 168, GDK_INTERP_BILINEAR);
	g_object_unref(pb);
	if (saved) r->mov.add(buf, len, (long long)(t * 1000));
	g_free(buf);
}

gboolean tickCb(gpointer) {
	Recorder* r = gRec;
	if (r == nullptr || r->stopping) return G_SOURCE_REMOVE;
	r->timer = 0;
	const gint64 began = g_get_monotonic_time();
	guarded("recording", [&] { frame(r); });
	if (r->timeLabel)
		gtk_label_set_text(GTK_LABEL(r->timeLabel), (mmss(elapsed(r)) + " of " + mmss(kMaxSeconds)).c_str());
	if (r->dot) gtk_widget_queue_draw(r->dot);
	if (elapsed(r) >= kMaxSeconds || (r->apng ? r->png.size() >= kMaxPngBytes : r->mov.size() >= kMaxBytes)) {
		finishRecording(true);
		return G_SOURCE_REMOVE;
	}
	// A slow frame (a Pi, a big circuit): fewer of them, so the app stays usable.
	const int took = (int)((g_get_monotonic_time() - began) / 1000);
	r->timer = g_timeout_add((guint)std::max(kFastestMs, took * 3), tickCb, nullptr);
	return G_SOURCE_REMOVE;
}

// The bar away (it may have gone with the window already).
void dropHud(Recorder* r) {
	if (r->hud == nullptr) return;
	GtkWidget* hud = r->hud;
	g_object_remove_weak_pointer(G_OBJECT(hud), (gpointer*)&r->hud);
	r->hud = nullptr;
	gtk_widget_destroy(hud);
}

void report(Recorder* r, Recording out) {
	std::function<void(const Recording&)> done = r->done;
	gRec = nullptr;
	delete r;
	if (done) done(out);
}

bool startFfmpeg(Recorder* r);

void ffmpegDone(GPid pid, gint status, gpointer) {
	g_spawn_close_pid(pid);
	Recorder* r = gRec;
	if (r == nullptr) return;
	if (r->ffmpegTimeout) g_source_remove(r->ffmpegTimeout);
	r->ffmpegTimeout = 0;
	r->ffmpeg = 0;
	GStatBuf st0;
	const bool made = g_spawn_check_exit_status(status, nullptr) && g_stat(r->mp4File.c_str(), &st0) == 0 && st0.st_size > 0;
	// An ffmpeg without H.264 (rare): MPEG-4, which QuickTime and VLC play.
	if (!made && r->ffmpegTry == 1 && startFfmpeg(r)) return;
	Recording out;
	out.seconds = r->seconds;
	out.frames = r->mov.count();
	out.thumb = r->first;
	r->first = nullptr;
	GStatBuf st;
	if (g_spawn_check_exit_status(status, nullptr) && g_stat(r->mp4File.c_str(), &st) == 0 && st.st_size > 0) {
		::g_remove(r->movFile.c_str());
		out.file = r->mp4File;
		out.type = "video/mp4";
		out.ext = "mp4";
	} else {
		::g_remove(r->mp4File.c_str());
		out.file = r->movFile;
		out.type = "video/quicktime";
		out.ext = "mov";
	}
	dropHud(r);
	report(r, out);
}

// The movie made an MP4 by ffmpeg, when the computer has it (on its own
// thread of the app's loop: the window carries on). False if it can't start.
bool startFfmpeg(Recorder* r) {
	if (r->ffmpegPath.empty()) return false;
	r->mp4File = r->movFile.substr(0, r->movFile.size() - 4) + ".mp4";
	std::vector<std::string> args = { r->ffmpegPath, "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", r->movFile, "-an" };
	if (r->ffmpegTry++ == 0) args.insert(args.end(), { "-c:v", "libx264", "-preset", "veryfast", "-crf", "26", "-pix_fmt", "yuv420p" });
	else args.insert(args.end(), { "-c:v", "mpeg4", "-q:v", "5", "-pix_fmt", "yuv420p" });
	args.insert(args.end(), { "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2", "-movflags", "+faststart", r->mp4File });
	std::vector<gchar*> argv;
	for (std::string& a : args) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	// The app's own library path (an AppImage's) isn't ffmpeg's.
	gchar** env = g_get_environ();
	env = g_environ_unsetenv(env, "LD_LIBRARY_PATH");
	env = g_environ_unsetenv(env, "LD_PRELOAD");
	GPid pid = 0;
	const gboolean ok = g_spawn_async(nullptr, argv.data(), env,
	                                  (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
	                                  nullptr, nullptr, &pid, nullptr);
	g_strfreev(env);
	if (!ok) return false;
	r->ffmpeg = pid;
	g_child_watch_add(pid, ffmpegDone, nullptr);
	// Two minutes at most: then the .mov goes as it is.
	r->ffmpegTimeout = g_timeout_add_seconds(120, +[](gpointer) -> gboolean {
		if (gRec && gRec->ffmpeg) kill(gRec->ffmpeg, SIGTERM);
		if (gRec) gRec->ffmpegTimeout = 0;
		return G_SOURCE_REMOVE;
	}, nullptr);
	return true;
}

void finishRecording(bool keep) {
	Recorder* r = gRec;
	if (r == nullptr || r->stopping) return;
	r->stopping = true;
	if (r->timer) g_source_remove(r->timer);
	r->timer = 0;
	gdk_event_handler_set((GdkEventFunc)gtk_main_do_event, nullptr, nullptr);
	if (r->destroyId && !r->gone) g_signal_handler_disconnect(r->gw, r->destroyId);
	r->destroyId = 0;
	const double length = r->seconds;
	const bool whole = r->apng ? r->png.finish((long long)(length * 1000)) : r->mov.finish((long long)(length * 1000));
	Recording out;
	out.seconds = length;
	out.frames = r->apng ? r->png.count() : r->mov.count();
	if (!keep || !whole || length < 0.3 || out.frames == 0) {
		::g_remove(r->movFile.c_str());
		if (keep) out.why = whole ? "The recording was too short." : "The recording couldn't be saved.";
		dropHud(r);
		if (r->first) g_object_unref(r->first);
		r->first = nullptr;
		report(r, out);
		return;
	}
	if (!r->apng && !r->gone && r->hud && startFfmpeg(r)) {
		// The bar says what's happening while ffmpeg works.
		gtk_label_set_text(GTK_LABEL(r->title), "Saving the recording…");
		gtk_label_set_text(GTK_LABEL(r->timeLabel), mmss(length).c_str());
		gtk_widget_hide(r->buttons);
		return;
	}
	dropHud(r);
	out.file = r->movFile;
	out.type = r->apng ? "image/png" : "video/quicktime";
	out.ext = r->apng ? "png" : "mov";
	out.thumb = r->first;
	r->first = nullptr;
	report(r, out);
}

GtkWidget* hudButton(const char* label, const char* icon, const char* tip, bool stop) {
	GtkWidget* b = icon ? gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_MENU) : gtk_button_new_with_mnemonic(label);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_style_context_add_class(gtk_widget_get_style_context(b), stop ? "rec-stop" : "rec-cancel");
	g_signal_connect(b, "clicked", CL_CALLBACK(+[](GtkButton*, gpointer keep) {
		guarded("recording", [&] { finishRecording(keep != nullptr); });
	}), stop ? (gpointer)1 : nullptr);
	return b;
}

// The bar over the canvas: a pulsing dot, what it's doing, the time, a
// cancel and Stop. In the window itself, so it's there on Wayland too.
void makeHud(Recorder* r) {
	GtkWidget* over = r->win->overlay();
	if (over == nullptr) return;
	r->hud = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_name(r->hud, "rec-hud");
	gtk_widget_set_halign(r->hud, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(r->hud, GTK_ALIGN_START);
	gtk_widget_set_margin_top(r->hud, 12);
	r->dot = gtk_drawing_area_new();
	gtk_widget_set_size_request(r->dot, 12, 12);
	gtk_widget_set_valign(r->dot, GTK_ALIGN_CENTER);
	g_signal_connect(r->dot, "draw", CL_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
		const double t = g_get_monotonic_time() / 1e6;
		const double a = gRec && !gRec->stopping ? 0.675 + 0.325 * std::cos(t * M_PI / 0.8) : 1.0;
		cairo_arc(cr, gtk_widget_get_allocated_width(w) / 2.0, gtk_widget_get_allocated_height(w) / 2.0, 5, 0, 2 * M_PI);
		cairo_set_source_rgba(cr, 0.92, 0.27, 0.24, a);
		cairo_fill(cr);
		return TRUE;
	}), nullptr);
	gtk_box_pack_start(GTK_BOX(r->hud), r->dot, FALSE, FALSE, 0);
	GtkWidget* words = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	r->title = gtk_label_new("Recording CedarLogic");
	gtk_style_context_add_class(gtk_widget_get_style_context(r->title), "rec-title");
	gtk_label_set_xalign(GTK_LABEL(r->title), 0);
	r->timeLabel = gtk_label_new(("0:00 of " + mmss(kMaxSeconds)).c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(r->timeLabel), "rec-time");
	gtk_label_set_xalign(GTK_LABEL(r->timeLabel), 0);
	gtk_box_pack_start(GTK_BOX(words), r->title, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(words), r->timeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r->hud), words, TRUE, TRUE, 0);
	r->buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_pack_start(GTK_BOX(r->buttons), hudButton(nullptr, "window-close-symbolic", "Throw this recording away", false), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r->buttons), hudButton("_Stop", nullptr, "Stop, and add the recording to your feedback", true), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(r->hud), r->buttons, FALSE, FALSE, 0);
	gtk_overlay_add_overlay(GTK_OVERLAY(over), r->hud);
	gtk_widget_show_all(r->hud);
	// Gone with the window, if it closes first.
	g_object_add_weak_pointer(G_OBJECT(r->hud), (gpointer*)&r->hud);
}

}  // namespace

bool recording() { return gRec != nullptr; }

bool record(CircuitWindow* w, const std::string& folder, std::function<void(const Recording&)> done) {
	if (gRec || w == nullptr) return false;
	GtkWindow* gw = w->window();
	const GdkRectangle f = frameOf(gw);
	if (f.width <= 0 || f.height <= 0) return false;
	Recorder* r = new Recorder;
	r->win = w;
	r->gw = gw;
	r->folder = folder;
	r->done = done;
	// JPEG pictures in a movie that ffmpeg makes an MP4 of at the end; with no
	// ffmpeg (or no JPEG writer), an animated PNG.
	if (!g_getenv("CL_CAPTURE_NO_FFMPEG"))
		if (gchar* ff = g_find_program_in_path("ffmpeg")) { r->ffmpegPath = ff; g_free(ff); }
	r->jpeg = false;
	GSList* formats = gdk_pixbuf_get_formats();
	for (GSList* l = formats; l; l = l->next) {
		GdkPixbufFormat* fm = static_cast<GdkPixbufFormat*>(l->data);
		gchar* name = gdk_pixbuf_format_get_name(fm);
		if (name && strcmp(name, "jpeg") == 0 && gdk_pixbuf_format_is_writable(fm)) r->jpeg = true;
		g_free(name);
	}
	g_slist_free(formats);
	r->apng = r->ffmpegPath.empty() || !r->jpeg;
	// As sharp as the screen, but no wider than the limit.
	const double scale = std::min((double)std::max(1, gtk_widget_get_scale_factor(GTK_WIDGET(gw))),
	                              (double)(r->apng ? kMaxPngWidth : kMaxWidth) / f.width);
	r->width = std::max(16, (int)std::lround(f.width * scale));
	r->height = std::max(16, (int)std::lround(f.height * scale));
	r->movFile = folder + "/" + format("%08lx", (unsigned long)(g_get_real_time() / 1000)) + (r->apng ? "-recording.png" : "-recording.mov");
	if (r->apng ? !r->png.open(r->movFile, r->width, r->height) : !r->mov.open(r->movFile, r->width, r->height, "jpeg")) {
		delete r;
		return false;
	}
	gRec = r;
	r->destroyId = g_signal_connect(gw, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer) {
		if (gRec) { gRec->gone = true; finishRecording(true); }
	}), nullptr);
	gdk_event_handler_set(eventHandler, nullptr, nullptr);
	makeHud(r);
	r->started = g_get_monotonic_time();
	frame(r);
	r->timer = g_timeout_add(kFastestMs, tickCb, nullptr);
	return true;
}

void stop(bool keep) { finishRecording(keep); }

// ---- The self-test ---------------------------------------------------------------

namespace {

bool looksDrawn(cairo_surface_t* s) {
	const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s), stride = cairo_image_surface_get_stride(s);
	const unsigned char* d = cairo_image_surface_get_data(s);
	std::vector<guint32> seen;
	for (int y = 0; y < h; y += std::max(1, h / 40))
		for (int x = 0; x < w; x += std::max(1, w / 40)) {
			guint32 p;
			memcpy(&p, d + (size_t)y * stride + (size_t)x * 4, 4);
			if (std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
		}
	return seen.size() >= 8;
}

}  // namespace

int selfTest(CircuitWindow* w, const std::string& folder) {
	g_mkdir_with_parents(folder.c_str(), 0700);
	int failures = 0;
	cairo_surface_t* shot = window(w->window(), 0);
	if (shot == nullptr) {
		fprintf(stderr, "capture: FAIL no screenshot\n");
		return 1;
	}
	const GdkRectangle f = frameOf(w->window());
	GtkAllocation all;
	gtk_widget_get_allocation(GTK_WIDGET(w->window()), &all);
	const bool drawn = looksDrawn(shot);
	fprintf(stderr, "capture: screenshot %dx%d of a %dx%d window (frame at %d,%d), %s\n", cairo_image_surface_get_width(shot),
	        cairo_image_surface_get_height(shot), all.width, all.height, f.x, f.y, drawn ? "drawn" : "FAIL: blank");
	failures += !drawn;
	cairo_surface_write_to_png(shot, (folder + "/capture-screenshot.png").c_str());
	cairo_surface_destroy(shot);

	static Recording got;
	static bool finished = false;
	GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
	if (!record(w, folder, [loop](const Recording& r) {
		    got = r;
		    finished = true;
		    g_main_loop_quit(loop);
	    })) {
		fprintf(stderr, "capture: FAIL the recording didn't start\n");
		return failures + 1;
	}
	// Something to see: Simulation View, a zoom, back again, a picture of the
	// window with the bar, then Stop.
	static CircuitWindow* cw;
	static std::string dir;
	cw = w;
	dir = folder;
	g_timeout_add(400, +[](gpointer) -> gboolean { cw->runAction("win.sim-view"); return G_SOURCE_REMOVE; }, nullptr);
	g_timeout_add(1000, +[](gpointer) -> gboolean { cw->runAction("win.zoom-in"); return G_SOURCE_REMOVE; }, nullptr);
	g_timeout_add(1700, +[](gpointer) -> gboolean {
		cw->runAction("win.sim-view");
		if (cairo_surface_t* s = window(cw->window(), 0)) {
			cairo_surface_write_to_png(s, (dir + "/capture-recording-bar.png").c_str());
			cairo_surface_destroy(s);
		}
		return G_SOURCE_REMOVE;
	}, nullptr);
	g_timeout_add(2600, +[](gpointer) -> gboolean { stop(true); return G_SOURCE_REMOVE; }, nullptr);
	g_timeout_add_seconds(150, +[](gpointer l) -> gboolean { g_main_loop_quit(static_cast<GMainLoop*>(l)); return G_SOURCE_REMOVE; }, loop);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	if (!finished || got.file.empty()) {
		fprintf(stderr, "capture: FAIL no recording (%s)\n", got.why.c_str());
		return failures + 1;
	}
	GStatBuf st;
	const long long size = g_stat(got.file.c_str(), &st) == 0 ? (long long)st.st_size : 0;
	const bool ok = got.frames >= 3 && size > 10000 && got.seconds > 2 && got.thumb != nullptr;
	fprintf(stderr, "capture: recording %s (%s), %.1f s, %d frame(s) stored, %lld bytes: %s\n", got.file.c_str(), got.type.c_str(),
	        got.seconds, got.frames, size, ok ? "ok" : "FAIL");
	const std::string kept = folder + "/capture-recording." + got.ext;
	if (g_rename(got.file.c_str(), kept.c_str()) == 0) fprintf(stderr, "capture: kept as %s\n", kept.c_str());
	if (got.thumb) {
		gdk_pixbuf_save(got.thumb, (folder + "/capture-thumb.png").c_str(), "png", nullptr, nullptr);
		g_object_unref(got.thumb);
	}
	return failures + !ok;
}

}  // namespace capture
