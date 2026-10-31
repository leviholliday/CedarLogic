// Send Feedback, as the Mac app's (Feedback.swift, FeedbackView.swift): what
// the tester writes, tags (the app suggests some from the words), how much
// it matters, their name and, if they like, an email, plus screenshots of
// the window, pictures, and the circuit. The version, the PC and the moment
// it was sent from are added by themselves. It goes to cedarlogic.netlify.app,
// which tells the developer.

#ifndef CL_WINDOWS_FEEDBACK_H
#define CL_WINDOWS_FEEDBACK_H

#include <functional>
#include <string>

class CircuitWindow;

namespace feedback {
void show(CircuitWindow* window);
// --feedback-probe: asks the site with a wrong key (it should say 403) and
// sends nothing. The HTTP status, or 0 when it couldn't be reached.
int probe();
// A recording for Send Feedback is going (FeedbackCapture.h): closing a
// window or quitting stops it first (kept) instead.
bool recording();
void stopRecording();
// --feedback-capture-test: screenshot and recording through the form, each
// step said as PASS or FAIL, pictures and the video written to `dir`.
// Returns how many failed.
int captureSelfTest(CircuitWindow* window, const std::string& dir, std::function<void(const std::string&)> say);
// --dialog feedback-sending, feedback-failed, feedback-sent (CI's pictures):
// the form as it looks mid-send or after a failed one (nothing is sent), or
// the thank-you card. 1 sending, 2 failed, 3 sent.
void showSample(CircuitWindow* window, int state);
}

#endif  // CL_WINDOWS_FEEDBACK_H
