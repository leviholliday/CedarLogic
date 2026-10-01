// Send Feedback, as the Mac app's (Feedback.swift, FeedbackView.swift): what
// the tester writes, tags (the app suggests some from the words), how much
// it matters, their name and, if they like, an email, plus screenshots of
// the window, pictures, and the circuit. The version, the PC and the moment
// it was sent from are added by themselves. It goes to cedarlogic.netlify.app,
// which tells the developer.

#ifndef CL_WINDOWS_FEEDBACK_H
#define CL_WINDOWS_FEEDBACK_H

class CircuitWindow;

namespace feedback {
void show(CircuitWindow* window);
// --feedback-probe: asks the site with a wrong key (it should say 403) and
// sends nothing. The HTTP status, or 0 when it couldn't be reached.
int probe();
}

#endif  // CL_WINDOWS_FEEDBACK_H
