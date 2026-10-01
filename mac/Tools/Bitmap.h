// Bitmap -- an offscreen RGBA image the checks draw into, read back and save:
// Core Graphics on the Mac, Cairo on Linux, Direct2D over a WIC bitmap on
// Windows. Its context is what CedarCore's
// drawing calls take.
#ifndef CL_TOOLS_BITMAP_H
#define CL_TOOLS_BITMAP_H

#if defined(_WIN32)
// First: the Windows headers' `byte` clashes with std::byte once something
// has said `using namespace std`.
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#endif
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
#elif defined(_WIN32)
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
		ID2D1Factory* d2d = nullptr;
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&d2d);
		if (wic) wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
		if (d2d && bmp) {
			const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
			d2d->CreateWicBitmapRenderTarget(bmp, props, &c);
		}
		if (d2d) d2d->Release();
		if (c) {
			c->BeginDraw();
			c->Clear(white ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 0));
		}
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
#elif defined(_WIN32)
		if (c) { finish(); c->Release(); }
		if (bmp) bmp->Release();
		if (wic) wic->Release();
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
#elif defined(_WIN32)
		// Direct2D's space is already y down.
		if (c) c->SetTransform(D2D1::Matrix3x2F::Scale((float)backingScale, (float)backingScale));
#else
		// Cairo's image space is already y down.
		cairo_scale(c, backingScale, backingScale);
#endif
	}

#if defined(_WIN32)
	CLContext ctx() const {
		if (c && !drawing) { c->BeginDraw(); drawing = true; }
		return (CLContext)c;
	}
#else
	CLContext ctx() const { return c; }
#endif

	// The pixels, 4 bytes each, alpha in the last (premultiplied).
	std::vector<unsigned char> pixels() const {
		std::vector<unsigned char> out((size_t)w * h * 4);
#if defined(__APPLE__)
		const unsigned char* src = (const unsigned char*)CGBitmapContextGetData(c);
		const size_t stride = CGBitmapContextGetBytesPerRow(c);
#elif defined(_WIN32)
		finish();
		std::vector<unsigned char> bgra((size_t)w * h * 4);
		if (bmp) bmp->CopyPixels(nullptr, (UINT)w * 4, (UINT)bgra.size(), bgra.data());
		for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);   // BGRA to RGBA
		const unsigned char* src = bgra.data();
		const size_t stride = (size_t)w * 4;
#else
		cairo_surface_flush(surface);
		const unsigned char* src = cairo_image_surface_get_data(surface);
		const size_t stride = (size_t)cairo_image_surface_get_stride(surface);
#endif
		for (int y = 0; y < h; y++) std::memcpy(&out[(size_t)y * w * 4], src + y * stride, (size_t)w * 4);
#if !defined(__APPLE__) && !defined(_WIN32) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
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
#elif defined(_WIN32)
		finish();
		if (!wic || !bmp) return false;
		std::wstring wpath;
		const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
		if (n <= 0) return false;
		wpath.resize((size_t)n);
		MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], n);
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* enc = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		bool ok = SUCCEEDED(wic->CreateStream(&stream)) &&
		          SUCCEEDED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE)) &&
		          SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
		          SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
		          SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
		          SUCCEEDED(frame->Initialize(nullptr)) &&
		          SUCCEEDED(frame->WriteSource(bmp, nullptr)) &&
		          SUCCEEDED(frame->Commit()) &&
		          SUCCEEDED(enc->Commit());
		if (frame) frame->Release();
		if (enc) enc->Release();
		if (stream) stream->Release();
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
#elif defined(_WIN32)
	IWICImagingFactory* wic = nullptr;
	IWICBitmap* bmp = nullptr;
	ID2D1RenderTarget* c = nullptr;
	mutable bool drawing = true;
	// End the drawing so the bitmap holds it (ctx() starts again).
	void finish() const {
		if (c && drawing) { c->EndDraw(); drawing = false; }
	}
#else
	cairo_surface_t* surface;
	cairo_t* c;
#endif
};

#endif  // CL_TOOLS_BITMAP_H
