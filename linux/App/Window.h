// A circuit window: the menus and toolbar, the gate palette, a tab for each
// page (each its own canvas) and the status bar, around one open circuit.
// It runs that circuit's simulation clock while it's open, and every command
// -- menus, toolbar, the canvas's keys -- comes through here (the Mac app's
// CanvasController).

#ifndef CL_LINUX_WINDOW_H
#define CL_LINUX_WINDOW_H

#include "App.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Canvas;
class GatePalette;
class ScopeWindow;

class CircuitWindow {
public:
	CircuitWindow(GtkApplication* app, CLDocument* doc, const std::string& path);
	~CircuitWindow();

	GtkWindow* window() const { return GTK_WINDOW(win); }
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
	GtkWidget* runButtonForTour() const { return runButton; }
	GtkWidget* tabStripForTour() const { return notebook; }
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

	// ---- Commands ----
	void newCircuit();
	void open();
	bool save();
	bool saveAs();
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
	void writeRecovery();

	GtkWidget* notebook = nullptr;
	GtkWidget* paned = nullptr;
	GtkWidget* paletteBox = nullptr;
	GtkWidget* statusBar = nullptr;
	GtkWidget* statusMessage = nullptr;
	GtkWidget* statusInfo = nullptr;
	GtkWidget* runButton = nullptr;
	GtkWidget* simViewButton = nullptr;
	GtkWidget* stepSpin = nullptr;
	GtkWidget* banner = nullptr;      // Tidy Up's keep/undo bar, Simulation View's
	GtkWidget* bannerLabel = nullptr;
	GtkWidget* bannerButtons = nullptr;
	GMenu* recentMenu = nullptr;
	GatePalette* palette = nullptr;
	class MiniMap* miniMap = nullptr;
	ScopeWindow* scope = nullptr;
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
	std::string selectionSignature;
	bool hasDragFade = false;
	double fadeL = 0, fadeB = 0, fadeR = 0, fadeT = 0;
	int lastPageCount = 1;
	std::map<uint64_t, bool> seenPages;

	void build();
	void buildMenus();
	GtkWidget* buildToolbar();
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
	std::string pageName(int page) const;

	static gboolean tickCb(gpointer);
	static gboolean deleteCb(GtkWidget*, GdkEvent*, gpointer);
	static void destroyCb(GtkWidget*, gpointer);
	static void switchPageCb(GtkNotebook*, GtkWidget*, guint, gpointer);
	static void reorderCb(GtkNotebook*, GtkWidget*, guint, gpointer);
	static gboolean keyCb(GtkWidget*, GdkEventKey*, gpointer);
	static gboolean stateCb(GtkWidget*, GdkEventWindowState*, gpointer);
	static void sizeCb(GtkWidget*, GdkRectangle*, gpointer);
	static void clipboardCb(GtkClipboard*, const gchar*, gpointer);

	friend class Canvas;
	friend class ScopeWindow;
};

#endif  // CL_LINUX_WINDOW_H
