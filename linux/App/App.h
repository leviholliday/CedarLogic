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
// G_CALLBACK for a lambda: commas in its body don't split the macro's argument.
#define CL_CALLBACK(...) G_CALLBACK((__VA_ARGS__))
#define CL_APP_ID "edu.cedarville.CedarLogic"
#ifndef CL_VERSION
#define CL_VERSION "0.4.1"
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
	bool showStatus = true;       // the status bar's readout (zoom, pointer, counts), as on the Mac
	bool showGateNames = true;
	bool showCategoryKeys = true; // Shift+1...0 beside the side panel's categories
	int gateSize = 48;            // the side panel's gates, in points (36...96)
	int toolbarStyle = 3;         // 0 Classic, 2 Minimal, 3 Seamless (the Mac's styles and default)
	int toolbarHidden = 0;        // tool groups hidden, a bit each (ToolGroup)
	bool showTitle = true;        // the circuit's name at the toolbar's left
	bool showThemeToggle = false; // a dark mode switch in the toolbar
	bool showStepClock = true;    // Step Clock in the toolbar and the Simulation View bar (the menu item and key stay)
	bool classicTabs = false;     // plain segments instead of the Mac's tab cards
	bool wireValueTag = false;    // resting on a wire shows what it carries
	bool confirmQuit = true;      // Ctrl+Q asks first
	bool openReplaces = true;     // opening a circuit replaces the one in the window (the Mac's default)
	// Chosen in Settings (so a saved openReplaces is someone's choice): earlier
	// builds saved "false" whether or not anyone chose it.
	bool openReplacesSet = false;
	std::string newTemplate;      // what Ctrl+N starts from: a template's id, or "" for a blank page
	std::string shortcuts;        // changed shortcuts: "action=keys;..." (Shortcuts.cpp)
	bool checkUpdates = true;     // look for a new version now and then
	// Settings > General > Testing group, as on the Mac: 0 Normal tester (the
	// versioned releases), 1 Beta tester (the rolling test build). A copy that
	// ran before there was a choice came from the test build, so it starts as
	// a Beta tester and says so once (channelNotice); see Updater.h.
	int updateChannel = 0;
	bool channelNotice = false;
	// The versioned release this copy's updater installed ("<tag> <build>"),
	// so the same one is never offered again.
	std::string installedRelease;
	// The answers to messages from CedarLogic (Updater.cpp): what cl_message_remember keeps (CedarMessage.h).
	std::string shownMessages;
	int tidyMode = 0;             // what Shift+S does: 0 keeps the shape, 1 by signal flow
	bool hasSeenWelcome = false;
	int windowWidth = 1180, windowHeight = 780;
	bool windowMaximized = false;
	int paletteWidth = 236;
	std::vector<std::string> recent;   // most recent first
	std::string lastFolder;
	std::string lastCircuit;                      // the Your Circuits circuit last in front
	bool newPagesShareLinks = true; // a page added later joins the pages' shared TO/FROM links (docs/PAGE-LINKS.md)
	// Drawing on the circuit (Canvas, Ink.cpp): the last tool, colour and width.
	int inkTool = 0;              // 0 pen, 1 highlighter, 2 eraser
	std::string inkPenColor = "ink", inkHighlighterColor = "yellow";
	double inkPenWidth = 0.25, inkHighlighterWidth = 1.2;
	bool exportInk = true;
	// Simulation View drawn for a projector (the Mac's Projector Mode): thick
	// wires, big labels and lights, brighter colours. Only in Simulation View,
	// so Escape (leaving it) puts the laptop's view back.
	bool projector = false;        // Export as Image: the drawing too, when there is one
	int templateClock = 0;        // New from Template's Clock line: 0 manual (Step Clock), 1 running, 2 a pulse button
	std::string studentName;                      // for the Lab Page template and exports
	// Send Feedback's id for this install (made once, kept): the server
	// counts reports by it, so a school's computers behind one address
	// aren't one sender.
	std::string feedbackInstallId;
	// Build from Formula: what was typed last, and how to build it.
	std::string lastFormula = "F = A'B + AC";
	int buildShape = 0, buildStyle = 0;
	bool buildTwoInput = false, buildNewPage = true;
	// Send Feedback remembers who's sending.
	std::string feedbackName, feedbackEmail;
	std::string feedbackTitle, feedbackDetails, feedbackTags;   // the draft, until it's sent
	int feedbackPriority = 1;
	bool feedbackContact = true;
	// What's New shows once per version.
	std::string lastSeenVersion;
	bool playLaunchSound = true;                  // the launch screen's chime
	bool firstLaunchPlayed = false;               // the first launch's slower splash and sound, once ever
	// The intro (Intro.h), played once each: on the first launch, and on the
	// first launch after an update (the What's New version it introduced).
	bool introFirstLaunchPlayed = false;
	std::string introUpdatePlayed;
	// Settings > General > Launch screen, "When CedarLogic opens" (Intro.h's
	// AtLaunch: 0 the short intro, everyone's to begin with; 1 the full one;
	// 2 the classic launch screen; 3 none). Saved as "short", "full",
	// "classic" or "none"; settled once from 0.3's "Play the intro every time
	// CedarLogic opens" (intro::settleAtLaunch).
	int introAtLaunch = 0;
	std::string seenWhatsNew;
	// Export as Image: the grid, the name-and-result strip, the style and size.
	bool exportGrid = false, exportInfo = true, exportWorks = true, exportColor = true;
	int exportLook = -1;          // Export as Image's look: 0 Match Window, 1 Light, 2 Black & White (-1: from exportColor)
	std::string exportProblem;
	int exportScale = 4;
	// The oscilloscope's timing diagrams: the whole recording or what's on screen; colour or black and white.
	// Export Lab Report: what goes in, and colour or black and white.
	bool reportCircuit = true, reportTable = true, reportFormulas = true, reportTiming = true, reportColor = true;
	bool timingWhole = false, timingInColor = false;                     // the What's New last shown (whatsnew::kVersion)
	// The Actions panel (Actions.h, the "rail" in the code), as the Mac's
	// cl.actions.*: Settings > Actions > Show the Actions panel (on, or none:
	// everything is still in the menus), the panel folded to its tab as it
	// was left, Open on hover (folded, it slides out when the pointer reaches
	// the window's right edge), every action in the Customize list's order
	// and the ones ticked there. 0.4 turned it on, and unfolded it, once for
	// everyone (actionsStart); after that the choice is theirs.
	bool actionsOn = true;
	bool actionsCollapsed = false;
	bool actionsHoverOpen = true;
	std::vector<std::string> actionsOrder;
	std::vector<std::string> actionsShown;
	// Help > Show Me Around (ShowMeAround.h) has been finished or skipped:
	// until then it starts by itself.
	bool showMeAroundSeen = false;
	// Crash reports and usage stats (Privacy.h, docs/PRIVACY.md), asked once,
	// as the Mac's cl.crashReports, cl.usageStats, cl.privacyAsked and
	// cl.privacyChoice (and the Windows app's). The usage id and what's counted
	// are in ~/.config/CedarLogic/usage.json (UsageReport.h), not here.
	int crashReports = -1;        // -1 not chosen yet, 0 off, 1 on (CrashReports.h)
	bool usageStats = false;      // the switch
	bool privacyAsked = false;
	std::string privacyChoice;    // "yes", "no" or "custom" ("" not asked)

	double wireScale() const;
	void load();
	void save() const;
	// Tell the engine about the wire settings (and whether new pages share links).
	void applyWireDots() const;
	void noteRecent(const std::string& path);
};
Prefs& prefs();

// The testing group a copy starts in (Prefs::load), from what its settings
// file had (whether there was one, and a group in it) and linux/install.sh's
// install-channel marker ("beta", "normal" or ""). Updater.h has the why.
struct ChannelStart { int channel; bool notice; };
ChannelStart startingChannel(bool prefsExisted, bool hasChannel, int storedChannel, bool storedNotice, const std::string& marker);

// The Actions panel at a launch (Prefs::load), as the Mac's
// Prefs.migrateActions: 0.4 turns it on for everyone once (new installs and
// updated ones, whatever the old On screen / In the toolbar / Hidden said:
// the `actionsOn04` key), and unfolds it once (`actionsOpen04`). After those,
// what's saved stays. `existed`: there was a settings file.
struct ActionsStart { bool on; bool collapsed; };
ActionsStart actionsStart(bool existed, bool migrated, bool storedOn, bool opened, bool storedCollapsed);

// The project's address, for questions (Help > About CedarLogic, as a
// mailto link). Send Feedback stays the way to report a problem.
#define CL_CONTACT_EMAIL "cedarlogic@proton.me"

// Whether the desktop asks for dark (GNOME's color-scheme, or a "-dark"
// theme name).
bool systemPrefersDark();
// Apply `prefs().dark` to GTK and every window.
void applyTheme();
// The app's CSS for the theme in use (Style.cpp).
void applyStyle();
// A desktop font in a light weight (Raspberry Pi OS's PibotoLt) swapped for
// the regular weight of its family, for this app; before anything draws text.
void sturdyUiFont();
// The mouse wheel over a dropdown, number box or slider scrolls whatever it
// sits in (Settings' page, a dialog's list) instead of changing its value,
// everywhere in the app (Style.cpp). Once, at startup.
void wheelLeavesValuesAlone();
// The part of the screen a window over `near` can use (Style.cpp): the
// monitor's work area, less a margin, and less room for a panel when the
// desktop doesn't say where its panels are (Wayland, and X without a window
// manager, report the whole monitor).
GdkRectangle usableArea(GtkWidget* near);
// The app is on a Wayland desktop (where it can't place its windows).
bool onWayland();
// What of `w` takes the pointer (null: all of it; an empty region: none), in
// its points (Style.cpp). Also for the window a GtkOverlay puts an overlay
// child in, which otherwise takes every press over the child's whole box
// (and the overlay's pass-through, which would, upsets the crossing events).
void inputShape(GtkWidget* w, const cairo_region_t* region);

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
// The menu bar's model, one for every window (main.cpp); nullptr before the
// app has started.
GMenuModel* appMenubar();
// The canvas's single keys (and Tidy Up's, which follows Settings) written
// on their menu items again, after Settings > Shortcuts or Settings changed.
void refreshMenuKeys();
// Window's list of the open circuits, again (a window came, went or was renamed).
void rebuildWindowMenu();
// A file people chose, into the desktop's recent list.
void noteDesktopRecent(const std::string& path);

// A field in a GtkRevealer that's sliding in, made now so it takes keys at
// once (FindBar, Notes; Style.cpp).
void readyForKeys(GtkWidget* revealer, GtkWidget* field);

// Open a URL or a file in the desktop's default app.
void openExternally(GtkWindow* parent, const std::string& uri);
// Help > Learn Logic Online…: the website's lessons (the Mac's AppActions.learnOnlineURL).
constexpr const char* kLearnOnlineUrl = "https://cedarlogic.netlify.app/learn/";

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
