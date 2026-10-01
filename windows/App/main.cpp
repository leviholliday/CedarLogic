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
#include "Updater.h"
#include "Welcome.h"
#include "Window.h"

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>

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
	if (gDialog) { prefs().save(); ExitProcess((UINT)gExitCode); }
	for (CircuitWindow* c : std::vector<CircuitWindow*>(circuitWindows())) c->destroy();
	PostQuitMessage(gExitCode);
}

// Once the first window is up, offer back work a CedarLogic that stopped
// unexpectedly left behind.
void CALLBACK recoveryTimer(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	CircuitWindow* w = circuitWindows().empty() ? nullptr : circuitWindows().front();
	guarded("recovering work", [&] { recovery::offer(w); });
}

bool down(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

// The app's keyboard shortcuts (the menus show them). Bare keys (R, S, T...)
// are the canvas's own, not here.
struct Shortcut { UINT vk; bool ctrl, shift; int command; };
const Shortcut kShortcuts[] = {
	{ 'N', true, false, CMD_NEW }, { 'O', true, false, CMD_OPEN }, { 'I', true, false, CMD_IMPORT }, { 'Q', true, false, CMD_QUIT },
	{ 'S', true, false, CMD_SAVE }, { 'S', true, true, CMD_SAVE_AS }, { 'E', true, false, CMD_EXPORT_IMAGE },
	{ 'P', true, false, CMD_PRINT }, { 'W', true, true, CMD_CLOSE_WINDOW },
	{ 'Z', true, false, CMD_UNDO }, { 'Z', true, true, CMD_REDO }, { 'Y', true, false, CMD_REDO },
	{ 'X', true, false, CMD_CUT }, { 'C', true, false, CMD_COPY }, { 'V', true, false, CMD_PASTE },
	{ 'D', true, false, CMD_DUPLICATE }, { 'A', true, false, CMD_SELECT_ALL }, { 'F', true, false, CMD_FIND },
	{ VK_OEM_PLUS, true, false, CMD_ZOOM_IN }, { VK_OEM_PLUS, true, true, CMD_ZOOM_IN }, { VK_ADD, true, false, CMD_ZOOM_IN },
	{ VK_OEM_MINUS, true, false, CMD_ZOOM_OUT }, { VK_SUBTRACT, true, false, CMD_ZOOM_OUT },
	{ '0', true, false, CMD_ZOOM_FIT }, { VK_NUMPAD0, true, false, CMD_ZOOM_FIT },
	{ '1', true, false, CMD_ZOOM_ACTUAL }, { VK_NUMPAD1, true, false, CMD_ZOOM_ACTUAL },
	{ 'D', true, true, CMD_DARK }, { VK_OEM_PERIOD, true, false, CMD_PALETTE }, { VK_OEM_COMMA, true, false, CMD_PREFERENCES },
	{ 'R', true, true, CMD_STEP }, { 'R', true, false, CMD_SIM_VIEW }, { 'G', true, false, CMD_SCOPE },
	{ 'T', true, false, CMD_NEW_TAB }, { 'W', true, false, CMD_CLOSE_TAB }, { 'T', true, true, CMD_REOPEN_TAB },
	{ VK_NEXT, true, false, CMD_NEXT_TAB }, { VK_PRIOR, true, false, CMD_PREVIOUS_TAB },
	{ VK_TAB, true, false, CMD_NEXT_TAB }, { VK_TAB, true, true, CMD_PREVIOUS_TAB },
	{ VK_F1, false, false, CMD_HELP }, { VK_OEM_2, true, false, CMD_SHORTCUTS },
};

// What a text box does itself with these (copy the text, not the gates).
bool isEditingKey(const Shortcut& s) {
	return s.ctrl && !s.shift && (s.vk == 'A' || s.vk == 'C' || s.vk == 'V' || s.vk == 'X' || s.vk == 'Z' || s.vk == 'Y');
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
	wchar_t cls[32] = L"";
	GetClassNameW(msg.hwnd, cls, 32);
	const bool inTextBox = lstrcmpiW(cls, L"Edit") == 0;
	for (const Shortcut& s : kShortcuts) {
		if (s.vk != msg.wParam || s.ctrl != ctrl || s.shift != shift) continue;
		if (inTextBox && isEditingKey(s)) return false;
		guarded("a shortcut", [&] { w->run(s.command); });
		return true;
	}
	return false;
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
	CircuitWindow* w;
	if (from && from->isPristine()) { from->replaceDocument(doc, target); w = from; }
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
		if (a == "--dialog" && i + 1 < argc) {
			const std::string d = U(argv[++i]);
			gDialog = d == "preferences" ? CMD_PREFERENCES : d == "shortcuts" ? CMD_SHORTCUTS
			        : d == "truth-table" ? CMD_TRUTH_TABLE : d == "add-gate" ? CMD_ADD_GATE
			        : d == "library" ? CMD_OPEN : d == "versions" ? CMD_VERSIONS : d == "templates" ? CMD_NEW_TEMPLATE
			        : d == "formula" ? CMD_BUILD_FORMULA : d == "scope" ? CMD_SCOPE
			        : d == "export" ? CMD_EXPORT_IMAGE
			        : d == "feedback" ? CMD_FEEDBACK : d == "help" ? -3 : d == "gate-settings" ? CMD_GATE_SETTINGS
			        : d == "welcome" ? -1 : d == "whatsnew" ? -2 : d == "tour" ? -4 : 0;
			continue;
		}
		files.push_back(a);
	}
	LocalFree(argv);

	prefs().load();
	if (gTheme >= 0) prefs().dark = gTheme == 1;
	if (!gFormula.empty()) prefs().lastFormula = gFormula;
	if (gTruthTab >= 0) prefs().truthTab = gTruthTab;
	if (!gTiming.empty()) prefs().timingInColor = gTimingColor;
	applyTheme();
	// Not for --screenshot: CI wants one deterministic frame.
	if (gScreenshot.empty() && gSplashFile.empty()) splash::show();
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
	if (!any && circuitWindows().empty() && gScreenshot.empty()) {
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
	else {
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
	prefs().save();
	OleUninitialize();
	return gScreenshot.empty() ? (int)msg.wParam : gExitCode;
}
