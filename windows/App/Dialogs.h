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

struct FormField {
	enum Kind { Text, Check, Choice, List, Note };
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
// PRINT, LIGHT or DARK for Export as Image; false when cancelled.
bool chooseExportStyle(HWND parent, int& style);

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
	HWND hwnd = nullptr, info = nullptr, clearButton = nullptr, inButton = nullptr, outButton = nullptr;
	WindowSurface surface;
	int zoom = 4;   // pixels per step

	int barHeight() const;
	void layout();
	void paint();
	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
	friend void registerDialogClasses();
};

void registerDialogClasses();

#endif  // CL_WINDOWS_DIALOGS_H
