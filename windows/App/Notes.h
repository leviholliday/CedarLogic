// Notes: the student's own notes for a circuit (the website's
// docs/DRAWING-NOTES.md 4.14, the Mac's Notes.swift), plain text saved inside
// it (cl_notes) and synced with it. A pane at the right of the window (View >
// Show Notes, Ctrl+Alt+N, or the Notes button by Draw), in Simulation View
// too. Not one of the circuit's undo steps: the text box has its own undo,
// so Ctrl+Z in the notes undoes typing and Ctrl+Z on the canvas undoes the
// circuit. 20,000 characters at most, with a count shown near the end.

#ifndef CL_WINDOWS_NOTES_H
#define CL_WINDOWS_NOTES_H

#include "Drawn.h"
#include <string>

class CircuitWindow;

class NotesPanel : public Drawn {
public:
	NotesPanel(CircuitWindow* window, HWND parent);
	static float panelWidth() { return 280; }   // points
	static const int kMax = 20000, kCounterFrom = 18000;

	// The circuit's notes into the box (opening it, a circuit reloaded under it).
	void load();
	void focus();
	void themeChanged();
	void dpiChanged();
	// Find landed on the notes: the next match of `query` after the selection, selected.
	void select(const std::string& query);

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
	CircuitWindow* win;
	HWND edit = nullptr;
	HBRUSH paper = nullptr;
	COLORREF paperColor = 0;
	bool loading = false;
	int count = 0;
	bool closeHot = false;
	D2D1_RECT_F closeBox{};
	void layoutEdit();
	void changed();
	static LRESULT CALLBACK editProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};

#endif  // CL_WINDOWS_NOTES_H
