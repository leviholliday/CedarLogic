// CairoScene -- the render Scene drawn with Cairo, for the native Linux front
// end: the Linux twin of the Mac's CGScene. The same gate and wire drawing
// code that feeds Skia in the wx app draws through this, so a circuit looks
// the same everywhere, and it draws on the processor: no OpenGL anywhere.
//
// Coordinates: the caller hands over a context whose units are physical pixels
// (the wx app's "device" space), so Stroke widths, which are in device pixels,
// come out exactly as they do there. Points are transformed in software and
// the context's own matrix is left alone.

#ifndef CL_LINUX_CAIROSCENE_H
#define CL_LINUX_CAIROSCENE_H

#include "render/Scene.h"
#include <cairo.h>
#include <vector>

namespace cl {
namespace cairo {

class CairoScene : public render::Scene {
public:
	explicit CairoScene(cairo_t* ctx);

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

	// A wire colour swapped on the way to the screen: the dark canvas's low
	// wire grey (guiWire's 0.35 floor) reads too close to the grid, so the app
	// can paint it in another colour. Only while wires are drawn.
	static bool remapLowWire;
	static render::Color lowWire;
	bool wiresPhase = false;

private:
	cairo_t* ctx;
	render::Color mapped(const render::Color& c) const;
	cairo_matrix_t viewport;
	std::vector<cairo_matrix_t> stack;   // world transforms, outermost first

	cairo_matrix_t current() const;
	void moveTo(const cairo_matrix_t& m, const render::Point& p);
	void lineTo(const cairo_matrix_t& m, const render::Point& p);
	void addCircle(const cairo_matrix_t& m, render::Point center, float radius);
	void stroke(const render::Stroke& s);
	void fill(const render::Color& c);
};

// The label face, for anyone else who draws text the way labels are drawn
// (null when no font could be found).
cairo_font_face_t* labelFace();

}  // namespace cairo
}  // namespace cl

#endif  // CL_LINUX_CAIROSCENE_H
