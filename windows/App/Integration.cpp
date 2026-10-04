// CedarLogic as a normal Windows app: in the Start menu, and opening .cdl
// files when they're double-clicked.
//
// The installer (windows/installer/CedarLogic.iss) puts CedarLogic in
// %LOCALAPPDATA%\Programs\CedarLogic for this user, with a Start menu entry,
// .cdl files opening in it and an uninstaller in Settings > Apps, and notes
// where it put it (HKCU\Software\CedarLogic\Native, InstallDir). A copy run
// from the zip does the first two for itself when asked -- once, the first
// time it runs, or from Help in the ••• menu -- for this user only, under the
// names the installer uses (an installer run later takes them over):
//
//   HKCU\Software\Classes\.cdl                  -> CedarLogic.Circuit
//   HKCU\Software\Classes\CedarLogic.Circuit    its name, icon and open command
//   <Start menu>\Programs\CedarLogic.lnk        a shortcut to this exe
//
// The answer is kept in native.ini (startMenu). A file double-clicked while
// CedarLogic runs goes to the running one (main.cpp's handToRunning).

#include "Integration.h"
#include "Brand.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Welcome.h"
#include "Window.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>

namespace integration {

namespace {

const wchar_t* kProgId = L"CedarLogic.Circuit";
const std::wstring kClasses = L"Software\\Classes\\";
const wchar_t* kInstallKey = L"Software\\CedarLogic\\Native";   // the installer's

std::wstring exePath() {
	wchar_t buf[MAX_PATH * 4];
	const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return std::wstring(buf, n);
}

std::wstring folderOf(const std::wstring& path) {
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool exists(const std::wstring& path) { return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// The same file or folder, however it's written (case, a trailing slash).
bool samePath(const std::wstring& a, const std::wstring& b) {
	if (a.empty() || b.empty()) return false;
	auto tidy = [](const std::wstring& p) {
		wchar_t full[MAX_PATH * 4];
		const DWORD n = GetFullPathNameW(p.c_str(), (DWORD)(sizeof full / sizeof full[0]), full, nullptr);
		std::wstring t = n > 0 && n < sizeof full / sizeof full[0] ? std::wstring(full, n) : p;
		while (t.size() > 3 && (t.back() == L'\\' || t.back() == L'/')) t.pop_back();
		return t;
	};
	const std::wstring x = tidy(a), y = tidy(b);
	return CompareStringOrdinal(x.c_str(), (int)x.size(), y.c_str(), (int)y.size(), TRUE) == CSTR_EQUAL;
}

// ---- This user's registry (HKEY_CURRENT_USER) -------------------------------------

std::wstring readString(const std::wstring& key, const wchar_t* name) {
	DWORD size = 0;
	if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
	    size < sizeof(wchar_t))
		return std::wstring();
	std::wstring s(size / sizeof(wchar_t), L'\0');
	if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, &s[0], &size) != ERROR_SUCCESS)
		return std::wstring();
	s.resize(wcslen(s.c_str()));
	return s;
}

bool writeString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
	HKEY k = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
		return false;
	const LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
	RegCloseKey(k);
	return r == ERROR_SUCCESS;
}

// A key with nothing left in it goes; one another app also uses stays.
void deleteIfEmpty(const std::wstring& key) {
	HKEY k = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
	DWORD subkeys = 1, values = 1;
	const bool empty = RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
	                                    nullptr, nullptr) == ERROR_SUCCESS && subkeys == 0 && values == 0;
	RegCloseKey(k);
	if (empty) RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}

std::wstring installDir() { return readString(kInstallKey, L"InstallDir"); }

// An installed CedarLogic, this one or another: that one has Start and .cdl
// files, and a copy from the zip leaves them to it.
bool installedAnywhere() {
	const std::wstring dir = installDir();
	return installed() || (!dir.empty() && exists(dir + L"\\CedarLogic.exe"));
}

// The exe .cdl files open with, from the open command ("" when there's none).
std::wstring registeredExe() {
	const std::wstring command = readString(kClasses + kProgId + L"\\shell\\open\\command", nullptr);
	if (command.size() < 2 || command[0] != L'"') return std::wstring();
	const size_t end = command.find(L'"', 1);
	return end == std::wstring::npos ? std::wstring() : command.substr(1, end - 1);
}

// Start and .cdl files lead to this copy.
bool added() { return samePath(registeredExe(), exePath()); }

// ---- The Start menu's shortcut ---------------------------------------------------------

std::wstring shortcutPath() {
	PWSTR p = nullptr;
	std::wstring path;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &p)) && p) path = std::wstring(p) + L"\\CedarLogic.lnk";
	if (p) CoTaskMemFree(p);
	return path;
}

// Where a shortcut leads ("" when it can't be read).
std::wstring shortcutTarget(const std::wstring& lnk) {
	std::wstring target;
	IShellLinkW* link = nullptr;
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return target;
	IPersistFile* file = nullptr;
	if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
		wchar_t buf[MAX_PATH * 4] = L"";
		if (SUCCEEDED(file->Load(lnk.c_str(), STGM_READ)) &&
		    SUCCEEDED(link->GetPath(buf, (int)(sizeof buf / sizeof buf[0]), nullptr, SLGP_RAWPATH)))
			target = buf;
		file->Release();
	}
	link->Release();
	return target;
}

bool makeShortcut(const std::wstring& lnk, const std::wstring& exe) {
	IShellLinkW* link = nullptr;
	if (lnk.empty() || FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
	link->SetPath(exe.c_str());
	link->SetWorkingDirectory(folderOf(exe).c_str());
	link->SetDescription(L"Build and simulate digital logic circuits");
	link->SetIconLocation(exe.c_str(), 0);
	bool ok = false;
	IPersistFile* file = nullptr;
	if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
		ok = SUCCEEDED(file->Save(lnk.c_str(), TRUE));
		file->Release();
	}
	link->Release();
	return ok;
}

// ---- Adding and removing -----------------------------------------------------------------

bool add() {
	const std::wstring exe = exePath(), prog = kClasses + kProgId;
	bool ok = writeString(kClasses + L".cdl", nullptr, kProgId);
	ok = writeString(kClasses + L".cdl\\OpenWithProgids", kProgId, L"") && ok;
	ok = writeString(prog, nullptr, L"CedarLogic Circuit") && ok;
	// The .cdl page icon (the exe's second), as the Mac's Finder shows them.
	ok = writeString(prog + L"\\DefaultIcon", nullptr, exe + L",1") && ok;
	ok = writeString(prog + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"") && ok;
	ok = makeShortcut(shortcutPath(), exe) && ok;
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	return ok;
}

void remove() {
	const std::wstring lnk = shortcutPath();
	if (exists(lnk)) {
		const std::wstring target = shortcutTarget(lnk);
		if (target.empty() || samePath(target, exePath()) || !exists(target)) DeleteFileW(lnk.c_str());
	}
	RegDeleteTreeW(HKEY_CURRENT_USER, (kClasses + kProgId).c_str());
	RegDeleteKeyValueW(HKEY_CURRENT_USER, (kClasses + L".cdl\\OpenWithProgids").c_str(), kProgId);
	deleteIfEmpty(kClasses + L".cdl\\OpenWithProgids");
	if (readString(kClasses + L".cdl", nullptr) == kProgId) RegDeleteKeyValueW(HKEY_CURRENT_USER, (kClasses + L".cdl").c_str(), nullptr);
	deleteIfEmpty(kClasses + L".cdl");
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool isOpen(CircuitWindow* w) {
	const std::vector<CircuitWindow*>& all = circuitWindows();
	return w && std::find(all.begin(), all.end(), w) != all.end();
}

// The question, in the app's own look (as Quit asks): the icon, what adding
// does, and that the folder should stay put. The first time, "No Thanks" is
// remembered too. True when it was added.
bool ask(CircuitWindow* w, bool firstTime) {
	Form f;
	f.title = "Add to Start";
	f.width = 460;
	f.okText = "Add to Start";
	f.cancelText = firstTime ? "No Thanks" : "Cancel";
	const std::string title = "Add CedarLogic to the Start menu?";
	const std::string body = "It'll be in Start with your other apps, and double-clicking a .cdl file will open it in "
	                         "CedarLogic. It still runs from this folder, so keep the folder where it is.";
	const std::string later = firstTime ? "You can change this later in the ••• menu, under Help." : "";
	const float textW = (float)f.width - 32 - 4;
	const D2D1_COLOR_F none = D2D1::ColorF(0, 0, 0);
	const float bodyH = brand::text(nullptr, body, 0, 0, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, none, textW);
	const float laterH = later.empty() ? 0 : brand::text(nullptr, later, 0, 0, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, none, textW);
	FormField q;
	q.kind = FormField::Picture;
	q.height = (int)std::ceil(92 + bodyH + (later.empty() ? 0 : 10 + laterH) + 8);
	q.paint = [=](ID2D1RenderTarget* rt, float width, float) {
		const D2D1_COLOR_F ink = prefs().dark ? D2D1::ColorF(0.89f, 0.9f, 0.93f) : D2D1::ColorF(0.1f, 0.11f, 0.13f);
		brand::icon(rt, 2, 4, 44, 0);
		brand::text(rt, title, 2, 61, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, ink);
		const float h = brand::text(rt, body, 2, 92, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, withAlpha(ink, 0.68f), width - 4);
		if (!later.empty()) brand::text(rt, later, 2, 92 + h + 10, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, withAlpha(ink, 0.5f), width - 4);
	};
	f.add(q);
	const int answer = f.run(isOpen(w) ? w->window() : nullptr);
	if (answer != IDOK) {
		if (firstTime) {
			prefs().startMenu = 2;
			prefs().save();
		}
		return false;
	}
	const bool ok = add();
	prefs().startMenu = 1;
	prefs().save();
	if (!ok)
		showMessage(isOpen(w) ? w->window() : nullptr, Tone::Warning, "CedarLogic couldn't add itself to the Start menu",
		            "Windows didn't let it change this account's settings. The installer from the download page does the same, "
		            "and more.");
	else if (isOpen(w))
		w->note("CedarLogic is in the Start menu now, and .cdl files open in it.");
	return ok;
}

// ---- When to ask -----------------------------------------------------------------------

bool g_now = false;   // CI's picture: ask at once
int g_calm = 0;       // looks in a row with nothing in the way

// A CedarLogic window in front, taking clicks (the welcome, What's New and
// dialogs turn it off), no tour, no drag or menu, and no typing or clicking
// for a moment.
bool calm(CircuitWindow* w) {
	if (w == nullptr || !IsWindowEnabled(w->window()) || IsIconic(w->window()) || splash::active() || welcome::tourWindow())
		return false;
	GUITHREADINFO gui = { sizeof gui };
	if (GetGUIThreadInfo(GetCurrentThreadId(), &gui) &&
	    (gui.hwndCapture || (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE))))
		return false;
	LASTINPUTINFO input = { sizeof input };
	return !GetLastInputInfo(&input) || GetTickCount() - input.dwTime >= 1500;
}

void CALLBACK askTimer(HWND, UINT, UINT_PTR id, DWORD) {
	CircuitWindow* front = nullptr;
	for (CircuitWindow* w : circuitWindows())
		if (w->window() == GetForegroundWindow()) front = w;
	if (g_now) {
		if (front == nullptr && !circuitWindows().empty()) front = circuitWindows().back();
	} else {
		// Answered meanwhile (Help in the menu), or installed after all.
		if (prefs().startMenu != 0 || installedAnywhere()) { KillTimer(nullptr, id); return; }
		g_calm = calm(front) ? g_calm + 1 : 0;
		if (g_calm < 2) return;
	}
	KillTimer(nullptr, id);
	if (front) guarded("asking about the Start menu", [&] { ask(front, true); });
}

}  // namespace

bool installed() {
	const std::wstring here = folderOf(exePath());
	return samePath(installDir(), here) || exists(here + L"\\unins000.exe");
}

void start(bool now) {
	// Added, then moved (a newer zip unzipped somewhere else, the old folder
	// gone): Start and .cdl files follow it here.
	if (!now && prefs().startMenu == 1 && !installedAnywhere()) {
		const std::wstring was = registeredExe();
		if (!was.empty() && !exists(was) && !samePath(was, exePath())) guarded("following a moved CedarLogic", [] { add(); });
	}
	g_now = now;
	g_calm = 0;
	if (now || (prefs().startMenu == 0 && !installedAnywhere())) SetTimer(nullptr, 0, now ? 500 : 1000, askTimer);
}

bool offered() { return !installedAnywhere(); }

std::string menuLabel() { return added() ? "Remove from &Start Menu" : "Add to &Start Menu…"; }

void menuCommand(CircuitWindow* w) {
	if (!added()) {
		ask(w, false);
		return;
	}
	remove();
	prefs().startMenu = 2;
	prefs().save();
	if (isOpen(w)) w->note("CedarLogic is out of the Start menu, and .cdl files no longer open in it.");
}

bool selfTest(std::string& report) {
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		report += strf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
		if (!ok) failures++;
	};
	// What Windows itself would open a .cdl with (Explorer asks the same).
	auto opener = [] {
		wchar_t buf[MAX_PATH * 4] = L"";
		DWORD n = (DWORD)(sizeof buf / sizeof buf[0]);
		return SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".cdl", L"open", buf, &n)) ? std::wstring(buf)
		                                                                                                  : std::wstring();
	};
	const std::wstring exe = exePath(), lnk = shortcutPath();
	const bool wasAdded = added();
	check(!installed(), "this copy isn't one the installer put here");
	check(add(), "added to Start, with .cdl files");
	check(added(), ".cdl files' open command is this copy");
	check(samePath(shortcutTarget(lnk), exe), "the Start menu's shortcut leads here: " + U(lnk));
	check(readString(kClasses + kProgId + L"\\DefaultIcon", nullptr) == exe + L",1", "the .cdl icon is the exe's second");
	check(samePath(opener(), exe), "Windows opens .cdl files with it: " + U(opener()));
	remove();
	check(!added() && !exists(registeredExe()), "removed: .cdl files' open command is gone");
	check(!exists(lnk), "removed: the Start menu's shortcut is gone");
	check(readString(kClasses + L".cdl", nullptr) != kProgId, "removed: .cdl isn't ours any more");
	if (wasAdded) add();
	report += failures ? strf("start menu test: %d failed\n", failures) : std::string("start menu test: all passed\n");
	return failures == 0;
}

}  // namespace integration
