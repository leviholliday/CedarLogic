// A side's tabs over the canvas, drawn as the Mac app's: a card for each
// page (the one in front lifted, with a shadow), a dot that's green while
// the circuit runs, a close button under the pointer, and + at the end. Drag
// a tab along the strip to move it, down onto the canvas to split the view,
// or onto the other side of a split to move it there. Double-click a tab to
// rename it in place, the empty strip for a new tab; middle-click closes.
// Each side of a split has its own strip: the side you're in has an accent
// rail under it, the other steps back. In focus mode the strips are the
// window's top row, with its minimize, maximize and close.

#ifndef CL_WINDOWS_TABSTRIP_H
#define CL_WINDOWS_TABSTRIP_H

#include "Drawn.h"
#include <cstdint>
#include <vector>

class CircuitWindow;

class TabStrip : public Drawn {
public:
	TabStrip(CircuitWindow* window, HWND parent, int pane);
	~TabStrip() override;
	static float stripHeight() { return 36; }   // points

	// Focus mode: the strip is the window's top row (the one at the right
	// has the window's buttons).
	void setTitleRow(bool on);
	// Rename a tab in place: a text box on its card. Enter keeps the name,
	// Escape puts the old one back, clicking away keeps it.
	void beginRename(int page);
	void commitRename(bool keep, bool refocus);
	// In focus mode, for the frame: where the maximize button is (window
	// client pixels; empty when it isn't shown), and the pointer over it.
	RECT maximizeRect() const;
	void setMaximizeHot(bool hot, bool pressed);
	// For --click-test: the middle of + or of a window button, in the
	// strip's pixels. False when it isn't shown.
	enum { kPlusButton = 1, kMinimizeButton, kCloseButton };
	bool buttonPoint(int which, POINT& p);

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	void mouseUp(int button, float x, float y) override;
	void captureLost() override;
	LRESULT hitTest(float x, float y) override;
	LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
	enum Caption { CapMin, CapMax, CapClose, CapCount };
	CircuitWindow* win;
	int pane;
	std::vector<int> pages;              // this side's pages, in order
	std::vector<D2D1_RECT_F> cards;      // one for each
	D2D1_RECT_F plus{};
	D2D1_RECT_F caps[CapCount] = {};     // the window's buttons (title row)
	bool titleRow = false, capsShown = false;
	int hot = -1, hotClose = -1, pressed = -1, hotCap = -1, pressedCap = -1;
	bool plusHot = false, maxHot = false, maxPressed = false;
	bool dragging = false;
	float pressX = 0, pressY = 0, grabOffset = 0, dragX = 0;
	// Renaming in place.
	HWND edit = nullptr;
	HFONT editFont = nullptr;
	uint64_t renamingKey = 0;

	void layout(float w);
	void placeEdit();
	D2D1_RECT_F closeRect(int k) const;
	int tabAt(float x, float y) const;
	int captionAt(float x, float y) const;
	bool activeSide() const;
	void endDrag();
	static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};

#endif  // CL_WINDOWS_TABSTRIP_H
