// The tabs over the canvas, drawn as the Mac app's: a card for each page
// (the one in front lifted, with a shadow), a dot that's green while the
// circuit runs, a close button under the pointer, and + at the end. Drag a
// tab to move it; double-click to rename; middle-click to close.

#ifndef CL_WINDOWS_TABSTRIP_H
#define CL_WINDOWS_TABSTRIP_H

#include "Drawn.h"
#include <vector>

class CircuitWindow;

class TabStrip : public Drawn {
public:
	TabStrip(CircuitWindow* window, HWND parent);
	static float stripHeight() { return 36; }   // points

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	void mouseUp(int button, float x, float y) override;
	void captureLost() override;

private:
	CircuitWindow* win;
	std::vector<D2D1_RECT_F> cards;
	D2D1_RECT_F plus{};
	int hot = -1, hotClose = -1, pressed = -1;
	bool plusHot = false;
	bool dragging = false;
	float pressX = 0, grabOffset = 0, dragX = 0;

	void layout(float w);
	D2D1_RECT_F closeRect(int i) const;
	int tabAt(float x, float y) const;
};

#endif  // CL_WINDOWS_TABSTRIP_H
