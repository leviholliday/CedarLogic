// The app's dialogs and extra windows: a gate's settings, quick add, truth
// tables, the oscilloscope, the memory editor, Preferences and the shortcut
// list.

#ifndef CL_LINUX_DIALOGS_H
#define CL_LINUX_DIALOGS_H

#include "App.h"
#include <string>

class CircuitWindow;

// A line of text from the user; false when they cancelled.
bool askText(GtkWindow* parent, const std::string& title, const std::string& prompt, std::string& value);

void showGateSettings(CircuitWindow* w, long gate);
void showQuickAdd(CircuitWindow* w);
void showTruthTable(CircuitWindow* w, int page);
void showRamEditor(CircuitWindow* w, long gate);
void showPreferencesDialog(GtkWindow* parent);
void showShortcutsWindow(GtkWindow* parent);

// The oscilloscope: every signal a TO label names, one row each, recorded
// one sample per simulation step.
class ScopeWindow {
public:
	explicit ScopeWindow(CircuitWindow* owner);
	~ScopeWindow();
	void present();
	void update();   // new samples (called a few times a second while it runs)
	void close();
	bool visible() const;

private:
	CircuitWindow* owner;
	GtkWidget* win;
	GtkWidget* area;
	GtkWidget* info;
	int zoom = 4;   // pixels per step

	void draw(cairo_t* cr);
	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean deleteCb(GtkWidget*, GdkEvent*, gpointer);
	static void clearCb(GtkButton*, gpointer);
	static void zoomInCb(GtkButton*, gpointer);
	static void zoomOutCb(GtkButton*, gpointer);
};

// A place to save a .cdl file ("" when cancelled).
std::string chooseSaveFile(GtkWindow* parent, const std::string& title, const std::string& suggested);

#endif  // CL_LINUX_DIALOGS_H
