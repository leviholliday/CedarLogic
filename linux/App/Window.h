// A circuit window: the menus and toolbar, the gate palette, a tab for each
// page (each its own canvas) and the status bar, around one open circuit.
// It runs that circuit's simulation clock while it's open, and every command
// -- menus, toolbar, the canvas's keys -- comes through here (the Mac app's
// CanvasController).

#ifndef CL_LINUX_WINDOW_H
#define CL_LINUX_WINDOW_H

#include "Anim.h"
#include "App.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class Canvas;
class GatePalette;
class Toolbar;
class TabStrip;
class ScopeWindow;
class StatusBar;
class Sash;
class FindBar;
class TabSwitcher;
namespace formula { struct Plan; }

class CircuitWindow {
public:
	CircuitWindow(GtkApplication* app, CLDocument* doc, const std::string& path);
	~CircuitWindow();

	GtkWindow* window() const { return GTK_WINDOW(win); }
	GtkApplication* application() const { return app; }
	CLDocument* document() const { return doc; }
	const std::string& filePath() const { return path; }
	// An untouched new circuit (a file opened next replaces it).
	bool isPristine() const;
	bool isDirty() const;

	// Replace the circuit shown (open over an untouched new one).
	void replaceDocument(CLDocument* newDoc, const std::string& newPath);
	// A circuit brought back from a recovery copy: unsaved, under its name.
	void markRecovered(const std::string& name);

	// ---- For the canvas ----
	Canvas* currentCanvas() const;
	int currentPage() const;                // the engine's index of the page in front
	// For the guided tour, to point a bubble at each: the palette, the run
	// button, the tab strip. currentCanvas()->widget() is the fourth.
	GtkWidget* paletteWidgetForTour() const { return paletteBox; }
	GtkWidget* runButtonForTour() const;
	GtkWidget* tabStripForTour() const;

	// ---- For the toolbar and the tab strip ----
	std::string titleText() const { return displayName(); }
	int stepMs() const;
	// An action by its full name ("win.save", "app.new"): run it, or ask
	// whether it's enabled now.
	void runAction(const char* name);
	bool actionEnabled(const char* name) const;
	// Every menu (•••), and the circuit's own (its name), under `anchor`
	// (points in `from`).
	void moreMenu(GtkWidget* from, GdkRectangle anchor, GdkEvent* e);
	void titleMenu(GtkWidget* from, GdkRectangle anchor, GdkEvent* e);
	// F10: every menu from the keyboard; Shift+F10 or the Menu key: the
	// canvas's menu for what's selected.
	void menusFromKeyboard();
	void contextMenuFromKeyboard();

	// ---- Tabs, and split view (the Mac's SplitState) ----
	// A tab is a page; its number is the page's index in the document.
	int tabCount() const { return (int)canvases.size(); }
	std::string tabName(int page) const { return pageName(page); }
	std::string pageName(int page) const;
	int currentTab() const { return currentPage(); }
	void showTab(int page) { showPage(page); }
	// Bring a page to the front of its side, and work in that side.
	void showPage(int page);
	// Move a page to where another is, within its side (the strip's drag).
	void movePage(int from, int to);
	void tabContextMenu(int page, GdkEvent* e);
	bool splitOpen() const { return !sideKeys.empty(); }
	// The side you're working in: 0 the first (left, unless swapped), 1 the second.
	int focusedPane() const { return focusPane; }
	int paneOf(const Canvas* c) const;
	// The pages in a side's strip, in order; the one it shows (-1 none).
	std::vector<int> panePages(int pane) const;
	int shownPage(int pane) const;
	Canvas* paneCanvas(int pane) const;
	// A click in a side: work there.
	void activatePane(int pane);
	void toggleSplit();
	void splitWith(int page, bool onRight);
	void movePageToPane(int page, int pane);
	void closeSplit();
	void switchPane();
	// A tab held over the canvas area: drop it to split, or onto the other
	// side to move it there.
	struct DropHint {
		int kind = 0;   // 0 nothing, 1 split, 2 move
		int side = 0;   // split: -1 left half, 1 right half; move: the pane
		bool operator==(const DropHint& o) const { return kind == o.kind && side == o.side; }
	};
	DropHint dropHintAt(int fromPane, GtkWidget* from, double x, double y) const;
	void showDropHint(const DropHint& h);
	void tabDropped(int page, const DropHint& h);
	// The strips' window buttons and drag, in focus mode (and the strip at
	// the window's left edge makes room for buttons there).
	bool focusMode() const { return focusOn; }
	Toolbar* toolbarWidget() const { return toolbar; }
	void toggleFocusMode();
	bool stripIsLeftmost(int pane) const;
	bool stripIsRightmost(int pane) const;
	void redrawStrips();
	// The side panel's drag: the canvas under a point in `from` (and the
	// point in it), or the side next to the panel.
	Canvas* canvasUnder(GtkWidget* from, double x, double y, double& cx, double& cy) const;
	bool addGateFloatingOn(Canvas* c, const std::string& name, double wx, double wy);
	// Bumped on every edit (undo history, page changes...); the minimap's
	// cache key, so it regenerates only when the picture could have changed.
	unsigned editStamp() const { return changes; }
	void redrawMiniMap();
	bool simView() const { return simViewOn; }
	bool locked() const { return lockedOn; }
	bool canEdit() const { return !lockedOn && !simViewOn; }
	bool running() const { return isRunning; }
	double flowPhase() const { return phase; }
	double selectionFade() const;
	double appearProgress() const;
	// A released selection box fading out.
	bool dragFadeBox(double& l, double& b, double& r, double& t, double& alpha) const;
	void fadeOutDragBox(double l, double b, double r, double t);

	void redraw();
	void pointerMoved(double wx, double wy);
	void selectionChanged();
	// After any edit: redraw, the title, the menus' state.
	void edited();
	void note(const std::string& message);
	// What the canvas draws over itself: a note fading in and out, and the
	// banner for Tidy Up's preview, Simulation View and Lock.
	bool toast(std::string& text, double& alpha) const;
	struct BannerButton { std::string label; const char* action; };
	bool bannerFor(std::string& text, std::vector<BannerButton>& buttons) const;
	void lockNudge();
	void showSettings();
	void showContextMenu(int target, double wx, double wy, GdkEventButton* e);
	// A gate chosen from the palette: it appears on the pointer at the next
	// move over the canvas.
	void addGateOnNextMove(const std::string& name);
	bool placePendingGate(double wx, double wy);
	bool hasPendingGate() const { return !pendingGate.empty(); }
	void clearPendingGate() { pendingGate.clear(); }
	bool addGateFloating(const std::string& name, double wx, double wy);
	bool isFloating() const;
	void cancelFloating();

	// .cdl files dropped on the window (from the file manager): opened once
	// the drop is over, the first in this window's place when that's an
	// untouched new one.
	void openDroppedFiles(GtkSelectionData* data);

	// ---- Your Circuits ----
	// Into Your Circuits, quietly (a version when one's due, or now when
	// `explicitSave`). False when it couldn't be written.
	bool saveQuietly(bool explicitSave);
	// A copy as a .cdl file anywhere (Your Circuits keeps the circuit itself).
	bool exportCopy();
	void renameFile();
	void duplicateCircuit();
	// Closed without saving: its circuit went (deleted from Your Circuits).
	void discard();
	void reloadFromDisk(const std::string& message);
	// Something in Your Circuits changed (a rename): the title follows.
	void libraryChanged();

	// ---- Sync (SyncApp.cpp) ----
	// The person did something here: when (ms since 1970), for holding back a change
	// to a circuit they're working in, and for polling while they work.
	void noteInput();
	int64_t lastInputMs() const { return lastInputMsec; }
	// In the middle of something (a drag, a gate on the pointer, Tidy Up's preview):
	// not a moment to save.
	bool busyEditing() const;
	// The circuit changed on disk (another device's edit): reloaded in place, on the same
	// page and with the same view, and a quiet note.
	void reloadForSync(const std::string& message);

	// ---- Templates, My Parts, Build from Formula ----
	// A circuit just made from a template: into Your Circuits under its name.
	void startAs(const std::string& name);
	void partsChanged();
	// ---- Find (Ctrl+F) and Ctrl+Tab ----
	void find();
	void showFoundGate(int page, long gate, double x, double y);
	std::vector<int> recentTabs() const;   // tab indexes, most recently used first
	// The opening card (the Mac's OpeningCard): seconds since it began, while
	// it plays; and what it says under the name.
	bool openingCard(double& t) const;
	std::string openingDetail() const;
	void beginOpening();
	// The overlay over the pages (the find bar's, the tour card's).
	GtkWidget* overlay() const { return pageOverlay; }
	// The plan's parts and wires, on a new page or beside this page's circuit.
	bool buildPlan(const formula::Plan& plan, bool onNewPage, const std::string& pageName);

	// ---- Commands ----
	void newCircuit();
	void open();
	bool save();
	bool saveAs();
	void exportImage();
	void exportReport();
	void exportOlder(int format);
	void print();
	void undo();
	void redo();
	void cut();
	void copy();
	void paste();
	void duplicate();
	void selectAll();
	void selectNone();
	void deleteSelection();
	void rotate();
	void straighten();
	void tidy(int mode = -1);
	void endTidy(bool keep);
	void switchTidyMode();
	bool tidyActive() const;
	void connectNearby(bool quietly);
	void nudge(double dx, double dy);
	void quickAdd();
	void setRunning(bool run);
	void toggleRunning() { setRunning(!isRunning); }
	void stepOnce();
	void setStepMs(int ms);
	void toggleSimView();
	void toggleLock();
	void makeTruthTable();
	void checkCircuit();   // the truth table at its Check tab
	void toggleScope();
	void newPage();
	void closePage(int page);
	void reopenPage();
	void renamePage(int page);
	void cyclePage(int delta);
	void toggleDark();
	void togglePalette();
	void showPaletteCategory(int index);
	void showRam(long gate);
	void showPreferences();
	void showShortcuts();
	void showHelp();
	void showAbout();

	// Ask to save first when there are changes. False: stay open.
	bool confirmClose();
	void themeChanged();
	void prefsChanged();
	void rebuildRecentMenu();

	bool hasSelection() const;

private:
	GtkWidget* win = nullptr;
	GtkApplication* app;
	CLDocument* doc;
	std::string path;
	bool forceDirty = false;          // unsaved, though the engine's copy says otherwise (a failed save, a recovery copy)
	// The recovery copy (Recovery.cpp): written now and then while there are
	// unsaved changes, removed on a clean close.
	std::string recoveryBase;
	std::string recoveredName;
	unsigned changes = 0, changesAtRecovery = 0;
	gint64 lastRecovery = 0;
	int64_t lastInputMsec = 0;
	void writeRecovery();

	GtkWidget* notebooks[2] = { nullptr, nullptr };
	GtkWidget* paneBoxes[2] = { nullptr, nullptr };
	TabStrip* strips[2] = { nullptr, nullptr };
	GtkWidget* splitPaned = nullptr;  // the two sides
	GtkWidget* area = nullptr;        // the canvas area (both sides), with the drop hint over it
	GtkWidget* hintLayer = nullptr;
	GtkWidget* scopePaned = nullptr;  // the area over the oscilloscope
	GtkWidget* sideRevealer = nullptr;
	GtkWidget* titleRevealer = nullptr;
	GtkWidget* paletteBox = nullptr;
	StatusBar* statusBar = nullptr;
	std::set<uint64_t> sideKeys;      // the second side's pages; empty: no split
	std::set<uint64_t> closedSideKeys;   // the second side's pages that closed: back there when they're reopened
	bool sideFirst = false;           // the second side sits on the left
	int focusPane = 0;
	DropHint hint;
	anim::Tween hintFade;
	DropHint hintShown;               // the last one drawn, kept while it fades out
	bool focusOn = false;
	int paletteWidth() const;
	// A tab one place along its side (the tab menu's Move Left and Right).
	void moveTabBy(int page, int delta);
	void layoutSplit();
	void reconcileSplit();
	// After a page closed in a split (closed, or a redo closing it again):
	// the side it was in shows its neighbour there, the other side keeps
	// the page it shows. `at` was its index; `otherFront` the other side's page.
	void showAfterSplitClose(int pane, int at, bool wasCurrent, bool wasFront, uint64_t otherFront);
	static gboolean drawHintCb(GtkWidget*, cairo_t*, gpointer);
	guint autosaveId = 0;             // saving as you go, a moment after the last change
	static gboolean autosaveCb(gpointer self);
	Toolbar* toolbar = nullptr;
	int lastZoomShown = -1;
	GtkWidget* banner = nullptr;      // Tidy Up's keep/undo bar, Simulation View's
	GtkWidget* bannerLabel = nullptr;
	GtkWidget* bannerButtons = nullptr;
	GMenu* recentMenu = nullptr;
	GatePalette* palette = nullptr;
	class MiniMap* miniMap = nullptr;
	ScopeWindow* scope = nullptr;
	FindBar* findBar = nullptr;
	GtkWidget* pageOverlay = nullptr;
	double openingAt = -1;            // when the opening card began (monotonic seconds; -1: none)
	bool openingRevealed = false;
	TabSwitcher* switcher = nullptr;
	std::vector<uint64_t> recentKeys;   // pages by key, most recently in front first
	std::vector<Canvas*> canvases;    // in tab order
	bool syncing = false;             // rebuilding the tabs; ignore the notebook's signals

	bool isRunning = true;
	bool simViewOn = false;
	bool lockedOn = false;
	double phase = 0;                 // Simulation View's marching dashes, in points
	guint timer = 0;
	gint64 lastTick = 0, lastStatus = 0, lastTitle = 0, lastScope = 0;
	bool statusDirty = true;
	double pointerX = 0, pointerY = 0;
	std::string pendingGate;
	gint64 selectionChangedAt = 0, appearStart = 0, dragFadeStart = 0, messageAt = 0;
	std::string noteText;
	std::string selectionSignature;
	bool hasDragFade = false;
	double fadeL = 0, fadeB = 0, fadeR = 0, fadeT = 0;
	int lastPageCount = 1;
	std::map<uint64_t, bool> seenPages;

	void build();
	void buildMenus();
	void addActions();
	void syncTabs();
	GtkWidget* tabLabel(Canvas* c);
	void updateTabLabels();
	void updateTitle();
	void updateStatus();
	void updateActions();
	void updateRunUI();
	void updateBanner();
	void refreshAfterHistory();
	void tick();
	bool writeTo(const std::string& file);
	bool placePoint(double& wx, double& wy) const;
	void floatSelection(double wx, double wy);
	void pasteText(const std::string& text, bool floating, bool shift);
	std::string displayName() const;
	// The name, as last read from Your Circuits, for the path and recovered
	// name it was read for (the toolbar asks on every paint).
	mutable std::string cachedName, cachedFor;
	mutable bool nameValid = false;

	static gboolean tickCb(gpointer);
	static gboolean deleteCb(GtkWidget*, GdkEvent*, gpointer);
	static void destroyCb(GtkWidget*, gpointer);
	static void switchPageCb(GtkNotebook*, GtkWidget*, guint, gpointer);
	static gboolean keyCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean keyReleaseCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean stateCb(GtkWidget*, GdkEventWindowState*, gpointer);
	static void sizeCb(GtkWidget*, GdkRectangle*, gpointer);
	static void clipboardCb(GtkClipboard*, const gchar*, gpointer);

	friend class Canvas;
	friend class StatusBar;
	friend class ScopeWindow;
};

#endif  // CL_LINUX_WINDOW_H
