// A circuit window (see Window.h).

#include "Window.h"
#include "Canvas.h"
#include "Dialogs.h"
#include "Palette.h"
#include "Recovery.h"
#include "TabStrip.h"
#include "FindBar.h"
#include "TabSwitcher.h"
#include "Toolbar.h"
#include "Updater.h"
#include "Welcome.h"
#include "Feedback.h"
#include "Help.h"
#include "Library.h"
#include "Collections.h"
#include "LibraryWindow.h"
#include "Chrome.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const wchar_t* kClass = L"CedarLogicWindow";
const UINT_PTR kClockTimer = 1, kAutosaveTimer = 2, kSwitchTimer = 3;
const double kSelectionFadeTime = 0.13, kAppearTime = 0.32, kDragFadeTime = 0.18, kNoteTime = 4.0;

double since(double t) { return nowSeconds() - t; }

const std::vector<FileFilter> kCdlFilters = { { "CedarLogic circuits (*.cdl)", "*.cdl" }, { "All files", "*.*" } };

bool isCircuitWindow(CircuitWindow* w) {
	const std::vector<CircuitWindow*>& all = circuitWindows();
	return std::find(all.begin(), all.end(), w) != all.end();
}

// A menu item: the command, and the label with its shortcut after a tab.
void item(HMENU m, int command, const char* label) { AppendMenuW(m, MF_STRING, command, W(label).c_str()); }
void separator(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
HMENU submenu(HMENU parent, const char* label) {
	HMENU m = CreatePopupMenu();
	AppendMenuW(parent, MF_POPUP, (UINT_PTR)m, W(label).c_str());
	return m;
}

}  // namespace

void registerWindowClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = CircuitWindow::proc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
	wc.hIconSm = (HICON)LoadImageW(appInstance(), MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
	                               GetSystemMetrics(SM_CYSMICON), 0);
	wc.lpszClassName = kClass;
	RegisterClassExW(&wc);
	registerCanvasClass();
	registerPaletteClasses();
	registerDialogClasses();
}

// ---- Building the window -----------------------------------------------------------

CircuitWindow::CircuitWindow(CLDocument* d, const std::string& p) : doc(d), path(p) {
	isRunning = cl_document_is_running(doc);
	circuitWindows().push_back(this);
	openMaximized = prefs().windowMaximized;   // before build() sizes the hidden window
	build();
	syncTabs();
	appearStart = nowSeconds();
	lastTick = nowSeconds();
	lastRecovery = lastTick;
	SetTimer(hwnd, kClockTimer, 15, nullptr);
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	layout();
	// While the launch screen is up, windows wait for it (it does the
	// introducing); otherwise the circuit comes in under its opening card.
	if (!splash::active()) {
		beginOpening();
		present();
	}
}

// The card plays once the window is in place and drawn.
void CircuitWindow::beginOpening() {
	openingAt = nowSeconds() + 0.08;
	openingRevealed = false;
}

double CircuitWindow::cardFreeze = -1;

bool CircuitWindow::openingCard(double& t) const {
	if (cardFreeze >= 0) { t = cardFreeze; return true; }
	if (openingAt < 0) return false;
	t = nowSeconds() - openingAt;
	return true;
}

std::string CircuitWindow::openingDetail() const {
	const int tabs = cl_document_page_count(doc);
	long gates = 0;
	for (int p = 0; p < tabs; p++) gates += cl_document_gate_count(doc, p);
	if (gates == 0) return tabs > 1 ? strf("%d empty tabs", tabs) : std::string("A blank page, ready to build");
	return strf("%d tab%s  \u00B7  %ld gate%s", tabs, tabs == 1 ? "" : "s", gates, gates == 1 ? "" : "s");
}

void CircuitWindow::present() {
	if (IsWindowVisible(hwnd)) return;
	ShowWindow(hwnd, openMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
	UpdateWindow(hwnd);
	appearStart = nowSeconds();
	if (Canvas* c = currentCanvas()) c->focus();
}

RECT CircuitWindow::tourAnchor(int which) const {
	RECT r = { 0, 0, 0, 0 };
	switch (which) {
	case 0: if (paletteHost && IsWindowVisible(paletteHost)) { GetWindowRect(paletteHost, &r); return r; } break;
	case 2: if (toolbar) return toolbar->commandRect(CMD_RUNNING); break;
	case 3: if (tabStrip) { GetWindowRect(tabStrip->widget(), &r); return r; } break;
	default: break;
	}
	if (Canvas* c = currentCanvas()) GetWindowRect(c->widget(), &r);
	return r;
}

CircuitWindow::~CircuitWindow() {
	delete scope;
	scope = nullptr;
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	delete palette;
	palette = nullptr;
	miniMap = nullptr;
	delete toolbar;
	toolbar = nullptr;
	delete tabStrip;
	tabStrip = nullptr;
	delete findBar;
	findBar = nullptr;
	delete switcher;
	switcher = nullptr;
	if (menus) DestroyMenu(menus);
	std::vector<CircuitWindow*>& all = circuitWindows();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
	// Closed on purpose (saved, or the changes let go): no copy to offer back.
	recovery::remove(recoveryBase);
	if (doc) cl_document_close(doc);
}

void CircuitWindow::build() {
	// Placed a little down and right of the window in front, or where
	// Windows likes.
	const UINT sysDpi = GetDpiForSystem();
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	if (circuitWindows().size() > 1) {
		CircuitWindow* before = circuitWindows()[circuitWindows().size() - 2];
		RECT r;
		if (before->hwnd && GetWindowRect(before->hwnd, &r)) { x = r.left + scaled(28, sysDpi); y = r.top + scaled(28, sysDpi); }
	}
	hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClass, L"CedarLogic", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y,
	                       scaled(prefs().windowWidth, sysDpi), scaled(prefs().windowHeight, sysDpi), nullptr, nullptr,
	                       appInstance(), this);
	dpi = dpiOf(hwnd);
	// Now it's on its monitor: the size in that monitor's pixels.
	if (dpi != sysDpi) {
		RECT r;
		GetWindowRect(hwnd, &r);
		SetWindowPos(hwnd, nullptr, 0, 0, scaled(prefs().windowWidth, dpi), scaled(prefs().windowHeight, dpi),
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	// On its screen, and no bigger (a size kept from a bigger one): the
	// window's own buttons are at the top right.
	RECT r;
	MONITORINFO mi = { sizeof mi };
	if (GetWindowRect(hwnd, &r) && GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
		const RECT& a = mi.rcWork;
		const int w = std::min<int>(r.right - r.left, a.right - a.left), h = std::min<int>(r.bottom - r.top, a.bottom - a.top);
		const int x = std::max<int>(a.left, std::min<int>(r.left, a.right - w));
		const int y = std::max<int>(a.top, std::min<int>(r.top, a.bottom - h));
		if (x != r.left || y != r.top || w != r.right - r.left || h != r.bottom - r.top)
			SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	setDarkTitleBar(hwnd, prefs().dark);
	buildMenus();
	toolbar = new Toolbar(this, hwnd);
	palette = new GatePalette(this, hwnd);
	paletteHost = palette->widget();
	miniMap = palette->miniMap();
	tabStrip = new TabStrip(this, hwnd);
	findBar = new FindBar(this, hwnd);
	switcher = new TabSwitcher(this);
	statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | SBARS_SIZEGRIP, 0, 0, 10, 10, hwnd, nullptr,
	                            appInstance(), nullptr);
	setFontTree(hwnd, uiFont(dpi));
	palette->dpiChanged();
	ShowWindow(paletteHost, prefs().showPalette ? SW_SHOW : SW_HIDE);
	ShowWindow(statusBar, prefs().showStatus ? SW_SHOW : SW_HIDE);
	// The title bar is the toolbar's: tell Windows the frame changed.
	SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Every menu, in one popup: the toolbar's •••, and Alt or F10. (The window
// has no menu bar: its title bar is the toolbar.)
void CircuitWindow::buildMenus() {
	menus = CreatePopupMenu();
	HMENU menuBar = menus;
	HMENU file = submenu(menuBar, "&File");
	item(file, CMD_NEW, "&New\tCtrl+N");
	item(file, CMD_NEW_TEMPLATE, "New from &Template\u2026");
	item(file, CMD_OPEN, "Your &Circuits\u2026\tCtrl+O");
	recentMenu = submenu(file, "Open &Recent");
	item(file, CMD_IMPORT, "&Import a File\u2026\tCtrl+I");
	item(file, CMD_OPEN_SAMPLE, "Open the &Practice Circuit");
	separator(file);
	item(file, CMD_SAVE, "&Save a Version\tCtrl+S");
	item(file, CMD_VERSIONS, "&Version History\u2026");
	item(file, CMD_SAVE_AS, "E&xport\u2026\tCtrl+Shift+S");
	item(file, CMD_SAVE_TEMPLATE, "Save as Te&mplate\u2026");
	item(file, CMD_EXPORT_IMAGE, "&Export as Image…\tCtrl+E");
	HMENU older = submenu(file, "Export for &Older CedarLogic");
	item(older, CMD_EXPORT_V2, "For CedarLogic &2…");
	item(older, CMD_EXPORT_V1, "For CedarLogic &1.x…");
	item(file, CMD_PRINT, "&Print…\tCtrl+P");
	separator(file);
	item(file, CMD_CLOSE_WINDOW, "&Close Window\tCtrl+Shift+W");
	item(file, CMD_QUIT, "E&xit\tCtrl+Q");

	// Bare-key shortcuts (R, S, T...) are the canvas's own; the labels only
	// mention them.
	HMENU edit = submenu(menuBar, "&Edit");
	item(edit, CMD_UNDO, "&Undo\tCtrl+Z");
	item(edit, CMD_REDO, "&Redo\tCtrl+Y");
	separator(edit);
	item(edit, CMD_CUT, "Cu&t\tCtrl+X");
	item(edit, CMD_COPY, "&Copy\tCtrl+C");
	item(edit, CMD_PASTE, "&Paste\tCtrl+V");
	item(edit, CMD_DUPLICATE, "D&uplicate\tCtrl+D");
	item(edit, CMD_DELETE, "&Delete\tDel");
	item(edit, CMD_SELECT_ALL, "Select &All\tCtrl+A");
	item(edit, CMD_FIND, "&Find\u2026\tCtrl+F");
	separator(edit);
	item(edit, CMD_ADD_GATE, "Add a &Gate…\tA");
	item(edit, CMD_GATE_SETTINGS, "Gate &Settings…");
	item(edit, CMD_ROTATE, "R&otate\tR");
	item(edit, CMD_STRAIGHTEN, "Straighten &Wires\tS");
	item(edit, CMD_TIDY, "T&idy Up\tShift+S");
	item(edit, CMD_TIDY_FLOW, "Tidy Up by Signal &Flow");
	item(edit, CMD_CONNECT_NEARBY, "Connect &Nearby Pins");
	item(edit, CMD_SAVE_PART, "Save as &Part\u2026");
	item(edit, CMD_BUILD_FORMULA, "&Build from Formula\u2026");
	separator(edit);
	item(edit, CMD_PREFERENCES, "Pr&eferences…\tCtrl+,");

	HMENU view = submenu(menuBar, "&View");
	item(view, CMD_ZOOM_IN, "Zoom &In\tCtrl+=");
	item(view, CMD_ZOOM_OUT, "Zoom &Out\tCtrl+-");
	item(view, CMD_ZOOM_FIT, "Zoom to &Fit\tCtrl+0");
	item(view, CMD_ZOOM_ACTUAL, "&Actual Size\tCtrl+1");
	separator(view);
	item(view, CMD_DARK, "&Dark Mode\tCtrl+Shift+D");
	item(view, CMD_PALETTE, "Gate &Palette\tCtrl+.");
	item(view, CMD_STATUS_BAR, "&Status Bar");

	HMENU sim = submenu(menuBar, "&Simulate");
	item(sim, CMD_RUNNING, "&Running");
	item(sim, CMD_STEP, "&Step Once\tCtrl+Shift+R");
	item(sim, CMD_SIM_VIEW, "Simulation &View\tCtrl+R");
	item(sim, CMD_LOCK, "&Lock the Circuit");
	separator(sim);
	item(sim, CMD_TRUTH_TABLE, "&Truth Table\tT");
	item(sim, CMD_SCOPE, "&Oscilloscope\tCtrl+G");

	HMENU tabsMenu = submenu(menuBar, "&Tabs");
	item(tabsMenu, CMD_NEW_TAB, "&New Tab\tCtrl+T");
	item(tabsMenu, CMD_CLOSE_TAB, "&Close Tab\tCtrl+W");
	item(tabsMenu, CMD_REOPEN_TAB, "&Reopen Closed Tab\tCtrl+Shift+T");
	item(tabsMenu, CMD_RENAME_TAB, "Re&name Tab…");
	item(tabsMenu, CMD_NEXT_TAB, "Ne&xt Tab\tCtrl+Tab");
	item(tabsMenu, CMD_PREVIOUS_TAB, "&Previous Tab\tCtrl+Shift+Tab");

	HMENU help = submenu(menuBar, "&Help");
	item(help, CMD_SHORTCUTS, "&Keyboard Shortcuts\t?");
	item(help, CMD_TOUR, "Guided &Tour");
	item(help, CMD_HELP, "CedarLogic &Help\tF1");
	item(help, CMD_WHATS_NEW, "&What's New in CedarLogic");
	separator(help);
	item(help, CMD_FEEDBACK, "Send &Feedback\u2026");
	item(help, CMD_CHECK_UPDATES, "Check for &Updates\u2026");
	item(help, CMD_ABOUT, "&About CedarLogic");
}

int CircuitWindow::toolbarHeight() const { return (int)std::lround(Toolbar::barHeight() * dpi / 96.0); }

// The line between the side panel and the canvas, and a few pixels either
// side of it to grab.
RECT CircuitWindow::splitterRect() const {
	RECT r = { 0, 0, 0, 0 };
	if (!prefs().showPalette) return r;
	RECT rc;
	GetClientRect(hwnd, &rc);
	RECT sb = { 0, 0, 0, 0 };
	if (prefs().showStatus && statusBar) GetWindowRect(statusBar, &sb);
	const int x = scaled(prefs().paletteWidth, dpi);
	r = { x - scaled(3, dpi), toolbarHeight(), x + 1 + scaled(3, dpi), rc.bottom - (sb.bottom - sb.top) };
	return r;
}

void CircuitWindow::layout() {
	if (hwnd == nullptr || toolbar == nullptr || tabStrip == nullptr) return;
	RECT rc;
	GetClientRect(hwnd, &rc);
	auto sc = [&](int v) { return scaled(v, dpi); };
	HDWP defer = BeginDeferWindowPos(16);
	auto place = [&](HWND h, int x, int y, int w, int hh) {
		if (h) defer = DeferWindowPos(defer, h, nullptr, x, y, std::max(0, w), std::max(0, hh), SWP_NOZORDER | SWP_NOACTIVATE);
	};
	const int top = toolbarHeight();
	place(toolbar->widget(), 0, 0, rc.right, top);

	// The status bar (off unless asked for) sizes itself along the bottom.
	int bottom = rc.bottom;
	if (prefs().showStatus) {
		SendMessageW(statusBar, WM_SIZE, 0, 0);
		RECT sb;
		GetWindowRect(statusBar, &sb);
		bottom -= sb.bottom - sb.top;
		const int infoW = std::min<int>(sc(460), rc.right / 2);
		int parts[2] = { rc.right - infoW, -1 };
		SendMessageW(statusBar, SB_SETPARTS, 2, (LPARAM)parts);
	}

	// The side panel, a hairline, then the tabs and the canvas.
	int left = 0;
	if (prefs().showPalette) {
		const int pw = std::min<int>(sc(prefs().paletteWidth), std::max<int>(sc(140), rc.right - sc(240)));
		place(paletteHost, 0, top, pw, bottom - top);
		left = pw + 1;
	}
	const int tabH = (int)std::lround(TabStrip::stripHeight() * dpi / 96.0);
	place(tabStrip->widget(), left, top, rc.right - left, tabH);
	for (Canvas* c : canvases) place(c->widget(), left, top + tabH, rc.right - left, bottom - top - tabH);
	if (findBar) {
		// Over the top of the canvas, in the middle.
		const int fw = std::min<int>((int)std::lround(FindBar::barWidth() * dpi / 96.0), rc.right - left - sc(24));
		const int fh = (int)std::lround(FindBar::barHeight() * dpi / 96.0);
		place(findBar->widget(), left + (rc.right - left - fw) / 2, top + tabH + sc(12), fw, fh);
	}
	EndDeferWindowPos(defer);
	if (findBar) SetWindowPos(findBar->widget(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	toolbar->layoutNow();
	InvalidateRect(hwnd, nullptr, TRUE);
}

// ---- Tabs ------------------------------------------------------------------------

std::string CircuitWindow::pageName(int page) const {
	const char* n = cl_document_page_name(doc, page);
	if (n && *n) return n;
	return strf("Page %d", page + 1);
}

std::string CircuitWindow::tabName(int index) const {
	if (index < 0 || index >= (int)canvases.size()) return std::string();
	const int p = canvases[index]->page();
	return p >= 0 ? pageName(p) : std::string();
}

void CircuitWindow::updateTabLabels() {
	if (tabStrip) tabStrip->redraw();
}

// Make the tabs match the document's pages: after opening, a new page, a
// close, an undo that brings one back, a move.
void CircuitWindow::syncTabs() {
	Canvas* front = currentCanvas();
	const uint64_t frontKey = front ? front->pageKey() : 0;
	const int n = cl_document_page_count(doc);
	std::vector<Canvas*> want;
	for (int i = 0; i < n; i++) {
		const uint64_t key = cl_document_page_id(doc, i);
		Canvas* found = nullptr;
		for (Canvas* c : canvases) if (c->pageKey() == key) found = c;
		if (found == nullptr) found = new Canvas(this, hwnd, key);
		want.push_back(found);
	}
	for (Canvas* c : canvases)
		if (std::find(want.begin(), want.end(), c) == want.end()) delete c;
	canvases = want;
	current = 0;
	for (int i = 0; i < n; i++) if (canvases[i]->pageKey() == frontKey) current = i;
	for (int i = 0; i < n; i++) canvases[i]->show(i == current);
	lastPageCount = n;
	updateTabLabels();
	layout();
	redrawMiniMap();
}

Canvas* CircuitWindow::currentCanvas() const {
	return current >= 0 && current < (int)canvases.size() ? canvases[current] : nullptr;
}

int CircuitWindow::currentPage() const {
	Canvas* c = currentCanvas();
	const int p = c ? c->page() : -1;
	return p >= 0 ? p : 0;
}

void CircuitWindow::showPage(int index) {
	if (index < 0 || index >= (int)canvases.size() || index == current) return;
	// Leaving a page lets go of its selection, as the wx app does.
	if (Canvas* old = currentCanvas()) {
		if (old->page() >= 0) {
			old->cancelDrag();
			cl_edit_select_none(doc, old->page());
		}
	}
	current = index;
	pageSwitched();
}

void CircuitWindow::pageSwitched() {
	Canvas* front = currentCanvas();
	if (front) {
		recentKeys.erase(std::remove(recentKeys.begin(), recentKeys.end(), front->pageKey()), recentKeys.end());
		recentKeys.insert(recentKeys.begin(), front->pageKey());
	}
	for (Canvas* c : canvases) c->show(c == front);
	statusDirty = true;
	selectionSignature.clear();
	if (front) {
		front->focus();
		if (!seenPages[front->pageKey()]) { seenPages[front->pageKey()] = true; appearStart = nowSeconds(); }
		front->redraw();
	}
	updateActions();
	updateTitle();
	updateTabLabels();
	redrawMiniMap();
}

void CircuitWindow::closeTab(int index) {
	if (index < 0 || index >= (int)canvases.size()) return;
	const int p = canvases[index]->page();
	if (p >= 0) closePage(p);
}

void CircuitWindow::moveTab(int from, int to) {
	const int n = cl_document_page_count(doc);
	if (from < 0 || to < 0 || from >= n || to >= n || from == to) return;
	// A tab without a name of its own is called by its place ("Page 2"): pin
	// those names first, so moving a tab doesn't rename the others.
	for (int i = 0; i < n; i++) {
		const char* name = cl_document_page_name(doc, i);
		if (name == nullptr || *name == 0) cl_document_rename_page(doc, i, pageName(i).c_str());
	}
	cl_document_move_page(doc, from, to);
	changes++;
	syncTabs();
	updateTitle();
}

void CircuitWindow::tabContextMenu(int index, POINT screen) {
	if (index >= 0) showPage(index);
	HMENU m = CreatePopupMenu();
	item(m, CMD_RENAME_TAB, "Re&name Tab\u2026");
	item(m, CMD_CLOSE_TAB, "&Close Tab\tCtrl+W");
	item(m, CMD_REOPEN_TAB, "&Reopen Closed Tab\tCtrl+Shift+T");
	separator(m);
	const int n = (int)canvases.size();
	AppendMenuW(m, MF_STRING | (current > 0 ? 0 : MF_GRAYED), 1, L"Move &Left");
	AppendMenuW(m, MF_STRING | (current < n - 1 ? 0 : MF_GRAYED), 2, L"Move Ri&ght");
	separator(m);
	item(m, CMD_NEW_TAB, "&New Tab\tCtrl+T");
	updateMenu(m);
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
	DestroyMenu(m);
	if (cmd == 1 || cmd == 2) moveTab(current, cmd == 1 ? current - 1 : current + 1);
	else if (cmd) run(cmd);
}

// ---- The clock -------------------------------------------------------------------

void CircuitWindow::tick() {
	const double t = nowSeconds();
	const double elapsed = (t - lastTick) * 1000.0;   // ms
	lastTick = t;
	Canvas* c = currentCanvas();
	if (c) c->stepAnimation();
	// The opening card: the circuit fades up as it lifts away.
	if (openingAt >= 0) {
		const double ot = t - openingAt;
		if (ot >= 0.58 && !openingRevealed) { openingRevealed = true; appearStart = t; }
		if (ot >= 0.88) openingAt = -1;
		redraw();
	}
	// Fades in progress (a new selection's halo, a page appearing, the drag box).
	if (since(selectionChangedAt) < kSelectionFadeTime || since(appearStart) < kAppearTime ||
	    (hasDragFade && since(dragFadeStart) < kDragFadeTime))
		redraw();
	if (hasDragFade && since(dragFadeStart) >= kDragFadeTime) { hasDragFade = false; redraw(); }

	// Simulation View's dashes march at the simulation's speed: 40 points a
	// second at 25 ms a step, faster as steps get shorter.
	if (simViewOn && isRunning) {
		const int ms = std::max(1, cl_document_step_ms(doc));
		const double pps = std::min(240.0, std::max(8.0, 40 * std::sqrt(25.0 / ms)));
		phase += std::min(0.05, elapsed / 1000) * pps;
		redraw();
	}
	if (isRunning) {
		const int r = cl_document_tick(doc, elapsed);
		// Redraw only when a step changed something that shows: a running
		// circuit with nothing moving costs next to nothing.
		if (r & CL_TICK_SHOWN) redraw();
		// The oscilloscope's traces grow with every step, changes or not.
		if ((r & CL_TICK_CHANGED) && scope && scope->visible() && (t - lastScope) > 1.0 / 15) {
			lastScope = t;
			scope->update();
		}
		if (r & CL_TICK_PAUSED) { isRunning = false; updateRunUI(); note("A part paused the simulation."); }
	}
	if (statusDirty && (t - lastStatus) > 0.1) { statusDirty = false; lastStatus = t; updateStatus(); }
	if ((t - lastTitle) > 0.5) { lastTitle = t; updateTitle(); }
	// A recovery copy of unsaved work, at most every 20 seconds.
	if (changes != changesAtRecovery && (t - lastRecovery) > 20) writeRecovery();
	// The note over the canvas fades in, stays a while, fades out.
	if (messageAt > 0) {
		const double age = since(messageAt);
		if (age > kNoteTime) { messageAt = 0; redraw(); }
		else if (age < 0.2 || age > kNoteTime - 0.5) redraw();
	}
}

double CircuitWindow::selectionFade() const {
	return std::min(1.0, std::max(0.0, since(selectionChangedAt) / kSelectionFadeTime));
}

double CircuitWindow::appearProgress() const {
	const double t = std::min(1.0, std::max(0.0, since(appearStart) / kAppearTime));
	return 1 - std::pow(1 - t, 3);
}

bool CircuitWindow::dragFadeBox(double& l, double& b, double& r, double& t, double& alpha) const {
	if (!hasDragFade) return false;
	l = fadeL; b = fadeB; r = fadeR; t = fadeT;
	alpha = std::max(0.0, 1 - since(dragFadeStart) / kDragFadeTime);
	return true;
}

void CircuitWindow::fadeOutDragBox(double l, double b, double r, double t) {
	fadeL = l; fadeB = b; fadeR = r; fadeT = t;
	hasDragFade = true;
	dragFadeStart = nowSeconds();
}

// ---- State shown around the canvas -------------------------------------------------

void CircuitWindow::redraw() {
	if (Canvas* c = currentCanvas()) c->redraw();
}

void CircuitWindow::redrawMiniMap() {
	if (miniMap) miniMap->queueDraw();
}

void CircuitWindow::pointerMoved(double wx, double wy) {
	pointerX = wx;
	pointerY = wy;
	statusDirty = true;
}

void CircuitWindow::selectionChanged() {
	// A new selection's halo fades in; clicking what's already selected
	// doesn't restart it.
	const int p = currentPage();
	const std::string sig = strf("%d/%d/%ld", cl_edit_selected_gate_count(doc, p), cl_edit_selected_wire_count(doc, p),
	                             cl_edit_single_gate(doc, p));
	if (sig != selectionSignature) {
		selectionSignature = sig;
		if (sig != "0/0/-1") selectionChangedAt = nowSeconds();
		updateActions();
	}
	statusDirty = true;
}

void CircuitWindow::edited() {
	changes++;
	// Saving as you go: a couple of seconds after the last change.
	SetTimer(hwnd, kAutosaveTimer, 2000, nullptr);
	if (cl_document_page_count(doc) != lastPageCount) syncTabs();
	redraw();
	selectionChanged();
	updateActions();
	updateTitle();
	updateBanner();
	updateTabLabels();
	statusDirty = true;
	if (findBar && findBar->isOpen()) findBar->run(false);
}

void CircuitWindow::note(const std::string& text) {
	noteText = text;
	messageAt = nowSeconds();
	if (prefs().showStatus) SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)W(text).c_str());
	redraw();
}

bool CircuitWindow::toast(std::string& text, double& alpha) const {
	if (messageAt <= 0 || noteText.empty()) return false;
	const double age = since(messageAt);
	if (age > kNoteTime) return false;
	text = noteText;
	alpha = std::min(1.0, std::min(age / 0.2, (kNoteTime - age) / 0.5));
	return alpha > 0;
}

void CircuitWindow::lockNudge() {
	if (simViewOn) note("Leave Simulation View (Escape) to edit.");
	else note("The circuit is locked. Unlock it (Simulate > Lock) to edit.");
	MessageBeep(MB_OK);
}

std::string CircuitWindow::displayName() const {
	library::Item it;
	if (library::itemFor(path, it)) return it.name;
	if (!recoveredName.empty()) return recoveredName;
	if (!path.empty()) return baseName(path);
	return "Untitled";
}

void CircuitWindow::markRecovered(const std::string& name) {
	recoveredName = name;
	forceDirty = true;
	changes++;
	updateTitle();
	note("Brought back from the copy kept when CedarLogic closed. Save it to keep it.");
}

void CircuitWindow::writeRecovery() {
	lastRecovery = nowSeconds();
	changesAtRecovery = changes;
	if (!isDirty()) { recovery::remove(recoveryBase); return; }
	// Writing the text marks the engine's copy saved; it isn't, so say so.
	const std::string text = cl_document_save_text(doc);
	forceDirty = true;
	if (recoveryBase.empty()) recoveryBase = recovery::newBase();
	recovery::write(recoveryBase, text, path, displayName());
}

bool CircuitWindow::isDirty() const { return forceDirty || cl_document_is_edited(doc); }

bool CircuitWindow::isPristine() const {
	if (!path.empty() || isDirty() || cl_document_page_count(doc) != 1) return false;
	return cl_document_gate_count(doc, 0) == 0 && !cl_edit_can_undo(doc);
}

void CircuitWindow::updateTitle() {
	std::string t = displayName();
	if (isDirty()) t = "*" + t;
	if (cl_document_page_count(doc) > 1) t += " - " + pageName(currentPage());
	t += " — " CL_APP_NAME;
	setWindowText(hwnd, t);
}

void CircuitWindow::updateStatus() {
	Canvas* c = currentCanvas();
	const int p = currentPage();
	std::string s = strf("%d gates", cl_document_gate_count(doc, p));
	const int sg = cl_edit_selected_gate_count(doc, p), sw = cl_edit_selected_wire_count(doc, p);
	if (sg + sw > 0) s += strf(" · %d selected", sg + sw);
	if (c) s += strf(" · %d%%", c->zoomPercent());
	s += strf(" · %.1f, %.1f", pointerX, pointerY);
	s += isRunning ? " · Running" : " · Paused";
	if (prefs().showStatus) SendMessageW(statusBar, SB_SETTEXTW, 1, (LPARAM)W(s).c_str());
	if (toolbar) toolbar->redraw();   // the zoom readout
}

bool CircuitWindow::hasSelection() const {
	const int p = currentPage();
	return cl_edit_selected_gate_count(doc, p) + cl_edit_selected_wire_count(doc, p) > 0;
}

bool CircuitWindow::commandEnabled(int command) const {
	const bool edit = canEdit();
	const bool sel = hasSelection();
	switch (command) {
	case CMD_UNDO: return cl_edit_can_undo(doc) && !simViewOn;
	case CMD_REDO: return cl_edit_can_redo(doc) && !simViewOn;
	case CMD_CUT: return edit && sel;
	case CMD_COPY: return sel;
	case CMD_DUPLICATE: return edit && sel;
	case CMD_DELETE: return edit && sel;
	case CMD_ROTATE: return edit && cl_edit_selected_gate_count(doc, currentPage()) > 0;
	case CMD_GATE_SETTINGS: return cl_edit_single_gate(doc, currentPage()) >= 0;
	case CMD_REOPEN_TAB: return cl_edit_undo_is_close_page(doc);
	case CMD_PASTE: case CMD_ADD_GATE: case CMD_STRAIGHTEN: case CMD_TIDY: case CMD_TIDY_FLOW: case CMD_CONNECT_NEARBY:
		return edit;
	default: return true;
	}
}

int CircuitWindow::commandChecked(int command) const {
	switch (command) {
	case CMD_RUNNING: return isRunning;
	case CMD_SIM_VIEW: return simViewOn;
	case CMD_LOCK: return lockedOn;
	case CMD_DARK: return prefs().dark;
	case CMD_PALETTE: return prefs().showPalette;
	case CMD_STATUS_BAR: return prefs().showStatus;
	default: return -1;
	}
}

void CircuitWindow::updateMenu(HMENU menu) {
	const int n = GetMenuItemCount(menu);
	for (int i = 0; i < n; i++) {
		const UINT id = GetMenuItemID(menu, i);
		if (id == (UINT)-1 || id == 0 || (id >= CMD_RECENT && id <= CMD_RECENT_LAST)) continue;
		EnableMenuItem(menu, i, MF_BYPOSITION | (commandEnabled((int)id) ? MF_ENABLED : MF_GRAYED));
		const int check = commandChecked((int)id);
		if (check >= 0) CheckMenuItem(menu, i, MF_BYPOSITION | (check ? MF_CHECKED : MF_UNCHECKED));
	}
	if (menu == recentMenu) rebuildRecentMenu();
}

void CircuitWindow::updateActions() {
	if (toolbar) toolbar->redraw();
}

void CircuitWindow::updateRunUI() {
	updateActions();
	updateTabLabels();
	statusDirty = true;
}

int CircuitWindow::stepMs() const { return cl_document_step_ms(doc); }

bool CircuitWindow::banner(std::string& text, std::vector<BannerButton>& buttons) const {
	buttons.clear();
	if (cl_edit_tidy_active(doc)) {
		text = cl_edit_tidy_mode(doc) == 1 ? "Tidy Up by signal flow: a preview. Enter keeps it, Esc puts it back."
		                                   : "Tidy Up: a preview. Enter keeps it, Esc puts it back.";
		buttons = { { "Keep", CMD_TIDY_KEEP }, { "Put Back", CMD_TIDY_REVERT }, { "Other Way", CMD_TIDY_SWITCH } };
		return true;
	}
	if (lockedOn && !simViewOn) {
		text = "Locked: switches still work; nothing else changes.";
		buttons = { { "Unlock", CMD_LOCK } };
		return true;
	}
	return false;
}

// The banner and Simulation View's bar are drawn on the canvas.
void CircuitWindow::updateBanner() { redraw(); }

void CircuitWindow::themeChanged() {
	setDarkTitleBar(hwnd, prefs().dark);
	updateActions();
	if (tabStrip) tabStrip->redraw();
	if (palette) palette->themeChanged();
	for (Canvas* c : canvases) c->redraw();
	redrawMiniMap();
	if (scope) scope->update();
	InvalidateRect(hwnd, nullptr, TRUE);
}

void CircuitWindow::prefsChanged() {
	ShowWindow(paletteHost, prefs().showPalette ? SW_SHOW : SW_HIDE);
	ShowWindow(statusBar, prefs().showStatus ? SW_SHOW : SW_HIDE);
	layout();
	themeChanged();
}

void CircuitWindow::showPaletteCategory(int index) {
	if (!prefs().showPalette) togglePalette();
	palette->showCategory(index);
}

// ---- Window messages -------------------------------------------------------------

LRESULT CALLBACK CircuitWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_NCCREATE) {
		CircuitWindow* w = static_cast<CircuitWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
		w->hwnd = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
	}
	CircuitWindow* w = reinterpret_cast<CircuitWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (w == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("the window", [&] { r = w->handle(msg, wp, lp); ok = true; });
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

// Where a point on the window is, now that the toolbar is its title bar:
// the resize edges, the drag area, the maximize button (as Windows' own, for
// snap layouts) or the inside.
LRESULT CircuitWindow::frameHitTest(LPARAM lp) {
	const LRESULT def = DefWindowProcW(hwnd, WM_NCHITTEST, 0, lp);
	if (def != HTCLIENT) return def;   // the left, right and bottom edges
	POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	ScreenToClient(hwnd, &p);
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int edge = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
	if (!IsZoomed(hwnd) && p.y < edge) {
		if (p.x < edge * 2) return HTTOPLEFT;
		if (p.x >= rc.right - edge * 2) return HTTOPRIGHT;
		return HTTOP;
	}
	if (p.y < toolbarHeight()) {
		const RECT mx = toolbar ? toolbar->maximizeRect() : RECT{ 0, 0, 0, 0 };
		if (PtInRect(&mx, p)) return HTMAXBUTTON;
		return HTCAPTION;
	}
	return HTCLIENT;
}

LRESULT CircuitWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_NCCALCSIZE:
		if (wp) {
			// No title bar: the toolbar is drawn where it was. The other
			// edges stay Windows' (resizing, the shadow, the rounded corners).
			RECT& r = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0];
			const int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
			const int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
			r.left += fx;
			r.right -= fx;
			r.bottom -= fy;
			// Maximized, the window hangs past the screen by its frame.
			if (IsZoomed(hwnd)) r.top += fy;
			return 0;
		}
		break;
	case WM_NCHITTEST:
		return frameHitTest(lp);
	case WM_NCACTIVATE:
		// Don't let Windows paint the old title bar over ours.
		if (toolbar) toolbar->redraw();
		return DefWindowProcW(hwnd, msg, wp, -1);
	case WM_NCMOUSEMOVE:
		if (!trackingNonClient) {
			trackingNonClient = true;
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
			TrackMouseEvent(&t);
		}
		if (toolbar) toolbar->setMaximizeHot(wp == HTMAXBUTTON, maxPressed && wp == HTMAXBUTTON);
		if (wp == HTMAXBUTTON) return 0;
		break;
	case WM_NCMOUSELEAVE:
		trackingNonClient = false;
		maxPressed = false;
		if (toolbar) toolbar->setMaximizeHot(false, false);
		break;
	case WM_NCLBUTTONDOWN:
		if (wp == HTMAXBUTTON) {
			maxPressed = true;
			if (toolbar) toolbar->setMaximizeHot(true, true);
			return 0;
		}
		break;
	case WM_NCLBUTTONUP:
		if (wp == HTMAXBUTTON) {
			const bool was = maxPressed;
			maxPressed = false;
			if (toolbar) toolbar->setMaximizeHot(true, false);
			if (was) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
			return 0;
		}
		maxPressed = false;
		break;
	case WM_SYSCOMMAND:
		// Alt or F10 on its own: every menu, from the toolbar's •••.
		if ((wp & 0xFFF0) == SC_KEYMENU && lp != VK_SPACE) {
			RECT rc;
			GetClientRect(hwnd, &rc);
			POINT p = { rc.right - scaled(150, dpi), toolbarHeight() };
			ClientToScreen(hwnd, &p);
			moreMenu(p, true);
			return 0;
		}
		break;
	case WM_TIMER:
		if (wp == kClockTimer) tick();
		if (wp == kSwitchTimer) {
			if (switcher) switcher->tick();
			if (!switcher || !switcher->active()) KillTimer(hwnd, kSwitchTimer);
		}
		if (wp == kAutosaveTimer) {
			KillTimer(hwnd, kAutosaveTimer);
			// Not in the middle of something (a drag, a gate on the pointer,
			// Tidy Up's preview): then a moment later.
			Canvas* c = currentCanvas();
			const bool busy = (c && c->isDragging()) || isFloating() || tidyActive() || cl_edit_is_connecting(doc);
			if (busy) SetTimer(hwnd, kAutosaveTimer, 1000, nullptr);
			else if (isDirty()) saveQuietly(false);
		}
		return 0;
	case WM_SIZE:
		if (wp == SIZE_MINIMIZED) return 0;
		// Kept for the next window, once this one is up (the sizes a new
		// window goes through while it's built aren't the user's).
		if (IsWindowVisible(hwnd)) {
			prefs().windowMaximized = wp == SIZE_MAXIMIZED;
			if (wp == SIZE_RESTORED) {
				RECT r;
				GetWindowRect(hwnd, &r);
				prefs().windowWidth = MulDiv(r.right - r.left, 96, (int)dpi);
				prefs().windowHeight = MulDiv(r.bottom - r.top, 96, (int)dpi);
			}
		}
		layout();
		return 0;
	case WM_GETMINMAXINFO: {
		MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
		m->ptMinTrackSize = { scaled(640, dpi), scaled(420, dpi) };
		return 0;
	}
	case WM_DPICHANGED: {
		dpi = HIWORD(wp);
		const RECT* r = reinterpret_cast<const RECT*>(lp);
		SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
		setFontTree(hwnd, uiFont(dpi));
		if (palette) palette->dpiChanged();
		if (findBar) findBar->dpiChanged();
		layout();
		for (Canvas* c : canvases) c->redraw();
		return 0;
	}
	case WM_ACTIVATE:
		if (toolbar) toolbar->redraw();
		if (LOWORD(wp) == WA_INACTIVE) {
			// What had the keyboard, to give it back (by the time the window
			// is active again, Windows has forgotten it).
			const HWND f = GetFocus();
			savedFocus = f && IsChild(hwnd, f) ? f : nullptr;
			break;
		}
		library::noteLastCircuit(path);
		// Back to what had the keyboard (Find's box, the side panel's
		// search), else the canvas.
		if (savedFocus && IsWindow(savedFocus) && IsChild(hwnd, savedFocus) && IsWindowVisible(savedFocus) &&
		    IsWindowEnabled(savedFocus))
			SetFocus(savedFocus);
		else if (Canvas* c = currentCanvas())
			c->focus();
		return 0;
	case WM_SETTINGCHANGE:
		// Windows switched apps between light and dark (by hand, or on a
		// schedule): follow it when that's the setting. Every window hears
		// it; the first one changes them all.
		if (lp && lstrcmpiW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0 && prefs().themeMode == 0 &&
		    systemPrefersDark() != prefs().dark) {
			prefs().dark = !prefs().dark;
			applyTheme();
		}
		break;
	case WM_INITMENUPOPUP:
		updateMenu((HMENU)wp);
		return 0;
	case WM_COMMAND: {
		const int id = LOWORD(wp);
		if (id >= CMD_RECENT && id <= CMD_RECENT_LAST) {
			// The circuit the menu showed (a save since moves the list).
			const size_t i = (size_t)(id - CMD_RECENT);
			if (i < recentPaths.size()) openCircuit(recentPaths[i], this);
			return 0;
		}
		if (id >= CMD_NEW && id < CMD_RECENT) {
			run(id);
			return 0;
		}
		break;
	}
	case WM_SETCURSOR: {
		POINT p;
		GetCursorPos(&p);
		ScreenToClient(hwnd, &p);
		const RECT s = splitterRect();
		if (LOWORD(lp) == HTCLIENT && PtInRect(&s, p)) {
			SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
			return TRUE;
		}
		break;
	}
	case WM_LBUTTONDOWN: {
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		const RECT s = splitterRect();
		if (PtInRect(&s, p)) {
			splitterDrag = true;
			splitterGrab = p.x - scaled(prefs().paletteWidth, dpi);
			SetCapture(hwnd);
			return 0;
		}
		break;
	}
	case WM_MOUSEMOVE:
		if (splitterDrag) {
			RECT rc;
			GetClientRect(hwnd, &rc);
			const int x = GET_X_LPARAM(lp) - splitterGrab;
			const int w = MulDiv(std::max(0, x), 96, (int)dpi);
			prefs().paletteWidth = std::min(std::max(w, 160), std::min(800, MulDiv(rc.right, 96, (int)dpi) - 300));
			layout();
			return 0;
		}
		break;
	case WM_LBUTTONUP:
		if (splitterDrag) {
			splitterDrag = false;
			ReleaseCapture();
			prefs().save();
			return 0;
		}
		break;
	case WM_CAPTURECHANGED:
		splitterDrag = false;
		break;
	case WM_COPYDATA:
		if (takeHandedFiles(hwnd, reinterpret_cast<const COPYDATASTRUCT*>(lp))) return TRUE;
		break;
	case kOpenHandedFiles:
		openHandedFiles(this);
		return 0;
	case WM_DROPFILES: {
		HDROP drop = (HDROP)wp;
		const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
		std::vector<std::string> files;
		for (UINT i = 0; i < n; i++) {
			wchar_t buf[MAX_PATH * 4];
			if (DragQueryFileW(drop, i, buf, (UINT)(sizeof buf / sizeof buf[0]))) files.push_back(U(buf));
		}
		DragFinish(drop);
		CircuitWindow* from = this;
		for (const std::string& f : files) { openCircuit(f, from); from = nullptr; }
		return 0;
	}
	case WM_CLOSE:
		// Not while one of its dialogs is up (the taskbar can still ask):
		// the dialog comes forward instead.
		if (!IsWindowEnabled(hwnd)) {
			const HWND popup = GetLastActivePopup(hwnd);
			if (popup && popup != hwnd) SetForegroundWindow(popup);
			MessageBeep(MB_ICONWARNING);
			return 0;
		}
		if (confirmClose()) destroy();
		return 0;
	case WM_ENDSESSION:
		// Signing out, or a restart (Windows Update): Windows ends the app
		// once this returns, so save now -- busy or not, and without a
		// question there's no time for. A save that fails leaves a
		// recovery copy, offered back at the next start.
		if (wp) {
			if (isDirty() && !saveQuietly(false)) writeRecovery();
			prefs().save();
		}
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kClockTimer);
		return 0;
	case WM_ERASEBKGND: {
		// Behind everything: the side panel's colour, and the hairline
		// between it and the canvas.
		RECT rc;
		GetClientRect(hwnd, &rc);
		const Chrome c = chrome();
		HBRUSH bg = CreateSolidBrush(c.gdi(c.panel()));
		FillRect((HDC)wp, &rc, bg);
		DeleteObject(bg);
		if (prefs().showPalette) {
			const int x = scaled(prefs().paletteWidth, dpi);
			RECT line = { x, toolbarHeight(), x + 1, rc.bottom };
			HBRUSH sash = CreateSolidBrush(c.gdi(c.sash()));
			FillRect((HDC)wp, &line, sash);
			DeleteObject(sash);
		}
		return 1;
	}
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

// Circuits save themselves, so closing doesn't ask: it saves. Only when that
// fails is there a question.
bool CircuitWindow::confirmClose() {
	if (!isDirty()) return true;
	if (saveQuietly(false)) return true;
	return askYesNo(hwnd, "This circuit couldn't be saved", "Close it anyway? The changes since it last saved will be lost.");
}

// Without saving: its circuit is going (deleted from Your Circuits).
void CircuitWindow::discard() {
	forceDirty = false;
	destroy();
}

void CircuitWindow::reloadFromDisk(const std::string& message) {
	char err[512] = "";
	CLDocument* fresh = cl_document_open(path.c_str(), err, sizeof err);
	if (fresh == nullptr) { showMessage(hwnd, Tone::Error, "The circuit couldn't be opened again", err); return; }
	replaceDocument(fresh, path);
	if (!message.empty()) note(message);
}

void CircuitWindow::startAs(const std::string& name) {
	recoveredName = name;
	forceDirty = true;
	saveQuietly(false);
	updateTitle();
	note("A new circuit from \u201C" + name + "\u201D, in Your Circuits.");
}

bool CircuitWindow::buildPlan(const formula::Plan& plan, bool onNewPage, const std::string& pageName) {
	if (plan.parts.empty()) return false;
	int target = currentPage();
	double dx = 0, dy = 0;
	if (onNewPage) {
		const int i = cl_document_add_page(doc);
		if (i < 0) return false;
		target = i;
		if (!pageName.empty()) cl_document_rename_page(doc, i, pageName.c_str());
	} else {
		double l, b, r, t;
		if (cl_document_page_bounds(doc, target, &l, &b, &r, &t)) {
			double minX = 1e9, maxY = -1e9;
			for (const formula::Plan::Part& p : plan.parts) { minX = std::min(minX, p.x); maxY = std::max(maxY, p.y); }
			dx = r + 16 - minX;
			dy = t - maxY;
		}
	}
	std::vector<CLBuildGate> gates;
	for (const formula::Plan::Part& p : plan.parts)
		gates.push_back({ p.gate.c_str(), p.x + dx, p.y + dy, p.label.empty() ? nullptr : p.label.c_str() });
	std::vector<CLBuildWire> wires;
	for (const formula::Plan::Wire& w : plan.wires) wires.push_back({ w.from, w.fromPin.c_str(), w.to, w.toPin.c_str() });
	if (cl_edit_build(doc, target, gates.data(), (int)gates.size(), wires.empty() ? nullptr : wires.data(), (int)wires.size(),
	                  "Build from Formula") <= 0)
		return false;
	if (onNewPage) {
		syncTabs();
		for (int i = 0; i < (int)canvases.size(); i++) if (canvases[i]->page() == target) current = i;
		pageSwitched();
		appearStart = nowSeconds();
	}
	edited();
	if (Canvas* c = currentCanvas()) c->zoomToFit(true);
	note(plan.summary());
	return true;
}

void CircuitWindow::switchTabs(bool backwards) {
	if (switcher && switcher->key(backwards)) SetTimer(hwnd, kSwitchTimer, 15, nullptr);
}

bool CircuitWindow::switcherActive() const { return switcher && switcher->active(); }

void CircuitWindow::cancelSwitcher() {
	if (switcher) switcher->cancel();
	KillTimer(hwnd, kSwitchTimer);
}

std::vector<int> CircuitWindow::recentTabs() const {
	std::vector<int> out;
	if (current >= 0 && current < (int)canvases.size()) out.push_back(current);
	for (uint64_t key : recentKeys)
		for (int i = 0; i < (int)canvases.size(); i++)
			if (canvases[i]->pageKey() == key && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
	for (int i = 0; i < (int)canvases.size(); i++)
		if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
	return out;
}

int CircuitWindow::pageOfTab(int index) const {
	return index >= 0 && index < (int)canvases.size() ? canvases[index]->page() : -1;
}

void CircuitWindow::showFoundGate(int page, long gate, double x, double y) {
	for (int i = 0; i < (int)canvases.size(); i++)
		if (canvases[i]->page() == page) showPage(i);
	cl_edit_select_gate(doc, page, gate);
	if (Canvas* c = currentCanvas()) c->centerOn(x, y);
	selectionChanged();
	redraw();
}

void CircuitWindow::partsChanged() {
	if (palette) palette->partsChanged();
}

void CircuitWindow::libraryChanged() {
	updateTitle();
	updateActions();
}

void CircuitWindow::destroy() {
	prefs().save();
	HWND h = hwnd;
	SetWindowLongPtrW(h, GWLP_USERDATA, 0);
	KillTimer(h, kClockTimer);
	delete this;
	DestroyWindow(h);
	if (circuitWindows().empty()) PostQuitMessage(0);
}

// ---- Commands -------------------------------------------------------------------

void CircuitWindow::run(int command) {
	auto editing = [&](void (CircuitWindow::*f)()) { if (canEdit()) (this->*f)(); else lockNudge(); };
	Canvas* c = currentCanvas();
	switch (command) {
	case CMD_NEW: newCircuitWindow(); break;
	case CMD_OPEN: showYourCircuits(this); break;
	case CMD_IMPORT: chooseAndOpen(this); break;
	case CMD_NEW_TEMPLATE: templates::showPicker(this); break;
	case CMD_SAVE_TEMPLATE: templates::saveCurrent(this); break;
	case CMD_SAVE_PART: parts::saveSelection(this); break;
	case CMD_BUILD_FORMULA: if (canEdit()) showBuildFormula(this); else lockNudge(); break;
	case CMD_FIND: {
		// A label or TO/FROM selected: look for its name.
		const long g = cl_edit_single_gate(doc, currentPage());
		const char* name = g >= 0 ? cl_gate_find_name(doc, g) : nullptr;
		findBar->open(name ? name : "");
		break;
	}
	case CMD_VERSIONS: showVersionHistory(this); break;
	case CMD_OPEN_SAMPLE: openPracticeCircuit(this); break;
	case CMD_SAVE: save(); break;
	case CMD_SAVE_AS: exportCopy(); break;
	case CMD_EXPORT_IMAGE: exportImage(); break;
	case CMD_EXPORT_V2: exportOlder(2); break;
	case CMD_EXPORT_V1: exportOlder(1); break;
	case CMD_PRINT: print(); break;
	case CMD_CLOSE_WINDOW: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
	case CMD_QUIT: if (confirmQuit(hwnd)) quitApp(); break;
	case CMD_UNDO: undo(); break;
	case CMD_REDO: redo(); break;
	case CMD_CUT: editing(&CircuitWindow::cut); break;
	case CMD_COPY: copy(); break;
	case CMD_PASTE: editing(&CircuitWindow::paste); break;
	case CMD_DUPLICATE: editing(&CircuitWindow::duplicate); break;
	case CMD_DELETE: editing(&CircuitWindow::deleteSelection); break;
	case CMD_SELECT_ALL: selectAll(); break;
	case CMD_ADD_GATE: editing(&CircuitWindow::quickAdd); break;
	case CMD_GATE_SETTINGS: showSettings(); break;
	case CMD_ROTATE: editing(&CircuitWindow::rotate); break;
	case CMD_STRAIGHTEN: editing(&CircuitWindow::straighten); break;
	case CMD_TIDY: if (canEdit()) tidy(0); else lockNudge(); break;
	case CMD_TIDY_FLOW: if (canEdit()) tidy(1); else lockNudge(); break;
	case CMD_TIDY_KEEP: endTidy(true); break;
	case CMD_TIDY_REVERT: endTidy(false); break;
	case CMD_TIDY_SWITCH: switchTidyMode(); break;
	case CMD_CONNECT_NEARBY: if (canEdit()) connectNearby(false); else lockNudge(); break;
	case CMD_PREFERENCES: showPreferencesDialog(hwnd); break;
	case CMD_ZOOM_IN: if (c) c->animateZoom(1 / 0.75); break;
	case CMD_ZOOM_OUT: if (c) c->animateZoom(0.75); break;
	case CMD_ZOOM_FIT: if (c) c->zoomToFit(true); break;
	case CMD_ZOOM_ACTUAL: if (c) c->zoomActual(); break;
	case CMD_DARK: toggleDark(); break;
	case CMD_PALETTE: togglePalette(); break;
	case CMD_STATUS_BAR:
		prefs().showStatus = !prefs().showStatus;
		prefs().save();
		for (CircuitWindow* o : circuitWindows()) o->prefsChanged();
		break;
	case CMD_RUNNING: toggleRunning(); break;
	case CMD_STEP: stepOnce(); break;
	case CMD_SIM_VIEW: toggleSimView(); break;
	case CMD_LOCK: toggleLock(); break;
	case CMD_TRUTH_TABLE: makeTruthTable(); break;
	case CMD_SCOPE: toggleScope(); break;
	case CMD_NEW_TAB: newPage(); break;
	case CMD_CLOSE_TAB:
		if (cl_document_page_count(doc) > 1) closePage(currentPage());
		else PostMessageW(hwnd, WM_CLOSE, 0, 0);
		break;
	case CMD_REOPEN_TAB: reopenPage(); break;
	case CMD_RENAME_TAB: renamePage(currentPage()); break;
	case CMD_NEXT_TAB: cyclePage(1); break;
	case CMD_PREVIOUS_TAB: cyclePage(-1); break;
	case CMD_SHORTCUTS: showShortcuts(); break;
	case CMD_HELP: help::show(this); break;
	case CMD_CHECK_UPDATES: updater::checkNow(hwnd); break;
	case CMD_TOUR: welcome::startTourOn(this); break;
	case CMD_WHATS_NEW: whatsnew::show(this); break;
	case CMD_FEEDBACK: feedback::show(this); break;
	case CMD_ABOUT:
		showMessage(hwnd, Tone::Info, "CedarLogic " CL_VERSION " (native Windows, testing)",
		            "A digital logic simulator, from Cedarville University.\n\n"
		            "This is the native Windows app: plain Windows controls and Direct2D on the shared CedarLogic "
		            "engine, with nothing else to install.\n\nhttps://github.com/leviholliday/CedarLogic");
		break;
	default: break;
	}
}

void CircuitWindow::moreMenu(POINT screen, bool rightAligned) {
	SetForegroundWindow(hwnd);
	const int cmd = TrackPopupMenu(menus, TPM_RETURNCMD | (rightAligned ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_TOPALIGN,
	                               screen.x, screen.y, 0, hwnd, nullptr);
	// Posted, so it runs once whatever opened the menu (the toolbar's •••)
	// is done with it: Exit and Close take the window, the toolbar too, away.
	if (cmd) PostMessageW(hwnd, WM_COMMAND, cmd, 0);
}

// The circuit's name in the toolbar: what a Mac window's title offers.
void CircuitWindow::titleMenu(POINT screen) {
	enum { RENAME = 1, DUPLICATE, VERSIONS, EXPORT, LIBRARY };
	HMENU m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, RENAME, L"&Rename\u2026");
	AppendMenuW(m, MF_STRING, DUPLICATE, L"&Duplicate");
	AppendMenuW(m, MF_STRING, VERSIONS, L"&Version History\u2026");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, EXPORT, L"&Export\u2026\tCtrl+Shift+S");
	AppendMenuW(m, MF_STRING, LIBRARY, L"Your &Circuits\u2026\tCtrl+O");
	SetForegroundWindow(hwnd);
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screen.x, screen.y, 0, hwnd, nullptr);
	DestroyMenu(m);
	switch (cmd) {
	case RENAME: renameFile(); break;
	case DUPLICATE: duplicateCircuit(); break;
	case VERSIONS: showVersionHistory(this); break;
	case EXPORT: exportCopy(); break;
	case LIBRARY: showYourCircuits(this); break;
	default: break;
	}
}

// The name it has in Your Circuits (one not in it yet joins under it).
void CircuitWindow::renameFile() {
	std::string name = displayName();
	if (!askText(hwnd, "Rename Circuit", "The name it has in Your Circuits:", name) || name.empty() || name == displayName()) return;
	library::Item it;
	if (library::itemFor(path, it)) {
		library::rename(it, name);
		for (CircuitWindow* w : circuitWindows()) w->libraryChanged();
	} else {
		recoveredName = name;
		forceDirty = true;
		saveQuietly(false);
	}
	updateTitle();
	updateActions();
	note("Renamed.");
}

// A copy, as a new circuit in Your Circuits, in a window of its own.
void CircuitWindow::duplicateCircuit() {
	saveQuietly(false);
	const std::string text = cl_document_save_text(doc);
	library::Item copy;
	if (!library::create(displayName() + " copy", text, "", copy)) {
		showMessage(hwnd, Tone::Error, "The circuit couldn't be duplicated", "");
		return;
	}
	char err[512] = "";
	CLDocument* d = cl_document_open(copy.circuit().c_str(), err, sizeof err);
	if (d == nullptr) { showMessage(hwnd, Tone::Error, "The circuit couldn't be duplicated", err); return; }
	CircuitWindow* w = new CircuitWindow(d, copy.circuit());
	w->note("A copy, in Your Circuits as \u201C" + copy.name + "\u201D.");
}

// ---- Files ---------------------------------------------------------------------

void CircuitWindow::replaceDocument(CLDocument* newDoc, const std::string& newPath) {
	for (Canvas* c : canvases) c->cancelDrag();
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	current = 0;
	if (scope) { delete scope; scope = nullptr; }
	cl_document_close(doc);
	doc = newDoc;
	path = newPath;
	forceDirty = false;
	recovery::remove(recoveryBase);
	recoveredName.clear();
	changesAtRecovery = changes;
	pendingGate.clear();
	selectionSignature.clear();
	seenPages.clear();
	isRunning = cl_document_is_running(doc);
	simViewOn = false;
	lockedOn = false;
	syncTabs();
	appearStart = nowSeconds();
	beginOpening();
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	if (Canvas* c = currentCanvas()) c->focus();
}

// Written to a temporary beside it, then moved over it, so a failed write
// leaves the old file as it was. "" when written, else why not.
static std::string writeText(const std::string& file, const std::string& text) {
	const std::wstring target = W(file), tmp = target + L".saving";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return "The file couldn't be written there. Check that the folder exists and you can save to it.";
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size() && FlushFileBuffers(h);
	CloseHandle(h);
	std::string err;
	if (!ok) err = "The disk may be full.";
	else if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		err = "The old file couldn't be replaced. Is it open in another program, or read-only?";
	if (!err.empty()) DeleteFileW(tmp.c_str());
	return err;
}

// Into Your Circuits: a circuit that isn't there yet joins it once there's
// something on it (so an empty new window leaves nothing behind). A version
// is kept when one's due, or now when `explicitSave` (Ctrl+S).
bool CircuitWindow::saveQuietly(bool explicitSave) {
	library::Item it;
	const bool inLibrary = library::itemFor(path, it);
	bool hasGates = false;
	for (int p = 0; p < cl_document_page_count(doc) && !hasGates; p++) hasGates = cl_document_gate_count(doc, p) > 0;
	if (!inLibrary && !hasGates && !explicitSave) return true;   // nothing to keep
	const std::string text = cl_document_save_text(doc);   // marks the engine's copy saved
	std::string err;
	if (inLibrary) {
		err = writeText(path, text);
	} else {
		std::string name = displayName();
		if (name == "Untitled") name = "Untitled Circuit";
		if (library::create(name, text, "", it)) path = it.circuit();
		else err = "Your Circuits' folder couldn't be written to.";
	}
	if (!err.empty()) {
		forceDirty = true;
		if (explicitSave) showMessage(hwnd, Tone::Error, "The circuit couldn't be saved", err);
		else note("Couldn't save just now. Your work is still here; try Ctrl+S.");
		updateTitle();
		return false;
	}
	forceDirty = false;
	recoveredName.clear();
	// Saved: the recovery copy isn't needed until the next change.
	recovery::remove(recoveryBase);
	changesAtRecovery = changes;
	const bool kept = library::noteSaved(path, explicitSave);
	library::noteLastCircuit(path);
	if (explicitSave) note(kept ? "Saved, and a version was kept." : "Saved.");
	updateTitle();
	return true;
}

bool CircuitWindow::save() { return saveQuietly(true); }

// A copy of the circuit as a .cdl file, anywhere (Your Circuits keeps the
// circuit itself).
bool CircuitWindow::exportCopy() {
	const std::string file = chooseSaveFile(hwnd, "Export", displayName() + ".cdl", kCdlFilters, ".cdl");
	if (file.empty()) return false;
	const bool wasDirty = isDirty();
	const std::string err = writeText(file, cl_document_save_text(doc));
	forceDirty = forceDirty || wasDirty;   // asking for the text marked it saved
	if (!err.empty()) { showMessage(hwnd, Tone::Error, "The circuit couldn't be exported", err); return false; }
	note("Exported " + baseName(file) + ".cdl.");
	return true;
}

void CircuitWindow::exportOlder(int format) {
	const std::string suggested = displayName() + (format == 1 ? " (v1.x).cdl" : " (v2).cdl");
	const std::string file = chooseSaveFile(hwnd, format == 1 ? "Export for CedarLogic 1.x" : "Export for CedarLogic 2",
	                                        suggested, kCdlFilters, ".cdl");
	if (file.empty()) return;
	char why[512] = "";
	const int rc = cl_document_export_legacy(doc, file.c_str(), format, why, sizeof why);
	if (rc == 0) { note("Exported."); return; }
	showMessage(hwnd, rc > 0 ? Tone::Info : Tone::Warning,
	            rc > 0 ? "Exported, with one thing left out" : "The circuit couldn't be exported", why);
}

namespace {

// Write a WIC bitmap to a PNG file.
bool savePng(IWICImagingFactory* wic, IWICBitmapSource* bmp, const std::string& file) {
	IWICStream* stream = nullptr;
	IWICBitmapEncoder* enc = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	const bool ok = SUCCEEDED(wic->CreateStream(&stream)) &&
	                SUCCEEDED(stream->InitializeFromFilename(W(file).c_str(), GENERIC_WRITE)) &&
	                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
	                SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
	                SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
	                SUCCEEDED(frame->WriteSource(bmp, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
	if (frame) frame->Release();
	if (enc) enc->Release();
	if (stream) stream->Release();
	return ok;
}

IWICImagingFactory* wicFactory() {
	static IWICImagingFactory* f = [] {
		IWICImagingFactory* made = nullptr;
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&made));
		return made;
	}();
	return f;
}

}  // namespace

// Export the page in front as a picture (Export.cpp).
void CircuitWindow::exportImage() { showExportImage(this, currentPage()); }

void CircuitWindow::print() {
	const int page = currentPage();
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) { note("This page is empty: nothing to print."); return; }
	PRINTDLGW pd = {};
	pd.lStructSize = sizeof pd;
	pd.hwndOwner = hwnd;
	pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
	// Wide circuits print across the page.
	if ((r - l) > (t - b)) {
		HGLOBAL mode = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVMODEW));
		if (DEVMODEW* dm = (DEVMODEW*)GlobalLock(mode)) {
			dm->dmSize = sizeof(DEVMODEW);
			dm->dmFields = DM_ORIENTATION;
			dm->dmOrientation = DMORIENT_LANDSCAPE;
			GlobalUnlock(mode);
			pd.hDevMode = mode;
		}
	}
	if (!PrintDlgW(&pd)) {
		if (pd.hDevMode) GlobalFree(pd.hDevMode);
		if (pd.hDevNames) GlobalFree(pd.hDevNames);
		return;
	}
	HDC dc = pd.hDC;
	const std::wstring name = W(displayName());
	DOCINFOW di = { sizeof di, name.c_str(), nullptr, nullptr, 0 };
	bool ok = false;
	if (StartDocW(dc, &di) > 0 && StartPage(dc) > 0) {
		const int pw = GetDeviceCaps(dc, HORZRES), ph = GetDeviceCaps(dc, VERTRES);
		const double dpiX = GetDeviceCaps(dc, LOGPIXELSX);
		// Points of 1/72 inch, as the Mac prints: lines a point wide.
		const double pointsPerPixel = 72.0 / std::max(1.0, dpiX);
		ID2D1DCRenderTarget* rt = nullptr;
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
		RECT area = { 0, 0, pw, ph };
		if (SUCCEEDED(d2dFactory()->CreateDCRenderTarget(&props, &rt)) && SUCCEEDED(rt->BindDC(dc, &area))) {
			rt->BeginDraw();
			rt->Clear(D2D1::ColorF(1, 1, 1, 1));
			rt->SetTransform(D2D1::Matrix3x2F::Scale((float)(1 / pointsPerPixel), (float)(1 / pointsPerPixel)));
			cl_document_draw_fitted(doc, page, rt, pw * pointsPerPixel, ph * pointsPerPixel, 12, 1, CL_STYLE_PRINT);
			ok = SUCCEEDED(rt->EndDraw());
		}
		if (rt) rt->Release();
		EndPage(dc);
		EndDoc(dc);
	}
	DeleteDC(dc);
	if (pd.hDevMode) GlobalFree(pd.hDevMode);
	if (pd.hDevNames) GlobalFree(pd.hDevNames);
	if (!ok) showMessage(hwnd, Tone::Error, "The page couldn't be printed", "");
}

bool CircuitWindow::screenshot(const std::string& file, HWND other) {
	HWND target = other ? other : hwnd;
	RECT rc;
	GetWindowRect(target, &rc);
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = SelectObject(mem, bmp);
	const bool drawn = PrintWindow(target, mem, PW_RENDERFULLCONTENT) != FALSE;
	SelectObject(mem, old);
	bool ok = false;
	IWICImagingFactory* wic = wicFactory();
	IWICBitmap* wb = nullptr;
	if (drawn && wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wb))) {
		ok = savePng(wic, wb, file);
		wb->Release();
	}
	DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return ok;
}

// ---- Editing ---------------------------------------------------------------------

void CircuitWindow::refreshAfterHistory() {
	// An undo or redo that closed or reopened a page shows that page.
	if (cl_document_page_count(doc) != lastPageCount) {
		const int show = cl_document_page_to_show(doc);
		syncTabs();
		if (show >= 0 && show < (int)canvases.size()) {
			current = show;
			pageSwitched();
			appearStart = nowSeconds();
		}
	}
	edited();
}

void CircuitWindow::undo() {
	if (simViewOn) return;
	if (Canvas* c = currentCanvas()) if (c->isDragging()) c->cancelDrag();
	if (cl_edit_undo(doc)) refreshAfterHistory();
	else MessageBeep(MB_OK);
}

void CircuitWindow::redo() {
	if (simViewOn) return;
	if (Canvas* c = currentCanvas()) if (c->isDragging()) c->cancelDrag();
	if (cl_edit_redo(doc)) refreshAfterHistory();
	else MessageBeep(MB_OK);
}

void CircuitWindow::copy() {
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) { note("Nothing is selected to copy."); return; }
	setClipboardText(hwnd, text);
	note("Copied.");
}

void CircuitWindow::cut() {
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) return;
	setClipboardText(hwnd, text);
	deleteSelection();
}

bool CircuitWindow::placePoint(double& wx, double& wy) const {
	Canvas* c = currentCanvas();
	if (c == nullptr) return false;
	if (!c->pointerWorld(wx, wy)) c->center(wx, wy);
	return true;
}

void CircuitWindow::floatSelection(double wx, double wy) {
	if (cl_edit_float_begin(doc, currentPage(), wx, wy)) {
		if (Canvas* c = currentCanvas()) c->focus();
	}
	edited();
}

bool CircuitWindow::isFloating() const { return cl_edit_is_floating(doc); }

void CircuitWindow::cancelFloating() {
	if (!isFloating()) return;
	// It was never placed, so take it back.
	cl_edit_cancel(doc);
	cl_edit_undo(doc);
	edited();
}

void CircuitWindow::paste() {
	if (!canEdit()) { lockNudge(); return; }
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	std::string text;
	if (!clipboardText(hwnd, text) || text.empty()) { note("Nothing to paste."); return; }
	pasteText(text, true, shift);
}

void CircuitWindow::pasteText(const std::string& text, bool floating, bool shift) {
	double wx, wy;
	if (!canEdit() || !placePoint(wx, wy)) return;
	const char* back = nullptr;
	if (!cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, shift, &back)) { note("Nothing to paste."); return; }
	if (back && *back) setClipboardText(hwnd, back);
	if (floating) floatSelection(wx, wy);
	else edited();
}

void CircuitWindow::duplicate() {
	double wx, wy;
	if (!canEdit() || !placePoint(wx, wy)) return;
	const std::string text = cl_edit_copy(doc, currentPage());
	if (text.empty()) return;
	if (prefs().duplicateUsesClipboard) setClipboardText(hwnd, text);
	const char* back = nullptr;
	cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, false, &back);
	floatSelection(wx, wy);
}

void CircuitWindow::selectAll() { cl_edit_select_all(doc, currentPage()); redraw(); selectionChanged(); }
void CircuitWindow::selectNone() { cl_edit_select_none(doc, currentPage()); redraw(); selectionChanged(); }
void CircuitWindow::deleteSelection() { cl_edit_delete(doc, currentPage()); edited(); }
void CircuitWindow::rotate() { cl_edit_rotate(doc, currentPage()); edited(); }
void CircuitWindow::straighten() { cl_edit_straighten(doc, currentPage()); edited(); }
void CircuitWindow::nudge(double dx, double dy) { cl_edit_nudge(doc, currentPage(), dx, dy); edited(); }

void CircuitWindow::tidy(int mode) {
	if (!cl_edit_tidy_begin(doc, currentPage(), mode < 0 ? prefs().tidyMode : mode)) note("Nothing to tidy on this page.");
	edited();
}

void CircuitWindow::endTidy(bool keep) {
	cl_edit_tidy_end(doc, keep);
	edited();
	if (Canvas* c = currentCanvas()) c->focus();
}

void CircuitWindow::switchTidyMode() {
	const int other = 1 - cl_edit_tidy_mode(doc);
	cl_edit_tidy_end(doc, false);
	cl_edit_tidy_begin(doc, currentPage(), other);
	edited();
	if (Canvas* c = currentCanvas()) c->focus();
}

bool CircuitWindow::tidyActive() const { return cl_edit_tidy_active(doc); }

void CircuitWindow::connectNearby(bool quietly) {
	Canvas* c = currentCanvas();
	if (!canEdit() || c == nullptr) return;
	const int n = cl_edit_connect_nearby(doc, currentPage(), c->unitsPerPoint());
	if (n > 0) note(strf("Connected %d pin%s.", n, n == 1 ? "" : "s"));
	else if (!quietly) note("Nothing close enough to connect.");
	edited();
}

void CircuitWindow::quickAdd() { showQuickAdd(this); }

void CircuitWindow::addGateOnNextMove(const std::string& name) {
	if (!canEdit()) { lockNudge(); return; }
	pendingGate = name;
	note("Move onto the canvas: the gate follows the pointer until you click to drop it.");
	if (Canvas* c = currentCanvas()) {
		c->focus();
		// Already over the canvas (the keyboard chose it): there at once.
		double wx, wy;
		if (c->pointerWorld(wx, wy)) placePendingGate(wx, wy);
	}
}

bool CircuitWindow::placePendingGate(double wx, double wy) {
	if (pendingGate.empty()) return false;
	const std::string name = pendingGate;
	pendingGate.clear();
	return addGateFloating(name, wx, wy);
}

bool CircuitWindow::addGateFloating(const std::string& name, double wx, double wy) {
	if (!canEdit()) { lockNudge(); return false; }
	// One of My Parts: its gates and wires, pasted, on the pointer.
	if (parts::isPart(name)) {
		parts::Part p;
		if (!parts::find(name, p)) return false;
		const std::string text = p.text();
		const char* back = nullptr;
		if (!cl_edit_paste(doc, currentPage(), text.c_str(), wx, wy, true, &back)) return false;
		floatSelection(wx, wy);
		redraw();
		return true;
	}
	if (!cl_edit_add_gate(doc, currentPage(), name.c_str(), wx, wy)) return false;
	floatSelection(wx, wy);
	redraw();
	return true;
}

void CircuitWindow::showSettings() {
	const long g = cl_edit_single_gate(doc, currentPage());
	if (g < 0) return;
	int addressBits = 0, dataBits = 0;
	if (cl_ram_info(doc, g, &addressBits, &dataBits)) showRamEditor(this, g);
	else showGateSettings(this, g);
}

// ---- The right-click menu ------------------------------------------------------

void CircuitWindow::showContextMenu(int target, double wx, double wy, POINT screen) {
	HMENU m = CreatePopupMenu();
	switch (target) {
	case CL_CONTEXT_PIN:
		item(m, CMD_DISCONNECT, "&Disconnect");
		break;
	case CL_CONTEXT_WIRE:
		item(m, CMD_STRAIGHTEN, "&Straighten Route\tS");
		separator(m);
		item(m, CMD_DELETE, "&Delete\tDel");
		break;
	case CL_CONTEXT_GATE:
		item(m, CMD_GATE_SETTINGS, "&Settings…");
		item(m, CMD_ROTATE, "&Rotate\tR");
		item(m, CMD_STRAIGHTEN, "Straighten Its &Wires\tS");
		separator(m);
		item(m, CMD_CUT, "Cu&t\tCtrl+X");
		item(m, CMD_COPY, "&Copy\tCtrl+C");
		item(m, CMD_DUPLICATE, "D&uplicate\tCtrl+D");
		item(m, CMD_DELETE, "&Delete\tDel");
		break;
	default:
		item(m, CMD_PASTE, "&Paste\tCtrl+V");
		item(m, CMD_SELECT_ALL, "Select &All\tCtrl+A");
		item(m, CMD_ADD_GATE, "&Add a Gate…\tA");
		separator(m);
		item(m, CMD_ZOOM_FIT, "Zoom to &Fit\tCtrl+0");
		break;
	}
	updateMenu(m);
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
	DestroyMenu(m);
	if (cmd == CMD_DISCONNECT) {
		if (Canvas* c = currentCanvas()) {
			cl_edit_disconnect_pin(doc, currentPage(), wx, wy, c->unitsPerPoint());
			edited();
		}
	} else if (cmd) {
		run(cmd);
	}
}

// ---- Simulation --------------------------------------------------------------------

void CircuitWindow::setRunning(bool run) {
	isRunning = run;
	cl_document_set_running(doc, run);
	lastTick = nowSeconds();
	updateRunUI();
}

void CircuitWindow::stepOnce() {
	// Stepping a running circuit means little: pause first.
	if (isRunning) setRunning(false);
	cl_document_step(doc);
	redraw();
	if (scope) scope->update();
}

void CircuitWindow::setStepMs(int ms) {
	if (ms < 1 || ms > 500 || ms == cl_document_step_ms(doc)) return;
	cl_document_set_step_ms(doc, ms);
	note(strf("Each step is now %d ms of circuit time.", cl_document_step_ms(doc)));
}

void CircuitWindow::toggleSimView() {
	simViewOn = !simViewOn;
	if (simViewOn) {
		if (Canvas* c = currentCanvas()) c->cancelDrag();
		cl_edit_cancel(doc);
		if (cl_edit_tidy_active(doc)) cl_edit_tidy_end(doc, true);
		cl_edit_select_none(doc, currentPage());
		pendingGate.clear();
		if (!isRunning) setRunning(true);   // "Run" means run
	}
	updateActions();
	updateBanner();
	for (Canvas* c : canvases) c->redraw();
	if (Canvas* c = currentCanvas()) c->focus();
}

void CircuitWindow::toggleLock() {
	lockedOn = !lockedOn;
	if (lockedOn) {
		if (Canvas* c = currentCanvas()) c->cancelDrag();
		pendingGate.clear();
	}
	note(lockedOn ? "Locked: switches still work; nothing else changes." : "Unlocked.");
	updateActions();
	updateBanner();
	if (Canvas* c = currentCanvas()) c->focus();
}

void CircuitWindow::makeTruthTable() { showTruthTable(this, currentPage()); redraw(); }

void CircuitWindow::toggleScope() {
	if (scope && scope->visible()) { scope->close(); return; }
	if (scope == nullptr) scope = new ScopeWindow(this);
	scope->present();
}

// ---- Pages ---------------------------------------------------------------------

void CircuitWindow::newPage() {
	int taken = cl_document_page_count(doc) + 1;
	std::vector<std::string> names;
	for (int i = 0; i < cl_document_page_count(doc); i++) names.push_back(pageName(i));
	while (std::find(names.begin(), names.end(), strf("Page %d", taken)) != names.end()) taken++;
	const int i = cl_document_add_page(doc);
	if (i < 0) return;
	cl_document_rename_page(doc, i, strf("Page %d", taken).c_str());
	syncTabs();
	current = i;
	pageSwitched();
	appearStart = nowSeconds();
	edited();
}

void CircuitWindow::closePage(int page) {
	if (cl_document_page_count(doc) < 2) return;
	// A tab with work on it asks first (wx CloseTabCanvas).
	if (cl_document_gate_count(doc, page) > 0 &&
	    !askYesNo(hwnd, "Close Tab", "All work on this tab will be lost. Would you like to close it?\n\n"
	                                 "(Ctrl+Shift+T brings it back.)"))
		return;
	for (Canvas* c : canvases) c->cancelDrag();
	cl_edit_select_none(doc, page);
	if (cl_document_close_page(doc, page)) {
		const int show = std::min(cl_document_page_to_show(doc), cl_document_page_count(doc) - 1);
		syncTabs();
		if (show >= 0) { current = show; pageSwitched(); }
		edited();
	}
}

void CircuitWindow::reopenPage() {
	if (!cl_edit_undo_is_close_page(doc)) { note("No closed tab to reopen."); MessageBeep(MB_OK); return; }
	undo();
	appearStart = nowSeconds();
	note("Reopened the closed tab.");
}

void CircuitWindow::renamePage(int page) {
	if (page < 0 || page >= cl_document_page_count(doc)) return;
	std::string name = pageName(page);
	if (!askText(hwnd, "Rename Tab", "The tab's name:", name)) return;
	if (name.empty()) return;
	cl_document_rename_page(doc, page, name.c_str());
	changes++;
	updateTabLabels();
	updateTitle();
}

void CircuitWindow::cyclePage(int delta) {
	const int n = (int)canvases.size();
	if (n < 2) return;
	showPage(((current + delta) % n + n) % n);
}

// ---- App-wide ------------------------------------------------------------------

void CircuitWindow::toggleDark() {
	prefs().dark = !prefs().dark;
	if (prefs().themeMode == 0) prefs().themeMode = 3;   // the choice sticks
	prefs().save();
	applyTheme();
}

void CircuitWindow::togglePalette() {
	prefs().showPalette = !prefs().showPalette;
	prefs().save();
	for (CircuitWindow* w : circuitWindows()) w->prefsChanged();
}

void CircuitWindow::showShortcuts() { showShortcutsWindow(hwnd); }

void CircuitWindow::rebuildRecentMenu() {
	while (GetMenuItemCount(recentMenu) > 0) DeleteMenu(recentMenu, 0, MF_BYPOSITION);
	const std::vector<library::Item> all = library::items();
	recentPaths.clear();
	int shown = 0;
	for (const library::Item& it : all) {
		// Ampersands in a name aren't mnemonics.
		std::string label;
		for (char c : it.name) { if (c == '&') label += '&'; label += c; }
		if (shown < 9) label = strf("&%d  ", shown + 1) + label;
		AppendMenuW(recentMenu, MF_STRING, CMD_RECENT + shown, W(label).c_str());
		recentPaths.push_back(it.circuit());
		if (++shown >= 10) break;
	}
	if (shown == 0) AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"No circuits yet");
}
