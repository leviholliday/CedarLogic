// CGScene -- the render Scene drawn with Core Graphics, for the native Mac
// front end. The same gate and wire drawing code that feeds Skia in the wx app
// draws through this, so a circuit looks the same in both.
//
// Coordinates: the caller hands over a context whose units are physical pixels
// (the wx app's "device" space), so Stroke widths, which are in device pixels,
// come out exactly as they do there. Points are transformed in software and
// the context's own matrix is left alone.

#ifndef CL_MAC_CGSCENE_H
#define CL_MAC_CGSCENE_H

#include "render/Scene.h"
#include <CoreGraphics/CoreGraphics.h>
#include <vector>

namespace cl {
namespace mac {

class CGScene : public render::Scene {
public:
	explicit CGScene(CGContextRef ctx);

	void setViewport(const render::Transform& worldToDevice) override;
	void pushTransform(const render::Transform& local) override;
	void popTransform() override;
	void lines(const render::Point* pts, std::size_t count, const render::Stroke&) override;
	void polyline(const render::Point* pts, std::size_t count, const render::Stroke&, bool closed) override;
	void fillPolygon(const render::Point* pts, std::size_t count, const render::Color&) override;
	void fillCircle(render::Point center, float radius, const render::Color&) override;
	void strokeCircle(render::Point center, float radius, const render::Stroke&) override;
	void arc(render::Point center, float radius, float startDeg, float sweepDeg, const render::Stroke&) override;
	void fillRect(render::Point lo, render::Point hi, const render::Color&) override;
	void text(render::Point origin, const char* utf8, float pixelHeight, const render::Color&) override;

private:
	CGContextRef ctx;
	CGAffineTransform viewport = CGAffineTransformIdentity;
	std::vector<CGAffineTransform> stack;   // world transforms, innermost last

	CGAffineTransform current() const;
	void stroke(CGPathRef path, const render::Stroke& s);
	void fill(CGPathRef path, const render::Color& c);
};

}  // namespace mac
}  // namespace cl

#endif  // CL_MAC_CGSCENE_H
