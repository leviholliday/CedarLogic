// A circuit window: the menu bar and toolbar, the gate palette, a tab for
// each page (each its own canvas) and the status bar, around one open
// circuit. It runs that circuit's simulation clock while it's open, and every
// command -- menus, toolbar, the canvas's keys -- comes through here (the Mac
// app's CanvasController, the Linux app's CircuitWindow).

#ifndef CL_WINDOWS_WINDOW_H
#define CL_WINDOWS_WINDOW_H

#include "App.h"
#include "Commands.h"
#include "Formula.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class Canvas;
class GatePalette;
class MiniMap;
class ScopeWindow;
class StatusBar;
class FindBar;
class TabSwitcher;
class TabStrip;
class Toolbar;

class CircuitWindow {
public:
	CircuitWindow(CLDocument* doc, const std::string& path);
	~CircuitWindow();

	HWND window() const { return hwnd; }
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
	// The opening card (the Mac's OpeningCard): seconds since it began, while
	// it plays; and what it says under the name.
	bool openingCard(double& t) const;
	static double cardFreeze;   // --card-frame: the card held at this moment
	std::string openingDetail() const;
	// A released selection box fading out.
	bool dragFadeBox(double& l, double& b, double& r, double& t, double& alpha) const;
	void fadeOutDragBox(double l, double b, double r, double t);
	void statusNeedsUpdate() { statusDirty = true; }
	// Where the pointer last was over the canvas, in the world (the status bar).
	void pointer(double& x, double& y) const { x = pointerX; y = pointerY; }

	void redraw();
	void pointerMoved(double wx, double wy);
	void selectionChanged();
	// After any edit: redraw, the title, the toolbar's state.
	void edited();
	void note(const std::string& message);
	void lockNudge();
	void showSettings();
	void showContextMenu(int target, double wx, double wy, POINT screen);
	// A gate chosen from the palette: it appears on the pointer at the next
	// move over the canvas.
	void addGateOnNextMove(const std::string& name);
	bool placePendingGate(double wx, double wy);
	bool hasPendingGate() const { return !pendingGate.empty(); }
	void clearPendingGate() { pendingGate.clear(); }
	bool addGateFloating(const std::string& name, double wx, double wy);
	bool isFloating() const;
	void cancelFloating();

	// ---- Commands ----
	void run(int command);
	// Save into Your Circuits (Ctrl+S also keeps a version).
	bool save();
	bool saveQuietly(bool explicitSave);
	bool exportCopy();
	// Close without saving (its circuit was deleted).
	void discard();
	// Open the circuit's file again (a version was restored).
	void reloadFromDisk(const std::string& message);
	// A circuit in Your Circuits was renamed.
	void libraryChanged();
	// A new circuit from a template: called `name`, and in Your Circuits.
	void startAs(const std::string& name);
	// My Parts gained or lost one.
	void partsChanged();
	// Build from Formula's circuit: on a new page (named), or beside this
	// page's circuit. False when nothing could be built.
	bool buildPlan(const formula::Plan& plan, bool onNewPage, const std::string& pageName);
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
	void toggleScope();
	void newPage();
	void closePage(int page);
	void reopenPage();
	void renamePage(int page);
	void cyclePage(int delta);
	void showPage(int index);
	void toggleDark();
	void togglePalette();
	void showPaletteCategory(int index);
	void showShortcuts();

	// Ask to save first when there are changes. False: stay open.
	bool confirmClose();
	// Take the window down (after confirmClose, or when quitting).
	void destroy();
	void themeChanged();
	void prefsChanged();

	bool hasSelection() const;
	// For --screenshot: the whole window (or a dialog in front of it), drawn
	// into a PNG.
	bool screenshot(const std::string& file, HWND other = nullptr);

	// ---- For the toolbar and the tabs ----
	bool commandEnabled(int command) const;
	int commandChecked(int command) const;   // -1 not a check item
	std::string titleText() const { return displayName(); }
	int stepMs() const;
	void titleMenu(POINT screen);
	// Every menu, as a popup (the toolbar's •••, Alt, F10).
	void moreMenu(POINT screen, bool rightAligned);
	// A tab is a page: its index in the document, as the canvases are in
	// the document's order.
	int tabCount() const { return (int)canvases.size(); }
	std::string tabName(int index) const;
	int currentTab() const { return current; }
	void closeTab(int index);
	void moveTab(int from, int to);
	// A tab one place along its side (the tab menu's Move Left and Right).
	void moveTabBy(int index, int delta);
	void tabContextMenu(int index, POINT screen);

	// ---- Split view (the Mac's SplitState) ----
	// A split puts some pages in a second side; each side has its own tab
	// strip and shows one of its pages.
	bool splitOpen() const { return !sideKeys.empty(); }
	// The side you're working in: 0 the first (left, unless swapped), 1 the second.
	int focusedPane() const { return focusPane; }
	int paneOf(const Canvas* c) const;
	std::vector<int> panePages(int pane) const;   // a side's pages, in order
	int shownPage(int pane) const;                 // the page it shows (-1 none)
	Canvas* paneCanvas(int pane) const;
	void activatePane(int pane);
	void toggleSplit();
	void splitWith(int page, bool onRight);
	void movePageToPane(int page, int pane);
	void closeSplit();
	void switchPane();
	bool stripIsRightmost(int pane) const;
	// A tab held over the canvas area (a screen point): drop it to split
	// the view, or onto the other side to move it there.
	struct DropHint {
		int kind = 0;   // 0 nothing, 1 split, 2 move
		int side = 0;   // split: -1 left half, 1 right half; move: the pane
		bool operator==(const DropHint& o) const { return kind == o.kind && side == o.side; }
	};
	DropHint dropHintAt(int fromPane, POINT screen) const;
	void showDropHint(const DropHint& h);
	void tabDropped(int page, const DropHint& h);
	// A canvas took the keyboard (a click in it): work in its side.
	void canvasFocused(Canvas* c);
	// On one of the lines that drag (the side panel's edge, the line between
	// the sides, the oscilloscope's top): the parts under it leave the
	// pointer to the window there.
	bool onDivider(POINT screen) const;
	TabStrip* tabStripWidget(int pane) const { return pane == 0 || pane == 1 ? strips[pane] : nullptr; }   // for --click-test

	// ---- Focus mode ----
	// The Mac's: the toolbar and the side panel slide away, and the tab
	// strips become the window's top row.
	bool focusMode() const { return focusOn; }
	void toggleFocusMode();

	// Ctrl+Tab: the switcher (TabSwitcher.h). Its tabs, most recently used
	// first, and the page a tab shows.
	void switchTabs(bool backwards);
	bool switcherActive() const;
	void cancelSwitcher();
	std::vector<int> recentTabs() const;
	int pageOfTab(int index) const;

	// Find's result: its page in front, it selected and in the middle.
	void showFoundGate(int page, long gate, double x, double y);
	std::string pageTitle(int page) const { return pageName(page); }

	// For the guided tour: 0 the palette, 1 the canvas, 2 Run, 3 the tabs
	// (screen pixels).
	RECT tourAnchor(int which) const;
	// Show it (after the launch screen).
	void present();

	// ---- For the canvas's overlays ----
	// The bar over the top of the canvas (Tidy Up's preview, Lock): its text
	// and buttons, or false.
	struct BannerButton { std::string label; int command; };
	bool banner(std::string& text, std::vector<BannerButton>& buttons) const;

private:
	HWND hwnd = nullptr;
	CLDocument* doc;
	std::string path;
	bool forceDirty = false;          // unsaved, though the engine's copy says otherwise (a failed save, a recovery copy)
	// The recovery copy (Recovery.cpp): written now and then while there are
	// unsaved changes, removed on a clean close.
	std::string recoveryBase;
	std::string recoveredName;
	unsigned changes = 0, changesAtRecovery = 0;
	double lastRecovery = 0;
	void writeRecovery();

	// The parts around the canvas.
	Toolbar* toolbar = nullptr;
	TabStrip* strips[2] = { nullptr, nullptr };
	FindBar* findBar = nullptr;
	TabSwitcher* switcher = nullptr;
	std::vector<uint64_t> recentKeys;   // pages by when they were last in front, most recent first
	HWND paletteHost = nullptr;
	StatusBar* statusBar = nullptr;   // where notes appear
	GatePalette* palette = nullptr;
	MiniMap* miniMap = nullptr;
	ScopeWindow* scope = nullptr;
	double openingAt = -1;            // when the opening card began (-1: none)
	bool openingRevealed = false;
	void beginOpening();
public:
	ScopeWindow* scopeWindow() const { return scope; }
	Toolbar* toolbarWidget() const { return toolbar; }   // for --click-test
	GatePalette* paletteWidget() const { return palette; }   // and its drag
	std::string pageName(int page) const;
private:
	std::vector<Canvas*> canvases;    // in tab order
	int current = 0;                  // the tab in front (of the side you're in)
	// Split view.
	std::set<uint64_t> sideKeys;      // the second side's pages; empty: no split
	std::set<uint64_t> closedSideKeys;   // the second side's pages that closed: back there when they're reopened
	bool sideFirst = false;           // the second side sits on the left
	int focusPane = 0;
	uint64_t frontKeys[2] = { 0, 0 }; // the page each side shows
	double splitAt = 0.5;             // the line between the sides, across the area
	DropHint hint, hintShown;
	HWND hintWindow = nullptr;        // "Drop to split here", over the canvases
	double hintFrom = 0, hintTo = 0, hintStart = -1;
	// Focus mode: the bars sliding away (0 shown, 1 away).
	bool focusOn = false;
	double focusFrom = 0, focusTo = 0, focusStart = -1;
	// The oscilloscope, docked under the canvases.
	bool scopeOpen = false;
	int scopeHeight = 230;            // points
	// The lines that drag: 1 the side panel's edge, 2 between the sides, 3 the oscilloscope's top.
	int dividerDrag = 0;
	int dividerGrab = 0;
	// Where things are, as last laid out (client pixels).
	int sashX = -1, splitX = -1, scopeY = -1, areaLeft = 0, areaTop = 0, areaBottom = 0, contentBottom = 0, captionBottom = 0;
	RECT paneRects[2] = {};
	bool maxPressed = false;          // the drawn maximize button, held down
	bool openMaximized = false;       // shown maximized (as the last window was)
	bool trackingNonClient = false;
	HWND savedFocus = nullptr;        // what had the keyboard when the window was last active
	UINT dpi = 96;

	bool isRunning = true;
	bool simViewOn = false;
	bool lockedOn = false;
	double phase = 0;                 // Simulation View's marching dashes, in points
	double lastTick = 0, lastStatus = 0, lastTitle = 0, lastScope = 0;   // seconds (nowSeconds)
	bool statusDirty = true;
	double pointerX = 0, pointerY = 0;
	std::string pendingGate;
	double selectionChangedAt = 0, appearStart = 0, dragFadeStart = 0;
	std::string selectionSignature;
	bool hasDragFade = false;
	double fadeL = 0, fadeB = 0, fadeR = 0, fadeT = 0;
	int lastPageCount = 1;
	uint64_t tidyKey = 0;             // the page Tidy Up's preview is on
	std::map<uint64_t, bool> seenPages;
	HMENU menus = nullptr, recentMenu = nullptr;
	std::vector<std::string> recentPaths;   // Open Recent's circuits, as the menu shows them

	void build();
	void buildMenus();
	void layout();
	int dividerAt(POINT client) const;
	int toolbarHeight() const;
	int statusHeight() const;   // as much of the status bar as shows (pixels)
	void reconcileSplit();
	void showFronts();
	// After a page closed in a split (closed, or a redo closing it again):
	// the side it was in shows its neighbour there, the other side keeps
	// the page it shows. `at` was its index; `otherFront` the other side's page.
	void showAfterSplitClose(int pane, int at, bool wasCurrent, bool wasFront, uint64_t otherFront);
	double focusAmount() const;
	void stepAnimations();
	double hintAlpha() const;
	void paintHint(double alpha);
	void setMaximizeHot(bool hot, bool pressed);
	TabStrip* rightStrip() const;
	void syncTabs();
	void updateTabLabels();
	void updateTitle();
	void updateStatus();
	void updateActions();
	void updateMenu(HMENU menu);
	void updateRunUI();
	void updateBanner();
	void refreshAfterHistory();
	void tick();
	void pageSwitched();
	bool placePoint(double& wx, double& wy) const;
	void floatSelection(double wx, double wy);
	void pasteText(const std::string& text, bool floating, bool shift);
	std::string displayName() const;
	void rebuildRecentMenu();
	LRESULT frameHitTest(LPARAM lp);
	void renameFile();
	void duplicateCircuit();

	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);

	friend class Canvas;
	friend class ScopeWindow;
	friend void registerWindowClasses();
};

// Register the window classes (once, at startup).
void registerWindowClasses();

#endif  // CL_WINDOWS_WINDOW_H
