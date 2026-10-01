// Pictures made off screen (see Images.h).

#include "Images.h"

#include <objidl.h>

#include <algorithm>
#include <cmath>

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

IWICBitmap* render(double w, double h, double scale, bool white, const std::function<void(ID2D1RenderTarget*)>& draw) {
	if (wic() == nullptr || w <= 0 || h <= 0) return nullptr;
	const UINT pw = (UINT)std::ceil(w * scale), ph = (UINT)std::ceil(h * scale);
	IWICBitmap* bmp = nullptr;
	if (FAILED(wic()->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return nullptr;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
	ID2D1RenderTarget* rt = nullptr;
	if (FAILED(d2dFactory()->CreateWicBitmapRenderTarget(bmp, props, &rt))) { bmp->Release(); return nullptr; }
	rt->BeginDraw();
	rt->Clear(white ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 0));
	rt->SetTransform(D2D1::Matrix3x2F::Scale((float)scale, (float)scale));
	draw(rt);
	const bool ok = SUCCEEDED(rt->EndDraw());
	rt->Release();
	if (!ok) { bmp->Release(); return nullptr; }
	return bmp;
}

bool savePng(IWICBitmapSource* bmp, const std::string& file) {
	IWICStream* stream = nullptr;
	const bool ok = wic() && SUCCEEDED(wic()->CreateStream(&stream)) &&
	                SUCCEEDED(stream->InitializeFromFilename(W(file).c_str(), GENERIC_WRITE)) && encode(bmp, stream);
	if (stream) stream->Release();
	return ok;
}

IWICBitmap* load(const std::string& file, UINT maxW, UINT maxH) {
	if (wic() == nullptr) return nullptr;
	IWICBitmapDecoder* dec = nullptr;
	IWICBitmapFrameDecode* frame = nullptr;
	IWICBitmapScaler* scaler = nullptr;
	IWICFormatConverter* conv = nullptr;
	IWICBitmap* out = nullptr;
	UINT w = 0, h = 0;
	if (SUCCEEDED(wic()->CreateDecoderFromFilename(W(file).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
	    SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
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
	if (dec) dec->Release();
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
