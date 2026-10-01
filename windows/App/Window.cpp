// A circuit window (see Window.h).

#include "Window.h"
#include "Canvas.h"
#include "Dialogs.h"
#include "Palette.h"
#include "Recovery.h"

#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const wchar_t* kClass = L"CedarLogicWindow";
const UINT_PTR kClockTimer = 1;
const int kStepEditId = 3000, kTabsId = 3001;
const double kSelectionFadeTime = 0.13, kAppearTime = 0.32, kDragFadeTime = 0.18;

double since(double t) { return nowSeconds() - t; }

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

// The toolbar's buttons: a command, its label, whether it stays pressed.
struct ToolButton { int command; const char* label; const char* tip; bool toggle; bool gapBefore; };
const ToolButton kToolButtons[] = {
	{ CMD_NEW, "New", "New circuit (Ctrl+N)", false, false },
	{ CMD_OPEN, "Open", "Open (Ctrl+O)", false, false },
	{ CMD_SAVE, "Save", "Save (Ctrl+S)", false, false },
	{ CMD_UNDO, "Undo", "Undo (Ctrl+Z)", false, true },
	{ CMD_REDO, "Redo", "Redo (Ctrl+Y)", false, false },
	{ CMD_ZOOM_IN, "Zoom In", "Zoom in (Ctrl+=)", false, true },
	{ CMD_ZOOM_OUT, "Zoom Out", "Zoom out (Ctrl+-)", false, false },
	{ CMD_ZOOM_FIT, "Fit", "Zoom to fit (Ctrl+0, or tap Space)", false, false },
	{ CMD_RUNNING, "Pause", "Run or pause the simulation", true, true },
	{ CMD_STEP, "Step", "Step once (Ctrl+Shift+R)", false, false },
	{ CMD_SIM_VIEW, "Simulation View", "Simulation View: watch it run (Ctrl+R)", true, true },
	{ CMD_LOCK, "Lock", "Lock: switches still work, nothing else changes", true, false },
	{ CMD_DARK, "Dark", "Dark mode (Ctrl+Shift+D)", true, true },
};

}  // namespace

void registerWindowClasses() {
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof wc;
	wc.lpfnWndProc = CircuitWindow::proc;
	wc.hInstance = appInstance();
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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
	ShowWindow(hwnd, prefs().windowMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
	UpdateWindow(hwnd);
	if (Canvas* c = currentCanvas()) c->focus();
}

CircuitWindow::~CircuitWindow() {
	if (tabs) RemoveWindowSubclass(tabs, tabsProc, 1);
	delete scope;
	scope = nullptr;
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	delete palette;
	palette = nullptr;
	miniMap = nullptr;
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
	setDarkTitleBar(hwnd, prefs().dark);
	buildMenus();
	buildToolbar();

	// A bar for Tidy Up's preview, Simulation View and Lock.
	bannerLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0, 0, 10, 10, hwnd,
	                              nullptr, appInstance(), nullptr);

	palette = new GatePalette(this, hwnd);
	paletteHost = palette->widget();
	miniMap = palette->miniMap();

	tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_TOOLTIPS,
	                       0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kTabsId, appInstance(), nullptr);
	SetWindowSubclass(tabs, tabsProc, 1, (DWORD_PTR)this);
	newTabButton = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd,
	                               (HMENU)(INT_PTR)CMD_NEW_TAB, appInstance(), nullptr);

	statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | SBARS_SIZEGRIP, 0, 0, 10, 10, hwnd, nullptr,
	                            appInstance(), nullptr);
	setFontTree(hwnd, uiFont(dpi));
	palette->dpiChanged();
	ShowWindow(paletteHost, prefs().showPalette ? SW_SHOW : SW_HIDE);
	ShowWindow(statusBar, prefs().showStatus ? SW_SHOW : SW_HIDE);
}

void CircuitWindow::buildMenus() {
	menuBar = CreateMenu();
	HMENU file = submenu(menuBar, "&File");
	item(file, CMD_NEW, "&New\tCtrl+N");
	item(file, CMD_OPEN, "&Open…\tCtrl+O");
	recentMenu = submenu(file, "Open &Recent");
	item(file, CMD_OPEN_SAMPLE, "Open the &Practice Circuit");
	separator(file);
	item(file, CMD_SAVE, "&Save\tCtrl+S");
	item(file, CMD_SAVE_AS, "Save &As…\tCtrl+Shift+S");
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
	separator(edit);
	item(edit, CMD_ADD_GATE, "Add a &Gate…\tA");
	item(edit, CMD_GATE_SETTINGS, "Gate &Settings…");
	item(edit, CMD_ROTATE, "R&otate\tR");
	item(edit, CMD_STRAIGHTEN, "Straighten &Wires\tS");
	item(edit, CMD_TIDY, "T&idy Up\tShift+S");
	item(edit, CMD_TIDY_FLOW, "Tidy Up by Signal &Flow");
	item(edit, CMD_CONNECT_NEARBY, "Connect &Nearby Pins");
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
	item(help, CMD_HELP, "CedarLogic &Help\tF1");
	separator(help);
	item(help, CMD_ABOUT, "&About CedarLogic");
	SetMenu(hwnd, menuBar);
}

void CircuitWindow::buildToolbar() {
	for (const ToolButton& b : kToolButtons) {
		const DWORD style = WS_CHILD | WS_VISIBLE | (b.toggle ? (BS_AUTOCHECKBOX | BS_PUSHLIKE) : BS_PUSHBUTTON);
		HWND h = CreateWindowExW(0, L"BUTTON", W(b.label).c_str(), style, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)b.command,
		                         appInstance(), nullptr);
		toolButtons.push_back(h);
		if (b.command == CMD_RUNNING) runButton = h;
		if (b.command == CMD_SIM_VIEW) simViewButton = h;
		if (b.command == CMD_LOCK) lockButton = h;
		if (b.command == CMD_DARK) darkButton = h;
	}
	// The step length, as the wx app's timestep box.
	stepEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT, 0, 0, 10, 10,
	                           hwnd, (HMENU)(INT_PTR)kStepEditId, appInstance(), nullptr);
	stepUpDown = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
	                                 UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 10, 10, hwnd, nullptr, appInstance(), nullptr);
	SendMessageW(stepUpDown, UDM_SETBUDDY, (WPARAM)stepEdit, 0);
	SendMessageW(stepUpDown, UDM_SETRANGE32, 1, 500);
	SendMessageW(stepUpDown, UDM_SETPOS32, 0, cl_document_step_ms(doc));

	// Tooltips for the buttons.
	HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
	                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, appInstance(), nullptr);
	for (size_t i = 0; i < toolButtons.size(); i++) {
		const std::wstring text = W(kToolButtons[i].tip);
		TTTOOLINFOW ti = {};
		ti.cbSize = sizeof ti;
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.hwnd = hwnd;
		ti.uId = (UINT_PTR)toolButtons[i];
		ti.lpszText = const_cast<wchar_t*>(text.c_str());
		SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	}
	TTTOOLINFOW ti = {};
	ti.cbSize = sizeof ti;
	ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	ti.hwnd = hwnd;
	ti.uId = (UINT_PTR)stepEdit;
	std::wstring stepTip = L"Milliseconds of circuit time per simulation step";
	ti.lpszText = &stepTip[0];
	SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

int CircuitWindow::toolbarHeight() const { return scaled(38, dpi); }
int CircuitWindow::bannerHeight() const { return bannerKind >= 0 ? scaled(36, dpi) : 0; }

RECT CircuitWindow::splitterRect() const {
	RECT r = { 0, 0, 0, 0 };
	if (!prefs().showPalette) return r;
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int top = toolbarHeight() + bannerHeight();
	RECT sb = { 0, 0, 0, 0 };
	if (prefs().showStatus && statusBar) GetWindowRect(statusBar, &sb);
	const int x = scaled(prefs().paletteWidth, dpi);
	r = { x, top, x + scaled(6, dpi), rc.bottom - (sb.bottom - sb.top) };
	return r;
}

void CircuitWindow::layout() {
	if (hwnd == nullptr || tabs == nullptr) return;
	RECT rc;
	GetClientRect(hwnd, &rc);
	auto sc = [&](int v) { return scaled(v, dpi); };
	HDWP defer = BeginDeferWindowPos(40);
	auto place = [&](HWND h, int x, int y, int w, int hh) {
		if (h) defer = DeferWindowPos(defer, h, nullptr, x, y, std::max(0, w), std::max(0, hh), SWP_NOZORDER | SWP_NOACTIVATE);
	};

	// The toolbar: buttons sized to their labels, left to right; Dark at the
	// right end.
	HDC dc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(dc, uiFont(dpi));
	int x = sc(6);
	const int by = sc(6), bh = sc(26);
	for (size_t i = 0; i < toolButtons.size(); i++) {
		const ToolButton& b = kToolButtons[i];
		const std::wstring label = W(b.command == CMD_RUNNING ? "Pause" : b.label);
		SIZE s = {};
		GetTextExtentPoint32W(dc, label.c_str(), (int)label.size(), &s);
		const int w = std::max(sc(44), (int)s.cx + sc(20));
		if (b.command == CMD_DARK) {
			place(toolButtons[i], rc.right - sc(6) - w, by, w, bh);
			continue;
		}
		if (b.gapBefore) x += sc(10);
		place(toolButtons[i], x, by, w, bh);
		x += w + sc(3);
		if (b.command == CMD_STEP) {
			x += sc(4);
			place(stepEdit, x, by + sc(1), sc(58), bh - sc(2));
			x += sc(58);
			place(stepUpDown, x, by + sc(1), sc(18), bh - sc(2));
			x += sc(20);
		}
	}
	SelectObject(dc, old);
	ReleaseDC(hwnd, dc);

	int top = toolbarHeight();
	// The banner.
	if (bannerKind >= 0) {
		int bx = rc.right - sc(8);
		for (auto it = bannerButtons.rbegin(); it != bannerButtons.rend(); ++it) {
			const int w = std::max(sc(70), (int)(textWidth(windowText(*it), 9 * 96.0f / 72) * dpi / 96) + sc(24));
			bx -= w;
			place(*it, bx, top + sc(5), w, sc(26));
			bx -= sc(6);
		}
		place(bannerLabel, sc(10), top, bx - sc(16), bannerHeight());
		top += bannerHeight();
	}

	// The status bar sizes itself along the bottom.
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

	int left = 0;
	if (prefs().showPalette) {
		const int pw = std::min<int>(sc(prefs().paletteWidth), std::max<int>(sc(120), rc.right - sc(200)));
		place(paletteHost, 0, top, pw, bottom - top);
		left = pw + sc(6);
	}

	// The tabs along the top of the canvas, the + at their right.
	const int tabH = sc(28), plusW = sc(28);
	place(tabs, left, top, rc.right - left - plusW - sc(4), tabH);
	place(newTabButton, rc.right - plusW - sc(2), top + sc(2), plusW, tabH - sc(4));
	const int canvasTop = top + tabH;
	for (Canvas* c : canvases) place(c->widget(), left, canvasTop, rc.right - left, bottom - canvasTop);
	EndDeferWindowPos(defer);
	InvalidateRect(hwnd, nullptr, TRUE);
}

// ---- Tabs ------------------------------------------------------------------------

std::string CircuitWindow::pageName(int page) const {
	const char* n = cl_document_page_name(doc, page);
	if (n && *n) return n;
	return strf("Page %d", page + 1);
}

void CircuitWindow::updateTabLabels() {
	const int n = (int)canvases.size();
	for (int i = 0; i < n; i++) {
		const int p = canvases[i]->page();
		if (p < 0) continue;
		std::wstring text = W(pageName(p));
		// Ampersands are shown as they are.
		std::wstring shown;
		for (wchar_t c : text) { if (c == L'&') shown += L'&'; shown += c; }
		wchar_t now[512] = L"";
		TCITEMW get = {};
		get.mask = TCIF_TEXT;
		get.pszText = now;
		get.cchTextMax = 511;
		SendMessageW(tabs, TCM_GETITEMW, i, (LPARAM)&get);
		if (shown == now) continue;
		TCITEMW t = {};
		t.mask = TCIF_TEXT;
		t.pszText = &shown[0];
		SendMessageW(tabs, TCM_SETITEMW, i, (LPARAM)&t);
	}
}

// Make the tabs match the document's pages: after opening, a new page, a
// close, an undo that brings one back, a move.
void CircuitWindow::syncTabs() {
	syncing = true;
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
	// The tab control is rebuilt to match (it's only labels).
	SendMessageW(tabs, TCM_DELETEALLITEMS, 0, 0);
	for (int i = 0; i < n; i++) {
		std::wstring text = L" ";
		TCITEMW t = {};
		t.mask = TCIF_TEXT;
		t.pszText = &text[0];
		SendMessageW(tabs, TCM_INSERTITEMW, i, (LPARAM)&t);
	}
	int sel = 0;
	for (int i = 0; i < n; i++) if (canvases[i]->pageKey() == frontKey) sel = i;
	SendMessageW(tabs, TCM_SETCURSEL, sel, 0);
	for (int i = 0; i < n; i++) canvases[i]->show(i == sel);
	lastPageCount = n;
	updateTabLabels();
	syncing = false;
	layout();
	redrawMiniMap();
}

Canvas* CircuitWindow::currentCanvas() const {
	if (tabs == nullptr) return nullptr;
	const int i = (int)SendMessageW(tabs, TCM_GETCURSEL, 0, 0);
	return i >= 0 && i < (int)canvases.size() ? canvases[i] : nullptr;
}

int CircuitWindow::currentPage() const {
	Canvas* c = currentCanvas();
	const int p = c ? c->page() : -1;
	return p >= 0 ? p : 0;
}

void CircuitWindow::showPage(int index) {
	if (index < 0 || index >= (int)canvases.size()) return;
	const int now = (int)SendMessageW(tabs, TCM_GETCURSEL, 0, 0);
	if (now == index) return;
	// Leaving a page lets go of its selection, as the wx app does.
	if (Canvas* old = currentCanvas()) {
		if (old->page() >= 0) {
			old->cancelDrag();
			cl_edit_select_none(doc, old->page());
		}
	}
	SendMessageW(tabs, TCM_SETCURSEL, index, 0);
	pageSwitched();
}

void CircuitWindow::pageSwitched() {
	Canvas* front = currentCanvas();
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
	redrawMiniMap();
}

void CircuitWindow::tabContextMenu(int index, POINT screen) {
	if (index >= 0) showPage(index);
	HMENU m = CreatePopupMenu();
	item(m, CMD_RENAME_TAB, "Re&name Tab…");
	item(m, CMD_CLOSE_TAB, "&Close Tab\tCtrl+W");
	item(m, CMD_REOPEN_TAB, "&Reopen Closed Tab\tCtrl+Shift+T");
	separator(m);
	const int at = (int)SendMessageW(tabs, TCM_GETCURSEL, 0, 0), n = (int)canvases.size();
	AppendMenuW(m, MF_STRING | (at > 0 ? 0 : MF_GRAYED), 1, L"Move &Left");
	AppendMenuW(m, MF_STRING | (at < n - 1 ? 0 : MF_GRAYED), 2, L"Move Ri&ght");
	separator(m);
	item(m, CMD_NEW_TAB, "&New Tab\tCtrl+T");
	updateMenu(m);
	const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
	DestroyMenu(m);
	if (cmd == 1 || cmd == 2) {
		const int from = currentPage();
		const int to = cmd == 1 ? from - 1 : from + 1;
		if (to < 0 || to >= cl_document_page_count(doc)) return;
		// A tab without a name of its own is called by its place ("Page 2"):
		// pin those names first, so moving a tab doesn't rename the others.
		for (int i = 0; i < cl_document_page_count(doc); i++) {
			const char* name = cl_document_page_name(doc, i);
			if (name == nullptr || *name == 0) cl_document_rename_page(doc, i, pageName(i).c_str());
		}
		cl_document_move_page(doc, from, to);
		changes++;
		syncTabs();
		updateTitle();
	} else if (cmd) {
		run(cmd);
	}
}

LRESULT CALLBACK CircuitWindow::tabsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
	CircuitWindow* w = reinterpret_cast<CircuitWindow*>(data);
	auto hit = [&]() {
		TCHITTESTINFO t = {};
		t.pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		return (int)SendMessageW(h, TCM_HITTEST, 0, (LPARAM)&t);
	};
	switch (msg) {
	case WM_LBUTTONDBLCLK: {
		const int i = hit();
		if (i >= 0) { guarded("renaming a tab", [&] { w->showPage(i); w->renamePage(w->currentPage()); }); return 0; }
		break;
	}
	case WM_MBUTTONUP: {
		// A middle click closes the tab, as in browsers.
		const int i = hit();
		if (i >= 0 && i < (int)w->canvases.size()) {
			const int p = w->canvases[i]->page();
			if (p >= 0) guarded("closing a tab", [&] { w->closePage(p); });
			return 0;
		}
		break;
	}
	case WM_RBUTTONUP: {
		const int i = hit();
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ClientToScreen(h, &p);
		guarded("the tab menu", [&] { w->tabContextMenu(i, p); });
		return 0;
	}
	}
	return DefSubclassProc(h, msg, wp, lp);
}

// ---- The clock -------------------------------------------------------------------

void CircuitWindow::tick() {
	const double t = nowSeconds();
	const double elapsed = (t - lastTick) * 1000.0;   // ms
	lastTick = t;
	Canvas* c = currentCanvas();
	if (c) c->stepAnimation();
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
	if (messageAt > 0 && since(messageAt) > 5) {
		messageAt = 0;
		SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)L"");
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
	if (cl_document_page_count(doc) != lastPageCount) syncTabs();
	redraw();
	selectionChanged();
	updateActions();
	updateTitle();
	updateBanner();
	updateTabLabels();
	statusDirty = true;
}

void CircuitWindow::note(const std::string& message) {
	SendMessageW(statusBar, SB_SETTEXTW, 0, (LPARAM)W(message).c_str());
	messageAt = nowSeconds();
}

void CircuitWindow::lockNudge() {
	if (simViewOn) note("Leave Simulation View (Escape) to edit.");
	else note("The circuit is locked. Unlock it (Simulate > Lock) to edit.");
	MessageBeep(MB_OK);
}

std::string CircuitWindow::displayName() const {
	if (!path.empty()) return baseName(path);
	return recoveredName.empty() ? "Untitled" : recoveredName;
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
	SendMessageW(statusBar, SB_SETTEXTW, 1, (LPARAM)W(s).c_str());
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
	for (size_t i = 0; i < toolButtons.size(); i++) {
		const int command = kToolButtons[i].command;
		const bool on = commandEnabled(command);
		if ((IsWindowEnabled(toolButtons[i]) != FALSE) != on) EnableWindow(toolButtons[i], on);
		const int check = commandChecked(command);
		if (check >= 0) {
			const LRESULT want = check ? BST_CHECKED : BST_UNCHECKED;
			if (SendMessageW(toolButtons[i], BM_GETCHECK, 0, 0) != want) SendMessageW(toolButtons[i], BM_SETCHECK, want, 0);
		}
	}
}

void CircuitWindow::updateRunUI() {
	if (runButton) setWindowText(runButton, isRunning ? "Pause" : "Run");
	updateActions();
	statusDirty = true;
}

void CircuitWindow::updateBanner() {
	int kind = -1;
	std::string text;
	std::vector<std::pair<const char*, int>> buttons;
	if (cl_edit_tidy_active(doc)) {
		kind = 0;
		text = cl_edit_tidy_mode(doc) == 1
			? "Tidy Up by signal flow: a preview. Enter keeps it, Escape puts it back, Tab tries keeping the shape."
			: "Tidy Up: a preview. Enter keeps it, Escape puts it back, Tab tries arranging by signal flow.";
		buttons = { { "Keep", CMD_TIDY_KEEP }, { "Put Back", CMD_TIDY_REVERT }, { "Other Way", CMD_TIDY_SWITCH } };
		kind = cl_edit_tidy_mode(doc) == 1 ? 1 : 0;
	} else if (simViewOn) {
		kind = 2;
		text = "Simulation View: switches and keypads still work. Space pauses; Escape goes back to editing.";
		buttons = { { "Edit", CMD_SIM_VIEW } };
	} else if (lockedOn) {
		kind = 3;
		text = "Locked: switches and keypads still work, but nothing can be moved or changed.";
		buttons = { { "Unlock", CMD_LOCK } };
	}
	if (kind == bannerKind) return;
	bannerKind = kind;
	for (HWND b : bannerButtons) DestroyWindow(b);
	bannerButtons.clear();
	for (auto& b : buttons) {
		HWND h = CreateWindowExW(0, L"BUTTON", W(b.first).c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd,
		                         (HMENU)(INT_PTR)b.second, appInstance(), nullptr);
		SendMessageW(h, WM_SETFONT, (WPARAM)uiFont(dpi), TRUE);
		bannerButtons.push_back(h);
	}
	setWindowText(bannerLabel, text);
	ShowWindow(bannerLabel, kind >= 0 ? SW_SHOW : SW_HIDE);
	layout();
}

void CircuitWindow::themeChanged() {
	setDarkTitleBar(hwnd, prefs().dark);
	updateActions();
	if (palette) palette->themeChanged();
	for (Canvas* c : canvases) c->redraw();
	redrawMiniMap();
	if (scope) scope->update();
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

LRESULT CircuitWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_TIMER:
		if (wp == kClockTimer) tick();
		return 0;
	case WM_SIZE:
		if (wp == SIZE_MINIMIZED) return 0;
		prefs().windowMaximized = wp == SIZE_MAXIMIZED;
		if (wp == SIZE_RESTORED) {
			RECT r;
			GetWindowRect(hwnd, &r);
			prefs().windowWidth = MulDiv(r.right - r.left, 96, (int)dpi);
			prefs().windowHeight = MulDiv(r.bottom - r.top, 96, (int)dpi);
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
		for (HWND b : bannerButtons) SendMessageW(b, WM_SETFONT, (WPARAM)uiFont(dpi), TRUE);
		layout();
		for (Canvas* c : canvases) c->redraw();
		return 0;
	}
	case WM_ACTIVATE:
		if (LOWORD(wp) != WA_INACTIVE) {
			// Back to the canvas, unless a text box had the keyboard.
			HWND f = GetFocus();
			wchar_t cls[32] = L"";
			if (f) GetClassNameW(f, cls, 32);
			if (f == nullptr || f == hwnd || (lstrcmpiW(cls, L"Edit") != 0)) {
				if (Canvas* c = currentCanvas()) c->focus();
			}
			return 0;
		}
		break;
	case WM_INITMENUPOPUP:
		updateMenu((HMENU)wp);
		return 0;
	case WM_COMMAND: {
		const int id = LOWORD(wp), code = HIWORD(wp);
		if (id == kStepEditId) {
			if (code == EN_CHANGE) {
				BOOL bad = FALSE;
				const int ms = (int)SendMessageW(stepUpDown, UDM_GETPOS32, 0, (LPARAM)&bad);
				if (!bad) setStepMs(ms);
			}
			return 0;
		}
		if (id >= CMD_RECENT && id <= CMD_RECENT_LAST) {
			const size_t i = (size_t)(id - CMD_RECENT);
			std::vector<std::string> shown;
			for (const std::string& r : prefs().recent) if (fileExists(r)) shown.push_back(r);
			if (i < shown.size()) openCircuit(shown[i], this);
			return 0;
		}
		if (id >= CMD_NEW && id < CMD_RECENT) {
			run(id);
			// A toolbar button keeps the keyboard off the canvas otherwise.
			if (lp && (HWND)lp != stepEdit && isCircuitWindow(this)) if (Canvas* c = currentCanvas()) c->focus();
			return 0;
		}
		break;
	}
	case WM_NOTIFY: {
		const NMHDR* n = reinterpret_cast<const NMHDR*>(lp);
		if (n->hwndFrom == tabs) {
			if (n->code == TCN_SELCHANGING) {
				// Leaving a page lets go of its selection, as the wx app does.
				if (Canvas* old = currentCanvas()) {
					if (old->page() >= 0) {
						old->cancelDrag();
						cl_edit_select_none(doc, old->page());
					}
				}
				return FALSE;
			}
			if (n->code == TCN_SELCHANGE && !syncing) { pageSwitched(); return 0; }
		}
		break;
	}
	case WM_SETCURSOR: {
		POINT p;
		GetCursorPos(&p);
		ScreenToClient(hwnd, &p);
		const RECT s = splitterRect();
		if ((HWND)wp == hwnd && PtInRect(&s, p)) {
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
			splitterGrab = p.x - s.left;
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
			prefs().paletteWidth = std::min(std::max(w, 140), std::min(800, MulDiv(rc.right, 96, (int)dpi) - 240));
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
		if (confirmClose()) destroy();
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kClockTimer);
		return 0;
	case WM_ERASEBKGND: {
		// The bars' background, and a grip line down the splitter.
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_BTNFACE));
		const RECT s = splitterRect();
		if (s.right > s.left) {
			RECT line = { (s.left + s.right) / 2, s.top, (s.left + s.right) / 2 + 1, s.bottom };
			FillRect((HDC)wp, &line, GetSysColorBrush(COLOR_BTNSHADOW));
		}
		if (bannerKind >= 0) {
			RECT b = { 0, toolbarHeight(), rc.right, toolbarHeight() + bannerHeight() };
			HBRUSH info = CreateSolidBrush(RGB(255, 244, 206));
			FillRect((HDC)wp, &b, info);
			DeleteObject(info);
		}
		return 1;
	}
	case WM_CTLCOLORSTATIC:
		if ((HWND)lp == bannerLabel) {
			HDC dc = (HDC)wp;
			SetBkColor(dc, RGB(255, 244, 206));
			SetTextColor(dc, RGB(40, 32, 0));
			static HBRUSH info = CreateSolidBrush(RGB(255, 244, 206));
			return (LRESULT)info;
		}
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

bool CircuitWindow::confirmClose() {
	if (!isDirty()) return true;
	const std::string text = strf("Save the changes to “%s” before closing?\n\nIf you don't save, your changes will be lost.",
	                              displayName().c_str());
	const int r = MessageBoxW(hwnd, W(text).c_str(), L"CedarLogic", MB_YESNOCANCEL | MB_ICONWARNING);
	if (r == IDNO) return true;
	if (r == IDYES) return save();
	return false;
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
	case CMD_OPEN: chooseAndOpen(this); break;
	case CMD_OPEN_SAMPLE: openPracticeCircuit(this); break;
	case CMD_SAVE: save(); break;
	case CMD_SAVE_AS: saveAs(); break;
	case CMD_EXPORT_IMAGE: exportImage(); break;
	case CMD_EXPORT_V2: exportOlder(2); break;
	case CMD_EXPORT_V1: exportOlder(1); break;
	case CMD_PRINT: print(); break;
	case CMD_CLOSE_WINDOW: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
	case CMD_QUIT: quitApp(); break;
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
	case CMD_HELP: {
		const std::string page = resourcesDir() + "\\help\\Introduction.htm";
		if (fileExists(page)) openExternally(hwnd, page);
		else showShortcuts();
		break;
	}
	case CMD_ABOUT:
		showMessage(hwnd, Tone::Info, "CedarLogic " CL_VERSION " (native Windows, testing)",
		            "A digital logic simulator, from Cedarville University.\n\n"
		            "This is the native Windows app: plain Windows controls and Direct2D on the shared CedarLogic "
		            "engine, with nothing else to install.\n\nhttps://github.com/leviholliday/CedarLogic");
		break;
	default: break;
	}
}

// ---- Files ---------------------------------------------------------------------

void CircuitWindow::replaceDocument(CLDocument* newDoc, const std::string& newPath) {
	for (Canvas* c : canvases) c->cancelDrag();
	for (Canvas* c : canvases) delete c;
	canvases.clear();
	SendMessageW(tabs, TCM_DELETEALLITEMS, 0, 0);
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
	SendMessageW(stepUpDown, UDM_SETPOS32, 0, cl_document_step_ms(doc));
	syncTabs();
	appearStart = nowSeconds();
	updateTitle();
	updateActions();
	updateRunUI();
	updateBanner();
	if (Canvas* c = currentCanvas()) c->focus();
}

bool CircuitWindow::writeTo(const std::string& file) {
	// Written to a temporary beside it, then moved over it, so a failed save
	// leaves the old file as it was.
	const std::string text = cl_document_save_text(doc);   // marks the engine's copy saved
	std::string err;
	{
		const std::wstring target = W(file), tmp = target + L".saving";
		HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			err = "The file couldn't be written there. Check that the folder exists and you can save to it.";
		} else {
			DWORD wrote = 0;
			const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size() &&
			                FlushFileBuffers(h);
			CloseHandle(h);
			if (!ok) err = "The disk may be full.";
			else if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				err = "The old file couldn't be replaced. Is it open in another program, or read-only?";
			if (!err.empty()) DeleteFileW(tmp.c_str());
		}
	}
	if (!err.empty()) {
		forceDirty = true;
		showMessage(hwnd, Tone::Error, "The circuit couldn't be saved", err);
		updateTitle();
		return false;
	}
	forceDirty = false;
	path = file;
	recoveredName.clear();
	// Saved: the recovery copy isn't needed until the next change.
	recovery::remove(recoveryBase);
	changesAtRecovery = changes;
	prefs().noteRecent(file);
	updateTitle();
	note("Saved.");
	return true;
}

bool CircuitWindow::save() {
	if (path.empty()) return saveAs();
	return writeTo(path);
}

static const std::vector<FileFilter> kCdlFilters = { { "CedarLogic circuits (*.cdl)", "*.cdl" }, { "All files", "*.*" } };

bool CircuitWindow::saveAs() {
	const std::string file = chooseSaveFile(hwnd, "Save Circuit", displayName() + ".cdl", kCdlFilters, ".cdl");
	if (file.empty()) return false;
	return writeTo(file);
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

// Export the page in front as a PNG picture, in the style chosen.
void CircuitWindow::exportImage() {
	const int p = currentPage();
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, p, &l, &b, &r, &t)) { note("This page is empty: nothing to export."); return; }
	static int style = CL_STYLE_LIGHT;
	if (!chooseExportStyle(hwnd, style)) return;
	std::string name = displayName();
	if (cl_document_page_count(doc) > 1) name += " - " + pageName(p);
	const std::string file = chooseSaveFile(hwnd, "Export as Image", name + ".png", { { "PNG pictures (*.png)", "*.png" } }, ".png");
	if (file.empty()) return;

	// Ten points a grid unit; two pixels a point.
	const double margin = 16, scale = 2;
	const double wPts = std::min(4000.0, std::max(300.0, (r - l) * 10 + 2 * margin));
	const double hPts = std::min(4000.0, std::max(200.0, (t - b) * 10 + 2 * margin));
	IWICImagingFactory* wic = wicFactory();
	IWICBitmap* bmp = nullptr;
	ID2D1RenderTarget* rt = nullptr;
	bool ok = false;
	if (wic && SUCCEEDED(wic->CreateBitmap((UINT)(wPts * scale), (UINT)(hPts * scale), GUID_WICPixelFormat32bppPBGRA,
	                                       WICBitmapCacheOnLoad, &bmp))) {
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
		if (SUCCEEDED(d2dFactory()->CreateWicBitmapRenderTarget(bmp, props, &rt))) {
			rt->BeginDraw();
			rt->SetTransform(D2D1::Matrix3x2F::Scale((float)scale, (float)scale));
			rt->Clear(style == CL_STYLE_DARK ? d2dColor(Palette{ true, false }.canvas()) : D2D1::ColorF(1, 1, 1, 1));
			cl_document_draw_fitted(doc, p, rt, wPts, hPts, margin, scale, style);
			ok = SUCCEEDED(rt->EndDraw()) && savePng(wic, bmp, file);
			rt->Release();
		}
		bmp->Release();
	}
	if (!ok) { showMessage(hwnd, Tone::Error, "The image couldn't be saved", file); return; }
	note("Exported " + baseName(file) + ".");
}

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

bool CircuitWindow::screenshot(const std::string& file) {
	RECT rc;
	GetWindowRect(hwnd, &rc);
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
	HGDIOBJ old = SelectObject(mem, bmp);
	const bool drawn = PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT) != FALSE;
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
			SendMessageW(tabs, TCM_SETCURSEL, show, 0);
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
	SendMessageW(tabs, TCM_SETCURSEL, i, 0);
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
		if (show >= 0) { SendMessageW(tabs, TCM_SETCURSEL, show, 0); pageSwitched(); }
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
	const int at = (int)SendMessageW(tabs, TCM_GETCURSEL, 0, 0);
	showPage(((at + delta) % n + n) % n);
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
	int shown = 0;
	for (const std::string& path : prefs().recent) {
		if (!fileExists(path)) continue;
		// Ampersands in a file name aren't mnemonics.
		std::string label;
		for (char c : baseName(path)) { if (c == '&') label += '&'; label += c; }
		if (shown < 9) label = strf("&%d  ", shown + 1) + label;
		AppendMenuW(recentMenu, MF_STRING, CMD_RECENT + shown, W(label).c_str());
		if (++shown >= 10) break;
	}
	if (shown == 0) AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"No recent circuits");
}
