/*****************************************************************************
   Project: CEDAR Logic Simulator
   Welcome: the first-run flow, the setup walkthrough, and the guided tour.

   Three things that share a look and a purpose -- getting someone from
   "what is this" to "I built a circuit" without reading anything.
*****************************************************************************/

#ifndef WELCOME_H_
#define WELCOME_H_

class MainFrame;

// The first-run flow: hello, then set the app up, then offer the tour.
// `startAtSetup` skips the hello page, for when it is opened from the Help
// menu by someone who already knows what the app is.
void ShowWelcome(MainFrame* frame, bool startAtSetup = false);

// The guided tour: a small panel that sits over the canvas and walks through
// building a working circuit, watching what you do and moving on by itself.
void StartTutorial(MainFrame* frame);
void StopTutorial();

// Test hook (headless --render-ui): every welcome page and tour card, light
// and dark, written as PNGs into `dir`.
class wxString;
bool RenderWelcomeSnapshots(MainFrame* frame, const wxString& dir);

#endif
