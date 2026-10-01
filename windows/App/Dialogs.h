// The app's dialogs and extra windows: a gate's settings, quick add, truth
// tables, the oscilloscope, the memory editor, Preferences and the shortcut
// list -- and Form, the small builder they're made with: a list of fields
// laid out top to bottom in a standard Windows dialog.

#ifndef CL_WINDOWS_DIALOGS_H
#define CL_WINDOWS_DIALOGS_H

#include "App.h"
#include <functional>
#include <string>
#include <vector>

class CircuitWindow;
struct IWICBitmap;

struct FormField {
	enum Kind { Text, Check, Choice, List, Note, Picture };
	Kind kind = Note;
	std::string label;                  // beside a Text or Choice; a Check's own text; a Note's text
	std::string value;                  // Text: the text. Check: "1" or "". Choice: the index.
	std::vector<std::string> choices;   // Choice: the items. List: the column titles.
	std::vector<int> columnWidths;      // List: in points (0 shares what's left)
	int lines = 1;                      // List and Note: how tall, in rows of text
	bool browse = false;                // Text: a Choose... button for a file
	bool browseSave = false;            //   which saves (else opens)
	bool mono = false;                  // List: a fixed-width font
	int arrowsMove = -1;                // Text: Up and Down move this List field's selection
	std::string tip;                    // shown under a Text field, dimmed
	// Picture: drawn with Direct2D, in points, `height` points tall and the
	// form's width; Form::refresh redraws it.
	int height = 0;
	std::function<void(ID2D1RenderTarget* rt, float width, float height)> paint;
	HWND hwnd = nullptr;
	HWND extra = nullptr;               // the label, or the Choose... button
};

class Form {
public:
	std::string title;
	int width = 380;                           // points
	std::string okText = "OK", cancelText = "Cancel";   // an empty one is left out
	std::vector<std::string> buttons;          // more buttons, left of OK: they return 100, 101...
	std::vector<FormField> fields;

	std::function<void(Form&)> onInit;
	std::function<void(Form&, int field)> onChange;       // a field changed (typing, a choice, a tick)
	std::function<std::string(Form&)> validate;           // on OK: "" to close, else what's wrong
	std::function<bool(Form&, int button)> onButton;      // one of `buttons`: true to close with it
	std::function<void(Form&, int field, int row)> onActivate;   // a List row double-clicked
	std::function<void(Form&)> onTimer;
	std::function<void(Form&, int field, float x, float y)> onClick;   // a Picture, in its points
	std::function<void(Form&, int field, int delta)> onWheel;          // the wheel over a Picture (120 a notch)
	std::function<bool(Form&, int field, UINT vk)> onKey;              // Up, Down, Page Up/Down in a Text field: true if used
	int timerMs = 0;

	int add(const FormField& f) { fields.push_back(f); return (int)fields.size() - 1; }
	// Show it and wait. Returns IDOK, IDCANCEL, or 100 + a button's index.
	int run(HWND owner);

	// While it's open.
	HWND dialog = nullptr;
	std::string text(int field) const;
	void setText(int field, const std::string& text);
	bool checked(int field) const;
	int choice(int field) const;
	void setRows(int field, const std::vector<std::vector<std::string>>& rows);
	void setCell(int field, int row, int column, const std::string& text);
	int selectedRow(int field) const;
	void selectRow(int field, int row);
	void setProblem(const std::string& text);
	void refresh(int field);                  // a Picture: draw it again
	void enable(int field, bool on);

	// For the dialog procedure.
	HWND problem = nullptr;
	std::vector<HWND> buttonWindows;
	void build();
	void capture();
};

// A line of text from the user; false when they cancelled.
bool askText(HWND parent, const std::string& title, const std::string& prompt, std::string& value);

void showGateSettings(CircuitWindow* w, long gate);
void showQuickAdd(CircuitWindow* w);
void showTruthTable(CircuitWindow* w, int page);
void showRamEditor(CircuitWindow* w, long gate);
void showPreferencesDialog(HWND parent);
void showShortcutsWindow(HWND parent);
void showBuildFormula(CircuitWindow* w);
// Ctrl+Q: true to go ahead (asked unless the user said not to).
bool confirmQuit(HWND parent);
// Export as Image (Export.cpp).
void showExportImage(CircuitWindow* w, int page);

// The oscilloscope, redesigned as the Mac app's (ScopeView.swift): every
// signal a TO label names as a clean waveform, a time cursor that reads every
// signal's value at once, zoom and scroll through time, and the keyboard:
//   Left/Right   move the cursor a step (Shift: 10 steps)
//   Alt+Left/Right  jump to the previous/next change on the chosen signal
//   Up/Down      choose a signal          + / -   zoom in and out in time
//   Home / End   first sample / follow the live end again
//   H            hide or show the chosen signal
//   Space        run or pause the simulation      C   clear the recording
// Its share button makes a timing diagram for a lab report (Scope.cpp).
class ScopeWindow {
public:
	explicit ScopeWindow(CircuitWindow* owner);
	~ScopeWindow();
	void present();
	void update();   // new samples (called a few times a second while it runs)
	void close();
	bool visible() const;
	// The timing diagram, as the share menu makes it (for --timing on CI too).
	bool saveTimingDiagram(const std::string& file);

private:
	CircuitWindow* owner;
	HWND hwnd = nullptr;
	WindowSurface surface;
	float pointsPerStep = 6;
	int cursor = -1;           // a sample index; -1 follows the live end
	int chosen = 0;            // the signal the keys act on
	std::vector<std::string> hidden;
	float scrollY = 0;
	int shownStart = 0, shownCount = 0;   // the samples last drawn
	bool dragging = false;
	int hot = -1;
	struct Button { D2D1_RECT_F r; int id; };
	std::vector<Button> buttons;

	std::vector<std::string> signals() const;
	std::vector<int> shownSignals() const;   // indexes of those not hidden
	void window(float width, int length, int& start, int& count) const;
	void setCursorAt(float x, float width);
	void press(int id);
	IWICBitmap* timingImage();
	void exportMenu(POINT at);
	void hiddenMenu(POINT at);
	bool key(UINT vk);
	void paint();
	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
	friend void registerDialogClasses();
};

void registerDialogClasses();

#endif  // CL_WINDOWS_DIALOGS_H
