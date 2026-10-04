// A question or a note as a card over the window -- the Mac's alerts, as the
// Linux app draws them: the app's icon (with a badge for a warning or a
// problem), a bold line, the explanation wrapped under it, an optional text
// field, and pill buttons showing their keys (↵ for the default, esc for the
// way out). The default is in the brand's green, a button that throws work
// away in red. Enter, Escape, Tab (and the arrows) work it from the keyboard;
// its window waits for an answer, as a dialog does.
//
// showMessage, askYesNo (App.h) and askText (Dialogs.h) all come here.

#ifndef CL_WINDOWS_ALERT_H
#define CL_WINDOWS_ALERT_H

#include "App.h"
#include <string>
#include <vector>

struct AlertButton {
	std::string label;
	int answer = 0;
	int kind = 0;          // 0 plain, 1 the default (green), 2 destructive (red)
	bool apart = false;    // on the left, away from the others (Throw Away, Put Them Back)
};

struct Alert {
	std::string title;     // the window's, for Alt+Tab and the taskbar
	std::string heading, text;
	int badge = 0;         // 0 none, 1 information, 2 warning, 3 a problem
	std::vector<AlertButton> buttons;   // right to left
	bool field = false;    // a line of text to type
	std::string value, placeholder;     // the field's text, in and out
	int escape = 0;        // the answer Escape (or closing it) gives
	int enter = 1;         // and Enter's
};

// Waits, over parent's window, for a button; returns its answer.
int runAlert(HWND parent, Alert& a);

// Yes and No by other names ("Delete" and "Cancel"); destructive paints the
// yes button red, as the Mac does for a delete.
bool askConfirm(HWND parent, const std::string& heading, const std::string& text, const std::string& yes,
                const std::string& no = "Cancel", bool destructive = false);

#endif  // CL_WINDOWS_ALERT_H
