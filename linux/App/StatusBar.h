// The status bar along the bottom, as the Mac app's (CLStatusBar): notes on
// the left (Saved, Copied...), fading in and out again after a while, and --
// with Settings > General > Status bar on -- the zoom, where the pointer is,
// and the page's counts on the right. Off, it slides in for a note and away
// once the note has gone.

#ifndef CL_LINUX_STATUSBAR_H
#define CL_LINUX_STATUSBAR_H

#include "Anim.h"
#include "Drawn.h"
#include <string>

class CircuitWindow;

class StatusBar : public Drawn {
public:
	explicit StatusBar(CircuitWindow* window);
	static float barHeight() { return 22; }
	// The revealer it slides in and out in.
	GtkWidget* outer() const { return revealer; }
	void note(const std::string& text);
	void update();   // the readout changed
	void settingsChanged();
	// From the window's clock: time a note out.
	void tick();

protected:
	void paint(cairo_t* cr, float w, float h) override;
	bool animating() override { return fade.active(); }

private:
	CircuitWindow* win;
	GtkWidget* revealer;
	std::string message, shownMessage;
	double messageAt = 0;
	anim::Tween fade;
	void showOrHide();
};

#endif  // CL_LINUX_STATUSBAR_H
