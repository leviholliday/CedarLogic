/*****************************************************************************
   Project: CEDAR Logic Simulator
   DcRasterPaint: show the processor fallback's frame without OpenGL.
*****************************************************************************/

#ifdef __WXGTK__

#include "DcRasterPaint.h"

#include <wx/bitmap.h>
#include <wx/dc.h>
#include <cstring>

#include "render/RasterPresent.h"

DcRasterPaint::DcRasterPaint() {
	cl::render::setRasterSink([this](int w, int h, const unsigned char* rgb) {
		if (w <= 0 || h <= 0 || rgb == nullptr) return;
		frame.Create(w, h, /*clear*/ false);
		std::memcpy(frame.GetData(), rgb, (size_t)w * h * 3);
	});
}

DcRasterPaint::~DcRasterPaint() {
	cl::render::setRasterSink(nullptr);
}

void DcRasterPaint::paint(wxDC& dc, double contentScale) const {
	if (!frame.IsOk()) return;
	const wxBitmap bmp(frame, -1, contentScale > 0 ? contentScale : 1.0);
	dc.DrawBitmap(bmp, 0, 0, false);
}

#endif // __WXGTK__
