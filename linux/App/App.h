// CedarLogic for Linux (native): what the pieces of the app share -- the
// settings, where the app's files are, and a few helpers.
//
// The app is GTK 3 on purpose: it's on every Linux desktop (Raspberry Pi OS,
// Ubuntu, Kali, Fedora, Mint...), draws with Cairo on the processor, and
// speaks Wayland and X11 natively. Nothing here uses OpenGL.

#ifndef CL_LINUX_APP_H
#define CL_LINUX_APP_H

#include "CedarCore.h"
#include <gtk/gtk.h>
#include <exception>
#include <string>
#include <vector>

#define CL_APP_NAME "CedarLogic"
#define CL_APP_ID "edu.cedarville.CedarLogic"
#ifndef CL_VERSION
#define CL_VERSION "0.1"
#endif

// ---- Settings ------------------------------------------------------------------
// Kept in ~/.config/CedarLogic/native.ini. The names follow the Mac app's.

struct Prefs {
	int themeMode = 0;            // 0 follow the system, 1 light, 2 dark, 3 as last time
	bool dark = false;            // dark right now
	int accent = 6;               // 0..6: Blue, Purple, Pink, Orange, Green, Graphite, CedarLogic green
	bool showGrid = true;
	int gridStyle = 0;            // 0 lines, 1 dots
	bool majorGrid = true;
	int wireThickness = 1;        // 0 thin, 1 normal, 2 thick
	bool wireDots = true;         // dots at every bend (else only where wires join)
	double wireDotSize = 0.18;
	int lowWire = 0;              // low wires on the dark canvas: silver, slate, white, classic
	int mouseWheel = 0;           // what a plain wheel does: 0 zooms, 1 moves around
	int touchpadScroll = 1;       // what a touchpad's two-finger scroll does
	bool reverseWheel = false;
	bool reverseTouchpad = false;
	bool rightClickRotate = false;
	bool duplicateUsesClipboard = false;
	bool showPalette = true;
	bool showStatus = false;      // the status bar (notes show on the canvas, as on the Mac)
	bool showGateNames = true;
	int tidyMode = 0;             // what Shift+S does: 0 keeps the shape, 1 by signal flow
	bool hasSeenWelcome = false;
	int windowWidth = 1180, windowHeight = 780;
	bool windowMaximized = false;
	int paletteWidth = 236;
	std::vector<std::string> recent;   // most recent first
	std::string lastFolder;

	double wireScale() const;
	void load();
	void save() const;
	// Tell the engine about the wire settings.
	void applyWireDots() const;
	void noteRecent(const std::string& path);
};
Prefs& prefs();

// Whether the desktop asks for dark (GNOME's color-scheme, or a "-dark"
// theme name).
bool systemPrefersDark();
// Apply `prefs().dark` to GTK and every window.
void applyTheme();
// The app's CSS for the theme in use (Style.cpp).
void applyStyle();

// ---- Files -----------------------------------------------------------------------

// Where cl_gatedefs.xml, the help pages and the samples are ("" if not found).
const std::string& resourcesDir();

// ---- Colours (the Mac app's CLPalette) ---------------------------------------

struct RGBA { double r, g, b, a; };
struct Palette {
	bool dark, simView;
	RGBA canvas() const;
	RGBA grid(double intensity) const;
};
RGBA accentColor(bool dark);

// ---- Little helpers ------------------------------------------------------------

std::string baseName(const std::string& path);   // without folder or .cdl
std::string format(const char* fmt, ...) G_GNUC_PRINTF(1, 2);
// A message box with one button.
void showMessage(GtkWindow* parent, GtkMessageType type, const std::string& title, const std::string& text);
// Yes/No; true for Yes.
bool askYesNo(GtkWindow* parent, const std::string& title, const std::string& text);

class CircuitWindow;
// Close every window (asking about unsaved work in each) and quit the app.
// Returns false, leaving the app open, if any window's confirmClose refused.
bool quitApp(GtkApplication* app);
// Open a circuit in a new window (or the current empty one). Returns false
// (after saying why) when it couldn't be opened.
bool openCircuit(GtkApplication* app, const std::string& path, CircuitWindow* from);
CircuitWindow* newCircuitWindow(GtkApplication* app);
// Every open circuit window.
std::vector<CircuitWindow*>& circuitWindows();
// Choose a file and open it.
void chooseAndOpen(GtkApplication* app, GtkWindow* parent);
void rebuildRecentMenus();

// Open a URL or a file in the desktop's default app.
void openExternally(GtkWindow* parent, const std::string& uri);

// ---- Staying up ------------------------------------------------------------------
// Every place GTK calls into the app runs through guarded(): if the engine
// throws, the app says so and carries on (with autosave, nothing is lost),
// where an exception left to reach GTK would end the process.
void reportException(const char* where, const char* what);
template <class F>
auto guarded(const char* where, F&& f) -> decltype(f()) {
	try {
		return f();
	} catch (const std::exception& e) {
		reportException(where, e.what());
	} catch (...) {
		reportException(where, "an unknown error");
	}
	return decltype(f())();
}

#endif  // CL_LINUX_APP_H
