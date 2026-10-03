// The app's dialogs and extra windows: a gate's settings, quick add, truth
// tables, the oscilloscope, the memory editor, Preferences and the shortcut
// list.

#ifndef CL_LINUX_DIALOGS_H
#define CL_LINUX_DIALOGS_H

#include "Chrome.h"
#include <vector>
#include <string>

class CircuitWindow;

// A line of text from the user; false when they cancelled.
bool askText(GtkWindow* parent, const std::string& title, const std::string& prompt, std::string& value);

void showGateSettings(CircuitWindow* w, long gate);
void showQuickAdd(CircuitWindow* w);
void showTruthTable(CircuitWindow* w, int page);   // TruthTableWindow.cpp
// Export as Image (ExportImage.cpp): the page as a picture, with your name and result.
void showExportImage(CircuitWindow* w, int page);
// Build from Formula: switches, gates and lights from a formula typed in.
void showBuildFormula(CircuitWindow* w);
void showRamEditor(CircuitWindow* w, long gate);
void showShortcutsWindow(GtkWindow* parent);

// The oscilloscope (Scope.cpp), as the Mac's ScopeView: every signal a TO
// label names, one row each, recorded one sample per simulation step; a time
// cursor that reads every signal at once, zoom, signals hidden with H, and a
// timing diagram for a lab report (copied, or saved as a PNG).
class ScopeWindow {
public:
	explicit ScopeWindow(CircuitWindow* owner);
	~ScopeWindow();
	void present();
	void update();   // new samples (called a few times a second while it runs)
	void close();
	bool visible() const;
	GtkWidget* widget() const { return win; }   // the panel, docked under the canvas

private:
	struct Button { RectF r; int id; };
	CircuitWindow* owner;
	GtkWidget* win;
	GtkWidget* area;
	float pointsPerStep = 6;
	int cursor = -1;   // the sample the cursor is on (-1: live, at the end)
	int chosen = 0;    // the signal the arrows and H act on
	float scrollY = 0;
	std::vector<std::string> hidden;
	std::vector<Button> buttons;
	int hot = -1;
	bool dragging = false;
	int shownStart = 0, shownCount = 0;

	std::vector<std::string> signals() const;
	std::vector<int> shownSignals() const;
	void window(float width, int length, int& start, int& count) const;
	void setCursorAt(float x, float width);
	void paint(cairo_t* cr, float w, float h);
	void press(int id, GdkEvent* e);
	bool key(guint keyval, guint state);
	void hiddenMenu(GdkEvent* e);
	void exportMenu(GdkEvent* e);
	cairo_surface_t* timingImage();
};

// A place to save a .cdl file ("" when cancelled).
std::string chooseSaveFile(GtkWindow* parent, const std::string& title, const std::string& suggested);

#endif  // CL_LINUX_DIALOGS_H
