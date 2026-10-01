// Drawn: a child window that draws itself with Direct2D, in points, and
// hands its pointer to the subclass in points too -- what the toolbar, the
// tab strip and the side panel's header are made of. Hover tracking, capture
// while a button is down, and tooltips per rectangle come with it.

#ifndef CL_WINDOWS_DRAWN_H
#define CL_WINDOWS_DRAWN_H

#include "App.h"
#include <string>
#include <vector>

class Drawn {
public:
	virtual ~Drawn();
	HWND widget() const { return hwnd; }
	void redraw() { InvalidateRect(hwnd, nullptr, FALSE); }
	double width() const;    // points
	double height() const;
	double scale() const { return dpiOf(hwnd) / 96.0; }

protected:
	// Make the window (call from the subclass's constructor).
	void create(HWND parent, DWORD style = WS_CHILD | WS_VISIBLE);

	virtual void paint(ID2D1RenderTarget* rt, float w, float h) = 0;
	virtual void mouseMove(float, float) {}
	virtual void mouseLeave() {}
	virtual void mouseDown(int /*button*/, float, float, bool /*doubleClick*/) {}
	virtual void mouseUp(int /*button*/, float, float) {}
	virtual void wheel(int /*delta*/, float, float) {}
	virtual void captureLost() {}
	// What the frame should make of a point here: HTCLIENT (ours), or
	// HTTRANSPARENT to let the window under it (the frame) decide.
	virtual LRESULT hitTest(float, float) { return HTCLIENT; }
	virtual LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) { (void)msg; (void)wp; (void)lp; handled = false; return 0; }

	// Tooltips: replace the set, each a rectangle in points.
	struct Tip { D2D1_RECT_F rect; std::string text; };
	void setTips(const std::vector<Tip>& tips);
	void cursorPoint(float& x, float& y) const;

	HWND hwnd = nullptr;
	bool pointerIn = false;

private:
	WindowSurface surface;
	HWND tooltip = nullptr;
	std::vector<std::wstring> tipTexts;
	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

#endif  // CL_WINDOWS_DRAWN_H
