// --session-check (see SessionCheck.h).

#include "SessionCheck.h"
#include "App.h"
#include "Canvas.h"
#include "Commands.h"
#include "Dialogs.h"
#include "FindBar.h"
#include "Library.h"
#include "TabStrip.h"
#include "Window.h"

#include <dbghelp.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>
#include <vector>

// ---- What the app notices going wrong ----------------------------------------------

namespace health {
long d2dFailures = 0;
long lastD2dError = 0;
long exceptions = 0;
std::string firstException;
int liveWindows = 0;
int liveCanvases = 0;
static std::string firstD2d;
void noteD2dFailure(long hr, const char* where) {
	if (d2dFailures++ == 0) firstD2d = strf("%s: 0x%08lx", where, (unsigned long)hr);
	lastD2dError = hr;
}
}  // namespace health

namespace sessioncheck {

namespace {

// ---- Output -------------------------------------------------------------------------

void say(const std::string& text) {
	static HANDLE out = [] {
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		if (h == nullptr || h == INVALID_HANDLE_VALUE) {
			if (AttachConsole(ATTACH_PARENT_PROCESS)) h = GetStdHandle(STD_OUTPUT_HANDLE);
		}
		return h;
	}();
	DWORD wrote = 0;
	if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), (DWORD)text.size(), &wrote, nullptr);
	OutputDebugStringA(text.c_str());
}

void sayf(const char* fmt, ...) {
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	say(buf);
}

// ---- Where the session is (for a crash or a hang) --------------------------------------

std::atomic<long> gAction{ -1 };            // the action running, -1 before the first
std::atomic<const char*> gActionName{ "starting" };
std::atomic<unsigned long> gProgress{ 0 };   // moves on as the main thread pumps
std::atomic<bool> gFinished{ false };
HANDLE gMainThread = nullptr;
std::string gHome, gDumpDir;
uint64_t gSeed = 1;
int gExit = 1;
bool gStarted = false;
int gActions = 400;
double gTimeCap = 600;

// ---- Stacks and minidumps (DbgHelp, loaded when needed) --------------------------------

struct Dbg {
	HMODULE dll = nullptr;
	decltype(&SymInitialize) init = nullptr;
	decltype(&SymSetOptions) options = nullptr;
	decltype(&SymFromAddr) fromAddr = nullptr;
	decltype(&SymGetLineFromAddr64) line = nullptr;
	decltype(&StackWalk64) walk = nullptr;
	decltype(&SymFunctionTableAccess64) table = nullptr;
	decltype(&SymGetModuleBase64) moduleBase = nullptr;
	decltype(&MiniDumpWriteDump) dump = nullptr;
	bool ready = false;
};

Dbg& dbg() {
	static Dbg d;
	if (d.dll) return d;
	d.dll = LoadLibraryW(L"dbghelp.dll");
	if (d.dll == nullptr) return d;
	d.init = (decltype(&SymInitialize))(void*)GetProcAddress(d.dll, "SymInitialize");
	d.options = (decltype(&SymSetOptions))(void*)GetProcAddress(d.dll, "SymSetOptions");
	d.fromAddr = (decltype(&SymFromAddr))(void*)GetProcAddress(d.dll, "SymFromAddr");
	d.line = (decltype(&SymGetLineFromAddr64))(void*)GetProcAddress(d.dll, "SymGetLineFromAddr64");
	d.walk = (decltype(&StackWalk64))(void*)GetProcAddress(d.dll, "StackWalk64");
	d.table = (decltype(&SymFunctionTableAccess64))(void*)GetProcAddress(d.dll, "SymFunctionTableAccess64");
	d.moduleBase = (decltype(&SymGetModuleBase64))(void*)GetProcAddress(d.dll, "SymGetModuleBase64");
	d.dump = (decltype(&MiniDumpWriteDump))(void*)GetProcAddress(d.dll, "MiniDumpWriteDump");
	if (d.init && d.options) {
		d.options(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		// The exe's folder: its .pdb is beside it in a CI build.
		wchar_t exe[MAX_PATH] = L"";
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::string dir = U(exe);
		const size_t slash = dir.find_last_of("\\/");
		if (slash != std::string::npos) dir.resize(slash);
		d.ready = d.init(GetCurrentProcess(), dir.c_str(), TRUE) != FALSE;
	}
	return d;
}

std::string where(DWORD64 address) {
	std::string s = strf("0x%llx", (unsigned long long)address);
	HMODULE m = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       (LPCWSTR)(uintptr_t)address, &m) && m) {
		wchar_t name[MAX_PATH] = L"";
		GetModuleFileNameW(m, name, MAX_PATH);
		std::string n = U(name);
		const size_t slash = n.find_last_of("\\/");
		if (slash != std::string::npos) n = n.substr(slash + 1);
		s = strf("%s+0x%llx", n.c_str(), (unsigned long long)(address - (DWORD64)(uintptr_t)m));
	}
	Dbg& d = dbg();
	if (d.ready && d.fromAddr) {
		alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512] = {};
		SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 511;
		DWORD64 off = 0;
		if (d.fromAddr(GetCurrentProcess(), address, &off, sym)) s += strf(" %s+0x%llx", sym->Name, (unsigned long long)off);
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof line;
		DWORD disp = 0;
		if (d.line && d.line(GetCurrentProcess(), address, &disp, &line) && line.FileName) {
			std::string f = line.FileName;
			const size_t at = f.find("CedarLogic");
			s += strf(" (%s:%lu)", f.c_str() + (at != std::string::npos ? at : 0), (unsigned long)line.LineNumber);
		}
	}
	return s;
}

void printStack(HANDLE thread, CONTEXT ctx) {
	Dbg& d = dbg();
	if (!d.walk || !d.table || !d.moduleBase) { say("  (no DbgHelp: no stack)\n"); return; }
	STACKFRAME64 f = {};
	DWORD machine;
#if defined(_M_ARM64) || defined(__aarch64__)
	machine = IMAGE_FILE_MACHINE_ARM64;
	f.AddrPC.Offset = ctx.Pc;
	f.AddrStack.Offset = ctx.Sp;
	f.AddrFrame.Offset = ctx.Fp;
#else
	machine = IMAGE_FILE_MACHINE_AMD64;
	f.AddrPC.Offset = ctx.Rip;
	f.AddrStack.Offset = ctx.Rsp;
	f.AddrFrame.Offset = ctx.Rbp;
#endif
	f.AddrPC.Mode = f.AddrStack.Mode = f.AddrFrame.Mode = AddrModeFlat;
	for (int i = 0; i < 48; i++) {
		if (!d.walk(machine, GetCurrentProcess(), thread, &f, &ctx, nullptr, d.table, d.moduleBase, nullptr)) break;
		if (f.AddrPC.Offset == 0) break;
		sayf("  #%-2d %s\n", i, where(f.AddrPC.Offset).c_str());
	}
}

std::string writeDump(EXCEPTION_POINTERS* e, DWORD threadId, const char* why) {
	Dbg& d = dbg();
	if (!d.dump) return "";
	const std::string path = gDumpDir + strf("\\cedarlogic-session-%llu-%s.dmp", (unsigned long long)gSeed, why);
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return "";
	MINIDUMP_EXCEPTION_INFORMATION info = { threadId, e, FALSE };
	const BOOL ok = d.dump(GetCurrentProcess(), GetCurrentProcessId(), f,
	                       (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), e ? &info : nullptr,
	                       nullptr, nullptr);
	CloseHandle(f);
	return ok ? path : "";
}

bool isFatal(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_IN_PAGE_ERROR:
	case 0xC0000374:   // heap corruption
	case 0xC0000409:   // a stack buffer overrun, or __fastfail
	case 0xC000041D:   // an exception in a callback, not handled
		return true;
	default:
		return false;
	}
}

std::atomic<bool> gCrashed{ false };

void crashed(EXCEPTION_POINTERS* e, const char* how) {
	if (gCrashed.exchange(true)) TerminateProcess(GetCurrentProcess(), 3);
	const DWORD code = e ? e->ExceptionRecord->ExceptionCode : 0;
	const DWORD64 at = e ? (DWORD64)(uintptr_t)e->ExceptionRecord->ExceptionAddress : 0;
	sayf("FAIL CRASH (%s): exception 0x%08lx at %s, during action %ld (%s)\n", how, (unsigned long)code, where(at).c_str(),
	     gAction.load(), gActionName.load());
	if (e && code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters >= 2)
		sayf("  (%s of address 0x%llx)\n", e->ExceptionRecord->ExceptionInformation[0] == 0 ? "a read" : e->ExceptionRecord->ExceptionInformation[0] == 1 ? "a write" : "running",
		     (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
	if (e) printStack(GetCurrentThread(), *e->ContextRecord);
	const std::string dump = writeDump(e, GetCurrentThreadId(), "crash");
	sayf("  minidump: %s\n", dump.empty() ? "(couldn't be written)" : dump.c_str());
	sayf("session-check: crashed (seed %llu); home kept at %s\n", (unsigned long long)gSeed, gHome.c_str());
	TerminateProcess(GetCurrentProcess(), 3);
}

bool inThisExe(const void* address) {
	HMODULE m = nullptr;
	return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &m) &&
	       m == GetModuleHandleW(nullptr);
}

LONG CALLBACK vectored(EXCEPTION_POINTERS* e) {
	// First chance: Windows swallows some exceptions in window procedures and
	// timer callbacks, which would hide a crash and carry on with whatever it
	// broke. One in the app's own code is a bug wherever it's caught; one in
	// Windows' code may be one it expects and handles itself, so that's noted
	// (with its stack) and left to the unhandled-exception filter.
	if (!gStarted || gFinished || !isFatal(e->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
	if (inThisExe(e->ExceptionRecord->ExceptionAddress)) crashed(e, "first chance");
	static std::atomic<int> noted{ 0 };
	if (noted++ < 3) {
		sayf("note  a first-chance exception 0x%08lx in Windows' code at %s, during action %ld (%s):\n",
		     (unsigned long)e->ExceptionRecord->ExceptionCode, where((DWORD64)(uintptr_t)e->ExceptionRecord->ExceptionAddress).c_str(),
		     gAction.load(), gActionName.load());
		printStack(GetCurrentThread(), *e->ContextRecord);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled(EXCEPTION_POINTERS* e) {
	crashed(e, "unhandled");
	return EXCEPTION_EXECUTE_HANDLER;
}

void stackHere(const char* what) {
	CONTEXT ctx = {};
	RtlCaptureContext(&ctx);
	sayf("FAIL CRASH (%s) during action %ld (%s)\n", what, gAction.load(), gActionName.load());
	printStack(GetCurrentThread(), ctx);
	const std::string dump = writeDump(nullptr, GetCurrentThreadId(), "abort");
	sayf("  minidump: %s\n", dump.empty() ? "(couldn't be written)" : dump.c_str());
	TerminateProcess(GetCurrentProcess(), 3);
}

void onTerminate() { stackHere("std::terminate: an exception nothing caught"); }
void onAbort(int) { stackHere("abort()"); }

// No progress (the main thread stuck: a loop, a wait, a window nobody can
// answer) for this long, or the whole run past its cap: what it was doing,
// and out.
DWORD WINAPI watchdog(void*) {
	unsigned long last = gProgress.load();
	double lastMoved = nowSeconds();
	const double began = nowSeconds();
	while (!gFinished) {
		Sleep(1000);
		const unsigned long now = gProgress.load();
		if (now != last) { last = now; lastMoved = nowSeconds(); }
		const bool hung = nowSeconds() - lastMoved > 45;
		const bool late = nowSeconds() - began > gTimeCap;
		if (!hung && !late) continue;
		if (gFinished) break;
		sayf("FAIL %s during action %ld (%s)\n", hung ? "HANG: the main thread made no progress for 45 s" : strf("the session ran past its %.0f s cap", gTimeCap).c_str(),
		     gAction.load(), gActionName.load());
		if (SuspendThread(gMainThread) != (DWORD)-1) {
			CONTEXT ctx = {};
			ctx.ContextFlags = CONTEXT_FULL;
			if (GetThreadContext(gMainThread, &ctx)) {
				say("  the main thread:\n");
				printStack(gMainThread, ctx);
			}
			const std::string dump = writeDump(nullptr, 0, hung ? "hang" : "late");
			sayf("  minidump: %s\n", dump.empty() ? "(couldn't be written)" : dump.c_str());
		}
		TerminateProcess(GetCurrentProcess(), hung ? 4 : 5);
	}
	return 0;
}

// ---- Measuring ----------------------------------------------------------------------

struct Use { DWORD gdi, user; double privateMB; };
Use useNow() {
	Use u;
	u.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	u.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
	u.privateMB = 0;
	typedef BOOL(WINAPI * Info)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
	static Info info = (Info)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
	PROCESS_MEMORY_COUNTERS_EX pm = {};
	pm.cb = sizeof pm;
	if (info && info(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pm, sizeof pm)) u.privateMB = pm.PrivateUsage / 1048576.0;
	return u;
}

struct Random {
	uint64_t state;
	uint64_t next() {
		state += 0x9E3779B97F4A7C15ULL;
		uint64_t z = state;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return z ^ (z >> 31);
	}
	int below(int n) { return (int)(next() % (uint64_t)std::max(n, 1)); }
	double unit() { return (double)(next() % 1000000) / 1000000.0; }
	bool chance(int percent) { return below(100) < percent; }
};

// A circuit's text less what running it changes (as the Mac's check and
// Version History compare them).
std::string shape(const std::string& text) {
	std::istringstream in(text);
	std::string line, out;
	while (std::getline(in, line)) {
		const size_t s = line.find_first_not_of(" \t");
		const std::string t = s == std::string::npos ? std::string() : line.substr(s);
		if (t.rfind("(lparam \"OUTPUT_NUM\"", 0) == 0 || t.rfind("(lparam \"CURRENT_VALUE\"", 0) == 0 || t.rfind("(generator ", 0) == 0)
			continue;
		out += line;
		out += '\n';
	}
	return out;
}

std::string firstDifference(const std::string& a, const std::string& b) {
	std::istringstream ia(a), ib(b);
	std::string la, lb;
	for (int n = 1;; n++) {
		const bool ga = (bool)std::getline(ia, la), gb = (bool)std::getline(ib, lb);
		if (!ga && !gb) return "(the same)";
		if (!ga || !gb || la != lb)
			return strf("line %d: \"%s\" vs \"%s\"", n, ga ? la.substr(0, 120).c_str() : "(end)", gb ? lb.substr(0, 120).c_str() : "(end)");
	}
}

std::string readFile(const std::string& path) {
	std::ifstream f(W(path).c_str(), std::ios::binary);
	std::ostringstream s;
	s << f.rdbuf();
	return s.str();
}

bool isCircuitWindow(CircuitWindow* w) {
	const std::vector<CircuitWindow*>& all = circuitWindows();
	return std::find(all.begin(), all.end(), w) != all.end();
}

std::string className(HWND h) {
	wchar_t c[128] = L"";
	GetClassNameW(h, c, 128);
	return U(c);
}

// ---- Pumping the app's messages ---------------------------------------------------------

bool gSawQuit = false;
// Timers seen after the window closed (none should be the app's).
bool gWatchTimers = false;
std::vector<std::string> gTimersAfterClose;
void CALLBACK closerTick(HWND, UINT, UINT_PTR, DWORD);

void noteTimer(const MSG& m) {
	if (!gWatchTimers || m.message != WM_TIMER) return;
	if (m.hwnd == nullptr && m.lParam == (LPARAM)closerTick) return;
	const std::string what = m.hwnd ? strf("window class %s, timer %llu", className(m.hwnd).c_str(), (unsigned long long)m.wParam)
	                                : strf("a thread timer calling %s", where((DWORD64)m.lParam).c_str());
	if (std::find(gTimersAfterClose.begin(), gTimersAfterClose.end(), what) == gTimersAfterClose.end()) gTimersAfterClose.push_back(what);
}

void pump(double seconds) {
	const double end = nowSeconds() + seconds;
	for (;;) {
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			if (m.message == WM_QUIT) { gSawQuit = true; continue; }
			noteTimer(m);
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
		gProgress++;
		if (nowSeconds() >= end) break;
		MsgWaitForMultipleObjects(0, nullptr, FALSE, 4, QS_ALLINPUT);
	}
}

// ---- Answering the app's questions and dialogs ------------------------------------------
// Every dialog, card and menu the session opens is answered by this timer
// (from inside their own message loops): closed, or with `accept` its OK.
// One nobody asked for (an error card) is a failure.

struct Closer {
	bool expected = false;   // the action opens something
	bool accept = false;     // its OK rather than Cancel
	bool pokeOwner = false;  // first a Close sent to the window under it (the taskbar's)
	bool poked = false;
	HWND seen = nullptr;
	double seenAt = 0, askedAt = 0;
	int answered = 0, menus = 0, stuck = 0, unexpected = 0;
	std::vector<std::string> unexpectedTitles;
	std::vector<std::string> kinds;
} gCloser;

void expect(bool accept, bool pokeOwner = false) {
	gCloser.expected = true;
	gCloser.accept = accept;
	gCloser.pokeOwner = pokeOwner;
	gCloser.poked = false;
}

void unexpect() { gCloser.expected = false; gCloser.pokeOwner = false; }

HWND popupNow(HWND& owner) {
	for (CircuitWindow* w : circuitWindows()) {
		const HWND p = GetLastActivePopup(w->window());
		if (p && p != w->window() && IsWindowVisible(p)) {
			const std::string c = className(p);
			if (c == "CedarLogicDropHint" || c == "tooltips_class32") continue;
			owner = w->window();
			return p;
		}
	}
	return nullptr;
}

void CALLBACK closerTick(HWND, UINT, UINT_PTR, DWORD) {
	gProgress++;
	Closer& c = gCloser;
	GUITHREADINFO gi = { sizeof gi };
	if (GetGUIThreadInfo(GetCurrentThreadId(), &gi) && (gi.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE))) {
		c.menus++;
		EndMenu();
		return;
	}
	HWND owner = nullptr;
	const HWND p = popupNow(owner);
	const double now = nowSeconds();
	if (p == nullptr) { c.seen = nullptr; c.askedAt = 0; return; }
	if (p != c.seen) { c.seen = p; c.seenAt = now; c.askedAt = 0; return; }
	if (now - c.seenAt < 0.25) return;
	const std::string kind = className(p);
	const bool dialog = kind == "#32770";
	if (c.askedAt > 0) {
		if (now - c.askedAt < 5) return;
		// Asked to close five seconds ago and still there.
		c.stuck++;
		sayf("FAIL a %s window (“%s”) wouldn't close, at action %ld (%s)\n", kind.c_str(), windowText(p).c_str(), gAction.load(),
		     gActionName.load());
		if (dialog) EndDialog(p, IDCANCEL);
		else DestroyWindow(p);
		c.askedAt = 0;
		return;
	}
	if (c.pokeOwner && !c.poked) {
		// The taskbar's Close while a dialog is up: the window must stay.
		c.poked = true;
		PostMessageW(owner, WM_CLOSE, 0, 0);
		return;
	}
	if (!c.expected) {
		c.unexpected++;
		const std::string t = windowText(p);
		if (c.unexpectedTitles.size() < 8) c.unexpectedTitles.push_back(strf("“%s” (%s) at action %ld (%s)", t.c_str(), kind.c_str(), gAction.load(), gActionName.load()));
	}
	if (std::find(c.kinds.begin(), c.kinds.end(), kind) == c.kinds.end()) c.kinds.push_back(kind);
	if (c.accept && c.expected) {
		if (dialog) PostMessageW(p, WM_COMMAND, IDOK, 0);
		else PostMessageW(p, WM_KEYDOWN, VK_RETURN, 0);
	} else {
		PostMessageW(p, WM_CLOSE, 0, 0);
	}
	c.answered++;
	c.askedAt = now;
}

// ---- The session --------------------------------------------------------------------

struct PinSpot { double dx, dy; };
struct PartKind { const char* name; std::vector<PinSpot> ins, outs; };
const std::vector<PartKind>& partKinds() {
	static const std::vector<PartKind> kinds = {
		{ "AA_AND2", { { -3, 1 }, { -3, -1 } }, { { 3, 0 } } },
		{ "AE_OR2", { { -3, 1 }, { -3, -1 } }, { { 3, 0 } } },
		{ "AI_XOR2", { { -3, 1 }, { -3, -1 } }, { { 3, 0 } } },
		{ "AA_INVERTER", { { -3, 0 } }, { { 3, 0 } } },
		{ "AA_TOGGLE", {}, { { 2, 0 } } },
		{ "AA_TOGGLE", {}, { { 2, 0 } } },
		{ "GA_LED", { { -1, 0 } }, {} },
		{ "BB_CLOCK", {}, { { 4, 0 } } },
	};
	return kinds;
}

struct Placed { uint64_t key; long id; int kind; };
struct WireSpot { uint64_t key; double x, y; };

class Session {
public:
	explicit Session(uint64_t seed) { rng.state = seed; }
	void run(int actions);

private:
	Random rng;
	CircuitWindow* w = nullptr;
	std::vector<Placed> placed;
	std::vector<WireSpot> wires;
	int failures = 0;

	// What was seen along the way.
	int editsChecked = 0, editsLost = 0, firstLost = -1;
	int titleWrong = 0, firstTitleWrong = -1;
	int pauses = 0, pausesUnsaved = 0, pausesStale = 0, firstStale = -1;
	int saves = 0, savesDiffer = 0, reopenDiffer = 0;
	std::string saveDifference, reopenDifference;
	int historyBad = 0, firstHistoryBad = -1, maxSteps = 0;
	int pagesBad = 0, firstPagesBad = -1;
	std::string pagesWhat;
	int undos = 0, redos = 0, tabsMade = 0, tabsClosed = 0, tabsMoved = 0, splits = 0, simViews = 0, dialogs = 0, windowsMade = 0;
	int wiresDrawn = 0, partsPlaced = 0, drags = 0, escapes = 0, reloads = 0, strokes = 0;
	int windowClosedUnasked = 0, windowGone = -1;
	bool longHistoryDone = false;
	bool editedPastFull = true, fullHistoryReached = false, undoAllOk = true, redoAllOk = true;
	bool longRunDone = false;
	long scopeLengthAfter = 0;
	std::vector<Use> use;
	std::vector<int> useAt;

	void check(bool ok, const std::string& what) {
		say((ok ? "PASS " : "FAIL ") + what + "\n");
		if (!ok) failures++;
	}

	CLDocument* doc() const { return w->document(); }
	Canvas* canvas() const { return w->currentCanvas(); }
	int page() const { return w->currentPage(); }
	uint64_t pageKey() const { Canvas* c = canvas(); return c ? c->pageKey() : 0; }

	// ---- The canvas's own input ----
	POINT pixel(Canvas* c, double wx, double wy) const {
		double ox, oy;
		c->origin(ox, oy);
		const double s = dpiOf(c->widget()) / 96.0, u = c->unitsPerPoint();
		const double x = (wx - ox) / u * s, y = (oy - wy) / u * s;
		return { (LONG)std::lround(std::max(-30000.0, std::min(30000.0, x))), (LONG)std::lround(std::max(-30000.0, std::min(30000.0, y))) };
	}
	bool mouse(Canvas* c, HWND h, UINT msg, double wx, double wy, WPARAM keys = 0) {
		if (!IsWindow(h)) return false;
		const POINT p = pixel(c, wx, wy);
		SendMessageW(h, msg, keys, MAKELPARAM((WORD)(short)p.x, (WORD)(short)p.y));
		return IsWindow(h);
	}
	void key(HWND h, UINT vk) {
		if (!IsWindow(h)) return;
		SendMessageW(h, WM_KEYDOWN, vk, 1);
		if (IsWindow(h)) SendMessageW(h, WM_KEYUP, vk, (LPARAM)0xC0000001);
	}
	void canvasKey(UINT vk) { if (Canvas* c = canvas()) key(c->widget(), vk); }
	void typeInto(HWND h, const std::string& text) {
		for (char ch : text) if (IsWindow(h)) SendMessageW(h, WM_CHAR, (WPARAM)(unsigned char)ch, 1);
	}
	// A press, moves and a release, as Windows sends a drag (Escape partway, when asked).
	void drag(double x0, double y0, double x1, double y1, int steps, bool escapeMid = false) {
		Canvas* c = canvas();
		if (c == nullptr) return;
		const HWND h = c->widget();
		mouse(c, h, WM_MOUSEMOVE, x0, y0);
		if (!mouse(c, h, WM_LBUTTONDOWN, x0, y0, MK_LBUTTON)) return;
		for (int i = 1; i <= steps; i++) {
			const double t = (double)i / steps;
			if (!mouse(c, h, WM_MOUSEMOVE, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, MK_LBUTTON)) return;
			if (escapeMid && i == steps / 2) { key(h, VK_ESCAPE); escapes++; }
			if (rng.below(3) == 0) pump(0.004);
			if (!IsWindow(h)) return;
		}
		mouse(c, h, WM_LBUTTONUP, x1, y1);
	}
	void click(double x, double y) {
		Canvas* c = canvas();
		if (c == nullptr) return;
		const HWND h = c->widget();
		mouse(c, h, WM_MOUSEMOVE, x, y);
		if (mouse(c, h, WM_LBUTTONDOWN, x, y, MK_LBUTTON)) mouse(c, h, WM_LBUTTONUP, x, y);
	}
	void moveTo(double x, double y) {
		if (Canvas* c = canvas()) mouse(c, c->widget(), WM_MOUSEMOVE, x, y);
	}
	// Somewhere in the view.
	void viewSpot(double& x, double& y) {
		Canvas* c = canvas();
		if (c == nullptr) { x = y = 0; return; }
		const double cw = std::max(80.0, c->width()), ch = std::max(80.0, c->height());
		c->worldPoint(30 + rng.unit() * (cw - 60), 30 + rng.unit() * (ch - 60), x, y);
	}
	// A part this session placed on the page in front (still there), or -1.
	int partHere(bool needsOut = false, bool needsIn = false) {
		std::vector<int> ok;
		const uint64_t k = pageKey();
		for (int i = 0; i < (int)placed.size(); i++) {
			if (placed[i].key != k) continue;
			const PartKind& pk = partKinds()[placed[i].kind];
			if ((needsOut && pk.outs.empty()) || (needsIn && pk.ins.empty())) continue;
			double x, y;
			if (cl_gate_position(doc(), placed[i].id, &x, &y)) ok.push_back(i);
		}
		return ok.empty() ? -1 : ok[rng.below((int)ok.size())];
	}
	bool partSpot(int i, double& x, double& y) { return i >= 0 && cl_gate_position(doc(), placed[i].id, &x, &y); }
	// Near a part (or anywhere in the view).
	void spot(double& x, double& y) {
		const int i = rng.chance(65) ? partHere() : -1;
		if (partSpot(i, x, y)) { x += rng.unit() * 2 - 1; y += rng.unit() * 2 - 1; }
		else viewSpot(x, y);
	}
	int gatesHere() { return cl_document_gate_count(doc(), page()); }
	void selectSomething() {
		const int i = partHere();
		double x, y;
		if (partSpot(i, x, y)) click(x, y);
		else w->run(CMD_SELECT_ALL);
	}
	// Whatever is on the pointer goes down where it is, or (sometimes) back with Escape.
	void dropFloating() {
		if (!w->isFloating()) return;
		double x, y;
		viewSpot(x, y);
		moveTo(x, y);
		moveTo(x + 0.5, y - 0.5);
		if (rng.chance(12)) { canvasKey(VK_ESCAPE); escapes++; }
		else click(x + 0.5, y - 0.5);
	}

	// ---- The actions ----
	void placePart();
	void drawWire(bool toWire);
	void dragPart(bool escapeMid);
	void boxSelect();
	void tabStripClick(int pane, int k, bool closeButton, bool dragAlong, bool dragDown);
	void longHistory(int at);
	void longRun();
	void action(int a);

	// ---- The checks ----
	bool needsSaving() const;
	void checkEdited(int a, const std::string& before);
	void checkTitle(int a);
	void checkHistory(int a);
	void checkPages(int a);
	void settle();
	void pause(int a);
	void saveAndReopen(const char* why);
	void sample(int a) { use.push_back(useNow()); useAt.push_back(a); }
	std::string textNow() { return cl_document_save_text_ex(doc(), 0); }
};

bool Session::needsSaving() const {
	library::Item it;
	if (library::itemFor(w->filePath(), it)) return true;
	for (int p = 0; p < cl_document_page_count(doc()); p++) if (cl_document_gate_count(doc(), p) > 0) return true;
	return false;
}

// The palette's way in: a gate chosen there follows the pointer onto the
// canvas, and a click puts it down.
void Session::placePart() {
	if (gatesHere() > 30) {
		w->run(CMD_SELECT_ALL);
		canvasKey(VK_DELETE);
		return;
	}
	const int kind = rng.below((int)partKinds().size());
	double x, y;
	viewSpot(x, y);
	w->addGateOnNextMove(partKinds()[kind].name);
	moveTo(x, y);
	moveTo(x + 0.4, y + 0.3);
	if (!w->isFloating()) return;
	if (rng.chance(8)) { canvasKey(VK_ESCAPE); escapes++; return; }
	click(x + 0.4, y + 0.3);
	const long id = cl_edit_single_gate(doc(), page());
	if (id >= 0) {
		placed.push_back({ pageKey(), id, kind });
		partsPlaced++;
	}
	if (placed.size() > 400) placed.erase(placed.begin(), placed.begin() + 100);
}

// From a part's output pin to another's input pin, or onto a wire.
void Session::drawWire(bool toWire) {
	const int a = partHere(true, false);
	double ax, ay;
	if (!partSpot(a, ax, ay)) { placePart(); return; }
	const PartKind& ka = partKinds()[placed[a].kind];
	const PinSpot out = ka.outs[rng.below((int)ka.outs.size())];
	ax += out.dx;
	ay += out.dy;
	double bx, by;
	std::vector<int> spots;
	for (int i = 0; i < (int)wires.size(); i++) if (wires[i].key == pageKey()) spots.push_back(i);
	if (toWire && !spots.empty()) {
		const WireSpot& s = wires[spots[rng.below((int)spots.size())]];
		bx = s.x;
		by = s.y;
	} else {
		const int b = partHere(false, true);
		if (!partSpot(b, bx, by) || b == a) { viewSpot(bx, by); }
		else {
			const PartKind& kb = partKinds()[placed[b].kind];
			const PinSpot in = kb.ins[rng.below((int)kb.ins.size())];
			bx += in.dx;
			by += in.dy;
			wires.push_back({ pageKey(), bx - 0.6, by });
			if (wires.size() > 300) wires.erase(wires.begin(), wires.begin() + 100);
		}
	}
	drag(ax, ay, bx, by, 4 + rng.below(6));
	wiresDrawn++;
}

void Session::dragPart(bool escapeMid) {
	double x, y;
	spot(x, y);
	drag(x, y, x + rng.unit() * 12 - 6, y + rng.unit() * 12 - 6, 3 + rng.below(8), escapeMid);
	drags++;
}

void Session::boxSelect() {
	double x, y;
	viewSpot(x, y);
	drag(x, y, x + rng.unit() * 30 - 15, y + rng.unit() * 30 - 15, 4);
	if (rng.chance(50) && w->hasSelection()) dragPart(false);
}

// The tab strip, as the mouse uses it: a tab clicked, its X, or dragged along
// the strip (moving it) or down onto the canvas (splitting the view).
void Session::tabStripClick(int pane, int k, bool closeButton, bool dragAlong, bool dragDown) {
	TabStrip* s = w->tabStripWidget(pane);
	if (s == nullptr || !IsWindowVisible(s->widget())) return;
	const HWND h = s->widget();
	POINT p;
	if (!s->tabPoint(k, closeButton, p)) return;
	auto send = [&](UINT m, LONG x, LONG y, WPARAM keys) {
		if (IsWindow(h)) SendMessageW(h, m, keys, MAKELPARAM((WORD)(short)x, (WORD)(short)y));
	};
	send(WM_MOUSEMOVE, p.x, p.y, 0);
	send(WM_LBUTTONDOWN, p.x, p.y, MK_LBUTTON);
	if (dragAlong || dragDown) {
		const UINT dpi = dpiOf(h);
		const LONG dx = dragAlong ? scaled((rng.chance(50) ? 1 : -1) * (60 + rng.below(160)), dpi) : 0;
		const LONG dy = dragDown ? scaled(220, dpi) : 0;
		for (int i = 1; i <= 6; i++) send(WM_MOUSEMOVE, p.x + dx * i / 6, p.y + dy * i / 6, MK_LBUTTON);
		send(WM_LBUTTONUP, p.x + dx, p.y + dy, 0);
		tabsMoved++;
	} else {
		send(WM_LBUTTONUP, p.x, p.y, 0);
	}
}

void Session::checkEdited(int a, const std::string& before) {
	const std::string now = shape(textNow());
	if (now == before) return;
	editsChecked++;
	if (w->isDirty()) return;
	// Saved already (autosave came during the pause after it): fine, if what's
	// on disk is the circuit now.
	if (!w->filePath().empty() && shape(readFile(w->filePath())) == now) return;
	editsLost++;
	if (firstLost < 0) firstLost = a;
}

void Session::checkTitle(int a) {
	const std::string t = windowText(w->window());
	const bool star = !t.empty() && t[0] == '*';
	if (star != w->isDirty()) {
		titleWrong++;
		if (firstTitleWrong < 0) firstTitleWrong = a;
	}
}

void Session::checkHistory(int a) {
	const int n = cl_edit_undo_count(doc());
	maxSteps = std::max(maxSteps, n);
	if (n < 0 || n > 500 || (n > 0) != cl_edit_can_undo(doc())) {
		historyBad++;
		if (firstHistoryBad < 0) firstHistoryBad = a;
	}
}

void Session::checkPages(int a) {
	CLDocument* d = doc();
	const int n = cl_document_page_count(d);
	std::string bad;
	if (n < 1) bad = "no pages";
	else if (w->tabCount() != n) bad = strf("%d tabs for %d pages", w->tabCount(), n);
	else if (w->currentTab() < 0 || w->currentTab() >= n) bad = strf("the tab in front is %d of %d", w->currentTab(), n);
	else if (canvas() == nullptr || canvas()->page() != w->currentPage() || canvas()->page() < 0) bad = "the canvas in front shows no page";
	else {
		for (int i = 0; i < n && bad.empty(); i++)
			if (w->pageOfTab(i) != i) bad = strf("tab %d shows page %d", i, w->pageOfTab(i));
		for (int pane = 0; pane < (w->splitOpen() ? 2 : 1) && bad.empty(); pane++) {
			const int s = w->shownPage(pane);
			if (s < 0 || s >= n) bad = strf("side %d shows page %d of %d", pane, s, n);
			else if (w->panePages(pane).empty()) bad = strf("side %d has no tabs", pane);
		}
		if (bad.empty() && w->splitOpen() && w->focusedPane() != 0 && w->focusedPane() != 1) bad = "no side in front";
	}
	if (bad.empty()) return;
	pagesBad++;
	if (firstPagesBad < 0) { firstPagesBad = a; pagesWhat = bad; }
}

// What a student does before leaving it a moment: Simulation View, the lock
// and Draw off, nothing on the pointer, Tidy Up's preview kept.
void Session::settle() {
	if (w->isFloating()) dropFloating();
	if (w->isFloating()) w->cancelFloating();
	if (w->hasPendingGate()) w->clearPendingGate();
	if (w->simView()) w->run(CMD_SIM_VIEW);
	if (w->locked()) w->run(CMD_LOCK);
	if (w->drawing()) w->run(CMD_DRAW);
	if (w->tidyActive()) w->run(CMD_TIDY_KEEP);
}

// A pause: autosave comes two seconds after the last change.
void Session::pause(int a) {
	settle();
	pump(2.7);
	if (w->busyEditing()) pump(1.5);
	pauses++;
	if (!needsSaving()) return;   // an empty new circuit isn't kept until it has something on it
	if (w->isDirty()) pausesUnsaved++;
	const std::string disk = w->filePath().empty() ? std::string() : readFile(w->filePath());
	if (shape(disk) != shape(textNow())) {
		pausesStale++;
		if (firstStale < 0) { firstStale = a; saveDifference = firstDifference(shape(disk), shape(textNow())); }
	}
}

// Saved by hand (Ctrl+S): the file is the circuit, and opens to the same circuit again.
void Session::saveAndReopen(const char* why) {
	settle();
	expect(false);
	w->run(CMD_SAVE);
	unexpect();
	if (w->filePath().empty()) return;
	saves++;
	const std::string disk = readFile(w->filePath());
	const std::string now = textNow();
	if (disk != now) {
		savesDiffer++;
		if (saveDifference.empty()) saveDifference = strf("%s: %s", why, firstDifference(disk, now).c_str());
	}
	cl_set_settle_on_open(false);
	char err[256] = "";
	CLDocument* again = cl_document_open_text(disk.c_str(), (long)disk.size(), err, sizeof err);
	cl_set_settle_on_open(true);
	const std::string back = again ? std::string(cl_document_save_text_ex(again, 0)) : std::string("(it didn't open: ") + err + ")";
	if (again) cl_document_close(again);
	if (back != disk) {
		reopenDiffer++;
		if (reopenDifference.empty()) reopenDifference = strf("%s: %s", why, firstDifference(disk, back).c_str());
	}
}

// Past the 500-step history: more than 500 small edits in a row, then every
// step undone and redone.
void Session::longHistory(int at) {
	longHistoryDone = true;
	gActionName = "500+ edits in a row";
	settle();
	w->run(CMD_SELECT_ALL);
	if (!w->hasSelection()) { placePart(); w->run(CMD_SELECT_ALL); }
	for (int i = 0; i < 540; i++) {
		canvasKey(i % 2 ? VK_LEFT : VK_RIGHT);
		if (i % 25 == 0) pump(0.01);
	}
	pump(0.05);
	fullHistoryReached = cl_edit_undo_count(doc()) == 500;
	checkHistory(at);
	pause(at);
	// One more change with the history full: the window must be edited again.
	canvasKey(VK_UP);
	pump(0.03);
	editedPastFull = w->isDirty();
	checkTitle(at);
	gActionName = "undo everything";
	int n = 0;
	while (cl_edit_can_undo(doc()) && n < 700) { w->run(CMD_UNDO); n++; if (n % 50 == 0) pump(0.01); }
	undos += n;
	undoAllOk = n == 500 && !cl_edit_can_undo(doc()) && cl_edit_undo_count(doc()) == 0;
	checkPages(at);
	gActionName = "redo everything";
	n = 0;
	while (cl_edit_can_redo(doc()) && n < 700) { w->run(CMD_REDO); n++; if (n % 50 == 0) pump(0.01); }
	redos += n;
	redoAllOk = n == 500 && cl_edit_undo_count(doc()) == 500;
	checkPages(at);
	checkHistory(at);
	pause(at);
}

// The clock run a long time with the oscilloscope open: past its 60,000-step
// recording, which drops its oldest 10,000 (the Mac crashed reading past a
// trimmed recording), then its keys.
void Session::longRun() {
	longRunDone = true;
	gActionName = "a long run with the oscilloscope";
	settle();
	double x, y;
	viewSpot(x, y);
	w->addGateOnNextMove("BB_CLOCK");
	moveTo(x, y);
	click(x, y);
	if (!w->scopeShown()) w->run(CMD_SCOPE);
	pump(0.2);
	CLDocument* d = doc();
	for (int i = 0; i < 62000; i++) {
		cl_document_step(d);
		if (i % 2000 == 0) pump(0.001);
	}
	scopeLengthAfter = (long)cl_scope_length(d);
	if (ScopeWindow* s = w->scopeWindow()) {
		s->update();
		const HWND h = s->widget();
		for (UINT vk : { VK_HOME, VK_LEFT, VK_RIGHT, VK_RIGHT, VK_END, VK_LEFT, VK_OEM_MINUS, VK_OEM_PLUS, VK_UP, VK_DOWN }) key(h, vk);
	}
	pump(0.3);
	// Fewer steps kept again after the trim: the scope's cursor must still be in it.
	cl_scope_clear(d);
	if (ScopeWindow* s = w->scopeWindow()) { s->update(); key(s->widget(), VK_LEFT); key(s->widget(), VK_HOME); }
	pump(0.1);
	if (w->scopeShown()) w->run(CMD_SCOPE);
}

void Session::action(int a) {
	const int roll = rng.below(100);
	Canvas* c = canvas();
	const HWND ch = c ? c->widget() : nullptr;
	auto named = [&](const char* n) { gActionName = n; };
	switch (roll) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12:
		named("place a part from the side panel"); placePart(); break;
	case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20:
		named("draw a wire, pin to pin"); drawWire(false); break;
	case 21: case 22: case 23:
		named("draw a wire, pin to wire"); drawWire(true); break;
	case 24: case 25: case 26: case 27: case 28: case 29: case 30:
		named("drag a part"); dragPart(false); break;
	case 31: case 32: named("drag a selection box"); boxSelect(); break;
	case 33: case 34: named("Escape in the middle of a drag"); dragPart(true); break;
	case 35: named("rotate (R)"); selectSomething(); canvasKey('R'); break;
	case 36: case 37: named("delete (Del)"); selectSomething(); canvasKey(VK_DELETE); break;
	case 38: named("cut"); selectSomething(); w->run(CMD_CUT); break;
	case 39: named("copy and paste"); selectSomething(); w->run(CMD_COPY); w->run(CMD_PASTE); dropFloating(); break;
	case 40: named("paste"); w->run(CMD_PASTE); dropFloating(); break;
	case 41: named("duplicate"); selectSomething(); w->run(CMD_DUPLICATE); dropFloating(); break;
	case 42: case 43: {
		named("nudge (arrows)");
		selectSomething();
		const UINT keys[] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN };
		for (int i = 0, n = 1 + rng.below(4); i < n; i++) canvasKey(keys[rng.below(4)]);
		break;
	}
	case 44: case 45: case 46: case 47: case 48: {
		named("undo");
		for (int i = 0, n = 1 + rng.below(6); i < n; i++) if (cl_edit_can_undo(doc())) { w->run(CMD_UNDO); undos++; }
		break;
	}
	case 49: case 50: case 51: {
		named("redo");
		for (int i = 0, n = 1 + rng.below(4); i < n; i++) if (cl_edit_can_redo(doc())) { w->run(CMD_REDO); redos++; }
		break;
	}
	case 52: case 53: named("new tab"); if (w->tabCount() < 9) { w->run(CMD_NEW_TAB); tabsMade++; } break;
	case 54: named("next or previous tab"); w->run(rng.chance(50) ? CMD_NEXT_TAB : CMD_PREVIOUS_TAB); break;
	case 55: {
		named("click a tab");
		const int pane = w->splitOpen() && rng.chance(50) ? 1 : 0;
		TabStrip* s = w->tabStripWidget(pane);
		if (s) tabStripClick(pane, rng.below(std::max(1, s->tabsShown())), false, false, false);
		break;
	}
	case 56: case 57: {
		named("close a tab (its X)");
		if (cl_document_page_count(doc()) < 2) break;
		const int pane = w->splitOpen() && rng.chance(50) ? 1 : 0;
		TabStrip* s = w->tabStripWidget(pane);
		if (s == nullptr) break;
		const int before = cl_document_page_count(doc());
		expect(rng.chance(60));   // a tab with parts asks first
		tabStripClick(pane, rng.below(std::max(1, s->tabsShown())), true, false, false);
		pump(0.05);
		unexpect();
		if (cl_document_page_count(doc()) < before) tabsClosed++;
		break;
	}
	case 58: named("reopen a closed tab"); w->run(CMD_REOPEN_TAB); break;
	case 59: {
		named("drag a tab along its strip");
		const int pane = w->splitOpen() && rng.chance(50) ? 1 : 0;
		TabStrip* s = w->tabStripWidget(pane);
		if (s && s->tabsShown() > 1) tabStripClick(pane, rng.below(s->tabsShown()), false, true, false);
		break;
	}
	case 60: {
		named("drag a tab down onto the canvas");
		TabStrip* s = w->tabStripWidget(0);
		if (s && s->tabsShown() > 1) tabStripClick(0, rng.below(s->tabsShown()), false, false, true);
		break;
	}
	case 61: named("rename a tab"); {
		w->run(CMD_RENAME_TAB);
		pump(0.02);
		const HWND f = GetFocus();
		if (f && className(f) == "Edit") {
			typeInto(f, rng.chance(50) ? "Adder" : "x");
			key(f, rng.chance(80) ? VK_RETURN : VK_ESCAPE);
		}
		break;
	}
	case 62: case 63: named("split view"); w->run(CMD_SPLIT_VIEW); splits++; break;
	case 64: named("switch side"); w->run(CMD_SWITCH_PANE); break;
	case 65: {
		named("move a tab to the other side");
		if (w->splitOpen() && w->tabCount() > 1) w->movePageToPane(rng.below(w->tabCount()), rng.below(2));
		break;
	}
	case 66: case 67: case 68: named("Simulation View"); w->run(CMD_SIM_VIEW); simViews++; break;
	case 69: case 70: {
		named("click parts (switches)");
		for (int i = 0, n = 1 + rng.below(3); i < n; i++) {
			const int p = partHere();
			double x, y;
			if (partSpot(p, x, y)) click(x, y);
		}
		break;
	}
	case 71: named("run or pause"); if (rng.chance(30) && ch) key(ch, VK_SPACE); else w->run(CMD_RUNNING); break;
	case 72: named("step"); w->run(rng.chance(50) ? CMD_STEP : CMD_STEP_CLOCK); break;
	case 73: named("the oscilloscope"); w->run(CMD_SCOPE); if (w->scopeShown() && w->scopeWindow()) key(w->scopeWindow()->widget(), VK_LEFT); break;
	case 74: named("the truth table"); expect(false); dialogs++; w->run(rng.chance(70) ? CMD_TRUTH_TABLE : CMD_CHECK_CIRCUIT); unexpect(); break;
	case 75: {
		named("find");
		w->openFind(rng.chance(50) ? "AND" : "Toggle");
		pump(0.02);
		for (int i = 0, n = rng.below(4); i < n; i++) w->run(rng.chance(70) ? CMD_FIND_NEXT : CMD_FIND_PREVIOUS);
		const HWND f = GetFocus();
		if (rng.chance(60) && f && className(f) == "Edit") key(f, VK_ESCAPE);
		else if (FindBar* b = w->findBarWidget()) b->close();
		break;
	}
	case 76: {
		named("gate settings");
		const int p = partHere();
		double x, y;
		if (!partSpot(p, x, y)) break;
		click(x, y);
		expect(rng.chance(40));
		dialogs++;
		w->run(CMD_GATE_SETTINGS);
		unexpect();
		break;
	}
	case 77: named("Settings"); expect(rng.chance(30)); dialogs++; w->run(CMD_PREFERENCES); unexpect(); break;
	case 78: named("Add a Gate"); expect(false); dialogs++; w->run(CMD_ADD_GATE); unexpect(); dropFloating(); break;
	case 79: {
		named("notes");
		w->run(CMD_NOTES);
		pump(0.02);
		if (w->notesShown()) {
			const HWND f = GetFocus();
			if (f && className(f) == "Edit") typeInto(f, rng.chance(50) ? "Q goes high on the clock. " : "check B ");
		}
		break;
	}
	case 80: case 81: {
		named("draw on the circuit");
		if (!w->drawing()) w->run(CMD_DRAW);
		if (!w->drawing()) break;
		w->run(rng.chance(25) ? CMD_INK_ERASER : rng.chance(30) ? CMD_INK_HIGHLIGHTER : CMD_INK_PEN);
		double x, y;
		viewSpot(x, y);
		drag(x, y, x + rng.unit() * 20 - 10, y + rng.unit() * 20 - 10, 8 + rng.below(10), rng.chance(10));
		strokes++;
		if (rng.chance(70)) { if (rng.chance(50)) w->run(CMD_DRAW); else canvasKey(VK_ESCAPE); }
		break;
	}
	case 82: named("straighten"); if (rng.chance(50)) w->run(CMD_SELECT_ALL); else selectSomething(); canvasKey('S'); break;
	case 83: case 84: {
		named("Tidy Up and its preview");
		w->run(rng.chance(70) ? CMD_TIDY : CMD_TIDY_FLOW);
		pump(0.03);
		if (!w->tidyActive()) break;
		const int r = rng.below(5);
		if (r == 0) canvasKey(VK_RETURN);
		else if (r == 1) canvasKey(VK_ESCAPE);
		else if (r == 2) { canvasKey(VK_TAB); pump(0.02); w->run(CMD_TIDY_KEEP); }
		else if (r == 3) w->run(CMD_TIDY_REVERT);
		// else: left on show for the next action
		break;
	}
	case 85: named("lock the circuit"); w->run(CMD_LOCK); break;
	case 86: named("lock parts in place"); selectSomething(); w->run(rng.chance(70) ? CMD_LOCK_IN_PLACE : CMD_UNLOCK_ALL_PARTS); break;
	case 87: {
		named("zoom");
		const int r = rng.below(4);
		if (r == 0) w->run(CMD_ZOOM_FIT);
		else if (r == 1) w->run(CMD_ZOOM_IN);
		else if (r == 2) w->run(CMD_ZOOM_OUT);
		else if (ch) {
			RECT rc;
			GetWindowRect(ch, &rc);
			SendMessageW(ch, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(rng.chance(50) ? 120 : -120)), MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2));
		}
		break;
	}
	case 88: named("focus mode"); w->run(CMD_FOCUS_MODE); break;
	case 89: named("select all, or none"); if (rng.chance(50)) w->run(CMD_SELECT_ALL); else canvasKey(VK_ESCAPE); break;
	case 90: named("save a version (Ctrl+S) and reopen it"); saveAndReopen("Save a Version"); break;
	case 91: {
		named("a new window, then closed");
		const size_t before = circuitWindows().size();
		w->run(CMD_NEW);
		if (circuitWindows().size() <= before) break;
		windowsMade++;
		CircuitWindow* other = circuitWindows().back();
		if (rng.chance(60)) {
			Canvas* oc = other->currentCanvas();
			double x, y;
			if (oc) {
				oc->center(x, y);
				cl_edit_add_gate(other->document(), other->currentPage(), "AA_AND2", x, y);
				other->edited();
			}
		}
		pump(0.15);
		PostMessageW(other->window(), WM_CLOSE, 0, 0);
		pump(0.15);
		if (isCircuitWindow(other)) { other->destroy(); pump(0.05); }
		SetForegroundWindow(w->window());
		break;
	}
	case 92: {
		named("reopen from disk (as Version History's restore does)");
		settle();
		if (w->filePath().empty() || !needsSaving()) break;
		w->saveQuietly(false);
		w->reloadFromDisk("");
		reloads++;
		break;
	}
	case 93: named("dark mode"); w->run(CMD_DARK); break;
	case 94: named("the window's Close while Settings is up"); expect(false, true); dialogs++; w->run(CMD_PREFERENCES); unexpect(); break;
	case 95: named("right-click menu"); {
		double x, y;
		spot(x, y);
		if (c) { mouse(c, ch, WM_RBUTTONDOWN, x, y, MK_RBUTTON); mouse(c, ch, WM_RBUTTONUP, x, y); }
		break;
	}
	case 96: named("connect nearby pins"); selectSomething(); w->run(CMD_CONNECT_NEARBY); break;
	case 97: named("drop what's on the pointer"); dropFloating(); break;
	default: named("click on nothing"); { double x, y; viewSpot(x, y); click(x, y); } break;
	}
}

void Session::run(int actions) {
	sayf("session-check: %d actions, seed %llu, home %s\n", actions, (unsigned long long)gSeed, gHome.c_str());
	SetTimer(nullptr, 0, 100, closerTick);
	const int docs0 = cl_document_live_count();

	// A window opened and closed first, so what the first one makes once (fonts,
	// brushes, the gate pictures) isn't counted against the session's.
	{
		gActionName = "a first window, opened and closed";
		CircuitWindow* first = newCircuitWindow();
		pump(0.6);
		PostMessageW(first->window(), WM_CLOSE, 0, 0);
		pump(0.4);
		if (isCircuitWindow(first)) first->destroy();
		pump(0.2);
	}
	const Use base = useNow();
	const int docsBase = cl_document_live_count();
	check(docsBase == docs0 && health::liveWindows == 0 && health::liveCanvases == 0,
	      strf("the first window, closed, freed its document, canvases and itself (%d documents, %d windows, %d canvases)", docsBase - docs0,
	           health::liveWindows, health::liveCanvases));

	gActionName = "opening the window";
	w = newCircuitWindow();
	const HWND hwnd = w->window();
	// Clear of the pointer, so a real mouse can't step in mid-drag.
	SetWindowPos(hwnd, nullptr, 0, 0, scaled(1000, dpiOf(hwnd)), scaled(700, dpiOf(hwnd)), SWP_NOZORDER);
	SetCursorPos(GetSystemMetrics(SM_CXVIRTUALSCREEN) - 2, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 2);
	SetForegroundWindow(hwnd);
	pump(1.0);
	sample(0);

	for (int a = 0; a < actions; a++) {
		gAction = a;
		if (!isCircuitWindow(w)) {
			windowGone = a;
			break;
		}
		const std::string before = shape(textNow());
		const long exceptionsBefore = health::exceptions, d2dBefore = health::d2dFailures;
		action(a);
		pump(0.012 + 0.03 * rng.unit());
		gProgress++;
		if (!isCircuitWindow(w)) { windowGone = a; break; }
		if (health::exceptions > exceptionsBefore && health::exceptions - exceptionsBefore == health::exceptions)
			sayf("note  the first exception into the app's guard came in action %d (%s)\n", a, gActionName.load());
		if (health::d2dFailures > d2dBefore && d2dBefore == 0)
			sayf("note  the first Direct2D failure came in action %d (%s)\n", a, gActionName.load());
		checkEdited(a, before);
		checkTitle(a);
		checkHistory(a);
		checkPages(a);
		if (a % 40 == 39) pause(a);
		if (a == actions / 2 && !longHistoryDone) longHistory(a);
		if (a == actions * 3 / 5 && !longRunDone) longRun();
		if (a == actions / 4 || a == actions / 2 || a == actions * 3 / 4) sample(a);
		if (a == actions - 1) { settle(); pause(a); saveAndReopen("the last save"); }
	}
	gActionName = "closing the window";
	if (windowGone < 0) sample(actions);

	sayf("session: %d actions, %d parts placed, %d wires drawn, %d drags, %d Escapes, %d undos, %d redos, %d undo steps at most, "
	     "%d tabs made, %d closed, %d moved, %d splits, %d Simulation Views, %d dialogs, %d windows made, %d reloads, %d strokes, %d pages now\n",
	     actions, partsPlaced, wiresDrawn, drags, escapes, undos, redos, maxSteps, tabsMade, tabsClosed, tabsMoved, splits, simViews, dialogs,
	     windowsMade, reloads, strokes, windowGone < 0 ? cl_document_page_count(doc()) : -1);
	sayf("answered %d dialogs and cards (%d menus closed); kinds:", gCloser.answered, gCloser.menus);
	for (const std::string& k : gCloser.kinds) sayf(" %s", k.c_str());
	say("\n");

	// Closing it, as its Close button does.
	if (windowGone < 0) {
		settle();
		pump(0.1);
		gWatchTimers = false;
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
		pump(0.5);
		for (CircuitWindow* o : std::vector<CircuitWindow*>(circuitWindows())) o->destroy();
		gWatchTimers = true;
		pump(1.5);
		gWatchTimers = false;
	}
	const Use after = useNow();
	const int docsAfter = cl_document_live_count();

	// Top-level windows of this thread still there (an IME's own are Windows').
	std::vector<std::string> left;
	EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM lp) -> BOOL {
		const std::string c = className(h);
		if (c == "IME" || c == "MSCTFIME UI" || c == "tooltips_class32" || c == "CicMarshalWndClass" || c == "OleMainThreadWndClass" ||
		    c.rfind("OleDde", 0) == 0 || c == "CedarLogicSyncWindow" || c == "UserAdapterWindowClass")
			return TRUE;
		reinterpret_cast<std::vector<std::string>*>(lp)->push_back(c + (IsWindowVisible(h) ? " (shown)" : " (hidden)"));
		return TRUE;
	}, (LPARAM)&left);

	// ---- The results ----
	check(windowGone < 0, windowGone < 0 ? std::string("the window stayed open through the session")
	                                     : strf("the window stayed open through the session (it went at action %d)", windowGone));
	check(health::exceptions == 0, health::exceptions == 0 ? std::string("nothing was thrown into the app's guard")
	                                                       : strf("nothing was thrown into the app's guard (%ld times; the first: %s)", health::exceptions, health::firstException.c_str()));
	check(health::d2dFailures == 0, health::d2dFailures == 0 ? std::string("no Direct2D frame or target failed")
	                                                         : strf("no Direct2D frame or target failed (%ld; the first: %s)", health::d2dFailures, health::firstD2d.c_str()));
	check(gCloser.unexpected == 0, gCloser.unexpected == 0 ? std::string("no card or dialog came up that the session didn't ask for")
	                                                       : strf("no card or dialog came up that the session didn't ask for (%d)", gCloser.unexpected));
	for (const std::string& t : gCloser.unexpectedTitles) sayf("  %s\n", t.c_str());
	check(gCloser.stuck == 0, strf("every dialog and card closed when asked (%d didn't)", gCloser.stuck));
	check(editsChecked > 0 && editsLost == 0,
	      strf("every change left the window edited, so it's saved (%d of %d%s)", editsChecked - editsLost, editsChecked,
	           firstLost >= 0 ? strf(", first lost at action %d", firstLost).c_str() : ""));
	check(titleWrong == 0, strf("the title's * matched the window's edited state after every action%s",
	                            titleWrong ? strf(" (%d times not, first at action %d)", titleWrong, firstTitleWrong).c_str() : ""));
	check(pauses > 0 && pausesUnsaved == 0, strf("autosave saved the circuit at every pause (%d of %d)", pauses - pausesUnsaved, pauses));
	check(pausesStale == 0, strf("the file on disk was the circuit at every pause (%d of %d%s)", pauses - pausesStale, pauses,
	                             firstStale >= 0 ? strf(", first stale at action %d: %s", firstStale, saveDifference.c_str()).c_str() : ""));
	check(saves > 0 && savesDiffer == 0, strf("a save wrote the circuit as it is (%d saves%s)", saves,
	                                          savesDiffer ? strf(", %d differed: %s", savesDiffer, saveDifference.c_str()).c_str() : ""));
	check(saves > 0 && reopenDiffer == 0, strf("the saved file reopens to the same circuit (%d saves%s)", saves,
	                                           reopenDiffer ? strf(", %d differed: %s", reopenDiffer, reopenDifference.c_str()).c_str() : ""));
	check(historyBad == 0, strf("the undo history's counts stayed in range (0 to 500, Undo on when there are steps)%s",
	                            historyBad ? strf(" (%d times not, first at action %d)", historyBad, firstHistoryBad).c_str() : ""));
	check(pagesBad == 0, strf("every tab and side showed a page that exists%s",
	                          pagesBad ? strf(" (%d times not; first at action %d: %s)", pagesBad, firstPagesBad, pagesWhat.c_str()).c_str() : ""));
	if (longHistoryDone) {
		check(fullHistoryReached, "540 edits in a row filled the 500-step history");
		check(editedPastFull, "an edit with the history full still left the window edited");
		check(undoAllOk, "Undo went back all 500 steps, and no further");
		check(redoAllOk, "Redo came forward all 500 steps again");
	}
	if (longRunDone) check(scopeLengthAfter > 0 && scopeLengthAfter <= 60000, strf("the oscilloscope kept its recording within 60,000 steps (%ld after 62,000)", scopeLengthAfter));
	if (use.size() >= 2) {
		say("use:");
		for (size_t i = 0; i < use.size(); i++) sayf("  action %d: %lu GDI, %lu USER, %.0f MB", useAt[i], (unsigned long)use[i].gdi, (unsigned long)use[i].user, use[i].privateMB);
		say("\n");
		// From a quarter in (everything has been made once) to the end.
		const Use& q = use.size() >= 3 ? use[1] : use[0];
		const Use& e = use.back();
		check((long)e.gdi - (long)q.gdi < 80, strf("GDI objects levelled off (%+ld over the last three quarters)", (long)e.gdi - (long)q.gdi));
		check((long)e.user - (long)q.user < 80, strf("USER objects levelled off (%+ld over the last three quarters)", (long)e.user - (long)q.user));
		check(e.privateMB - q.privateMB < 150, strf("memory levelled off (%+.0f MB over the last three quarters)", e.privateMB - q.privateMB));
	}
	sayf("before the window: %lu GDI, %lu USER, %.0f MB; after closing it: %lu GDI, %lu USER, %.0f MB\n", (unsigned long)base.gdi,
	     (unsigned long)base.user, base.privateMB, (unsigned long)after.gdi, (unsigned long)after.user, after.privateMB);
	check((long)after.gdi - (long)base.gdi < 40, strf("closing the window gave back its GDI objects (%+ld)", (long)after.gdi - (long)base.gdi));
	check((long)after.user - (long)base.user < 40, strf("closing the window gave back its USER objects (%+ld)", (long)after.user - (long)base.user));
	check(health::liveWindows == 0 && health::liveCanvases == 0,
	      strf("closing the window freed it and its canvases (%d windows, %d canvases left)", health::liveWindows, health::liveCanvases));
	check(docsAfter == docs0, strf("closing the window freed every document (%d left)", docsAfter - docs0));
	check(gTimersAfterClose.empty(), strf("no timer ran after the window closed (%d)", (int)gTimersAfterClose.size()));
	for (const std::string& t : gTimersAfterClose) sayf("  %s\n", t.c_str());
	check(left.empty(), strf("no window of the app's was left after closing (%d)", (int)left.size()));
	for (const std::string& l : left) sayf("  %s\n", l.c_str());

	gExit = failures ? 1 : 0;
	sayf(failures ? "session-check: %d failed (seed %llu)\n" : "session-check: all passed (seed %llu)\n", failures, (unsigned long long)gSeed);
}

void removeTree(const std::string& dir) {
	std::wstring from = W(dir);
	from.push_back(L'\0');
	SHFILEOPSTRUCTW op = {};
	op.wFunc = FO_DELETE;
	op.pFrom = from.c_str();
	op.fFlags = FOF_NO_UI;
	SHFileOperationW(&op);
}

void CALLBACK startTimer(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	gStarted = true;
	guarded("the session check", [] {
		Session s(gSeed);
		s.run(gActions);
	});
	gFinished = true;
	if (gExit != 0) sayf("session-check: home kept at %s\n", gHome.c_str());
	PostQuitMessage(gExit);
}

}  // namespace

void prepare(uint64_t seed) {
	gSeed = seed;
	wchar_t tmp[MAX_PATH] = L"";
	GetTempPathW(MAX_PATH, tmp);
	gHome = U(tmp) + strf("cedarlogic-session-%lu-%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)seed);
	CreateDirectoryW(W(gHome).c_str(), nullptr);
	SetEnvironmentVariableW(L"CEDARLOGIC_HOME", W(gHome).c_str());
	wchar_t env[MAX_PATH] = L"";
	gDumpDir = GetEnvironmentVariableW(L"CL_SESSION_DUMPS", env, MAX_PATH) > 0 ? U(env) : U(tmp);
	if (!gDumpDir.empty() && (gDumpDir.back() == '\\' || gDumpDir.back() == '/')) gDumpDir.pop_back();
	CreateDirectoryW(W(gDumpDir).c_str(), nullptr);
	if (GetEnvironmentVariableW(L"CL_SESSION_TIME_CAP", env, MAX_PATH) > 0) gTimeCap = std::max(30.0, _wtof(env));
	// Timer callbacks' exceptions are swallowed by default (UOI_TIMERPROC_EXCEPTION_SUPPRESSION): not here.
	BOOL suppress = FALSE;
	SetUserObjectInformationW(GetCurrentProcess(), 7, &suppress, sizeof suppress);
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &gMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	AddVectoredExceptionHandler(1, vectored);
	SetUnhandledExceptionFilter(unhandled);
	std::set_terminate(onTerminate);
	signal(SIGABRT, onAbort);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	dbg();
}

void start(int actions, uint64_t seed) {
	gActions = std::max(1, actions);
	gSeed = seed;
	CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
	SetTimer(nullptr, 0, 300, startTimer);
}

int exitCode() {
	// Its home folder goes once it passed (the app's last settings save is done by now).
	if (gExit == 0 && !gHome.empty()) removeTree(gHome);
	return gExit;
}

}  // namespace sessioncheck
