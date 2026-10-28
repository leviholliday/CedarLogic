// Updates for the test build, the way the Linux app's AppImage updates:
//
//   1. ask GitHub for the commit the "windows-native-testing" tag points at
//   2. compare it with CL_GIT_COMMIT, baked in at build time
//   3. different -> fetch the release's SHA256SUMS and SHA256SUMS.sig and
//      check them (update-signing/UpdateSignature.h): signed with the preview
//      key built in here, for this tag and the tag's commit, a build newer
//      than this one (CL_BUILD_TIME), with this CPU's zip in it. Anything
//      less: no update is offered, and why goes to update.log in the
//      settings folder
//   4. verified -> ask; yes -> download that zip, check its size and SHA-256
//      against the manifest before anything else touches it, and unpack it
//      (with the tar that comes with Windows 10 and 11)
//   4. move the running CedarLogic.exe aside (Windows lets a running program
//      be renamed, not replaced), put the new one and its res folder in its
//      place, and offer to restart -- through the app's normal Quit, so
//      unsaved work is asked about first. The old exe is removed at the next
//      launch.
//
// The same for a copy unzipped anywhere and one the installer put in
// %LOCALAPPDATA%\Programs\CedarLogic: only the exe and res beside it change
// (the installer's uninstaller and its notes stay as they are; Settings >
// Apps learns the new version at the next launch, in Integration.cpp).
//
// The network calls run on a thread of their own; only the main thread
// touches windows.

#include "Updater.h"
#include "Alert.h"
#include "Window.h"

#include "UpdateSignature.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <atomic>
#include <ctime>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#ifndef CL_GIT_COMMIT
#define CL_GIT_COMMIT "unknown"
#endif
// The commit's committer time (Unix seconds): the build number the signed
// manifest's must be above. 0 when it isn't known (a build outside git).
#ifndef CL_BUILD_TIME
#define CL_BUILD_TIME 0
#endif

namespace updater {

namespace {

const char* kOwnerRepo = "leviholliday/CedarLogic";
const char* kTag = "windows-native-testing";

#if defined(_M_ARM64) || defined(__aarch64__)
const char* kArch = "ARM64";
#else
const char* kArch = "x64";
#endif

std::string exePath() {
	wchar_t buf[MAX_PATH * 4];
	const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return U(std::wstring(buf, n));
}

// A GET over HTTPS, following redirects (release downloads go through
// GitHub's storage). Into `out`, or straight into `file` when one is given.
bool httpGet(const std::string& url, std::string* out, const std::string& file, size_t limit) {
	const std::wstring wurl = W(url);
	URL_COMPONENTS u = {};
	u.dwStructSize = sizeof u;
	wchar_t host[256] = L"", path[2048] = L"";
	u.lpszHostName = host;
	u.dwHostNameLength = 255;
	u.lpszUrlPath = path;
	u.dwUrlPathLength = 2047;
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u)) return false;
	HINTERNET session = WinHttpOpen(L"CedarLogic", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
	                                WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
	bool ok = false;
	HINTERNET connect = WinHttpConnect(session, host, u.nPort, 0);
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
	                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                                 u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                            : nullptr;
	HANDLE f = INVALID_HANDLE_VALUE;
	if (request && WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
	    WinHttpReceiveResponse(request, nullptr)) {
		DWORD status = 0, size = sizeof status;
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		                    &status, &size, WINHTTP_NO_HEADER_INDEX);
		if (status == 200) {
			if (!file.empty())
				f = CreateFileW(W(file).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			ok = file.empty() || f != INVALID_HANDLE_VALUE;
			size_t total = 0;
			std::vector<char> chunk(64 * 1024);
			for (DWORD got = 0; ok;) {
				if (!WinHttpReadData(request, chunk.data(), (DWORD)chunk.size(), &got)) { ok = false; break; }
				if (got == 0) break;
				total += got;
				if (total > limit) { ok = false; break; }
				if (out) out->append(chunk.data(), got);
				DWORD wrote = 0;
				if (f != INVALID_HANDLE_VALUE && (!WriteFile(f, chunk.data(), got, &wrote, nullptr) || wrote != got)) ok = false;
			}
		}
	}
	if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
	if (!ok && !file.empty()) DeleteFileW(W(file).c_str());
	if (request) WinHttpCloseHandle(request);
	if (connect) WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok;
}

// The value of one "key":"value" string field in a JSON document, from
// `from` on (the Linux updater's: enough for GitHub's answers).
bool jsonString(const std::string& json, const std::string& key, std::string& out, size_t from = 0,
                size_t* endOut = nullptr, size_t* startOut = nullptr) {
	const std::string needle = "\"" + key + "\":\"";
	size_t at = json.find(needle, from);
	if (at == std::string::npos) return false;
	at += needle.size();
	if (startOut) *startOut = at;
	std::string raw;
	for (size_t i = at; i < json.size(); i++) {
		if (json[i] == '"') { out.swap(raw); if (endOut) *endOut = i + 1; return true; }
		if (json[i] == '\\' && i + 1 < json.size()) raw += json[++i];
		else raw += json[i];
	}
	return false;
}

// ---- Verifying ------------------------------------------------------------------

// Why a check found no update, or a download was refused: a line in
// update.log in the settings folder (the last 64 KB kept), and the debugger.
void logUpdate(const std::string& what) {
	time_t now = time(nullptr);
	char when[32] = "";
	struct tm t;
	if (gmtime_s(&t, &now) == 0) strftime(when, sizeof when, "%Y-%m-%d %H:%M:%SZ", &t);
	const std::string line = std::string(when) + " " + what + "\r\n";
	OutputDebugStringW(W("CedarLogic update: " + line).c_str());
	const std::wstring path = W(settingsDir() + "\\update.log");
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 64 * 1024))
		DeleteFileW(path.c_str());
	HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD wrote = 0;
	WriteFile(f, line.data(), (DWORD)line.size(), &wrote, nullptr);
	CloseHandle(f);
}

// SHA-256 with Windows' own BCrypt, over bytes handed to `feed`.
bool sha256With(const std::function<bool(BCRYPT_HASH_HANDLE)>& feed, std::string& hex) {
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	unsigned char digest[32];
	bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
	          BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 && feed(hash) &&
	          BCryptFinishHash(hash, digest, sizeof digest, 0) == 0;
	if (hash) BCryptDestroyHash(hash);
	if (alg) BCryptCloseAlgorithmProvider(alg, 0);
	if (ok) hex = updatesig::toHex(digest, sizeof digest);
	return ok;
}

std::string sha256Bytes(const std::string& bytes) {
	std::string hex;
	sha256With([&](BCRYPT_HASH_HANDLE h) {
		return bytes.empty() || BCryptHashData(h, (PUCHAR)bytes.data(), (ULONG)bytes.size(), 0) == 0;
	}, hex);
	return hex;
}

// A file's SHA-256 and size, read through once.
bool sha256File(const std::string& path, std::string& hex, long long& size) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	size = 0;
	const bool ok = sha256With([&](BCRYPT_HASH_HANDLE h) {
		std::vector<unsigned char> chunk(64 * 1024);
		DWORD got = 0;
		while (ReadFile(f, chunk.data(), (DWORD)chunk.size(), &got, nullptr)) {
			if (got == 0) return true;
			size += got;
			if (BCryptHashData(h, chunk.data(), got, 0) != 0) return false;
		}
		return false;
	}, hex);
	CloseHandle(f);
	return ok;
}

// A downloaded file against the signed manifest's entry for it.
bool verifyFile(const std::string& path, const updatesig::File& expected, std::string& why) {
	std::string hex;
	long long size = 0;
	if (!sha256File(path, hex, size)) {
		why = "couldn't read " + path;
		return false;
	}
	return updatesig::checkDownload(expected, size, hex, why);
}

// This CPU's zip among the manifest's names.
bool isOurs(const std::string& name) { return name == std::string("CedarLogic-Windows-native-") + kArch + ".zip"; }

std::string releaseFileUrl(const std::string& name) {
	return strf("https://github.com/%s/releases/download/%s/%s", kOwnerRepo, kTag, name.c_str());
}

struct Asset {
	std::string url;
	updatesig::File file;
};

// The release's manifest and signature through `get` (the release's
// download URLs, or a folder in the self-test), and the decision on them.
updatesig::Decision findVerified(const std::function<bool(const std::string& name, std::string& out)>& get, const unsigned char key[32],
                                 const std::string& releaseCommit, const std::string& runningCommit, long long runningBuild, Asset* out) {
	std::string manifest, signature;
	bool haveManifest = false, haveSignature = false;
	if (releaseCommit != runningCommit) {
		haveManifest = get(updatesig::kManifestName, manifest);
		haveSignature = haveManifest && get(updatesig::kSignatureName, signature);
	}
	const updatesig::Decision d = updatesig::decide(haveManifest, manifest, haveSignature, signature, key, kTag, releaseCommit, runningCommit,
	                                                runningBuild, isOurs);
	if (out && d.result == updatesig::Result::Offer) {
		out->url = releaseFileUrl(d.file.name);
		out->file = d.file;
	}
	return d;
}

bool fetchReleaseFile(const std::string& name, std::string& out) {
	if (!updatesig::safeName(name)) return false;
	out.clear();
	return httpGet(releaseFileUrl(name), &out, "", 64 * 1024) && !out.empty();
}

struct Check {
	std::thread worker;
	std::atomic<bool> done{ false };
	bool interactive = false;
	HWND parent = nullptr;
	std::string commit;   // "" when GitHub couldn't be asked
	updatesig::Decision decision;
	Asset asset;
};

std::unique_ptr<Check> g_check;
std::string g_offered;   // a background check offers each build once

void runCheck(Check* c) {
	std::string body;
	if (httpGet(strf("https://api.github.com/repos/%s/commits/%s", kOwnerRepo, kTag), &body, "", 4u << 20))
		jsonString(body, "sha", c->commit);   // the first "sha" is the commit's own
	if (!c->commit.empty()) {
		c->decision = findVerified(fetchReleaseFile, updatesig::kPublicKey, c->commit, CL_GIT_COMMIT, (long long)CL_BUILD_TIME, &c->asset);
		logUpdate(c->decision.why);
	}
	c->done.store(true);
}

// Run something on a thread while a small window says so, the app still
// drawing behind it. The window it's over is left as it was found: one a
// dialog had disabled stays disabled (enabled, it could be closed under
// that dialog, whose code is still running).
template <class F>
bool withProgress(HWND parent, const char* text, F&& work) {
	const UINT dpi = dpiOf(parent);
	HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"Updating CedarLogic", WS_POPUP | WS_CAPTION, 0, 0,
	                         scaled(360, dpi), scaled(110, dpi), parent, nullptr, appInstance(), nullptr);
	CreateWindowExW(0, L"STATIC", W(text).c_str(), WS_CHILD | WS_VISIBLE, scaled(16, dpi), scaled(14, dpi), scaled(320, dpi),
	                scaled(20, dpi), w, nullptr, appInstance(), nullptr);
	HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE, scaled(16, dpi), scaled(42, dpi),
	                           scaled(320, dpi), scaled(14, dpi), w, nullptr, appInstance(), nullptr);
	SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
	setFontTree(w, uiFont(dpi));
	setDarkTitleBar(w, prefs().dark);
	RECT pr = {}, wr = {};
	if (parent == nullptr || !GetWindowRect(parent, &pr)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &pr, 0);
	GetWindowRect(w, &wr);
	SetWindowPos(w, nullptr, (pr.left + pr.right - (wr.right - wr.left)) / 2, (pr.top + pr.bottom - (wr.bottom - wr.top)) / 2, 0, 0,
	             SWP_NOSIZE | SWP_NOZORDER);
	ShowWindow(w, SW_SHOW);
	const bool parentWasEnabled = parent && IsWindowEnabled(parent);
	if (parentWasEnabled) EnableWindow(parent, FALSE);
	std::atomic<bool> done{ false };
	bool ok = false;
	std::thread worker([&] { ok = work(); done.store(true); });
	while (!done.load()) {
		MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
		MSG m;
		while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
			if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); break; }
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
	}
	worker.join();
	if (parentWasEnabled) EnableWindow(parent, TRUE);
	DestroyWindow(w);
	if (parentWasEnabled) SetForegroundWindow(parent);
	return ok;
}

// Run a program (by its full path: never one found in the current folder)
// without a window, and wait for it. True when it finished with 0.
bool runHidden(const std::wstring& program, const std::wstring& arguments) {
	STARTUPINFOW si = { sizeof si };
	PROCESS_INFORMATION pi = {};
	std::wstring cmd = L"\"" + program + L"\" " + arguments;
	if (!CreateProcessW(program.c_str(), &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		return false;
	WaitForSingleObject(pi.hProcess, 120000);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return code == 0;
}

bool copyTree(const std::wstring& from, const std::wstring& to) {
	CreateDirectoryW(to.c_str(), nullptr);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;
	bool ok = true;
	do {
		const std::wstring name = fd.cFileName;
		if (name == L"." || name == L"..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = copyTree(from + L"\\" + name, to + L"\\" + name) && ok;
		else ok = CopyFileW((from + L"\\" + name).c_str(), (to + L"\\" + name).c_str(), FALSE) && ok;
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return ok;
}

void removeTree(const std::wstring& dir) {
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removeTree(dir + L"\\" + name);
			else DeleteFileW((dir + L"\\" + name).c_str());
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir.c_str());
}

// The unpacked folder that has CedarLogic.exe: the zip's CedarLogic folder,
// else the zip's top, else whichever folder a level down has it.
std::wstring unpacked(const std::wstring& work) {
	auto has = [](const std::wstring& dir) { return GetFileAttributesW((dir + L"\\CedarLogic.exe").c_str()) != INVALID_FILE_ATTRIBUTES; };
	if (has(work + L"\\CedarLogic")) return work + L"\\CedarLogic";
	if (has(work)) return work;
	std::wstring found = work + L"\\CedarLogic";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((work + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return found;
	do {
		const std::wstring name = fd.cFileName;
		if (name != L"." && name != L".." && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && has(work + L"\\" + name)) {
			found = work + L"\\" + name;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return found;
}

// The app in the middle of something: a dialog up over a window (its code
// is waiting on that dialog, so the window mustn't close or be asked over),
// the mouse held down, a menu open, a window being moved or sized. Any
// window's dialog counts, not only the one asked over: a question asked
// inside another window's dialog loop holds that loop up, and the dialog,
// finished meanwhile, gives its window back to be closed under its code.
// `window`, the one asked over, must take clicks; when it's a dialog itself
// (Settings' Check Now), the circuit window under it is that dialog's.
bool inTheMiddle(HWND window) {
	if (window && !IsWindowEnabled(window)) return true;
	const HWND under = window ? GetAncestor(window, GA_ROOTOWNER) : nullptr;
	for (CircuitWindow* w : circuitWindows())
		if (w->window() != under && !IsWindowEnabled(w->window())) return true;
	GUITHREADINFO gui = { sizeof gui };
	return GetGUIThreadInfo(GetCurrentThreadId(), &gui) &&
	       (gui.hwndCapture || (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE)));
}

// "Restart Now": once nothing is in the middle of something (quitting
// can't happen under a dialog, whose code is waiting on it), the windows
// close as Quit closes them and the new exe starts.
std::wstring g_restartExe;

void CALLBACK restartTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (inTheMiddle(nullptr)) return;   // once it's over
	KillTimer(nullptr, id);
	if (!quitApp()) return;
	STARTUPINFOW si = { sizeof si };
	PROCESS_INFORMATION pi = {};
	std::wstring cmd = L"\"" + g_restartExe + L"\"";
	if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

void install(HWND parent, const Asset& asset) {
	const std::string exe = exePath();
	const std::string dir = dirName(exe);
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	const std::wstring work = std::wstring(tmp) + L"CedarLogic-update-" + std::to_wstring(GetCurrentProcessId());
	const std::wstring zip = work + L".zip";
	removeTree(work);
	CreateDirectoryW(work.c_str(), nullptr);
	// Windows' own tar (Windows 10 1803 and later), from System32.
	wchar_t sys[MAX_PATH] = L"";
	const UINT sysLength = GetSystemDirectoryW(sys, MAX_PATH);
	const std::wstring tar = sysLength > 0 && sysLength < MAX_PATH ? std::wstring(sys) + L"\\tar.exe" : std::wstring();
	if (tar.empty() || GetFileAttributesW(tar.c_str()) == INVALID_FILE_ATTRIBUTES) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "This copy of Windows can't unpack the update",
		            "Updating needs Windows 10 version 1803 or later. Download the new build from the release page instead.");
		return;
	}
	// Nothing is unpacked, let alone run, before the zip is the one the
	// signed manifest names: its size and SHA-256.
	bool verified = false;
	std::string why;
	const bool got = withProgress(parent, "Downloading the update…", [&] {
		if (!httpGet(asset.url, nullptr, U(zip), 512u << 20)) return false;
		if (!verifyFile(U(zip), asset.file, why)) return false;
		verified = true;
		return runHidden(tar, L"-xf \"" + zip + L"\" -C \"" + work + L"\"");
	});
	DeleteFileW(zip.c_str());
	if (!verified && !why.empty()) {
		removeTree(work);
		logUpdate("the download didn't verify, not installing it: " + why);
		showMessage(parent, Tone::Warning, "The update didn't pass its check",
		            "The downloaded file isn't the one the signed list of this build names, so it wasn't installed. (" + why + ")");
		return;
	}
	const std::wstring fresh = unpacked(work);
	if (!got || GetFileAttributesW((fresh + L"\\CedarLogic.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be downloaded", "Check your connection and try again.");
		return;
	}
	// The running exe moves aside; the new one takes its place.
	const std::wstring wexe = W(exe), old = wexe + L".old";
	DeleteFileW(old.c_str());
	if (!MoveFileExW(wexe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update was downloaded but couldn't be put in place",
		            "Check that you can change the files in " + dir + ".");
		return;
	}
	if (!CopyFileW((fresh + L"\\CedarLogic.exe").c_str(), wexe.c_str(), FALSE)) {
		MoveFileExW(old.c_str(), wexe.c_str(), MOVEFILE_REPLACE_EXISTING);   // put it back
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be put in place", "Check that you can change the files in " + dir + ".");
		return;
	}
	// The res folder beside it (the gates, help and samples): copied whole
	// to res.new, then swapped in, so a file that can't be replaced (in use,
	// read-only, a full disk) leaves the old exe and res, not a mix.
	const std::wstring res = W(dir) + L"\\res", resNew = res + L".new", resOld = res + L".old";
	removeTree(resNew);
	removeTree(resOld);
	const bool hadRes = GetFileAttributesW(res.c_str()) != INVALID_FILE_ATTRIBUTES;
	const bool staged = copyTree(fresh + L"\\res", resNew);
	const bool movedOld = staged && (!hadRes || MoveFileExW(res.c_str(), resOld.c_str(), 0));
	if (!(movedOld && MoveFileExW(resNew.c_str(), res.c_str(), 0))) {
		if (movedOld && hadRes) MoveFileExW(resOld.c_str(), res.c_str(), 0);
		removeTree(resNew);
		MoveFileExW(old.c_str(), wexe.c_str(), MOVEFILE_REPLACE_EXISTING);   // the old exe back
		removeTree(work);
		showMessage(parent, Tone::Warning, "The update couldn't be put in place",
		            "Some of CedarLogic's files in " + dir + "\\res couldn't be replaced. Close anything using them and try again.");
		return;
	}
	removeTree(resOld);
	removeTree(work);
	if (askConfirm(parent, "The update is installed", "Restart CedarLogic now to use it? Your circuits are saved.", "Restart Now", "Later")) {
		// Asked from Settings' Check Now: Settings closes first (its loop
		// ends before the restart's turn comes).
		bool fromWindow = false;
		for (CircuitWindow* w : circuitWindows()) fromWindow = fromWindow || w->window() == parent;
		wchar_t cls[16] = L"";
		if (parent && IsWindow(parent) && !fromWindow && GetClassNameW(parent, cls, 16) && lstrcmpW(cls, L"#32770") == 0)
			PostMessageW(parent, WM_COMMAND, IDCANCEL, 0);
		g_restartExe = wexe;
		SetTimer(nullptr, 0, 100, restartTimer);
	}
}

// The window a check's answer goes over: the one Check Now was asked from
// while it's there, else a circuit window with no dialog up.
HWND answerParent(const Check* c) {
	if (c->parent && IsWindow(c->parent)) return c->parent;
	for (CircuitWindow* w : circuitWindows())
		if (IsWindowEnabled(w->window())) return w->window();
	return circuitWindows().empty() ? nullptr : circuitWindows().front()->window();
}

void finish(HWND parent) {
	Check* c = g_check.get();
	const bool interactive = c->interactive;
	const std::string commit = c->commit;
	const Asset asset = c->asset;
	const updatesig::Result result = c->decision.result;
	g_check->worker.join();
	g_check.reset();
	if (commit.empty()) {
		if (interactive) showMessage(parent, Tone::Warning, "Couldn't check for updates", "Check your connection and try again.");
		return;
	}
	if (result == updatesig::Result::UpToDate) {
		if (interactive) showMessage(parent, Tone::Info, "CedarLogic is up to date", "You have the newest version.");
		return;
	}
	// A new build that isn't signed yet, or one that doesn't verify
	// (update.log says which): never offered.
	if (result != updatesig::Result::Offer || asset.url.empty()) {
		if (interactive)
			showMessage(parent, Tone::Info, "No verified update yet",
			            "There's a newer test build, but it hasn't been signed yet, so CedarLogic won't install it. Try again later.");
		return;
	}
	if (!interactive && g_offered == commit) return;
	g_offered = commit;
	if (askConfirm(parent, "A new version of CedarLogic is ready",
	               strf("Build %s is out (you have %s). Download and install it now?", commit.substr(0, 7).c_str(),
	                    std::string(CL_GIT_COMMIT).substr(0, 7).c_str()),
	               "Install", "Not Now"))
		install(parent, asset);
}

void CALLBACK pollTimer(HWND, UINT, UINT_PTR id, DWORD) {
	if (!g_check) { KillTimer(nullptr, id); return; }
	if (!g_check->done.load()) return;
	// This timer is the thread's, so it fires inside a dialog's loop too
	// (gate settings, Settings, Export...). The answer waits till that's
	// over, and tries again 200 ms on: Settings' Check Now is answered over
	// Settings, inside its loop; any other dialog, in any window, waits.
	const HWND parent = answerParent(g_check.get());
	if (inTheMiddle(parent)) return;
	KillTimer(nullptr, id);
	guarded("checking for updates", [parent] { finish(parent); });
}

void begin(HWND parent, bool interactive) {
	// One at a time: Check for Updates while one runs makes that one say
	// how it went (finish() reads these on this thread; the worker doesn't).
	if (g_check) {
		if (interactive) {
			g_check->interactive = true;
			g_check->parent = parent;
		}
		return;
	}
	g_check.reset(new Check());
	g_check->interactive = interactive;
	g_check->parent = parent;
	Check* c = g_check.get();
	c->worker = std::thread([c] { runCheck(c); });
	SetTimer(nullptr, 0, 200, pollTimer);
}

// Unless Settings > General says not to (Check Now still asks).
void CALLBACK firstCheck(HWND, UINT, UINT_PTR id, DWORD) {
	KillTimer(nullptr, id);
	if (prefs().checkUpdates) begin(nullptr, false);
}

void CALLBACK laterCheck(HWND, UINT, UINT_PTR, DWORD) {
	if (prefs().checkUpdates) begin(nullptr, false);
}

// ---- Self-test -------------------------------------------------------------------

std::string joinPath(const std::string& dir, const std::string& name) { return dir + "\\" + name; }

bool readWhole(const std::string& path, std::string& bytes) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	bytes.clear();
	char chunk[65536];
	DWORD got = 0;
	bool ok = true;
	while ((ok = ReadFile(f, chunk, sizeof chunk, &got, nullptr) != 0) && got > 0) bytes.append(chunk, got);
	CloseHandle(f);
	return ok;
}

bool writeWhole(const std::string& path, const std::string& bytes) {
	HANDLE f = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = bytes.empty() || (WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size());
	CloseHandle(f);
	return ok;
}

updatesig::SelfTestHooks selfTestHooks() {
	updatesig::SelfTestHooks h;
	h.tag = kTag;
	h.assetName = std::string("CedarLogic-Windows-native-") + kArch + ".zip";
	h.makeDir = [](const std::string& d) { return CreateDirectoryW(W(d).c_str(), nullptr) != 0; };
	h.writeFile = writeWhole;
	h.sha256Hex = sha256Bytes;
	h.join = joinPath;
	h.find = [](const std::string& dir, const unsigned char key[32], const std::string& releaseCommit, const std::string& runningCommit,
	            long long runningBuild) {
		auto fromDir = [&dir](const std::string& name, std::string& out) { return readWhole(joinPath(dir, name), out); };
		return findVerified(fromDir, key, releaseCommit, runningCommit, runningBuild, nullptr);
	};
	h.verifyFile = verifyFile;
	return h;
}

}  // namespace

bool runSelfTest(std::string& report, const std::string& release, const std::string& key) {
	bool ok;
	if (!release.empty()) {
		report += "Signed folder " + release + "\n";
		ok = updatesig::checkSignedFolder(release, key, selfTestHooks(), readWhole, report);
	} else {
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		const std::wstring dir = std::wstring(tmp) + L"CedarLogic-update-test-" + std::to_wstring(GetCurrentProcessId());
		removeTree(dir);
		CreateDirectoryW(dir.c_str(), nullptr);
		report += "Zip updates\n";
		ok = updatesig::selfTest(U(dir), selfTestHooks(), report);
		removeTree(dir);
	}
	report += strf("update self-test (this build: %s, build %lld): %s\n", CL_GIT_COMMIT, (long long)CL_BUILD_TIME,
	               ok ? "all passed" : "FAILED");
	return ok;
}

void start() {
	// The exe a finished update moved aside, and the res folder it replaced.
	DeleteFileW((W(exePath()) + L".old").c_str());
	removeTree(W(dirName(exePath())) + L"\\res.old");
	// Builds made outside CI (no commit known) don't update themselves.
	if (std::string(CL_GIT_COMMIT) == "unknown") return;
	SetTimer(nullptr, 0, 8000, firstCheck);
	SetTimer(nullptr, 0, 4 * 60 * 60 * 1000, laterCheck);   // and every few hours
}

void checkNow(HWND parent) { begin(parent, true); }

void shutdown() {
	if (!g_check) return;
	// A check still on its way can't be waited for (a slow network takes a
	// minute), and a std::thread left joinable when the statics go aborts
	// the process. Let it run until the process ends, and keep what it
	// writes to alive till then.
	g_check->worker.detach();
	(void)g_check.release();
}

}  // namespace updater
