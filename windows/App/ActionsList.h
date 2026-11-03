// The Customize list (the Mac's ActionsCustomizeList): every action, a tick
// each, in the order they show on the rail, rows dragged up or down to
// reorder (here or on the rail itself: the same order), a search field that
// filters by name, menu and shortcut -- every word typed has to match, "ctrl
// e" too -- with dragging off while it's searching, Escape clearing it, and
// Reset to Default. On Settings > Actions and in the rail's Customize sheet,
// both Forms (Dialogs.h).

#ifndef CL_WINDOWS_ACTIONSLIST_H
#define CL_WINDOWS_ACTIONSLIST_H

#include "Dialogs.h"
#include <string>
#include <vector>

struct ActionsList {
	Form* form = nullptr;
	int search = -1, reset = -1, list = -1, footer = -1;
	std::string query;
	bool disabled = false;     // Settings > Actions with the actions hidden
	// Scrolling, the row under the pointer or the keyboard, and a drag.
	float scroll = 0, contentH = 0, viewH = 0;
	int hot = -1;
	int pressRow = -1;
	float pressY = 0, dragY = 0;
	bool dragging = false;
	struct Row { std::string id; D2D1_RECT_F rect, box; };
	std::vector<Row> rows;      // as last drawn

	// The search (with Reset to Default beside it), the list `height` points
	// tall, and the line under it. Call while building the Form.
	void add(Form& f, int height);
	// From the Form's onInit, onChange and onWheel; Escape (onCancel): true
	// when it cleared the search (so the Form stays open).
	void init(Form& f);
	bool changed(Form& f, int field);
	bool wheel(int field, int delta);
	bool clearSearch(Form& f);
	void setDisabled(bool off);
	void refresh();

	// For the list's window procedure.
	bool searching() const;
	std::vector<std::string> visible() const;
	int rowAt(float x, float y) const;
	int dropIndex() const;
	void toggle(int row);
	void moveBy(int row, int delta);
	void choose(int row);
	void after();               // anything changed: saved, every window, the footer
	void paint(ID2D1RenderTarget* rt, float w, float h);
};

#endif  // CL_WINDOWS_ACTIONSLIST_H
