/*****************************************************************************
   Project: CEDAR Logic Simulator
   DcRasterPaint: show the processor fallback's frame without OpenGL.
*****************************************************************************/

#ifndef DCRASTERPAINT_H_
#define DCRASTERPAINT_H_

#ifdef __WXGTK__

#include <wx/image.h>

class wxDC;

// Held across a GL canvas's paint. If the drawing engine has fallen back to
// the processor, the finished frame lands here instead of going through
// glDrawPixels, and paint() puts it on screen with the window's own drawing --
// the same drawing the palette and dialogs already use. That keeps the canvas
// visible on drivers where OpenGL can neither run the engine nor take a CPU
// image: the Raspberry Pi's V3D (OpenGL 3.1) is the one that showed it.
//
// While nothing is captured (the engine is on the GPU) this does nothing, and
// the canvas presents with SwapBuffers as before.
class DcRasterPaint {
public:
	DcRasterPaint();
	~DcRasterPaint();
	DcRasterPaint(const DcRasterPaint&) = delete;
	DcRasterPaint& operator=(const DcRasterPaint&) = delete;

	// Whether a CPU frame arrived during this paint.
	bool captured() const { return frame.IsOk(); }
	// Draw it at the window's origin. contentScale is the canvas's
	// GetContentScaleFactor(): the frame is in device pixels.
	void paint(wxDC& dc, double contentScale) const;

private:
	wxImage frame;
};

#endif // __WXGTK__

#endif
