// Notes: the student's own notes for a circuit (the website's
// docs/DRAWING-NOTES.md, the Mac's Notes.swift), plain text saved inside it
// and synced with it. A pane at the right of the circuit window (View >
// Show Notes), in Simulation View too. Not the circuit's undo steps: the
// notes have their own undo, so Ctrl+Z in the notes undoes typing and Ctrl+Z
// on the canvas undoes the circuit. Up to 20,000 characters.

#ifndef CL_LINUX_NOTES_H
#define CL_LINUX_NOTES_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;

class NotesPane {
public:
	explicit NotesPane(CircuitWindow* window);
	GtkWidget* widget() const { return revealer; }
	bool shown() const;
	void setShown(bool shown);
	// The document's notes into the pane (a new or reloaded circuit).
	void load();
	// For a picture (--show): this text instead of the circuit's.
	void setSample(const std::string& text);
	// Find: select the next match after the selection; false when none.
	bool select(const std::string& query);
	void themeChanged();

	static const int kMax = 20000, kCounterFrom = 18000;

private:
	CircuitWindow* win;
	GtkWidget* revealer = nullptr;
	GtkWidget* view = nullptr;
	GtkWidget* placeholder = nullptr;
	GtkWidget* counter = nullptr;
	bool loading = false;
	// The notes' own undo: snapshots, a typing burst each.
	std::vector<std::string> undoStack, redoStack;
	std::string lastText;
	gint64 lastEdit = 0;

	std::string text() const;
	void setText(const std::string& s);
	void changed();
	void refresh();
	bool key(GdkEventKey* e);
};

#endif  // CL_LINUX_NOTES_H
