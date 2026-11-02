// Pictures made off screen (see Images.h).

#include "Images.h"
#include "Pdf.h"

#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <vector>

namespace images {

namespace {

IWICImagingFactory* wic() {
	static IWICImagingFactory* f = [] {
		IWICImagingFactory* made = nullptr;
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&made));
		return made;
	}();
	return f;
}

bool encode(IWICBitmapSource* bmp, IStream* out) {
	IWICBitmapEncoder* enc = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	const bool ok = wic() && SUCCEEDED(wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
	                SUCCEEDED(enc->Initialize(out, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
	                SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->WriteSource(bmp, nullptr)) &&
	                SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
	if (frame) frame->Release();
	if (enc) enc->Release();
	return ok;
}

}  // namespace

Sheet::~Sheet() {
	if (rt) {
		rt->EndDraw();
		rt->Release();
	}
	if (bmp) bmp->Release();
}

bool Sheet::begin(double w, double h, double scale, bool white) {
	if (wic() == nullptr || w <= 0 || h <= 0 || rt != nullptr) return false;
	const UINT pw = (UINT)std::ceil(w * scale), ph = (UINT)std::ceil(h * scale);
	if (FAILED(wic()->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	if (FAILED(d2dFactory()->CreateWicBitmapRenderTarget(bmp, props, &rt))) {
		bmp->Release();
		bmp = nullptr;
		return false;
	}
	rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // no coloured fringes on a picture
	rt->BeginDraw();
	rt->Clear(white ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 0));
	rt->SetTransform(D2D1::Matrix3x2F::Scale((float)scale, (float)scale));
	return true;
}

IWICBitmap* Sheet::finish() {
	if (rt == nullptr) return nullptr;
	const bool ok = SUCCEEDED(rt->EndDraw());
	rt->Release();
	rt = nullptr;
	IWICBitmap* out = bmp;
	bmp = nullptr;
	if (!ok && out) {
		out->Release();
		out = nullptr;
	}
	return out;
}

IWICBitmap* render(double w, double h, double scale, bool white, const std::function<void(ID2D1RenderTarget*)>& draw) {
	Sheet sheet;
	if (!sheet.begin(w, h, scale, white)) return nullptr;
	draw(sheet.target());
	return sheet.finish();
}

bool pdfPixels(IWICBitmap* bmp, std::vector<unsigned char>& px, int& components, UINT& w, UINT& h) {
	w = h = 0;
	if (bmp == nullptr || FAILED(bmp->GetSize(&w, &h)) || w == 0 || h == 0) return false;
	std::vector<unsigned char> src((size_t)w * h * 4);
	if (FAILED(bmp->CopyPixels(nullptr, w * 4, (UINT)src.size(), src.data()))) return false;
	const size_t count = (size_t)w * h;
	bool grayOnly = true;
	for (size_t i = 0; i < count; i++) {
		unsigned char* p = &src[i * 4];   // blue, green, red, alpha (premultiplied): onto white
		if (p[3] != 255) {
			const int add = 255 - p[3];
			for (int k = 0; k < 3; k++) p[k] = (unsigned char)std::min(255, p[k] + add);
		}
		if (p[0] != p[1] || p[1] != p[2]) grayOnly = false;
	}
	components = grayOnly ? 1 : 3;
	px.assign(count * components, 0);
	for (size_t i = 0; i < count; i++) {
		const unsigned char* p = &src[i * 4];
		if (grayOnly) {
			px[i] = p[2];
		} else {
			px[i * 3] = p[2];
			px[i * 3 + 1] = p[1];
			px[i * 3 + 2] = p[0];
		}
	}
	return true;
}

bool savePdf(IWICBitmap* bmp, float widthPt, float heightPt, const std::string& title, const std::string& file) {
	std::vector<unsigned char> px;
	int components = 3;
	UINT w = 0, h = 0;
	if (!pdfPixels(bmp, px, components, w, h)) return false;
	SYSTEMTIME t;
	GetLocalTime(&t);
	char date[32];
	snprintf(date, sizeof date, "%04u%02u%02u%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	pdf::Document doc(title, "CedarLogic " CL_VERSION, date);
	// PDF points are 1/72 inch; the picture's are 1/96.
	doc.addPage(widthPt * 0.75f, heightPt * 0.75f, (int)w, (int)h, components, px.data());
	const std::string& bytes = doc.finish();
	HANDLE f = CreateFileW(W(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size();
	CloseHandle(f);
	if (!ok) DeleteFileW(W(file).c_str());
	return ok;
}

bool savePng(IWICBitmapSource* bmp, const std::string& file) {
	IWICStream* stream = nullptr;
	const bool ok = wic() && SUCCEEDED(wic()->CreateStream(&stream)) &&
	                SUCCEEDED(stream->InitializeFromFilename(W(file).c_str(), GENERIC_WRITE)) && encode(bmp, stream);
	if (stream) stream->Release();
	return ok;
}

namespace {

// A decoder's first frame, shrunk to fit and made ready to draw.
IWICBitmap* fromDecoder(IWICBitmapDecoder* dec, UINT maxW, UINT maxH) {
	IWICBitmapFrameDecode* frame = nullptr;
	IWICBitmapScaler* scaler = nullptr;
	IWICFormatConverter* conv = nullptr;
	IWICBitmap* out = nullptr;
	UINT w = 0, h = 0;
	if (dec && SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
		const double s = std::min(1.0, std::min((double)maxW / w, (double)maxH / h));
		const UINT sw = std::max(1u, (UINT)(w * s)), sh = std::max(1u, (UINT)(h * s));
		if (SUCCEEDED(wic()->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)) &&
		    SUCCEEDED(wic()->CreateFormatConverter(&conv)) &&
		    SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
			wic()->CreateBitmapFromSource(conv, WICBitmapCacheOnLoad, &out);
	}
	if (conv) conv->Release();
	if (scaler) scaler->Release();
	if (frame) frame->Release();
	return out;
}

}  // namespace

IWICBitmap* load(const std::string& file, UINT maxW, UINT maxH) {
	if (wic() == nullptr) return nullptr;
	IWICBitmapDecoder* dec = nullptr;
	if (FAILED(wic()->CreateDecoderFromFilename(W(file).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
	IWICBitmap* out = fromDecoder(dec, maxW, maxH);
	dec->Release();
	return out;
}

IWICBitmap* loadResource(int id, UINT maxW, UINT maxH) {
	if (wic() == nullptr) return nullptr;
	HRSRC res = FindResourceW(appInstance(), MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
	HGLOBAL mem = res ? LoadResource(appInstance(), res) : nullptr;
	const DWORD size = res ? SizeofResource(appInstance(), res) : 0;
	void* data = mem ? LockResource(mem) : nullptr;
	if (data == nullptr || size == 0) return nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapDecoder* dec = nullptr;
	IWICBitmap* out = nullptr;
	if (SUCCEEDED(wic()->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory((BYTE*)data, size)) &&
	    SUCCEEDED(wic()->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec)))
		out = fromDecoder(dec, maxW, maxH);
	if (dec) dec->Release();
	if (stream) stream->Release();
	return out;
}

bool copyToClipboard(HWND owner, IWICBitmap* bmp) {
	UINT w = 0, h = 0;
	if (bmp == nullptr || FAILED(bmp->GetSize(&w, &h))) return false;
	// The pixels, as a bottom-up 32-bit DIB on white (no transparency).
	std::vector<BYTE> px((size_t)w * h * 4);
	if (FAILED(bmp->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) return false;
	const size_t headerSize = sizeof(BITMAPINFOHEADER);
	HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, headerSize + px.size());
	if (dib == nullptr) return false;
	BYTE* p = (BYTE*)GlobalLock(dib);
	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof bi;
	bi.biWidth = (LONG)w;
	bi.biHeight = (LONG)h;   // bottom-up
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	memcpy(p, &bi, headerSize);
	for (UINT y = 0; y < h; y++) {
		BYTE* row = p + headerSize + (size_t)(h - 1 - y) * w * 4;
		memcpy(row, px.data() + (size_t)y * w * 4, (size_t)w * 4);
		for (UINT x = 0; x < w; x++) row[x * 4 + 3] = 255;
	}
	GlobalUnlock(dib);
	// And as a PNG.
	HGLOBAL png = nullptr;
	IStream* stream = nullptr;
	if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, FALSE, &stream))) {
		if (encode(bmp, stream)) GetHGlobalFromStream(stream, &png);
		stream->Release();
	}
	if (!OpenClipboard(owner)) {
		GlobalFree(dib);
		if (png) GlobalFree(png);
		return false;
	}
	EmptyClipboard();
	const bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
	if (!ok) GlobalFree(dib);
	if (png) {
		static const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
		if (!SetClipboardData(pngFormat, png)) GlobalFree(png);
	}
	CloseClipboard();
	return ok;
}

}  // namespace images
