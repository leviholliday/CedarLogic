// Pictures made off screen: drawn with Direct2D into a WIC bitmap, then saved
// as a PNG or put on the clipboard (as a bitmap, and as a PNG for the apps
// that take one: Word, Docs, OneNote).

#ifndef CL_WINDOWS_IMAGES_H
#define CL_WINDOWS_IMAGES_H

#include "App.h"
#include <wincodec.h>

#include <functional>
#include <string>

namespace images {

// A white (or clear) picture w x h points at `scale` pixels a point; `draw`
// gets a target in points. Null if it couldn't be made. Release it after.
IWICBitmap* render(double w, double h, double scale, bool white, const std::function<void(ID2D1RenderTarget*)>& draw);
bool savePng(IWICBitmapSource* bmp, const std::string& file);
// A picture file (PNG, JPEG...) shrunk to fit maxW x maxH pixels, ready to
// draw (premultiplied BGRA). Null if it couldn't be read.
IWICBitmap* load(const std::string& file, UINT maxW, UINT maxH);
bool copyToClipboard(HWND owner, IWICBitmap* bmp);

}  // namespace images

#endif  // CL_WINDOWS_IMAGES_H
