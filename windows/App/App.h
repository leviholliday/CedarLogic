// CedarLogic for Windows (native): what the pieces of the app share -- the
// settings, where the app's files are, text conversion, drawing helpers and a
// few small dialogs.
//
// Plain Win32 on purpose: windows, standard controls and menus that come with
// Windows, Direct2D for the canvas. No wxWidgets, no Skia, nothing to install
// beside the app. Text inside the app is UTF-8; it becomes UTF-16 only at the
// Windows calls.

#ifndef CL_WINDOWS_APP_H
#define CL_WINDOWS_APP_H

// The Windows headers first: once anything says `using namespace std`, their
// `byte` clashes with std::byte.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>

#include "CedarCore.h"
#include <exception>
#include <string>
#include <vector>

#define CL_APP_NAME "CedarLogic"
#ifndef CL_VERSION
#define CL_VERSION "0.1"
#endif

// ---- Settings ------------------------------------------------------------------
// Kept in %APPDATA%\CedarLogic\native.ini. The names follow the Mac and Linux
// apps'.

struct Prefs {
	int themeMode = 0;            // 0 follow Windows, 1 light, 2 dark, 3 as last time
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
	bool showStatus = false;      // the old status bar (notes show on the canvas instead)
	bool showGateNames = true;
	int tidyMode = 0;             // what Shift+S does: 0 keeps the shape, 1 by signal flow
	bool hasSeenWelcome = false;
	bool firstLaunchPlayed = false;               // the launch screen's sound, once ever
	int windowWidth = 1180, windowHeight = 780;   // in 96-dpi units
	bool windowMaximized = false;
	int paletteWidth = 210;                       // in 96-dpi units
	std::vector<std::string> recent;              // most recent first
	std::string lastFolder;
	std::string lastCircuit;                      // the Your Circuits circuit last in front
	std::string studentName;                      // for the Lab Page template and exports
	// Build from Formula's last choices.
	std::string lastFormula;
	int buildShape = 0, buildStyle = 0;
	bool buildTwoInput = false, buildNewPage = true;
	int truthTab = 0;                             // the truth table's tab, as last left
	// Timing diagrams (the oscilloscope's share menu).
	bool timingWhole = false, timingInColor = false;
	// Export as Image's last choices.
	bool exportGrid = false, exportColor = true, exportInfo = false, exportWorks = true;
	int exportScale = 4;                          // 2, 4 or 6 pixels a point
	std::string exportProblem;                    // "does not work because…"
	// Send Feedback's draft, kept until it's sent.
	std::string feedbackTitle, feedbackDetails, feedbackTags, feedbackEmail;   // tags: comma separated
	int feedbackPriority = 1;                     // low, normal, high, blocking
	bool feedbackContact = true;
	bool confirmQuit = true;                      // Ctrl+Q asks first
	std::string seenWhatsNew;                     // the version What's New was last shown for

	double wireScale() const;
	void load();
	void save() const;
	// Tell the engine about the wire settings.
	void applyWireDots() const;
	void noteRecent(const std::string& path);
};
Prefs& prefs();

// Whether Windows is set to dark for apps.
bool systemPrefersDark();
// Apply `prefs().dark` to every window.
void applyTheme();

// ---- Files -----------------------------------------------------------------------

// Where cl_gatedefs.xml, the help pages and the samples are ("" if not found).
const std::string& resourcesDir();
// %APPDATA%\CedarLogic (made if it isn't there).
std::string settingsDir();
bool fileExists(const std::string& path);

// ---- Text ------------------------------------------------------------------------

std::wstring W(const std::string& utf8);
std::string U(const std::wstring& wide);
std::string baseName(const std::string& path);   // without folder or .cdl
std::string dirName(const std::string& path);
std::string strf(const char* fmt, ...);
std::string lowerCase(const std::string& s);

// ---- Colours (the Mac app's CLPalette) ---------------------------------------

struct RGBA { double r, g, b, a; };
struct Palette {
	bool dark, simView;
	RGBA canvas() const;
	RGBA grid(double intensity) const;
};
RGBA accentColor(bool dark);
inline D2D1_COLOR_F d2dColor(const RGBA& c) { return D2D1::ColorF((float)c.r, (float)c.g, (float)c.b, (float)c.a); }

// ---- Drawing -----------------------------------------------------------------------

ID2D1Factory* d2dFactory();
IDWriteFactory* dwFactory();

// A Direct2D target for a window, made on first paint and remade when the
// display takes it away. Its units are points (1/96 inch): the transform
// scales by the window's DPI.
class WindowSurface {
public:
	~WindowSurface() { release(); }
	// Between BeginPaint and EndPaint: the target, ready to draw (after
	// BeginDraw), or null. Call end() after drawing.
	ID2D1HwndRenderTarget* begin(HWND hwnd);
	void end();
	void release();
	double scale() const { return dpiScale; }

private:
	ID2D1HwndRenderTarget* rt = nullptr;
	double dpiScale = 1;
};

// UI text drawn with DirectWrite, in the system's UI font.
enum class TextAlign { Leading, Center, Trailing };
void drawText(ID2D1RenderTarget* rt, const std::string& text, const D2D1_RECT_F& box, float size,
              const D2D1_COLOR_F& color, TextAlign align = TextAlign::Leading, bool bold = false);
float textWidth(const std::string& text, float size, bool bold = false);

// ---- Windows and controls ----------------------------------------------------------

// A steady clock, in seconds.
double nowSeconds();

HINSTANCE appInstance();
UINT dpiOf(HWND hwnd);
inline int scaled(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }
// The system's message font at a DPI (cached; don't delete it).
HFONT uiFont(UINT dpi);
// Set a font on a window and all its children.
void setFontTree(HWND hwnd, HFONT font);
std::string windowText(HWND hwnd);
void setWindowText(HWND hwnd, const std::string& text);
// A dark or light title bar, to match the canvas.
void setDarkTitleBar(HWND hwnd, bool dark);
// A standard control in Windows' own dark style (Explorer's), when dark:
// `theme` is "Explorer" for buttons, lists and scroll bars, "CFD" for text
// boxes and drop-down lists, "ItemsView" for list views.
void darkenControl(HWND control, bool dark, const wchar_t* theme);

// ---- Messages ----------------------------------------------------------------------

enum class Tone { Info, Warning, Error };
void showMessage(HWND parent, Tone tone, const std::string& title, const std::string& text);
bool askYesNo(HWND parent, const std::string& title, const std::string& text);

// ---- Clipboard -----------------------------------------------------------------------

bool setClipboardText(HWND owner, const std::string& text);
bool clipboardText(HWND owner, std::string& out);

// ---- File choosers --------------------------------------------------------------------

struct FileFilter { const char* name; const char* pattern; };
// Returns the chosen files (several only if `multiple`); empty when cancelled.
std::vector<std::string> chooseOpenFiles(HWND parent, const std::string& title, const std::vector<FileFilter>& filters,
                                         bool multiple);
// "" when cancelled. `ext` (".cdl") is added when the name has none.
std::string chooseSaveFile(HWND parent, const std::string& title, const std::string& suggested,
                           const std::vector<FileFilter>& filters, const char* ext);

// ---- The app ---------------------------------------------------------------------------

class CircuitWindow;
// Close every window (asking about unsaved work in each) and quit the app.
// Returns false, leaving the app open, if any window's confirmClose refused.
bool quitApp();
// Open a circuit in a new window (or the current empty one). Returns false
// (after saying why) when it couldn't be opened.
bool openCircuit(const std::string& path, CircuitWindow* from);
CircuitWindow* newCircuitWindow();
// Every open circuit window.
std::vector<CircuitWindow*>& circuitWindows();
// Choose files and open them.
void chooseAndOpen(CircuitWindow* from);
// The practice circuit, as a new untitled copy.
void openPracticeCircuit(CircuitWindow* from);
// Open a URL or a file in its default app.
void openExternally(HWND parent, const std::string& target);
// Handle a key the whole app answers to (Ctrl+S, Ctrl+Tab...). True when it
// was one.
bool handleShortcut(CircuitWindow* w, const MSG& msg);

// ---- Staying up ------------------------------------------------------------------
// Every place Windows calls into the app runs through guarded(): if the engine
// throws, the app says so and carries on (with recovery copies, nothing is
// lost), where an exception left to reach Windows would end the process.
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

#endif  // CL_WINDOWS_APP_H
