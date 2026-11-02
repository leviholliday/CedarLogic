// A question or a note as a card over the window -- the Mac's alerts, drawn
// the way the quit question is: the app's icon (with a badge for a warning
// or a problem), a bold line, the explanation wrapped under it, an optional
// text field, and pill buttons showing their keys (↩ for the default, esc
// for the way out). It arrives with the Mac's little drop and fade.
//
// showMessage, askYesNo and askText (App.h, Dialogs.h) all come here.

#ifndef CL_LINUX_ALERT_H
#define CL_LINUX_ALERT_H

#include "App.h"
#include <string>
#include <vector>

struct AlertButton {
	std::string label;
	int answer = 0;
	int kind = 0;          // 0 plain, 1 the default (neon), 2 destructive (red)
	bool apart = false;    // on the left, away from the others (Always Quit, Throw Away)
};

struct Alert {
	std::string title;     // the window's, for the window list
	std::string heading, text;
	int badge = 0;         // 0 none, 1 information, 2 warning, 3 a problem
	std::vector<AlertButton> buttons;   // right to left
	bool field = false;    // a line of text to type
	std::string value, placeholder;     // the field's text, in and out
	int escape = 0;        // the answer Esc (or closing the window) gives
	int enter = 1;         // and Enter's
};

// Runs modally over parent; returns the button's answer.
int runAlert(GtkWindow* parent, Alert& a);

// Yes and No by other names ("Delete" and "Cancel"); destructive paints the
// yes button red, as the Mac does for a delete.
bool askConfirm(GtkWindow* parent, const std::string& heading, const std::string& text, const std::string& yes,
                const std::string& no = "Cancel", bool destructive = false);

#endif  // CL_LINUX_ALERT_H
