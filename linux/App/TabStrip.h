// The tabs over the canvas, drawn as the Mac app's: a card for each page
// (the one in front lifted, with a shadow), a dot that's green while the
// circuit runs, a close button under the pointer, and + at the end. Drag a
// tab to move it; double-click to rename; middle-click to close. (The pages
// themselves stay in a GtkNotebook with its own tabs hidden.)

#ifndef CL_LINUX_TABSTRIP_H
#define CL_LINUX_TABSTRIP_H

#include "Drawn.h"
#include <vector>

class CircuitWindow;

class TabStrip : public Drawn {
public:
	explicit TabStrip(CircuitWindow* window);
	static float stripHeight() { return 36; }   // points

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;

private:
	CircuitWindow* win;
	std::vector<RectF> cards;
	RectF plus{};
	int hot = -1, hotClose = -1, pressed = -1;
	bool plusHot = false;
	bool dragging = false;
	float pressX = 0, grabOffset = 0, dragX = 0;

	void layout(float w);
	RectF closeRect(int i) const;
	int tabAt(float x, float y) const;
};

#endif  // CL_LINUX_TABSTRIP_H
