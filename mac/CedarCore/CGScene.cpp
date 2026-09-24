// CGScene -- see CGScene.h.

#include "CGScene.h"
#include "render/SkiaProbe.h"   // measuredTextWidth/Height, supplied below
#include <CoreText/CoreText.h>
#include <cmath>

namespace cl {
namespace mac {

namespace {

// Glyph outlines are built at this size and scaled into place, as the Skia
// backend does (SkiaScene.h), so labels measure and sit the same in both apps.
const CGFloat kGlyphUnits = 100.0;

CGAffineTransform toCG(const render::Transform& t) {
	// Scene transforms are [a c e ; b d f], the same layout CGAffineTransform uses.
	return CGAffineTransformMake(t.a, t.b, t.c, t.d, t.e, t.f);
}

// The label face: Helvetica, which is what the wx app's font search lands on
// on a Mac (its Arial Bold path isn't where macOS keeps Arial).
CTFontRef labelFont() {
	static CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica"), kGlyphUnits, nullptr);
	return font;
}

// A laid-out line of text at kGlyphUnits; the caller releases it.
CTLineRef makeLine(const char* utf8) {
	if (utf8 == nullptr || *utf8 == 0) return nullptr;
	CFStringRef str = CFStringCreateWithCString(nullptr, utf8, kCFStringEncodingUTF8);
	if (str == nullptr) return nullptr;
	const void* keys[] = { kCTFontAttributeName };
	const void* vals[] = { labelFont() };
	CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, vals, 1,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attrs);
	CTLineRef line = CTLineCreateWithAttributedString(as);
	CFRelease(as);
	CFRelease(attrs);
	CFRelease(str);
	return line;
}

CGLineCap toCap(render::Cap c) {
	switch (c) {
	case render::Cap::Round:  return kCGLineCapRound;
	case render::Cap::Square: return kCGLineCapSquare;
	default:                  return kCGLineCapButt;
	}
}

}  // namespace

CGScene::CGScene(CGContextRef ctx) : ctx(ctx) {}

CGAffineTransform CGScene::current() const {
	// The innermost local transform applies first, the viewport last.
	CGAffineTransform m = viewport;
	for (auto it = stack.rbegin(); it != stack.rend(); ++it) m = CGAffineTransformConcat(*it, m);
	return m;
}

void CGScene::setViewport(const render::Transform& worldToDevice) {
	viewport = toCG(worldToDevice);
	stack.clear();
}

void CGScene::pushTransform(const render::Transform& local) {
	// Stored outermost first; current() composes them.
	stack.insert(stack.begin(), toCG(local));
}

void CGScene::popTransform() {
	if (!stack.empty()) stack.erase(stack.begin());
}

void CGScene::stroke(CGPathRef path, const render::Stroke& s) {
	CGContextSaveGState(ctx);
	CGContextAddPath(ctx, path);
	CGContextSetLineWidth(ctx, s.width);   // device pixels, like the wx app
	CGContextSetLineCap(ctx, toCap(s.cap));
	if (s.dashed) {
		const CGFloat dash[] = { 3.0, 3.0 };
		CGContextSetLineDash(ctx, 0, dash, 2);
	}
	CGContextSetRGBStrokeColor(ctx, s.color.r, s.color.g, s.color.b, s.color.a);
	CGContextStrokePath(ctx);
	CGContextRestoreGState(ctx);
}

void CGScene::fill(CGPathRef path, const render::Color& c) {
	CGContextSaveGState(ctx);
	CGContextAddPath(ctx, path);
	CGContextSetRGBFillColor(ctx, c.r, c.g, c.b, c.a);
	CGContextFillPath(ctx);
	CGContextRestoreGState(ctx);
}

void CGScene::lines(const render::Point* pts, std::size_t count, const render::Stroke& s) {
	if (count < 2) return;
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	for (std::size_t i = 0; i + 1 < count; i += 2) {
		CGPathMoveToPoint(path, &m, pts[i].x, pts[i].y);
		CGPathAddLineToPoint(path, &m, pts[i + 1].x, pts[i + 1].y);
	}
	stroke(path, s);
	CGPathRelease(path);
}

void CGScene::polyline(const render::Point* pts, std::size_t count, const render::Stroke& s, bool closed) {
	if (count < 2) return;
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathMoveToPoint(path, &m, pts[0].x, pts[0].y);
	for (std::size_t i = 1; i < count; i++) CGPathAddLineToPoint(path, &m, pts[i].x, pts[i].y);
	if (closed) CGPathCloseSubpath(path);
	stroke(path, s);
	CGPathRelease(path);
}

void CGScene::fillPolygon(const render::Point* pts, std::size_t count, const render::Color& c) {
	if (count < 3) return;
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathMoveToPoint(path, &m, pts[0].x, pts[0].y);
	for (std::size_t i = 1; i < count; i++) CGPathAddLineToPoint(path, &m, pts[i].x, pts[i].y);
	CGPathCloseSubpath(path);
	fill(path, c);
	CGPathRelease(path);
}

void CGScene::fillCircle(render::Point center, float radius, const render::Color& c) {
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathAddEllipseInRect(path, &m, CGRectMake(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
	fill(path, c);
	CGPathRelease(path);
}

void CGScene::strokeCircle(render::Point center, float radius, const render::Stroke& s) {
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathAddEllipseInRect(path, &m, CGRectMake(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
	stroke(path, s);
	CGPathRelease(path);
}

void CGScene::arc(render::Point center, float radius, float startDeg, float sweepDeg, const render::Stroke& s) {
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	if (std::fabs(sweepDeg) >= 360.0f) {
		// A whole turn is asked for as a circle (see SkiaScene::arc for why).
		CGPathAddEllipseInRect(path, &m, CGRectMake(center.x - radius, center.y - radius, 2 * radius, 2 * radius));
	} else {
		// Scene angles run clockwise from +Y; Core Graphics measures from +X
		// counterclockwise (in this y-up world space). Clockwise sweeps stay
		// clockwise.
		const CGFloat k = M_PI / 180.0;
		const CGFloat a0 = (90.0 - startDeg) * k, a1 = (90.0 - (startDeg + sweepDeg)) * k;
		const CGFloat sx = center.x + radius * std::cos(a0), sy = center.y + radius * std::sin(a0);
		CGPathMoveToPoint(path, &m, sx, sy);
		CGPathAddArc(path, &m, center.x, center.y, radius, a0, a1, sweepDeg > 0);
	}
	stroke(path, s);
	CGPathRelease(path);
}

void CGScene::fillRect(render::Point lo, render::Point hi, const render::Color& c) {
	const CGAffineTransform m = current();
	CGMutablePathRef path = CGPathCreateMutable();
	CGPathAddRect(path, &m, CGRectMake(std::fmin(lo.x, hi.x), std::fmin(lo.y, hi.y),
	                                   std::fabs(hi.x - lo.x), std::fabs(hi.y - lo.y)));
	fill(path, c);
	CGPathRelease(path);
}

void CGScene::text(render::Point origin, const char* utf8, float pixelHeight, const render::Color& c) {
	CTLineRef line = makeLine(utf8);
	if (line == nullptr) return;
	// `origin` is the top of the capitals (see SkiaScene::text): the baseline
	// sits a cap height below it. Glyph outlines are y-up like the world.
	const CGFloat k = pixelHeight / kGlyphUnits;
	const CGFloat cap = CTFontGetCapHeight(labelFont());
	const CGAffineTransform place = CGAffineTransformConcat(
		CGAffineTransformMake(k, 0, 0, k, origin.x, origin.y - cap * k), current());
	CGMutablePathRef path = CGPathCreateMutable();
	CFArrayRef runs = CTLineGetGlyphRuns(line);
	for (CFIndex r = 0; r < CFArrayGetCount(runs); r++) {
		CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
		CTFontRef runFont = (CTFontRef)CFDictionaryGetValue(CTRunGetAttributes(run), kCTFontAttributeName);
		const CFIndex n = CTRunGetGlyphCount(run);
		std::vector<CGGlyph> glyphs(n);
		std::vector<CGPoint> pos(n);
		CTRunGetGlyphs(run, CFRangeMake(0, n), glyphs.data());
		CTRunGetPositions(run, CFRangeMake(0, n), pos.data());
		for (CFIndex i = 0; i < n; i++) {
			CGPathRef glyph = CTFontCreatePathForGlyph(runFont, glyphs[i], nullptr);
			if (glyph == nullptr) continue;
			const CGAffineTransform t = CGAffineTransformConcat(
				CGAffineTransformMakeTranslation(pos[i].x, pos[i].y), place);
			CGPathAddPath(path, &t, glyph);
			CGPathRelease(glyph);
		}
	}
	fill(path, c);
	CGPathRelease(path);
	CFRelease(line);
}

}  // namespace mac

// Text metrics for the gate code's hit boxes, measured the way text() draws.
namespace render {

float measuredTextWidth(const char* utf8, float pixelHeight) {
	CTLineRef line = mac::makeLine(utf8);
	if (line == nullptr) return 0.0f;
	const double w = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
	CFRelease(line);
	return (float)(w * pixelHeight / mac::kGlyphUnits);
}

float measuredTextHeight(float pixelHeight) {
	CTFontRef f = mac::labelFont();
	if (f == nullptr) return 0.0f;
	return (float)((CTFontGetCapHeight(f) + CTFontGetDescent(f)) * pixelHeight / mac::kGlyphUnits);
}

}  // namespace render
}  // namespace cl
