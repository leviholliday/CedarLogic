// The status bar along the bottom, as the Mac app's (CLStatusBar) and the
// Linux app's: notes on the left (Saved, Copied...), rising in and fading out
// again after a while, and -- with View > Status Bar on, as it is to begin
// with -- the page's counts, where the pointer is and the zoom on the right.
// Off, it slides up for a note and away once the note has gone. Drawn in the
// tab strip's colours, light or dark.

#ifndef CL_WINDOWS_STATUSBAR_H
#define CL_WINDOWS_STATUSBAR_H

#include "Drawn.h"
#include <string>

class CircuitWindow;

class StatusBar : public Drawn {
public:
	StatusBar(CircuitWindow* window, HWND parent);
	static float barHeight() { return 22; }
	// How much of it shows, 0..1: the window lays it out that tall.
	double shown() const;
	void note(const std::string& text);
	void update() { redraw(); }   // the readout changed
	// From the window's clock: times a note out. True while it slides (the
	// window lays itself out again).
	bool tick();

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;

private:
	// A value easing (out) towards where it was sent.
	struct Tween {
		double from = 0, to = 0, at = -10, time = 0.2;
		double value() const;
		bool active() const;
		void go(double target, double seconds);
		void set(double v) { from = to = v; at = -10; }
	};
	CircuitWindow* win;
	std::string message, shownMessage;
	double messageAt = 0;
	Tween fade, slide;
	bool sliding = false;   // at the last tick
};

#endif  // CL_WINDOWS_STATUSBAR_H
