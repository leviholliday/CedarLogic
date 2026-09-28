// Bitmap -- an offscreen RGBA image the checks draw into, read back and save:
// Core Graphics on the Mac, Cairo on Linux. Its context is what CedarCore's
// drawing calls take.
#ifndef CL_TOOLS_BITMAP_H
#define CL_TOOLS_BITMAP_H

#include "CedarCore.h"
#include <cstring>
#include <vector>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <CoreServices/CoreServices.h>
#include <ImageIO/ImageIO.h>
#endif

class Bitmap {
public:
	// Cleared to white, or left transparent.
	Bitmap(int w, int h, bool white = true) : w(w), h(h) {
#if defined(__APPLE__)
		cs = CGColorSpaceCreateDeviceRGB();
		c = CGBitmapContextCreate(nullptr, w, h, 8, 0, cs, kCGImageAlphaPremultipliedLast);
		if (white) { CGContextSetRGBFillColor(c, 1, 1, 1, 1); CGContextFillRect(c, CGRectMake(0, 0, w, h)); }
#else
		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		c = cairo_create(surface);
		if (white) { cairo_set_source_rgb(c, 1, 1, 1); cairo_paint(c); }
#endif
	}
	~Bitmap() {
#if defined(__APPLE__)
		CGContextRelease(c);
		CGColorSpaceRelease(cs);
#else
		cairo_destroy(c);
		cairo_surface_destroy(surface);
#endif
	}
	Bitmap(const Bitmap&) = delete;
	Bitmap& operator=(const Bitmap&) = delete;

	// A flipped view in points, as a window's is: y down, scaled by the
	// backing factor.
	void flip(double backingScale) {
#if defined(__APPLE__)
		CGContextTranslateCTM(c, 0, h);
		CGContextScaleCTM(c, backingScale, -backingScale);
#else
		// Cairo's image space is already y down.
		cairo_scale(c, backingScale, backingScale);
#endif
	}

	CLContext ctx() const { return c; }

	// The pixels, 4 bytes each, alpha in the last (premultiplied).
	std::vector<unsigned char> pixels() const {
		std::vector<unsigned char> out((size_t)w * h * 4);
#if defined(__APPLE__)
		const unsigned char* src = (const unsigned char*)CGBitmapContextGetData(c);
		const size_t stride = CGBitmapContextGetBytesPerRow(c);
#else
		cairo_surface_flush(surface);
		const unsigned char* src = cairo_image_surface_get_data(surface);
		const size_t stride = (size_t)cairo_image_surface_get_stride(surface);
#endif
		for (int y = 0; y < h; y++) std::memcpy(&out[(size_t)y * w * 4], src + y * stride, (size_t)w * 4);
#if !defined(__APPLE__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		// Cairo stores native-endian ARGB words: A first on big-endian.
		for (size_t i = 0; i < out.size(); i += 4) {
			const unsigned char a = out[i];
			std::memmove(&out[i], &out[i + 1], 3);
			out[i + 3] = a;
		}
#endif
		return out;
	}

	bool savePng(const char* path) const {
#if defined(__APPLE__)
		CGImageRef img = CGBitmapContextCreateImage(c);
		CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path, strlen(path), false);
		CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
		CGImageDestinationAddImage(d, img, nullptr);
		const bool ok = CGImageDestinationFinalize(d);
		CFRelease(d);
		CFRelease(url);
		CGImageRelease(img);
		return ok;
#else
		cairo_surface_flush(surface);
		return cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS;
#endif
	}

private:
	int w, h;
#if defined(__APPLE__)
	CGColorSpaceRef cs;
	CGContextRef c;
#else
	cairo_surface_t* surface;
	cairo_t* c;
#endif
};

#endif  // CL_TOOLS_BITMAP_H
