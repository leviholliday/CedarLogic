// Settings, resources, colours, text, drawing and small helpers (see App.h).

#include "App.h"
#include <d2d1_1.h>
#include "Window.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

// ---- Text ------------------------------------------------------------------------

std::wstring W(const std::string& s) {
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w((size_t)std::max(n, 0), L'\0');
	if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

std::string U(const std::wstring& w) {
	if (w.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)std::max(n, 0), '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

std::string baseName(const std::string& path) {
	const size_t slash = path.find_last_of("\\/");
	std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
	if (s.size() > 4 && lowerCase(s.substr(s.size() - 4)) == ".cdl") s.resize(s.size() - 4);
	return s;
}

std::string dirName(const std::string& path) {
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string strf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list copy;
	va_copy(copy, ap);
	const int n = vsnprintf(nullptr, 0, fmt, copy);
	va_end(copy);
	std::string out;
	if (n > 0) {
		out.resize((size_t)n + 1);
		vsnprintf(&out[0], out.size(), fmt, ap);
		out.resize((size_t)n);
	}
	va_end(ap);
	return out;
}

std::string lowerCase(const std::string& s) {
	std::wstring w = W(s);
	if (!w.empty()) CharLowerBuffW(&w[0], (DWORD)w.size());
	return U(w);
}

// ---- Files -----------------------------------------------------------------------

bool fileExists(const std::string& path) {
	const DWORD a = GetFileAttributesW(W(path).c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string exeDir() {
	wchar_t buf[MAX_PATH * 4];
	const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return dirName(U(std::wstring(buf, n)));
}

static std::string fullPath(const std::string& p) {
	wchar_t buf[MAX_PATH * 4];
	const DWORD n = GetFullPathNameW(W(p).c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
	return n ? U(std::wstring(buf, n)) : p;
}

const std::string& resourcesDir() {
	static std::string dir = []() -> std::string {
		std::vector<std::string> candidates;
		wchar_t env[MAX_PATH * 4];
		const DWORD n = GetEnvironmentVariableW(L"CEDARLOGIC_RESOURCES", env, (DWORD)(sizeof env / sizeof env[0]));
		if (n > 0 && n < sizeof env / sizeof env[0]) candidates.push_back(U(std::wstring(env, n)));
		const std::string b = exeDir();
		candidates.push_back(b + "\\res");          // installed: res beside the app
		candidates.push_back(b);
		candidates.push_back(b + "\\..\\..\\res");  // windows/build in the source tree
		candidates.push_back(b + "\\..\\..\\..\\res");
		candidates.push_back("res");
		for (const std::string& c : candidates)
			if (fileExists(c + "\\cl_gatedefs.xml")) return fullPath(c);
		return std::string();
	}();
	return dir;
}

std::string settingsDir() {
	PWSTR p = nullptr;
	std::string dir;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) dir = U(p) + "\\CedarLogic";
	if (p) CoTaskMemFree(p);
	if (dir.empty()) dir = exeDir();
	CreateDirectoryW(W(dir).c_str(), nullptr);
	return dir;
}

// ---- Settings ------------------------------------------------------------------

Prefs& prefs() {
	static Prefs p;
	return p;
}

double Prefs::wireScale() const {
	static const double scales[] = { 0.7, 1.0, 1.6 };
	return scales[std::min(std::max(wireThickness, 0), 2)];
}

namespace {

std::string prefsPath() { return settingsDir() + "\\native.ini"; }

// key=value lines, UTF-8.
std::map<std::string, std::string> readIni(const std::string& path) {
	std::map<std::string, std::string> out;
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t eq = line.find('=');
		if (line.empty() || line[0] == '[' || line[0] == '#' || eq == std::string::npos) continue;
		out[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return out;
}

struct Reader {
	std::map<std::string, std::string> m;
	bool has(const char* k) const { return m.count(k) > 0; }
	int i(const char* k, int fallback, int lo, int hi) const {
		auto it = m.find(k);
		if (it == m.end()) return fallback;
		char* end = nullptr;
		const long v = strtol(it->second.c_str(), &end, 10);
		if (end == it->second.c_str()) return fallback;
		return (int)std::min<long>(std::max<long>(v, lo), hi);
	}
	bool b(const char* k, bool fallback) const {
		auto it = m.find(k);
		if (it == m.end()) return fallback;
		return it->second == "true" || it->second == "1";
	}
	double d(const char* k, double fallback, double lo, double hi) const {
		auto it = m.find(k);
		if (it == m.end()) return fallback;
		char* end = nullptr;
		const double v = strtod(it->second.c_str(), &end);
		if (end == it->second.c_str() || !std::isfinite(v)) return fallback;
		return std::min(std::max(v, lo), hi);
	}
	std::string s(const char* k, const std::string& fallback) const {
		auto it = m.find(k);
		return it == m.end() ? fallback : it->second;
	}
};

// Text of several lines, on one line of the file: new lines kept as \x1F.
std::string oneLine(const std::string& text) {
	std::string one;
	for (char c : text) if (c != '\r') one += c == '\n' ? '\x1F' : c;
	return one;
}

std::string manyLines(std::string text) {
	for (char& c : text) if (c == '\x1F') c = '\n';
	return text;
}

}  // namespace

void Prefs::load() {
	Reader r{ readIni(prefsPath()) };
	themeMode = r.i("themeMode", themeMode, 0, 3);
	dark = r.b("lastDark", dark);
	accent = r.i("accent", accent, 0, 6);
	// The icon's green became the default (and everyone's accent, once), as
	// on the Mac.
	if (!r.b("brandAccentSet", false)) accent = 6;
	showGrid = r.b("showGrid", showGrid);
	gridStyle = r.i("gridStyle", gridStyle, 0, 1);
	majorGrid = r.b("majorGrid", majorGrid);
	wireThickness = r.i("wireThickness", wireThickness, 0, 2);
	wireDots = r.b("wireDots", wireDots);
	wireDotSize = r.d("wireDotSize", wireDotSize, 0.05, 0.5);
	lowWire = r.i("lowWire", lowWire, 0, 3);
	mouseWheel = r.i("mouseWheel", mouseWheel, 0, 1);
	touchpadScroll = r.i("touchpadScroll", touchpadScroll, 0, 1);
	reverseWheel = r.b("reverseWheel", reverseWheel);
	reverseTouchpad = r.b("reverseTouchpad", reverseTouchpad);
	rightClickRotate = r.b("rightClickRotate", rightClickRotate);
	duplicateUsesClipboard = r.b("duplicateClipboard", duplicateUsesClipboard);
	showPalette = r.b("showPalette", showPalette);
	showStatus = r.b("statusBar", showStatus);
	// The drawn status bar, where notes appear, is on, as on the Mac (the
	// old one was off).
	if (!r.b("drawnStatusBar", false)) showStatus = true;
	showGateNames = r.b("showGateNames", showGateNames);
	wireValueTag = r.b("wireValueTag", wireValueTag);
	tidyMode = r.i("tidyMode", tidyMode, 0, 1);
	showCategoryKeys = r.b("showCategoryKeys", showCategoryKeys);
	gateSize = r.i("gateSize", gateSize, 36, 96);
	// Seamless for a new install; whoever had the app before keeps Classic,
	// the look they knew, until they choose.
	toolbarStyle = r.i("toolbarStyle", r.m.empty() ? toolbarStyle : 0, 0, 3);
	if (toolbarStyle == 1) toolbarStyle = 0;
	toolbarHidden = r.i("toolbarHidden", toolbarHidden, 0, 1 << 12);
	showTitle = r.b("showTitle", showTitle);
	showThemeToggle = r.b("showThemeToggle", showThemeToggle);
	openReplaces = r.b("openReplaces", openReplaces);
	newTemplate = r.s("newTemplate", newTemplate);
	shortcuts = r.s("shortcuts", shortcuts);
	checkUpdates = r.b("checkUpdates", checkUpdates);
	hasSeenWelcome = r.b("hasSeenWelcome", hasSeenWelcome);
	firstLaunchPlayed = r.b("firstLaunchPlayed", firstLaunchPlayed);
	windowWidth = r.i("windowWidth", windowWidth, 400, 20000);
	windowHeight = r.i("windowHeight", windowHeight, 300, 20000);
	windowMaximized = r.b("windowMaximized", windowMaximized);
	paletteWidth = r.i("sidePanelWidth", paletteWidth, 160, 800);
	lastFolder = r.s("lastFolder", lastFolder);
	lastCircuit = r.s("lastCircuit", lastCircuit);
	studentName = r.s("studentName", studentName);
	lastFormula = manyLines(r.s("lastFormula", lastFormula));
	buildShape = r.i("buildShape", buildShape, 0, 2);
	buildStyle = r.i("buildStyle", buildStyle, 0, 2);
	buildTwoInput = r.b("buildTwoInput", buildTwoInput);
	buildNewPage = r.b("buildNewPage", buildNewPage);
	truthTab = r.i("truthTab", truthTab, 0, 2);
	timingWhole = r.b("timingWhole", timingWhole);
	timingInColor = r.b("timingInColor", timingInColor);
	reportCircuit = r.b("reportCircuit", reportCircuit);
	reportTable = r.b("reportTable", reportTable);
	reportFormulas = r.b("reportFormulas", reportFormulas);
	reportTiming = r.b("reportTiming", reportTiming);
	reportColor = r.b("reportColor", reportColor);
	exportGrid = r.b("exportGrid", exportGrid);
	exportColor = r.b("exportColor", exportColor);
	exportInfo = r.b("exportInfo", exportInfo);
	// The name-and-result strip is in an exported picture to begin with, as
	// on the Mac and Linux (it was left out here, once).
	if (!r.b("exportStripOn", false)) exportInfo = true;
	exportWorks = r.b("exportWorks", exportWorks);
	exportScale = r.i("exportScale", exportScale, 2, 6);
	exportProblem = manyLines(r.s("exportProblem", exportProblem));
	feedbackTitle = r.s("feedbackTitle", feedbackTitle);
	feedbackDetails = manyLines(r.s("feedbackDetails", feedbackDetails));
	feedbackTags = r.s("feedbackTags", feedbackTags);
	feedbackEmail = r.s("feedbackEmail", feedbackEmail);
	feedbackPriority = r.i("feedbackPriority", feedbackPriority, 0, 3);
	feedbackContact = r.b("feedbackContact", feedbackContact);
	seenWhatsNew = r.s("seenWhatsNew", seenWhatsNew);
	confirmQuit = r.b("confirmQuit", confirmQuit);
	startMenu = r.i("startMenu", startMenu, 0, 2);
	recent.clear();
	for (int i = 0; i < 10; i++) {
		const std::string v = r.s(strf("recent%d", i).c_str(), "");
		if (!v.empty()) recent.push_back(v);
	}
	switch (themeMode) {
	case 1: dark = false; break;
	case 2: dark = true; break;
	case 3: break;   // as last time
	default: dark = systemPrefersDark(); break;
	}
}

void Prefs::save() const {
	std::ostringstream o;
	auto b = [](bool v) { return v ? "true" : "false"; };
	o << "[CedarLogic]\n";
	o << "themeMode=" << themeMode << "\n";
	o << "lastDark=" << b(dark) << "\n";
	o << "accent=" << accent << "\n";
	o << "brandAccentSet=1\n";
	o << "showGrid=" << b(showGrid) << "\n";
	o << "gridStyle=" << gridStyle << "\n";
	o << "majorGrid=" << b(majorGrid) << "\n";
	o << "wireThickness=" << wireThickness << "\n";
	o << "wireDots=" << b(wireDots) << "\n";
	o << "wireDotSize=" << wireDotSize << "\n";
	o << "lowWire=" << lowWire << "\n";
	o << "mouseWheel=" << mouseWheel << "\n";
	o << "touchpadScroll=" << touchpadScroll << "\n";
	o << "reverseWheel=" << b(reverseWheel) << "\n";
	o << "reverseTouchpad=" << b(reverseTouchpad) << "\n";
	o << "rightClickRotate=" << b(rightClickRotate) << "\n";
	o << "duplicateClipboard=" << b(duplicateUsesClipboard) << "\n";
	o << "showPalette=" << b(showPalette) << "\n";
	o << "statusBar=" << b(showStatus) << "\n";
	o << "drawnStatusBar=1\n";
	o << "showGateNames=" << b(showGateNames) << "\n";
	o << "wireValueTag=" << b(wireValueTag) << "\n";
	o << "tidyMode=" << tidyMode << "\n";
	o << "showCategoryKeys=" << b(showCategoryKeys) << "\n";
	o << "gateSize=" << gateSize << "\n";
	o << "toolbarStyle=" << toolbarStyle << "\n";
	o << "toolbarHidden=" << toolbarHidden << "\n";
	o << "showTitle=" << b(showTitle) << "\n";
	o << "showThemeToggle=" << b(showThemeToggle) << "\n";
	o << "openReplaces=" << b(openReplaces) << "\n";
	o << "newTemplate=" << newTemplate << "\n";
	o << "shortcuts=" << shortcuts << "\n";
	o << "checkUpdates=" << b(checkUpdates) << "\n";
	o << "hasSeenWelcome=" << b(hasSeenWelcome) << "\n";
	o << "firstLaunchPlayed=" << b(firstLaunchPlayed) << "\n";
	o << "windowWidth=" << windowWidth << "\n";
	o << "windowHeight=" << windowHeight << "\n";
	o << "windowMaximized=" << b(windowMaximized) << "\n";
	o << "sidePanelWidth=" << paletteWidth << "\n";
	o << "lastFolder=" << lastFolder << "\n";
	o << "lastCircuit=" << lastCircuit << "\n";
	o << "studentName=" << studentName << "\n";
	o << "lastFormula=" << oneLine(lastFormula) << "\n";
	o << "buildShape=" << buildShape << "\n";
	o << "buildStyle=" << buildStyle << "\n";
	o << "buildTwoInput=" << b(buildTwoInput) << "\n";
	o << "buildNewPage=" << b(buildNewPage) << "\n";
	o << "truthTab=" << truthTab << "\n";
	o << "timingWhole=" << b(timingWhole) << "\n";
	o << "timingInColor=" << b(timingInColor) << "\n";
	o << "reportCircuit=" << b(reportCircuit) << "\n";
	o << "reportTable=" << b(reportTable) << "\n";
	o << "reportFormulas=" << b(reportFormulas) << "\n";
	o << "reportTiming=" << b(reportTiming) << "\n";
	o << "reportColor=" << b(reportColor) << "\n";
	o << "exportGrid=" << b(exportGrid) << "\n";
	o << "exportColor=" << b(exportColor) << "\n";
	o << "exportInfo=" << b(exportInfo) << "\n";
	o << "exportStripOn=1\n";
	o << "exportWorks=" << b(exportWorks) << "\n";
	o << "exportScale=" << exportScale << "\n";
	o << "exportProblem=" << oneLine(exportProblem) << "\n";
	o << "feedbackTitle=" << feedbackTitle << "\n";
	o << "feedbackDetails=" << oneLine(feedbackDetails) << "\n";
	o << "feedbackTags=" << feedbackTags << "\n";
	o << "feedbackEmail=" << feedbackEmail << "\n";
	o << "feedbackPriority=" << feedbackPriority << "\n";
	o << "feedbackContact=" << b(feedbackContact) << "\n";
	o << "seenWhatsNew=" << seenWhatsNew << "\n";
	o << "confirmQuit=" << b(confirmQuit) << "\n";
	o << "startMenu=" << startMenu << "\n";
	for (size_t i = 0; i < recent.size() && i < 10; i++) o << "recent" << i << "=" << recent[i] << "\n";
	const std::string text = o.str();
	// A temporary beside it, then moved over: a crash mid-write can't leave
	// the settings half written.
	const std::wstring path = W(prefsPath()), tmp = path + L".tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(h);
	if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
	else DeleteFileW(tmp.c_str());
}

void Prefs::applyWireDots() const {
	cl_set_wire_dots(wireDots, wireDotSize);
	static const double colors[][3] = { { 0.62, 0.67, 0.76 }, { 0.47, 0.58, 0.84 }, { 0.86, 0.87, 0.90 } };
	if (lowWire >= 0 && lowWire < 3) cl_set_low_wire_color(true, colors[lowWire][0], colors[lowWire][1], colors[lowWire][2]);
	else cl_set_low_wire_color(false, 0, 0, 0);
}

void Prefs::noteRecent(const std::string& path) {
	if (path.empty()) return;
	recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
	recent.insert(recent.begin(), path);
	if (recent.size() > 10) recent.resize(10);
	lastFolder = dirName(path);
	save();
	// Windows' own list too (the taskbar's jump list, Recent files).
	SHAddToRecentDocs(SHARD_PATHW, W(path).c_str());
}

// ---- The theme -----------------------------------------------------------------

bool systemPrefersDark() {
	DWORD value = 1, size = sizeof value;
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
	                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
		return false;
	return value == 0;
}

// Popup menus in the app's light or dark. Windows has no documented way to
// ask for this; uxtheme's SetPreferredAppMode (ordinal 135, Windows 10 1903
// and later) is what Explorer, Notepad and Terminal use. Missing, the menus
// simply stay light.
static void setMenuTheme(bool dark) {
	static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (ux == nullptr) return;
	using SetMode = int(WINAPI*)(int);
	using Flush = void(WINAPI*)();
	auto set = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
	auto flush = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
	if (set) set(dark ? 2 : 3);   // force dark, force light
	if (flush) flush();
}

void darkenControl(HWND control, bool dark, const wchar_t* theme) {
	static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	using Allow = BOOL(WINAPI*)(HWND, BOOL);
	static Allow allow = ux ? reinterpret_cast<Allow>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(133)))) : nullptr;
	if (allow) allow(control, dark);
	const std::wstring name = dark ? std::wstring(L"DarkMode_") + theme : std::wstring(theme);
	SetWindowTheme(control, name.c_str(), nullptr);
	SendMessageW(control, WM_THEMECHANGED, 0, 0);
}

void applyTheme() {
	setMenuTheme(prefs().dark);
	for (CircuitWindow* w : circuitWindows()) w->themeChanged();
}

void setDarkTitleBar(HWND hwnd, bool dark) {
	const BOOL on = dark ? TRUE : FALSE;
	// DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on Windows 10 2004 and later, 19 before.
	if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof on))) DwmSetWindowAttribute(hwnd, 19, &on, sizeof on);
}

// ---- Colours ---------------------------------------------------------------------

RGBA Palette::canvas() const {
	if (simView) return { 0.030, 0.038, 0.050, 1 };
	return dark ? RGBA{ 0.075, 0.082, 0.098, 1 } : RGBA{ 1, 1, 1, 1 };
}

RGBA Palette::grid(double intensity) const {
	if (simView) return { 0.25, 0.80, 1.0, intensity * 0.55 };
	return dark ? RGBA{ 1, 1, 1, intensity } : RGBA{ 0, 0, intensity, intensity };
}

RGBA accentColor(bool dark) {
	RGBA c{ 0, 0, 0, 1 };
	cl_accent_color(prefs().accent, dark, &c.r, &c.g, &c.b);
	return c;
}

// ---- Drawing -----------------------------------------------------------------------

ID2D1Factory* d2dFactory() {
	static ID2D1Factory* f = [] {
		// Direct2D 1.1 where there is one (Windows 8 and later): its render
		// targets are device contexts too, with effects (blur, shadows) for
		// the launch screen. The plain factory otherwise.
		ID2D1Factory* made = nullptr;
		if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, (void**)&made)) || !made)
			D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&made);
		return made;
	}();
	return f;
}

IDWriteFactory* dwFactory() {
	static IDWriteFactory* f = [] {
		IUnknown* made = nullptr;
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &made);
		return (IDWriteFactory*)made;
	}();
	return f;
}

ID2D1HwndRenderTarget* WindowSurface::begin(HWND hwnd) {
	// The last frame never ended (drawing it threw, and guarded() carried
	// on): that target is stuck in it, so a new one.
	if (drawing) release();
	window = hwnd;
	RECT rc;
	GetClientRect(hwnd, &rc);
	const UINT32 w = (UINT32)std::max<LONG>(1, rc.right - rc.left), h = (UINT32)std::max<LONG>(1, rc.bottom - rc.top);
	if (rt == nullptr) {
		ID2D1Factory* f = d2dFactory();
		if (f == nullptr) return nullptr;
		// 96 dpi, so the target's own units are pixels; the transform below
		// makes them points.
		const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
		if (FAILED(f->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(w, h)), &rt)))
			return rt = nullptr;
		static unsigned long long count = 0;
		made = ++count;
	} else {
		const D2D1_SIZE_U now = rt->GetPixelSize();
		if (now.width != w || now.height != h) rt->Resize(D2D1::SizeU(w, h));
	}
	dpiScale = dpiOf(hwnd) / 96.0;
	rt->BeginDraw();
	drawing = true;
	rt->SetTransform(D2D1::Matrix3x2F::Scale((float)dpiScale, (float)dpiScale));
	return rt;
}

void WindowSurface::end() {
	if (rt == nullptr) return;
	drawing = false;
	// The frame didn't make it: the display took the target away (a driver
	// update, a remote session) or something in it failed. A new target, and
	// the frame again (a few times in a row at most).
	if (FAILED(rt->EndDraw())) {
		release();
		if (failures++ < 3) InvalidateRect(window, nullptr, FALSE);
	} else {
		failures = 0;
	}
}

void WindowSurface::release() {
	if (rt) rt->Release();
	rt = nullptr;
	drawing = false;
}

namespace {

std::wstring uiFaceName() {
	NONCLIENTMETRICSW m = {};
	m.cbSize = sizeof m;
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) return m.lfMessageFont.lfFaceName;
	return L"Segoe UI";
}

IDWriteTextFormat* textFormat(float size, bool bold, TextAlign align) {
	static std::map<std::tuple<int, bool, int>, IDWriteTextFormat*> cache;
	const auto key = std::make_tuple((int)std::lround(size * 10), bold, (int)align);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	IDWriteTextFormat* f = nullptr;
	if (IDWriteFactory* dw = dwFactory()) {
		static const std::wstring face = uiFaceName();
		dw->CreateTextFormat(face.c_str(), nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
		if (f) {
			f->SetTextAlignment(align == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
			                    : align == TextAlign::Trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING
			                                                   : DWRITE_TEXT_ALIGNMENT_LEADING);
			f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			// Too long for its box: end it with an ellipsis.
			IDWriteInlineObject* dots = nullptr;
			if (SUCCEEDED(dw->CreateEllipsisTrimmingSign(f, &dots))) {
				DWRITE_TRIMMING t = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
				f->SetTrimming(&t, dots);
				dots->Release();
			}
		}
	}
	cache[key] = f;
	return f;
}

}  // namespace

void drawText(ID2D1RenderTarget* rt, const std::string& text, const D2D1_RECT_F& box, float size,
              const D2D1_COLOR_F& color, TextAlign align, bool bold) {
	IDWriteTextFormat* f = textFormat(size, bold, align);
	if (f == nullptr || text.empty()) return;
	ID2D1SolidColorBrush* brush = nullptr;
	if (FAILED(rt->CreateSolidColorBrush(color, &brush))) return;
	const std::wstring w = W(text);
	rt->DrawText(w.c_str(), (UINT32)w.size(), f, box, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	brush->Release();
}

float textWidth(const std::string& text, float size, bool bold) {
	IDWriteTextFormat* f = textFormat(size, bold, TextAlign::Leading);
	IDWriteFactory* dw = dwFactory();
	if (f == nullptr || dw == nullptr || text.empty()) return 0;
	const std::wstring w = W(text);
	IDWriteTextLayout* layout = nullptr;
	if (FAILED(dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, 10000, 100, &layout))) return 0;
	DWRITE_TEXT_METRICS m = {};
	layout->GetMetrics(&m);
	layout->Release();
	return m.widthIncludingTrailingWhitespace;
}

// ---- Windows and controls ----------------------------------------------------------

HINSTANCE appInstance() { return GetModuleHandleW(nullptr); }

double nowSeconds() {
	static const double period = [] {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return 1.0 / (double)f.QuadPart;
	}();
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * period;
}

UINT dpiOf(HWND hwnd) {
	const UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
	return d ? d : 96;
}

HFONT uiFont(UINT dpi) {
	static std::map<UINT, HFONT> fonts;
	auto it = fonts.find(dpi);
	if (it != fonts.end()) return it->second;
	NONCLIENTMETRICSW m = {};
	m.cbSize = sizeof m;
	HFONT f = nullptr;
	if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, dpi)) f = CreateFontIndirectW(&m.lfMessageFont);
	if (f == nullptr) f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	fonts[dpi] = f;
	return f;
}

void setFontTree(HWND hwnd, HFONT font) {
	SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
	EnumChildWindows(hwnd, [](HWND child, LPARAM f) -> BOOL {
		SendMessageW(child, WM_SETFONT, (WPARAM)f, TRUE);
		return TRUE;
	}, (LPARAM)font);
}

std::string windowText(HWND hwnd) {
	const int n = GetWindowTextLengthW(hwnd);
	std::wstring w((size_t)n + 1, L'\0');
	GetWindowTextW(hwnd, &w[0], n + 1);
	w.resize((size_t)n);
	return U(w);
}

void setWindowText(HWND hwnd, const std::string& text) {
	if (windowText(hwnd) != text) SetWindowTextW(hwnd, W(text).c_str());
}

// ---- Messages ----------------------------------------------------------------------
// showMessage and askYesNo are the app's alert card (Alert.cpp).

void reportException(const char* where, const char* what) {
	OutputDebugStringW(W(strf("CedarLogic: %s failed: %s\n", where, what)).c_str());
	// Tell the user once a minute at most, in every window's status bar.
	static ULONGLONG lastTold = 0;
	const ULONGLONG now = GetTickCount64();
	if (lastTold && now - lastTold < 60000) return;
	lastTold = now;
	for (CircuitWindow* w : circuitWindows())
		w->note(strf("Something went wrong (%s). CedarLogic kept going; saving a copy is a good idea.", where));
}

void openExternally(HWND parent, const std::string& target) {
	const HINSTANCE r = ShellExecuteW(parent, L"open", W(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if ((INT_PTR)r <= 32) showMessage(parent, Tone::Warning, "Couldn't open it", target);
}

// ---- Clipboard -----------------------------------------------------------------------

bool setClipboardText(HWND owner, const std::string& text) {
	if (!OpenClipboard(owner)) return false;
	EmptyClipboard();
	std::wstring w = W(text);
	// Windows line ends, so Notepad shows the text as it is.
	std::wstring crlf;
	crlf.reserve(w.size() + w.size() / 16);
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) crlf += L'\r';
		crlf += w[i];
	}
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (crlf.size() + 1) * sizeof(wchar_t));
	bool ok = false;
	if (mem) {
		if (void* p = GlobalLock(mem)) {
			memcpy(p, crlf.c_str(), (crlf.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(mem);
			ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		}
		if (!ok) GlobalFree(mem);
	}
	CloseClipboard();
	return ok;
}

bool clipboardText(HWND owner, std::string& out) {
	out.clear();
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return false;
	bool ok = false;
	if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
			std::wstring w;
			for (; *p; p++) if (*p != L'\r') w += *p;
			out = U(w);
			ok = true;
			GlobalUnlock(h);
		}
	}
	CloseClipboard();
	return ok;
}

// ---- File choosers --------------------------------------------------------------------

namespace {

// "Name\0*.cdl;*.CDL\0...\0\0"
std::wstring filterText(const std::vector<FileFilter>& filters) {
	std::wstring s;
	for (const FileFilter& f : filters) {
		s += W(f.name);
		s += L'\0';
		s += W(f.pattern);
		s += L'\0';
	}
	s += L'\0';
	return s;
}

}  // namespace

std::vector<std::string> chooseOpenFiles(HWND parent, const std::string& title, const std::vector<FileFilter>& filters,
                                         bool multiple) {
	std::vector<wchar_t> buf(65536, L'\0');
	const std::wstring filter = filterText(filters), wtitle = W(title), folder = W(prefs().lastFolder);
	OPENFILENAMEW o = {};
	o.lStructSize = sizeof o;
	o.hwndOwner = parent;
	o.lpstrFilter = filter.c_str();
	o.lpstrFile = buf.data();
	o.nMaxFile = (DWORD)buf.size();
	o.lpstrTitle = wtitle.c_str();
	o.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
	o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (multiple ? OFN_ALLOWMULTISELECT : 0);
	std::vector<std::string> out;
	if (!GetOpenFileNameW(&o)) return out;
	// One file: its full path. Several: the folder, then each name.
	const wchar_t* p = buf.data();
	const std::wstring first = p;
	p += first.size() + 1;
	if (*p == 0) {
		out.push_back(U(first));
	} else {
		for (; *p; p += wcslen(p) + 1) out.push_back(U(first) + "\\" + U(p));
	}
	if (!out.empty()) prefs().lastFolder = dirName(out.front());
	return out;
}

std::string chooseSaveFile(HWND parent, const std::string& title, const std::string& suggested,
                           const std::vector<FileFilter>& filters, const char* ext) {
	std::vector<wchar_t> buf(32768, L'\0');
	std::wstring name = W(suggested);
	// Characters Windows won't have in a file name.
	for (wchar_t& c : name) if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
	wcsncpy(buf.data(), name.c_str(), buf.size() - 1);
	const std::wstring filter = filterText(filters), wtitle = W(title), folder = W(prefs().lastFolder);
	const std::wstring defExt = ext && *ext == '.' ? W(ext + 1) : std::wstring();
	OPENFILENAMEW o = {};
	o.lStructSize = sizeof o;
	o.hwndOwner = parent;
	o.lpstrFilter = filter.c_str();
	o.lpstrFile = buf.data();
	o.nMaxFile = (DWORD)buf.size();
	o.lpstrTitle = wtitle.c_str();
	o.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
	o.lpstrDefExt = defExt.empty() ? nullptr : defExt.c_str();
	o.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&o)) return std::string();
	const std::string out = U(buf.data());
	prefs().lastFolder = dirName(out);
	return out;
}
