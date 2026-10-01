// NativeScene -- the platform's render Scene and the device-pixel setup every
// drawing call shares: Core Graphics on the Mac (CGScene), Cairo on Linux
// (linux/CedarCore/CairoScene), Direct2D on Windows (windows/CedarCore/
// D2DScene). The drawing code in this directory names only these, so the same
// engine draws for every front end.

#ifndef CL_NATIVE_SCENE_H
#define CL_NATIVE_SCENE_H

#include "CedarCore.h"

#if defined(__APPLE__)
#include "CGScene.h"
#elif defined(_WIN32)
#include "D2DScene.h"
#else
#include "CairoScene.h"
#endif

namespace cl {
namespace native {

#if defined(__APPLE__)
using PlatformScene = mac::CGScene;
#elif defined(_WIN32)
using PlatformScene = d2d::D2DScene;
#else
using PlatformScene = cairo::CairoScene;
#endif

// Work in physical pixels, as the wx app's device space does, so stroke
// widths (device pixels) match it exactly -- until this goes out of scope.
struct DevicePixels {
	CLContext ctx;
	DevicePixels(CLContext ctx, double backingScale) : ctx(ctx) {
#if defined(__APPLE__)
		CGContextSaveGState(ctx);
		CGContextScaleCTM(ctx, 1.0 / backingScale, 1.0 / backingScale);
#elif defined(_WIN32)
		saved = d2d::scaleTransform(ctx, 1.0 / backingScale);
#else
		cairo_save(ctx);
		cairo_scale(ctx, 1.0 / backingScale, 1.0 / backingScale);
#endif
	}
	~DevicePixels() {
#if defined(__APPLE__)
		CGContextRestoreGState(ctx);
#elif defined(_WIN32)
		d2d::restoreTransform(ctx, saved);
#else
		cairo_restore(ctx);
#endif
	}
#if defined(_WIN32)
	d2d::SavedTransform saved;
#endif
	DevicePixels(const DevicePixels&) = delete;
	DevicePixels& operator=(const DevicePixels&) = delete;
};

}  // namespace native
}  // namespace cl

#endif  // CL_NATIVE_SCENE_H
