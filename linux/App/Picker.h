// Picker: the window Your Circuits, Version History, New from Template and
// the like are made of, drawn as the Mac app's LibraryView (as the Windows
// app's Picker): a heading, a quiet line under it, an optional search field,
// a list of tall rows (a gate tile, a name, a second line, a badge), an
// optional picture beside it, and buttons along the bottom. It runs modally
// over the circuit window.

#ifndef CL_LINUX_PICKER_H
#define CL_LINUX_PICKER_H

#include "Chrome.h"

#include <functional>
#include <string>
#include <vector>

namespace picker {

// LibraryDialogs' colours (the Mac's PickerLook).
struct Look {
	bool dark;
	Color paper() const { return dark ? rgb255(28, 31, 37) : rgb255(250, 250, 252); }
	Color ink(float a = 1) const { return dark ? rgb255(226, 230, 238, a) : rgb255(30, 33, 40, a); }
	Color sheet() const { return dark ? rgb255(22, 24, 29) : rgb255(255, 255, 255); }
};

struct Row {
	std::string id, title, subtitle, badge;
	bool heading = false;   // a section's title ("BUILT IN"), not chosen
	bool tile = true;       // the gate tile in front
};

const float kRowH = 62, kMargin = 22;

// A tile with a logic-gate silhouette, tinted by the accent.
void gateTile(cairo_t* cr, const RectF& r, Color accent, bool on);

class Picker {
public:
	std::string title, line, emptyText = "Nothing here yet.";
	bool search = true;
	float width = 600, height = 540, listWidth = 0;   // 0: the list takes the width
	bool listOnLeft = false;                          // with a preview: the list first
	std::vector<std::string> leftButtons, rightButtons;   // the last right one is the default
	std::function<std::vector<Row>(const std::string& query)> rows;
	// A button pressed (left ones 0.., right ones 100..): true closes.
	std::function<bool(Picker&, int button)> onButton;
	// A key (GDK keyval, with Ctrl down or not): true when it's used.
	std::function<bool(Picker&, guint key, bool ctrl)> onKey;
	std::function<void(cairo_t*, const RectF&)> preview;
	std::function<void(Picker&)> onSelect;
	// Draws a row's tile in place of the gate silhouette (Add a Gate draws the gate itself).
	std::function<void(cairo_t*, const Row&, const RectF&)> drawTile;

	std::vector<Row> shown;
	int selection = 0;
	GtkWidget* window = nullptr;   // while it runs
	GtkWidget* entry = nullptr;

	void reload();
	void select(int i, int direction = 1);
	const Row* selected() const;
	void redraw();
	void close() { done = true; if (loop) g_main_loop_quit(loop); }
	// Modal over `owner` until a button or Escape closes it.
	void run(GtkWindow* owner);

private:
	GtkWidget* area = nullptr;
	GMainLoop* loop = nullptr;
	float scroll = 0;
	int hot = -1, hotButton = -1;
	bool done = false;
	struct Button { RectF rect; int id; std::string label; bool primary; };
	std::vector<Button> buttons;

	float clientW() const;
	float clientH() const;
	float top() const { return kMargin + 34 + 44 + (search ? 44 : 0); }
	RectF listRect() const;
	void clampScroll();
	void paint(cairo_t* cr);
	int rowAt(float x, float y) const;
	int buttonAt(float x, float y) const;
	void press(int id);
	bool key(guint keyval, bool ctrl);

	static gboolean drawCb(GtkWidget*, cairo_t*, gpointer);
	static gboolean pressCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean releaseCb(GtkWidget*, GdkEventButton*, gpointer);
	static gboolean motionCb(GtkWidget*, GdkEventMotion*, gpointer);
	static gboolean leaveCb(GtkWidget*, GdkEventCrossing*, gpointer);
	static gboolean scrollCb(GtkWidget*, GdkEventScroll*, gpointer);
	static gboolean keyCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean deleteCb(GtkWidget*, GdkEvent*, gpointer);
	static void changedCb(GtkEditable*, gpointer);
};

}  // namespace picker

#endif  // CL_LINUX_PICKER_H
