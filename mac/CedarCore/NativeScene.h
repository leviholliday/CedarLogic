// NativeScene -- the platform's render Scene and the device-pixel setup every
// drawing call shares: Core Graphics on the Mac (CGScene), Cairo on Linux
// (linux/CedarCore/CairoScene). The drawing code in this directory names only
// these, so the same engine draws for both front ends.

#ifndef CL_NATIVE_SCENE_H
#define CL_NATIVE_SCENE_H

#include "CedarCore.h"

#if defined(__APPLE__)
#include "CGScene.h"
#else
#include "CairoScene.h"
#endif

namespace cl {
namespace native {

#if defined(__APPLE__)
using PlatformScene = mac::CGScene;
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
#else
		cairo_save(ctx);
		cairo_scale(ctx, 1.0 / backingScale, 1.0 / backingScale);
#endif
	}
	~DevicePixels() {
#if defined(__APPLE__)
		CGContextRestoreGState(ctx);
#else
		cairo_restore(ctx);
#endif
	}
	DevicePixels(const DevicePixels&) = delete;
	DevicePixels& operator=(const DevicePixels&) = delete;
};

}  // namespace native
}  // namespace cl

#endif  // CL_NATIVE_SCENE_H
