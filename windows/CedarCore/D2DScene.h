// D2DScene -- the render Scene drawn with Direct2D, for the native Windows
// front end: the Windows twin of the Mac's CGScene and Linux's CairoScene. The
// same gate and wire drawing code that feeds Skia in the wx app draws through
// this, so a circuit looks the same everywhere. Direct2D and DirectWrite ship
// with Windows, so nothing here is an extra download.
//
// Coordinates: the caller hands over a render target whose transform makes its
// units physical pixels (the wx app's "device" space), so Stroke widths, which
// are in device pixels, come out exactly as they do there. Points are
// transformed in software and the target's own transform is left alone.

#ifndef CL_WINDOWS_D2DSCENE_H
#define CL_WINDOWS_D2DSCENE_H

#include "render/Scene.h"
#include <vector>

// The Direct2D types, named without the Windows headers: this header reaches
// engine code that says `using namespace std`, whose std::byte the Windows
// headers' `byte` would clash with.
struct ID2D1RenderTarget;
struct ID2D1Factory;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct ID2D1Geometry;
struct ID2D1PathGeometry;
struct IDWriteFactory;

namespace cl {
namespace d2d {

// Direct2D's D2D1_MATRIX_3X2_F and D2D1_POINT_2F, laid out the same.
struct Matrix { float _11, _12, _21, _22, _31, _32; };
struct Point2 { float x, y; };

class D2DScene : public render::Scene {
public:
	explicit D2DScene(ID2D1RenderTarget* rt);
	~D2DScene() override;

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

	// Shapes drawn inside a pushTransform (a gate's body, its pins, its
	// bubbles, in the gate's own coordinates) and every label's glyph outline
	// are built as Direct2D geometry once and kept, then carried into place
	// with a transform each frame, rather than built again every frame
	// (windows/DRAG-DIAGNOSIS.md, hypothesis 3). Wires, drawn in world
	// coordinates that change as parts move, are built each time as before.
	// On unless CEDARLOGIC_NO_GEOMETRY_CACHE=1; --bench-draw times both.
	static bool cacheGeometry;
	static std::size_t cachedGeometries();   // how many are kept now
	static void clearGeometryCache();

private:
	ID2D1RenderTarget* rt;
	ID2D1Factory* factory = nullptr;
	ID2D1SolidColorBrush* brush = nullptr;
	Matrix viewport;
	std::vector<Matrix> stack;   // world transforms, outermost first

	render::Color mapped(const render::Color& c) const;
	Matrix current() const;
	ID2D1SolidColorBrush* paint(const render::Color& c);
	ID2D1StrokeStyle* strokeStyle(const render::Stroke& s);
	void strokeGeometry(ID2D1Geometry* g, const render::Stroke& s);
	void fillGeometry(ID2D1Geometry* g, const render::Color& c);
	// A path through points already in device pixels.
	ID2D1PathGeometry* path(const std::vector<Point2>& pts, bool closed, bool filled);
	// A circle in world space, carried into device pixels.
	ID2D1Geometry* circle(const Matrix& m, render::Point center, float radius);
	// The kept shape through these local points, carried by `m` into device
	// pixels (a new reference the caller releases); null when it isn't kept
	// (no local transform, caching off), so the caller builds it as before.
	ID2D1Geometry* keptShape(const Matrix& m, const render::Point* pts, std::size_t count, bool closed, bool filled);
};

// The render target's transform before DevicePixels scaled it.
struct SavedTransform { Matrix m; };
SavedTransform scaleTransform(ID2D1RenderTarget* rt, double factor);
void restoreTransform(ID2D1RenderTarget* rt, const SavedTransform& saved);

// The shared factories (created on first use; null if Windows can't make them).
ID2D1Factory* factory();
IDWriteFactory* writeFactory();

}  // namespace d2d
}  // namespace cl

#endif  // CL_WINDOWS_D2DSCENE_H
