// The find bar (Ctrl+F) over the canvas, as the Mac app's FindBar: labels,
// TO/FROM names and parts on every page. Enter goes to the next one (Shift
// the one before), the arrows either way, Escape closes it. With a label or a
// TO/FROM selected, it opens looking for that name, so a TO's FROMs are one
// Enter away.

#ifndef CL_LINUX_FINDBAR_H
#define CL_LINUX_FINDBAR_H

#include "App.h"
#include <string>
#include <vector>

class CircuitWindow;

class FindBar {
public:
	explicit FindBar(CircuitWindow* window);
	GtkWidget* widget() const { return revealer; }
	void open(const std::string& query);
	void close();
	bool isOpen() const { return gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)); }
	// Look again (the circuit changed); jump to the first hit when asked.
	void run(bool jump);
	void step(int delta);

private:
	struct Hit { int page; long gate; double x, y; std::string text, kind; };
	CircuitWindow* win;
	GtkWidget* root;
	GtkWidget* revealer;   // it slides down into place
	GtkWidget* entry;
	GtkWidget* status;
	GtkWidget* up;
	GtkWidget* down;
	std::vector<Hit> hits;
	int total = 0, index = 0;

	void show(int i);
	void updateStatus();
	static void changedCb(GtkEditable*, gpointer);
	static gboolean keyCb(GtkWidget*, GdkEventKey*, gpointer);
};

#endif  // CL_LINUX_FINDBAR_H
