// Recordings of a CedarLogic window for Send Feedback (the Mac's
// FeedbackCapture.swift): only that window, never the rest of the screen,
// and nothing to give permission for. (Send Feedback's screenshot is still
// CircuitWindow::screenshot; window() here makes the recording's frames,
// the recording bar's picture and CI's picture of an open menu.)
//
// A frame is Windows drawing the window into a bitmap (PrintWindow, with
// what Direct2D drew on it), cut to the window you see (not the invisible
// edges Windows resizes it by). Some graphics drivers hand back an empty
// picture that way; then the window is brought in front and copied from the
// screen instead.
//
// A recording is those frames 15 times a second, with the pointer and a
// ring where it clicks drawn in, made into an MP4 (H.264) by Media
// Foundation on a thread of its own: at most 1600 pixels wide and a minute
// long, a few megabytes. While it records, a small dark bar over the top of
// the window says so, counts the time, and has Stop and a ✕ to throw it
// away; it isn't in the recording. Windows 10 1903 and later, Intel/AMD and
// ARM. Media Foundation is loaded when a recording starts (Windows "N"
// editions without the Media Feature Pack haven't got it: the app still
// runs, and Record says what's missing).

#ifndef CL_WINDOWS_FEEDBACK_CAPTURE_H
#define CL_WINDOWS_FEEDBACK_CAPTURE_H

#include "App.h"
#include <wincodec.h>

#include <functional>
#include <string>

namespace capture {

// The window as it is on screen, ready to draw (premultiplied BGRA). Null,
// with `why` in plain words, if it couldn't be taken.
IWICBitmap* window(HWND h, std::string* why = nullptr);
// The window with a popup over it (an open menu), for --menu's picture.
IWICBitmap* windowAndPopup(HWND h, HWND popup);
bool savePng(IWICBitmapSource* bmp, const std::string& file);

const double kMaxSeconds = 60;

// What a recording ends with: its file ("" if it was thrown away or didn't
// work), how long it is, a PNG of its first frame for the thumbnail, and
// what went wrong (plain words; "" when nothing did).
using Finished = std::function<void(const std::string& file, double seconds, const std::string& firstFrame, const std::string& problem)>;
// Starts recording `window` into `file` (an .mp4). False, with `why`, if it
// couldn't start; otherwise `done` is called once it ends (Stop, the ✕, a
// minute, or stopRecording), on this thread.
bool startRecording(HWND window, const std::string& file, Finished done, std::string& why);
bool recording();
double recordingSeconds();
// Ends it now: kept (as Stop) or thrown away (as the ✕).
void stopRecording(bool keep);
// The bar shown while recording (for its picture in CI), or null.
HWND recordingBar();

// --feedback-capture-test's look at a finished recording: its length in
// seconds (0 if it can't be read) and its first frame as a PNG.
double probeVideo(const std::string& file, const std::string& framePng);

}  // namespace capture

#endif  // CL_WINDOWS_FEEDBACK_CAPTURE_H
