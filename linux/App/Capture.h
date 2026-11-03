// Recordings of the app for Send Feedback, as the Mac's FeedbackCapture:
// made by the app drawing its own window, so they work the same on X11 and
// Wayland (where an app can't read the screen), need no permission, and
// have nothing but CedarLogic in them.
//
// A recording is the window drawn a few times a second (8, fewer when a
// frame takes long, as on a Pi with a big circuit), with the pointer drawn
// in and a ring where it clicks. A frame the same as the one before isn't
// stored again, so a still window costs nothing.
//
// When the computer has ffmpeg, the frames go into a QuickTime movie as JPEG
// pictures as they come (cheap enough for a Pi), and ffmpeg makes it an
// H.264 MP4 at the end (MPEG-4 if that ffmpeg has no H.264); the MP4 plays
// in any browser. Without ffmpeg (Ubuntu doesn't ship it), it's an animated
// PNG, each frame only the rectangle that changed: browsers play it too,
// and the site takes it as a picture. It stops by itself after a minute (or
// about 40 MB of movie, 11 MB of PNG), and a bar over the canvas says it's
// recording, with Stop and a cancel (the bar isn't in the recording).

#ifndef CL_LINUX_CAPTURE_H
#define CL_LINUX_CAPTURE_H

#include <gtk/gtk.h>
#include <functional>
#include <string>

class CircuitWindow;

namespace capture {

// The window as it looks (a recording's frame), title bar included and its shadow left out, at
// the screen's sharpness (no wider than maxWidth pixels when that's > 0).
// nullptr if it has no size. The caller destroys it.
cairo_surface_t* window(GtkWindow* w, int maxWidth = 0);
// A picture of a surface, for a thumbnail (no image loader needed).
GdkPixbuf* pixbuf(cairo_surface_t* s, int maxW, int maxH);

struct Recording {
	std::string file;   // empty: cancelled, or it failed (why says why)
	std::string type;   // "video/mp4", "image/png" (animated) or "video/quicktime"
	std::string ext;    // "mp4", "png" or "mov"
	double seconds = 0;
	int frames = 0;
	GdkPixbuf* thumb = nullptr;   // the first frame (the receiver owns it)
	std::string why;
};

// Starts recording `window` into `folder`; `done` runs once, on the main
// loop, when it's stopped (Stop, the cancel, the limits, the window
// closing). False (and `done` never runs) if it couldn't start.
bool record(CircuitWindow* window, const std::string& folder, std::function<void(const Recording&)> done);
bool recording();
// Stop now: keep it (as Stop does) or throw it away.
void stop(bool keep);

// For CI (--capture-test <folder>): a screenshot and a short recording of
// the window, checked and reported; the exit code.
int selfTest(CircuitWindow* window, const std::string& folder);

}  // namespace capture

#endif  // CL_LINUX_CAPTURE_H
