// NullScene -- a render Scene that draws nothing, for headless builds of the
// engine (CL_HEADLESS): the tests and fuzzers that run on any machine, with no
// Core Graphics, Cairo or Direct2D to draw with. Every drawing call is accepted
// and dropped, so the code that walks a circuit to draw it still runs.

#ifndef CL_NULL_SCENE_H
#define CL_NULL_SCENE_H

#include "render/Scene.h"

namespace cl {
namespace headless {

class NullScene : public render::Scene {
public:
	explicit NullScene(CLContext) {}

	void setViewport(const render::Transform&) override {}
	void pushTransform(const render::Transform&) override {}
	void popTransform() override {}
	void lines(const render::Point*, std::size_t, const render::Stroke&) override {}
	void polyline(const render::Point*, std::size_t, const render::Stroke&, bool) override {}
	void fillPolygon(const render::Point*, std::size_t, const render::Color&) override {}
	void fillCircle(render::Point, float, const render::Color&) override {}
	void strokeCircle(render::Point, float, const render::Stroke&) override {}
	void arc(render::Point, float, float, float, const render::Stroke&) override {}
	void fillRect(render::Point, render::Point, const render::Color&) override {}
	void text(render::Point, const char*, float, const render::Color&) override {}

	// What the platform scenes offer beyond Scene (see CGScene.h).
	static inline bool remapLowWire = false;
	static inline render::Color lowWire;
	bool wiresPhase = false;
};

}  // namespace headless
}  // namespace cl

#endif  // CL_NULL_SCENE_H
