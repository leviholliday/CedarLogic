/*****************************************************************************
   Project: CEDAR Logic Simulator
   Feedback: Help > Send Feedback. A window for saying what's working, what's
   broken and what you'd love to see -- with tags, how much it matters, a
   screenshot of the app, and who's writing -- sent to the feedback site
   (cedarlogic.netlify.app, the cedarlogic-site repo), where Levi reads it.

   The same form as the native Mac app's (mac/App/Feedback*.swift), in wx for
   Windows and Linux. What it sends is described in the site's
   netlify/functions/feedback.mjs: the item as JSON, then each attachment in
   pieces of at most 3 MB, then "complete". The requests go through curl
   (Windows 10 and later ship it; Linux and macOS always have it), one process
   per request, so nothing here blocks the window.

   CL_FEEDBACK_URL and CL_FEEDBACK_KEY in the environment point it somewhere
   else, such as `netlify dev` while working on the site.
*****************************************************************************/

#ifndef FEEDBACK_H_
#define FEEDBACK_H_

#include <wx/string.h>

class MainFrame;
class wxImage;
class wxTopLevelWindow;

// Open the Send Feedback window, or bring it forward if it is open.
void ShowFeedback(MainFrame* frame);

// A picture of the app's window as it looks now, canvas included. False if
// this platform would not give one.
bool CaptureAppWindow(MainFrame* frame, wxImage& out);

// For --render-windows: the window, open on the form (dark or light follows
// the app), and closing it again.
wxTopLevelWindow* FeedbackWindowForCapture(MainFrame* frame);
void DismissFeedback();

// --feedback-probe: ask the site for something it has to refuse, to show the
// sending works on this machine (curl found, TLS, an answer read back)
// without sending any feedback. Ends the app when it has its answer: exit
// code 0 when it worked. Call once the main loop is running.
void StartFeedbackProbe();

// --feedback-selftest <title>: send a real item with a screenshot of the
// window, say on stderr how it went, and end the app (0 when it arrived).
// CL_FEEDBACK_KEEP=<path> keeps a copy of the screenshot.
void StartFeedbackSelfTest(MainFrame* frame, const wxString& title);

#endif
