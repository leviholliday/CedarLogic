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
#include <string>
#include <vector>

class Canvas;
class GatePalette;
class MiniMap;
class ScopeWindow;
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
	// A released selection box fading out.
	bool dragFadeBox(double& l, double& b, double& r, double& t, double& alpha) const;
	void fadeOutDragBox(double l, double b, double r, double t);
	void statusNeedsUpdate() { statusDirty = true; }

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
	int tabCount() const { return (int)canvases.size(); }
	std::string tabName(int index) const;
	int currentTab() const { return current; }
	void closeTab(int index);
	void moveTab(int from, int to);
	void tabContextMenu(int index, POINT screen);

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
	// A short note shown over the bottom of the canvas, fading after a while.
	bool toast(std::string& text, double& alpha) const;
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
	TabStrip* tabStrip = nullptr;
	FindBar* findBar = nullptr;
	TabSwitcher* switcher = nullptr;
	std::vector<uint64_t> recentKeys;   // pages by when they were last in front, most recent first
	HWND paletteHost = nullptr;
	HWND statusBar = nullptr;
	GatePalette* palette = nullptr;
	MiniMap* miniMap = nullptr;
	ScopeWindow* scope = nullptr;
	std::vector<Canvas*> canvases;    // in tab order
	int current = 0;                  // the tab in front
	bool splitterDrag = false;
	int splitterGrab = 0;
	bool maxPressed = false;          // the drawn maximize button, held down
	bool trackingNonClient = false;
	UINT dpi = 96;
	std::string noteText;             // the toast

	bool isRunning = true;
	bool simViewOn = false;
	bool lockedOn = false;
	double phase = 0;                 // Simulation View's marching dashes, in points
	double lastTick = 0, lastStatus = 0, lastTitle = 0, lastScope = 0;   // seconds (nowSeconds)
	bool statusDirty = true;
	double pointerX = 0, pointerY = 0;
	std::string pendingGate;
	double selectionChangedAt = 0, appearStart = 0, dragFadeStart = 0, messageAt = 0;
	std::string selectionSignature;
	bool hasDragFade = false;
	double fadeL = 0, fadeB = 0, fadeR = 0, fadeT = 0;
	int lastPageCount = 1;
	std::map<uint64_t, bool> seenPages;
	HMENU menus = nullptr, recentMenu = nullptr;

	void build();
	void buildMenus();
	void layout();
	RECT splitterRect() const;
	int toolbarHeight() const;
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
	std::string pageName(int page) const;
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
