// Send Feedback, as the Mac app's (Feedback.swift, FeedbackView.swift) and
// the Windows app's: what the tester writes, tags (the app suggests some from
// the words), how much it matters, their name and, if they like, an email,
// plus screenshots of the window, pictures, and the circuit. The version, the
// computer and the moment it was sent from are added by themselves. It goes
// to cedarlogic.netlify.app (with curl, which every desktop has), which
// tells the developer.

#ifndef CL_LINUX_FEEDBACK_H
#define CL_LINUX_FEEDBACK_H

class CircuitWindow;

namespace feedback {
// `demo` (--show feedback:N) pictures a state without sending: 1 sending,
// 2 sent, 3 failed, 4 over the allowance (Request More), 5 no summary.
void show(CircuitWindow* window, int demo = 0);
// --feedback-probe: asks the site with a wrong key (it should say 403) and
// sends nothing. The HTTP status, or 0 when it couldn't be reached.
int probe();
// --feedback-check: the client's checks against the stand-in server at
// CL_FEEDBACK_URL (mac/Tools/feedback-check/server.py). 0 when all pass.
int check();
}

#endif  // CL_LINUX_FEEDBACK_H
