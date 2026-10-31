// Text metrics for a headless build (CL_HEADLESS, NullScene.h): no font to
// measure with, so a fixed advance per character. A label's hit box comes out
// the same on every machine, which is what the tests and fuzzers need.
// Empty in every other build: each platform's scene supplies these instead.

#if defined(CL_HEADLESS)

#include "render/SkiaProbe.h"

namespace cl {
namespace render {

float measuredTextWidth(const char* utf8, float pixelHeight) {
	if (utf8 == nullptr) return 0.0f;
	int chars = 0;
	for (const char* p = utf8; *p; p++)
		if ((*p & 0xC0) != 0x80) chars++;   // count characters, not UTF-8 bytes
	return 0.6f * pixelHeight * (float)chars;
}

float measuredTextHeight(float pixelHeight) { return 0.9f * pixelHeight; }

}  // namespace render
}  // namespace cl

#endif  // CL_HEADLESS
