// CedarLogic for Windows (native): the application -- starting up, the
// message loop and its keyboard shortcuts, opening circuits, and the list of
// open windows.

#include "App.h"
#include "Canvas.h"
#include "Dialogs.h"
#include "Feedback.h"
#include "Help.h"
#include "Library.h"
#include "Recovery.h"
#include "Shortcuts.h"
#include "Toolbar.h"
#include "Updater.h"
#include "Welcome.h"
#include "Window.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace {

// --screenshot <out.png>: once the window is up, draw it to a PNG and quit
// (a check that a build really starts, drawing and all; used by CI).
std::string gScreenshot;
int gExitCode = 0;
// For the screenshot runs: --dark or --light for this run, --sim-view on.
int gTheme = -1;
bool gSimView = false;
// --dialog <preferences|shortcuts|truth-table|add-gate|scope|...>: open it, and the
// screenshot is of it.
int gDialog = 0;
std::string gFormula;   // --formula: what Build from Formula opens with
int gTruthTab = -1;     // --truth-tab: which tab the truth table opens on
// --timing <out.png> (with --dialog scope): the oscilloscope's timing
// diagram too; --timing-color for it in color.
std::string gTiming;
bool gTimingColor = false;
// --splash-frame <seconds> <out.png> [--first-launch]: the launch screen at
// that moment, drawn to a PNG, and quit.
double gSplashAt = 0;
std::string gSplashFile;
bool gFirstLaunch = false;
std::string gPlace;     // --place: a gate by library name, put on the page and selected
std::string gSelect;    // --select: the first part Find finds, selected (for --dialog gate-settings)
std::string gHelpPage;  // --help-page: Help opens on it (--dialog help)
int gPage = 0;          // --page: What's New opens on it (--dialog whatsnew)
int gToolbarStyle = -1; // --toolbar-style classic|seamless|minimal: the toolbar in that style, for this run

Prefs gPrefsBefore;     // the settings as loaded: put back after a test run changed them for itself

void writeOut(const std::string& text) {
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	if (out == nullptr || out == INVALID_HANDLE_VALUE) {
		if (AttachConsole(ATTACH_PARENT_PROCESS)) out = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	DWORD wrote = 0;
	if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), (DWORD)text.size(), &wrote, nullptr);
}

void CALLBACK screenshotTimer(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().back();
	gExitCode = 1;
	HWND dialog = w ? GetLastActivePopup(w->window()) : nullptr;
	if (dialog == (w ? w->window() : nullptr)) dialog = nullptr;
	if (help::window()) dialog = help::window();
	if (welcome::tourWindow()) dialog = welcome::tourWindow();
	if (w && w->screenshot(gScreenshot, gDialog ? dialog : nullptr)) gExitCode = 0;
	writeOut(strf("%s %s\n", gExitCode ? "couldn't write" : "wrote", gScreenshot.c_str()));
	if (!gTiming.empty()) {
		const bool ok = w && w->scopeWindow() && w->scopeWindow()->saveTimingDiagram(gTiming);
		writeOut(strf("%s %s\n", ok ? "wrote" : "couldn't write", gTiming.c_str()));
	}
	// A dialog is still open (its own loop is running): just stop.
	if (gDialog) {
		if (gToolbarStyle >= 0) prefs().toolbarStyle = gPrefsBefore.toolbarStyle;
		prefs().save();
		ExitProcess((UINT)gExitCode);
	}
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) c->destroy();
	PostQuitMessage(gExitCode);
}

// --click-test: clicks on the toolbar's buttons and the window's drawn
// Minimize and Close, sent as Windows sends a real one (the press, which
// takes the pointer, then the release), each checked for what it should do.
// A line per click, PASS, FAIL or SKIP; the exit code is 1 if any failed (CI
// runs it: a picture of the window can't tell whether its buttons work).
bool gClickTest = false;
HWND gClickWindow = nullptr;
size_t gClickNext = 0;
long gClickBefore = 0;
int gClickFailures = 0;
bool gClickClosing = false;   // the drawn Close was clicked: the window should go

void report(const char* result, const std::string& what) {
	writeOut(strf("%s  %s\n", result, what.c_str()));
	if (strcmp(result, "FAIL") == 0) gClickFailures++;
}

CircuitWindow* clickWindow() {
	for (CircuitWindow* w : circuitWindows()) if (w->window() == gClickWindow) return w;
	return nullptr;
}

void closeAll() {
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) c->destroy();
}

// Press and release the left button over one of the toolbar's buttons.
// False when the bar doesn't show that button.
bool clickButton(CircuitWindow* w, int button, const char* name) {
	Toolbar* bar = w->toolbarWidget();
	POINT p;
	if (bar == nullptr || !bar->buttonPoint(button, p)) return false;
	const HWND h = bar->widget();
	const LPARAM at = MAKELPARAM(p.x, p.y);
	SendMessageW(h, WM_MOUSEMOVE, 0, at);
	SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, at);
	if (GetCapture() != h) writeOut(strf("note  %s: the bar didn't take the pointer on the press\n", name));
	SendMessageW(h, WM_LBUTTONUP, 0, at);
	return true;
}

struct ClickCase {
	const char* name;
	int button;                                   // a command, or Toolbar::kMinimize
	bool mayBeHidden;                             // on the bar's left, which a narrow window leaves out
	std::function<long(CircuitWindow*)> state;    // what the click should change
	std::function<bool(long before, long after)> worked;
	std::function<void(CircuitWindow*)> tidy;     // put things back for the next click
};

const std::vector<ClickCase>& clickCases() {
	auto flipped = [](long before, long after) { return before != after; };
	auto oneMore = [](long before, long after) { return after == before + 1; };
	static const std::vector<ClickCase> cases = {
		{ "Zoom In", CMD_ZOOM_IN, true,
		  [](CircuitWindow* w) -> long { Canvas* c = w->currentCanvas(); return c ? c->zoomPercent() : 0; },
		  [](long before, long after) { return after > before; }, nullptr },
		{ "New Tab", CMD_NEW_TAB, false, [](CircuitWindow* w) -> long { return w->tabCount(); }, oneMore, nullptr },
		{ "Pause", CMD_RUNNING, false, [](CircuitWindow* w) -> long { return w->running(); }, flipped, nullptr },
		{ "Resume", CMD_RUNNING, false, [](CircuitWindow* w) -> long { return w->running(); }, flipped, nullptr },
		{ "Simulation View on", CMD_SIM_VIEW, false, [](CircuitWindow* w) -> long { return w->simView(); }, flipped, nullptr },
		{ "Simulation View off", CMD_SIM_VIEW, false, [](CircuitWindow* w) -> long { return w->simView(); }, flipped, nullptr },
		{ "Lock", CMD_LOCK, false, [](CircuitWindow* w) -> long { return w->locked(); }, flipped, nullptr },
		{ "Unlock", CMD_LOCK, false, [](CircuitWindow* w) -> long { return w->locked(); }, flipped, nullptr },
		{ "New circuit", CMD_NEW, true, [](CircuitWindow*) -> long { return (long)circuitWindows().size(); }, oneMore,
		  [](CircuitWindow* w) {
			  for (CircuitWindow* o : std::vector<CircuitWindow*>(circuitWindows())) if (o != w) o->destroy();
		  } },
		{ "Minimize (drawn)", Toolbar::kMinimize, false,
		  [](CircuitWindow* w) -> long { return IsIconic(w->window()) ? 1 : 0; },
		  [](long, long after) { return after == 1; }, [](CircuitWindow* w) { ShowWindow(w->window(), SW_RESTORE); } },
	};
	return cases;
}

// After the message loop: the drawn Close's result, and the summary.
int finishClickTest() {
	if (gClickClosing) report(IsWindow(gClickWindow) ? "FAIL" : "PASS", "Close (drawn): the window closed");
	gClickClosing = false;
	writeOut(gClickFailures ? strf("click test: %d failed\n", gClickFailures) : std::string("click test: all passed\n"));
	prefs() = gPrefsBefore;
	return gClickFailures ? 1 : 0;
}

// A step a tick: check the last click's effect, then make the next.
int clickTestStep() {
	CircuitWindow* w = clickWindow();
	if (w == nullptr) { report("FAIL", "the window went away"); return 0; }
	if (gClickClosing) {   // still here: Close did nothing
		report("FAIL", "Close (drawn): the window is still open");
		gClickClosing = false;
		closeAll();
		return 0;
	}
	const std::vector<ClickCase>& cases = clickCases();
	if (gClickNext > 0) {
		const ClickCase& done = cases[gClickNext - 1];
		const long after = done.state(w);
		report(done.worked(gClickBefore, after) ? "PASS" : "FAIL", strf("%s: %ld, then %ld", done.name, gClickBefore, after));
		if (done.tidy) done.tidy(w);
	}
	while (gClickNext < cases.size()) {
		const ClickCase& c = cases[gClickNext++];
		// The canvas drawn first, so its first fit can't undo a zoom.
		if (Canvas* cv = w->currentCanvas()) UpdateWindow(cv->widget());
		gClickBefore = c.state(w);
		if (clickButton(w, c.button, c.name)) return 500;
		// Minimal leaves most tools to the ••• menu.
		if (w->toolbarWidget() && !w->toolbarWidget()->hasButton(c.button)) {
			report("SKIP", strf("%s: not in this style of toolbar", c.name));
			continue;
		}
		const int width = w->toolbarWidget() ? (int)w->toolbarWidget()->width() : 0;
		report(c.mayBeHidden ? "SKIP" : "FAIL", strf("%s: not on the toolbar at this width (%d points)", c.name, width));
	}
	// Last, the drawn Close: the window goes, and with it the app (the
	// result is read once the message loop ends).
	if (!clickButton(w, Toolbar::kClose, "Close")) {
		report("FAIL", "Close (drawn): not on the toolbar");
		closeAll();
		return 0;
	}
	gClickClosing = true;
	return 1500;
}

void CALLBACK clickTestTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (id) KillTimer(nullptr, id);
	int next = 0;
	guarded("the click test", [&] { next = clickTestStep(); });
	if (next > 0) SetTimer(nullptr, 0, (UINT)next, clickTestTimer);
	else if (!circuitWindows().empty()) {
		report("FAIL", "the click test stopped early");
		closeAll();
	}
}

void CALLBACK clickTestStart(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	if (w == nullptr) { report("FAIL", "no window opened"); PostQuitMessage(1); return; }
	gClickWindow = w->window();
	SetForegroundWindow(gClickWindow);
	const int style = prefs().toolbarStyle;
	writeOut(strf("click test: the %s toolbar\n", style == TSClassic ? "Classic" : style == TSMinimal ? "Minimal" : "Seamless"));
	// As wide as the whole bar needs (a narrow bar leaves out the tools on
	// its left), past the screen's edge if it must: not asked first, Windows
	// doesn't hold the window to the screen's size (CI's is 1024 wide).
	POINT p;
	if (w->toolbarWidget() && !w->toolbarWidget()->buttonPoint(CMD_ZOOM_IN, p)) {
		RECT r;
		GetWindowRect(gClickWindow, &r);
		SetWindowPos(gClickWindow, nullptr, 0, 0, scaled(1400, dpiOf(gClickWindow)), r.bottom - r.top,
		             SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING);
	}
	clickTestTimer(nullptr, 0, 0, 0);
}

// A click that hangs (a dialog no one answers) fails rather than waits.
void CALLBACK clickTestWatchdog(HWND, UINT, UINT_PTR, DWORD) {
	report("FAIL", "the click test took too long");
	writeOut(strf("click test: %d failed\n", gClickFailures));
	prefs() = gPrefsBefore;
	prefs().save();
	ExitProcess(1);
}

// Once the first window is up, offer back work a CedarLogic that stopped
// unexpectedly left behind.
void CALLBACK recoveryTimer(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	guarded("recovering work", [&] { recovery::offer(w); });
}

bool down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// What a text box does itself with these: types them (any key without Ctrl
// but the F keys), or edits its text with them (copies the text, not the
// gates), so a shortcut can't have them there.
bool isTypingKey(const shortcuts::Key& k) {
	const UINT v = k.vk;
	if (!k.ctrl) return !(v >= VK_F1 && v <= VK_F24);
	const bool words = v == VK_LEFT || v == VK_RIGHT || v == VK_HOME || v == VK_END || v == VK_BACK || v == VK_DELETE;
	if (k.shift) return words;
	return words || v == 'A' || v == 'C' || v == 'V' || v == 'X' || v == 'Z' || v == 'Y';
}

CircuitWindow* windowFor(HWND h) {
	HWND root = GetAncestor(h, GA_ROOT);
	for (CircuitWindow* w : circuitWindows()) if (w->window() == root) return w;
	return nullptr;
}

}  // namespace

bool handleShortcut(CircuitWindow* w, const MSG& msg) {
	if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
	if (down(VK_MENU)) return false;   // Alt belongs to the menus
	const bool ctrl = down(VK_CONTROL), shift = down(VK_SHIFT);
	// Ctrl+Tab: the tab switcher (Escape, while it's up, leaves it).
	if (msg.wParam == VK_TAB && ctrl) { guarded("switching tabs", [&] { w->switchTabs(shift); }); return true; }
	if (msg.wParam == VK_ESCAPE && w->switcherActive()) { w->cancelSwitcher(); return true; }
	// The keys Settings > Shortcuts gives the menus' commands. (The canvas's
	// single keys, A, R, S..., are its own: it looks them up as they reach it.)
	const shortcuts::Key key = shortcuts::pressed((UINT)msg.wParam);
	const shortcuts::Action* a = shortcuts::match(key, false);
	if (a == nullptr) return false;
	wchar_t cls[32] = L"";
	GetClassNameW(msg.hwnd, cls, 32);
	if (lstrcmpiW(cls, L"Edit") == 0 && isTypingKey(key)) return false;
	guarded("a shortcut", [&] { w->run(a->command); });
	return true;
}

// ---- Shared with the windows -----------------------------------------------------

std::vector<CircuitWindow*>& circuitWindows() {
	static std::vector<CircuitWindow*> all;
	return all;
}

CircuitWindow* newCircuitWindow() { return new CircuitWindow(cl_document_new(), ""); }

// Each window asks about its own changes; stop at the first "Cancel".
bool quitApp() {
	std::vector<CircuitWindow*> all = circuitWindows();
	// A window with a dialog or the truth table up is in the middle of it
	// (its code is waiting on that): not yet. The dialog comes forward.
	for (CircuitWindow* w : all) {
		if (IsWindowEnabled(w->window())) continue;
		const HWND popup = GetLastActivePopup(w->window());
		SetForegroundWindow(popup ? popup : w->window());
		MessageBeep(MB_ICONWARNING);
		return false;
	}
	for (CircuitWindow* w : all) {
		SetForegroundWindow(w->window());
		if (!w->confirmClose()) return false;
	}
	for (CircuitWindow* w : all) w->destroy();
	PostQuitMessage(0);
	return true;
}

// Every circuit lives in Your Circuits: a .cdl file from elsewhere carries
// on as a copy there (the file itself is left alone; Export gets one out),
// and opening the same file again finds that copy.
bool openCircuit(const std::string& path, CircuitWindow* from) {
	std::string target = path;
	library::Item existing;
	if (!library::contains(path) && library::imported(path, existing)) target = existing.circuit();
	// Already open: bring that window forward.
	for (CircuitWindow* w : circuitWindows()) {
		if (!w->filePath().empty() && lowerCase(w->filePath()) == lowerCase(target)) {
			if (IsIconic(w->window())) ShowWindow(w->window(), SW_RESTORE);
			SetForegroundWindow(w->window());
			return true;
		}
	}
	char err[512] = "";
	CLDocument* doc = cl_document_open(target.c_str(), err, sizeof err);
	if (doc == nullptr) {
		showMessage(from ? from->window() : nullptr, Tone::Error,
		            strf("\u201C%s\u201D couldn't be opened", baseName(path).c_str()), err);
		return false;
	}
	bool importedNow = false;
	if (!library::contains(target)) {
		library::Item it;
		if (library::create(baseName(path), cl_document_save_text(doc), path, it)) {
			target = it.circuit();
			importedNow = true;
		}
		prefs().lastFolder = dirName(path);
	}
	// In the window's place: an untouched new one, or any when Settings says
	// opening replaces the circuit you're in (saved first).
	CircuitWindow* w;
	if (from && (from->isPristine() || (prefs().openReplaces && from->saveQuietly(false)))) { from->replaceDocument(doc, target); w = from; }
	else w = new CircuitWindow(doc, target);
	library::noteLastCircuit(target);
	// What loading had to say (an older format converted, an unknown gate...).
	std::string notes;
	bool warning = false;
	for (int i = 0; i < cl_document_notice_count(doc); i++) {
		notes += std::string("\u2022 ") + cl_document_notice(doc, i) + "\n";
		warning = warning || cl_document_notice_is_warning(doc, i);
	}
	if (warning) showMessage(w->window(), Tone::Warning, "Opened, with notes", notes);
	else if (!notes.empty()) w->note(notes.substr(4, notes.find('\n') - 4));
	else if (importedNow) w->note("In Your Circuits now, as a copy. The file itself is left as it was.");
	return true;
}

void chooseAndOpen(CircuitWindow* from) {
	const std::vector<std::string> files = chooseOpenFiles(
		from ? from->window() : nullptr, "Import a Circuit",
		{ { "CedarLogic circuits (*.cdl)", "*.cdl" }, { "All files", "*.*" } }, true);
	for (const std::string& file : files) {
		openCircuit(file, from);
		from = nullptr;   // the rest get windows of their own
	}
}

// The practice circuit opens as a new, untitled copy, so saving asks where.
void openPracticeCircuit(CircuitWindow* from) {
	const std::string file = resourcesDir() + "\\samples\\practice.cdl";
	char err[512] = "";
	CLDocument* doc = cl_document_open(file.c_str(), err, sizeof err);
	if (doc == nullptr) {
		showMessage(from ? from->window() : nullptr, Tone::Warning, "The practice circuit couldn't be opened", err);
		return;
	}
	if (from && from->isPristine()) from->replaceDocument(doc, "");
	else new CircuitWindow(doc, "");
}

// ---- One CedarLogic at a time ----------------------------------------------------
// Two would each save the same circuits (and settings) over the other's. A
// second start hands its files to the one running, which opens them (or,
// with none, comes forward), and ends.

namespace {

const wchar_t* kInstanceMutex = L"Local\\CedarLogic.Native";
const wchar_t* kWindowClass = L"CedarLogicWindow";   // a circuit window's (Window.cpp)
const ULONG_PTR kHandedFilesTag = 0x434C4F50;        // WM_COPYDATA's: files to open, a line each
std::vector<std::string> gHanded;                    // taken, waiting to be opened

// True when a CedarLogic already running took the files.
bool handToRunning(const std::vector<std::string>& files) {
	// Held for this process's life by the first; the others find it there.
	static HANDLE mutex = nullptr;
	mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
	if (mutex == nullptr || GetLastError() != ERROR_ALREADY_EXISTS) return false;
	std::string text;   // full paths: the running one's current folder isn't this one's
	for (const std::string& f : files) {
		wchar_t full[MAX_PATH * 4];
		const DWORD n = GetFullPathNameW(W(f).c_str(), (DWORD)(sizeof full / sizeof full[0]), full, nullptr);
		text += (n > 0 && n < sizeof full / sizeof full[0] ? U(full) : f) + "\n";
	}
	// It may still be starting (no window yet) or on its way out (an
	// update's restart): wait a few seconds for one or the other.
	for (int tries = 0; tries < 100; tries++) {
		const DWORD gone = WaitForSingleObject(mutex, 0);
		if (gone == WAIT_OBJECT_0 || gone == WAIT_ABANDONED) return false;   // it ended: this one is the one now
		if (HWND other = FindWindowW(kWindowClass, nullptr)) {
			DWORD pid = 0;
			GetWindowThreadProcessId(other, &pid);
			AllowSetForegroundWindow(pid);
			COPYDATASTRUCT cd = { kHandedFilesTag, (DWORD)text.size(), text.empty() ? nullptr : (void*)text.data() };
			DWORD_PTR answer = 0;
			// No answer (it's stuck): open them here after all.
			return SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 10000, &answer) && answer;
		}
		Sleep(100);
	}
	return false;
}

}  // namespace

bool takeHandedFiles(HWND window, const COPYDATASTRUCT* data) {
	if (data == nullptr || data->dwData != kHandedFilesTag) return false;
	const std::string text = data->lpData && data->cbData ? std::string((const char*)data->lpData, data->cbData) : std::string();
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		if (end > at) gHanded.push_back(text.substr(at, end - at));
		at = end + 1;
	}
	PostMessageW(window, kOpenHandedFiles, 0, 0);
	return true;
}

void openHandedFiles(CircuitWindow* w) {
	const std::vector<std::string> files = gHanded;
	gHanded.clear();
	// The first goes into this window when it's an untouched new one (not
	// while a dialog of its is up), the rest into windows of their own.
	CircuitWindow* from = IsWindowEnabled(w->window()) ? w : nullptr;
	const bool intoThis = !files.empty() && from && (from->isPristine() || prefs().openReplaces);
	const size_t windowsBefore = circuitWindows().size();
	for (const std::string& f : files) {
		openCircuit(f, from);
		from = nullptr;
	}
	// Forward: a new window, else this one when it took the first file or
	// there was none (a circuit already open in another window came forward
	// in openCircuit, and stays there).
	CircuitWindow* show = circuitWindows().size() > windowsBefore ? circuitWindows().back()
	                    : (files.empty() || (intoThis && !w->isPristine())) ? w : nullptr;
	if (show == nullptr) return;
	if (IsIconic(show->window())) ShowWindow(show->window(), SW_RESTORE);
	SetForegroundWindow(GetLastActivePopup(show->window()));
}

// ---- Starting up -----------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	// Every window sharp at its own monitor's scale (the manifest says so
	// too; this covers a copy run without it).
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	OleInitialize(nullptr);
	INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS |
	                                             ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);

	// Our own options, and the files to open.
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::vector<std::string> files;
	for (int i = 1; i < argc; i++) {
		const std::string a = U(argv[i]);
		if (a == "--feedback-probe") {
			const int status = feedback::probe();
			writeOut(strf("feedback server: %s (%d)\n", status == 403 ? "reachable, key checked" : status ? "answered" : "unreachable", status));
			return status == 403 ? 0 : 1;
		}
		if (a == "--version") { writeOut("CedarLogic " CL_VERSION " (native Windows)\n"); return 0; }
		if (a == "--click-test") { gClickTest = true; continue; }
		if (a == "--screenshot" && i + 1 < argc) { gScreenshot = U(argv[++i]); continue; }
		if (a == "--dark" || a == "--light") { gTheme = a == "--dark"; continue; }
		if (a == "--sim-view") { gSimView = true; continue; }
		if (a == "--formula" && i + 1 < argc) { gFormula = U(argv[++i]); continue; }
		if (a == "--truth-tab" && i + 1 < argc) { gTruthTab = atoi(U(argv[++i]).c_str()); continue; }
		if (a == "--timing" && i + 1 < argc) { gTiming = U(argv[++i]); continue; }
		if (a == "--timing-color") { gTimingColor = true; continue; }
		if (a == "--splash-frame" && i + 2 < argc) { gSplashAt = atof(U(argv[++i]).c_str()); gSplashFile = U(argv[++i]); continue; }
		if (a == "--first-launch") { gFirstLaunch = true; continue; }
		if (a == "--card-frame" && i + 1 < argc) { CircuitWindow::cardFreeze = atof(U(argv[++i]).c_str()); continue; }
		if (a == "--place" && i + 1 < argc) { gPlace = U(argv[++i]); continue; }
		if (a == "--select" && i + 1 < argc) { gSelect = U(argv[++i]); continue; }
		if (a == "--help-page" && i + 1 < argc) { gHelpPage = U(argv[++i]); continue; }
		if (a == "--page" && i + 1 < argc) { gPage = atoi(U(argv[++i]).c_str()); continue; }
		if (a == "--toolbar-style" && i + 1 < argc) {
			const std::string v = U(argv[++i]);
			gToolbarStyle = v == "classic" ? TSClassic : v == "minimal" ? TSMinimal : TSSeamless;
			continue;
		}
		if (a == "--dialog" && i + 1 < argc) {
			const std::string d = U(argv[++i]);
			gDialog = d == "preferences" ? CMD_PREFERENCES : d == "shortcuts" ? CMD_SHORTCUTS
			        : d == "truth-table" ? CMD_TRUTH_TABLE : d == "add-gate" ? CMD_ADD_GATE
			        : d == "library" ? CMD_OPEN : d == "versions" ? CMD_VERSIONS : d == "templates" ? CMD_NEW_TEMPLATE
			        : d == "formula" ? CMD_BUILD_FORMULA : d == "scope" ? CMD_SCOPE
			        : d == "export" ? CMD_EXPORT_IMAGE
			        : d == "feedback" ? CMD_FEEDBACK : d == "help" ? -3 : d == "quit" ? CMD_QUIT : d == "gate-settings" ? CMD_GATE_SETTINGS
			        : d == "welcome" ? -1 : d == "whatsnew" ? -2 : d == "tour" ? -4 : 0;
			continue;
		}
		files.push_back(a);
	}
	LocalFree(argv);
	// Test runs (CI's pictures, the click test) are CedarLogics of their own.
	const bool testRun = !gScreenshot.empty() || !gSplashFile.empty() || gClickTest || gDialog != 0;
	if (!testRun && handToRunning(files)) return 0;

	prefs().load();
	gPrefsBefore = prefs();
	if (gTheme >= 0) prefs().dark = gTheme == 1;
	if (gToolbarStyle >= 0) prefs().toolbarStyle = gToolbarStyle;
	// The click test wants every tool on the bar, and New in a window of its own.
	if (gClickTest) { prefs().toolbarHidden = 0; prefs().openReplaces = false; prefs().newTemplate.clear(); }
	if (!gFormula.empty()) prefs().lastFormula = gFormula;
	if (gTruthTab >= 0) prefs().truthTab = gTruthTab;
	if (!gTiming.empty()) prefs().timingInColor = gTimingColor;
	applyTheme();
	// Not for --screenshot: CI wants one deterministic frame.
	if (gScreenshot.empty() && gSplashFile.empty() && !gClickTest) splash::show();
	splash::setStatus("Loading the gate library\u2026");
	const std::string lib = resourcesDir().empty() ? std::string() : resourcesDir() + "\\cl_gatedefs.xml";
	if (lib.empty() || !cl_library_load(lib.c_str())) {
		splash::hideSoon(nullptr);
		showMessage(nullptr, Tone::Error, "CedarLogic can't find its gate library",
		            "cl_gatedefs.xml wasn't found in the res folder next to CedarLogic.exe. Reinstall CedarLogic, or "
		            "set CEDARLOGIC_RESOURCES to the folder that has it.");
		return 1;
	}
	if (!gSplashFile.empty()) {
		const bool ok = splash::renderFrame(gSplashAt, gFirstLaunch, gSplashFile);
		writeOut(strf("%s %s\n", ok ? "wrote" : "couldn't write", gSplashFile.c_str()));
		return ok ? 0 : 1;
	}
	prefs().applyWireDots();
	registerWindowClasses();
	splash::setStatus("Opening the workspace\u2026");

	bool any = false;
	for (const std::string& f : files) any = openCircuit(f, nullptr) || any;
	// Nothing asked for: the circuit you were last in, as the wx and Mac apps
	// do; else the most recent one; else a new circuit.
	if (!any && circuitWindows().empty() && gScreenshot.empty() && !gClickTest) {
		std::string last = library::lastCircuit();
		if (last.empty() || !fileExists(last)) {
			const std::vector<library::Item> all = library::items();
			last = all.empty() ? std::string() : all.front().circuit();
		}
		if (!last.empty()) openCircuit(last, nullptr);
	}
	if (circuitWindows().empty()) newCircuitWindow();
	// The launch screen says what it's opening.
	if (!circuitWindows().empty() && !circuitWindows().front()->filePath().empty())
		splash::setOpening(circuitWindows().front()->titleText());
	if (gSimView && !circuitWindows().empty()) circuitWindows().back()->toggleSimView();
	if (!gPlace.empty() && !circuitWindows().empty()) {
		CircuitWindow* w = circuitWindows().back();
		if (cl_edit_add_gate(w->document(), w->currentPage(), gPlace.c_str(), 0, 0)) w->redraw();
	}
	if (!gSelect.empty() && !circuitWindows().empty()) {
		CircuitWindow* w = circuitWindows().back();
		CLFindResult found;
		if (cl_find(w->document(), gSelect.c_str(), &found, 1) > 0) {
			w->showPage(found.page);
			cl_edit_select_gate(w->document(), found.page, found.gate);
		}
	}
	if (gDialog == CMD_PREFERENCES) setPreferencesPage(gPage);
	if (gDialog > 0 && !circuitWindows().empty()) PostMessageW(circuitWindows().back()->window(), WM_COMMAND, gDialog, 0);
	if (gDialog == -1 && !circuitWindows().empty()) {
		prefs().hasSeenWelcome = false;
		welcome::offer(circuitWindows().back());
		welcome::pageForScreenshot(gPage);
	}
	if (gDialog == -4 && !circuitWindows().empty()) welcome::startTour(circuitWindows().back());
	if (gDialog == -2 && !circuitWindows().empty()) whatsnew::show(circuitWindows().back(), gPage);
	if (gDialog == -3 && !circuitWindows().empty()) help::show(circuitWindows().back(), gHelpPage);
	if (!gScreenshot.empty()) SetTimer(nullptr, 0, 2000, screenshotTimer);
	else if (gClickTest) {
		SetTimer(nullptr, 0, 1500, clickTestStart);
		SetTimer(nullptr, 0, 60000, clickTestWatchdog);
	} else {
		// Once the launch screen goes: the windows, then the welcome the
		// first time, or work a CedarLogic that stopped unexpectedly left.
		splash::hideSoon([] {
			for (CircuitWindow* w : circuitWindows()) w->present();
			CircuitWindow* front = circuitWindows().empty() ? nullptr : circuitWindows().front();
			if (!welcome::offer(front) && !whatsnew::offer(front))
				SetTimer(nullptr, 0, 300, recoveryTimer);
			updater::start();
		});
	}

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
			if (CircuitWindow* w = windowFor(msg.hwnd)) {
				if (IsWindowEnabled(w->window()) && handleShortcut(w, msg)) continue;
			}
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	const int clickResult = gClickTest ? finishClickTest() : 0;
	updater::shutdown();
	if (gToolbarStyle >= 0) prefs().toolbarStyle = gPrefsBefore.toolbarStyle;
	prefs().save();
	OleUninitialize();
	if (gClickTest) return clickResult;
	return gScreenshot.empty() ? (int)msg.wParam : gExitCode;
}
