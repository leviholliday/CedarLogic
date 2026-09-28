/*****************************************************************************
   Project: CEDAR Logic Simulator
   ModernToolbar: the custom-drawn toolbar and its styles (Segmented,
   Minimal, Seamless). The native toolbar stays as the "Classic" style and as
   the holder of the Pause/Lock toggle state the rest of MainFrame reads.
*****************************************************************************/

#ifndef MODERNTOOLBAR_H_
#define MODERNTOOLBAR_H_

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <vector>

class MainFrame;
class wxGraphicsContext;

namespace cl {
namespace tb {

enum Style { Classic = 0, Segmented, Minimal, Seamless, StyleCount };

// Tool groups the user can hide, one bit each in appSettings.toolbarHidden.
enum Group { GFile = 0, GUndo, GClipboard, GZoom, GSim, GRun, GLock, GTheme, GTab, GroupCount };

const char* styleName(int style);
const char* styleBlurb(int style);
const char* groupName(int group);

// One of res/icons in colour `c`, `px` points square at `scale`, cached --
// the toolbar's own icons, for other bars that should match it.
wxBitmap ToolIcon(const char* name, const wxColour& c, int px, double scale);

// Everything the bar shows that comes from the app. Filled live by
// ModernToolbar, or with sample values for a Preferences preview.
struct State {
	bool dark = false, paused = false, locked = false, simView = false;
	bool canUndo = true, canRedo = false;
	int zoomPct = 100, stepMs = 25, accent = 0;
	wxString title = "Untitled", subtitle = "Page 1";
	wxColour canvas = *wxWHITE;   // what Seamless blends into
	bool operator==(const State& o) const;
};

// A toolbar laid out for one width: the pieces and where they go.
struct Item {
	enum Kind { Button, Toggle, Run, Zoom, Speed, Title, More } kind;
	int id;              // the command it sends (wxID_UNDO, Tool_Pause, ...)
	const char* icon;    // res/icons name, or nullptr
	wxString tip;
	int segment;         // items sharing a segment draw in one rounded group
	wxRect rect;
};

std::vector<Item> layout(int style, int hiddenMask, const State& s, int width, int height);
void paint(wxGraphicsContext* gc, int style, const std::vector<Item>& items, const State& s,
           int width, int height, int hover, int pressed, double scale);

}  // namespace tb
}  // namespace cl

class ModernToolbar : public wxPanel {
public:
	ModernToolbar(wxWindow* parent, MainFrame* frame);

	// Style or hidden tools changed.
	void Reconfigure();
	// Re-read the app's state; repaints only if something visible changed.
	void Poll();

	// Same height as a macOS unified title bar + toolbar; the bar sits in the
	// title bar row, with the window buttons centered on its left.
	// Windows has a title bar of its own above it, so less height there.
#ifdef __WXMSW__
	static int BarHeight() { return 46; }
#else
	static int BarHeight() { return 52; }
#endif
	// The bar drawn off-screen with sample content, for Preferences.
	static wxBitmap RenderPreview(int style, bool dark, int width, double scale);
	// The bar's colour as it is drawn right now, and the colour of its text,
	// so the Windows title bar above can match it.
	wxColour BarColour() const;
	wxColour InkColour() const;

private:
	cl::tb::State readState() const;
	void relayout();
	int hitTest(const wxPoint& p) const;
	void activate(const cl::tb::Item& item);
	void setSpeedFromX(int x, const wxRect& track);

	void OnPaint(wxPaintEvent& e);
	void OnMotion(wxMouseEvent& e);
	void OnLeave(wxMouseEvent& e);
	void OnDown(wxMouseEvent& e);
	void OnUp(wxMouseEvent& e);
	void OnSize(wxSizeEvent& e);

	MainFrame* frame;
	cl::tb::State state;
	std::vector<cl::tb::Item> items;
	int hover = -1, pressed = -1;
	bool draggingSpeed = false;
};

#endif
